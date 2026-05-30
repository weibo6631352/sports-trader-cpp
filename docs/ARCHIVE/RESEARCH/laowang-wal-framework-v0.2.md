# 自研 WAL Framework v0.2 (Sprint-2 W2)

- Owner: 老王
- Date: 2026-05-28
- 验收人: 老韩 (RM v0.3) + 老唐 (audit schema v1) + 小蒋 (paper / shadow) + 小邓 (ML shadow) + 老郭 (ADR)
- 关联:
  - v0.1: `docs/RESEARCH/laowang-wal-framework-v0.1.md`
  - 老唐: `docs/RESEARCH/laotang-audit-schema-v1.md` (BLAKE3 链式 hash, payload 内)
  - 老韩: `docs/RESEARCH/laohan-riskmanager-design-v0.3.md` (21 reject enum)
  - GM R-20: `docs/ADR/2026-05-28-gm-redline-data-source-timestamping.md` (4 时间戳)
  - 小蒋: `docs/RESEARCH/xiaojiang-paper-engine-skeleton-v1.md` §5
  - 小邓: `docs/RESEARCH/xiaodeng-ml-roadmap-v2.md` §6
  - 配套 cpp: `docs/RESEARCH/laowang-wal-framework-cpp-interface-v1.md`
- 状态: v0.2 待会签

---

## 0. v0.1 → v0.2 变更摘要

| # | 变更 | 来源 |
|---|---|---|
| C1 | **追加第 4 类 `shadow_audit` WAL** (ML/random shadow, owner = 小蒋, 与 paper_audit 物理隔离) | 小蒋 §5 + 小邓 ML-R1 |
| C2 | **R-20 4 时间戳全程携带** (event_ts / data_source_ts / ingestion_ts / as_of_ts, int64 epoch_ns UTC) 写入 framework header | GM R-20 |
| C3 | **WalWriter 模板化** `WalWriter<R>`, concept `WalRecord<T>` 强制暴露 4 ts + audit_id + serialize | 老唐 + R-20 |
| C4 | **PIT assert 接口** `pit::AssertChain()` Append 同步入口必调, fail → REJECT(INVALID_INTENT) | R-20 §7 + 老韩 v0.3 |
| C5 | **物理隔离硬校验** Open() 期 path_prefix 必须命中 4 白名单根目录之一, 否则 abort | R-11 + R-20 |
| C6 | **CI lint TOML caller 配置** 工具 `stcpp-wal-config-lint` | 老练 + 老郭 |
| C7 | Q-PE1 回执: shadow bg pin core 6; Q-PE2 回执: shadow replay 失败自重置不拒启动 | 小蒋 |
| C8 | header v1 16B → **v2 64B** (含 4 ts + audit_id), MAGIC 改 "WAL2", 不兼容 v1 | C2 副产 |
| C9 | cpp skeleton 拆独立文档 (`-cpp-interface-v1.md`) | 派单要求 3 |

v0.1 §1-§14 (设计原则 / 三 WAL 物理矩阵 / 同步异步路径 / replay 协议 / S3 archiver / TOML / 单测 / chaos / 落地计划) **不变**, 本文仅写增量.

---

## 1. 新增红线 + 原则

| # | (v0.2 新) | 出处 |
|---|---|---|
| P7 | 4 时间戳全程携带; 任一缺失 = framework reject + audit P1 | R-20 |
| P8 | PIT 不等式同步校验, fail = REJECT(INVALID_INTENT), 不写入 ring | R-20 §7 + 老韩 v0.3 |
| P9 | 物理隔离硬校验; Open() path_prefix 不命中白名单 → std::abort | R-11 + 小蒋 |
| R10 | 4 类独立 WAL: risk_audit / position / paper_audit / shadow_audit | 小蒋 §5 + 小邓 |
| R11 | shadow_audit 与 paper_audit 物理隔离 fd / 目录 / systemd group | R-11 |
| R12 | PIT 违例不阻塞热路径 emit risk_audit RECON_DRIFT; 连续 5min > 100 → SAFE_MODE | R-20 + 老韩 §13 |

---

## 2. Record 格式 (header v1 16B → v2 64B)

### 2.1 Header layout (v2 = 64B)

