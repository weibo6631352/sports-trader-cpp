# RiskManager 设计 v0.2 (修 ADR-001 C-H1..C-H6)

- Owner: 老韩 (risk-engineer)
- Last review: 2026-05-28
- 验收人: 老郭 (24h sign-off) + 小梁 (参数会签)
- 关联:
  - `docs/ADR/2026-05-28-arch-and-rm-v0.1-review.md` (ADR-001)
  - `docs/ADR/2026-05-28-gm-signoff-adr-001.md` (GM sign-off)
  - 旧版: `docs/RESEARCH/laohan-riskmanager-design-v0.1.md` (保留, 不替换)
  - 同 Sprint 对齐:
    - `docs/RESEARCH/laozhou-architecture-v0.1.md` (§9.2 SAFE_MODE 路径)
    - `docs/RESEARCH/laowu-cross-region-deployment-v0.1.md` (us-east-1 c6i.xlarge)
    - `docs/RESEARCH/laojiang-latency-budget-v1.md` (内环 p99 预算 / 信号 → 下单)
    - laosun-key-management v1 (本地 signer 50us 参考; Rust 版已删, 当前 SSOT: v5.1)

---

## 0. v0.1 → v0.2 变更摘要 + C-H 修复对照表

### 0.1 关键变更摘要 (一行话版)

| 域 | v0.1 立场 | v0.2 新立场 | 触发原因 |
|---|---|---|---|
| 部署假设 | 跨洋 (隐含 RTT 100ms+) | us-east-1 同区, RTT < 10ms | ADR-001 §3.2 + GM W-6 批 c6i.xlarge |
| STALE 阈值 (WSS) | 30s/60s | **2s WARNING / 10s HALT** | ADR-001 §3.2 / D-06 |
| STALE 阈值 (Goalserve) | 30s/60s | **5s WARNING / 15s HALT** | 同上 |
| STALE 阈值 (对账) | 30s/60s | **10s WARNING / 30s HALT** | 同上, 30s 是 D-06 硬上限 |
| audit fsync | 同步 fsync 每 N=10 条 | **SPSC ring → 背景 fsync 线程, batch=64 OR 1ms** | ADR-001 §3.3 |
| WAL 数量 | 1 个 (audit + idempotency 混用) | **2 条独立 WAL** (audit / position+nonce) + sqlite 独立 | ADR-001 §3.3.5 |
| EDGE_CI_NEGATIVE | 待会签 (size=0 软放行 vs 单列) | **单列 enum**, REJECTED | ADR-001 F-6 |
| sqlite (idempotency) | `PRAGMA synchronous=FULL` 隐含 | **`NORMAL` + WAL mode + 60s checkpoint** | ADR-001 F-7 |
| 崩溃恢复 | 未明示 | **默认进 SAFE_MODE, 显式 unlock 才解锁** | GM W-3 红线 |
| RM 主线程 | 单线程 + SPSC | **事件循环, `fill_in` > `intent_in` 优先级** | ADR-001 F-9 |
| 状态转移副作用 | §4.2 隐含 | **§4.3 副作用表显式化** | ADR-001 F-8 |

### 0.2 C-H1..C-H6 修复对照表

| # | ADR-001 整改项 | v0.2 落位章节 | 完成度 | 备注 |
|---|---|---|---|---|
| **C-H1** | §3.7 STALE 阈值改 (2s/5s/10s WARNING + 10s/15s/30s HALT) | §3.7 + §11 (新增专章) | DONE (设计) | 实测校准延 S1-021 (@老陈 RTT 数据出), 见 §14 R-2 |
| **C-H2** | §5.1 audit fsync 改 group commit + WAL | §5.1 (改写) + §12 (新增专章) | DONE (设计), 实现 Sprint-2 | WAL framework 实现 @老王 (persistence), 见 §12.6 |
| **C-H3** | §3.2 + §3.10 EDGE_CI_NEGATIVE 单列 enum 拒因 | §3.2 + §3.10 | DONE | enum 加 `EDGE_CI_NEGATIVE`, R8 现在可 REJECT (不再只是 size=0) |
| **C-H4** | §3.8 sqlite PRAGMA 配置 + 崩溃恢复语义 | §3.8 (扩写) | DONE | `synchronous=NORMAL` + WAL mode + 60s checkpoint |
| **C-H5** | §4.2 状态转移副作用表 | §4.3 (新增) | DONE | 列每个状态对 (KELLY_FRACTION / DEFERRED / 取消未成交) 影响 |
| **C-H6** | §8.1 RM 事件循环明示 `fill_in` 优先级 | §8.1 (改写) | DONE | 单线程 event loop, `fill_in` 高优, 每次 evaluate 前 drain |

**Hard block 项** (GM 红线 + ADR W-2/W-3/C-H1):
- C-H1: 部署前置项, 阈值不到位禁止 merge → v0.2 已落.
- W-3 (SAFE_MODE 默认): §13 已落.
- W-2 (RiskGateway link 阻断): 不属 RM 内部, 由老周 v0.2 + CMake / CI 落地, 本文 §2.4 引用.

---

## 1. 设计目标 (含红线) — 沿用 v0.1, 补 G7/G8

### 1.1 设计目标 (扩展)

| # | 目标 | 度量 |
|---|---|---|
| G1 | 100% 拦截违规下单 | 0 起绕过事故 (年度) |
| G2 | 拒单理由可追溯 | 任意拒单, 30s 内 grep 出 audit 记录 |
| G3 | 决策延迟 p99 ≤ 200us | 不阻塞执行层热路径; 同步路径 audit 部分 ≤ 6us |
| G4 | 接口前瞻全盘口 | MVP Moneyline → 后续 Totals/Spreads/Prop 不改 API |
| G5 | 故障安全 (fail-closed) | 任何内部异常 → 默认 REJECT |
| G6 | 单笔上限只能调低, 不能调高 (运行时) | constexpr ceiling + config soft cap |
| **G7 (v0.2 新增)** | audit WAL 不丢条 (RPO = 0 逻辑持久) | SPSC append OK = 视为持久; 满 = fail-closed REJECT |
| **G8 (v0.2 新增)** | 崩溃后默认 SAFE_MODE | 重启不自动开仓; 人工 unlock + 对账完成 5min 才放行开仓 |

