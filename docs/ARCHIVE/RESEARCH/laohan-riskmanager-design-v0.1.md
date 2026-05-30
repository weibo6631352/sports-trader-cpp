# RiskManager 设计文档 v0.1

- Owner: 老韩 (risk-engineer)
- Last review: 2026-05-28
- 验收人: 小梁 (quant) + 老郭 (high-frequency systems)
- 状态: DRAFT, 待会签
- 适用范围: Sprint-1 MVP (Moneyline 单盘口), 接口前瞻全盘口

---

## 0. 红线声明 (read me first)

**RiskManager 是本系统唯一合法的下单决策点.**

- 任何下单链路 (策略层 → 执行层) **必须**同步经过 `RiskManager::evaluate()`, 拿到 `Decision::APPROVED` 才能去签名.
- 绕过 RiskManager 直接调签名模块 = **P0 事故**, 当事人停职复盘.
- RiskManager **只有一个**实例 (singleton + 启动期注入), 拒绝多实例并存.
- RiskManager 状态变更 (RUNNING/HALTED/...) 必须 audit, 任何状态都不允许"静默放行".
- 拒单理由表 (`RejectReason`) 是**封闭 enum**, 不允许 "OTHER" 兜底, 每个拒绝必须落到一个明确码.

---

## 1. 设计目标 (含红线)

### 1.1 设计目标

| # | 目标 | 度量 |
|---|---|---|
| G1 | 100% 拦截违规下单 | 0 起绕过事故 (年度) |
| G2 | 拒单理由可追溯 | 任意拒单, 30s 内 grep 出 audit 记录 |
| G3 | 决策延迟 P99 ≤ 200μs | 不阻塞执行层热路径 |
| G4 | 接口前瞻全盘口 | MVP Moneyline → 后续 Totals/Spreads/Prop 不改 API |
| G5 | 故障安全 (fail-closed) | 任何内部异常 → 默认 REJECT, 不允许 fail-open |
| G6 | 单笔上限**只能调低**, 不能调高 (运行时) | 硬编码 ceiling + config soft cap |

### 1.2 不在本文档范围

- 策略本身 (edge 估算, 信号生成) → 小梁负责
- 签名 / 链上交互 → 加密签名工程师负责
- WSS / REST 数据采集 → 高频系统老郭负责
- RiskManager **只 enforce**, **不 estimate**

---

## 2. 门禁接口 (C++ class 形状 + 同步调用)

### 2.1 调用契约

**所有下单**只能通过下面这一个同步入口:

```
RiskDecision RiskManager::evaluate(const OrderIntent& intent) noexcept;
```

- **同步** (不允许 async/future), 调用者必须在拿到 `RiskDecision` 之后才能进入签名.
- **noexcept** — RiskManager 内部所有异常自包, 一律转 `REJECT(INTERNAL_ERROR)`.
- 决策必为 `APPROVED | REJECTED | DEFERRED`, **没有第四种**.
- `DEFERRED` 含义: 当前 WARNING 状态, 允许策略层在 N 秒后重试 (用于 stale-data 短抖动), 但不允许"重试到通过".

### 2.2 输入 `OrderIntent` (字段清单, 不含 C++ 实现)

| 字段 | 说明 | MVP 是否必填 |
|---|---|---|
| `intent_id` (UUID v4) | 策略层生成的意图唯一 ID | 必填 |
| `idempotency_key` | 见 §3.8 | 必填 |
| `market_id` | Polymarket condition_id | 必填 |
| `market_type` | enum: MONEYLINE / TOTAL / SPREAD / PROP / OUTRIGHT / PERIOD / HALF / SERIES | 必填, MVP 只接 MONEYLINE |
| `side` | BUY_YES / BUY_NO | 必填 |
| `price` | 限价 (0,1), Polymarket 单位 | 必填 |
| `size_usdc` | 策略层**请求**的下注额 (USDC) | 必填 |
| `edge_bps` | 策略层估算 edge (bps), 用于 Kelly 校验 | 必填 |
| `edge_ci_low_bps` | edge 置信区间下界 | 必填 (见 §3.2) |
| `signal_ts_ns` | 信号生成时间戳 | 必填 |
| `data_freshness_ms` | 报价数据距 now 的延迟 | 必填 |
| `strategy_tag` | 策略标识 (审计) | 必填 |