```
Offset Size Field
 0      4   MAGIC "WAL2" (v1 = "WAL1", 不兼容)
 4      1   VER = 2
 5      1   WAL_KIND (0=risk_audit, 1=position, 2=paper_audit, 3=shadow_audit)
 6      2   LEN_PAYLOAD (u16 LE)
 8      8   SEQ (u64 LE, framework 管, monotonic per-WAL)
16      8   event_ts_ns        (R-20)
24      8   data_source_ts_ns  (R-20)
32      8   ingestion_ts_ns    (R-20)
40      8   as_of_ts_ns        (R-20)
48     16   audit_id (ULID, caller 填, framework 校验单调)
64      N   PAYLOAD (老唐 schema, framework 不解析)
64+N    4   CRC32C (over MAGIC..PAYLOAD)
```

- header +48B / record. shadow_audit 1 KiB record header 占 6%, 可接受.
- 4 ts 放 header: framework / index sqlite 不解析 payload 也能做 PIT assert 与监管 export (老唐 §5.1 直读 header).
- audit_id 放 header: framework replay 时校验 ULID monotonic (老唐 §2.1 不变量) 不需要解析 payload.
- BLAKE3 `prev_hash` / `payload_hash` / `current_hash` 仍**在 payload 内** (老唐 ownership, 老孙实现), framework **不算 hash**, 仅算 CRC32C.

### 2.2 4 时间戳 (R-20 §2.2 来源 + 退化)

| 字段 | 来源 | framework 校验 |
|---|---|---|
| event_ts_ns | 上游 payload (Polymarket WSS / Goalserve `<match @date>`) | > 0; ≤ data_source_ts |
| data_source_ts_ns | 上游 payload > HTTP Date > INFERRED | ≤ ingestion_ts |
| ingestion_ts_ns | 本地 `clock_gettime(CLOCK_MONOTONIC_RAW)` (R-20 §6) | ≤ as_of_ts |
| as_of_ts_ns | 本地 `clock_gettime(CLOCK_REALTIME)` 使用时打 | ≤ now() |

framework 只做不等式 assert, 不管 INFERRED 退化原因 (caller 在 payload 内单列 `DataSourceTimestamp::source` enum, R-20 §2.2).

---

## 3. 4 WAL 详细 spec (v0.1 三 WAL 矩阵 + shadow_audit 第 4 列)

| 维度 | risk_audit | position | paper_audit | **shadow_audit (新)** |
|---|---|---|---|---|
| 目录根 | `/var/lib/stcpp/audit/` | `/var/lib/stcpp/exec/` | `/var/lib/stcpp/paper/` | `/var/lib/stcpp/shadow/` |
| 文件权限 | `0640 stcpp:stcpp-audit` | `0640 stcpp:stcpp-exec` | `0640 stcpp:stcpp-paper` | `0640 stcpp:stcpp-shadow` |
| systemd group | stcpp-audit | stcpp-exec | stcpp-paper | **stcpp-shadow** |
| Owner | 老韩 RM | 老周 + 老孙 | 小蒋 | 小蒋 (生产) / 小邓 (消费) |
| FsyncMode | GroupCommit | PerRecord | GroupCommit | GroupCommit |
| Ring 容量 | 65536 | 4096 | 16384 | 16384 |
| batch | 64 OR 1ms | 每条 fsync | 64 OR 1ms | 64 OR 1ms |
| RPO | ≤ 1ms | **0** | ≤ 1ms | ≤ 1ms (允许丢) |
| segment | 256 MiB | 64 MiB | 64 MiB | 64 MiB |
| 本地保留 | 7d | 7d | 30d | 30d |
| S3 保留 | 7y Glacier | 90d | 90d | 90d (advisory) |
| bg cpu pin | core 7 | core 7 | core 7 | **core 6** (Q-PE1) |
| 失败语义 | SAFE_MODE 全局 | SAFE_MODE 全局 | SAFE_MODE 局部 (仅 paper) | **重置 paper 档** (Q-PE2), live 不锁 |

### 3.1 Q-PE1 回执 (小蒋 提问 — shadow core pin)

shadow_audit bg fsync 线程 pin **core 6**, 与其他 3 类错开避 thrash. core 6 同时是 backtest replay bg core (二者不并发: backtest 是 nightly cron, shadow 是 live 同步). 阶段 3 shadow 进 live (2027 Q1, 小邓 roadmap §6) 再申请 core 5, 走 ADR.

### 3.2 Q-PE2 回执 (小蒋 提问 — shadow replay 失败语义)