### 1.2 不在本文档范围

(沿用 v0.1)
- 策略本身 → 小梁
- 签名 / 链上 → 老孙 / 老叶
- WSS / REST 采集 → 老李 / 小董
- WAL framework 实现 → 老王 (老韩定接口, 老王实现存储层)
- Kelly slippage model → 小肖 (v0.3, 见 §14 R-1)

---

## 2. 门禁接口 (RiskGateway, 唯一对外符号)

### 2.1 调用契约 — 沿用 v0.1 §2.1

`evaluate()` 同步, `noexcept`, 决策三态 `APPROVED | REJECTED | DEFERRED`.

### 2.2 输入 `OrderIntent` — 沿用 v0.1 §2.2

无变化.

### 2.3 输出 `RiskDecision` — 沿用 v0.1 §2.3

无变化, 但 `audit_id` 字段语义强化:
- v0.1: "audit 记录 ID, 必填".
- **v0.2**: "audit_id 在 evaluate 同步路径生成 (ULID), 调用方拿到即视为 audit 已逻辑持久 (G7); 物理 fsync 可能滞后 ≤ 1ms, 由背景线程完成 (§12)".

### 2.4 调用方式约束 + RiskGateway 与外部边界

- 沿用 v0.1 §2.4.
- 增补 (引用 ADR-001 §2.2): `risk` 库**只** export `RiskGateway::evaluate()` 单符号. `RiskGateway::halt_state()` / `metrics_snapshot()` 走只读旁路接口, 不在 evaluate 路径.
- **build / CI / runtime 三层防御** 由老周 v0.2 + 老练 CI 落地, 本文不重复, 关联引用 ADR-001 §2.2.

---

## 3. 规则集 — 沿用 v0.1, 修订 §3.2 / §3.7 / §3.8 / §3.10

### 3.1 通用变量 — 沿用 v0.1

### 3.2 Kelly 公式实现 (修订: EDGE_CI_NEGATIVE 单列)

**v0.2 变更:**

`q_low = p + E_low / 10000` 计算后:

- **若 `q_low <= p` (CI 下界穿越 0) → `REJECTED(EDGE_CI_NEGATIVE)`** (v0.1 "APPROVED 但 size=0 软放行" 改为单列拒因).
- 否则按 v0.1 公式继续 (`b = (1-p)/p`, `f_kelly`, `f_used = f_kelly * KELLY_FRACTION`, `approved_size = min(...)`).

**理由 (ADR-001 F-6)**:
- 审计可读性: rule_trace `rejected EDGE_CI_NEGATIVE` 一目了然, 不需读 audit detail 才知道是"数据问题"还是"edge 问题".
- 策略层 DEFERRED 重试逻辑清晰: edge CI 穿 0 重试无意义 (不是 stale 问题), 应等下一个信号.
- 运行时开销 0.

**仍待会签 @小梁:**
- Q1: KELLY_FRACTION 初值 (建议 0.25, 待会签)
- Q3 / Q4: edge CI 是否需 RM 二次缩水, slippage 怎么进 sizing → 见 §14 R-1, 留 v0.3.

### 3.3 单笔最大下注额 — 沿用 v0.1

### 3.4 单市场最大敞口 — 沿用 v0.1

### 3.5 连续亏损熔断 — 沿用 v0.1

### 3.6 日内最大亏损 — 沿用 v0.1

### 3.7 数据源异常保护 (修订: STALE 阈值收紧, D-06 红线遵守)

**v0.2 阈值表** (替换 v0.1 30s/60s):

| 数据源 | DEFERRED / WARNING 阈值 | REJECT / HALT 阈值 | 说明 |
|---|---|---|---|
| Polymarket clob WSS | **2,000 ms** | **10,000 ms** | 心跳 < 1s, 2s 已可疑, 10s HALT |
| Goalserve inplay | **5,000 ms** | **15,000 ms** | poll 1s, 5 次 miss → WARNING, 15s 死透 |
| 资金对账 (positions ledger) | **10,000 ms** | **30,000 ms** | 30s = D-06 红线硬上限, 不可上调 |

**触发行为** (与 v0.1 相同, 只是阈值变):

- 单源 freshness > WARNING 阈值 → 状态 RUNNING → WARNING, evaluate 返回 `DEFERRED`.
- 单源 freshness > HALT 阈值 → 状态 → HALTED, evaluate 返回 `REJECTED(STALE_DATA)`.

**v0.1 错误纠正:**
- v0.1 §3.7 把 D-06 的 30s "数据无更新自动暂停" 当 WARNING 阈值, HALT 推到 60s, **违反 D-06 红线** (ADR-001 §3.2 已点名).
- v0.2: HALT ≤ 30s 是系统级硬上限, 任何数据源 HALT 阈值都不能超过 30s.

**完整触发动作矩阵见 §11.**

### 3.8 幂等防重 (修订: sqlite PRAGMA + 崩溃恢复)

**v0.2 变更:**

- sqlite 配置 (落入 RM 启动期):
  - `PRAGMA journal_mode = WAL` (sqlite 内部 WAL, 与 audit WAL 无关)
  - `PRAGMA synchronous = NORMAL` (不是 FULL)
  - `PRAGMA wal_autocheckpoint = 1000` (默认 1000 页 ≈ 4MB)
  - 显式 checkpoint 周期: **60s 一次** (后台线程 trigger).

**崩溃恢复语义:**
- 崩溃后 sqlite WAL replay 可能丢失最后 ≤ 60s 的 idempotency key 入库.
- **风险评估**: 重启后 60s 内若策略层重提同 key, 会被放行二次评估 → 但**下游链上 nonce 不会复用** (老孙 v1 §2.1 + 老周 §9.4: nonce WAL fsync 在递增前), 物理上不会下两次单.
- 即, idempotency 层崩溃容忍是"逻辑去重退化为"近似去重"≤ 60s", 物理去重由 nonce manager 兜底.

**为何不走 `synchronous = FULL`:**
- FULL 每个 evaluate 多一次 fsync, 直接吃 200us 预算, 与 audit WAL fsync 路径冲突 (ADR-001 F-7).
- NORMAL 性能优, 崩溃风险由 nonce manager 双保险吃下.