### 2.3 输出 `RiskDecision`

| 字段 | 说明 |
|---|---|
| `decision` | APPROVED / REJECTED / DEFERRED |
| `approved_size_usdc` | 经过 sizing 收敛后的最终下注额 (≤ 请求值) |
| `reject_code` | `RejectReason` enum, REJECTED 时必填 |
| `reject_detail` | 人类可读字符串 (audit 用, 不参与逻辑) |
| `audit_id` | 当次评估的 audit 记录 ID, 必填 (含 APPROVED) |
| `evaluated_at_ns` | 决策时刻 |
| `state_snapshot` | 决策时 RiskManager 状态 (RUNNING/WARNING/...) |

**注:** 即使 APPROVED, `approved_size_usdc` 也可能 < `intent.size_usdc` (sizing 收敛). 调用方必须用 `approved_size_usdc` 去签名, 不允许用原值.

### 2.4 调用方式约束

- 调用方**禁止**自行做"预审" (例如本地判断 bankroll), 任何看似冗余的检查都必须 round-trip 进 RiskManager.
- 调用方**禁止**捕获 `RiskDecision` 之后篡改 `approved_size_usdc`.
- 一次 evaluate 对应一次 audit 记录, 不允许"先 evaluate 再 evaluate 第二次取最优".

---

## 3. 规则集 (10 条核心规则详述)

规则之间是**短路 AND**: 任何一条 REJECT, 立即返回, 后续不评估.
求值顺序按下表 (从便宜到贵):

| 顺序 | 规则 | 拒单码 | 备注 |
|---|---|---|---|
| R0 | 状态闸门 | `STATE_HALTED` | 见 §4 |
| R1 | 幂等检查 | `DUPLICATE_INTENT` | §3.8 |
| R2 | 数据新鲜度 | `STALE_DATA` | §3.7 |
| R3 | 输入合法性 | `INVALID_INTENT` | 价格 ∈ (0,1), size > 0, 必填字段非空 |
| R4 | 单笔硬上限 | `EXCEED_PER_ORDER_CAP` | §3.3 |
| R5 | 单市场敞口 | `EXCEED_MARKET_EXPOSURE` | §3.4 |
| R6 | 日内亏损熔断 | `DAILY_LOSS_HALT` | §3.6 |
| R7 | 连续亏损熔断 | `CONSEC_LOSS_HALT` | §3.5 |
| R8 | Kelly sizing | (不拒, 只**收敛** size) | §3.2 |
| R9 | bankroll 充足性 | `INSUFFICIENT_BANKROLL` | 收敛后再校验 |

### 3.1 通用变量

- `B` = bankroll (USDC, 实时, 由资金对账模块同步, 见 §6)
- `E` = edge_bps (策略层提供)
- `E_low` = edge_ci_low_bps (置信区间下界)
- `p` = price (Polymarket 价格, 等价于隐含概率)
- 对 Polymarket binary outcome: BUY_YES @ p, 赔率 `b = (1-p)/p` (赔 b 赚 1, 失 1)

### 3.2 Kelly 公式实现

**保守 Kelly + CI 下界 + 分数因子.**

设策略对 YES 的真实概率估计 `q_hat`, edge `E = q_hat - p`. 我们**不直接用 `E`**, 而用 **`E_low` (置信区间下界)** 做 sizing, 这是公司硬规矩.

记 `q_low = p + E_low / 10000` (`E_low` 为 bps).

- 若 `q_low <= p` (CI 下界穿越 0) → **size = 0** (不下), 返回 `APPROVED` 但 `approved_size_usdc = 0` (策略层视为软拒, 不会签名).
  - 备选: 单独走 `EDGE_CI_NEGATIVE` reject code, 更清晰. **待会签 @小梁.**