shadow_audit 启动期 replay 失败 (mid-corruption / seq gap / ULID 非单调) → **重置 paper 档**:

```
1. rename ALL segments → /var/lib/stcpp/shadow/.quarantine/{ts}/ (留证据不删)
2. 创建空 segment 重启 (next_seq=1, prev_hash=zeros)
3. 不进 SAFE_MODE
4. emit risk_audit AET_RECON_DRIFT (kind=SHADOW_WAL_RESET) 留痕
```

理由: shadow signal advisory (M4.5 不看, 小邓 §6), 损坏不影响 live / paper 决策. 对比 risk_audit / position / paper_audit replay 失败一律 SAFE_MODE 拒启动 (v0.1 §5.3 不变).

### 3.3 物理隔离硬校验 (P9 落地)

WalWriter::Open() 期:

```cpp
constexpr std::array<std::pair<WalKind, std::string_view>, 4> kPathRoots = {{
  {WalKind::RiskAudit,    "/var/lib/stcpp/audit/"},
  {WalKind::Position,     "/var/lib/stcpp/exec/"},
  {WalKind::PaperAudit,   "/var/lib/stcpp/paper/"},
  {WalKind::ShadowAudit,  "/var/lib/stcpp/shadow/"},
}};
if (!cfg.path_prefix.starts_with(kPathRoots[cfg.kind].second)) std::abort();
```

不抛异常直接 abort, 防 caller try-catch 绕过. CI lint (§6) 提前在 PR 阶段拦.

---

## 4. PIT assert 接口 (P8 落地, R-20 §7)

### 4.1 namespace + API

```cpp
namespace stcpp::infra::wal::pit {

[[nodiscard]] inline bool AssertChain(const WalRecordHeader& h) noexcept {
  const int64_t now = clock_realtime_ns();
  return (h.event_ts_ns       > 0)
      && (h.data_source_ts_ns >= h.event_ts_ns)
      && (h.ingestion_ts_ns   >= h.data_source_ts_ns)
      && (h.as_of_ts_ns       >= h.ingestion_ts_ns)
      && (h.as_of_ts_ns       <= now);
}

enum class PitViolation : uint8_t { Ok, EventTsZero, DsBeforeEvent,
  IngestionBeforeDs, AsOfBeforeIngestion, AsOfInFuture };
PitViolation DiagnoseViolation(const WalRecordHeader& h) noexcept;
}
```

### 4.2 调用约定

- 所有 4 类 WAL `Append()` 入口**必调** `AssertChain`. 内联无分支, p99 < 100 ns.
- caller 不可绕过 (framework 私有, 不暴露 skip).
- caller 侧 (PaperSigner / LiveSigner / BacktestSigner) 也做一次早 reject (R-20 §7 红线), framework 内再校一次双保险.

### 4.3 fail 处理

- Append → `tl::unexpected(WalError::PitViolation)`
- caller → `REJECT(INVALID_INTENT)` (老韩 v0.3 reject enum)
- 同步 emit risk_audit `AET_RECON_DRIFT` (kind=PIT_VIOLATION), 不阻塞热路径
- 连续 5min > 100 条 → SAFE_MODE 全局 (R12)

---

## 5. 5 种 crash 场景恢复

| # | 场景 | framework 行为 | SAFE_MODE 联动 |
|---|---|---|---|
| 1 | `kill -9` | replay 到 last_good_seq, tail truncation 末尾 1 MiB 内自动截断 + `.truncation.log` | 默认 SAFE_MODE (W-3) 等运维 unlock; shadow 自重置 |
| 2 | 断电 / 主机宕 | 同 #1 + 依赖 ext4 `data=ordered` journal | 同 #1 |
| 3 | 磁盘满 (ENOSPC) | bg `fdatasync` ENOSPC, `failed_=true`, 后续 Append 全 backpressure | SAFE_MODE 全局; 运维清盘重启 |
| 4 | `fdatasync` EIO (硬盘故障) | bg `failed_=true` + alert P0; 5min 持续 → 老吴 cross-region cold standby | SAFE_MODE 全局 |
| 5 | mid-corruption (启动期) | risk/exec/paper: exit `SAFE_MODE_LOCKED` 等 `stcpp-ctl wal-recover` 人工; shadow: quarantine + 重置 (Q-PE2) | risk/exec/paper 锁; shadow 不锁 |