### 3.9 bankroll 充足性 — 沿用 v0.1

### 3.10 拒单原因 enum (修订: EDGE_CI_NEGATIVE 正式入表)

| code | 含义 | 触发规则 |
|---|---|---|
| `STATE_HALTED` | 全局停盘 | R0 |
| `STATE_DRAIN` | 仅平仓 | R0 |
| `STATE_SAFE_MODE` (v0.2 新) | 崩溃后默认状态, 只撤不开仓 | R0, 见 §13 |
| `DUPLICATE_INTENT` | 幂等命中 | R1 |
| `STALE_DATA` | 数据停摆 | R2 |
| `INVALID_INTENT` | 字段不合法 | R3 |
| `EXCEED_PER_ORDER_CAP` | 超单笔上限 | R4 |
| `EXCEED_MARKET_EXPOSURE` | 单市场敞口爆 | R5 |
| `DAILY_LOSS_HALT` | 日亏熔断 | R6 |
| `CONSEC_LOSS_HALT` | 连亏熔断 | R7 |
| `EDGE_CI_NEGATIVE` **(v0.2 单列)** | edge CI 下界 ≤ 0 | R8 |
| `INSUFFICIENT_BANKROLL` | 资金不足 | R9 |
| `MARKET_TYPE_NOT_ENABLED` | MVP 只开 MONEYLINE | R3 子项 |
| `AUDIT_WAL_BACKPRESSURE` **(v0.2 新)** | audit WAL SPSC ring 满, fail-closed | 见 §12.3 |
| `INTERNAL_ERROR` | RM 内部异常 (fail-closed) | 兜底 |

**封闭性:** 不允许 `OTHER`. 新增拒因 = 改 enum + bump 版本 + audit 字段兼容.

---

## 4. 状态机 (修订: 加 SAFE_MODE, 加副作用表)

### 4.1 状态定义

| 状态 | 含义 | 允许的 evaluate 结果 |
|---|---|---|
| `RUNNING` | 正常 | APPROVED / REJECTED (业务规则) |
| `WARNING` | 监控指标偏离, 但还能下单 | APPROVED (收紧 sizing) / REJECTED / DEFERRED |
| `HALTED` | 全局停盘 | REJECTED(STATE_HALTED) — 一律 |
| `DRAIN` | 只允许平仓 | 平仓 APPROVED, 开仓 REJECTED(STATE_DRAIN) |
| `SAFE_MODE` **(v0.2 新)** | 启动 / 崩溃恢复默认; 行为同 DRAIN, 加额外 unlock 仪式 | 平仓 APPROVED, 开仓 REJECTED(STATE_SAFE_MODE) |

### 4.2 状态转移 (修订)

```
[启动 / systemd restart] → SAFE_MODE   (v0.2 改, v0.1 是 RUNNING)

SAFE_MODE --(对账完成 + heartbeat OK 5min + 人工 unlock)--> RUNNING
SAFE_MODE --(对账失败 / 超时)--> HALTED

RUNNING --(stale WARNING 阈值命中 / consec loss N)--> WARNING
WARNING --(stale HALT 阈值命中 / consec loss N+K / daily loss)--> HALTED
WARNING --(指标恢复 + 持续 5min OK)--> RUNNING
RUNNING --(daily loss 命中)--> HALTED   (跳过 WARNING)

HALTED --(人工 ack: 双人复核)--> SAFE_MODE | DRAIN
   (注: v0.2 HALTED 不直接回 RUNNING, 必须经 SAFE_MODE 再 unlock)
DRAIN --(人工 ack)--> SAFE_MODE | HALTED

任何状态 --(SIGTERM / 紧急)--> HALTED   (优雅停, drain audit WAL 后退出)
```

### 4.3 状态转移副作用表 (v0.2 新增, 修 C-H5)

| 进入状态 | KELLY_FRACTION 切换 | 取消未成交意向 | DEFERRED 是否允许 | 是否允许开仓 | 备注 |
|---|---|---|---|---|---|
| SAFE_MODE | `KELLY_FRACTION_WARNING` (再保守) | **取消所有**未成交开仓意向 | 否 (立即 REJECT) | 否 | 启动默认; 平仓允许 |
| RUNNING | `KELLY_FRACTION` (常态) | 否 | 否 (无 stale 时) | 是 | — |
| WARNING | `KELLY_FRACTION_WARNING` (= 0.5 × 常态) | 否, 但 evaluate 全部 sizing 收敛 | 是 | 是 (sizing 缩) | — |
| HALTED | 不适用 (全 REJECT) | **取消所有**未成交开仓 + 平仓意向 | 否 | 否 | 平仓也不允许; 等人工 |
| DRAIN | `KELLY_FRACTION_WARNING` (即使有开仓信号也是 0) | **取消所有**未成交开仓意向; 平仓意向保留 | 否 | 否 | 只允许平仓 |

**离开状态时的反向副作用:**
- 离开 WARNING → RUNNING: KELLY 切回常态; 已取消的意向**不自动重发** (策略层重新生成).
- 离开 SAFE_MODE → RUNNING: 同上, 加"对账完成 + 5min OK"前提.

### 4.4 状态变更规则 — 沿用 v0.1 §4.3

- 状态变更只允许在 RM 主线程, 通过状态机事件队列.
- 每次状态变更落 audit (`STATE_TRANSITION` event), 含 from/to/trigger/operator.
- HALTED → 任何状态: 双人 ack, audit 双签.
- 没有 AUTO_RESUME 从 HALTED → RUNNING, 永远要人.
- **v0.2 新增**: 启动期默认 SAFE_MODE, 离开 SAFE_MODE 也需人工 unlock (不依赖单纯"5min OK" 自动恢复).

---

## 5. Audit log schema (修订: 与 audit WAL 衔接)

### 5.1 存储 (改写)

