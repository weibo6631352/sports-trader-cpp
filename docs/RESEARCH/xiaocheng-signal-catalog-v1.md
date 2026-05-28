# 信号假设清单 v1

- Owner: 小程 (quant-signal-research)
- Last review: 2026-05-28
- 验收人: 小梁 (financial-expert)
- 关联 Ticket: S1-017 (本报告) / S1-007 (小梁市场结构) / S1-012 (老彭行业分析) / S1-002 (老李 CLOB 实测) / S1-003 (小段 Goalserve) / S1-D (小余历史数据)
- Status: v1, 待 Sprint-1 末与回测框架 (小蒋) 对接

> 小程按: 这份清单是把小梁市场结构 12 条候选 + 老彭行业 6 条假设整合形式化, 不重复他们已经讲清楚的机制, 我的活是把每条变成可执行的实验设计 + 数据契约 + 回测接力点. 数字里, 凡是带 `[估]` 是我基于小梁 + 老彭口径推算, 凡是带 `[待小蒋回测]` 是必须用历史数据闭环验证后才算数.

---

## 0. 命名约定 + 信号 ID 规则

```
SIG-{P0|P1|P2}-{NN}: {short_name}
  例: SIG-P0-01: pinnacle-novig-revert
       SIG-P0-02: score-price-mismatch
       SIG-P1-03: goalserve-lead-taker
```

- ID 不可变, 一旦上线一个版本 (例如 `pinnacle-novig-revert@v1.2`) 永久保留, 用于 attribution
- `@vN` 表示参数/特征版本号; 重大假设变更必须 bump 主版本
- P0/P1/P2 由本目录给出, 但具体上线顺序由 小梁 + 老钱 + 老周 联合决定

---

## 1. 候选信号总表 (12 条)

| ID | 名称 | 来源 | 类型 | 周期 | 预期 hit rate | 预期 edge/trade | 容量 (单笔) | 容量 (日累计) | 优先级 |
|---|---|---|---|---|---|---|---|---|---|
| SIG-P0-01 | pinnacle-novig-revert | 小梁 5.2 + 老彭 S1 | 价值回归 | pregame 6h | 60-65% | 2-3 ¢ | $1-2K | $20-40K | **P0** |
| SIG-P0-02 | score-price-mismatch | 小梁 5.3 | 价值-逆势 | inplay 1-10min | 55-58% | 1.5-2 ¢ | $500-1K | $10-25K | **P0** |
| SIG-P1-03 | goalserve-lead-taker | 小梁 5.1 + 老彭 S4 | 顺势-时延 | inplay 秒级 | 55-60% | 1-2 ¢ | $300-800 | $5-15K | P1 |
| SIG-P1-04 | lineup-news-lag | 老彭 S2 | 时延-公告 | pregame 5min | 65-70% | 3-5 ¢ | $1-3K | $5-15K | P1 |
| SIG-P1-05 | favorite-overpay-fade | 老彭 S5 + 小梁 5.7 | 价值-逆势 | pregame 3h | 53-58% | 1.5-2.5 ¢ | $1-2K | $10-20K | P1 |
| SIG-P1-06 | event-overreaction-fade | 小梁 5.4 | 逆势-微观 | inplay 1-3min | 53-56% | 2-3 ¢ | $500-1K | $5-12K | P1 |
| SIG-P2-07 | maker-cascade-detect | 小梁 5.5 | 微观-顺势 | 秒 | 52-55% | 1-2 ¢ | $300-600 | $3-8K | P2 |
| SIG-P2-08 | cross-market-arb | 小梁 5.6 | 套利 | 任意 | 偶发 | 3-8 ¢ | $1-3K | 不稳定 | P2 |
| SIG-P2-09 | nfl-key-number-fade | 老彭 S3 | 价值 | pregame | 54-57% | 2-3 ¢ | $1-2K | NFL 季度限定 | P2 |
| SIG-P2-10 | series-lead-overprice | 老彭 S6 | 价值-逆势 | 系列赛级 | 55-60% | 3-5 ¢ | $1-3K | 季后赛限定 | P2 |
| SIG-P2-11 | steam-follow | 小梁 5.8 | 顺势 | 分钟 | 53-55% | 1-2 ¢ | $500-1K | $5-10K | P2 |
| SIG-P2-12 | settlement-tail-yield | 小梁 5.10 | 套利-末端 | 比赛后 2h | 70%+ (低 edge) | 0.5-1 ¢ | $2-5K | 偶发 | P2 |

(对应 小梁 5.9 长尾错价 / 5.11 数据流不一致 / 5.12 news catalyst, 我评估容量过小或工程门槛过高, M5 前不进, 不进本表正式列, 仅作为附录 B 候选.)

