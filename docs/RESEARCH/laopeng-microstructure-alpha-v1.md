---
owner: 老彭 (betting-industry-expert, C 单元 IC)
last_review: 2026-05-29
status: Draft — 待小梁 review
wave: 93 (W10 W1, 小梁派单)
scope: inplay 微观结构 α 因子定义 + net_edge_bps 口径 + MetricsSnapshot 接法
adr_cite: ADR-008 §5 (inplay de-vig 例外); ADR-027 N/A (纯 doc); ADR-038 (MetricsSnapshot)
report_to: 小梁 (C 主管)
---

# Inplay 微观结构 Alpha — 因子定义 + net_edge 口径 v1

**老彭, 2026-05-29 (Wave 93)**

---

## §0 写作目的

小梁派单: 把停在 spec 层面的 inplay 微观结构 α 推进到**可回测因子定义**, 并给出
`MetricsSnapshot.net_edge_bps` 的精确计算口径, 与小程 PnL 归因统一成本模型.

本文件覆盖:

1. 三个可回测 inplay 微观结构 α 因子的完整定义 (特征 + 信号 + 预期 decay)
2. `net_edge_bps` 计算公式 — pregame / inplay **分桶分离** (G-06 合规)
3. 成本口径与小程 PnL 归因统一: 同一成本模型, 不出两套数
4. 如何接 `MetricsSnapshot.net_edge_bps` + 看板量化区

---

## §1 背景: 现有信号的 alpha 状态

### §1.1 P0-02 净 edge 现状

```
P0-02 (inplay score-price-mismatch, 小程 v0.2):

gross edge = bet365 de-vig fair_value - PM microprice
           = post-bias, pre-fee 1.5-2.5%    (C2 >= 6¢ 过滤后 ~4%)

成本明细:
  taker fee    = 3.0% (Polymarket sports_fees_v2, 小袁 Q3 实测)
  slippage     = 0.3% (NBA/Soccer gameday $500-1K 单笔, 小袁 §3.3)
  spread       = 0.5% (等效 vig 中位 1¢ / 0.5 价位 ≈ 2%, 半进半吃 1¢)
  gas          = 0    (链上 Polygon, USDC 无 gas; Polymarket CTF = ERC-1155 链上结算, 无 per-tx gas 摩擦)

net_edge (中位) = 1.5-2.5% - 3.0% - 0.3% - 0.5% = -2.3% ~ -1.3%  (未过 C2 门槛)
C2 >= 6¢ 子集:  gross edge > 4%
net_edge (C2>=6¢) = 4% - 3.0% - 0.3% - 0.5% = +0.2%   (正期望, 但薄)
```

**核心问题**: 以 bet365 单家为锚, vig 4.5-6% (soccer), de-vig 精度损失 0.7-1.2pp.
锚不够 sharp → 因子噪声大 → 必须补充更锐的微观结构因子提高信号纯度.

### §1.2 本文件的三个新因子

| 因子 ID | 名称 | 类型 | 适用桶 | 预期贡献 |
|---|---|---|---|---|
| **F-MS-01** | Cross-source devig spread | 跨源价差 | inplay | 主锚; 替代单 bet365 偏差 |
| **F-MS-02** | Orderbook imbalance momentum | 微观结构 | inplay | 执行时机过滤 |
| **F-MS-03** | Lineup-news lag (分桶) | 公告时延 | pregame | 独立 pregame 桶 |

---

## §2 因子一: Cross-source Devig Implied Price Spread (F-MS-01)

### §2.1 老彭视角

这是博彩行业最标准的 edge 识别方法. 当两个来源 (Goalserve inplay bet365 vs Pinnacle pregame consensus)
对同一盘口给出不同隐含概率时, 差值即为 cross-source spread. 这个差值:
- 若持续 > 一定阈值 → PM 对某个来源定价错误
- 若短暂尖峰 → inplay 事件还没被 PM 消化 (= 时差 alpha, P0-02 本质)
- 若稳定单向 → square-book bias (bet365 书偏, 要过滤)

**本因子的目标**: 用更可靠的跨源偏离替代纯 bet365 单源 de-vig, 提高 fair_value 精度.

### §2.2 特征定义

