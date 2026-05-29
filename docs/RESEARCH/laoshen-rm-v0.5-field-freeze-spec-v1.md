---
owner: 老沈 (B 风控合规部 IC, security-engineer #B-002)
last_review: 2026-05-29
sprint: W10 W1
status: DRAFT — 待老韩 (B 主管) 签字后转 APPROVED; A 单元 (老陈/小卢) 可据此并行开工
trigger: 老韩 §8.1 派单 — RM v0.5 字段冻结契约 + 门禁 spec (关键路径首环)
adr_ref: ADR-027 §4 (Enforce-1/2/3/4); ADR-004 (reject 顺序); ADR-005 (派单层级)
cite:
  polymarket_ssot_cite: laoli-w8-polymarket-data-structure-ssot-v1.md §2.3 §3 §4.1 §6
  laosun_v62_spec_cite: laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md §2.3 §5.1
  laotang_v14_cite:     include/stcpp/observability/audit_record.hpp v1.4
  laohan_v05_spec_cite: laohan-rm-v0.5-integration-spec-v1.md (老韩 W10 Wave 99)
  handshake_cite:       laoli-laoSun-handshake-v1.md §3 SignedOrder ABI lock
  gm_directive_cite:    laolei-2026-drive-directive-paper-profit-v1.md §8.2 §9
---

# RM v0.5 字段冻结契约 + 门禁 Spec v1

> 本文件是 B 单元对 A 单元的承诺文件。A 单元 (老陈 serialize_into / 小卢 REST) 可据 §1
> 字段冻结契约并行开工，不依赖 B 单元后续实施进度。
>
> 安全原则：门禁强制不可绕过。paper 赶进度不是绕过理由。

---

## §1 OrderIntent v0.6 字段冻结契约 (Hard Freeze)

> **ABI Lock v1.8 — 字段集此处锁定。任何增/删/改字段须经 B 主管 (老韩) 签字 + ADR 记录。**
> 老陈 serialize_into 和小卢 REST client 可据此契约并行实施，不等 RM 实施完成。

### §1.1 冻结字段表

#### 组 G1 — R-20 四时间戳 (不变量: 严格单调递增链)

| 字段名 | C++ 类型 | 单位 | 语义 | 不变量 |
|---|---|---|---|---|
| `event_ts_ns` | `int64_t` | ns | 事件发生时刻 (数据源侧) | > 0; ≤ data_source_ts_ns |
| `data_source_ts_ns` | `int64_t` | ns | 数据源发布时刻 | ≥ event_ts_ns |
| `ingestion_ts_ns` | `int64_t` | ns | 系统收到时刻 | ≥ data_source_ts_ns |
| `as_of_ts_ns` | `int64_t` | ns | 信号生成 / intent 构建时刻 | ≥ ingestion_ts_ns; ≤ now_ns |

红线 R-20: event_ts_ns ≤ data_source_ts_ns ≤ ingestion_ts_ns ≤ as_of_ts_ns 必须成立。
任一违反 → INVALID_INTENT / TS_ORDER_VIOLATED。禁止 RM 内部用 now() 替代上游 ts。

#### 组 G2 — 市场标识 (双主键，v0.5 ABI break)

| 字段名 | C++ 类型 | 格式约束 | 语义 | 不变量 |
|---|---|---|---|---|
| `condition_id` | `std::string` | bytes32 hex, 0x 前缀, 66 chars (^0x[0-9a-f]{64}$) | Polymarket market 级主键 | 非空; 格式合法 |
| `token_id` | `std::string` | uint256 十进制字符串, 无 0x, ≤ 77 位纯数字 | Polymarket outcome 级主键 (EIP-712 Order.tokenId) | 非空; 纯数字; ≤ 77 chars |
| `outcome` | `Outcome` (uint8) | enum: Yes=0/No=1/Home=2/Draw=3/Away=4/Over=5/Under=6 | token 语义标注 (冗余, audit 用) | 与 token_id 对齐 |
| `side` | `Side` (uint8) | enum: Buy=0/Sell=1 | 买/卖方向 (与 outcome 解耦) | Buy=0/Sell=1，与 EIP-712 一致 |

历史字段废弃: v0.4 `market_id` 已 rename → `condition_id`; `is_buy: bool` 已替换 → `side: Side`。

#### 组 G3 — 业务 ID

| 字段名 | C++ 类型 | 语义 | 不变量 |
|---|---|---|---|
| `strategy_id` | `std::string` | 策略标识 (SignalId enum 字符串表示) | 非空 |
| `signal_id` | `std::string` | 幂等键 (RM 去重用) | 非空; 全局唯一 |
| `feature_snapshot_id` | `std::string` | ML 复盘锚 (R-20 audit hash) | 非空 |

#### 组 G4 — 定价与 book context

| 字段名 | C++ 类型 | 约束 | 语义 | 不变量 |
|---|---|---|---|---|
| `price` | `double` | (0.0, 1.0) 开区间 | 报价概率 | 有限值; > 0; < 1 |
| `size_pUSD_micro` | `int64_t` | > 0 | 下单规模 (micro pUSD, 1 pUSD = 1e6 micro) | > 0; ≤ per_order_cap |
| `book_depth_l1_usdc` | `double` | > 0.0 | L1 book 深度 (usdc) | 有限正值 |
| `book_snapshot_ts_ns` | `int64_t` | > 0; ≤ now-0; ≥ now-60s | book 快照时刻 (必须与 token_id 对齐) | R8.4: 与 token_id 一致性强校验 |
| `tick_size` | `double` | ∈ {0.001, 0.01} | per-token tick (SSOT §5 T-07) | 仅允许这两个值 |

注: `size_pUSD_micro` 是 v0.6 rename (原 v0.5: `size_usdc`)。pUSD = Polymarket USD (USDC.e 迁移后统一称谓)。

#### 组 G5 — V2 CLOB 字段 (v0.6 新增，ABI break #6/#7/#8)

| 字段名 | C++ 类型 | 格式约束 | 语义 | 不变量 | 填充责任 |
|---|---|---|---|---|---|
| `timestamp_ms` | `int64_t` | > 0; ∈ [now_ms-60000, now_ms+5000] | V2 EIP-712 Order.timestamp (ms); 替代 nonce | 非零; 窗口内 | Orchestrator 层填入; RM 禁止内部 now() 生成 |
| `metadata` | `std::string` | ^0x[0-9a-f]{64}$，66 chars | V2 EIP-712 Order.metadata (bytes32 hex) | 格式合法; 不用填 bytes32(0) | 策略层填; 默认值已设 |
| `builder` | `std::string` | ^0x[0-9a-f]{64}$，66 chars | V2 EIP-712 Order.builder optional (gasless relayer) | 格式合法; 不用填 bytes32(0) | 策略层填; 默认值已设 |

默认值:
- `metadata` 默认: `"0x0000000000000000000000000000000000000000000000000000000000000000"` (66 chars)
- `builder` 默认: `"0x0000000000000000000000000000000000000000000000000000000000000000"` (66 chars)

#### 组 G6 — 平仓标志

| 字段名 | C++ 类型 | 语义 | 不变量 |
|---|---|---|---|
| `is_close` | `bool` | 平仓单标识 (DRAIN 模式依赖) | true = 平仓; DRAIN 下必须 is_close=true AND side=Sell 才放行 |

### §1.2 字段冻结状态分类

| 状态 | 字段组 | 含义 |
|---|---|---|
| FROZEN — 不可更改 | G1 (4 ts) / G2 (双主键+outcome+side) / G3 (业务ID) / G4 (定价) / G5 (V2) / G6 (平仓) | 老陈/小卢 可直接按此实施 serialize/deserialize + REST client |
| RESERVED — 预留槽位 | 无 — 当前 OrderIntent 无保留字段 | 下版本如需扩展须经 ADR |
| 废弃 — 禁止使用 | `market_id` (v0.4), `is_buy` (v0.4), `size_usdc` (v0.5), `nonce` (V1) | 新代码禁用这些旧字段名 |

### §1.3 ABI Lock 不变量 (CI 守护)

以下不变量由 `abi_lock.py v1.8` (老高 CI) grep 强 enforce:
1. OrderIntent struct body 必含: `condition_id`, `token_id`, `side`, `outcome`, `timestamp_ms`, `metadata`, `builder`, `size_pUSD_micro`
2. `Side::Buy = 0`, `Side::Sell = 1` 枚举值不可变
3. `Outcome` 枚举值 Yes=0..Under=6 不可变
4. `timestamp_ms` 不可 = 0 (Orchestrator 红线)

头文件位置: `include/stcpp/risk/risk_gateway.hpp` (OrderIntent struct, 第 93 行起)

---

## §2 RM v0.5 门禁 Spec

### §2.1 老韩指定 6 类拒单 enum (CLAUDE.md 红线级别)

> 以下 6 类 enum 是老韩本轮指定的语义分组，映射到现有 RejectCode 枚举体系。
> 映射关系由老沈确认后锁定，不可绕过。

| 老韩指定 enum | 映射到 RejectCode | 值 | 触发语义 |
|---|---|---|---|
| `CAP_EXCEEDED` | `EXCEED_PER_ORDER_CAP` (6) + `EXCEED_CONDITION_EXPOSURE` (7) + `EXCEED_PER_OUTCOME_CAP` (22) | 6/7/22 | 单笔/条件级/outcome 级仓位上限超限 |
| `KELLY_OVERSIZE` | `EDGE_CI_NEGATIVE` (11) + `EDGE_NEGATED_BY_SLIPPAGE` (12) | 11/12 | Kelly+CI sizing 拒绝: CI 下界不足 / 滑点/fee 侵蚀净边 |
| `DD_BREAKER_ACTIVE` | `DAILY_LOSS_HALT` (8) + `CONSEC_LOSS_HALT` (9) | 8/9 | Drawdown breaker 激活: 日亏损 / 连续亏损熔断 |
| `DUPLICATE_NONCE` | `DUPLICATE_INTENT` (3) | 3 | 幂等 signal_id 重复; V2 timestamp_ms 维度去重 |
| `TIER_LOCKED` | `STATE_HALTED` (0) + `STATE_DRAIN` (1) + `STATE_SAFE_MODE` (2) | 0/1/2 | 资金阶梯/状态机锁定: HALTED/DRAIN/SAFE_MODE |
| `PAPER_LEDGER_GUARD` | 见 §4 专项 | — | Paper 模式下拒写真账本 (R-11 执行级别) |

注意: 老韩指定 enum 是语义分组标签 (audit 元数据层)，不替换 RejectCode 枚举值体系。RejectCode 枚举体系维持 ADR-003 C-4 "21 active codes" 不增。

### §2.2 evaluate() 10 规则校验顺序 (SSOT = ADR-004, hard freeze)

短路顺序不可调换。任一规则命中立即 emit audit + 返回 REJECTED。

```
1. check_state_()        — STATE_HALTED/STATE_DRAIN/STATE_SAFE_MODE
                          (DRAIN 放行条件: is_close=true AND side=Sell)
                          (SAFE_MODE 放行条件: is_close=true)
2. check_invalid_intent_() — R-20 PIT 4ts + 字段合法性 + V2 校验
                           (含 timestamp_ms/metadata/builder 格式)
3. check_duplicate_()    — DUPLICATE_INTENT (signal_id 幂等去重)
4. check_stale_data_()   — STALE_DATA (MarketState 5档 + recon全局 + R8.4 book/token mismatch)
5. check_market_()       — MARKET_TYPE_NOT_ENABLED / MARKET_NOT_ACTIVE
6. check_position_caps_() — EXCEED_PER_ORDER_CAP / EXCEED_CONDITION_EXPOSURE /
                            EXCEED_PER_OUTCOME_CAP / INSUFFICIENT_BANKROLL /
                            DAILY_LOSS_HALT / CONSEC_LOSS_HALT
                            [ADR-004: caps 红线前移，早于流动性检查]
7. check_liquidity_()    — EXCEED_BOOK_DEPTH / LOW_FILL_RATE / EXCESSIVE_SLIPPAGE
                           (此处计算 slippage_bps，为后续 signal 校验提供数据)
8. check_signal_()       — EDGE_CI_NEGATIVE / EDGE_NEGATED_BY_SLIPPAGE
                           (含 fee estimate net_edge 校验 R-fee-2)
9. check_strategy_decayed_() — STRATEGY_DECAYED (OQ-D13 Bayesian kill switch)
10. emit_audit_()         — AUDIT_WAL_BACKPRESSURE (emit 失败兜底, fail-closed)
```

**APPROVED 路径**: 全部 10 规则通过 → emit audit → 返回 APPROVED。
**REJECTED 路径**: 任一规则命中 → emit audit (失败则 AUDIT_WAL_BACKPRESSURE 覆盖) → 返回 REJECTED。

### §2.3 各类拒单触发条件 + audit record 字段

#### 2.3.1 CAP_EXCEEDED — 仓位上限

触发顺序 (均在 check_position_caps_() 内，按此顺序短路):

| 规则 | 触发条件 | RejectCode | audit 字段 |
|---|---|---|---|
| 单笔上限 | `intent.size_pUSD_micro > cfg.per_order_cap_usdc` | EXCEED_PER_ORDER_CAP (6) | reject=6, sub_reason=NONE |
| per-condition cap (R6.2a) | `condition_exposure[cid] + size > cfg.market_exposure_cap_usdc` | EXCEED_CONDITION_EXPOSURE (7) | reject=7, sub_reason=NONE, condition_id 透传 |
| per-outcome cap (R6.2b) | `token_exposure[token_id] + size > cfg.per_outcome_cap_usdc` | EXCEED_PER_OUTCOME_CAP (22) | reject=22, sub_reason=NONE, token_id 透传 |
| 资金不足 | `intent.size_pUSD_micro > bankroll_usdc` | INSUFFICIENT_BANKROLL (10) | reject=10 |

默认配置值 (RiskConfig):
- `per_order_cap_usdc = 10,000` micro pUSD
- `market_exposure_cap_usdc = 50,000` micro pUSD (per-condition 上限)
- `per_outcome_cap_usdc = 25,000` micro pUSD (per-token 上限, R6.2b)
- `bankroll_usdc = 100,000` micro pUSD

#### 2.3.2 KELLY_OVERSIZE — Kelly+CI sizing

触发在 check_signal_() 内:

| 规则 | 触发条件 | RejectCode | 说明 |
|---|---|---|---|
| CI 下界不足 | `edge_ci_lower[signal_id] <= cfg.edge_ci_lower_floor (默认 0.0)` | EDGE_CI_NEGATIVE (11) | Kelly f* ≤ 0 → 不下注 |
| 净边被侵蚀 (slippage) | `edge_bps < slippage_bps (来自 step 7)` | EDGE_NEGATED_BY_SLIPPAGE (12) | gross edge 不够覆盖滑点 |
| 净边被侵蚀 (fee, R-fee-2) | `net_edge_after_fee < cfg.edge_ci_lower_floor` | EDGE_NEGATED_BY_SLIPPAGE (12, 复用) | sports taker fee = 3% × p × (1-p); fee 不进 EIP-712 |

**Kelly sizing 公式** (CI 下界版):
```
f_kelly = max(0, (q_low - p) / (1.0 - p))
  q_low = edge_ci_lower (95% CI 下界)
  p     = intent.price
f_used  = KELLY_FRACTION * f_kelly  (MVP: KELLY_FRACTION = 0.25, 1/4 Kelly)
suggested_size = f_used * bankroll
```

**fee estimate** (内部计算，不传出):
```
sports_taker_fee = size_pUSD_micro * 0.03 * p * (1-p)
net_edge_after_fee = edge_ci_lower - fee / size_pUSD_micro
```
`kSportsTakerFeeRate = 0.03` 硬编码常量，不入 RiskConfig (防漂移)。

#### 2.3.3 DD_BREAKER_ACTIVE — Drawdown 熔断 (老韩裁决: -3% 软 / -5% 硬)

> GM §9 裁决 #1: 采老韩 3% 作为单日亏损上限。老韩 RM 主权。

| 层级 | 阈值 | 触发条件 | RejectCode | 状态转换 |
|---|---|---|---|---|
| 软 (Warning) | daily_pnl < -3% × bankroll | `daily_pnl_usdc < 0 AND -daily_pnl > bankroll * 0.03` | DAILY_LOSS_HALT (8) | RmState 维持 RUNNING; 拒新开仓 (is_close=false 均拒); 平仓放行 |
| 硬 (Kill) | daily_pnl < -5% × bankroll | `daily_pnl_usdc < 0 AND -daily_pnl > bankroll * 0.05` | DAILY_LOSS_HALT (8) | RmState → HALTED; 全拒含平仓; 人工解除 |
| 连续亏损 | consec_loss >= consec_loss_halt | `consec_loss_ >= cfg.consec_loss_halt_count (默认 5)` | CONSEC_LOSS_HALT (9) | RmState → HALTED |

**-3% 软 / -5% 硬 分层实施说明:**
- 当前 `RiskConfig.daily_loss_halt_usdc` 对应硬 kill 阈值 (绝对值)。
- -3% 软熔断需在 check_position_caps_() 中新增一个分支: 若 daily_pnl 跌破 -3% 但未跌破 -5%，拒 is_close=false 的新开仓单 (DAILY_LOSS_HALT)，放行 is_close=true 的平仓单。
- -5% 硬 kill: 触发后调用 `set_state(HALTED)`，全部拒单含平仓，等人工 unlock。
- 实施时 RiskConfig 需新增 `daily_loss_soft_pct = 0.03` 和 `daily_loss_hard_pct = 0.05` 两个字段 (后续 PR 实施)。

audit record 透传字段: `reject_code=DAILY_LOSS_HALT, decision_ts_ns, condition_id, token_id, signal_id`。

#### 2.3.4 DUPLICATE_NONCE — 幂等去重

| 维度 | 去重键 | 实现 | 生命周期 |
|---|---|---|---|
| signal_id (现有) | `intent.signal_id` | `unordered_set<string> seen_signal_ids` | 进程级，重启清零 |
| timestamp_ms (V2, 待加) | `(wallet_address, timestamp_ms)` | 需注入钱包地址上下文 | 同 signal_id |

现有实现: `check_duplicate_()` 以 `signal_id` 作幂等键。V2 timestamp_ms 唯一性约束 (`同地址同 ms 不可重复`) 由 CLOB V2 服务端保证，RM 侧以 `timestamp_ms != 0 AND 在时间窗口内` 作客户端前置校验 (INVALID_INTENT/TS_V2_MISSING/TS_V2_STALE/TS_V2_FUTURE)。

audit record: `reject_code=DUPLICATE_INTENT, signal_id 透传`。

#### 2.3.5 TIER_LOCKED — 资金阶梯/状态机锁定

| 状态 | 触发条件 | 放行条件 | RejectCode | 解锁机制 |
|---|---|---|---|---|
| SAFE_MODE | 进程启动默认 / 崩溃恢复 | is_close=true 平仓放行 | STATE_SAFE_MODE (2) | 人工 set_state(RUNNING) 确认 |
| DRAIN | 人工 / 策略退出 | is_close=true AND side=Sell | STATE_DRAIN (1) | 仓位清零 + 人工 set_state(RUNNING) |
| HALTED | drawdown 熔断 / 人工 / 系统 P0 | 无 (全拒含平仓) | STATE_HALTED (0) | 人工 set_state(RUNNING) |

**资金阶梯解锁约束 (per GM 指令 §3):**
- paper 模式: 模拟本金固定 (GM §6 风险 #3); 阶梯解锁逻辑同 live 代码路径 (R-11 保证同 binary)
- 阶梯启用: `SAFE_MODE → RUNNING` 需要人工确认，不允许自动升级 (防止崩溃恢复后自动开单)

audit record: `reject_code=STATE_HALTED/STATE_DRAIN/STATE_SAFE_MODE, event_type=StateTransition`。

#### 2.3.6 audit record 完整字段 (每条拒单必须包含)

每次 evaluate() 无论 APPROVED 或 REJECTED 均 emit 一条 AuditRecord (v1.4):

| 字段 | 类型 | 来源 | 必填 |
|---|---|---|---|
| `audit_id_bytes` | `array<uint8, 16>` | ULID (ts_ms + seq) | 必填, 非空 |
| `schema_version` | `uint8` | 常量 0x14 | 必填 |
| `event_ts` | `int64_t` | `intent.event_ts_ns` | 必填 |
| `data_source_ts` | `int64_t` | `intent.data_source_ts_ns` | 必填 |
| `ingestion_ts` | `int64_t` | `intent.ingestion_ts_ns` | 必填 |
| `as_of_ts` | `int64_t` | `intent.as_of_ts_ns` | 必填 |
| `decision_ts` | `int64_t` | RM 内部 now_ns (决策时刻, 不参与 PIT 链) | 必填 |
| `event_type` | `AuditEventType` | OrderApproved=1 / OrderRejected=2 | 必填 |
| `reject_code` | `RejectCode` | REJECTED 时: 具体 code; APPROVED 时: INTERNAL_ERROR (占位) | 必填 |
| `sub_reason` | `InvalidIntentSubReason` | 仅 reject=INVALID_INTENT 时非 NONE | 必填 |
| `condition_id` | `char[32]` | `intent.condition_id` | 必填 |
| `token_id` | `char[80]` | `intent.token_id` | 必填 |
| `strategy_id` | `char[32]` | `intent.strategy_id` | 必填 |
| `size_pUSD_micro` | `int64_t` | `intent.size_pUSD_micro` | 必填 |
| `price` | `double` | `intent.price` | 必填 |
| `outcome` | `uint8` | `intent.outcome` | 必填 |
| `side` | `uint8` | `intent.side` | 必填 |
| `timestamp_ms` | `int64_t` | `intent.timestamp_ms` (V2) | 必填 (v1.4) |
| `metadata` | `char[68]` | `intent.metadata` (V2) | 必填 (v1.4) |
| `builder` | `char[68]` | `intent.builder` (V2) | 必填 (v1.4) |
| `prev_hash` | `array<uint8, 32>` | BLAKE3 chain (stub: XOR prev) | 必填 |
| `payload_hash` | `array<uint8, 32>` | BLAKE3 payload (stub: counter+len) | 必填 |
| `current_hash` | `array<uint8, 32>` | BLAKE3 chain head | 必填 |
| `crc32c` | `uint32_t` | payload 内嵌 sanity | 必填 |

**可追溯红线 (CLAUDE.md §7 #6):** 每条 audit record 必须含 timestamp_ms/metadata/builder — APPROVED 和 REJECTED 均记录。

---

## §3 paper R-11 PAPER_LEDGER_GUARD — Paper 模式账本隔离

### §3.1 红线原文

> CLAUDE.md §8: "Paper mode 污染真账本 (写入 position / pnl_ledger / nonce_ledger) → P0"

### §3.2 build-time 隔离机制 (现有实现确认)

R-11 通过 **build-time 宏** 实现完全隔离，不依赖运行时检查：

| 编译宏 | WAL 种类 | 路径 | 实盘账本写入 |
|---|---|---|---|
| `STCPP_EXEC_MODE_live` | `WalKind::RiskAudit` | `/var/lib/stcpp/audit/` | 允许 |
| `STCPP_EXEC_MODE_paper` | `WalKind::PaperAudit` | `/var/lib/stcpp/paper/` | 禁止 |
| `STCPP_EXEC_MODE_backtest` | `WalKind::PaperAudit` | `/var/lib/stcpp/paper/` | 禁止 |
| 缺定义 | `WalKind::PaperAudit` | `/var/lib/stcpp/paper/` | 禁止 (开发期保守默认) |

实现位置: `include/stcpp/observability/audit_record.hpp::AuditWalKindForBuild()` (constexpr, 编译期决定)

### §3.3 RM 侧 PAPER_LEDGER_GUARD 校验逻辑

paper 模式下 RM evaluate() 必须拒绝任何试图写入真账本的路径。具体执行层面:

**1. AuditEmitter 接线约束:**
- paper binary 链接的 `AuditEmitter` 实现必须只写 `WalKind::PaperAudit` (`/var/lib/stcpp/paper/`)
- 禁止 paper binary 中出现 `WalKind::RiskAudit` 或 `WalKind::Position` 的 WalWriter 实例
- CI 检查: paper binary 的 symbol table 不得含 `RiskAudit` 路径字符串

**2. PositionLedger 接线约束:**
- paper 模式的 PositionLedger 实例只接受来自 VirtualMatcher 的 `VirtualFill` (不接真成交)
- paper 模式下 `PositionLedger::apply_fill()` 是 paper-only 实例，与 live `PositionLedger` 完全隔离
- 具体实现: paper binary 通过 `PaperPolymarketClient` 路径 (不 link LivePolymarketClient)

**3. nonce_ledger 隔离:**
- V2 timestamp_ms 替代 nonce (CLOB V2)
- paper 模式下 timestamp_ms 校验逻辑与 live 相同 (不放水)
- paper 不向 CLOB 提交真实 EIP-712 签名，PaperSigner 生成 mock 签名 (`paper-<seq>`)

**4. 运行时守护 (defense in depth):**
- `WalWriter::Open()` 期间硬校验 path prefix (fail = abort)
  - paper binary 中 Open() 调用 PaperAudit 路径，若传入非 paper 路径则 abort
- `PaperPolymarketClient::SubmitOrder()` 返回 mock order_id `"paper-<seq>"` 而非真 CLOB order_id

**5. PAPER_LEDGER_GUARD audit 事件 (新增):**
当 paper 模式检测到非法写入尝试时 (defense in depth 兜底), emit 一条特殊 audit record:
```
event_type = ReconDrift (12)    -- 复用 (paper 污染视为 recon 漂移 P0)
reject_code = INTERNAL_ERROR    -- 系统层面异常
sub_reason = NONE
note: "PAPER_LEDGER_GUARD: attempted write to live ledger path"
```
然后 **abort 进程** (P0 级别，不允许静默继续)。

### §3.4 单测要求 (老唐 R-11 verify W10 W2)

| 测试场景 | 期望行为 |
|---|---|
| paper binary 下 evaluate() APPROVED | audit 落 `/var/lib/stcpp/paper/`，不落 `/var/lib/stcpp/audit/` |
| paper binary 下显式构造 RiskAudit WalWriter | 编译失败 (或链接失败，取决于 CMake 守卫) |
| PaperPolymarketClient.SubmitOrder | order_id 格式为 `paper-<seq>`，不调链上接口 |
| PositionLedger.apply_fill in paper mode | 只写 paper 专属 PositionLedger 实例 |

---

## §4 A↔B 契约清单 (W10 多人讨论会签字用)

### §4.1 契约 AB-01 — OrderIntent v0.6 字段冻结

| 项目 | 内容 |
|---|---|
| 契约编号 | AB-01 |
| 双方 | B 单元 (老沈 RM 实施) ↔ A 单元 (老陈 serialize_into + 小卢 REST client) |
| 内容 | OrderIntent v0.6 字段集 (§1.1) 硬冻结。老陈 serialize_into 和小卢 REST client 可据此并行实施，不依赖 RM 实施进度 |
| B 承诺 | RM evaluate() 只读 §1.1 所列字段；不新增字段不经 ADR |
| A 承诺 | serialize_into 按 §1.1 字段顺序和类型实现；REST client 请求体按 V2 CLOB 格式填充 (token_id/side/timestamp_ms/metadata/builder) |
| 违约处置 | 任一方变更需 B 主管 (老韩) + A 主管 (老周) 联合签字 + PR review |
| 签字状态 | 待老韩 + 老周 W10 多人讨论会签字 |

### §4.2 契约 AB-02 — AuditRecord v1.4 schema 冻结

| 项目 | 内容 |
|---|---|
| 契约编号 | AB-02 |
| 双方 | B 单元 (老沈 RM + 老唐 audit replay) ↔ A 单元 (老陈 WAL 接线) |
| 内容 | AuditRecord v1.4 (schema_version=0x14) 字段集冻结 (见 §2.3.6 完整字段表) |
| B 承诺 | emit_audit_() 填充所有 §2.3.6 必填字段；schema_version=0x14 |
| A 承诺 | WAL writer 接收 AuditRecord，按 serialize_into() (POD memcpy) 落盘；不依赖字段语义，只依赖 size 和 layout |
| 签字状态 | 待老韩 + 老周 W10 签字 |

### §4.3 契约 AB-03 — DRAIN StateMachine 复用边界

| 项目 | 内容 |
|---|---|
| 契约编号 | AB-03 |
| 双方 | B 单元 (老沈 RM DRAIN 逻辑) ↔ A 单元 (小颜 paper 调度状态机 A-PAPER-02) |
| 内容 | `RmState::DRAIN` 放行条件 = `is_close=true AND side=Sell`。paper 调度状态机复用此条件判断是否允许平仓单进入 VirtualMatcher |
| B 承诺 | DRAIN 放行条件不变；`check_state_()` 逻辑 SSOT = 本 spec §2.2 |
| A 承诺 | paper 调度状态机调用 RM evaluate() 而不是自行判断 DRAIN 条件 (不绕过 RM) |
| 签字状态 | 待老韩 + 老周 W10 签字 |

### §4.4 契约 AB-04 — RiskGateway 接口稳定性 (小卢 REST 并行)

A 单元 (小卢 REST 接真) 需要调用 RM 的以下接口，B 单元承诺这些接口在 v0.5 → v0.6 期间不发生 breaking change:

| 接口 | 签名 | 稳定承诺 |
|---|---|---|
| `evaluate()` | `RiskDecision evaluate(OrderIntent const&) noexcept` | 签名冻结 |
| `set_state()` | `void set_state(RmState) noexcept` | 签名冻结 |
| `set_condition_exposure()` | `void set_condition_exposure(string const&, int64_t) noexcept` | 签名冻结 |
| `set_outcome_exposure()` | `void set_outcome_exposure(string const&, int64_t) noexcept` | 签名冻结 |
| `set_daily_pnl()` | `void set_daily_pnl(int64_t) noexcept` | 签名冻结 |
| `set_bankroll()` | `void set_bankroll(int64_t) noexcept` | 签名冻结 |
| `set_edge_ci_lower()` | `void set_edge_ci_lower(string const&, double) noexcept` | 签名冻结 |
| `set_market_freshness_ms()` | `void set_market_freshness_ms(string const&, uint32_t) noexcept` | 签名冻结 |
| `set_token_book_freshness_ms()` | `void set_token_book_freshness_ms(string const&, uint32_t) noexcept` | 签名冻结 |
| `set_market_state()` | `void set_market_state(string const&, MarketState) noexcept` | 签名冻结 |
| `set_market_active()` | `void set_market_active(string const&, bool) noexcept` | 签名冻结 |

---

## §5 安全审计要点 (老沈专项)

以下是 RM 实施过程中的关键安全控制点，PR 审查时须逐条验证:

### §5.1 私钥不接触 RM 层

- RM evaluate() 不接触钱包私钥，不调用 Signer
- Signer 在 RM APPROVED 之后由 Orchestrator 层调用
- paper 模式: PaperSigner 生成 mock 签名，不持有真私钥

### §5.2 timestamp_ms 生成责任隔离

- timestamp_ms 由 Orchestrator 层填入 (系统时间 ms)
- RM 内部禁止 `std::chrono::now()` 替代 timestamp_ms (这是 Orchestrator 的责任)
- RM 内部 `now_realtime_ns()` 仅用于: (a) PIT 校验参照基准; (b) audit_id ULID 生成
- 违反此规则 = 破坏 R-20 data provenance

### §5.3 fee 计算隔离

- `sports_taker_fee_estimate` 在 evaluate() 内部计算，是局部变量
- fee estimate 不写入 OrderIntent、不写入 SignedOrder、不进 EIP-712
- fee rate (`0.03`) 硬编码常量 `kSportsTakerFeeRate`，不入 RiskConfig (防配置注入攻击)

### §5.4 audit 不可篡改性

- BLAKE3 hash chain: prev_hash → payload_hash → current_hash (stub 阶段: XOR chain，Sprint-3 真算)
- audit 写入后不可修改 (WAL append-only)
- 老唐 audit replay 通过 hash chain 校验完整性

### §5.5 fail-closed 原则

- evaluate() 标注 `noexcept`: 不允许抛异常，内部异常 → INTERNAL_ERROR + emit audit
- emitter 失败 → AUDIT_WAL_BACKPRESSURE (拒单而非通过)
- 任何 NaN / Inf 输入 → INVALID_INTENT / NAN_OR_INF (不降级处理)
- RM 启动默认 `SAFE_MODE` (不是 RUNNING)，人工确认后才允许开单

---

## §6 待实施 delta (B 单元 W10 W1 后续 PR)

以下是本 spec 识别的、当前代码中尚未实现的 delta，供实施参考:

| Delta | 描述 | 优先级 | 实施文件 |
|---|---|---|---|
| D1 | DD 分层: -3% 软熔断 (RiskConfig 新增 daily_loss_soft_pct) | P0 | risk_gateway.hpp + .cpp |
| D2 | check_invalid_intent_: timestamp_ms V2 校验 (R3.6/R3.7/R3.8) | P0 | risk_gateway.cpp |
| D3 | check_invalid_intent_: metadata/builder 格式校验 (R3.9/R3.10) | P0 | risk_gateway.cpp |
| D4 | check_signal_: fee estimate net_edge 校验 (R-fee-2) | P0 | risk_gateway.cpp |
| D5 | InvalidIntentSubReason: TS_V2_MISSING/TS_V2_STALE/TS_V2_FUTURE/INVALID_BYTES32_FORMAT 枚举 | P0 | reject_enum.hpp |
| D6 | emit_audit_: timestamp_ms/metadata/builder 写入 AuditRecord v1.4 | P0 | risk_gateway.cpp |
| D7 | PAPER_LEDGER_GUARD: CMake 守卫防止 paper binary link RiskAudit WalWriter | P0 | CMakeLists.txt |
| D8 | PAPER_LEDGER_GUARD: WalWriter::Open() path prefix 硬校验 abort | P0 | wal_writer.cpp |

注: D2-D6 对应 laohan-rm-v0.5-integration-spec-v1.md §3/§4/§7 实施任务 T1-T6。

---

## §7 引用文件索引

| 文件 | 路径 | 用途 |
|---|---|---|
| OrderIntent v0.6 struct | `include/stcpp/risk/risk_gateway.hpp` L93-173 | 字段冻结基准 |
| RiskGateway 实现 | `src/stcpp/risk/risk_gateway.cpp` | evaluate() 10 规则实现 |
| RejectCode enum | `include/stcpp/risk/reject_enum.hpp` | 拒单 enum 体系 |
| AuditRecord v1.4 | `include/stcpp/observability/audit_record.hpp` | audit schema |
| WalKind + 路径 | `include/stcpp/infra/wal/wal_kind.hpp` | R-11 路径白名单 |
| PositionLedger | `include/stcpp/risk/position_ledger.hpp` | 仓位 read API |
| PaperPolymarketClient | `include/stcpp/polymarket/paper/paper_pm_client.hpp` | paper 模式隔离 |
| SystemState StateMachine | `include/stcpp/risk/system_state.hpp` | DRAIN CAS 状态机 |
| Signal 接口 | `include/stcpp/strategy/signal_iface.hpp` | Outcome/Side enum |
| Kelly 模型 | `docs/RESEARCH/xiaoxiao-kelly-slippage-model-v1.md` | Kelly+CI sizing 理论 |
| 老韩 RM v0.5 spec | `docs/RESEARCH/laohan-rm-v0.5-integration-spec-v1.md` | W10 Wave 99 实施指令 |
| GM 推进指令 | `docs/OKR/laolei-2026-drive-directive-paper-profit-v1.md` | §8.2 契约 + §9 裁决 |

---

**END spec v1.**

**待签字:** 老韩 (B 主管, RM 主权) — 签字后状态转 APPROVED，A 单元 (老陈/小卢) W10 开工。
**Last updated:** 2026-05-29 by 老沈 (B 风控合规部 IC, Wave 104 P0 跟进)