- 否则:
  - `b = (1-p) / p`
  - `f_kelly = (b * q_low - (1 - q_low)) / b`   (经典 Kelly, 用 q_low 代替 q_hat)
  - `f_used = f_kelly * KELLY_FRACTION`  (KELLY_FRACTION 是分数因子, 见 §7)
  - `size_kelly = f_used * B`
  - `approved_size = min(intent.size_usdc, size_kelly, PER_ORDER_CAP, MARKET_REMAINING_CAP)`

**疑问 @小梁:**
- Q1: `KELLY_FRACTION` 初值用 0.25 还是 0.5? Polymarket 流动性薄, 我倾向 0.25, 你拍.
- Q2: edge 置信区间是策略层给, 还是 RiskManager 自己根据成交簿厚度再缩水一次? MVP 我先信你给的.
- Q3: 跨市场相关性 (例如同一比赛 Moneyline + Spread) 的 Kelly 联合优化, 不在 v0.1, 留到 v0.3.

**疑问 @小肖 (撮合/盘口):**
- Q4: Polymarket 滑点对实际 fill price 的影响, Kelly 用 quote price 还是 expected fill price? MVP 我先用 quote, 后续若 slip 显著再修.

### 3.3 单笔最大下注额 (硬上限)

- **`PER_ORDER_CAP_HARD`** = 代码中 `constexpr`, 编译期常量, **运行时不可改**.
- **`PER_ORDER_CAP_SOFT`** = config 可调, 启动时校验 `SOFT <= HARD`, 否则启动失败.
- 运行时只读 `min(HARD, SOFT)`.
- 调低: 允许, 立即生效 (热 reload config).
- 调高: **禁止** — config 只允许往下走, watcher 检测到调高 → 拒绝 reload + 报警.

**初值 (待会签):**
- `PER_ORDER_CAP_HARD` = ?  → 见 §7
- `PER_ORDER_CAP_SOFT` = ?  → 见 §7

### 3.4 单市场最大敞口

- 同一 `market_id` (跨 side, 跨 outcome) 的累计**净持仓**不超过 `MARKET_EXPOSURE_PCT * B`.
- "净持仓"定义: BUY_YES 持仓 + BUY_NO 持仓 的 mark-to-market USDC 总和 (不抵消, 取保守值).
- 跨市场但同 game_id 的累计敞口: **v0.1 不管**, 留 v0.2. (@小梁: 你定义 game-level cap)
- 计算时机: evaluate 调用时**实时**查头寸账本 (positions ledger), 不允许用缓存超过 5s 的快照.

**初值 (待会签):** `MARKET_EXPOSURE_PCT` = ? (我建议 2%, @小梁)

### 3.5 连续亏损熔断

- 滚动窗口: 最近 `CONSEC_LOSS_WINDOW` 笔**已结算**订单 (不含未结算).
- 触发条件: 连续 `CONSEC_LOSS_N` 笔 PnL < 0 → 状态 RUNNING → WARNING, 触发后 `CONSEC_LOSS_N + K` 笔 → HALTED.
- 恢复: HALTED 状态需人工 ack (CLI 命令 + 双人复核 audit), 不自动恢复.
- "已结算"定义: Polymarket 市场 resolve 之后的最终 PnL, 不用 mark-to-market.

**初值 (待会签):**
- `CONSEC_LOSS_N` = ? (建议 5)
- `CONSEC_LOSS_WINDOW` = ? (建议 20)

### 3.6 日内最大亏损 (硬熔断)

- 日界: UTC 00:00 滚动.
- 度量: 当日 realized PnL + 当日 unrealized PnL 变动 (mark-to-market).
- 阈值: `DAILY_LOSS_PCT * B_day_open` (B_day_open 是当日 00:00 的 bankroll 快照).
- 触发: **立即** RUNNING/WARNING → HALTED, 当日不再开仓, 仅允许平仓 (DRAIN 模式).
- 恢复: 跨日自动重置, 但需在新日开盘前由人 (我 + 老雷) 双 ack.

**初值 (待会签):** `DAILY_LOSS_PCT` = ? (建议 3%, @小梁)

### 3.7 数据源异常保护

- 监控对象 (每个独立计算 freshness):
  - Polymarket clob WSS (盘口)
  - Goalserve inplay (赛况)
  - 资金对账 (positions ledger sync)
