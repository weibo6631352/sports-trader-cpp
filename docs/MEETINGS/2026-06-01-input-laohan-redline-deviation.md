# 老韩 input — RM 红线 vs W5 末 actual code 偏离审查

- **owner:** 老韩 (B 主管, risk-engineer)
- **last_review:** 2026-06-01
- **场合:** W5 末 Wave 26 代码 review 大会
- **ADR-009 model:** Opus
- **约束:** 我不写代码 (W5 cpp = 0, ADR-005 §2.2); 不耻下问 @老郭 / @老周 / @老沈 / @老唐 / @老雷
- **审查对象:**
  - `include/stcpp/risk/reject_enum.hpp` (73 行)
  - `tests/unit/test_risk_gateway.cpp` (473 行, 40 case)
  - `tests/unit/risk_enum_coverage_test.cpp` (59 行, 2 case)
  - W5 集成 fixture (小宋)

---

## 0. 派单 prompt 与 actual code enum 命名校对 (前置)

派单 prompt 21 enum 列表与 `reject_enum.hpp` actual 有命名偏差, 本 report **以 actual code 为准** (老郭 ADR-003 C-4: enum 总数 21 不增, 命名以 header 为 SSOT):

| # | actual (header) | 派单 prompt 旧名 |
|---|---|---|
| 6 | EXCEED_PER_ORDER_CAP | EXCEED_PER_ORDER_CAP ✓ |
| 7 | EXCEED_MARKET_EXPOSURE | (派单 #10 EXCEED_EXPOSURE_LIMIT) |
| 8 | DAILY_LOSS_HALT | (派单 #9 EXCEED_DAILY_CAP) |
| 9 | CONSEC_LOSS_HALT | (派单 #12 CONSECUTIVE_LOSS_LIMIT) |
| 11 | EDGE_CI_NEGATIVE | (派单 #14 SIGNAL_NEGATED) |
| 13 | MARKET_TYPE_NOT_ENABLED | (派单 #7 MARKET_TYPE_UNSUPPORTED) |
| 14 | MARKET_NOT_ACTIVE | (派单 #6 MARKET_INACTIVE) |

派单 prompt 编号顺序与 header 不一致 — 不算偏离, 只是 review 沟通口径问题。**统一以 `reject_enum.hpp` header 为口径**, 我 W6 推 owner 周同步纪要校正。

---

## Part 1: 21 RejectCode 实际单测覆盖

### 1.1 21 enum × case 矩阵 (按 header 顺序)

| # | RejectCode (header SSOT) | unit case | 备注 |
|---|---|---|---|
| 0 | STATE_HALTED | ✓ | |
| 1 | STATE_DRAIN | ✓ | |
| 2 | STATE_SAFE_MODE | ✓ | |
| 3 | DUPLICATE_INTENT | ✓ | |
| 4 | STALE_DATA | ✓ | |
| 5 | INVALID_INTENT | ✓ | 见 1.2 sub_reason |
| 6 | EXCEED_PER_ORDER_CAP | ✓ | ADR-004 regression 也命中 |
| 7 | EXCEED_MARKET_EXPOSURE | ✓ | |
| 8 | DAILY_LOSS_HALT | ✓ | |
| 9 | CONSEC_LOSS_HALT | ✓ | |
| 10 | INSUFFICIENT_BANKROLL | ✓ | |
| 11 | EDGE_CI_NEGATIVE | ✓ | |
| 12 | EDGE_NEGATED_BY_SLIPPAGE | ✓ | |
| 13 | MARKET_TYPE_NOT_ENABLED | ✓ | |
| 14 | MARKET_NOT_ACTIVE | ✓ | |
| 15 | LOW_FILL_RATE | ✓ | |
| 16 | EXCESSIVE_SLIPPAGE | ✓ | |
| 17 | EXCEED_BOOK_DEPTH | ✓ | ADR-004 regression 顺序检查 |
| 18 | AUDIT_WAL_BACKPRESSURE | ✓ | |
| 19 | STRATEGY_DECAYED | ✓ | unit 覆盖 enum, 触发逻辑未实施 (见 Part 7) |
| 20 | INTERNAL_ERROR | ✓ | |

**unit 覆盖率: 21 / 21 = 100% ✓** (老韩 W4 派单要求达成)

### 1.2 InvalidIntentSubReason 覆盖

header 9 值 (含 NONE 默认), unit 命中 8 个:

| sub_reason | unit case |
|---|---|
| NONE | 默认值, 无需专测 |
| BOOK_TS_ZERO | ✓ |
| BOOK_TS_STALE | ✓ |
| NAN_OR_INF | ✓ |
| NEGATIVE | ✓ |
| ILLEGAL_TICK | ✓ |
| TS_ORDER_VIOLATED | ✓ |
| TS_FUTURE | ✓ |
| TS_UNKNOWN_SRC | ✓ |

**sub_reason 覆盖: 8 / 8 实质覆盖 ✓** (NONE 是 default, invariant 即 `code != INVALID_INTENT ⟹ sub_reason == NONE`)

### 1.3 W5 patch case 校验

| W5 patch | 来源 | case |
|---|---|---|
| ADR-004 短路顺序 regression (position_caps 先于 liquidity) | 老沈 | ✓ |
| AuditId.NonZero_O2 (BUG-W5-001, 1000 次) | 老沈 | ✓ |

unit 总 case: 40 (test_risk_gateway) + 2 (risk_enum_coverage_test) = **42 case** (派单口径 41, 多 1, 因为 risk_enum_coverage 拆了 INVALID_INTENT 独立文件)

### 1.4 集成测试覆盖 (小宋 W5)

- 4 reject 路径已覆盖: `STALE_DATA / DUPLICATE_INTENT / EXCEED_PER_ORDER_CAP / STATE_HALTED` ✓
- 余 17 reject 路径 (按 header 21 - 4 已覆盖 = 17, 派单口径 12 因为减去 sub_reason 与状态机重叠) — **W5-T8 老沈派单中**

**偏离 1 (P1):** 集成测试 reject 路径覆盖 4 / 21, 余 17 路径 W5 末派老沈 (我 spec, 老沈 cpp), M1-G8 acceptance gate。

---

## Part 2: 4 wal 物理隔离 (R-11) 实际

v0.6 设计 4 wal kind: `risk_audit / paper_audit / shadow_audit / position`

| WAL kind | 落地状态 | owner |
|---|---|---|
| paper_audit (paper mode) | ✓ 老唐 audit_emitter v0.1 build-time switch | 老唐 |
| risk_audit (live mode) | ✓ 同上, build target 切 | 老唐 |
| shadow_audit (ML / 实验性) | ✓ 小邓 ML hook v0.1 用 ShadowAudit | 小邓 |
| **position** | **未实施** | 待派 |

**SingleInstanceLock 协同:** 小卢 v0.1 3 PID file (paper / live / backtest), paper mode 不锁 live ✓, 与 4 wal 协同。

**偏离 2 (P1):** Position WAL 未落代码, R-11 物理隔离当前是 3 wal (paper / risk / shadow), 缺 position。W6 派单 owner 待 — 我建议 owner = 老周 (Position WAL 与 v0.7 §8 启动期序列同步), @老周 确认接单。

---

## Part 3: 4 ts R-20 全链路实际

v0.6 设计: `event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts`

| 模块 | owner | 4 ts 透传 |
|---|---|---|
| WSS 入口 (UPSTREAM_PAYLOAD 优先) | 小冯 | ✓ |
| Goalserve 入口 (data_source_ts ← last_update) | 小段 | ✓ |
| Signal compute (SignalContext + feature_snapshot_id) | 小卢 P0-01 | ✓ |
| RM evaluate (OrderIntent + PIT chain assert) | 老韩 + 老沈 | ✓ |
| AuditEmitter (AuditRecord + decision_ts 第 5 不等式) | 老唐 | ✓ |
| PaperSigner (SignRequest) | 小蒋 | ✓ |
| VirtualMatcher (VirtualFill) | 小蒋 | ✓ |
| ML hook (FeatureSnapshot) | 小邓 | ✓ |
| M4.5 gate (GateMetrics + check_pit) | 小董 | ✓ |

CI grep enforce 8 项 + 集成 test T3 验证。

**偏离 3: 0 偏离 ✓** R-20 全链路 enforce。

---

## Part 4: R-7 build-time mode 实际

R-7: ExecutionMode 4 build target (paper / live / backtest / shadow), CMake `if-else` 物理隔离。

| 模块 | owner | 物理隔离 |
|---|---|---|
| polymarket | 老李 | paper / live ✓ |
| signer | 小蒋 | paper / live ✓ |
| execution | 小蒋 | paper only ✓ |
| ml | 小邓 | paper / backtest (不进 live) ✓ |
| process (PID file) | 小卢 W5-A-09 | paper / live / backtest 3 PID ✓ |

`ExecutionContext::Init` 双调 abort 守门。

**偏离 4: 0 偏离 ✓**

---

## Part 5: R-1 RM 唯一入口实际

R-1: 所有下单链路必经 `RiskGateway::evaluate()`, 0 绕过。

- paper signer (小蒋): 接 `RiskDecision = Allowed` 才 sign ✓
- virtual matcher: 接 `SignResponse` 才 match ✓
- W5 集成 4 reject 路径全经 evaluate() ✓
- CI grep enforce (W4 老周, W5 老高 PR review v1.1): 直接调 `signer.sign()` 严禁 ✓
- audit_emitter 追踪每次 evaluate ✓

**偏离 5: 0 偏离 ✓** R-1 严格 enforce。

---

## Part 6: BUG-W5-001 教训 (audit_id 非空 invariant)

**事故:** UB shift (`u64 << 64` 类型错误)。老沈 W5 fix: 10B → 6B big-endian, AuditId.NonZero_O2 1000 次 release 验证。

**永久 enforcement:**
- CI `build-release-ubsan` job 防同类 UB ✓
- AuditId.NonZero_O2 release-mode 1000 iter ✓

**教训外推 (我建议, W6 推老何 footgun checklist v1.1):**
- 所有 shift 走 `std::shift_left/std::shift_right` (C++23) 或自定义 helper with `static_assert(shift < sizeof(T) * 8)`
- 所有 release build 必跑 UBSAN (老高 PR review v1.2 强 enforce)
- 任何 `u64 >> N` / `u64 << N` 文字常量 shift 都过 `static_assert`

— @老何 接 footgun v1.1, @老高 接 PR review v1.2

---

## Part 7: STRATEGY_DECAYED (R-19) 三签解锁实际

v0.3.1 立 STRATEGY_DECAYED (OQ-D13 Bayesian kill switch): `P(μ<0|data) > 0.3` 持续 2 周 → 自动 halt + 三签解锁 (老韩 + 老唐 + GM)。

| 项 | 状态 |
|---|---|
| 21 enum #19 STRATEGY_DECAYED 已立 | ✓ |
| unit case 覆盖 (路径触发) | ✓ |
| 实际触发逻辑 (Bayesian posterior 算) | **未落代码** (paper 跑起来后才有数据) |
| 三签解锁机制 (audit chain 含 3 audit_id) | **未实施** |

**偏离 6 (P2):** STRATEGY_DECAYED 触发逻辑 + 三签解锁 W5 未实施。**M2 落地** (paper 跑 2 周才有 posterior 数据), W6 出设计 spec, M2 cpp 实施。

owner: 我 (spec) + 老唐 (BLAKE3 audit chain 3 签) + GM (解锁审批)

---

## Part 8: 红线 vs code 6 偏离总结

| # | 偏离 | 严重度 | W6 修复? | owner |
|---|---|---|---|---|
| 1 | 17 reject 路径 integration 未覆盖 (派单口径 12) | P1 | W5 末老沈 W5-T8 | 老沈 (cpp) + 老韩 (spec) |
| 2 | Position WAL 未落代码 | P1 | W6 派单 | 老周 (待确认) |
| 3 | 4 ts 全链路 R-20 | **0 偏离 ✓** | - | - |
| 4 | R-7 build-time mode | **0 偏离 ✓** | - | - |
| 5 | R-1 RM 唯一入口 | **0 偏离 ✓** | - | - |
| 6 | STRATEGY_DECAYED 触发逻辑 + 三签解锁 | P2 | M2 cpp (W6 spec) | 老韩 + 老唐 + GM |

**3 偏离 + 3 零偏离**, 3 偏离全有 owner 与时间窗。

---

## Part 9: 决议建议 (给老郭 F 顾问团集成)

1. **W5 末 (本周):** 老沈 W5-T8 fixture, 17 reject 路径 integration 覆盖 (我 spec, 老沈 cpp), M1-G8 acceptance gate
2. **W6 P0:** Position WAL 落代码, owner 老周 (与 v0.7 §8 启动期序列同步) — 待 @老雷 拍优先级
3. **W6 spec / M2 cpp:** STRATEGY_DECAYED 触发 (Bayesian posterior) + 三签解锁 (BLAKE3 audit chain 3 audit_id)
4. **W6 起:** 所有 shift 走 `std::shift_left/right` (C++23) 或 helper, 老何 footgun v1.1 加这条; 老高 PR review v1.2 release build 必跑 UBSAN
5. **口径校正:** owner 周同步纪要把派单 prompt 21 enum 编号与 header SSOT 对齐, 避免后续 review 混淆

---

## Part 10: 跨域求助 (不耻下问)

- @老郭 (F 集成): 6 偏离决议进 ADR? 建议进 ADR-010 (W5 偏离 follow-up)
- @老周 (A 系统): Position WAL W6 P0 接单确认? 与 v0.7 §8 启动期序列同时设计
- @老沈 (B): W5-T8 17 路径 fixture 我先出 spec (W5 末交付), 你 cpp
- @老唐 (B): STRATEGY_DECAYED 三签 audit chain 设计 (M2), 你给 BLAKE3 hash chain 接口
- @老雷 (GM): W6 P0 派单优先级 — Position WAL vs STRATEGY_DECAYED spec, 我建议 Position WAL 先 (依赖路径长)

---

**完成汇报:** RM 红线 vs code **6 偏离 (3 零 + 3 待修) + W5-T8 派单 (老沈 cpp) + W6 Position WAL (老周 待确认) + M2 STRATEGY_DECAYED (老韩 + 老唐 + GM)**

**我 W5 cpp = 0 守住 ✓** (ADR-005 §2.2)