- **主存路径**: 每条 evaluate 同步写入 audit SPSC ring buffer (内存), 由背景线程批量 `write() + fdatasync()` 到 audit WAL 文件 (jsonl, 按日切).
- **持久语义**: SPSC append OK = 逻辑持久 (G7); 物理 fsync 由 §12 group commit 完成.
- **索引**: sqlite 单表 + 主键 audit_id + 二级索引 (intent_id, market_id, decision, evaluated_at). sqlite 与 audit WAL **不是一份数据**, sqlite 走 §3.8 同款 PRAGMA, 仅用于查询; audit WAL 才是法定真值.
- **异地备份**: 每小时打包上传 (S3), v0.2 落入 Sprint-2.

详细 WAL 架构见 §12.

### 5.2 字段 — 沿用 v0.1 §5.2 (无变化)

### 5.3 不变量 (扩充)

- 一次 evaluate 必产 1 条 audit, 不多不少.
- audit 写入失败 (SPSC ring 满) → evaluate 必须返回 `REJECTED(AUDIT_WAL_BACKPRESSURE)` (fail-closed, 不是 INTERNAL_ERROR, 单列码).
- audit 必须包含当时的 bankroll/exposure 快照, 不允许"事后查询".
- **v0.2 新**: audit WAL 单调递增 audit_id (ULID), 任何乱序 = bug, 启动重放期发现 → 进入 SAFE_MODE 并告警.

### 5.4 状态变更 audit — 沿用 v0.1 §5.4

---

## 6. 与上下游交互 — 沿用 v0.1 §6

- 6.1 上游 (策略层) — 无变化
- 6.2 下游 (执行层 / 签名) — 无变化, 但 audit_id → client_order_id 派生关系**仍然在 SPSC 入队即生效** (策略层不必等 fsync).
- 6.3 资金对账 — 阈值改 §3.7 新值 (10s WARNING / 30s HALT, 不再是 30s 唯一阈).
- 6.4 监控/告警 — 无变化, 新增 SAFE_MODE 进入也走电话告警.

---

## 7. 参数初值表 (待会签, 修订 STALE)

**法律效力:** 本表是 RM 的真值源. 表外参数 = 不存在.

| 参数 | 类型 | v0.2 建议 | 待会签 | 调整规则 |
|---|---|---|---|---|
| `PER_ORDER_CAP_HARD` | USDC | TBD @小梁 | 必签 | constexpr, 不可改 |
| `PER_ORDER_CAP_SOFT` | USDC | TBD @小梁 | 必签 | config, 只可调低 |
| `MARKET_EXPOSURE_PCT` | % | 2% | @小梁 | 只可调低 |
| `KELLY_FRACTION` | ratio | 0.25 | @小梁 | 只可调低 |
| `KELLY_FRACTION_WARNING` | ratio | 0.125 (= 0.5 × KELLY) | @小梁 | — |
| `DAILY_LOSS_PCT` | % | 3% | @小梁 | 只可调低 |
| `CONSEC_LOSS_N` | int | 5 | @小梁 | 只可调低 |
| `CONSEC_LOSS_WINDOW` | int | 20 | @小梁 | — |
| **`STALE_WSS_WARN_MS`** | ms | **2,000** | (v0.2 设, 等老陈 S1-021 校准) | 只可调低 |
| **`STALE_WSS_HALT_MS`** | ms | **10,000** | 同上 | 只可调低, **≤ 30,000 硬上限** |
| **`STALE_GOAL_WARN_MS`** | ms | **5,000** | 同上 | 只可调低 |
| **`STALE_GOAL_HALT_MS`** | ms | **15,000** | 同上 | 只可调低, ≤ 30,000 |
| **`STALE_RECON_WARN_MS`** | ms | **10,000** | 同上 | 只可调低 |
| **`STALE_RECON_HALT_MS`** | ms | **30,000** | 同上 | **= 30,000 红线, 不可上调** |
| `SAFETY_BUFFER` | % | 5% | @小梁 | 只可调高 |
| `IDEMPOTENCY_TTL_HOURS` | h | 24 | — | — |
| **`AUDIT_WAL_BATCH_SIZE`** (v0.2) | int | 64 | @老姜 review | — |
| **`AUDIT_WAL_BATCH_MS`** (v0.2) | ms | 1 | @老姜 review | — |
| **`AUDIT_WAL_RING_CAPACITY`** (v0.2) | records | 65,536 (4MB at 64B/record avg) | @老王 review | 满 = REJECT |
| **`SAFE_MODE_UNLOCK_HEARTBEAT_S`** (v0.2) | s | 300 (5min) | @老雷 | — |

**注**: "只可调低" 指朝更保守方向单调. STALE 阈值的"更保守"是**调低** (越快 HALT 越保守).

---

## 8. 实现注意事项 (修订: 事件循环 + fill 优先)

### 8.1 并发模型 (改写, 修 C-H6)

**RM 主线程 = 单线程事件循环**, 两条输入 SPSC:

- `intent_in` (来自策略层 IntentAggregator): `OrderIntent` 流, 调用方阻塞等 future.
- `fill_in` (来自执行层 fill MPSC): 成交回报流, 用于 §3.4 / §3.5 / §3.6 实时计算.

**调度优先级 (硬约束):**

```
loop:
  # 优先级 1: 先 drain fill_in (最多 BATCH_FILL = 32 条, 防饿死 intent)
  while not fill_in.empty() and drained < BATCH_FILL:
    apply_fill(fill_in.pop())   # 更新 positions_ledger, PnL, consec_loss
    drained += 1
  
  # 优先级 2: 处理 intent
  if intent_in.try_pop(intent):
    decision = evaluate_internal(intent)
    return_future(decision)
  
  # 优先级 3: 状态机 tick (1ms 心跳, 检查 stale / consec / daily)
  if now - last_tick >= 1ms:
    state_machine.tick()
    last_tick = now
```

**理由:**
- fill 先于 intent: position 必须先一致再做新决策, 否则 §3.4 / §3.5 用脏 position.
- 32 条 BATCH_FILL 上限: 防 fill 风暴饿死 intent 评估 (Polymarket 实际 fill QPS 不会爆 32/loop, 设计余量).
- 状态机 tick 1ms: stale 阈值最严 2s, 1ms 检测频率足够.

**性能预算 (沿用 G3):**
- 单次 evaluate p99 ≤ 200us (含 audit SPSC append ≤ 1us, ULID gen ≤ 5us, 规则评估 ≤ 194us).
- @老姜 review event loop 调度策略 (S1-007 阶段 7).