---

## 2. 优先级分组

### 2.1 P0 — MVP 首发 (2 条)

**SIG-P0-01: pinnacle-novig-revert** — 小梁特批 (S1-017 主任务) + 老彭 #1 推荐. 详细实验设计见第 3 节.

**SIG-P0-02: score-price-mismatch** — 小梁 5.3 衍生. 详细实验设计见第 4 节. 选 P0 不是 P1 的理由:
- 与 P0-01 互补: P0-01 是 pregame, P0-02 是 inplay; 两条并发可以让 MVP 在 pregame + inplay 都有信号, 避免 inplay 段空跑
- 数据依赖与 P0-01 部分重叠 (都需要 Polymarket book + 历史成交), 工程边际成本低
- hit rate 53% 已经在 KR-C-4 阈值边缘, 老韩 + 老钱已经知道

### 2.2 P1 — M5 上线候选 (4 条)

- **SIG-P1-03**: goalserve-lead-taker — 需要先等 Q4/Q5/Q6 (端到端延迟 + Goalserve push 延迟) 闭环, 这是工程前置条件
- **SIG-P1-04**: lineup-news-lag — 需要 lineup 数据源选型 (老彭推荐 Rotoworld + ESPN dual source), 数据接入是前置
- **SIG-P1-05**: favorite-overpay-fade — 散户行为类信号, 需要 narrative 标签 (老彭可手工标注首批)
- **SIG-P1-06**: event-overreaction-fade — 与 P0-02 共享比分模型, 但需要事件分类器 (得分/红牌/受伤)

### 2.3 P2 — 18 月扩展 (6 条)

P2-07 / P2-08 / P2-09 / P2-10 / P2-11 / P2-12.

P2 的共同特征:
- 容量小 (单笔 < $1K 且日累计 < $10K), 不影响 portfolio 总 PnL
- 或周期长 (P2-09 NFL 季限, P2-10 季后赛限)
- 或依赖未上线模块 (P2-07 / P2-08 需要 book WSS 解析完整 + 跨市场状态同步)

P2 信号上线节奏: 等 M5 实盘稳定 4 周, P0/P1 Sharpe > 1.0 落地, 再分批接入.

---

## 3. 详细实验设计: SIG-P0-01 pinnacle-novig-revert

### 3.1 信号正式定义

**触发条件 (机器判定)**:
```
let p_pm = polymarket.moneyline.yes.mid_price()           // [0, 1]
let p_pinn_novig = pinnacle.novig_fair(yes_odds, no_odds) // [0, 1]
let dev = p_pm - p_pinn_novig                              // 偏离 (cents)

trigger = (|dev| >= 0.03)                                  // ≥ 3 ¢
       && (game.kickoff_in_hours <= 6 && >= 0.5)           // pregame 6h ~ 30min
       && (polymarket.book.depth_24h >= 50_000_usdc)       // 流动性门槛 (小梁 8.3 要求)
       && (pinnacle.last_update_age <= 5_min)              // Pinnacle 价不能 stale
       && (no_news_event_within_30s())                     // 老彭 S1 滤波: news gate
```

**入场逻辑**:
```
size_base = kelly_fractional(p_pinn_novig, 1/p_pm - 1) * 0.25  // 1/4 Kelly
size = clamp(size_base * bankroll, 200, 1500)                  // 美刀, MVP 阶段
direction = sign(p_pinn_novig - p_pm)                          // 朝 Pinnacle 方向
order_type = LIMIT @ (mid + dev * 0.3)                         // 入价 = mid + 30% of dev (passive)
fallback = TAKER @ best_ask if not filled in 60s
```

**出场逻辑**:
```
take_profit:  p_pm 与 p_pinn_novig 收敛到 |dev| < 1¢          → close
stop_loss:    |dev| 反向扩大到 > 6¢ (Pinnacle 也动了, 我们看错) → close
time_stop:    持仓 6h 未触发 TP/SL                              → close
news_stop:    持仓期间有 lineup/injury news event              → close (与 P1-04 让路)
```

### 3.2 Pinnacle no-vig 计算公式

[共识公式, 老彭 + 小梁双方都认可]

二元市场 (NBA/NFL/MLB Moneyline, 网球 ML, 1X2 取主胜+客胜重归一化):

```
给定 Pinnacle 美式 odds (yes_odds, no_odds), 转十进制 decimal odds:
    d_yes = american_to_decimal(yes_odds)
    d_no  = american_to_decimal(no_odds)

implied_prob (含 vig):
    p_yes_raw = 1 / d_yes
    p_no_raw  = 1 / d_no
    overround = p_yes_raw + p_no_raw    // 通常 1.02 - 1.06

no-vig fair price (按 multiplicative 方法去 vig, 行业标准):
    p_yes_fair = p_yes_raw / overround
    p_no_fair  = p_no_raw  / overround
    (确保 p_yes_fair + p_no_fair = 1)
```