```
F-MS-01: cross_devig_spread_bps

输入:
  p_gs_inplay   = Goalserve inplay bet365 fair_value (单 bet365 multiplicative de-vig, ADR-008 §5)
  p_pinn_pregame = Pinnacle pregame no-vig fair_value (8-家均值 de-vig, ADR-008 主路径)
                 (注: pregame 数字从 Goalserve getodds 拉, 不另接 Pinnacle API)
  p_pm          = Polymarket microprice (小袁 §2.3, cap |micro-mid| <= 2 tick)

偏离计算:
  dev_gs   = p_gs_inplay  - p_pm       (Goalserve inplay 锚 vs PM)
  dev_pinn = p_pinn_pregame - p_pm     (Pinnacle pregame 锚 vs PM)
  consensus_dev = 0.4 * dev_gs + 0.6 * dev_pinn
              // 权重: inplay bet365 噪声更大 (0.7-1.2pp bias), Pinnacle 更 sharp 给高权
              // 权重由老彭行业 prior 设定, 待小蒋回测校准

cross_devig_spread_bps = consensus_dev * 10000  (转 bps)
```

**时效约束**:
- `p_gs_inplay`: suspend == "0" 且 ingestion_ts 距 event_ts <= 10s (ADR-008 §5 stale guard)
- `p_pinn_pregame`: pregame 数字在 inplay 开始后最多用 kickoff 前 5 min 的快照, 超时降权到 0.3
- 两者都 stale 则 F-MS-01 = NaN, 信号不触发

### §2.3 信号构造

```
SIG-F-MS-01: cross_devig_alpha

触发条件 (AND):
  A. |cross_devig_spread_bps| >= 400 bps (4¢, 对应 fv ≈ 0.55 赔率)
     方向性收紧: home/over 方向 >= 600 bps (6¢); away/draw/under >= 400 bps (4¢)
  B. consensus_dev 方向与 dev_gs 方向一致 (避免两源矛盾时下注)
  C. |dev_gs - dev_pinn| <= 800 bps (8¢) (两源不能太分歧; 分歧 > 8¢ = 一源 stale 或异常)
  D. C3/C4/C5 同 P0-02 (流动性 >= $2K, fill_rate >= 0.50, inplay_active)

方向: sign(consensus_dev) — 朝 consensus fair_value 买
size: 同 P0-02 kelly_fractional (1/5 Kelly), 用 consensus_dev 替代 dev_gs 计算 f*

优势 vs 纯 P0-02:
  - Pinnacle pregame 锚更 sharp (无 square-book bias), 权重 0.6 稀释 bet365 偏差
  - 条件 C 过滤 "两源矛盾" 场景, 减少数据质量触发的假阳性
```

### §2.4 预期 Decay

```
decay 结构: 双层

Layer 1 (信息时差 decay, 与 P0-02 一致):
  alpha_t = alpha_0 * exp(-t / tau_info),  tau_info = 25s
  来源: Goalserve bet365 inplay 领先 PM 调价机器人 5-15s; 事件发生后 PM 快速调价

Layer 2 (跨源收敛 decay):
  consensus_dev 趋势: 开赛后 Pinnacle pregame 锚与 inplay 逐渐分离
  实际影响: pregame Pinnacle 锚在 inplay 开始后 15-20 min 价值衰减 (比赛进行中双方分离)
  处理: 对 inplay time > 20 min 的场景, p_pinn_pregame 权重逐渐降至 0.3 (线性插值)

tau_info = 25s (同 P0-02, 主导 decay)
tau_consensus = ~1200s (20 min, 跨源失去一致性)

预期 alpha 窗口: T+0 到 T+30s (同 P0-02); 跨源一致性检验延长有效信号比例 ~15%
```

---

## §3 因子二: Orderbook Imbalance Momentum (F-MS-02)

### §3.1 老彭视角

在 sportsbook 行业, 这等价于 "steam" 监测: 当大量同方向资金快速流入 (order imbalance 急剧变化),
说明 informed money 在行动. Polymarket 的 maker-driven liquidity 结构 (小袁 §4.1: 报价 vs 成交 = 1893:1)
使得 imbalance 信号比传统股票 CLOB 更清晰 — maker 是 bot, 不会乱打方向; imbalance 变化 = taker 方向性.