### 8.2 内部数据结构 — 沿用 v0.1

- `positions_ledger`: 内存 hashmap.
- `seen_keys` (idempotency): in-memory LRU + sqlite.
- `state`: atomic enum.

### 8.3 时钟 — 沿用 v0.1

### 8.4 启动期校验 (扩充)

启动 self-check (v0.1 + v0.2 补):
1. config 加载, SOFT ≤ HARD 校验.
2. audit WAL 文件可写 (创建/打开 + 头部 magic 校验).
3. sqlite idempotency table 可读 + PRAGMA 应用.
4. **v0.2 新**: WAL replay (audit WAL + position/nonce WAL 两条独立) → 重建未确认 audit + position + nonce 高水位.
5. **v0.2 新**: 启动后**默认进 SAFE_MODE**, 不是 RUNNING (§13).
6. 资金对账连通 → 否则 HALTED.

启动失败 = 进程退出, 不允许"降级启动".

### 8.5 热 reload — 沿用 v0.1

- config 单调性校验扩到新增的 STALE 阈值 (只可调低 / RECON HALT 不可上调).

---

## 9. 测试策略 — 沿用 v0.1 §9, 增补

(沿用 9.1/9.2/9.3/9.4)

**v0.2 新增混沌场景** (与小宋协同, 见 §12.6):
- audit WAL fsync 线程 hang → SPSC ring 满 → evaluate 必返 `AUDIT_WAL_BACKPRESSURE`.
- audit WAL 文件满 (磁盘空间耗尽) → fail-closed.
- WSS 阶段性抖动 (1.8s 到达 2.1s 间) → DEFERRED 频率监控.
- 崩溃恢复 → 验证默认进 SAFE_MODE, 不自动放行开仓.

---

## 10. 开放问题 — 沿用 v0.1, 修订

### 10.1 已闭项 (v0.1 → v0.2 已闭)

| # | v0.1 项 | 闭法 |
|---|---|---|
| Q2 | EDGE_CI_NEGATIVE 单列? | ADR-001 F-6 / v0.2 §3.2: 单列 |
| Q9 | stale 阈值跨洋实测 | ADR-001 §3.2: 假设 us-east-1, v0.2 §3.7 给阈值, S1-021 实测后 v0.3 校准 |
| Q11 | audit fsync 频率 N | ADR-001 §3.3 / v0.2 §12: 改 group commit, N=64 OR 1ms timer |

### 10.2 仍待会签 (沿用 v0.1, 与小梁/老雷)

- Q1 KELLY_FRACTION 初值 → @小梁
- Q3 edge CI 二次缩水 → @小梁 (建议 v0.3)
- Q4 Kelly quote vs expected fill → @小肖 (建议 v0.3, 与 R-1 一并)
- Q5..Q8 cap / loss 阈值 → @小梁 + @老雷
- Q10 NTP 漂移 → @老郭 + @老姜
- Q12 影子模式时长 → @老雷
- Q13 双人 ack 具体是谁 → @老雷

### 10.3 v0.2 不做, 留 v0.3+ (沿用 v0.1)

(沿用)
- 跨市场相关性 (game-level cap)
- 多账户分账户风控
- 异地 audit 备份 (Sprint-2 落)
- prop / outright / period 启用
- 实时 PnL 归因
- 动态 Kelly fraction
- **v0.2 新增延 v0.3**: STALE 阈值实测校准 (待 S1-021); Kelly slippage 模型 (待 @小肖).

---

## 11. STALE 阈值表 + 触发动作矩阵 (D-06 红线遵守版, v0.2 新增)

### 11.1 阈值表 (法律效力, 与 §7 同源)

| 数据源 | 测量方式 | WARNING 阈值 (DEFERRED) | HALT 阈值 (REJECT) | D-06 上限 |
|---|---|---|---|---|
| Polymarket clob WSS | `now - last_book_update_ts` | 2,000 ms | 10,000 ms | 30,000 ms (硬) |
| Goalserve inplay | `now - last_score_update_ts` | 5,000 ms | 15,000 ms | 30,000 ms |
| 资金对账 (positions ledger) | `now - last_recon_sync_ts` | 10,000 ms | 30,000 ms | 30,000 ms (= 红线) |
| (反向心跳) RM 内部 ticker | `now - last_self_tick` | 50 ms | 200 ms | (自检, 进程级 abort) |

**测量来源** (双重保险, 与 v0.1 §3.7 一致):
- `OrderIntent.data_freshness_ms` (策略层告知 — 主源)
- RM 自己订阅各源 heartbeat (旁路源, 不信策略层单方)
- 任一源越阈即触发, 取**最大值** (最保守).

### 11.2 触发动作矩阵

| 触发条件 | 状态转移 | evaluate 返回 | 副作用 (引用 §4.3) |
|---|---|---|---|
| WSS freshness > 2s | RUNNING → WARNING | DEFERRED | KELLY_FRACTION → 0.5x, sizing 收敛 |
| WSS freshness > 10s | WARNING → HALTED | REJECTED(STALE_DATA) | 取消所有未成交开仓 + 平仓意向 |
| Goalserve freshness > 5s | RUNNING → WARNING | DEFERRED | 同上 KELLY 缩 |
| Goalserve freshness > 15s | WARNING → HALTED | REJECTED(STALE_DATA) | 取消未成交 |
| 对账 freshness > 10s | RUNNING → WARNING | DEFERRED | 同 |
| 对账 freshness > 30s (= D-06) | WARNING → HALTED | REJECTED(STALE_DATA) | 取消未成交 + 电话告警 |
| RM self-tick > 200ms | (异常) | — | 整进程 abort (§9 老周 fail-fast) |

### 11.3 恢复路径

- WARNING → RUNNING: freshness 持续 < WARNING 阈值 5min.
- HALTED → SAFE_MODE: 人工 ack (双人), 进 SAFE_MODE 等对账 + 5min OK 再 unlock.

### 11.4 D-06 红线遵守说明

老雷 D-06: "数据 30s 无更新自动暂停市场" — 30s 是 **HALT 上限, 不是 WARNING**.