- 单源 freshness `> STALE_THRESHOLD_MS` → 状态 RUNNING → WARNING, evaluate 返回 `DEFERRED`.
- 单源 freshness `> STALE_HALT_MS` → 状态 → HALTED, evaluate 返回 `REJECTED(STALE_DATA)`.
- 来源: `OrderIntent.data_freshness_ms` (策略层告知) **+** RiskManager 自己订阅 heartbeat (双重保险, 不信单方).

**初值 (待会签, @老郭 跨洋链路你比我清楚):**
- `STALE_THRESHOLD_MS` = ? (建议 30000ms, 即 30s)
- `STALE_HALT_MS` = ? (建议 60000ms)

### 3.8 幂等防重 (idempotency key)

**设计目标:** 同一个"决策意图"重复提交 (网络重试 / 策略层 bug / 进程崩溃重启) 不允许变成两笔下单.

**Key 构造规则 (策略层生成, RiskManager 校验):**

```
idempotency_key = sha256(
    strategy_tag || market_id || side || price_bucket ||
    signal_ts_bucket_ns || size_bucket_usdc
)
```

- `price_bucket`: price 量化到 0.001 网格 (避免浮点不稳)
- `signal_ts_bucket_ns`: signal_ts_ns 量化到 100ms 桶
- `size_bucket_usdc`: size_usdc 量化到 1 USDC

**RiskManager 行为:**
- 维护 `seen_keys` 集合 (LRU + TTL 24h, 持久化到 sqlite, 进程重启不丢).
- evaluate 第一步 (R1): 若 key 已存在 → `REJECTED(DUPLICATE_INTENT)`, 返回上次的 `audit_id` 引用.
- 若 key 新 → 入库, 继续评估.

**与 Polymarket 订单 client_order_id 的关系:**
- 上游 idempotency_key (RiskManager 层) ≠ Polymarket client_order_id (执行层).
- 执行层从 RiskManager 的 audit_id 派生 client_order_id, 一对一.

### 3.9 bankroll 充足性

- 收敛后 `approved_size + 已锁定保证金 + 在途订单 <= B * (1 - SAFETY_BUFFER)`.
- `SAFETY_BUFFER` = 5% (待会签).
- 不足 → `REJECTED(INSUFFICIENT_BANKROLL)`.

### 3.10 总结表 — 拒单原因 enum (封闭集合)

| code | 含义 | 触发规则 |
|---|---|---|
| `STATE_HALTED` | RiskManager 全局停盘 | R0 |
| `STATE_DRAIN` | 仅允许平仓, 拒绝开仓 | R0 |
| `DUPLICATE_INTENT` | 幂等命中 | R1 |
| `STALE_DATA` | 数据源停摆 | R2 |
| `INVALID_INTENT` | 字段不合法 | R3 |
| `EXCEED_PER_ORDER_CAP` | 请求超单笔上限 (且 sizing 收敛后仍超) | R4 |
| `EXCEED_MARKET_EXPOSURE` | 单市场敞口爆表 | R5 |
| `DAILY_LOSS_HALT` | 日亏熔断 | R6 |
| `CONSEC_LOSS_HALT` | 连亏熔断 | R7 |
| `EDGE_CI_NEGATIVE` | edge CI 下界 ≤ 0 (sizing → 0) | R8 (待会签是否单列) |
| `INSUFFICIENT_BANKROLL` | 资金不足 | R9 |
| `MARKET_TYPE_NOT_ENABLED` | MVP 只开 MONEYLINE, 其他直接拒 | R3 子项 |
| `INTERNAL_ERROR` | RiskManager 内部异常 (fail-closed) | 全局兜底 |

**封闭性:** 不允许 `OTHER`. 新增拒因 = 改 enum + bump 版本 + audit 字段兼容.

---

## 4. 状态机

### 4.1 状态定义