### 5.1 RPO / RTO 目标

- RPO ≤ 1ms (risk_audit / paper_audit / shadow_audit, GroupCommit)
- RPO = 0 (position, PerRecord)
- RTO ≤ 30s (replay 1M records ≤ 30s)

### 5.2 SafeModeFlag 联动 (老周 §14)

- 全局 `std::atomic<bool>`, RM evaluate 同步读 (老韩 §13).
- 启动期默认 = true (W-3 红线), 运维 `stcpp-ctl unlock` → false.
- 任一 (除 shadow) WAL fsync 失败 / mid-corruption / ENOSPC → set true.
- shadow_audit 失败**不 set** (Q-PE2).

---

## 6. CI lint (C6 落地)

`tools/wal-config-lint/main.cc` 扫所有服务的 TOML config:

```
for each [wal.{name}] section:
  assert: path_prefix.starts_with(kPathRoots[name])
  assert: wal_kind in {risk_audit, position, paper_audit, shadow_audit}
  assert: (name == "position") == (fsync_mode == "per_record")
  assert: ring_capacity 是 2 的幂
  assert: bg_cpu_core ∈ [4..7]
  assert: shadow_audit.bg_cpu_core != position.bg_cpu_core
```

PR 阶段跑, fail = block merge (老练 + 老高 接入).

---

## 7. TOML 配置增量 (v0.1 §10 + shadow)

```toml
[wal.shadow_audit]
path_prefix             = "/var/lib/stcpp/shadow/shadow_audit"
wal_kind                = "shadow_audit"
ring_capacity           = 16384
segment_max_bytes       = 67108864
rotation_period_sec     = 3600
fsync_mode              = "group_commit"
batch_size              = 64
batch_timeout_us        = 1000
bg_cpu_core             = 6                       # Q-PE1
local_retention_days    = 30
s3_bucket               = "stcpp-wal-archive"
s3_prefix               = "shadow_audit/"
s3_retention_days       = 90
compression             = "gzip"
reset_on_replay_failure = true                    # Q-PE2
```

老吴 deploy 增量:

```
groupadd stcpp-shadow
mkdir -p /var/lib/stcpp/shadow
chown stcpp:stcpp-shadow /var/lib/stcpp/shadow
chmod 0750 /var/lib/stcpp/shadow
```

---

## 8. 性能 budget (v0.1 §8 更新)

| 路径 | budget | 说明 |
|---|---|---|
| 同步 Append p99 | ≤ 6 us | 含 PIT assert + CRC + ring (R4 不变) |
| PIT assert | ≤ 100 ns | 内联无分支 |
| build_frame (header 64B + payload 1 KiB + CRC) | < 2.5 us p99 | 实测 SSE4.2 + memcpy |
| 异步 fsync batch p99 | ≤ 1 ms | NVMe SSD |
| Replay 1M records | ≤ 30 s | RTO 红线 |
| Header v2 增量 | +48B / record | shadow 1 KiB ≈ 5% 增量 |

同步预算分解 (v0.1 §3.1 更新):

| 步骤 | p99 |
|---|---|
| PIT assert | 100 ns |
| `next_seq_.fetch_add` | 50 ns |
| build_frame (CRC32C ≤ 1 KiB) | 2.5 us |
| `ring.try_push` (rigtorp SPSC) | 500 ns |
| **framework 同步** | **< 3.5 us** |
| 含 caller ULID + serialize | < 6 us ✓ |

---

## 9. 测试增量 (v0.1 §11 + v0.2 新增)

### 9.1 单测 (新增)

- `wal_writer_pit_violation_event_ts_zero` — Append 返 PitViolation
- `wal_writer_pit_violation_ds_before_event` — 同上
- `wal_writer_pit_violation_as_of_in_future` — 同上
- `wal_writer_path_prefix_mismatch_aborts` — death test
- `wal_writer_header_v2_64b_layout` — sizeof == 64, 4 ts offset 正确
- `wal_replayer_shadow_reset_on_corruption` — quarantine + 重置 + RECON_DRIFT emit
- `wal_replayer_risk_audit_lock_on_corruption` — exit SAFE_MODE_LOCKED
- `wal_replayer_ulid_non_monotonic_halt` — 拒启动

### 9.2 chaos (v0.1 §11.2 5 case 不变 + 加)