**F-MS-02 的设计目标**: 不是生成独立买入信号, 而是作为 P0-02/F-MS-01 的执行质量过滤器.
当 imbalance 方向与信号方向一致 → sizing 加强 x1.2; 反向 → sizing 降权 x0.7.

### §3.2 特征定义

```
F-MS-02: imbalance_momentum

基础 imbalance (小袁 §5.1):
  imb_t = (bid_qty_t - ask_qty_t) / (bid_qty_t + ask_qty_t),  imb ∈ [-1, 1]

Momentum 构造 (双轨 EMA):
  imb_ema_fast_t = EMA(imb_t, span=5s)    (快轨: 秒级 order flow)
  imb_ema_slow_t = EMA(imb_t, span=30s)   (慢轨: 分钟级 flow 趋势)

momentum = imb_ema_fast_t - imb_ema_slow_t
         > 0: 近期 bid 比历史均值厚 → 买方流入 (多头动量)
         < 0: 近期 ask 比历史均值厚 → 卖方流入 (空头动量)

数据来源: Polymarket WSS price_change 事件 (小袁 §3.5 实测: 热门 token inter-arrival p50=0ms)
注意: 小袁实测 WSS 总流量 31 msg/s, 订阅 token 实际 < 0.1 msg/s (临场前)
      临场开赛后热门 token lambda ~3.28/s → EMA 可靠
```

### §3.3 信号构造

```
SIG-F-MS-02: imbalance_momentum_filter

本因子不独立触发, 作为 P0-02 和 F-MS-01 的执行质量调制器:

sizing_multiplier = f(momentum, direction):
  if sign(momentum) == sign(signal_direction) AND |momentum| >= 0.3:
    multiplier = 1.2   (顺流: 同向大单在涌入, 信号更可靠)
  elif sign(momentum) != sign(signal_direction) AND |momentum| >= 0.5:
    multiplier = 0.7   (逆流: 市场在反向 → sizing 降权)
  else:
    multiplier = 1.0   (中性)

size_final = size_kelly * multiplier  (经 RM cap 后)

独立信号条件 (非过滤, 纯动量 alpha, M5+ 考虑):
  |momentum| >= 0.4 AND lambda_t > 1/s (热门 token 确认)
  → 方向跟随 momentum 方向, 小仓位 ($100-300), 90s time_stop
  → M5 前不做独立信号, 因为缺少 inplay 比赛上下文 (得分/时间) 作为 conditioning
```

### §3.4 预期 Decay

```
F-MS-02 本质是 "当前时刻" 执行时机信号, 无 alpha decay 概念 (每 tick 重算)

动量信号有效期:
  fast EMA (5s): 捕捉 30s 内 order flow; 超过 30s 前次 imbalance 失效
  slow EMA (30s): 趋势维持 2-3 min; inplay 事件后通常 60s 内逆转

作为 P0-02 multiplier 的价值:
  小袁实测: |imb| > 0.5 的样本比例 NBA 66%, Tennis 90%
  这意味着 2/3 的 NBA 场景有强方向 imbalance, F-MS-02 能有效区分"跟流"还是"逆流"

预期 alpha 贡献: 不提高 hit rate, 而是减少 slippage (顺流时 maker quote 更稳, 逆流时及时降档)
量化估计: sizing 调制后 slippage 节省 ~0.1-0.2% (slippage 从 0.3% 降到 0.1-0.2% 顺流场景)
```

---

## §4 因子三: Lineup-news Lag (F-MS-03, pregame 桶)

### §4.1 老彭视角

P1-04 spec (laopeng-w10-w1-p1-04-lineup-news-lag-spec-v1.md) 已有完整的信号定义.
本节做的是:
1. 将 P1-04 映射到**可回测因子定义** (特征提取格式)
2. 明确 **pregame 桶** 分离 (G-06 合规: pregame vs inplay 成本桶不混用)
3. 给出与 F-MS-01/02 的区别和互补关系

### §4.2 特征定义

