# WAL Framework C++ Interface Skeleton v1

- Owner: 老王 / 2026-05-28 / Sprint-2 W2
- 配套: `docs/RESEARCH/laowang-wal-framework-v0.2.md`
- 风格: header-only 草图. 不写实现.
- 不耻下问: SPSC @小石, BLAKE3 @老孙, RM 接入 @老韩, ML shadow @小邓

## 1. 头文件布局

```
infra/wal/{wal_kind, wal_record_header, wal_error, wal_pit,
          wal_record_concept, wal_config, wal_writer, wal_replayer,
          safe_mode_flag}.h
```

## 2. WalKind + 白名单

```cpp
namespace stcpp::infra::wal {
enum class WalKind : uint8_t { RiskAudit=0, Position=1, PaperAudit=2, ShadowAudit=3 };
inline constexpr std::array<std::pair<WalKind, std::string_view>, 4> kPathRoots{{
  {WalKind::RiskAudit,   "/var/lib/stcpp/audit/"},
  {WalKind::Position,    "/var/lib/stcpp/exec/"},
  {WalKind::PaperAudit,  "/var/lib/stcpp/paper/"},
  {WalKind::ShadowAudit, "/var/lib/stcpp/shadow/"},
}};
constexpr std::string_view PathRootOf(WalKind k) noexcept { return kPathRoots[size_t(k)].second; }
}
```

## 3. WalRecordHeader (64B POD)

```cpp
#pragma pack(push, 1)
struct WalRecordHeader {                          // exactly 64 B
  std::array<char, 4>     magic;                  // "WAL2"
  uint8_t                 ver;                    // = 2
  uint8_t                 wal_kind;
  uint16_t                len_payload;            // ≤ 64 KiB
  uint64_t                seq;                    // framework 管, monotonic per-WAL
  int64_t                 event_ts_ns;            // R-20
  int64_t                 data_source_ts_ns;      // R-20
  int64_t                 ingestion_ts_ns;        // R-20
  int64_t                 as_of_ts_ns;            // R-20
  std::array<uint8_t, 16> audit_id;               // ULID, caller 填
};
#pragma pack(pop)
static_assert(sizeof(WalRecordHeader) == 64);
static_assert(std::is_trivially_copyable_v<WalRecordHeader>);
inline constexpr std::array<char, 4> kMagicV2 = {'W','A','L','2'};
```

## 4. WalError

```cpp
enum class WalError : uint8_t {
  Ok=0, PitViolation, Backpressure, FsyncFailed, PathPrefixMismatch /*abort, 不返回*/,
  VersionMismatch, CrcMismatch, SeqGap, UlidNonMonotonic, MidCorruption,
  TailTruncated /*warn*/, DiskFull, Io
};
template <typename T> using WalResult = tl::expected<T, WalError>;
constexpr std::string_view ToString(WalError) noexcept;
```

## 5. PIT assert (R-20 §7, p99 < 100 ns 内联无分支)

```cpp
namespace pit {
enum class PitViolation : uint8_t { Ok=0, EventTsZero, DsBeforeEvent,
  IngestionBeforeDs, AsOfBeforeIngestion, AsOfInFuture };

[[nodiscard]] inline bool AssertChain(const WalRecordHeader& h) noexcept {
  timespec ts; ::clock_gettime(CLOCK_REALTIME, &ts);
  const int64_t now = int64_t(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
  return (h.event_ts_ns       > 0)
      && (h.data_source_ts_ns >= h.event_ts_ns)
      && (h.ingestion_ts_ns   >= h.data_source_ts_ns)
      && (h.as_of_ts_ns       >= h.ingestion_ts_ns)
      && (h.as_of_ts_ns       <= now);
}
PitViolation DiagnoseViolation(const WalRecordHeader&) noexcept;  // debug only
}
```

## 6. concept WalRecord

```cpp
template <typename T>
concept WalRecord = requires(const T& r, std::span<std::byte> out) {
  { r.event_ts_ns()         } -> std::same_as<int64_t>;
  { r.data_source_ts_ns()   } -> std::same_as<int64_t>;
  { r.ingestion_ts_ns()     } -> std::same_as<int64_t>;
  { r.as_of_ts_ns()         } -> std::same_as<int64_t>;
  { r.audit_id()            } -> std::convertible_to<std::array<uint8_t, 16>>;
  { r.serialize_into(out)   } -> std::convertible_to<size_t>;
  { T::max_serialized_size()} -> std::convertible_to<size_t>;
};

template <WalRecord R>
inline void FillHeaderFromRecord(const R& r, WalRecordHeader& h) noexcept {
  h.event_ts_ns=r.event_ts_ns(); h.data_source_ts_ns=r.data_source_ts_ns();
  h.ingestion_ts_ns=r.ingestion_ts_ns(); h.as_of_ts_ns=r.as_of_ts_ns();
  h.audit_id=r.audit_id();
}
```

## 7. WalConfig