| 状态 | 含义 | 允许的 evaluate 结果 |
|---|---|---|
| `RUNNING` | 正常 | APPROVED / REJECTED (业务规则) |
| `WARNING` | 监控指标偏离, 但还能下单 | APPROVED (收紧 sizing) / REJECTED / DEFERRED |
| `HALTED` | 全局停盘 | REJECTED(STATE_HALTED) — 一律 |
| `DRAIN` | 只允许平仓 (close-only) | 平仓单 APPROVED, 开仓单 REJECTED(STATE_DRAIN) |

### 4.2 状态转移

```
[启动] → RUNNING

RUNNING --(stale 30s / consec loss N)--> WARNING
WARNING --(stale 60s / consec loss N+K / daily loss hit)--> HALTED
WARNING --(指标恢复 + 持续 5min OK)--> RUNNING
RUNNING --(日亏命中)--> HALTED   (跳过 WARNING)
HALTED --(人工 ack: 双人复核)--> RUNNING | DRAIN
DRAIN --(人工 ack)--> RUNNING | HALTED
任何状态 --(SIGTERM / 紧急)--> HALTED   (优雅停)
```

### 4.3 状态变更规则

- 状态变更**只允许**在 RiskManager 主线程, 通过状态机事件队列.
- 每次状态变更必落 audit (`STATE_TRANSITION` event), 含 from/to/trigger/operator.
- HALTED → 任何状态: **必须**双人 ack, audit 双签.
- 状态机自身**没有** "AUTO_RESUME" 从 HALTED → RUNNING, 永远要人.

---

## 5. Audit log schema

### 5.1 存储

- 主存: append-only log file (jsonl), 按日切, 同步 fsync 每 N 条 (N=10, @老郭 你定 N).
- 异地备份: 每小时打包上传 (S3 / 等价). v0.1 先本地, 备份 v0.2.
- 索引: sqlite 单表 + 主键 audit_id + 二级索引 (intent_id, market_id, decision, evaluated_at).

### 5.2 字段 (per evaluate)

```
audit_id            : ULID (排序友好, 全局唯一)
schema_version      : "v0.1"
evaluated_at_ns     : i64
intent_id           : UUID
idempotency_key     : hex32
strategy_tag        : str
market_id           : str
market_type         : enum
side                : enum
requested_size_usdc : decimal
approved_size_usdc  : decimal
price               : decimal
edge_bps            : i32
edge_ci_low_bps     : i32
data_freshness_ms   : i32
bankroll_snapshot   : decimal
market_exposure_now : decimal
state_before        : enum
state_after         : enum
decision            : enum (APPROVED / REJECTED / DEFERRED)
reject_code         : enum | null
reject_detail       : str | null
rule_trace          : [str]   // 命中的规则顺序, 调试用
latency_us          : i32
```

### 5.3 不变量

- 一次 evaluate 必产 1 条 audit, **不多不少**.
- audit 写入失败 → evaluate 必须返回 `REJECTED(INTERNAL_ERROR)` (fail-closed).
- audit 必须包含**当时**的 bankroll/exposure 快照, 不允许"事后查询" — 取证完整性.

### 5.4 状态变更 audit (单独 event 类型)

```
audit_id, schema_version, ts_ns, type="STATE_TRANSITION",
state_from, state_to, trigger_code, operator (人工 ack 时填), note
```

---

## 6. 与上下游交互

### 6.1 上游 (策略层 — 小梁)

- 策略层产生 `OrderIntent`, **必须**填齐 §2.2 所有字段.
- 策略层**禁止**自行解释 `DEFERRED` 为"重试到成功", DEFERRED 最多重试 1 次 (退避 2s).
- 策略层**禁止**绕过 RiskManager 调签名模块. CI 静态扫描 + code review 双保险.

### 6.2 下游 (执行层 / 签名模块)

- 拿到 `RiskDecision.decision == APPROVED` 且 `approved_size_usdc > 0` 才能签名.
- 必须用 `approved_size_usdc` (不是 `intent.size_usdc`).
- 必须把 `audit_id` 透传到 Polymarket client_order_id (派生关系), 出问题能反查.
- 成交回报 (fill) 回灌 RiskManager 的 positions ledger, 用于 §3.4 / §3.5 / §3.6 实时计算.

### 6.3 资金对账模块