```
F-MS-03: lineup_news_lag

输入特征:
  lineup_event_ts_ns   = Goalserve lineup update event_ts (R-20 四时间戳契约)
  pm_mid_pre_news      = PM microprice 在 lineup_event_ts 前 30s 均值 (基线)
  pm_mid_post_t        = PM microprice 在 lineup_event_ts + t 时刻
  sharp_line_move_ts   = Goalserve bm 字段赔率变化时间戳 (Pinnacle/DraftKings 代理)
  star_tier            = {S+, S, A} (老彭静态配置, NBA Finals 10 球员覆盖)

lag 测量:
  lag_ms = pm_mid_post_t_threshold - sharp_line_move_ts
           其中 t_threshold = 首次 |pm_mid_post_t - pm_mid_pre_news| >= 3¢ 的时刻

delta_p 估算 (简化版, MVP 查表):
  S+ player (RAPM > +6): delta_p = -8%
  S  player (RAPM +3~6): delta_p = -4.5%
  A  player (RAPM +1~3): delta_p = -2%
  方向: star on home team → p_home += delta_p; star on away team → p_home -= delta_p

edge_raw = |p_post_news_estimated - pm_mid_current|
```

### §4.3 信号构造

```
SIG-F-MS-03: lineup_news_lag_alpha (pregame 桶)

触发条件 (AND):
  A. Goalserve lineup update: player.status 变化 → "inactive" / "dnp"
     player.status 枚举: 待小段 W10 W2 audit 确认 (P1-04 spec §4.1)
  B. star_tier IN {S+, S} (A 级暂不触发; delta_p 信噪比不足)
  C. lag_elapsed < 90s (信号窗口)
  D. lineup_confidence == HIGH (Goalserve 官方确认; 非预测)
  E. edge_raw >= 3¢ AND net_edge >= 2¢

桶: PREGAME (G-06 分离 — 不进 inplay 桶)

入场:
  order_type = TAKER (时效敏感, 不挂 maker)
  size = clamp(kelly_fractional(p_post_news, q_pm) * 0.25 * bankroll, 200, 1000)
  decay_gate: 下单必须在 T+tau (30s, Finals 高流动) 内完成
```

### §4.4 预期 Decay

```
decay 函数:
  edge_t = edge_0 * exp(-t / tau)
  tau = 30s (NBA Finals 高流动)
  tau = 60s (NBA 常规赛低流动)

decay 预期 (基于老彭行业估算, 待小蒋 W10 W3 backtest 验证):

| t | edge 残余 | 动作 |
|---|---|---|
| T+0s  | 100% | Goalserve lineup update 收到 |
| T+15s | 61% | 系统完成定价 + 下单 (15s budget) |
| T+30s | 37% | tau 点; 剩余 edge 显著收窄 |
| T+60s | 14% | edge < fee; 不值得追单 |
| T+90s | 5%  | 信号窗口关闭 |

**特别注意**: 不同于 F-MS-01/02 的 inplay 信号 (tau=25s),
F-MS-03 的 tau=30s 且属于 pregame 桶:
- 比赛未开始 → Polymarket 流动性通常不如 inplay 深
- PM game day 深度 (小袁 §1.3): NBA gameday ±2tick 中位 $15,125 — 实际充足
- 但比赛前 60-90 min (lineup 公告窗口) 流动可能略差 → tau 保守取 30s
```

---

## §5 net_edge_bps 计算口径

### §5.1 定义

```
net_edge_bps = (gross_edge - cost_total) * 10000

gross_edge = |fair_value - pm_price|    (信号方向的绝对偏离, 小数形式)

cost_total = fee_bps/10000 + gas_bps/10000 + slippage_bps/10000 + spread_bps/10000
```

### §5.2 各成本项精确口径

**Fee (Polymarket taker fee)**

```
fee_per_unit = kSportsTakerFeeRate * p * (1 - p)
             = 0.03 * p * (1 - p)
             (来源: 小袁 Q3 实测, sports_fees_v2, taker only)

近似: near-even 盘 (p≈0.5): fee ≈ 0.03 * 0.25 = 0.0075 = 75 bps
      非 near-even (p≈0.7): fee ≈ 0.03 * 0.21 = 0.0063 = 63 bps

注: 小梁 kelly sizing spec v1 §1.2 采用此公式, 老彭确认与 RM check_signal_ 同口径
```

**Gas**

