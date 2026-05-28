# 系统架构 v0.5 (Sprint-2 W3 整改, R-20 时间戳全链路 + PREGAME 取齐)

- Owner: 老周 (cpp-chief-architect)
- Date: 2026-05-28 UTC (R-20)
- Status: Sprint-2 W3 整改版, **v0.4 主体保留, 本文 = v0.4 + §21 新章 + §17.6.1 修正**
- 验收人: 老郭 (ADR-003 6/26 死线 C-Z2 + C-3)
- 关联:
  - `docs/RESEARCH/laozhou-architecture-v0.4.md` (676 行, 主体保留, 仅 §17.6.1 表修正)
  - `docs/ADR/2026-06-15-arch-rm-key-v0.4-v0.3-v5-review.md` (ADR-003, Conditional Accepted, 整改 C-2 + C-3)
  - `docs/ADR/2026-05-28-gm-redline-data-source-timestamping.md` (R-20)
  - `docs/RESEARCH/laohan-riskmanager-design-v0.3.md` §14.1 (PREGAME HALT 15000ms 锚)
  - `docs/RESEARCH/laowang-wal-framework-v0.2.md` §4 (PIT assert 接口, 老王已落 C++ namespace)
  - `docs/RESEARCH/laotang-audit-schema-v1.md` §6 (audit emit 入口)
  - `docs/RESEARCH/laoli-polymarket-endpoint-matrix-v3.md` + `docs/RESEARCH/xiaoduan-goalserve-api-spec-v1.md` (上游 ts 字段位置)

---

## 0. v0.4 → v0.5 变更摘要

### 0.1 一句话版

ADR-003 Conditional Accepted 两条整改: (1) **C-Z2 (C-2 全文称)** R-20 4 ts 全链路需架构层 enforcement → 新增 §21; (2) **C-3** PREGAME HALT 阈值与 老韩 v0.3 §14.1 不一致 → §17.6.1 表修正取 15000ms (附带 OUTRIGHT 修正落 30s 红线). 其他 v0.4 §15.6 / §17.1 / §18 / §19 / §20 一字不改.

### 0.2 v0.4 → v0.5 diff

| 模块 | v0.4 | v0.5 | 触发源 |
|---|---|---|---|
| §17.6.1 PREGAME_FAR HALT | 30000ms | **15000ms** (取齐老韩 v0.3 §14.1) | ADR-003 C-3 |
| §17.6.1 OUTRIGHT HALT | 60000ms | **30000ms** (D-06 红线 ≤ 30s 硬上限) | ADR-003 C-3 附带 |
| §21 (新) | — | R-20 数据时间戳 enforcement (4 ts 全链路 + PIT assert + DataSourceTimestamp::Source enum + 退化 audit emit + CI grep) | ADR-003 C-Z2 |

其余 §0 ~ §20 全部沿用 v0.4 (含 §18 Paper mock 四接口 / §19 ML shadow 三接口 / §20 跨域 review 清单).

---

## 17.6.1 修正: 5 档 STALE 表 (替代 v0.4 §17.6.1 表)

**与老韩 v0.3 §14.1 取齐, D-06 红线 30s 硬上限**.

| micro-state | T_{1/2} 实测 | WARNING (DEFERRED) | HALT (REJECT) | 触发条件 | D-06 上限 |
|---|---|---|---|---|---|
| **INPLAY_HOT_CRIT** | 0.12-0.15s (n=23) | 200ms | 800ms | NBA Q4<2min / NFL 2-min warning / MLB ≥8 局 ≤1 分差 / NHL P3<5min ≤1 分差 / OT | ≤ 30s ✓ |
| **INPLAY_HOT** | 0.21-0.45s | 500ms | 2000ms | game_in_progress && wss_rate_60s > 1/s | ≤ 30s ✓ |
| **INPLAY_COLD** (= v0.4 PREGAME_NEAR 重命名取齐老韩) | 8-30s | 2000ms | 10000ms | 邻近开赛 30min 内 / 暂停 / replay review | ≤ 30s ✓ |
| **PREGAME** (= v0.4 PREGAME_FAR 重命名取齐老韩) | 60-120s | 5000ms | **15000ms** (v0.5 改) | event_start - now > 30min | ≤ 30s ✓ |
| **SETTLED + OUTRIGHT** (= v0.4 OUTRIGHT 取齐老韩 SETTLED) | 120s+ | 10000ms | **30000ms** (v0.5 改, D-06 红线硬上限) | series / season-long / final / postponed | = 30s ✓ |

**命名取齐**: v0.4 用 PREGAME_NEAR/PREGAME_FAR/OUTRIGHT, 老韩 v0.3 §14.1 用 INPLAY_COLD/PREGAME/SETTLED. v0.5 改用老韩命名 (RM 是阈值消费方, 命名权归 RM owner). classifier 决策树 / 实现位置 (T2 book_builder vCPU1) 不变.