- `chaos/wal/shadow_mid_corruption_reset` — `dd` 改 shadow 中间 byte → quarantine + reset + RECON_DRIFT emit OK

### 9.3 集成 (与小蒋 / 小邓)

- shadow signal 1000 条 replay 一致
- paper + shadow 并发 1h, fd / core / safe_mode 不交叉

---

## 10. 落地计划

| W | 任务 | Owner | DoD |
|---|---|---|---|
| W2 (本周) | v0.2 设计 + cpp skeleton | 老王 | 本文 + cpp-interface 出 |
| W2 末 | 4 签 (老韩/老唐/小蒋/小邓) + 老郭 ADR review | 老王推动 | 签到位 |
| W3 | header v2 + PIT assert + Open() 硬校验 + 单测 | 老王 | §9.1 全绿 |
| W3 | shadow_audit core 6 pin 实测 | 老王 + 小蒋 | bg 不抢 core 7 |
| W4 | replay shadow 自重置 + 单测 | 老王 + 小蒋 | quarantine + RECON_DRIFT 通 |
| W4 | CI lint tool | 老王 + 老练 | PR 阶段拦误配 |
| S3 W1 | RM v0.3 + paper + ML shadow 三方接入 | 老韩 + 小蒋 + 小邓 | 集成 test 通过 |

---

## 11. 开放问题 (v0.1 OQ-1..OQ-8 沿用; v0.2 新)

| # | 问 | 倾向 | 待会签 |
|---|---|---|---|
| OQ-9 | header v2 64B × 7y audit segment 累积 ~7 TiB. 单列 header 索引文件减 S3 cost? | 不, gzip 后压缩比高 (相邻 int64 ts), Glacier 月 $7 可接受 | @老吴 |
| OQ-10 | PIT `now()` 用 REALTIME 还是 MONOTONIC_RAW? | as_of_ts 是 REALTIME, ingestion 是 MONOTONIC_RAW; framework PIT 用 REALTIME 比 REALTIME | @小邓 + @老练 |
| OQ-11 | shadow PIT 违例频发 (ML 时间戳 bug) 是否全局 SAFE_MODE? | 否, 只 emit RECON_DRIFT + 月度报表; live PIT 违例才 SAFE_MODE | @小邓 + @老韩 |
| OQ-12 | header 4 ts 与 payload 老唐 schema `evaluated_at_ns` / `ingested_at_ns` 是否重复? | 双轨保留: header 给 framework / index, payload 给 BLAKE3 hash 完整性; 双向校验不冗余 | @老唐 |
| OQ-13 | shadow_audit 重置后 ULID 跳跃 (clock skew), replay 是否关闭 ULID 校验? | 是, ShadowAudit ReplayOptions.check_ulid_monotonic=false | @小蒋 |

---

## 12. 新增风险登记

| # | 风险 | 等级 | 缓解 |
|---|---|---|---|
| WR-8 | header v2 不兼容 v1, 升级时旧文件无法读 | 中 | W2 升级时 WAL 全空 (M4.5 前无历史); `stcpp-wal-cat --ver=1` fallback |
| WR-9 | PIT assert 100ns 预算被 caller now() 拖到 1us | 低 | caller 在 ingest 时打一次, framework 内 PIT 不重打 now(); RDTSC 兜底 |
| WR-10 | shadow core 6 pin 与 backtest replay 抢 | 中 | shadow live / backtest cron 时间错开; 并发时 backtest 让步 |
| WR-11 | ULID 单调性多 WAL 间不保证, 索引交叉查询歧义 | 低 | 索引 sqlite (wal_kind, ulid) 双键; 老唐 §5.1 已留位 |

---

**END v0.2.** 签收:

- [ ] 老韩 (RM v0.3 → framework Append + 21 reject enum + INVALID_INTENT)
- [ ] 老唐 (header 4 ts 与 payload 双轨 OQ-12)
- [ ] 小蒋 (Q-PE1 core 6 + Q-PE2 shadow 自重置)
- [ ] 小邓 (shadow_audit ML sink + OQ-11)
- [ ] 老郭 (ADR review header v2 + 物理隔离硬校验)
- [ ] 老周 (knowing, SAFE_MODE 联动)
- [ ] 老练 (CI lint + OQ-10)
- [ ] 老吴 (4 根目录 + systemd group + OQ-9)