```
gas_bps = 0

Polymarket CTF Exchange 在 Polygon 链上, USDC 转账无 per-tx gas 摩擦.
老叶 Polygon RPC 文档 + 小沈密钥管理设计均确认: 链上结算成本已内置于 Polymarket fee 结构.
gas = 0 是正确的, 不是漏项.
```

**Slippage (按市场类型分档)**

```
slippage_bps:
  GAMEDAY (NBA/MLB/Tennis 真比赛):
    $500 单: 0 bps (中位), 11 bps (p90)    (小袁 §3.3)
    $1000 单: 0-8 bps (中位), 15 bps (p90)
    MVP 保守估算: 30 bps ($500-1K 单笔, 含 adverse selection)

  INPLAY_HOT (临场中, event 发生后 30s 内):
    quote T_half = 0.21s (小袁 §3.5, 热门 token)
    adverse selection score 升至 0.6 → slippage 估计上调
    MVP 保守估算: 50 bps

  PREGAME (lineup news lag 专用):
    gameday 前 60-90 min, 流动接近 GAMEDAY
    MVP 保守估算: 30 bps

  OUTRIGHT (冠军/MVP 盘):
    $500 单: 320 bps (中位, 小袁 §3.2)
    不进 net_edge_bps 计算 (outright 单独桶, MVP 不交易)
```

**Spread (等效 vig)**

```
spread_bps:
  GAMEDAY 主流盘口 (NBA/MLB/Tennis): 10 bps (中位 1¢ / p≈0.5 赔率 ≈ 100 bps 双边,
                                     我们是 taker 单边 ≈ 50 bps tick, 取一半进成本)
  实测: cross-side vig 中位 1¢ = 100 bps 等效双边 vig (小袁 §1.4)
        taker 单边: 约 50 bps 机会成本 (buying the spread)
  MVP 口径: spread_bps = 50 bps (GAMEDAY/INPLAY_HOT 统一)
            Soccer outright: 880 bps (中位, 不进 MVP 计算)

注: maker 路径 spread = 0 (我们提供流动性, 无 spread 成本) + 0.75% rebate
    MVP 全部走 taker, 因此 spread != 0
```

### §5.3 汇总公式

```
net_edge_bps (GAMEDAY taker, near-even):
  = gross_edge_bps - fee_bps - gas_bps - slippage_bps - spread_bps
  = gross_edge_bps - 75 - 0 - 30 - 50
  = gross_edge_bps - 155 bps

net_edge_bps (INPLAY_HOT taker, event-driven):
  = gross_edge_bps - 75 - 0 - 50 - 50
  = gross_edge_bps - 175 bps

net_edge_bps (PREGAME lineup-news, taker):
  = gross_edge_bps - 75 - 0 - 30 - 50
  = gross_edge_bps - 155 bps

breakeven gross_edge:
  GAMEDAY/PREGAME: gross > 155 bps (1.55%) 才是正期望
  INPLAY_HOT:      gross > 175 bps (1.75%) 才是正期望
```

### §5.4 pregame vs inplay 分桶 (G-06 合规)

G-06 反操纵合规要求: pregame 与 inplay 成本桶分离, 不混算 net_edge.

```
桶定义:
  BUCKET_PREGAME   = game.kickoff_in_minutes > 0          (赛前, F-MS-03 专属)
  BUCKET_INPLAY    = goalserve.status IN {In Play, 1H, 2H}  (赛中, F-MS-01/02 专属)
  BUCKET_HALFTIME  = goalserve.status == "Half Time"        (中场, 不下单)

MetricsSnapshot.net_edge_bps 计算时按桶分离:
  - pregame_net_edge_bps: F-MS-03 信号实现 edge 的 EMA (30 次触发滚动)
  - inplay_net_edge_bps: F-MS-01/02 + P0-02 信号实现 edge 的 EMA

看板 /metrics 暴露: 当前 net_edge_bps 字段 = 两桶加权均值 (inplay 权重 0.6, pregame 0.4)
  原因: inplay 高频触发, 更新速度快; pregame 低频 (每 event 1-2 次), 权重低

审计字段 (Prometheus 低基数 label 扩展, 建议小卢在 endpoint_metrics.cpp 增加):
  stcpp_net_edge_bps{mode="paper|live", bucket="inplay"}   # inplay 桶
  stcpp_net_edge_bps{mode="paper|live", bucket="pregame"}  # pregame 桶
  (label 基数: mode x bucket = 2 x 2 = 4, 满足 ADR-038 §3 低基数原则)
```