**fail-safe 退档不变**: 任何 classify 异常 → 退 INPLAY_COLD (中间档保守, 老韩 §14.3).

---

## 21. R-20 数据时间戳全链路 enforcement (新章)

### 21.0 章节定位

R-20 (老雷 2026-05-28 hard redline) + ADR-003 §6 派单: v0.4 通篇未提 4 ts 契约, §18.1.1 SignRequest / §19.1 MLSignalCandidate / §17.1.2 ring frame 都没说 4 ts 在哪打 / 在哪验. 本章把 R-20 在架构层闭环, **不重定义 4 ts 字段** (小邓 data-contract-v1.md §2 定字段, 老唐 audit-schema-v1.md §2 定 envelope, 老王 WAL v0.2 §4 定 PIT assert), 只定**架构层落点**.

### 21.1 4 ts 在哪些模块流转 (按 §17.1.2 ring buffer 路径)

| 阶段 | 模块 (§17.1.1 线程) | 4 ts 状态 | 打在哪 |
|---|---|---|---|
| L0 ingest | T0a/T0b/T0c/T1 (vCPU0 reactor) | 打 `data_source_ts` (优先级见 §21.3) + `ingestion_ts` | recv 后立刻, simdjson 解析时同步从 payload 抽 `event_ts` / `data_source_ts`; `ingestion_ts` = `clock_gettime(CLOCK_MONOTONIC_RAW)` (R-12 红线 < 50us, MONOTONIC_RAW 防 NTP 跳) |
| L1 book | T2 book_builder (vCPU1) | 透传 4 ts, 不重写; `FeatureSnapshot.feature_compute_ts` 在 T2 计算时打 | RCU snapshot 字段 |
| L2 strategy | T3 strategy_engine (vCPU2) | 读 RCU 拿 4 ts, decision 时打 `as_of_ts` (= `decision_ts`) | OrderIntent / MLSignalCandidate envelope |
| L3 RM | T3 同线程 (RiskGateway::evaluate) | 4 ts 全继承 OrderIntent, RM 内打 `rm_eval_ts` | 老韩 v0.3 §5 audit schema 字段 (派单 v0.3.1) |
| L4 signer | signer IPC (Live/Paper) | 4 ts 全继承 + `sign_request_ts` (caller 打) + `sign_complete_ts` (signer 打) | 老孙 v5.1 §5.4 IPC SignRequest schema (派单) |
| L5 audit | T8 audit_fsync (vCPU3) | envelope.event_ts/data_source_ts/ingestion_ts/as_of_ts 全字段, payload_hash 进 BLAKE3 链 | 老唐 audit v1.1 envelope (派单) |
| L5 exec WAL | T9 exec_fsync (vCPU3) | header v2 直读 4 ts, fsync 前调 `pit::AssertChain()` | 老王 WAL v0.2 header (已落) |
| paper mock §18 | PaperSigner / VirtualNonce / VirtualGas / VirtualConfirm | 4 ts 全字段继承, 加 `virtual_match_ts` / `virtual_confirm_ts` (小蒋 paper engine 打) | §18.1.x 接口 spec 加字段 |
| ML shadow §19 | IMLSignalEngine / IShadowSignalSink | MLSignalCandidate 必带 4 ts + `model_id` + `feature_snapshot_id` + `inference_ts` | ML-R8 红线已要求, 本章把 4 ts 一并强制 |
| backtest | xiaojiang backtest framework | PIT 严格守 4 ts 不等式 | 小蒋 v0.2 PIT CI grep 已派单 |

**故障域**: T0c user_reactor 打 `ingestion_ts` 与 T0a/b market 完全独立 (D-05), 4 ts 跨 ring 不串.

### 21.2 PIT assert 接口 (调用方必须传 4 ts)

**架构层强制规则**: 任何写 WAL / emit audit / 进 signer IPC 的调用方, **必须**先调老王 v0.2 §4 已落地的 PIT assert:

```cpp
// 老王 namespace, 不在本架构层重新定义
namespace stcpp::infra::wal::pit {
  [[nodiscard]] bool AssertChain(const WalRecordHeader& h) noexcept;  // p99 < 100ns
  enum class PitViolation : uint8_t { Ok, EventTsZero, DsBeforeEvent,
    IngestionBeforeDs, AsOfBeforeIngestion, AsOfInFuture };
  PitViolation DiagnoseViolation(const WalRecordHeader& h) noexcept;
}
```

**架构层调用方约束**:

| 调用方 | 入口 | 失败处理 |
|---|---|---|
| RiskGateway::evaluate (T3) | OrderIntent 入参第一行 | `REJECT(INVALID_INTENT)` + audit emit `AET_RECON_DRIFT` kind=PIT_VIOLATION (老唐) |
| PaperSigner::sign_and_fill (§18.1.1) | SignRequest 入参第一行 | 同上, paper 走 `paper_audit.wal` (R-11) |
| LiveSigner::sign_and_submit (老孙 v5.1) | 同 PaperSigner | 同 |
| BacktestSigner (小蒋) | 同 | 同, fail 不污染回测 (回测 strict) |
| IMLSignalEngine::tick (§19.1) | FeatureSnapshot 入参 | 不发 MLSignalCandidate, drop + counter (ML-R2: 不污染 rule) |
| audit::emit_decision (老唐 §6) | envelope 入参 | 拒写, 上游 caller 已经 REJECT |

**双保险**: caller 早 reject + framework Append 内再校 (老王 §4.2). 编译期 `[[nodiscard]]` 防漏调.

**性能预算**: 100ns × 6 调用点 = 600ns, 不挤 G3 evaluate 200us / hot path 500us 预算.

### 21.3 上游 ts 优先级 (R-20 §2.2 强制规则)

R-20 §2.2 用户原话: **"时间信息优先用数据源内部自带的"**. 架构层不允许用 `now()` 替代上游 ts.

```cpp
struct DataSourceTimestamp {  // 老雷 R-20 §2.2 原 schema, 不动
  int64_t epoch_ns;
  enum Source : uint8_t {
    UPSTREAM_PAYLOAD       = 0,  // 首选, payload 内字段 (Polymarket WSS timestamp / Goalserve <match @date> / Polygon block timestamp)
    UPSTREAM_HEADER        = 1,  // 次选, HTTP Date header
    INFERRED_FROM_DS_TS    = 2,  // 退化 1: event_ts 缺, 用 data_source_ts 代
    INFERRED_FROM_INGESTION= 3,  // 退化 2: 上游完全无 ts, 用 ingestion_ts 代
    UNKNOWN                = 255 // 异常, 触发 P1 alert
  } source;
};
```

**上游字段位置 (由各 endpoint owner 派单确认 — 派单 @老李 / @小段, ADR-003 §6)**:

| 数据源 | 字段位置 | 退化策略 |
|---|---|---|
| Polymarket WSS market channel | payload `timestamp` (ms) | 无 → UPSTREAM_HEADER (HTTP Date 升 WSS 时不可用, 实际退 INFERRED_FROM_INGESTION) |
| Polymarket WSS user channel | 同上 | 同 |
| Polymarket gamma REST | payload `last_updated_iso` + HTTP `Date` | payload 缺 → UPSTREAM_HEADER |
| Polymarket clob REST | HTTP `Date` 兜底 | 无 payload ts |
| Goalserve inplay XML | `<match @updated>` (.NET ticks, 小段 §259 转换公式) | 无 → UPSTREAM_HEADER |
| Goalserve pregame XML | 同 | 同 |
| Polygon WSS newHeads / logs | block `timestamp` (秒) | 链上字段必存, INFERRED 视为 P1 异常 |

**退化标识硬性 audit emit**: `source != UPSTREAM_PAYLOAD` 全部进 audit (老唐 envelope 加 `data_source_ts_source` u8 字段, 派单 @老唐 audit v1.1). 连续 N=100 条 INFERRED → 小郑 dashboard alert P1 (说明上游 schema 静默变更, R-20 §2.3 红线).

### 21.4 退化标识 audit emit (派 @老唐)

老唐 audit v1.1 envelope 加 3 字段 (与老唐 现有 `evaluated_at_ns` / `ingested_at_ns` 并列):

```protobuf
message AuditEnvelope {
  // ... v1 已有字段
  int64  event_ts_ns          = 50;  // R-20 §2.1, 上游声明 (UPSTREAM_PAYLOAD 优先)
  int64  data_source_ts_ns    = 51;  // R-20 §2.1
  uint32 data_source_ts_source = 52; // R-20 §2.2 enum DataSourceTimestamp::Source
  // ingestion_ts_ns 复用现有 ingested_at_ns (=ingestion_ts) — 老郭 ADR-003 §6 老唐"部分覆盖"
  // as_of_ts_ns      复用现有 evaluated_at_ns (=as_of_ts)
}
```

**新 AET 不加** — INFERRED 退化用现有 `AET_RECON_DRIFT` (kind=PIT_VIOLATION / kind=DS_TS_INFERRED) 表达, 保 12 AET 封闭性 (老唐 §3 不允许 OTHER). 老韩 §16 STRATEGY_DECAYED / UNLOCK 2 个新 AET 走老唐 v1.2 (ADR-003 §7.2 单列), 与本章 R-20 落地无关.