v0.1 错把它当 WARNING (推 HALT 到 60s), v0.2 修正:
- WSS HALT 10s ≤ 30s ✓
- Goalserve HALT 15s ≤ 30s ✓
- 对账 HALT 30s = 30s ✓ (= 红线本身)

任何配置变更让任何源 HALT > 30s = 启动失败.

---

## 12. Audit WAL 架构 (group commit + 双 WAL 隔离, v0.2 新增, 修 C-H2)

### 12.1 设计原则

| 原则 | 做法 |
|---|---|
| evaluate 同步拿 audit_id | ULID 在 evaluate 内生成, 同步写 SPSC ring |
| audit 必须持久 (G7) | append-only WAL, group commit, fail-closed |
| 单 evaluate 不阻塞 fsync | fsync 走背景线程 (老周 §8 core 7 bg) |
| 故障域隔离 | audit WAL 与 position+nonce WAL **两条独立** 文件 / 独立 fsync 线程 |
| 崩溃可恢复 | 启动期 WAL replay 重建未确认 audit |
| WAL 写不进去 → REJECT | 即使背景 fsync hang, SPSC ring 满 = `REJECTED(AUDIT_WAL_BACKPRESSURE)`, **进 SAFE_MODE** |

### 12.2 路径设计

```
RM 主线程 (core 5)                  bg audit fsync 线程 (core 7)
─────────────────────                ───────────────────────────────
evaluate(intent):
  decision = check_rules(intent)
  audit_id = ulid_gen()             ──┐
  audit = build_record(decision, ...) │  约 5 us (含 ulid + build)
  audit.audit_id = audit_id           │
  ok = audit_ring.try_push(audit)   ──┴─> SPSC ring (64K capacity)
  if not ok:                              │
    return REJECTED(AUDIT_WAL_BACKPRESSURE)│
  return decision_with_audit_id          │
                                          │
                                          ▼
                                     batch read:
                                       while True:
                                         records = ring.drain_up_to(64)
                                         if records.empty() and elapsed < 1ms:
                                           park(remaining)
                                           continue
                                         buf = serialize(records)         # ~10us / batch
                                         write(audit_fd, buf)             # ~5us
                                         fdatasync(audit_fd)              # 100us..1ms SSD
                                         high_watermark = max audit_id
                                         metric.observe(batch_size, latency)
```

### 12.3 关键不变量 (与 ADR-001 §3.3.3 对齐)

1. **WAL append-only**: 一旦 `audit_ring.try_push()` 返回 OK, audit 视为**逻辑持久**.
2. **崩溃恢复**: 进程崩溃 → systemd 重启 → 读 audit WAL → 重建未确认 audit → 进 SAFE_MODE 等人 unlock.
3. **SPSC 满 = fail-closed**: ring 满 → `REJECTED(AUDIT_WAL_BACKPRESSURE)` 立即返回; 同时触发 metric + 告警, **5 分钟内连续 > 10 次** 自动进 SAFE_MODE.
4. **签名前不必等 fsync**: 因为 audit_id 一旦生成且入 ring, 即使崩溃也能 replay, 不影响事后审计闭合. 与 nonce manager (老周 §9.4 + 老孙 v1) 必须 fsync 才递增不矛盾, 两条 WAL 各管各的.

### 12.4 性能预算 (与 G3 = 200us 对齐)

| 阶段 | 预算 |
|---|---|
| ulid_gen | < 2 us |
| build_record (内存填充) | < 3 us |
| audit_ring.try_push (SPSC) | < 1 us (小石 v1 阶段 4 数据: 0.5us p99) |
| **同步部分合计 (audit-only)** | **< 6 us** |
| 规则评估 (R0..R9) | 余下 ~ 194 us |
| **G3 evaluate 全程** | **< 200 us** |
| 异步 fsync 批量 (64 records / 1ms) | 不在同步路径 |

老姜 latency budget §1 给 RM 50us 硬预算 (内环 p99), 与本 200us 不冲突:
- 老姜 50us = 规则评估热点部分 (R0..R9 跳过 audit ring 极快 IO 的部分);
- 老韩 200us = evaluate 全程含 audit ring push;
- 老郭 ADR-001 §5.1 已确认两者兼容 ("老韩 200us 是 evaluate 全程, 老周 50us 是规则评估部分").

### 12.5 双 WAL 隔离 (RPO / RTO 推算)

**Audit WAL** (本设计):
- 写盘节奏: group commit batch=64 OR 1ms timer.
- IO pattern: 高频小记录 (~80-150 byte/record), 突发可达 500 ops/s.
- 故障 RPO (Recovery Point Objective):
  - 进程崩溃前最后 1ms 内的 audit 可能未 fsync, **但已在 ring buffer 中, 启动 replay 可恢复** (前提: ring buffer 落在 mmap 文件而非纯 RAM, **本 v0.2 设计 SPSC 走纯 RAM, 故崩溃丢失 ring 内未 flush 部分**).
  - 实际 RPO = 最后一次 fdatasync 完成时刻到崩溃时刻之间的所有 audit, 最坏 1ms ≈ 0-64 条.
  - **业务影响**: 这 ≤ 64 条 audit 对应的 decision 已经被策略/执行层使用 (audit_id 已下发), 但 audit WAL 物理缺失这几条 → audit 残缺. **缓解**: §12.7 兜底.
- RTO (Recovery Time Objective): replay 整个 WAL 文件 ~10MB/s 顺读, 重建索引几秒内.

**Position+Nonce WAL** (老周 §7.3 / §9.4, 不是本文 owner):
- 写盘节奏: 必须 fsync 后才允许 nonce 递增 / position 应用 (每笔下单同步 fsync).
- IO pattern: 中频中记录, 与 audit 完全不同.
- 隔离原因 (ADR-001 §3.3.5):
  - 故障域: audit 文件损坏不影响交易状态恢复.
  - IO pattern 不同: 合并会让 audit 高频压垮 position 低频, 或 position fsync 拖垮 audit 吞吐.
  - 安全分级: audit 合规级, position 业务级, 权限可不同.
- **实现**: 同一个 WAL framework (@老王 owner, S1-013), 跑两个独立 instance + 两个独立文件 + 两个独立 fsync 线程.

### 12.6 fsync 兜底策略