[注] 行业另一派用 Shin 方法或 power 方法去 vig, 假设大热门 implied vig 不对称. 我建议 MVP 先用 multiplicative (最简单, 在 sharp book 上误差 < 0.3¢), 18 月后若 NBA prop 类信号扩展再考虑 Shin.

三向市场 (足球 1X2) 公式相同, 三个 raw 概率除以 overround 即可.

### 3.3 "偏离 > 3¢, 6h 内 60% 收敛" 假设的 backtest 方案

**Backtest 主流程** (交给小蒋, 我提需求):

```
输入: 配对样本 (game_id, snapshot_ts, p_pm, p_pinn_novig)
    采样: pregame 6h 内每 5min 一次 snapshot
    过滤: depth >= 50K, news_gate
输出: 每条触发 (dev >= 3¢) 的后续轨迹
    度量 1: 6h 内 |dev_t+τ| < 1¢ 是否出现 (二元命中)
    度量 2: |dev_τ| 的衰减速率 (τ = 1h, 2h, 3h, 6h)
    度量 3: 朝 Pinnacle 方向的实现 edge (实际 close 价 - 入场价)
```

**假设的可证伪条件**:
- H0 (null): P(收敛到 < 1¢ in 6h | dev >= 3¢) = baseline ≈ 25-30% (即"无信号时" pregame mid 自然漂移概率)
- H1 (alt): P(收敛 | dev >= 3¢) >= 60%

**统计检验**:
- 用 binomial test, n >= 500, 期望 P_obs - P_baseline >= 30%, 双尾 p < 0.01
- 同步给小董做 effect size: Cohen's h >= 0.5 视为 large effect

**样本量需求**:

| Sport | 时间窗 | 单赛季样本估算 | 是否够 500 | 优先级 |
|---|---|---|---|---|
| NBA | 2024-25 季常规赛 + 季后赛 | ~1230 + 89 = 1319 场, 每场 6h 内 ~ 36 snapshots, 大场约 20% (~260 场) × 2-3 触发 = **~700 触发** | 够 | 主样本 |
| NFL | 2024 季常规赛 + 季后赛 | 285 场, 大场 50% (~140) × 1-3 触发 = **~200 触发** | 不够 (需要 2 季) | 辅样本 |
| MLB | 2024 季 | 2430 场, 大场 10% × 1 = ~240 触发 | 不够 | 辅样本 |
| Soccer 五大联赛 | 2024-25 季 | 2000+ 场, 大场 15% × 2 = ~600 触发 | 够 | 备选 |

**主推方案 (Sprint-1 backtest 第一炮)**:
- Sport = NBA, 时间窗 = 2024-10 ~ 2025-04 (完整一季 + 季后赛), 期望 500-700 触发
- 数据时长 6 个月, 与小梁 5.2 给的需求一致
- 单 sport 单赛季先证明假设, 再扩到 NFL/Soccer 做 robustness 验证

**OOS 验证**:
- IS = 2024-10 ~ 2025-02 (常规赛主体)
- OOS = 2025-02 ~ 2025-04 (常规赛末 + 季后赛)
- 切分理由: 季后赛市场动态可能与常规赛不同 (流动更深, sharp 占比更高), 是天然的 regime shift, 用来测信号 robustness

### 3.4 数据获取方案 (Pinnacle 数据从哪来)

[必须 @老李 协助] Pinnacle 数据获取三个候选路径, 按可行性排序:

**路径 A (推荐): Pinnacle 公开 API (有官方文档)**
- 优点: 直接, 实时, 有官方支持
- 缺点: 需要账号 (老彭手头应该有), 有 rate limit, 跨洋链路要测
- 字段需求: `/v1/odds`, `/v1/leagues`, sport filter
- @老李 帮: 评估 Pinnacle API 跨洋拉数稳定性, 是否需要 colo

**路径 B (备选): 第三方聚合 (OddsPortal / OddsJam / The Odds API)**
- 优点: 单点接入, 已经聚合多家 sharp book
- 缺点: 历史数据通常付费 (The Odds API 历史 6 月 ~$500/月), 延迟不可控
- @老李 帮: 评估 The Odds API 数据质量

**路径 C (最差但可保底): 老彭手工 + 历史 CSV**
- 老彭手头应该有 2024 季 NBA/NFL Pinnacle closing line CSV (CLV 评估必需)
- 缺点: 只有 closing line, 没有 intra-day snapshot, 无法测 "6h 内收敛" 的中间路径
- 仅作为 Sprint-1 末 MVP 灌数据用, 不能长期支撑

