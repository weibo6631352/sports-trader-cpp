---
owner: 小程 (quant-signal-research, C 单元 IC #19)
last_review: 2026-05-29
status: v0.2 正式稿 — 待小梁 review → GM ack
relates_to:
  - docs/RESEARCH/xiaocheng-p0_02-signal-spec-v0.1.md
  - docs/RESEARCH/laopeng-w8-oq-p02-3-inplay-single-source-ack.md
  - docs/RESEARCH/laopeng-w9-inplay-edge-gross-net-confirm.md
  - docs/RESEARCH/laopeng-w9-w5-betting-industry-research-update-v1.md
  - docs/ADR/2026-06-W4-adr-027-core-data-structure-ssot-enforce.md
---

# P0-02 Score Price Mismatch — 信号 Spec v0.2

- **Owner**: 小程 (quant-signal-research, C 单元 IC #19)
- **Date**: 2026-05-29
- **Last review**: 2026-05-29
- **Status**: v0.2 正式稿 — 待小梁 first review → GM ack
- **验收人**: 小梁 (C 主管)
- **ADR cite**: ADR-027 (N/A — 本文件无核心 struct 改动, 纯 signal spec doc)
- **v0.1 → v0.2 变更摘要**:
  1. **Alpha v2 修正**: edge 从 "post-fee 2-3%" 改为 "post-bias, pre-Polymarket-fee 1.5-2.5%" (老彭 W9 W2 OQ-P02-3 ack 澄清歧义)
  2. **C2 门槛**: |dev| ≥ 5¢ 升为 |dev| ≥ **6¢**；主队/Over 方向 ≥ **7¢** (老彭 W8 推荐，W9 W5 再确认)
  3. **死区 25%/$500**: 仓位死区规则落 spec (v0.1 仅在 v0.1 设计意图里口头提及，v0.2 正式写入)
  4. **Fee 短路 3%**: taker fee 短路设定维持 V2 3%（老李 W85 confirm），不再标注为"草稿"
  5. **inplay 单 bet365 de-vig**: 正式写入 ADR-008 §5 inplay 例外路径，取代原 9-家多源假设
  6. **hit rate / Sharpe 带下调**: 老彭 W9 W2 修正后统一更新

**关联文档**:
- `xiaocheng-p0_02-signal-spec-v0.1.md` (v0.1 原稿, 保留归档)
- `laopeng-w8-oq-p02-3-inplay-single-source-ack.md` (OQ-P02-3 ack, bet365 单源确认)
- `laopeng-w9-inplay-edge-gross-net-confirm.md` (gross/net 歧义澄清)
- `laopeng-w9-w5-betting-industry-research-update-v1.md` (W9 W5 行业调研, §3.2 §5.2)
- `laopeng-multiplicative-devig-calibration-v1.md` (ADR-008 de-vig, inplay §5 例外)
- `xiaoyuan-fill-rate-model-v0.1.md` (fill_rate ≥ 0.50 协议)
- `laohan-riskmanager-design-v0.3.1.md` (RM evaluate 接口)

**拒接声明**: 本文件是 signal spec, 不含 C++ 代码。实现由小卢 IC pool 接单。

---

## 0. 信号基本信息

| 字段 | 值 |
|---|---|
| 信号 ID | `P0_02_ScorePriceMismatch` |
| 信号全名 | Score Price Mismatch — Inplay Event Arbitrage |
| 信号类型 | inplay event-driven, 价值-逆势, 信息时差 |
| alpha 来源 | Goalserve inplay bet365 push vs PM mid 调价滞后 5-15s |
| 盘口 | Soccer Moneyline (1X2 → Home/Away 二元化), MVP 首发；Basketball M4 后扩 |
| MVP scope | Soccer 五大联赛 + Champions League |
| 单 bet365 de-vig | ADR-008 §5 inplay 例外路径 (正式) |

---

## 1. v0.1 → v0.2 差异说明

### 1.1 Alpha 命名歧义修复 (老彭 W9 W2 OQ-P02-3 ack §1)

v0.1 §4.2 中 "Edge post-fee (per trade) 2-3%" 的命名有歧义。老彭 W9 W2 澄清：

```
v0.1 写的是:
  Edge post-fee = 2-3%
  (实际含义: 扣除 bet365 book bias 后的有效偏离, 未扣 Polymarket fee)

v0.2 正名为:
  Edge post-bias, pre-Polymarket-fee = 1.5-2.5%
  (已扣 bet365 book bias 0.7-1.2pp; 尚未扣 Polymarket 3% taker fee + slippage)

net edge = 1.5-2.5% - 3% - ~0.3% = -1.8% ~ -0.8%  (中位负)
```

**关键含义：净 edge 中位为负。** 这是 v0.2 收紧 C2 门槛的核心驱动。

### 1.2 C2 门槛从 5¢ 升至 6¢ (方向性差异化)

老彭 OQ-P02-3 ack §4.2 + W9 W5 §5.2 双重确认：

- **标准方向** (Away/Draw/Under): C2 ≥ **6¢**
- **主队/Over 方向**: C2 ≥ **7¢** (bet365 square-book 对热门方向系统高估约 0.5-1.5pp, 假阳性率更高)

C2 = 6¢ 等效于筛选 gross edge > 4% (Moneyline ~0.55 赔率估算), 扣 3% fee 后净 edge ≥ 1%, 有正期望。

### 1.3 死区规则 (v0.2 新增)

v0.1 未显式写出死区。v0.2 补入：

- **仓位死区 (size dead-zone)**: size_base < $500 (bankroll 0.5% 以下) → 不触发下单
- **score dead-zone (25%)**: 任一方胜率已超 75% (fv > 0.75 或 fv < 0.25) → 不进，避免 inplay 末段极端赔率下 bet365 book bias 膨胀

### 1.4 Fee 短路 3% (老李 W85 confirm, V2 不变)

Polymarket 现行 taker fee = 3%, 维持 v0.1 设定，不打折。

### 1.5 de-vig 方法降级 (ADR-008 §5 例外)

v0.1 错误假设 inplay odds 有 9 家 bookmaker 多源。OQ-P02-3 实证确认 inplay feed bm 字段全部 = "bet365"，是结构性单源。

**v0.2 正式使用单 bet365 multiplicative de-vig：**

```
fair_p_bet365 = implied_p_bet365 / overround_bet365
overround_bet365 = sum_i(1 / value_eu_i)  // i = {home, draw, away}
```

精度损失 0.7-1.2pp (老彭确认), 由 C2 门槛上调 (5¢ → 6¢) 吸收。ADR-008 §5 例外条款由老郭 W8 W3 立。

---

## 2. 触发条件 (5 条 AND, short-circuit C5→C1→C3→C4→C2)

**5 条 AND 短路顺序：先评估开销低的门槛，最贵 C2 最后算。**

### C1: inplay event 触发 (事件驱动门)

```
C1 = (event_type IN {goal, red_card, penalty})
  && (event_id != last_seen_event_id)    // 去重，防止同 event 重复触发
  && (suspend == "0")                    // 赔率未暂停
```

**event 推断方法 (Goalserve inplay-soccer.gz, 小段 v3 实测)**:

```
goal_event    = score_home 或 score_away 在相邻 poll 间净增 ≥ 1
red_card      = state 5 位码包含 red_card bit (若小段 W10 audit 确认有独立 bit; 否则 fallback 到 state bit)
penalty       = state bit 对应 penalty kick flag
event_id      = hash(match_id + minute + second + score_home + score_away)
```

字段来源: `inplay.goalserve.com/inplay-soccer.gz` (小段 v3 §1 CONTAINS_VALUE 实测)

### C2: PM mid vs bet365 de-vig fair_value 偏离 (v0.2 升级)

```
let fv = bet365_devig_fair_value(sport, match_id, as_of_ts)
       // ADR-008 §5 inplay 例外: 单 bet365 multiplicative de-vig
       // fair_p = (1/value_eu_home) / overround_b365
       // inplay.goalserve.com/inplay-soccer.gz, value_eu 字段
       // stale guard: 上次非-suspend 快照时效 ≤ 10s; 否则 C2 不满足

let p_pm = polymarket.moneyline.yes.microprice()  // 小袁 §2.3 microprice，cap |micro-mid| ≤ 2 tick

let dev = fv - p_pm   // 正 = PM 低估 Yes → 买 Yes
                      // 负 = PM 高估 Yes → 买 No

// v0.2 方向性门槛:
direction = sign(dev)
is_home_or_over = (pm_market.outcome_label IN {home, over})

C2 = (is_home_or_over  && |dev| >= 0.07)   // 主队/Over: ≥ 7¢ (bet365 square-book bias 更大)
  || (!is_home_or_over && |dev| >= 0.06)   // Away/Draw/Under: ≥ 6¢
```

**C2 逻辑变更说明**: v0.1 是 |dev| ≥ 5¢ 无方向区分 → v0.2 方向性 6¢/7¢。预期触发频率下降 20-30%，hit rate 回升约 1pp（假阳性减少）。

### C3: PM book 流动性 ≥ $2K

```
let depth_2tick = polymarket.orderbook.depth_within_2_ticks(side=direction)

C3 = depth_2tick >= 2000   // $2K, 与 v0.1 一致
```

### C4: 预期 fill_rate ≥ 0.50

```
let fr = FillRateModel.compute_taker(market_class=INPLAY, ...)  // 小袁 v0.1

C4 = fr >= 0.50
```

adverse_selection_score: 事件发生后 30s 内 = 0.6 (informed trader 活跃期), >30s 后降回 0.3。

### C5: LiveSection == INPLAY_ACTIVE

```
C5 = (status IN {"In Play", "1st Half", "2nd Half", "1st Period", "2nd Period"})
  && (minutes_remaining > 2)    // 末段 2 分钟内不进
  && (extra_time == false)      // 加时赛不进
```

字段来源: Goalserve state 5 位状态码 (v3 §4 dictionaries/states/soccer)。

---

## 3. 入场 / 出场逻辑

### 3.1 入场

```
direction  = sign(fv - p_pm)
size_base  = kelly_fractional(fv, 1/p_pm - 1) * 0.20   // 1/5 Kelly (inplay 更保守)
size_raw   = clamp(size_base * bankroll, 500, 1000)     // USD, MVP inplay 单笔 $500-$1K

// v0.2 死区:
// score dead-zone: fv > 0.75 或 fv < 0.25 → size = 0 (不进)
// size dead-zone: size_raw < 500 → size = 0 (不进)
size = (fv > 0.75 || fv < 0.25) ? 0 : size_raw

order_type = LIMIT @ (microprice + direction * 0.5 * tick)  // 半 tick 进
fallback   = TAKER @ best_ask/bid  if not filled in 10s
```

**死区 25%/$500 含义**: |dev| ≥ 6¢ 在极端赔率下 (fv > 0.75) 很可能是 bet365 噪声而非真 PM 错误，下限 $500 避免超小仓位浪费 gas。

### 3.2 出场

```
take_profit:  |dev_t| < 1¢                       → close (回归 fair_value)
stop_loss:    |dev_t| 反向扩大到 > 8¢              → close (fair_value 自己跳了)
time_stop:    持仓 90s 未触发 TP/SL               → close (alpha decay 强制)
event_stop:   新 inplay event (goal/red/penalty)  → re-evaluate，条件未满足则 close
suspend_stop: bet365 suspend == "1"               → 立即 close
```

---

## 4. Alpha v2 估计 (老彭 W9 W2 修正版)

### 4.1 参数对比表

| 参数 | v0.1 原估 | v0.2 修正 | 修正依据 |
|---|---|---|---|
| Hit rate (IS) | 56-60% | **55-58%** | 单 bet365 de-vig 精度损失 → 假阳性升 → hit 下调 |
| Hit rate (OOS) | 54-56% | **54-57%** | C2 门槛收紧抵消部分精度损失 |
| Edge | "post-fee 2-3%" (命名错误) | **post-bias, pre-fee: 1.5-2.5%** | bet365 book bias 0.7-1.2pp 吸收后有效偏离 |
| Net edge (中位) | 未明确 | **-1.8% ~ -0.8%** (中位负) | gross 1.5-2.5% - 3% fee - 0.3% slippage |
| Sharpe (paper 目标) | 1.0-1.5 | **0.8-1.2** | hit -2pp + edge 变薄联合影响 |
| Max drawdown | ≤ 10% | ≤ 10% (不变) | RM 硬 cap 不变 |
| Decay tau | 25s | 25s (不变) | 信息时差来源不变 |

**净 edge 中位为负的含义**: 在 C2 ≥ 6¢ 过滤前，原始信号净 edge 为负。v0.2 的 C2 ≥ 6¢ 是筛选"大 dev 高质量"信号，预期 C2 ≥ 6¢ 子集净 edge ≈ +0.3-0.8%（老彭 W9 W5 §5.2 估算）。

### 4.2 Alpha Decay 曲线 (维持 v0.1, tau = 25s)

```
alpha_t = alpha_0 * exp(-t / tau),  tau = 25s

t=0s   : 100%  — event 发生, bet365 已更新, PM 尚未调价
t=10s  : ~67%  — 最佳入场窗口; bet365 领先 PM 5-15s
t=30s  : ~30%  — 多数 take_profit 发生区间
t=60s  : ~9%   — edge ≈ taker fee (3%), 接近边界
t=90s  : ~3%   — time_stop 强制平
```

decay 来源是 Goalserve → PM 调价机器人滞后, 与 de-vig 精度无关, tau 不变。

### 4.3 C2 ≥ 6¢ 门槛的净 edge 估算

```
以 Moneyline near-even 赔率 (fv ≈ 0.55) 近似:

C2 ≥ 6¢ 等效于 gross edge > 4%
gross edge > 4% → net edge = 4% - 3% (fee) - 0.3% (slippage) = +0.7%  (有正期望)

C2 = 5¢ (v0.1) 等效于 gross edge > 3.3%
gross edge = 3.3% → net edge = 3.3% - 3% - 0.3% = 0%  (盈亏平衡边界, 不充分)
```

**结论**: C2 从 5¢ 升至 6¢ 是必要条件, 否则净 edge 大概率为负。

### 4.4 验收度量 (老彭回测目标, 与 v0.1 对齐)

| 度量 | IS 阈值 | OOS 阈值 | 备注 |
|---|---|---|---|
| Hit rate | ≥ 55% | ≥ 54% | v0.2 调整 (v0.1: IS 56%, OOS 54%) |
| Net edge / trade | ≥ 0.5¢ (含 3% fee) | ≥ 0.3¢ | v0.2 新增净 edge 阈值 |
| Sharpe (年化) | ≥ 0.8 | ≥ 0.7 | v0.2 调整 (v0.1: IS 1.0, OOS 0.8) |
| Max drawdown | ≤ 10% | ≤ 12% | 不变 |
| OOS/IS Sharpe ratio | ≥ 0.6 | — | 过拟合检验, 不变 |

---

## 5. 数据源依赖

### 5.1 数据源清单

| 数据 | 接口 | Owner | 用途 | v0.2 变更 |
|---|---|---|---|---|
| inplay event (goal/red/penalty) | `inplay.goalserve.com/inplay-soccer.gz` (v3 §1) | 小段 | C1 事件触发 | 无 |
| inplay odds (value_eu, **单 bet365**) | 同上, `events.<id>.odds.<mid>.participants.<pid>.value_eu` | 小段 | C2 fair_value 计算 | **v0.2 明确单 bet365 de-vig (ADR-008 §5 例外)** |
| PM mid / microprice | Polymarket WSS market channel (小冯 W5-02) | 小冯 | C2 偏离计算 | 无 |
| PM orderbook depth | 同上, `book` event | 小袁 | C3 流动性门槛 | 无 |
| fill_rate | FillRateModel lib (小袁 v0.1) | 小袁 | C4 | 无 |
| LiveSection | GoalserveClient (小段 W4 v0.1) | 小段 | C5 | 无 |

### 5.2 de-vig 方法 (v0.2 修正)

```
// v0.1: 错误假设 9-家多源均值
// v0.2: 单 bet365 multiplicative de-vig (ADR-008 §5 inplay 例外)

overround_b365 = 1/value_eu_home + 1/value_eu_draw + 1/value_eu_away
fair_p_home    = (1/value_eu_home) / overround_b365
fair_p_draw    = (1/value_eu_draw) / overround_b365
fair_p_away    = (1/value_eu_away) / overround_b365

// 精度损失: 0.7-1.2pp vs 9-家均值 (老彭 W8 ack §2.2)
// 补偿: C2 门槛从 5¢ 升至 6¢/7¢ 方向性
```

suspend 期间: 使用上一个 suspend=="0" 快照, 时效 ≤ 10s; 超 10s 则 C2 不满足。

### 5.3 与 P0-01 正交性 (维持 v0.1 结论)

P0-01 (pregame 6h) vs P0-02 (inplay 90s): 时间维度不同, 触发机制不同, covariance < 0.2。精确数字待老彭历史回测联合输出。

---

## 6. ADR-027 Cite

本 spec v0.2 不涉及 OrderIntent / SignedOrder / Position / MarketInfo / FairValue / OrderBookSnapshot 6 个核心 struct 改动。

ADR-027 cite: **N/A** (纯 signal spec doc, 无核心 struct 改动)。

C++ 实现阶段 (小卢), 若修改 FairValue struct 接入 inplay de-vig 路径, 需补 ADR-027 Enforce-1 cite。

---

## 7. W10 W2 派单 (配套 P0-03 spec 同步派出)

### 7.1 小卢 IC pool — P0-02 cpp 实现 (W10 W3 起)

- 依赖: v0.2 spec ack (小梁 review → GM ack)
- 交付: `p0_02_score_price_mismatch.hpp/.cpp` + CMakeLists 更新 + `tests/unit/test_p0_02.cpp` ≥ 20 test case, ctest 100% pass, `-Werror` 全过
- 变更点 vs v0.1: C2 判断加方向性分支 (home/over → 7¢, 其他 → 6¢) + 死区 25%/$500 逻辑 + 单 bet365 de-vig 入参 (无多源均值)

### 7.2 老彭 — P0-02 历史回测 (W10 W3)

- 用 v0.2 C2 ≥ 6¢ (方向性) 重跑 inplay 历史子样本
- 输出: IS/OOS hit rate / net edge / Sharpe, 与 §4.4 验收度量对比
- P0-02 vs P0-03 covariance matrix 联合输出

### 7.3 小蒋 — paper engine 联调 (W10 W4 起)

- P0-02 走 paper_engine, 首笔 paper 成交
- P0-02 与 P0-03 同 event 反向触发检测 (老彭 W9 W5 §7.3 要求)

---

## 8. 开放问题 (继承 v0.1, 状态更新)

| # | 问题 | Owner | 状态 | 截止 |
|---|---|---|---|---|
| OQ-P02-1 | Goalserve inplay-soccer.gz 的 goal/red_card 是否有独立字段 | 小段 | Open | W10 W2 |
| OQ-P02-2 | Soccer gameday PM depth 真实值 (Soccer N=29 全是 outright) | 小袁 | Open | M1 (7/9) |
| OQ-P02-3 | 9 家 bookmaker inplay 覆盖率 | 老彭 + 小段 | **ACK by 老彭 W8 W2** — 单 bet365 确认 | 已关 |
| OQ-P02-4 | P0-02 Soccer 触发频率 daylight 分布 | 小梁 | Open (spec review 时) | W10 W2 |
| OQ-P02-5 (新) | P0-02 vs P0-03 同 event 反向触发: 是否可能同时触发反向信号? | 小程 + 老彭 | Open | W10 W2 联合 spec |
| OQ-P02-6 (新) | C2 ≥ 7¢ 对主队/Over 方向触发频率的实测影响 (预期 -30% 触发) | 老彭回测 | Open | W10 W3 |

---

## 9. 不耻下问记录 (v0.2 新增待确认)

- **@小段**: OQ-P02-1 inplay state 码 goal/red_card 独立 bit 状态 (W10 W2 截止)
- **@小袁**: FillRateModel INPLAY 路径 adverse_selection_score 接口 — 30s 衰减逻辑是否已实现 (W10 W2 前 1:1 对齐)
- **@老彭**: P0-02 v0.2 C2 ≥ 6¢/7¢ 回测子样本 (W10 W3 截止); P0-02 vs P0-03 covariance (联合输出)
- **@老钱 CPO**: G3 KR 方案确认 — 方案一 (hit ≥ 56%) 还是方案二 (C2 门禁前置)? 截止 6/01 (见老彭 W9 W2 gross/net confirm §3)
- **@小梁**: v0.2 整体 review + G3 KR 方案 ack + ADR-008 §5 例外条款处理方式 (§5 子条款 vs v1.1 complete update?)

---

## 10. Spec Testable 自检 (v0.2 更新)

新增 C2 方向性逻辑 + 死区逻辑测试 case：

| 条件 | 新增 v0.2 test case |
|---|---|
| C2 方向性门槛 | TC2e: home 方向 |dev|=0.065 → C2=false (需≥7¢); TC2f: away 方向 |dev|=0.065 → C2=true |
| C2 方向性门槛 | TC2g: home 方向 |dev|=0.075 → C2=true; TC2h: away 方向 |dev|=0.055 → C2=false (需≥6¢) |
| 死区 25% | TC_dz1: fv=0.80 → size=0 (score dead-zone); TC_dz2: fv=0.65 → size 正常计算 |
| 死区 $500 | TC_dz3: kelly 计算 size_raw=$300 → size=0 (size dead-zone); TC_dz4: size_raw=$600 → size=$600 |

继承 v0.1 §10 的 14 个 test case (TC1a-TC_r20), IC pool 小卢实现时合并扩充到 ≥ 25 个。

---

**v0.2 完成汇报**:

P0-02 spec v0.2 核心变更 4 点: (1) alpha 命名修正 edge = post-bias pre-fee 1.5-2.5%; (2) C2 ≥ 6¢/7¢ 方向性门槛; (3) 死区 25%/$500 正式写入; (4) 单 bet365 de-vig 路径 (ADR-008 §5 例外) 正式落 spec。

净 edge 中位为负是已知问题, C2 门槛收紧是保证 MVP 阶段正期望的必要条件。OOS 验收门槛相应下调 (Sharpe ≥ 0.7 OOS, hit ≥ 54% OOS)。

待 **小梁 first review → GM ack → 老彭 W10 W3 回测** 后进 cpp 实现。

— 小程，2026-05-29