### 21.5 CI grep 反模式拦 (与小宋 / 老高 / 老练 联签)

派单 @老高 + @老练 加 grep job (R-20 §7 已开列, 本章把架构层反模式补全):

| 反模式 | grep pattern | 拦截位置 |
|---|---|---|
| 用 `now()` 替代上游 ts | `now()` 同行 `data_source_ts`/`event_ts` 赋值 | PR-merge gate (§20.5) |
| 用 `gettimeofday()` 打 `ingestion_ts` | `gettimeofday\(` | PR-merge gate (R-12 + R-20 §2.2) |
| 用 `localtime()` / `mktime()` (非 UTC) | `(localtime\|mktime)\(` | PR-merge gate (R-20 §6) |
| time 字段用 `int` (溢出风险) | `int\s+\w*(ts\|time)\w*\s*[;=]` | clang-tidy 自定义 rule |
| SignalCandidate / OrderIntent 缺 `feature_snapshot_id` | struct 定义 grep 必字段 | PR-merge gate (ML-R8 + R-20) |
| ML 路径缺 `model_id` + `inference_ts` | 同上 | PR-merge gate |
| `pit::AssertChain` 在 §21.2 6 个入口缺调 | static analyzer 调用图 | nightly CI (老练) |
| 文档引用 API 实测数据无采集时间 | docs/*.md grep "实测.*UTC" 缺失 | docs CI (小米 R-20 §8) |

**7 项 + 1 项 docs = 8 项, 与小宋 fixture / 老高 PR 模板 / 老练 nm 联签**, ADR-003 §6 老高 + 老练 派单 6/19 截止.

---

## 22. 不耻下问 (Sprint-2 W3 派单)

按 4 价值观 "不耻下问", 本章涉及他人接口必须他们确认:

| @ 谁 | 问什么 | 截止 |
|---|---|---|
| @小蒋 | §21.2 BacktestSigner PIT 入口与你 paper engine v0.2 §PIT CI grep 是否落同一函数 (避免双实现) | 6/19 |
| @老李 | §21.3 Polymarket WSS payload `timestamp` 字段是否 ms / 是否 epoch (gamma `last_updated_iso` 解析 owner) | 6/19 |
| @小段 | §21.3 Goalserve `<match @updated>` .NET ticks 转 epoch_ns 公式 (你 §259 已给, 我引用) 是否仍准 | 6/19 |
| @老唐 | §21.4 envelope 3 字段 (50/51/52) 是否与你 audit v1.1 编号冲突 + `AET_RECON_DRIFT.kind` 是否容纳 PIT_VIOLATION / DS_TS_INFERRED 2 子类 | 6/19 |
| @老韩 | §17.6.1 命名取齐 (INPLAY_COLD/PREGAME/SETTLED 用你 v0.3 §14.1 命名) 是否 OK + classifier code-level (小袁 owner) 是否同步改名 | 6/19 |
| @老孙 | §21.1 L4 signer `sign_request_ts` 由 caller 打 / `sign_complete_ts` 由 signer 打 (R-20 §3 已派单) — v5.1 §5.4 是否落同款 | 6/26 |
| @老郭 | §21 整体 enforcement 是否够闭环 + §17.6.1 OUTRIGHT 顺手取齐 30s 红线 (ADR-003 C-3 附带) 是否同意 | 6/26 |

---

## 23. v0.5 自评 (老周自查)

| 检查项 | 状态 |
|---|---|
| C-2 (R-20 4 ts 全链路) — §21 5 小节闭环 | ✓ |
| C-3 (PREGAME HALT 取齐 15000ms) — §17.6.1 表 | ✓ |
| 不重写 v0.4 全文 — 本文 200 行内, v0.4 §0~§20 全保留 | ✓ |
| 不耻下问 (派 7 单确认) — §22 | ✓ |
| PIT assert 接口不重复定义 — 引用老王 v0.2 §4 | ✓ |
| 上游 ts 字段位置不重复定义 — 引用老李 v3 + 小段 v1 | ✓ |
| audit envelope 不重复定义 — 引用老唐 v1 + 加 3 字段派单 | ✓ |
| 与 R-11 协同 (paper 4 ts 进 paper_audit.wal) — §21.1 L5 行 | ✓ |
| 与 R-12 协同 (ingestion_ts 用 CLOCK_MONOTONIC_RAW < 50us) — §21.1 L0 行 | ✓ |
| 性能预算不挤 G3 — §21.2 末 (600ns / 200us) | ✓ |

**Escalated**: 无.
**Compromised**: 无.
**Agreed**: 整改 2 条 (C-2 + C-3) 全闭合.

— 老周, 2026-05-28 UTC