**Sprint-1 推荐**: 老李评估路径 A (Pinnacle API), 若 1 周内拉不通则路径 B (The Odds API 付费一个月), 路径 C 用来做 sanity check.

### 3.5 与小蒋 (回测) 的接力点

| 接力项 | 我交付 | 小蒋交付 | 截止 |
|---|---|---|---|
| 信号定义 contract | 触发条件 / 入场 / 出场逻辑 (本节 3.1) | 在 backtest 引擎内实现该 contract | T+2 周 |
| Pinnacle no-vig 计算 | 公式 + 单元测试用例 (本节 3.2) | 内置 utility function | T+2 周 |
| 数据 schema | 配对样本 schema (game_id, ts, p_pm, p_pinn, depth, news_flag) | 数据 loader | T+3 周 |
| 切分约定 | IS/OOS 时间窗 (3.3) | 自动 train/test split | T+3 周 |
| 度量定义 | hit rate / edge / Sharpe / max DD (本节 3.6) | 输出报表模板 | T+3 周 |
| 第一份回测报告 | review + 阈值微调 | 跑通并出 PDF/HTML | T+4 周 (M2 前) |

### 3.6 验收度量

| 度量 | 阈值 | 数据源 |
|---|---|---|
| Hit rate (binary: dev 收敛到 < 1¢ in 6h) | ≥ 55% IS, ≥ 53% OOS | backtest |
| Edge per trade (cents) | ≥ 1.5¢ net of spread | backtest |
| Sharpe (年化) | ≥ 1.0 IS, ≥ 0.8 OOS | backtest |
| Max drawdown | ≤ 8% of bankroll | backtest |
| Trades per month | 20-60 (NBA + NFL 合计) | backtest |
| OOS / IS Sharpe ratio | ≥ 0.6 (避免过拟合) | backtest |

任何一项 OOS 失败, 信号不上线, 回炉重做.

---

## 4. 详细实验设计: SIG-P0-02 score-price-mismatch (P0-2 候选)

### 4.1 信号正式定义

**触发条件**:
```
let state = (sport, league, time_remaining_bucket, score_diff, possession)
let p_model = score_model.predict(state)                  // [0, 1]
let sigma_model = score_model.residual_std(state)         // 历史残差 std

let p_pm = polymarket.moneyline.yes.mid_price()
let z = (p_pm - p_model) / sigma_model                    // 标准化偏离

trigger = (|z| >= 2.0)                                     // 2σ 偏离
       && (sport in ["NBA", "NFL"])                        // P0 仅 NBA/NFL
       && (inplay == true)
       && (depth_in_market >= 30_000_usdc)
       && (time_to_end >= 5_min)                           // 比赛末段不进
```

**入场逻辑**:
```
direction = sign(p_model - p_pm)                          // 朝模型方向 fade
size = clamp(0.15 * Kelly(p_model) * bankroll, 100, 800)  // inplay 单笔更小
order_type = LIMIT @ best_bid/ask 1 tick 进 (passive 1 step)
fallback = TAKER if not filled in 15s (inplay 时效紧)
```

**出场逻辑**:
```
take_profit: |z| < 0.5   → close (回归 model 中性)
stop_loss:   |z| > 3.5   → close (模型可能错, 跑路)
time_stop:   持仓 10min  → close (alpha decay 强假设)
event_stop:  得分变化 OR 关键事件 → re-evaluate, 否则 close
quarter_end_stop: NBA Q 末 30s 内强制平 (流动性骤降)
```

### 4.2 比分模型设计 (score_model)

**模型类型**: 分层回归 + 历史 lookup hybrid

```
基线: 按 (sport, league, time_remaining_bucket=每30s, score_diff_bucket=每3分) 查历史命中率
    → empirical P(home_win | state)
增量: 加入 possession_indicator (谁球权) + clutch_factor (末节加权)
```

**输入特征**:
- `sport`: 枚举 {NBA, NFL}
- `time_remaining_seconds`: int, 0-2880 (NBA 48min) 或 0-3600 (NFL 60min)
- `score_diff`: int, signed (home - away)
- `possession`: enum {home, away, none} (NBA 通常 none 在死球时, NFL 大部分时间有)
- `period`: int (NBA: 1-4 + OT, NFL: 1-4)
- `is_clutch`: bool (last 5 min + diff <= 5)
- `home_team_rating`: float (季前 Elo, 比赛中不变)
- `away_team_rating`: float

**训练数据**:
- 2 年历史 inplay 数据 (2023-24 + 2024-25 NBA 季, 2023 + 2024 NFL 季)
- 数据源: Goalserve livescore 历史 (S1-D 小余 + 小冯 已规划) + ESPN play-by-play API 补充
- 样本量估算: NBA 2 季 × 1230 场 × ~50 状态/场 = ~120K NBA 样本, NFL 2 季 × 285 场 × ~30 = ~17K NFL 样本

