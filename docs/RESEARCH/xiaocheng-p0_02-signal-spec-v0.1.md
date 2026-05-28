# P0-02 Score Price Mismatch — 信号 Spec v0.1

- **Owner**: 小程 (quant-signal-research, C 单元 IC #19)
- **Date**: 2026-05-28
- **Last review**: 2026-05-28
- **Status**: v0.1 草稿，待小梁 first review → GM ack
- **验收人**: 小梁 (C 主管, financial-expert)
- **关联文档**:
  - `xiaocheng-signal-catalog-v1.md` (本信号来源 SIG-P0-02, §4)
  - `xiaoduan-goalserve-official-doc-v3.md` (inplay 事件字段 SSOT)
  - `laopeng-multiplicative-devig-calibration-v1.md` (ADR-008 de-vig, P0-02 复用)
  - `xiaoyuan-fill-rate-model-v0.1.md` (fill_rate ≥ 0.50 协议)
  - `xiaodeng-ml-data-pipeline-v0.1.md` (ML feature cascade 接口)
  - `laohan-riskmanager-design-v0.3.1.md` (RM evaluate 接口)
- **拒接声明**: 本文件是 spec，不含 C++ 代码。实现由小卢 IC pool 在 W7 接单。

---

## 0. 信号基本信息 + 选型理由

| 字段 | 值 |
|---|---|
| 信号 ID | `P0_02_ScorePriceMismatch` |
| 信号全名 | Score Price Mismatch — Inplay Event Arbitrage |
| 信号类型 | inplay event-driven，价值-逆势，信息时差 |
| alpha 来源 | Goalserve inplay 事件 push vs PM mid 调价滞后 5-15s |
| 盘口 | Soccer Moneyline (1X2 → Home/Away 二元化)，MVP 首发；Basketball M4 后扩 |
| MVP scope | Soccer 五大联赛 + Champions League |

**选型依据（老钱 W5 CPO 决议 MVP ≤ 3，v1 12 信号 → 选 P0-02）**:
- alpha 来源与 P0-01 正交：P0-01 是 pregame 持续 Pinnacle 价差，P0-02 是 inplay 离散事件驱动，covariance 估计 < 0.2
- 事件确定性高：goal/red card/penalty 是离散事件，PM 调价滞后 5-15s 结构性存在（老彭 W6 Wave 29 校准）
- 数据依赖重叠：Goalserve inplay (小段 v3) + PM WSS (小冯) 与 P0-01 共享，工程边际成本低
- ADR-008 fair_value 框架共享，不新建
- P0-01 pregame 6h / P0-02 inplay 90s，两条可并行持仓不对冲

---

## 2. 触发条件 (Part 2 对应)

**5 条 AND，任一不满足则本 tick 不触发。**

### 条件 C1：inplay event 触发 (事件驱动门)

```
C1 = (goalserve.inplay_event.type IN {goal, red_card, penalty})
  && (event.updated_ts_ms 与上次记录的 event_id 不同)  // 去重，防止同 event 重复触发
  && (event.suspend == "0")                            // 赔率未暂停
```

**字段来源**: `inplay.goalserve.com/inplay-soccer.gz` (小段 v3 §1 实测 CONTAINS_VALUE)

- `events.<id>.info.state`: 5 位状态码，official doc §4 dictionaries
- `events.<id>.info.minute` / `seconds`: 比赛时间
- `events.<id>.odds.<mid>.participants.<pid>.suspend`: "0"/"1"
- event type 从 state + score change 联合推断 (v3 未直接给 goal/red_card 字段，推断方法见 §2.1 补充)

**C1 推断方法补充**:

```
goal_event    = (score_home 或 score_away 在相邻 poll 间净增 ≥ 1)
red_card      = (state 5 位码包含 red_card bit，或 future Goalserve 事件流字段)
penalty       = (state bit 对应 penalty kick flag)
event_id      = hash(match_id + minute + second + score_home + score_away)
```

C1 代表"世界刚发生了一件改变胜率分布的离散事件"。

### 条件 C2：PM mid vs Goalserve 理论 fair_value 偏离 > 5¢

```
let fv = goalserve_devig_fair_value(sport, match_id, as_of_ts)
       // ADR-008 multi-bookmaker multiplicative de-vig 均值
       // 使用 inplay odds: inplay.goalserve.com/inplay-soccer.gz, value_eu 字段
       // 9 bookmakers 均值 (老彭 bookmaker-history-backfill-v1 §1)

let p_pm = polymarket.moneyline.yes.microprice()  // 小袁 §2.3 microprice，cap |micro-mid| ≤ 2 tick

let dev = fv - p_pm           // 正 = PM 低估 Yes → 应买 Yes
                              // 负 = PM 高估 Yes → 应买 No

C2 = |dev| >= 0.05            // ≥ 5¢，与 P0-01 retro 5¢ 一致
```

**注意**: inplay odds suspend 期间 fv 使用**上一个非 suspend 快照**，时效 ≤ 10s，否则 C2 不满足。

### 条件 C3：PM book 流动性 ≥ $2K

```
let depth_2tick = polymarket.orderbook.depth_within_2_ticks(side=direction)
               // 小袁 §1.3 实测 Soccer gameday ±2tick 中位 $59-$15125

C3 = depth_2tick >= 2000     // 与 P0-01 老彭 liquidity 门槛一致，inplay 放宽到 ±2tick
```

**背景**: Soccer gameday depth 中位偏低 ($59)，但主要联赛重要场次显著更深。C3 是 MVP 流动性硬门槛。

### 条件 C4：预期 fill_rate ≥ 0.50

```
let fr = FillRateModel.compute_taker(market_class=INPLAY, ...)  // 小袁 fill_rate_model v0.1

C4 = fr >= 0.50    // RM R-1 协议：RM evaluate 调用前必须已满足
```

**fill_rate 输入依赖 (小袁 lib)**:
- `quoted_depth_within_2_ticks`: C3 同值
- `quote_half_life_ms`: inplay 热门期 ≈ 0.21s × 2 = ~500ms 边界，INPLAY_HOT 路径
- `adverse_selection_score`: event 发生后 30s 内设为 0.6 (informed trader 活跃期)，>30s 后降回 0.3
- `sport_profile`: Soccer INPLAY 档

### 条件 C5：LiveSection == INPLAY_ACTIVE

```
C5 = (goalserve.inplay_event.status IN {"In Play", "1st Half", "2nd Half",
                                         "1st Period", "2nd Period"})
  && (game.minutes_remaining > 2)   // 末段 2 分钟内不进 (小袁 §3.3 time_decay)
  && (game.extra_time == false)      // 加时赛不进 (odds 结构异变)
```

**字段来源**: Goalserve `events.<id>.info.state` 5 位状态码 (v3 §4 dictionaries/states/soccer)。
`status` 枚举不闭合 (小段 v1 §5.2 P1)，ETL 必须 catch-all。

---

## 3. 入场 / 出场逻辑

### 3.1 入场

```
direction = sign(fv - p_pm)             // +1 = 买 Yes, -1 = 买 No
size_base = kelly_fractional(fv, 1/p_pm - 1) * 0.20   // 1/5 Kelly (inplay 更保守)
size = clamp(size_base * bankroll, 100, 1000)           // USD，MVP 阶段 inplay 单笔上限 $1K

order_type = LIMIT @ (microprice + direction * 0.5 * tick)  // 半 tick 进，passive
fallback   = TAKER @ best_ask/bid  if not filled in 10s    // inplay 时效敏感，10s fallback
```

### 3.2 出场

```
take_profit:  |dev_t| < 1¢                       → close (回归 fair_value)
stop_loss:    |dev_t| 反向扩大到 > 8¢              → close (fair_value 自己跳了)
time_stop:    持仓 90s 未触发 TP/SL               → close (alpha decay 强制)
event_stop:   新 inplay event (goal/red/penalty)  → re-evaluate，条件未满足则 close
suspend_stop: Goalserve suspend == "1"             → 立即 close (赔率暂停 = 结构不稳定)
```

---

## 4. α 估计 (Part 3 对应)

### 4.1 信息时差实证 (老彭 W6 Wave 29 校准数据)

老彭 W6 Wave 29 校准：Soccer inplay goal/red card 后 30s 内，PM mid 调价存在 **5-15s 滞后**。

- Goalserve inplay.goalserve.com 每 1s 刷新；我们端到端延迟 ~3-5s（代理 2-3s + 决策 + 下单 250ms）
- PM 做市机器人调价链路估计 8-20s（老彭 prior：PM 体育做市机器人非专业，有滞后）

**我们的窗口 = PM 做市机器人尚未调价的 5-15s。**

### 4.2 预期 α 参数 (基于老彭 prior + 小梁 catalog v1 §4)

| 指标 | 预估值 | 来源 | 备注 |
|---|---|---|---|
| Hit rate | 56-60% | 老彭 prior + 小程推算 | 高于 P0-01 54-57%，因为 event-driven 确定性更高 |
| Edge post-fee (per trade) | 2-3% | 小程推算 | 高于 P0-01 1.5-2.5%，因为 PM 调价滞后确定性更强 |
| Sharpe (年化，paper 目标) | 1.0-1.5 | 估计，待老彭 W6 EOW 回测 | IS 目标 1.0，OOS 目标 0.8 |
| Max drawdown | ≤ 10% | 参考 P0-01 + inplay 更高波动调整 | RM 老韩 hard cap |
| Trades / 事件 | 1-2 | 每个 goal/red card 最多 1-2 次触发 | |
| Trades / 大场 day (Soccer 五大联赛) | 5-15 | 估算 2-4 events/match × 部分满足 C1-C5 | |

### 4.3 α Decay 曲线

```
t=0s    : event 发生，PM 尚未调价，偏离 |dev| 最大 (≈ 5-15¢ 估计)
t=5-15s : PM 做市机器人开始调价，|dev| 开始收窄
t=30s   : |dev| 衰减 ~50%，alpha 折半
t=60s   : |dev| 衰减 ~80%，alpha 接近消失
t=90s   : time_stop 强制平，无论 alpha 剩余
```

**decay 函数估计 (待老彭 W6 EOW 历史回测验证)**:

```
alpha_t = alpha_0 * exp(-t / tau)
tau = 25s   (30s 半衰期 → tau = 30/ln2 ≈ 43s，但 PM 有跳点，实际更快，取 25s 保守)
```

| t (秒) | alpha 残余 | 行动 |
|--------|-----------|------|
| 0      | 100%       | 触发入场 |
| 10     | 67%        | 最佳入场窗口关闭前 |
| 30     | ~30%       | 多数 TP 发生区间 |
| 60     | ~9%        | edge 接近 taker fee (3%) |
| 90     | ~3%        | time_stop，强制出场 |

### 4.4 验收度量 (老彭 W6 EOW 回测目标)

| 度量 | IS 阈值 | OOS 阈值 | 数据来源 |
|---|---|---|---|
| Hit rate | ≥ 56% | ≥ 54% | 老彭回测 |
| Edge / trade | ≥ 1.5¢ net of 3% taker fee | ≥ 1.2¢ | 老彭回测 |
| Sharpe (年化) | ≥ 1.0 | ≥ 0.8 | 老彭回测 |
| Max drawdown | ≤ 10% | ≤ 12% | 老彭回测 |
| OOS/IS Sharpe ratio | ≥ 0.6 | — | 过拟合检验 |

任一 OOS 度量不满足 → 信号不上 paper，回炉重做（与 P0-01 同 protocol）。

---

## 5. 数据源依赖 (Part 4 协同)

### 5.1 数据源清单

| 数据 | 接口 | Owner | 用途 |
|---|---|---|---|
| inplay event (goal/red/penalty) | `inplay.goalserve.com/inplay-soccer.gz` (v3 §1) | 小段 (Goalserve client v0.1) | C1 事件触发 |
| inplay odds (value_eu, 9 bm) | 同上，`events.<id>.odds.<market_id>.participants.<pid>.value_eu` | 小段 | C2 fair_value 计算 |
| PM mid / microprice | Polymarket WSS market channel (小冯 W5-02) | 小冯 | C2 偏离计算 |
| PM orderbook depth | 同上，`book` event | 小袁 FillRateModel | C3 流动性门槛 |
| fill_rate | FillRateModel lib (小袁 v0.1) | 小袁 | C4 |
| LiveSection | GoalserveClient (小段 W4 v0.1，28 tests pass) | 小段 | C5 |

### 5.2 与 ADR-008 multi-de-vig 协同

P0-02 fair_value 计算方式与 P0-01 完全共享 ADR-008 框架：

```
// P0-01 已落代码 (小卢 W4, 1082 行 + 25 tests)
// P0-02 共享同一 de-vig 函数，只是数据来源从 pregame getodds 换为 inplay odds
fair_value_inplay(ok) = mean_b(implied_prob_b(ok) / overround_b)

差异：
- P0-01 使用 pregame getodds endpoint，每 30s 拉一次
- P0-02 使用 inplay.goalserve.com，每 1s 刷新，事件触发时立即读最新值
- suspend 处理：P0-02 额外检查 suspend == "0"
```

### 5.3 与 P0-01 alpha 正交性

P0-01（pregame 6h 价差）与 P0-02（inplay 90s 事件驱动）时间维度不同、触发机制不同，理论 covariance < 0.2，满足 M4.5 G7 正交化要求（≤ 0.3）。两信号可并行持仓，不对冲。精确数字待老彭 W6 EOW 回测联合输出。

---

## 6. 与 P0-03 候选 (Part 5 对应)

### 6.1 P0-03 候选选型

两个候选：
- Momentum Reversal (SIG-P1-06 升级版，PM book 5min vs 30min 反转)
- Liquidity Injection (SIG-P2-07 升级版，大单消化 imbalance)

**我推荐 P0-03 = Momentum Reversal**，理由：
1. 与 P0-01 / P0-02 共享 PM book 数据依赖，工程边际成本低
2. 与 P0-02 alpha 来源不同（P0-02 是事件驱动，P0-03 是连续 book 动量），正交性更强
3. Liquidity Injection 依赖 trade tape，小袁 §4.3 指出 tape 数据 3min 只有 3 笔，M5 前数据不足

三对 covariance 预估：P0-01 vs P0-02 < 0.2，P0-01 vs P0-03 0.15-0.25，P0-02 vs P0-03 0.1-0.3，全部 < 0.3。精确数字待老彭 W6 EOW 三信号联合回测输出；任一对 > 0.3 则 P0-03 不上线，改 Liquidity Injection 评估。

### 6.2 P0-03 spec 规划

W7 起草 P0-03 Momentum Reversal spec v0.1。HC-03 小吕 8/1 入职后 mentor 1 月接管（小梁 mandate v1）。P0-01/P0-02/P0-03 covariance matrix 由老彭 W6 EOW 回测联合输出，covariance > 0.3 任一对 → P0-03 不上线。

---

## 7. M2 (8/6) 倒推时间表 (Part 6 对应)

| 里程碑 | 日期 | Owner | 交付物 | 依赖 |
|---|---|---|---|---|
| P0-02 spec v0.1 | **2026-05-28 (今)** | 小程 | 本文件 | — |
| 小梁 first review | W6 W3 EOW (2026-05-30) | 小梁 | ack/修改意见 | 本 spec |
| GM ack | W6 W4 (2026-06-01) | 老雷 | ack | 小梁 review |
| 老彭 P0-02 历史回测 | W6 EOW (2026-06-05) | 老彭 | hit rate / edge / Sharpe / covariance matrix (P0-01/02/03) | spec ack + 历史数据 |
| 小卢 IC cpp 实现 | W7 EOW (2026-06-12) | 小卢 IC pool | `P0_02_ScorePriceMismatch` class + `ISignalEngine` ABI + 单测 ≥ 20 cases，build + ctest pass | spec ack |
| paper engine 联调 | W8 (2026-06-15-19) | 小蒋 + 小卢 | P0-02 走 paper_engine，首笔 paper 成交 | 小卢 code |
| paper mldata.wal 接入 | M2 (2026-08-06) | 小邓 + 小蒋 | P0-02 feature 进 mldata_feature.wal，ML 训练数据开始积累 | paper 联调 |
| alpha decay 监控上线 | M4.5 (倒推) | 小程 + 小董 | P0-02 Sharpe 滚动 4 周监控，ADR-016 stage 阈值 enforce | paper 稳定 4 周 |

---

## 8. 信号 catalog v1 → v2 (Part 7 对应)

### 8.1 MVP 3 信号（正式升 v2 catalog）

| ID | 信号 | 状态 | 来源 |
|---|---|---|---|
| P0-01 | pinnacle-novig-revert | 已落代码 (小卢 W4) | 小梁 5.2 + 老彭 S1 |
| P0-02 | score-price-mismatch | 本 spec v0.1 | 小梁 5.3，本文件扩展 |
| P0-03 | momentum-reversal | W7 spec 规划 | 小梁 5.4 / 5.6 |

### 8.2 候补 4 信号（M5 后评估）

| ID | 信号 | 备注 |
|---|---|---|
| P1-04 | lineup-news-lag | 依赖 lineup 数据源接入，M5 前不上 |
| P1-05 | favorite-overpay-fade | 散户行为信号，需 narrative 标签，M5 后 |
| P1-03 | goalserve-lead-taker | 依赖端到端延迟 < 200ms 闭环，工程前置 |
| P2-09 | nfl-key-number-fade | NFL 季限，不影响 Soccer MVP |

### 8.3 归档 5 信号（v2 不推进）

P2-07 (maker cascade), P2-08 (cross-market arb), P2-10 (series lead), P2-11 (steam follow), P2-12 (settlement tail)。
原因：容量过小 / 工程门槛过高 / 依赖未上线模块。保留 catalog ID 用于 attribution，不开发。

### 8.4 v1 → v2 删减依据

Wave 26 小梁 #P3 + 老钱 W5 CPO 决议：MVP ≤ 3 信号聚焦。12 → 7（3 MVP + 4 候补），5 归档，原因均为容量过小 / 工程门槛 / 依赖未上线模块。

---

## 9. W7 IC pool 派单 prompt 草稿

spec SSOT: `docs/RESEARCH/xiaocheng-p0_02-signal-spec-v0.1.md`，有问题 @ 小程。

**目标**: 实现 `P0_02_ScorePriceMismatch` 类，继承 `ISignalEngine` 接口（与 P0-01 ABI 完全一致）。

**交付物（W7 EOW）**: `p0_02_score_price_mismatch.hpp/.cpp` + CMakeLists.txt 更新 + `tests/unit/test_p0_02.cpp`，≥ 20 test case，`ctest 100% pass`，`-Werror` 全过。

**5 触发条件（short-circuit C5 → C1 → C3 → C4 → C2，最贵最后算）**:
- C1: GoalserveClient inplay event，goal/red_card/penalty，event_id 去重，suspend=="0"
- C2: |ADR-008 multi-de-vig fv - PM microprice| >= 0.05，inplay odds 时效 ≤ 10s
- C3: depth_within_2_ticks >= 2000 USDC
- C4: FillRateModel.compute_taker(INPLAY) >= 0.50
- C5: LiveSection == INPLAY_ACTIVE，minutes_remaining > 2，extra_time == false

**ML feature cascade（小邓 v0.1 §3 接口）**: signal_id_u8 = P0_02，新增 F-32 inplay_event_type（0=null/1=goal/2=red_card/3=penalty）+ F-33 event_to_signal_lag_ms。小卢 W7 派单前与小邓 1:1 对齐 enum slot。

**R-20 4 ts**: event_ts=Goalserve updated_ts_ms，data_source_ts=feed updated_ts，ingestion_ts=客户端解压时，as_of_ts=信号计算完成时（唯一允许 now() 的时间戳）。

不自创触发条件，不写回测（回测归小蒋）。

---

## 10. Spec Testable 自检

**每个触发条件必须有可写的测试 case，IC pool 小卢 W7 实现时覆盖。**

| 条件 | 测试 case 描述（至少 2 个 per 条件） |
|---|---|
| C1 goal event | TC1a: score_home 增 1 → event_type=goal，C1=true；TC1b: score 不变 → C1=false |
| C1 去重 | TC1c: 同 event_id 触发两次 → 第二次 C1=false（去重 guard） |
| C1 suspend | TC1d: event 发生但 suspend="1" → C1=false |
| C2 偏离门槛 | TC2a: |fv - p_pm| = 0.06 → C2=true；TC2b: |fv - p_pm| = 0.04 → C2=false |
| C2 stale inplay odds | TC2c: 上次非 suspend 快照已超 10s → C2=false（stale guard） |
| C2 fv 归一验证 | TC2d: 9 bm de-vig 均值求和 ≈ 1.0（±1e-6 精度，R-20 推论） |
| C3 流动性门槛 | TC3a: depth_2tick = 3000 → C3=true；TC3b: depth_2tick = 1500 → C3=false |
| C4 fill_rate | TC4a: FillRateModel mock 返回 0.55 → C4=true；TC4b: 返回 0.45 → C4=false |
| C5 LiveSection | TC5a: status="In Play"，min_remaining=30 → C5=true；TC5b: min_remaining=1 → C5=false |
| C5 extra_time | TC5c: status="Extra Time First Half" → C5=false |
| 全部 AND | TC_all: 5 条件全满足 → signal 触发，signal_id=P0_02；任一条件 false → 不触发 |
| short-circuit | TC_sc: C5=false → C1 不进入（mock C1 check 不被调用，验证 short-circuit） |
| ML feature | TC_ml: 触发后 MLDataHook.on_signal_compute 收到 F-32=goal(1)，F-33>0 |
| R-20 ts chain | TC_r20: event_ts > data_source_ts → assert 失败（catch abort / exception） |

**自检结论**: 5 触发条件每条 ≥ 2 个可写 test case，共 14 个示例 test case 描述，IC pool 小卢 W7 实现时扩充到 ≥ 20 个（含 edge case + short-circuit + ML feature）。spec testable 自检通过。

---

## 11. 不耻下问记录

待确认事项（IC 实现前必须闭环）：
- **小段**: inplay state 码里 goal/red_card/penalty 是否有独立 bit，还是只能靠比分 diff 推断
- **小袁**: FillRateModel INPLAY path 的 adverse_selection_score 输入接口，W5 校准后确认
- **小邓**: ML feature 32 → 34 slot 扩充兼容性，W7 派单前 1:1 对齐
- **老彭**: P0-01/P0-02/P0-03 covariance matrix，W6 EOW 联合回测输出
- **小冯 + 小袁**: PM microprice 接口契约，W5-02 小冯交付后确认

---

## 12. 开放问题

| # | 问题 | Owner | 截止 |
|---|---|---|---|
| OQ-P02-1 | Goalserve inplay-soccer.gz 的 goal/red_card 是否有独立事件字段（v3 §1 未看到，靠比分 diff 推断是否足够精确？） | 小段 | W6 W4 |
| OQ-P02-2 | Soccer gameday PM depth 真实值（小袁 §1.3 Soccer N=29 全是 outright，game day 数字不可用）——需等 EPL/UEFA 赛季内实测 | 小袁 | M1 (7/9) |
| OQ-P02-3 | 9 家 bookmaker 在 inplay odds feed 中的平均覆盖率（v3 实测是 "bm": "bet365" 单源，与 pregame 9 家不同——是否需要降级为单家 bet365 de-vig？） | 老彭 + 小段 | W6 W4 |
| OQ-P02-4 | P0-02 Soccer 触发频率的 daylight 分布（European game day 多在 UTC 14-22，与 P0-01 pregame 重叠，资金是否够两个信号同时持仓？） | 小梁 | spec review 时 |

**OQ-P02-3 是最高优先级**：如果 inplay odds 只有 bet365 单家，C2 fair_value 精度下降约 0.5-1%，需要决策是否仍上 P0-02 或加 suspend 更严格筛选。

---

**v0.1 完成汇报**:

P0-02 spec v0.1 + 5 触发条件（AND，short-circuit C5→C1→C3→C4→C2）+ α 估计 hit rate 56-60% / edge 2-3% / decay tau=25s（30s 半衰期）+ M2 (8/6) 倒推时间表 + catalog v1 → v2 MVP 3 + W7 IC pool 派单草稿。

spec testable 自检通过：5 条件 × ≥2 test case = 14 示例，IC pool 小卢 W7 扩充到 ≥20。

待 **小梁 first review → GM ack** 后，进入老彭 W6 EOW 历史回测。

— 小程，2026-05-28