---

## §6 成本口径与小程 PnL 归因统一

### §6.1 统一原则

**同一成本模型, 不出两套数. 老彭负责博彩口径, 小程负责 PnL 归因落地, 两者必须使用相同的成本常量.**

### §6.2 小程 PnL 归因瀑布 (state_provider.hpp PnlAttribution)

```
PnlAttribution 字段映射到本文件成本口径:

  gross        = sum(gross_edge_per_trade * size)           -- 扣费前总收益
  fee          = sum(fee_per_unit_per_trade * size)         -- kSportsTakerFeeRate * p * (1-p) * size
  gas          = 0                                          -- Polygon CTF, 无 per-tx gas
  slippage     = sum(slippage_bps/10000 * size)             -- 按 market_class 分档 (§5.2)
  spread       = sum(spread_bps/10000 * size)               -- taker spread 50 bps * size
  net          = gross - fee - gas - slippage - spread      -- 净 PnL

一致性检验 (老彭视角):
  net_edge_bps = (net / total_notional) * 10000
  应等于 MetricsSnapshot.net_edge_bps (在同一时间窗口内)
```

### §6.3 常量对齐表 (两边必须用同一个值)

| 成本项 | 小梁 kelly sizing spec §1.2 | 老彭本文件 §5.2 | RM check_signal_ | 一致? |
|---|---|---|---|---|
| taker fee | `kSportsTakerFeeRate = 0.03` | 0.03 | `kSportsTakerFeeRate` | 是 |
| fee 公式 | `0.03 * p * (1-p)` | 同左 | 同左 | 是 |
| gas | 未列 | 0 | 未检测 | 对齐 (gas=0) |
| slippage GAMEDAY | `slippage_bps` (FillRateModel) | 30 bps MVP | FillRateModel 输出 | 同源 |
| slippage INPLAY_HOT | 未单独列 | 50 bps MVP | 同 | 需对齐 (见 §6.4) |
| spread (taker) | 未列 | 50 bps | 未检测 | **需对齐** |

### §6.4 需要对齐的两个开放项

**开放项 A: INPLAY_HOT slippage = 50 bps**

小梁 kelly spec 没有 INPLAY_HOT 独立档位 (只有通用 FillRateModel 输出).
老彭的 50 bps 是 INPLAY_HOT 事件后 30s 内的保守估算.
建议: 小程在 PnlAttribution 实现时调用 FillRateModel.compute_taker(INPLAY_HOT),
如果 FillRateModel 已实现 adverse_selection_score 衰减逻辑 (小袁 §3.5 Q1 实测),
则直接用 FillRateModel 输出, 不 hardcode 50 bps.

**开放项 B: spread_bps = 50 bps 是否进 PnL 归因**

现有 PnlAttribution.spread 字段存在, 但成本定义没有统一.
老彭定义: spread = taker 买卖价差成本 = best_ask - mid (单边), 约 0.5 tick = 50 bps.
这与 PnlAttribution.spread 语义一致. 建议小程在 backtest_metrics.cpp 里加:
  `trade.spread_cost = (trade.fill_price - trade.pm_mid) * trade.size`
对应 PnlAttribution.spread 字段.

---

## §7 MetricsSnapshot.net_edge_bps 接法

### §7.1 现有字段

```cpp
// src/stcpp/debug_api/state_provider.hpp (现有)
struct MetricsSnapshot {
    // ...
    double net_edge_bps{0.0};   // 当前字段 (已存在, 但无精确计算口径)
    // ...
};
```

### §7.2 精确计算口径 (老彭定义)