**模型验收**:
- 训练集 Brier score < 0.20 (基线: 0.25 = uniform)
- 验证集 Brier score < 0.21 (无显著过拟合)
- 不同 score_diff 区段都有 N >= 100 个样本 (避免极端 state 估计不准)

### 4.3 backtest 设计

**对照组**:
- Control: 随机入场 (随机时刻 sample 同 sport / time bucket, 不看 z), 跑相同期望 trades 数
- Naive: 只在 |z| >= 1σ 入场 (vs 我们用 2σ), 看阈值敏感性

**度量**:

| 度量 | 阈值 |
|---|---|
| Hit rate | ≥ 53% IS, ≥ 51% OOS |
| Edge per trade | ≥ 1.0¢ net |
| Sharpe (年化) | ≥ 0.8 IS, ≥ 0.6 OOS |
| Max DD | ≤ 10% |
| Trades / 比赛 (NBA) | 3-8 |
| Trades / 比赛 (NFL) | 1-3 |

### 4.4 数据依赖

| 字段 | 来源 | 频率 | Owner |
|---|---|---|---|
| 比分轨迹 (历史) | Goalserve livescore + ESPN PBP 补 | 每事件 | 小余 (S1-D) |
| 比分流 (实时) | Goalserve inplay WSS | 1-3s push | 小段 (S1-003) |
| Polymarket book (历史) | S1-D 历史 snapshot | 5s 频率 | 小余 |
| Polymarket book (实时) | Polymarket WSS | tick by tick | 老李 (S1-002) |
| 队伍 Elo rating | 538 / FiveThirtyEight 历史 | 比赛级 | 我自己手工灌 |

### 4.5 风险点

- **模型外推风险**: 极端 state (score_diff > 30 in NBA) 历史样本少, 模型估计不稳 → 加 fallback: 若 N_state < 50, 不触发
- **比分数据延迟**: Goalserve 比分若延迟 > 3s, 我们看到的 state 已经 stale, 触发条件本身就有偏差 → P0-02 必须等 Q5 (Goalserve push 延迟) 闭环
- **Polymarket 价格反向时延 vs P1-03 冲突**: 同一时刻可能两个信号反向开仓 (P1-03 顺势, P0-02 逆势 fade), 必须在 portfolio 层做 reconcile

---

## 5. 特征工程清单

### 5.1 特征总表

| 特征 ID | 名称 | 频率 | 计算复杂度 | 用于信号 | 存储建议 |
|---|---|---|---|---|---|
| F-01 | polymarket.mid | tick (subsec) | O(1) | 全部 | 内存 ring buffer + 1min 落 KV (小田) |
| F-02 | polymarket.book.depth_24h | 5min | O(N_orders) 滑窗 | P0-01, P0-02 | KV 5min 粒度 |
| F-03 | polymarket.book.spread | tick | O(1) | P0-02, P1-06, P2-07 | 内存 |
| F-04 | polymarket.last_trade_size | 事件触发 | O(1) | P2-07, P2-11 | KV |
| F-05 | pinnacle.novig_fair_price | 30s | O(1) | P0-01, P1-05 | KV 30s |
| F-06 | pinnacle.last_update_age | 计算时 | O(1) | P0-01 滤波 | inferred |
| F-07 | goalserve.score_diff | 1s | O(1) | P0-02, P1-03, P1-06 | 内存 ring |
| F-08 | goalserve.time_remaining | 1s | O(1) | P0-02 | 内存 ring |
| F-09 | goalserve.event_flag | 事件触发 | O(1) | P1-03, P1-06 | 事件流 (Kafka-like, 小董) |
| F-10 | score_model.p_predict | 比分变化时 | O(lookup) | P0-02 | precompute table |
| F-11 | score_model.residual_std | 比分变化时 | O(lookup) | P0-02 | precompute table |
| F-12 | depth_imbalance | tick | O(1) | P2-07, P2-11 | 内存 |
| F-13 | book.maker_cancel_rate | 5s 滑窗 | O(N_events) | P2-07 | 滑窗 buffer |
| F-14 | news.lineup_flag | 事件触发 | O(1) | P0-01 滤波, P1-04 | 事件流 |
| F-15 | game.kickoff_in_minutes | 1min | O(1) | P0-01, P1-04, P1-05 | inferred |
| F-16 | game.is_major_market | 比赛级 | O(1) | 全部滤波 | static table |
| F-17 | cross_market.consistency_residual | 1min | O(N_markets) | P2-08 | 内存 |