- bankroll `B` 来自资金对账, 实时性要求: 滞后 ≤ 5s.
- RiskManager 自己**不**做链上余额查询, 只消费对账输出.
- 对账失联 (> 30s) → STALE_DATA → WARNING/HALTED.

### 6.4 监控/告警

- 状态变更 → 推 webhook (Slack/电话).
- WARNING 持续 > 1min → 报警.
- HALTED → 立即电话 (我 + 老雷).

---

## 7. 参数初值表 (待会签)

**法律效力:** 本表是 RiskManager 的真值源. 表外的参数 = 不存在.

| 参数 | 类型 | 我的建议 | 待会签 | 调整规则 |
|---|---|---|---|---|
| `PER_ORDER_CAP_HARD` | USDC | **TBD** @小梁 | 必签 | constexpr, 不可改 |
| `PER_ORDER_CAP_SOFT` | USDC | **TBD** @小梁 | 必签 | config, 只可调低 |
| `MARKET_EXPOSURE_PCT` | % | 2% | @小梁 | 只可调低 |
| `KELLY_FRACTION` | ratio | 0.25 | @小梁 | 只可调低 |
| `DAILY_LOSS_PCT` | % | 3% | @小梁 | 只可调低 |
| `CONSEC_LOSS_N` | int | 5 | @小梁 | 只可调低 (更敏感) |
| `CONSEC_LOSS_WINDOW` | int | 20 | @小梁 | — |
| `STALE_THRESHOLD_MS` | ms | 30000 | @老郭 | — |
| `STALE_HALT_MS` | ms | 60000 | @老郭 | — |
| `SAFETY_BUFFER` | % | 5% | @小梁 | 只可调高 (更保守) |
| `IDEMPOTENCY_TTL_HOURS` | h | 24 | — | — |
| `KELLY_FRACTION_WARNING` | ratio | 0.5 × KELLY_FRACTION (WARNING 状态再缩半) | @小梁 | — |

**注:** "只可调低/调高"语义指**朝更保守方向**单调. config watcher 拒绝反向调整.

---

## 8. 实现注意事项 (lock 设计 / 性能预算)

### 8.1 并发模型

- RiskManager **单线程**主循环 + lock-free SPSC 队列接收 evaluate 请求.
- evaluate 调用方阻塞等待结果 (future), 实际计算在 RiskManager 线程.
- 这是 fail-closed + audit 完整性的代价, 不接受"为了延迟把规则放到 caller 线程"的提议.
- 性能预算: P99 ≤ 200μs (从入队到拿到 decision). MVP 单线程能撑住, QPS < 1000 (Polymarket 体育撑死了).

### 8.2 内部数据结构

- `positions_ledger`: 内存 hashmap, market_id → position 聚合, evaluate 时只读.
  - 写入路径在执行层 fill 回灌, MPSC 队列推入 RiskManager.
- `seen_keys` (idempotency): in-memory LRU + sqlite WAL.
- `state`: atomic enum, 只 RiskManager 线程写, 其他线程只读.

### 8.3 时钟

- 全用 `CLOCK_MONOTONIC_RAW` 做相对时延.
- 用 NTP-sync 的 `CLOCK_REALTIME` 做 audit 时间戳 (有 jitter 但跨进程对齐方便).
- @老郭 跨洋链路 NTP 漂移问题, 麻烦你给个误差预估.

### 8.4 启动期校验

- 启动时跑一遍 self-check:
  1. config 加载 → 与 HARD ceiling 对比, 任何 SOFT > HARD → 启动失败.
  2. audit log 可写 → 否则启动失败.
  3. sqlite idempotency table 可读 → 否则启动失败.
  4. 资金对账模块连通 → 否则启动 = HALTED 而非 RUNNING.
- 启动失败 = 进程退出, 不允许"降级启动".

### 8.5 热 reload

- config 文件 inotify 监听.
- 变更 → diff → 校验单调性 → 应用 (atomic swap).
- 任何不合规 diff → 拒绝 + 报警, 沿用旧 config.

---

## 9. 测试策略 (与小宋协同点)

### 9.1 单元测试 (我自己写)