```
net_edge_bps (MetricsSnapshot 赋值) = 滚动 N 次触发的 realized net edge 均值

计算方式:
  每次信号触发 + 持仓关闭后, 记录 realized_net_edge_bps:
    realized_net_edge_bps_i = (realized_pnl_i / notional_i - cost_rate_i) * 10000

  滚动均值 (EMA, span = 30 次触发):
    net_edge_bps_t = (1 - alpha) * net_edge_bps_{t-1} + alpha * realized_net_edge_bps_i
    alpha = 2 / (30 + 1) ≈ 0.0645

  分桶暴露:
    inplay_net_edge_bps = EMA of inplay-bucket realized net edges
    pregame_net_edge_bps = EMA of pregame-bucket realized net edges
    net_edge_bps (汇总) = 0.6 * inplay_net_edge_bps + 0.4 * pregame_net_edge_bps

注: realized net edge vs expected net edge
  - 看板展示: realized (实际成交后计算, 最可信)
  - 信号触发时: expected (用于 kelly sizing 决策, §5.3 公式)
  两者共用同一成本口径, 但 realized 用真实 fill_price, expected 用 slippage_bps 估算
```

### §7.3 看板量化区展示方案

```
MetricsSnapshot 对应前端展示 (现有 endpoint_metrics.cpp + 小卢观测 API):

1. stcpp_net_edge_bps{mode, bucket="inplay"}    -- inplay 桶实现 edge
   stcpp_net_edge_bps{mode, bucket="pregame"}   -- pregame 桶实现 edge
   (建议小卢在 endpoint_metrics.cpp 扩展 label, §5.4 已定义)

2. /api/v1/quote/{condition_id} 的 edge_bps 字段 (QuoteParams.edge_bps):
   = expected net_edge_bps (信号触发时的预期值)
   = cross_devig_spread_bps - 155 (GAMEDAY) 或 - 175 (INPLAY_HOT)

3. /api/v1/pnl/attribution (PnlAttribution):
   瀑布图: gross → -fee → -gas → -slippage → -spread → net
   与 net_edge_bps 口径一致 (§6.2)

4. 看板量化区的 α 衰减监控:
   当 stcpp_net_edge_bps{bucket="inplay"} 连续 4 周 < 100 bps → yellow alert
   连续 4 周 < 50 bps → red alert (信号衰减, 暂停 inplay 交易)
   (对应小程 signal-catalog-v1.md §6.1 A/B 测试方案)
```

---

## §8 因子汇总与 edge 预期

### §8.1 三因子 net_edge_bps 预期区间

| 因子 | 桶 | 预期 gross edge (C2 门槛后) | 成本 | 预期 net_edge_bps | 触发频率 |
|---|---|---|---|---|---|
| F-MS-01 (cross-devig) | INPLAY | 400-700 bps | -175 bps | **225-525 bps** | 5-15 次/大场 |
| F-MS-02 (imb momentum) | INPLAY | 调制器 (无独立 edge) | -- | +10-20 bps 节省 | N/A (调制器) |
| F-MS-03 (lineup lag) | PREGAME | 300-500 bps | -155 bps | **145-345 bps** | 5-8 次/Finals 场 |
| P0-02 (C2>=6¢) | INPLAY | 400 bps (≥6¢=4%) | -175 bps | **225 bps** | 5-15 次/大场 |

**注意**: 以上均为老彭行业 prior 估算, 需小蒋 backtest 验证.
F-MS-01 vs P0-02 的关系: F-MS-01 是 P0-02 的"信号质量升级版",
用 Pinnacle pregame 锚加权后精度更高; 两者不同时触发同一市场.

### §8.2 回测验收门槛 (小蒋 W10 W3 backtest 用)

| 度量 | F-MS-01 IS | F-MS-01 OOS | F-MS-03 IS | F-MS-03 OOS |
|---|---|---|---|---|
| Hit rate | ≥ 57% | ≥ 55% | ≥ 62% | ≥ 58% |
| net_edge_bps / trade | ≥ 150 | ≥ 100 | ≥ 100 | ≥ 80 |
| Sharpe (年化) | ≥ 0.8 | ≥ 0.6 | ≥ 1.0 | ≥ 0.8 |
| Max drawdown | ≤ 10% | ≤ 12% | ≤ 8% | ≤ 10% |
| OOS/IS Sharpe ratio | ≥ 0.6 | -- | ≥ 0.6 | -- |

---

## §9 实施依赖与 W10 派单候选

以下为老彭作为 C 单元 IC 提出的候选 ticket, **须经小梁 review + 派单, 不由老彭直接派** (ADR-005).