### 5.2 与小董 / 小田 对接

**实时 feature pipeline (小田)**:
- F-01 / F-03 / F-04 / F-07 / F-08 / F-12: 进 in-process feature store, ring buffer + atomic snapshot
- 拉取延迟必须 < 5ms (策略 hot path 内)

**离线 feature warehouse (小董)**:
- F-02 / F-05 / F-09 / F-10 / F-11 / F-14: KV store (Redis-like) + 时间序列 (Parquet 落盘)
- 历史 feature 必须可重放 (用于 backtest), 命名规则 `feature.{id}.{ts}` time-versioned

**特征版本管理** (与小张 / 小董协商):
- 任何特征定义变更, 必须 bump version `F-XX@v{N}`
- 旧版本必须保留至少 30 天, 用于 attribution

---

## 6. A/B 测试方案

### 6.1 上线后 alpha decay 监控

**目标**: 检测信号上线后是否在 4-8 周内出现 alpha 衰减.

**方法**:

```
分桶 1 (live): 真实交易触发 N 次, 记录 realized PnL
分桶 2 (shadow): 同信号 sandbox 触发 (相同逻辑, 不下单), 记录 paper PnL
分桶 3 (counterfactual): 相同时间窗内随机入场, 记录 random PnL
```

**度量**:
- `live_pnl / shadow_pnl` 比值: 接近 1.0 说明信号本身没 decay, 仅 slippage; 显著 < 1.0 说明实盘 alpha 被吃
- `(live - random) / random_std`: 信号 - 噪声 z-score, 应稳定 > 1.5

**报警阈值** (与小董 data-stats 协作):
- 滚动 4 周 Sharpe < 0.5 → yellow alert (评估)
- 滚动 4 周 Sharpe < 0.2 → red alert (暂停, 重新研究)
- shadow / live ratio > 1.3 持续 2 周 → red alert (实盘有冲击, 容量到顶)

### 6.2 信号融合 A/B

**目标**: 评估 P0-01 + P0-02 同时上线后的 portfolio 协同效应.

**方法**:
- Bucket A: 只跑 P0-01, 50% 资金
- Bucket B: 只跑 P0-02, 50% 资金
- Bucket C (合并后): 两个信号 portfolio, 100% 资金
- 比较 C 的 Sharpe vs (A_Sharpe + B_Sharpe) / 2

**期望**:
- 若 C > avg(A, B) → 信号互补 (不相关), 容量可加
- 若 C ≈ avg(A, B) → 信号同向, 容量受限
- 若 C < avg(A, B) → 信号冲突, 必须做 portfolio decorrelation

### 6.3 容量压测 (M5 后)

- 单笔 size 从 $500 → $1K → $2K → $5K → $10K 渐增
- 测每档 size 下的 realized edge per trade 是否衰减 (m-style impact curve)
- 容量上限 = edge 衰减到 50% 时的 size (老韩定 hard cap)

---

## 7. 与小蒋回测框架的接力点

### 7.1 我交付给小蒋的物件

| 物件 | 形式 | 截止 |
|---|---|---|
| 信号 contract (P0-01, P0-02) | YAML / JSON 描述 (触发 / 入场 / 出场 / 度量) | T+2 周 |
| Pinnacle no-vig 公式 + 单元测试 | Python / C++ 双语实现 + 10 test cases | T+2 周 |
| score_model v1 训练好的 weights | pickled model + feature spec | T+5 周 |
| 数据 loader spec | schema 定义 + sample CSV | T+3 周 |
| IS/OOS 切分约定 | 时间窗表格 | T+2 周 |
| 度量阈值 | 第 3.6 / 4.3 节表格 | T+2 周 |

### 7.2 小蒋交付给我的物件

| 物件 | 形式 | 截止 |
|---|---|---|
| backtest 引擎 | 可接 contract, 支持 walk-forward, 含 slippage 模型 | T+4 周 (小蒋自己排期) |
| 第一次 P0-01 回测报告 | PDF/HTML, 含 hit rate / edge / Sharpe / DD | T+5 周 |
| 第一次 P0-02 回测报告 | 同上 | T+7 周 (依赖 score_model) |
| 重放接口 | 给 P0-02 用于 model 训练 / OOS 验证 | T+5 周 |

### 7.3 接力 SOP

- 我和小蒋每周一同步一次, 持续 6 周
- 任何 contract 字段变更, 必须 RFC + 双方签字 (避免回测口径漂移)
- backtest 结果若与我预期偏差 > 30%, 我先 review feature pipeline, 再 review 信号本身; 不直接调阈值过拟合

---

## 8. 与老李 / 小段 API 实测对接的字段需求

### 8.1 给老李 (S1-002 CLOB 实测) 的字段需求