- 每条规则独立 case, 边界值 + off-by-one.
- Kelly 公式: 已知 q/p/b 验证 f_kelly 数值.
- idempotency: 同 key 二次 evaluate 必拒 + 返回首次 audit_id.
- 状态机: 所有转移路径覆盖.
- fail-closed: mock 内部异常, 必返 REJECT(INTERNAL_ERROR).

### 9.2 集成测试 (与小宋 @qa)

- **黑盒契约:** evaluate API 输入输出对照表 (csv 驱动), 小宋拉清单.
- **回归:** 历史 audit log replay, 同一 intent 序列必产同一 decision 序列 (相同 config + 相同 bankroll).
- **混沌:** 注入 stale data / bankroll 抖动 / config 热改 → 状态机表现.
- **绕过检测:** 写一个静态扫描脚本, grep 签名模块调用点, 必须前序调用 evaluate. CI 拦截.
- **压测:** 1000 QPS 持续 1 小时, 验证 P99 延迟 + audit 不丢条.

### 9.3 影子模式 (与老雷)

- 接入实盘前, 跑 2 周影子: RiskManager 评估真实信号, 但 decision 不下单, 只 audit.
- 复盘: 拒单率分布, 是否过严 / 过松.

### 9.4 演练

- 每月一次"红线演练": 模拟绕过, 看 CI/扫描/audit 能否抓到. 不通过 → 我负全责.

---

## 10. 开放问题 + 待会签项

### 10.1 待会签

| # | 项 | 找谁 |
|---|---|---|
| Q1 | KELLY_FRACTION 初值 | 小梁 |
| Q2 | edge CI 下界穿 0 是单列拒因还是 size=0 软放行 | 小梁 |
| Q3 | edge CI 是否需要 RiskManager 二次缩水 | 小梁 |
| Q4 | Kelly 用 quote price 还是 expected fill | 小肖 |
| Q5 | `PER_ORDER_CAP_HARD` / `SOFT` 数值 | 小梁 + 老雷 |
| Q6 | `MARKET_EXPOSURE_PCT` | 小梁 |
| Q7 | `DAILY_LOSS_PCT` | 小梁 |
| Q8 | `CONSEC_LOSS_N` / `WINDOW` | 小梁 |
| Q9 | stale 阈值 (跨洋链路实测) | 老郭 |
| Q10 | NTP 漂移误差预估 | 老郭 |
| Q11 | audit fsync 频率 N | 老郭 |
| Q12 | 影子模式时长 | 老雷 |
| Q13 | 双人 ack 的"双人"具体是谁 | 老雷 |

### 10.2 v0.1 不做, 留 v0.2+

- 跨市场相关性 (game-level cap)
- 多账户 / 多 wallet 分账户风控
- 异地 audit 备份 (v0.1 本地)
- prop / outright / period 盘口启用 (接口预留, MVP 不开)
- 实时 PnL 归因 (区分 alpha / slippage / fee)
- 动态 Kelly fraction (按市场厚度自适应)

### 10.3 已知风险 (我自己列, 不藏)

- **R-1 (高):** Polymarket 流动性薄, Kelly 收敛后实际 fill 可能远低于 approved_size, 导致"以为下了 X, 实际下了 0.3X". v0.1 用 quote price, 后续需引入 slippage model.
- **R-2 (中):** 跨洋链路抖动可能频繁触发 STALE → DEFERRED → DEFERRED, 实际机会全错过. 阈值需要老郭实测后微调.
- **R-3 (中):** audit log fsync 是性能瓶颈, N 选大了丢数据风险, 选小了延迟超预算. 需老郭压测后定.

---

## 附录 A — 与现有代码库对齐

- 当前 repo 主线在 `main`, 无 RiskManager 实现.
- 本设计是 v0.1, 实现 PR 命名: `feat(risk): RiskManager skeleton v0.1`, 拆 4 个 PR (skeleton / rules / audit / state-machine).
- ADR 同步立项: `docs/ADR/ADR-XXX-riskmanager-as-sole-gate.md` (待开).

---

**END v0.1.** 等小梁 + 老郭 + 老雷过一遍, 我再 bump v0.2.