| 场景 | 策略 |
|---|---|
| 正常 | group commit batch=64 OR 1ms timer (取先到) |
| 低 QPS (< 1 QPS) | 1ms timer 兜底, < 10 Hz fsync |
| 高 QPS (突发 500 ops/s) | 攒满 64 即 fsync, ~8 Hz, IOPS 友好 |
| 关停 (SIGTERM) | drain SPSC + 最后一次 fsync + sync syscall |
| 崩溃 (signal handler) | best-effort fsync (不阻塞过久), core dump 前 |
| fsync 线程 hang | 主线程 SPSC 满 → REJECT + 进 SAFE_MODE |
| 磁盘满 | write 返回 ENOSPC → 主线程 ring 持续满 → 全部 REJECT + 告警 |

### 12.7 RPO=0 边界情况兜底

对 §12.5 提到的"最后 1ms 内 0-64 条 audit 丢失":
- **缓解 1**: SPSC ring 可选 mmap 化 (mmap MAP_SHARED 文件) — 但 mmap dirty page 也不保证 fsync, 等同于 OS pagecache, 改善有限. **v0.2 不开**, 留 v0.3 评估.
- **缓解 2**: signal handler 在 SIGSEGV/SIGABRT 时尝试 drain SPSC 写到 emergency file + fsync. 时间预算 ~100ms 内, 大部分情况能保住. **v0.2 实现**.
- **缓解 3**: 接受 ≤ 1ms / ≤ 64 条的 audit 残缺, 但必须**在启动 replay 时检测并告警** (audit_id 序列里有 hole 即报). 不允许静默吞.

### 12.8 派单分工

- **设计** → 本文 (老韩) + ADR-001 §3.3 (老郭).
- **WAL framework 实现** → @老王 (persistence), S1-013 给最小可用; 老韩定接口.
- **SPSC ring 选型** → @小石 S1-011 (rigtorp SPSC, 已确认).
- **性能压测** → @老姜 component-bench 加 "audit WAL throughput / fsync p99" 两指标.
- **故障注入** → @小宋 加 "fsync hang / WAL 满 / 磁盘满" 三场景.
- **CI 静态扫描 (任何 audit 缺漏)** → @老练 在 PR gate 加 "audit_id 单调递增" 不变量测试.

---

## 13. SAFE_MODE 联动 (与老周 §9.2 协调, v0.2 新增)

### 13.1 SAFE_MODE 定义 (RM 视角)

`SAFE_MODE` 是 RM 状态机的一个**特殊状态**, 与老周 §9.2 / GM W-3 红线对齐:

- 进入: 启动 (含 systemd 重启) 默认; 或 HALTED → 人工 ack 进入.
- 行为: 与 DRAIN 类似 (只允许平仓), 加额外 unlock 仪式.
- 离开: **必须**双重条件:
  1. 对账 (position ledger / nonce manager / Polymarket open orders) 全部一致.
  2. WSS / Goalserve / 对账 三源 heartbeat 持续 OK ≥ 5min (`SAFE_MODE_UNLOCK_HEARTBEAT_S = 300`).
  3. **人工 unlock** (CLI 命令 + 双人 audit 双签).
- 三者全满才 RUNNING; 任一不满, 留在 SAFE_MODE.

### 13.2 与老周 §9.2 重启链对齐

老周 §9.2 给出 systemd 重启链:
```
systemd restart → load config → replay WAL → 启动 RM (注入红线 + halt switch)
  → 连 RPC → 拉 nonce/持仓 → 与 ledger 对账 → 对账不一致 → SAFE_MODE
  → 对账一致 → 连 WSS → first heartbeat → 解锁交易
```

**RM v0.2 在这条链上的具体动作:**

| 重启步骤 | RM 动作 | 状态 |
|---|---|---|
| RM 启动注入 | 读 audit WAL replay + position WAL replay | 状态 = SAFE_MODE (强制) |
| 对账阶段 (位置 + nonce + open orders) | 监听对账模块结果; 任何不一致 → 进 HALTED (不是停在 SAFE_MODE) | SAFE_MODE 或 HALTED |
| 对账 OK + WSS 连上 | 开始计 heartbeat 5min 倒计时 | 仍 SAFE_MODE |
| 5min OK + 人工 unlock | RM 状态 → RUNNING, **允许开仓** | RUNNING |
| heartbeat 失败 / 对账二次失败 | 倒计时重置或退 HALTED | SAFE_MODE → HALTED |

### 13.3 与老周 §9.3 fail-fast 对齐

老周 §9.3: 热路径任意线程 abort → 整进程 abort + core dump → systemd 拉起.

RM 视角:
- 任何 RM 内部不变量 (如 audit_id 非单调, ring buffer 损坏, lock 状态不一致) 检测到 → 整进程 abort, 由 §13.2 链重启进 SAFE_MODE.
- "业务规则触发" (例如 daily loss / consec loss / stale) **不 abort**, 走状态机进 HALTED.

### 13.4 SAFE_MODE 期间允许的 evaluate

| intent 类型 | 决策 |
|---|---|
| 开仓 (新建头寸) | `REJECTED(STATE_SAFE_MODE)` |
| 平仓 (减仓 / 反向打平) | `APPROVED` (经其他规则) |
| 撤单 (非 evaluate 路径, 走 exec 直接撤) | 不经 RM evaluate, 直接放行; 但状态变更落 audit |

**判别"开仓 vs 平仓"**: 根据 `OrderIntent.side` + 当前 `positions_ledger.market_id` 净头寸方向. 若新成交会**减小** |净头寸| → 平仓; 若**增大** → 开仓.

### 13.5 SAFE_MODE 告警与人工 unlock 流程

- 进入 SAFE_MODE: 立即电话告警 (我 + 老雷), Slack 推消息含原因 + audit_id.
- unlock CLI 命令: `stcpp-admin risk unlock --reason <text> --operator <name1> --co-signer <name2>`.
  - 必须双人 (operator + co-signer), 否则命令拒绝.
  - 落 audit (`STATE_TRANSITION` event), 双签字段.
- unlock 前置 check:
  - 对账状态 = OK (来自对账模块查询).
  - 三源 heartbeat 连续 5min OK.
  - 任一不满 → CLI 拒绝, 提示原因.

