// stcpp/infra/wal/wal_writer.hpp — WalWriter<R> 模板 + concept WalRecord
//
// 落:
//   laowang-wal-framework-v0.2.md §3.3 (P9 path prefix 硬校验) + §4 PIT + §8 性能
//   laowang-wal-framework-cpp-interface-v1.md §6 concept + §7 config + §8 writer
//
// 红线:
//   R-11  Open() path_prefix.starts_with(PathRootOf(kind)) 不命中 → std::abort (不抛, 防绕过)
//   R-20  Append() 入口必调 pit::AssertChain (内联 100ns)
//   预算  同步 Append p99 ≤ 6us (PIT 100ns + seq 50ns + frame 2.5us + ring 500ns + caller 序列化)
//   背压  ring 满 → WalError::Backpressure → caller REJECT(AUDIT_WAL_BACKPRESSURE) (老韩 #18)
//
// W3 skeleton: ring / bg fsync / fd 全部 stub. W4 接 rigtorp SPSC (@小石) + 真 fsync.
//
// 不耻下问: SPSC ring 类型 @小石, BLAKE3 不进 framework @老孙, RM 回调 @老韩

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/infra/wal/wal_error.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/infra/wal/wal_record_header.hpp"

namespace stcpp::infra::wal {

// ---------- concept WalRecord (cpp-interface §6) -------------------------

template <typename T>
concept WalRecord = requires(const T& r, std::span<std::byte> out) {
    { r.event_ts_ns() } -> std::same_as<std::int64_t>;
    { r.data_source_ts_ns() } -> std::same_as<std::int64_t>;
    { r.ingestion_ts_ns() } -> std::same_as<std::int64_t>;
    { r.as_of_ts_ns() } -> std::same_as<std::int64_t>;
    { r.audit_id() } -> std::convertible_to<std::array<std::uint8_t, 16>>;
    { r.serialize_into(out) } -> std::convertible_to<std::size_t>;
    { T::max_serialized_size() } -> std::convertible_to<std::size_t>;
};

template <WalRecord R>
inline void FillHeaderFromRecord(const R& r, WalKind kind, WalRecordHeader& h) noexcept {
    h.magic = kMagicV2;
    h.ver = kHeaderVersionV2;
    h.wal_kind = static_cast<std::uint8_t>(kind);
    h.event_ts_ns = r.event_ts_ns();
    h.data_source_ts_ns = r.data_source_ts_ns();
    h.ingestion_ts_ns = r.ingestion_ts_ns();
    h.as_of_ts_ns = r.as_of_ts_ns();
    h.audit_id = r.audit_id();
}

// ---------- WalConfig (cpp-interface §7) ---------------------------------

enum class FsyncMode : std::uint8_t {
    GroupCommit = 0,
    PerRecord = 1,  // position only, RPO=0
};

struct WalConfig {
    WalKind kind;
    std::string path_prefix;            // 必须 starts_with(PathRootOf(kind))
    std::size_t ring_capacity = 16384;  // 2 的幂
    std::size_t segment_max_bytes = 64ULL << 20;
    std::chrono::seconds rotation_period{3600};
    FsyncMode fsync_mode = FsyncMode::GroupCommit;
    std::uint16_t batch_size = 64;
    std::chrono::microseconds batch_timeout{1000};
    int bg_cpu_core = 7;                   // shadow = 6 (Q-PE1)
    bool reset_on_replay_failure = false;  // ShadowAudit = true (Q-PE2)
};

// ---------- WalWriter<R> (cpp-interface §8) ------------------------------
//
// W3 skeleton 公开 API. 私有实现 (ring / bg thread / fd) W4 接.

template <WalRecord R>
class WalWriter {
public:
    // Open(): 构造期 P9 path prefix 硬校验 — 不命中 → std::abort (不抛, R-11 防绕过).
    // 启动 SPSC ring + bg fsync 线程 (pin cfg.bg_cpu_core).
    // 返回错误仅限非 R-11 类 (例如 path_prefix 已通过白名单但 fd 打不开 → Io).
    static WalResult<std::unique_ptr<WalWriter>> Open(const WalConfig& cfg);

    // 同步 Append. p99 ≤ 6us.
    // 流程: PIT assert → next_seq → build_frame(CRC32C) → ring.try_push.
    // PerRecord 模式等 bg fsync 完成才返回 (position only).
    // ring 满 → Backpressure (caller REJECT AUDIT_WAL_BACKPRESSURE 老韩 #18).
    [[nodiscard]] WalResult<std::uint64_t> Append(const R& record) noexcept;

    // 等到指定 seq 已落盘 (group commit 显式 flush, position PerRecord 用不上).
    [[nodiscard]] WalResult<void> FlushUntil(std::uint64_t seq, std::chrono::milliseconds timeout) noexcept;

    [[nodiscard]] std::uint64_t HighWatermark() const noexcept {
        return high_watermark_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool IsFailed() const noexcept { return failed_.load(std::memory_order_acquire); }
    [[nodiscard]] WalKind Kind() const noexcept { return cfg_.kind; }

    ~WalWriter();

    WalWriter(const WalWriter&) = delete;
    WalWriter& operator=(const WalWriter&) = delete;
    WalWriter(WalWriter&&) = delete;
    WalWriter& operator=(WalWriter&&) = delete;

private:
    WalWriter() = default;

    // W4 接 rigtorp::SPSCQueue<Frame> (@小石), std::jthread bg, RAII fd_.
    // 当前 skeleton 仅保留计数器, 让 PIT / path check / API 闭环可测.
    WalConfig cfg_;
    std::atomic<std::uint64_t> next_seq_{0};
    std::atomic<std::uint64_t> high_watermark_{0};
    std::atomic<bool> failed_{false};
};

// ---------- 4 类 record forward-declare (W3 仅声明, 实现在 src/.../wal_writer.cpp) ----------
//
// 老唐 audit envelope (RiskAudit / PaperAudit / ShadowAudit) + 老周 position record.
// 真 struct 在各自 owner 仓位下定义, framework 只通过 WalRecord concept 约束.

struct RiskAuditRecord;    // owner: 老韩 + 老唐 — laotang v1.1 audit envelope
struct PositionRecord;     // owner: 老周 + 老孙 — laozhou v0.5 §X position WAL
struct PaperAuditRecord;   // owner: 小蒋
struct ShadowAuditRecord;  // owner: 小蒋 / 小邓

}  // namespace stcpp::infra::wal
