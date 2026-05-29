---
owner: 老韩 (B 主管, risk-engineer #09)
last_review: 2026-05-29
sprint: W10 Wave 99
status: APPROVED — 派老沈 W10 W3 实施
trigger: Wave 99 — GM 派 RM v0.5 整合 spec
adr_ref: ADR-027 (SSOT enforce), ADR-029 (worktree push PR flow), ADR-032 (local-first CI)
---

# RM v0.5 整合 Spec — W10 W3 实施派单

cite:
  polymarket_ssot_cite: laoli-w8-polymarket-data-structure-ssot-v1.md §3 §4.1 §6
  goalserve_ssot_cite:  N/A (OrderIntent 不直接对接 Goalserve)
  handshake_cite:       laoli-laoSun-handshake-v1.md §3 + laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md §2.3
  adr_ref:              ADR-027 §4 Enforce-1/2/3/4; ADR-029 §3.1; ADR-032 §3

---

## §0 背景 + 现状

### §0.1 现有基础

| 组件 | 版本 | 文件 | 状态 |
|---|---|---|---|
| OrderIntent struct | v0.5 | `include/stcpp/risk/risk_gateway.hpp` | 已落码 (老沈 W9 Wave 57) |
| RiskGateway 10 reject rules | v0.5 | `src/stcpp/risk/risk_gateway.cpp` | 已落码 (老沈 W9 Wave 57) |
| R6.3 per-outcome cap | v0.5 | risk_gateway.hpp + .cpp | 已落码 |
| RmState 状态机 | RUNNING/WARNING/HALTED/SAFE_MODE/DRAIN | risk_gateway.hpp | 已落码 |
| RejectCode enum | v0.5 (含 EXCEED_PER_OUTCOME_CAP=22) | `include/stcpp/risk/reject_enum.hpp` | 已落码 |

### §0.2 v0.5 整合 spec 目标

本 spec 定义 RM v0.5 整合所需的 3 个增量变更：

1. **OrderIntent v0.6 V2 字段** — 新增 `timestamp_ms / metadata / builder`，RM evaluate() 校验
2. **V2 fee 体系内部 estimate** — sports taker = 0.03，fee 不入签名，RM 内部估算 net_edge
3. **StateMachine 状态 DECAYED** — evaluate() 内 strategy_decayed 状态与 HALTED 语义区分明确

---

## §1 ADR-027 cite 块 (强 enforce, C1-C4)

| 检查 | 内容 | 本 spec 状态 |
|---|---|---|
| C1 | PR diff 改 OrderIntent 须含 SSOT cite | 已含 (见 frontmatter) |
| C2 | OrderIntent / SignedOrder struct 含 token_id | v0.5 已含; v0.6 维持 |
| C3 | Side enum 含 Buy 且含 Sell | v0.5 已含; v0.6 不改 enum |
| C4 | 跨 struct ABI handshake doc 存在引用 | 已含 (laoli-laoSun-handshake-v1.md §3) |

---

## §2 OrderIntent v0.6 — V2 新增字段

### §2.1 变更摘要 (v0.5 → v0.6)

| 变更类型 | 字段 | 说明 |
|---|---|---|
| 新增 | `timestamp_ms: int64_t` | V2 替代 nonce；Orchestrator 层填入 ms 级系统时间；EIP-712 Order.timestamp |
| 新增 | `metadata: std::string` | bytes32 hex (0x 前缀, 66 char)；默认填 `bytes32(0)`；策略层可自定义；进 EIP-712 |
| 新增 | `builder: std::string` | bytes32 hex optional；gasless relayer 专用；不用填 `bytes32(0)`；进 EIP-712 |
| 移除 | `nonce` (如 v0.5 有) | V2 废弃; 唯一性由 timestamp_ms 保证 |

**cite:** laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md §2.3 + §4.1

### §2.2 OrderIntent v0.6 struct 增量 (仅展示变更段)

在现有 `include/stcpp/risk/risk_gateway.hpp` OrderIntent struct 末尾追加：

```cpp
// ---- V2 新增字段 (老韩 W10 Wave 99 spec, laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md §2.3) ----
// timestamp_ms: V2 EIP-712 Order.timestamp (uint256, ms); Orchestrator 层按系统时间填入
// 作用: 替代 nonce 保证唯一性; 同地址同 ms 内不可重复提交
// RM 校验: != 0 且 > (now_ms - 60_000) 且 <= (now_ms + 5_000) (时钟漂移 5s 容忍)
std::int64_t  timestamp_ms{0};

// metadata: bytes32 hex (0x 前缀 + 64 hex chars = 66 chars); 进 EIP-712 Order.metadata
// RM 校验: 长度 == 66 且以 "0x" 开头; 不做内容校验
// 默认: "0x0000000000000000000000000000000000000000000000000000000000000000"
std::string   metadata;

// builder: bytes32 hex optional; 进 EIP-712 Order.builder; 不用则填 bytes32(0)
// RM 校验: 与 metadata 相同格式校验 (66 char 0x 前缀)
// 默认: "0x0000000000000000000000000000000000000000000000000000000000000000"
std::string   builder;
```

### §2.3 ABI lock v1.8 影响 (老高 W10 W2)

- `abi_lock.py v1.7 → v1.8`: 新增 grep pattern `timestamp_ms / metadata / builder` 在 OrderIntent struct body
- PR CI `core_data_structure_ssot_check.py` C2 更新: OrderIntent 含 token_id + timestamp_ms

---

## §3 RM evaluate() — V2 字段校验规则

### §3.1 check_invalid_intent_() 增量

在现有 `check_invalid_intent_()` 中，v0.5 已有 `MISSING_TOKEN_ID / MISSING_CONDITION_ID / INVALID_TOKEN_ID_FORMAT` 检查。v0.6 追加以下子检查（在现有子检查之后，按顺序短路）：

| 顺序 | 检查 | SubReason / RejectCode | 触发条件 |
|---|---|---|---|
| 新 R3.6 | timestamp_ms 为 0 | `INVALID_INTENT` + `TS_V2_MISSING` | `intent.timestamp_ms == 0` |
| 新 R3.7 | timestamp_ms 过期 | `INVALID_INTENT` + `TS_V2_STALE` | `intent.timestamp_ms < (now_ms - 60_000)` (60s 窗口) |
| 新 R3.8 | timestamp_ms 未来 | `INVALID_INTENT` + `TS_V2_FUTURE` | `intent.timestamp_ms > (now_ms + 5_000)` (5s 漂移容忍) |
| 新 R3.9 | metadata 格式非法 | `INVALID_INTENT` + `INVALID_BYTES32_FORMAT` | `metadata.size() != 66 || !starts_with("0x")` |
| 新 R3.10 | builder 格式非法 | `INVALID_INTENT` + `INVALID_BYTES32_FORMAT` | `builder.size() != 66 || !starts_with("0x")` |

**InvalidIntentSubReason 新增枚举值（追加在 reject_enum.hpp，现有 12 个 BOOK_TOKEN_ID_MISMATCH 之后）：**

```cpp
// v0.6 V2 新增:
TS_V2_MISSING         = 13,  // timestamp_ms == 0
TS_V2_STALE           = 14,  // timestamp_ms < now_ms - 60s
TS_V2_FUTURE          = 15,  // timestamp_ms > now_ms + 5s
INVALID_BYTES32_FORMAT = 16, // metadata 或 builder 格式非法 (非 66 char 0x hex)
```

### §3.2 ADR-004 短路顺序不变

evaluate() 10 reject 短路顺序严格保持 ADR-004 v2 定义：V2 字段校验在 R3 (invalid_intent) 内部追加，不改变 1-10 顶层顺序。

---

## §4 V2 fee 体系 — RM 内部 estimate

### §4.1 fee 规则 (ADR-027 cite: laoli-w8-polymarket-data-structure-ssot-v1.md §3.2 feeSchedule)

| 角色 | fee 率 | 说明 |
|---|---|---|
| Sports taker | 3.0% (`rate=0.03`) | 公式: `fee = C × 0.03 × p × (1-p)` |
| Maker | 0% | 永远不付费 |
| Maker rebate | 25% of taker fee pool | 每日结算，计入 maker 净 PnL |

**关键约束:** fee 字段 V2 已移出 EIP-712 签名层 (V1 feeRateBps=300 进签名; V2 exchange 自动 calc)。RM 内部 estimate 仅用于 net_edge 校验，**不传入 SignV62Request，不进 EIP-712**。

### §4.2 RM 内部 fee estimate 公式

```
now_ms 取自 std::chrono (系统时钟, 非 intent 中的 timestamp_ms)

p         = intent.price                       // ∈ (0, 1)
size_pUSD = intent.size_usdc                   // 单位: USDC micro cent (语义等价 pUSD micro)

sports_taker_fee_estimate =
    static_cast<double>(size_pUSD) * 0.03 * p * (1.0 - p)
    // 单位: pUSD micro (与 size_pUSD 相同单位)

net_edge_after_fee =
    gross_edge_ci_lower - (sports_taker_fee_estimate / static_cast<double>(size_pUSD))
    // gross_edge_ci_lower: 来自 edge_ci_lower_ map[signal_id]
    // 结果: 无量纲, 相对于 size 的净优势

maker_rebate_estimate =
    sports_taker_fee_estimate * 0.25
    // 做市商视角附加收益 (每日结算, 不影响即时下单拒绝决策)
```

### §4.3 新增 fee-related reject rule (插入 check_signal_() 内)

**RM R-fee-2: net_edge 校验**

在现有 `check_signal_()` 中，EDGE_CI_NEGATIVE 检查之后，追加：

```
R-fee-2: net_edge_after_fee >= cfg_.net_edge_floor
  触发: net_edge_after_fee < cfg_.net_edge_floor (默认 cfg_.edge_ci_lower_floor = 0.0)
  RejectCode: EDGE_NEGATED_BY_SLIPPAGE (复用现有 enum; fee 侵蚀与 slippage 侵蚀语义一致)
  audit sub_reason: NONE (EDGE_NEGATED_BY_SLIPPAGE 不使用 sub_reason, 参 reject_enum.hpp invariant)
```

**注:** `cfg_.edge_ci_lower_floor` 现有配置字段兼作 net_edge_floor。fee estimate 在 check_signal_() 内部计算（无状态，< 10ns），不需要新配置字段。

### §4.4 RiskConfig 不新增字段

fee rate (0.03) 为 Polymarket sports 市场固定值，硬编码为常量 `constexpr double kSportsTakerFeeRate = 0.03;`，不放入 RiskConfig（避免配置漂移风险，金融专家定阈值由老叶 SSOT 控）。

---

## §5 StateMachine — RUNNING/DRAIN/HALTED/DECAYED 语义

### §5.1 现有状态机 (v0.5 已落码)

```
enum class RmState : uint8_t {
    RUNNING   = 0,  // 正常运行
    WARNING   = 1,  // 警告 (不拒单, monitor 报警)
    HALTED    = 2,  // 全停: 拒所有单
    SAFE_MODE = 3,  // 启动/崩溃默认: 拒所有单 (人工确认后转 RUNNING)
    DRAIN     = 4,  // 平仓模式: 仅放行 is_close=true AND side=Sell
};
```

### §5.2 STRATEGY_DECAYED 与 HALTED 的语义区分

`STRATEGY_DECAYED` 是 RejectCode（第 9 个 reject rule，check_strategy_decayed_()），**不是 RmState**。两者语义不同：

| 维度 | RmState::HALTED | RejectCode::STRATEGY_DECAYED |
|---|---|---|
| 触发 | 人工操作 / drawdown 熔断 / 系统异常 | EV 衰减: `EV(realized)/EV(forecast) < 0.3` 持续 2 周 (OQ-D13) |
| 作用范围 | 全局停止 (所有策略、所有市场) | 按 strategy_id 单策略停止 |
| 平仓行为 | HALTED 拒平仓; DRAIN 放行平仓 | STRATEGY_DECAYED 拒该策略所有单 (含平仓，走人工撤单) |
| 恢复机制 | 人工 set_state(RUNNING) | 老董 Bayesian decay monitor 自动 unlock (P(μ>0|data) > 0.7 持续 1 周) |
| audit emit | STATE_HALTED | AET_STRATEGY_DECAYED |

### §5.3 DECAYED 状态 — evaluate() 内部流程 (check_strategy_decayed_() 现有逻辑确认)

现有 `check_strategy_decayed_()` 实现：
- 查 `strategy_ev_ratio_[intent.strategy_id]`
- 若 `ratio < cfg_.strategy_decay_min_ev_ratio (= 0.3)` → REJECT(STRATEGY_DECAYED)
- 短路顺序: 位于 check_signal_() 之后，emit_audit_() 之前 (ADR-004 第 9 位)
- **DRAIN 模式下 STRATEGY_DECAYED 仍生效**: DRAIN check 在第 1 步 (check_state_())，DRAIN 只放行 is_close=true+side=Sell 的单；对于通过 DRAIN 放行的平仓单，仍会走后续 check_strategy_decayed_() — 若策略已衰减，平仓也被拒（人工撤单路径，见 v0.3 §16）

本 spec 不改变 check_strategy_decayed_() 逻辑，仅在 §5.2 明确与 HALTED 的区别，便于老沈实施时无歧义。

### §5.4 状态转换图 (不变，确认 v0.5 现状)

```
SAFE_MODE (启动默认)
    → RUNNING          (人工 set_state, 实盘确认)
    → HALTED           (drawdown 熔断 / 人工 / 系统 P0)
    → DRAIN            (人工 / drawdown 触发平仓模式)

RUNNING
    → WARNING          (日损 > 阈值 * 0.8, 不拒单)
    → HALTED           (drawdown 熔断: daily_pnl < -daily_loss_halt / consec_loss >= consec_loss_halt)
    → DRAIN            (人工 / 策略退出)
    → SAFE_MODE        (进程重启, 默认保守)

HALTED → RUNNING       (人工解除)
DRAIN  → RUNNING       (仓位清零 + 人工确认)
```

---

## §6 AuditRecord v1.4 增量 (配套派单 @老唐)

V2 字段需同步进 AuditRecord（老唐 W10 W4 spec 联动）：

```
AuditRecord v1.4 新增 (末尾追加, L1 变更, 兼容 v1.3):
  std::int64_t  timestamp_ms{0};               // OrderIntent.timestamp_ms (V2)
  char          metadata[66]{};                // bytes32 hex 0x 前缀 66 char
  char          builder[66]{};                 // bytes32 hex 0x 前缀 66 char
```

**audit 可追溯红线 (CLAUDE.md §7 #6):** 每条 audit record 必含 timestamp_ms / metadata / builder 字段（APPROVED 或 REJECTED 均记录）。

---

## §7 拒绝原因表 — v0.6 新增

### §7.1 新增 RejectCode (无新增; 复用现有)

V2 字段校验失败全部走 `INVALID_INTENT` + 新增 `InvalidIntentSubReason` 枚举，不新增 RejectCode。遵守 ADR-003 C-4 "21 active codes" 原则。

### §7.2 InvalidIntentSubReason 新增 4 项

| 值 | 名称 | 触发条件 | 规则编号 |
|---|---|---|---|
| 13 | `TS_V2_MISSING` | `timestamp_ms == 0` | R3.6 |
| 14 | `TS_V2_STALE` | `timestamp_ms < now_ms - 60_000` | R3.7 |
| 15 | `TS_V2_FUTURE` | `timestamp_ms > now_ms + 5_000` | R3.8 |
| 16 | `INVALID_BYTES32_FORMAT` | metadata 或 builder 格式非法 | R3.9 / R3.10 |

### §7.3 完整风控拒绝原因表 (v0.6 快照)

| RejectCode | 值 | 触发规则 | v0.6 变化 |
|---|---|---|---|
| STATE_HALTED | 0 | RmState::HALTED | 不变 |
| STATE_DRAIN | 1 | RmState::DRAIN (开仓) | 不变 |
| STATE_SAFE_MODE | 2 | RmState::SAFE_MODE | 不变 |
| DUPLICATE_INTENT | 3 | signal_id 重复 | 不变 |
| STALE_DATA | 4 | 含 R8.4 BOOK_TOKEN_ID_MISMATCH | 不变 |
| INVALID_INTENT | 5 | sub_reason 细分 13 项 (v0.5 9项 → v0.6 13项) | +4 sub_reason |
| EXCEED_PER_ORDER_CAP | 6 | size > per_order_cap | 不变 |
| EXCEED_CONDITION_EXPOSURE | 7 | R6.2a per-condition | 不变 |
| DAILY_LOSS_HALT | 8 | daily_pnl < -daily_loss_halt | 不变 |
| CONSEC_LOSS_HALT | 9 | consec_loss >= consec_loss_halt | 不变 |
| INSUFFICIENT_BANKROLL | 10 | size > bankroll * 0.x | 不变 |
| EDGE_CI_NEGATIVE | 11 | gross edge CI lower < 0 | 不变 |
| EDGE_NEGATED_BY_SLIPPAGE | 12 | net_edge_after_fee < floor (R-fee-2 新) | **fee estimate 复用此 code** |
| MARKET_TYPE_NOT_ENABLED | 13 | 市场类型未启用 | 不变 |
| MARKET_NOT_ACTIVE | 14 | 市场未激活 | 不变 |
| LOW_FILL_RATE | 15 | fill_rate < threshold | 不变 |
| EXCESSIVE_SLIPPAGE | 16 | slippage > excessive_slippage_bps | 不变 |
| EXCEED_BOOK_DEPTH | 17 | size > book_depth | 不变 |
| AUDIT_WAL_BACKPRESSURE | 18 | emit() 返 false | 不变 |
| STRATEGY_DECAYED | 19 | ev_ratio < decay_min (R-fee-2 之后) | 不变 |
| INTERNAL_ERROR | 20 | 内部异常兜底 | 不变 |
| EXCEED_PER_OUTCOME_CAP | 22 | R6.2b per-token (v0.5 已落) | 不变 |

---

## §8 测试 spec (派老沈 W10 W3)

以下 6 个新增 test case，补入 ctest 现有 cases：

### T-v06-1: timestamp_ms 校验 (R3.6/R3.7/R3.8)

```
T-v06-1a: timestamp_ms == 0 → REJECTED(INVALID_INTENT, TS_V2_MISSING)
T-v06-1b: timestamp_ms = now_ms - 120_000 (2 min ago) → REJECTED(INVALID_INTENT, TS_V2_STALE)
T-v06-1c: timestamp_ms = now_ms + 10_000 (10s future) → REJECTED(INVALID_INTENT, TS_V2_FUTURE)
T-v06-1d: timestamp_ms = now_ms - 30_000 (30s ago, 在窗口内) → 不触发此规则
```

### T-v06-2: metadata / builder 格式校验 (R3.9/R3.10)

```
T-v06-2a: metadata = "" → REJECTED(INVALID_INTENT, INVALID_BYTES32_FORMAT)
T-v06-2b: metadata = "0x" + "00" * 31 (64 hex, 共 66 char) → 通过校验 (正确格式)
T-v06-2c: builder = "xyz" (非 0x 前缀) → REJECTED(INVALID_INTENT, INVALID_BYTES32_FORMAT)
```

### T-v06-3: fee estimate + net_edge reject (R-fee-2)

```
场景: gross_edge_ci_lower = 0.01 (1%)
      price = 0.5; size = 10_000 USDC (micro)
      sports_taker_fee = 10_000 * 0.03 * 0.5 * 0.5 = 75 (micro)
      net_edge = 0.01 - 75/10_000 = 0.01 - 0.0075 = 0.0025 > 0.0 → APPROVED
输入: 修改 cfg_.edge_ci_lower_floor = 0.005 (5 bps floor)
      gross_edge = 0.004 → net_edge = 0.004 - 0.0075 = -0.0035 < 0.005 → REJECTED
期望: d.reject == RejectCode::EDGE_NEGATED_BY_SLIPPAGE
```

### T-v06-4: STRATEGY_DECAYED vs HALTED 行为对比

```
T-v06-4a: set_state(RUNNING); set_strategy_ev_ratio("strat_A", 0.1) (< 0.3)
           intent{strategy_id="strat_A", is_close=true, side=Sell}
           期望: REJECTED(STRATEGY_DECAYED)
           (HALTED 不影响, DECAYED 拦平仓)

T-v06-4b: set_state(HALTED); set_strategy_ev_ratio("strat_A", 0.5) (> 0.3)
           intent{is_close=false, side=Buy}
           期望: REJECTED(STATE_HALTED)
           (HALTED 在第 1 步短路，不到 check_strategy_decayed_())

T-v06-4c: set_state(DRAIN); set_strategy_ev_ratio("strat_A", 0.1) (< 0.3)
           intent{is_close=true, side=Sell, strategy_id="strat_A"}
           期望: REJECTED(STRATEGY_DECAYED)
           (DRAIN 放行 is_close+Sell; 但仍走 check_strategy_decayed_() → REJECT)
```

### T-v06-5: V2 字段通过 + audit record 落

```
输入: OrderIntent v0.6 全字段合法 (timestamp_ms=now-1000, metadata=bytes32(0), builder=bytes32(0))
     其余条件满足 (edge > 0, caps 未超)
期望: APPROVED; audit_record.timestamp_ms == intent.timestamp_ms
     audit_record.metadata == intent.metadata
     audit_record.builder == intent.builder
```

### T-v06-6: hot path latency P99 < 50us (V2 字段不退化)

```
在 bench_risk_gateway.cpp 追加 V2 字段构造后的 evaluate() 100k 循环
期望: P99 < 50_000 ns (与 v0.5 baseline 相同)
注: timestamp_ms/metadata/builder 均为值类型/short string, 无额外 heap
```

---

## §9 实施派单 — @老沈 W10 W3

### §9.1 任务清单

| 编号 | 任务 | 文件 | 依赖 |
|---|---|---|---|
| T1 | OrderIntent v0.6 struct 追加 3 字段 | `include/stcpp/risk/risk_gateway.hpp` | 本 spec §2 |
| T2 | InvalidIntentSubReason 追加 4 枚举 (13-16) | `include/stcpp/risk/reject_enum.hpp` | 本 spec §7.2 |
| T3 | check_invalid_intent_() 追加 R3.6-R3.10 | `src/stcpp/risk/risk_gateway.cpp` | T1 + T2 |
| T4 | check_signal_() 追加 R-fee-2 net_edge 校验 | `src/stcpp/risk/risk_gateway.cpp` | 本 spec §4.3 |
| T5 | emit_audit_() 追加 V2 字段写入 AuditRecord | `src/stcpp/risk/risk_gateway.cpp` | T1 |
| T6 | AuditRecord 追加 3 字段 (v1.4 联动 stub) | `include/stcpp/risk/risk_gateway.hpp` (AuditRecord) | 本 spec §6 |
| T7 | 新增 6 个 test case (T-v06-1 到 T-v06-6) | `tests/unit/test_risk_gateway.cpp` | T1-T6 |
| T8 | bench_risk_gateway.cpp 追加 V2 字段测试 | `tests/perf/bench_risk_gateway.cpp` | T1 |

### §9.2 约束 (ADR-032 本地优先)

```
老沈实施约束:
  1. cmake --build build --parallel && ctest --output-on-failure 全过才回汇
  2. ctest 结果摘要必须含 "PASS N/M" 具体数字
  3. 本地 pre-push hook 跑通再 push (ADR-032 §3.1)
  4. cpp = 0 for 老韩 (主管不亲自写 cpp)
```

### §9.3 ADR-029 回汇格式

老沈完成后按 ADR-029 §3.1 Step 8 回汇：

```
commit hash: <7位 hex>
PR URL: https://github.com/.../pull/<N>
ctest 状态: <N/M PASS>
ADR-027 cite: 已含 (见 PR body)
```

---

## §10 跨域派单 (非老沈)

| 派给 | 任务 | Deadline | 依据 |
|---|---|---|---|
| @老唐 | AuditRecord v1.4 spec: 新增 timestamp_ms/metadata/builder; WAL v1.3→v1.4 | W10 W4 | 本 spec §6 |
| @老高 | abi_lock.py v1.8: 新增 V2 keyword grep (timestamp_ms/metadata/builder) | W10 W2 | 本 spec §2.3 |
| @老李 | handshake v2 co-sign: OrderIntent v0.6 字段确认 + V2 EIP-712 字段顺序 | W10 W2 | laosun v1 §2.2 |

这 3 项派单通过 B 主管 (老韩) → 对应主管 (老周 / 老郭 / F 顾问团) 渠道，老韩不直接派 A/D 单元 IC。

---

## §11 风控审计 (老韩自检)

1. **V2 fee 不入签名** — 确认 fee estimate 在 evaluate() 内部局部变量计算，不写入任何 IPC 结构
2. **timestamp_ms 窗口** — 60s stale + 5s future 窗口；实盘高延迟场景（跨洋 880ms RTT）仍充裕
3. **DECAYED 拦平仓** — 设计正确；策略衰减需人工撤单，不能自动平仓绕过 DECAYED
4. **21 enum 上限** — v0.6 无新增 RejectCode（INVALID_BYTES32_FORMAT 走 sub_reason，不计入顶层 enum）
5. **bytes32(0) 默认值** — `"0x" + "00" * 32 = 66 char`，CI grep C2 不需要适配（metadata/builder 字段不做内容 hash 校验）

---

**END spec.** 派老沈 W10 W3 实施 (T1-T8)。回汇含 ctest 结果 + PR URL。

**Last updated:** 2026-05-29 by 老韩 (B 主管, Wave 99)