### 13.6 与 GM W-3 红线对齐

GM sign-off §4: "进程重启后默认进入只读 / 不下单状态, 运维显式 unlock 才恢复交易".

v0.2 §13 完全遵守:
- 重启默认 SAFE_MODE = 只读 + 只撤不开仓 = "只读 / 不下单".
- 显式 unlock = `stcpp-admin risk unlock` + 双签.
- 任何下一次 ADR 不得弱化此红线 (GM 红线锁死).

---

## 14. R-1 / R-2 / R-3 残留风险点 (Kelly slippage + RTT 待数据 + 双 WAL RPO)

### 14.1 R-1 (高): Kelly slippage 模型缺失

**风险**: v0.2 Kelly sizing 用 quote price (= Polymarket 报价), 不考虑实际 fill 滑点. Polymarket 流动性薄, 实际 fill price 可能比 quote 差 10-50 bps, 导致:
- 名义 approved_size 真实下单, 但实际仓位 < 预期 (吃不下);
- 或 fill 平均价比预期差, 真实 edge 被吃掉.

**v0.2 立场**:
- v0.2 沿用 v0.1 立场: 用 quote, 不缩水.
- v0.3 引入 slippage 模型 (与 @小肖 协同): 根据 book 厚度 + size 估算 expected fill price, 用 expected fill price 重算 edge → 重做 sizing.

**派单**: @小肖 (撮合/盘口) 出 slippage 估算模型设计, owner = 小肖, deadline = Sprint-2 中.

**临时缓解**:
- PER_ORDER_CAP_SOFT 取较小值 (待小梁会签);
- 监控实际 fill vs quote 偏差, 偏差 > 阈值告警.

### 14.2 R-2 (中→低): STALE 阈值需 RTT 数据落地后再次校准

**风险**: v0.2 §3.7 / §11 阈值基于"us-east-1 同区 RTT < 10ms"假设. 实测 RTT 与假设有差 → 阈值偏紧 (false-positive DEFERRED 多) 或偏松 (漏报).

**v0.2 立场**:
- 阈值采用 ADR-001 §3.2 老郭给的初值 (2s/10s, 5s/15s, 10s/30s) — 偏保守 (宁可 false-positive 不漏报).
- 等 @老陈 S1-021 网络实测 (us-east-1 → Polymarket WSS / Goalserve / Polygon RPC 的 p50/p99/p99.9) 数据出.
- 数据出后 → 老韩做 **v0.3 调优**: 若 p99.9 > WSS WARNING 阈值 50%, 上调 WARNING 阈值 (但 HALT 仍 ≤ 30s 红线锁).

**派单**: @老陈 S1-021 实测, deadline 2026-06-04 (Sprint-1 末).

**配套**: 实测前 (即 Sprint-1 内), 老韩在 RM 内埋 metric:
- `stale_deferred_count{source=wss|goalserve|recon}`
- `stale_halt_count{source=...}`
- `freshness_ms_p99{source=...}`
- 频率异常 (deferred 触发率 > 1%) 即告警, 不等 v0.3.

### 14.3 R-3 (中): 双 WAL RPO ≤ 1ms / ≤ 64 条 audit 残缺

**风险**: §12.5 / §12.7 已分析:
- audit WAL group commit 设计下, 进程崩溃可能丢失最后 1ms 内的 0-64 条 audit (SPSC ring 在 RAM, 未 flush).
- 这些 audit 对应的 decision 已经下发给执行层 (audit_id 已透传), 但 audit 残缺 = 事后审计无法闭合此几条.
- 业务影响: G2 ("任意拒单 30s 内 grep 出 audit") 在崩溃前 1ms 内的极少数 audit 上失效.

**v0.2 立场**:
- 接受 RPO ≤ 1ms 的窗口, 由 §12.7 缓解 2 (signal handler 内 best-effort drain + fsync) 兜.
- 启动 replay 时检测 audit_id 非单调 (有 hole) → 主动告警, 不静默.
- v0.3 评估 mmap-backed SPSC ring (但 mmap 不保证 fsync, 收益有限).

**派单**:
- 缓解 2 实现 → @老韩 + @老王 联合, Sprint-2.
- replay 不变量检测 → @小段 (replay engineer).

### 14.4 R-4 (新增, 低): SAFE_MODE 启动期不下单的 alpha 错失

**风险**: 启动期 SAFE_MODE → 对账 + 5min OK + 人工 unlock, 期间不开仓. 如启动恰逢比赛黄金窗口, 错失 alpha.

**v0.2 立场**:
- 这是 GM W-3 红线锁死的成本, 不接受弱化.
- 缓解: 老吴 S1-010 c6i.xlarge 部署 + systemd 快速拉起, 整链 < 30s. unlock 仪式人工部分 5min 是主要瓶颈, 可考虑"白班/夜班 oncall 即时人工 ack".
- 监控 SAFE_MODE 累计时长 metric, 长期 > 5min/重启 = 部署/运维问题.

---

## 附录 A — v0.2 PR 拆分与 owner

| PR | 内容 | Owner | 依赖 |
|---|---|---|---|
| PR-1 | RM skeleton + RiskGateway 单符号 | 老韩 | 老周 v0.2 (CMake link 阻断) |
| PR-2 | 规则 R0..R9 + EDGE_CI_NEGATIVE 单列 | 老韩 | — |
| PR-3 | 状态机 + SAFE_MODE + §4.3 副作用表 | 老韩 | — |
| PR-4 | audit WAL group commit (SPSC + bg fsync) | 老韩 + 老王 | 老王 WAL framework |
| PR-5 | sqlite idempotency PRAGMA + 崩溃恢复 | 老韩 | — |
| PR-6 | 事件循环 + fill_in 优先 | 老韩 | 老姜 review |
| PR-7 | metrics 埋点 (stale freshness / WAL throughput) | 老韩 + 小郑 | 小郑 metrics 系统 |
| PR-8 | 测试 + 混沌场景 | 老韩 + 小宋 | 小宋 chaos 框架 |

---

**END v0.2.** 等老郭 24h sign-off + 小梁参数会签 → bump v1.0 进 Sprint-1 实现.