```cpp
enum class FsyncMode : uint8_t { GroupCommit, PerRecord /*position only, RPO=0*/ };

struct WalConfig {
  WalKind     kind;
  std::string path_prefix;                       // 必须 starts_with(PathRootOf(kind))
  size_t      ring_capacity        = 16384;      // 2 的幂
  size_t      segment_max_bytes    = 64ULL << 20;
  std::chrono::seconds      rotation_period{3600};
  FsyncMode   fsync_mode           = FsyncMode::GroupCommit;
  uint16_t    batch_size           = 64;
  std::chrono::microseconds batch_timeout{1000};
  int         bg_cpu_core          = 7;          // shadow = 6 (Q-PE1)
  bool        reset_on_replay_failure = false;   // ShadowAudit = true (Q-PE2)
};
```

## 8. WalWriter

```cpp
template <WalRecord R>
class WalWriter {
 public:
  // Open() P9 校验 path_prefix → fail = std::abort. SPSC + bg fsync 启动.
  static WalResult<std::unique_ptr<WalWriter>> Open(const WalConfig& cfg);

  // 同步, p99 ≤ 6 us. 内部: PIT assert → next_seq → build_frame(CRC) → ring.try_push.
  // PerRecord 模式等 fsync 完成才返回.
  [[nodiscard]] WalResult<uint64_t> Append(const R& record) noexcept;

  WalResult<void> FlushUntil(uint64_t seq, std::chrono::milliseconds timeout) noexcept;
  uint64_t HighWatermark() const noexcept;
  bool     IsFailed() const noexcept;
  WalKind  Kind() const noexcept;
  ~WalWriter();                                  // drain + final fsync + close

  WalWriter(const WalWriter&) = delete;
  WalWriter& operator=(const WalWriter&) = delete;
 private:
  WalWriter() = default;
  // === 私有 (W3-W4 实现) ===
  // rigtorp::SPSCQueue<Frame>  ring_;       // @小石
  // std::jthread               bg_thread_;  // pin cfg_.bg_cpu_core
  // std::atomic<uint64_t>      next_seq_{0}, high_watermark_{0};
  // std::atomic<bool>          failed_{false};
  // int                        fd_active_segment_{-1};
  // WalConfig                  cfg_;
};
```

调用方 (老韩 RM v0.3):

```cpp
auto seq_or = risk_audit_wal_->Append(audit_envelope);
if (!seq_or) switch (seq_or.error()) {
  case WalError::PitViolation: return Reject(RejectCode::INVALID_INTENT);
  case WalError::Backpressure: return Reject(RejectCode::WAL_BACKPRESSURE);
  default:                     return Reject(RejectCode::INTERNAL_ERROR);
}
```

## 9. WalReplayer

```cpp
struct ReplayOptions {
  bool   allow_tail_truncation = true;
  size_t tail_truncation_bytes = 1ULL << 20;
  bool   reset_on_corruption   = false;          // ShadowAudit = true (Q-PE2)
  bool   check_ulid_monotonic  = true;           // shadow 重置时关 (OQ-13)
};

struct ReplaySummary {
  uint64_t last_good_seq=0, records_replayed=0, tail_truncated_bytes=0;
  bool     shadow_reset_triggered=false;
};

using ReplayVisitor = std::function<
    bool(const WalRecordHeader&, std::span<const std::byte> payload)>;

class WalReplayer {
 public:
  static WalResult<std::unique_ptr<WalReplayer>> Open(WalKind, const std::string& path_prefix);

  // 失败语义按 kind 分支 (v0.2 §5):
  //   RiskAudit / Position / PaperAudit: mid-corruption → exit SAFE_MODE_LOCKED
  //   ShadowAudit: mid-corruption → quarantine + reset (Q-PE2)
  WalResult<ReplaySummary> Replay(ReplayVisitor v, ReplayOptions opts = {}) noexcept;
  ~WalReplayer();
 private:
  WalReplayer() = default;
};
```

## 10. SafeModeFlag

```cpp
class SafeModeFlag {
 public:
  static SafeModeFlag& Instance() noexcept;
  [[nodiscard]] bool IsSet() const noexcept { return flag_.load(std::memory_order_acquire); }
  void Set(const char* reason) noexcept;                  // framework 内部
  bool TryUnlock(const char* operator_id) noexcept;       // CLI stcpp-ctl unlock
 private:
  std::atomic<bool> flag_{true};                          // 默认 true (W-3)
};
```

## 11. 不耻下问 (W3 内回执)

| # | 问 | 找 |
|---|---|---|
| WQ-1 | rigtorp SPSC ARM 实测 p99? PerRecord 能回压 caller? | @小石 |
| WQ-2 | BLAKE3 是否进 framework? 倾向不进, payload owner 算 | @老孙 |
| WQ-3 | RM v0.3 reject 加 `WAL_BACKPRESSURE`? `INVALID_INTENT` 已含 | @老韩 |
| WQ-4 | ML shadow 一条 record 多大? ring 16384 是否够 | @小邓 |
| WQ-5 | header v2 64B fuzzer 时长? | @小宋 |

## 12. 完成清单 (W2)

- [x] v0.2 设计 + cpp skeleton 出
- [ ] 4 签 (老韩 / 老唐 / 小蒋 / 小邓) + 老郭 ADR review
- [ ] W3 PIT + header v2 实现 + 单测; shadow core 6 pin 实测
- [ ] W4 shadow 自重置 replay + CI lint tool

**END.**