| 字段 | 用途 | 信号 | 必需性 |
|---|---|---|---|
| `book.bids[0..N].price/size` | mid / spread / depth | 全部 | 必需 |
| `book.asks[0..N].price/size` | 同上 | 全部 | 必需 |
| `book.last_update_ts` | stale detection | 全部 | 必需 |
| `trade.price/size/ts` | F-04, P2-11 size-based 信号 | P1-03, P2-07, P2-11 | 必需 |
| `trade.taker_side` | 大单方向 | P2-11 | 推荐 (若 API 提供) |
| `market.token_id (YES/NO)` | 二元识别 | 全部 | 必需 |
| `market.depth_24h` (聚合) | 流动性滤波 (depth >= 50K 门槛) | P0-01, P0-02 | 必需 |
| `market.tick_size` | 入价计算 (passive 1 tick) | 全部 | 必需 |
| `market.fee_schedule` | 净 edge 计算 (现在 0%, 但要留接口) | 全部 | 必需 |
| order ack latency p50/p90/p99 | 决策延迟 D 估算 | P1-03 前置 | 必需 |

### 8.2 给小段 (S1-003 Goalserve 接入) 的字段需求

| 字段 | 用途 | 信号 | 必需性 |
|---|---|---|---|
| `score.home / score.away` | F-07 | P0-02, P1-03, P1-06 | 必需 |
| `clock.time_remaining` | F-08 | P0-02 | 必需 |
| `clock.period` | F-10 (score_model state) | P0-02 | 必需 |
| `event.type` (goal, foul, red_card, injury, timeout) | F-09 | P1-03, P1-06 | 必需 |
| `event.ts` (Goalserve push 时间) | Δ 测量 (P1-03 核心) | P1-03 | 必需 |
| `lineup.starters/inactives` | P1-04 | P1-04 | 必需 (lineup 数据源选型) |
| `lineup.publish_ts` | P1-04 时延窗口起点 | P1-04 | 必需 |
| `team.elo_rating` (季前) | F-static | P0-02 | 推荐 |

### 8.3 给老彭的咨询请求

| 问题 | 用途 |
|---|---|
| Pinnacle 不同 sport sharp 程度排序 (NBA vs NFL vs MLB vs Soccer vs Tennis) | P0-01 在哪些 sport 优先上 |
| Pinnacle no-vig 在低概率区 (< 0.15) 的偏差是否更大 | P0-01 是否需要按价格区间分桶 |
| sharp money 在 Polymarket 上的 size 阈值 (单笔 > 多少 USDC 算 sharp) | P2-11 触发阈值 |
| narrative 标签 (是否大场 / prime-time / 大牌对决) 历史标注 | P1-05 滤波 |
| 假球高发赛事黑名单 | 全部信号黑名单 (ITF/Challenger / 低级别 soccer) |

---

## 9. 开放问题

### 9.1 待 [实测] 数字 (Sprint-1 内闭环)

| # | 问题 | Owner | 截止 |
|---|---|---|---|
| OQ-1 | Pinnacle API 跨洋链路实测延迟 + 稳定性 | 老李 + 我 | 6/12 |
| OQ-2 | Pinnacle 历史 6 月数据获取方案 (路径 A/B/C 哪个 work) | 老李 + 老彭 | 6/12 |
| OQ-3 | Polymarket 24h depth 在 NBA/NFL 大场的真实分布 | 老李 (S1-002) | 6/12 |
| OQ-4 | Goalserve inplay 事件 push 延迟 (P1-03 前置) | 小段 (S1-003) | 6/12 |
| OQ-5 | 跨洋端到端决策延迟 D 的 p50/p90/p99 | 老姜 + 老吴 | 6/12 |
| OQ-6 | (Polymarket maker 调整时间) - (Goalserve push) Δ 分布 | 我 + 小段 联合 | 6/19 |
| OQ-7 | Lineup webhook 数据源选型 (Rotoworld vs ESPN vs Goalserve 自带) | 小段 + 老彭 | 6/19 |
| OQ-8 | score_model 训练数据完整性 (2 年 NBA + NFL inplay PBP) | 小余 (S1-D) | 6/26 |

### 9.2 待 [咨询] 问题

| # | 问题 | 咨询对象 |
|---|---|---|
| OQ-9 | Pinnacle no-vig 用 multiplicative vs Shin vs power 哪个对 NBA 更准 | 老彭 |
| OQ-10 | inplay 比分 → 胜率回归模型, 业界有 open source 参考实现? (FiveThirtyEight 模型已停更) | 老彭 + 小董 |
| OQ-11 | P0-02 模型用 lookup table 还是 GBM 还是简单 logistic? MVP 选哪个 | 小梁 + 小董 |
| OQ-12 | A/B 测试 50/50 资金切分, 在 MVP $20-50K 总资金下统计功效够吗 | 小董 |