### §9.1 小程 W10 W2: net_edge_bps 成本常量统一

```
提案人: 老彭
建议 owner: 小程 (C 单元 IC)
内容:
  - 在 backtest_metrics.cpp 或 PnlAttribution 实现中, 统一成本常量到本文件 §5.2
  - 特别: spread_cost 字段 (50 bps) 加入 PnlAttribution.spread 计算
  - INPLAY_HOT slippage: 调用 FillRateModel.compute_taker(INPLAY_HOT) 而非 hardcode
  - 验收: net 字段 = gross - fee - gas - slippage - spread, 浮点误差 < 1e-9
截止: W10 W2 EOD
验收方: 小梁
```

### §9.2 小蒋 W10 W3: F-MS-01/F-MS-03 backtest

```
提案人: 老彭
建议 owner: 小蒋 (C 单元 IC)
内容:
  - 用本文件 §2/§4 信号定义跑 IS/OOS backtest
  - 数据集: 2024 NBA Finals (F-MS-03); 2024-25 Soccer 五大联赛 inplay (F-MS-01)
  - 输出: §8.2 验收表各行数字
  - 成本口径: 严格用本文件 §5.3 公式 (fee + 0 gas + slippage + spread)
截止: W10 W3 EOD
验收方: 小梁
```

### §9.3 小袁 W10 W2: FillRateModel INPLAY_HOT 路径确认

```
提案人: 老彭
建议 owner: 小袁 (C 单元 IC)
内容:
  - 确认 FillRateModel.compute_taker(INPLAY_HOT) 是否已实现 adverse_selection_score 衰减
  - 若已实现: 告知小程接口参数; 若未实现: 提出 W10 W2 实现计划
截止: W10 W2 EOD
验收方: 小梁
```

---

## §10 ADR-027 + R-20 合规声明

本文件为纯博彩业务 spec + 因子定义文档, 无 C++ 代码变更.

**ADR-027 cite: N/A**
本文件不修改 OrderIntent / SignedOrder / Position / MarketInfo / FairValue / OrderBookSnapshot 6 个核心 struct.
后续 F-MS-01 C++ 实施 (小程/小卢) 若涉及 FairValue 或 OrderBookSnapshot 字段扩展, 须单独补 ADR-027 cite.

**R-20 四时间戳契约**:
- F-MS-03 lineup_event_ts_ns: 来自 Goalserve event_ts (数据源自带), 严禁用本地 now() 替代
- F-MS-01 的 p_gs_inplay 时效检查: 用 data_source_ts_ns, 不用本地时间
- F-MS-02 的 EMA 计算基于 WSS message 的 ingestion_ts_ns (客户端接收时间戳)

---

## §11 汇报小梁

Wave 93 inplay 微观结构 α v1 完成. 核心交付:

**1. 三因子完整定义 (可回测)**
- F-MS-01 (cross-devig spread): Goalserve bet365 + Pinnacle pregame 加权锚, 精度优于纯 bet365 单源
- F-MS-02 (imbalance momentum): 执行时机调制器, 节省 10-20 bps slippage
- F-MS-03 (lineup-news lag): pregame 桶独立因子, 基于 P1-04 spec 可回测化

**2. net_edge_bps 精确口径**
```
net_edge_bps = gross_edge_bps - fee_bps(75) - gas_bps(0) - slippage_bps(30/50) - spread_bps(50)
GAMEDAY/PREGAME: breakeven = 155 bps; INPLAY_HOT: breakeven = 175 bps
```

**3. pregame/inplay 分桶分离 (G-06 合规)**
两桶成本参数不同 (slippage 30 vs 50 bps), MetricsSnapshot.net_edge_bps 加权合并 (0.6/0.4)

**4. 小程 PnL 归因统一**
同一成本常量表 (§6.3), spread_cost 字段加入 PnlAttribution.spread 计算 (§6.4 开放项 B)

**5. MetricsSnapshot 接法**
realized net edge EMA (span=30 次触发) + /metrics label 扩展 (bucket="inplay"|"pregame")

**W10 ticket 候选 (待小梁派单)**: 小程 W10 W2 成本统一 + 小蒋 W10 W3 backtest + 小袁 W10 W2 FillRateModel 确认.

— 老彭, 2026-05-29