### 9.3 待 [战略] 决策

| # | 问题 | 决策人 |
|---|---|---|
| OQ-13 | P0-02 是否真的进 MVP 首发 (vs 只上 P0-01 单信号 MVP) | 老钱 + 小梁 + 老周 |
| OQ-14 | Pinnacle API 若拉不通是否花钱买 The Odds API ($500/月) | 老钱 + 老雷 |
| OQ-15 | NBA 季后赛 (5-6 月) 是否作为 OOS 强测窗口, 可能造成 MVP 推迟 | 老钱 + 小梁 |

### 9.4 已知未知

- Polymarket 体育历史成交 + book snapshot 数据完整性: 小余 S1-D 仓库到底有多深, 是否够 6 月回测
- Pinnacle closing line 历史数据老彭手里到底完整不完整 (claimed 有, 但还没看到)
- score_model 用 logistic 还是 lookup 还是 tree, 这个决策取决于训练数据丰度, 现在估不准
- inplay 信号实盘冲击 (P0-02 / P1-03) 在 NBA prime time 同时多个 market 一起触发时的 portfolio 协同效应

---

## 附录 A: 信号假设来源映射

| 本目录 ID | 小梁 5.x | 老彭 SX | 合并方式 |
|---|---|---|---|
| SIG-P0-01 | 5.2 | S1 | 直接合并 (同一假设, 都是 Pinnacle 回归) |
| SIG-P0-02 | 5.3 | — | 仅小梁 (比分回归模型) |
| SIG-P1-03 | 5.1 | S4 | 合并 (Goalserve 时延) |
| SIG-P1-04 | — | S2 | 仅老彭 (lineup 公告时延) |
| SIG-P1-05 | 5.7 | S5 | 合并 (大热门 fade) |
| SIG-P1-06 | 5.4 | — | 仅小梁 |
| SIG-P2-07 | 5.5 | — | 仅小梁 |
| SIG-P2-08 | 5.6 | — | 仅小梁 |
| SIG-P2-09 | — | S3 | 仅老彭 (NFL key number) |
| SIG-P2-10 | — | S6 | 仅老彭 (系列赛 2-0) |
| SIG-P2-11 | 5.8 | — | 仅小梁 |
| SIG-P2-12 | 5.10 | — | 仅小梁 |

未进入本目录的原始假设 (M5 前不做):
- 小梁 5.9 长尾错价: 容量过小, 单笔 < $100
- 小梁 5.11 数据流不一致: 工程信号, 稳定性差
- 小梁 5.12 News catalyst: 需要 news pipeline, M5 后再考虑

## 附录 B: M5 后可能新增的信号方向 (备忘)

- News-driven outright (小梁 5.12): 需要 news NLP pipeline
- Cross-platform arb (老彭 §7.2): PM vs Pinnacle 隐式概率差 > 3% 持续 > 30s 套利, 需 Pinnacle 实盘 + 多账号资金管理
- Weather edge (老彭 §7.4): MLB total + NFL total, 需 weather API
- Cross-book hedge (老彭 §7.5 CLV): PM 闭盘 vs Pinnacle closing line 差额, 作为 long-term 评估指标

---

## 附录 C: 与其他 Sprint-1 文档的依赖关系

```
S1-017 (本报告, 小程)
  ├── 输入依赖 →
  │     ├── S1-007 (小梁): 12 条候选 + 框架
  │     ├── S1-012 (老彭): 6 条假设 + 行业 prior
  │     ├── S1-002 (老李): book / depth / latency 字段
  │     ├── S1-003 (小段): Goalserve 字段
  │     └── S1-D (小余): 历史数据仓库
  │
  ├── 直接产出 →
  │     ├── 小蒋 (回测框架): 信号 contract + 数据 schema
  │     ├── 小董 (data-stats): A/B 测试方案 + feature 仓库需求
  │     ├── 小田 (实时 feature pipeline): F-01 ~ F-17 实时性需求
  │     ├── 小梁: 阈值确认 + Kelly 系数审定
  │     └── 老韩 (风控): 单笔 size / 持仓 / 出场风控参数初值
  │
  └── 等候反馈 →
        ├── 小梁验收本报告
        ├── 老钱 (战略决策 OQ-13/14/15)
        └── 老雷 (容量极限战略)
```

---

**v1 收尾**. 本报告完成 Sprint-1 S1-017 主体. v2 在 T+8 周发布, 灌入 OQ-1 ~ OQ-8 的实测数据 + P0-01 的 IS 回测初步结果.

— 小程, 2026-05-28
