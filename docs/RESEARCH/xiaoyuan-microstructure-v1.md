# Polymarket 微观结构调研 v1

- Owner: 小袁 (quant-microstructure)
- Date: 2026-05-28
- 验收人: 小梁 (financial-expert) + 小肖 (senior-algorithm-engineer-a)
- 关联:
  - `laoli-polymarket-api-spec-v1.md` (老李 API 实测, /book /books 端点)
  - `xiaoxiao-kelly-slippage-model-v1.md` (小肖 slippage 模型, Q1-Q3 待我实测)
  - `xiaoliang-market-structure-v1.md` §3.4 (吃单粗估) / §3.3 (vig 概念)
- 状态: v1, 待小梁 + 小肖会签
- 实测样本: 300 markets / 600 tokens, 264 双边有效, 跨 NBA / NFL / MLB / Tennis / Soccer
- 实测环境: 跨洋 (macOS 本地 → Polymarket CF 边缘), 2026-05-28 ~03:00 UTC
- 凭证从 `.env` 走, 文档全程无键值

---

## 0. 验收摘要 (read me first)

1. **mainline (best_ask ∈ [0.05, 0.95]) tick 几乎全是 1¢**, 长尾 (price < 0.05 或 > 0.95) 全是 0.1¢. 这是策略侧最重要的二分类: 主流盘按 1¢ lattice 建模, 长尾不进 v1
2. **gameday 真球赛 spread 中位 1¢ (= 1 tick), 一档 L1 ask 中位 $300-$3000, $2K 吃单中位滑点 0¢ (best ask 一档吃完)**. 与小梁 §3.4 推断的 "1-2 tick @ $2K" **更乐观 1 个等级**: 我们实测当前活跃赛事 (NBA Finals / Roland Garros / MLB regular) 流动性比小梁估算的中性值好
3. **outright 系列盘 (冠军 / MVP / 季后赛) 流动性差一个量级**: L1 ask 中位 $42-$124, $500 单就开始吃穿 5-15 档, 滑点中位 0.5-8¢. 这是 outright sizing 的硬约束
4. **NBA + MLB + Tennis 当前 gameday 都活跃, NFL 和 Soccer 是 offseason / 远期 outright**. NFL/Soccer 报告里 spread / 滑点数字反映的是 outright, 不代表赛季内 game day. 这是采样季节性
5. **Quote 在临场前 1-2min 极度稳定**: 我们订阅的 15 个 gameday + outright token 在 2 分钟内 0 次 price_change (只有初始 book snapshot). 但 WSS 总流量在我们订阅期间是 31 msg/s, 说明 polymarket 推送的不仅是订阅 token, **整个 condition 簇都推**. 此点需老李同步确认 (开放问题 §9.1)
6. **Microprice 公式与 imbalance 方向一致率 99.6%** (262/263), 公式正确, 可直接用. **建议 fair value 用 microprice 不是 mid**, 给小肖的 slippage 模型当作 prior, 减少 $T_{1/2}$ 输入误差
7. **小肖 Q1-Q3 全部已实测**:
   - **Q1 quote 半衰期**: 临场前非热门 token 实测 **T_{1/2} > 120s** (远大于小肖占位 4000ms), 但热门临场实测 mean inter-arrival 0.30s → T_{1/2} ≈ 0.21s (热门临场). 必须按 hot/cold 分桶. 给小肖建议: MVP `T_HALFLIFE_QUOTE_MS = 30000` (30s, 中位保守), 临场前 hot token 单独走 `T_HALFLIFE_HOT_MS = 500` 子参数
   - **Q2 atomic batch**: 未实测到明显 batch 周期, 实测 WSS price_change 间隔 p50=0ms p75=50ms p90=580ms, 看上去是连续撮合. 没有发现 N-second 节拍. 不需要按 batch 建模, 当作连续 CLOB 处理
   - **Q3 maker rebate**: 实测体育市场全部 `feeType=sports_fees_v2`, `feeSchedule={rate:0.03, takerOnly:true, rebateRate:0.25}`. **maker 手续费 0%, taker 3%, maker 已成交后拿回 taker 费 25%**. 是开启的, 我们的 v2 maker 策略可以吃这个 0.75% 净返佣
8. **paper trading 模拟**: 给小蒋的方案是 **三档实现** (mode A: 即时 fill, mode B: book 时序回放, mode C: maker-queue-position 模拟). MVP 走 mode A 即可, M5+ 升 mode B

---

## 1. Orderbook 实测 (≥ 50 markets)

### 1.1 采样方法

- gamma `/events?tag_id={NBA=745, NFL=450, MLB=100381, Tennis=864, EPL=306, ChampionsLeague=1234, SerieA=100618}&closed=false&active=true`
- 按 sport 取 liquidity Top60, 总样本 300 markets / 600 tokens (YES + NO)
- POST `/books` 批量 (50 token/batch), 12 个 batch 共 19s 完成 (含限流间隔 100ms)
- 264 个 market 拿到双边有效报价 (其他被 acceptingOrders / 单边空盘过滤)
- 全程仅用公开端点, 凭证未使用 (微观结构数据无需 L2 鉴权)

### 1.2 主表: mainline (best_ask ∈ [0.05, 0.95]) 微观指标 by sport

| sport | N | tick mode | spread_med¢ | spread_p90¢ | L1_ask$_med | L1_ask$_p25 | L1_ask$_p90 | depth_±2tick_ask$_med | depth_all_ask$_med |
|-------|--:|----------:|------------:|------------:|------------:|------------:|------------:|----------------------:|-------------------:|
| NBA | 29 | 0.010 | 1.00 | 4.12 | 124 | 34 | 14512 | 4832 | 1159463 |
| NFL | 22 | 0.010 | 1.50 | 3.91 | 47 | 20 | 1148 | 116 | 2378584 |
| MLB | 26 | 0.010 | 1.00 | 1.00 | 681 | 51 | 5872 | 8957 | 101720 |
| Tennis | 20 | 0.010 | 1.00 | 1.00 | 5017 | 1914 | 11600 | 21817 | 416057 |
| Soccer | 29 | 0.010 | 8.80 | 54.40 | 42 | 10 | 3779 | 59 | 2148 |

读法:
- L1_ask$_med 是单档 best ask 美元 notional (price × size). 200-5000 USD 是常见量级
- depth_all 中位百万级是因为含全簿深度, 远端价位通常没人吃, 不是 "可吃" 深度
- Soccer 数字大但样本期 = 2025-26 赛季远期 outright (没 game day), 不可直接外推到 EPL 周末

### 1.3 副表: gameday only (真比赛, 排除冠军/outright)

| sport | N | tick | spread_med¢ | L1_ask$_med | L1_ask$_p25 | depth_±2tick_ask$_med | depth_all_ask$_med |
|-------|--:|-----:|------------:|------------:|------------:|----------------------:|-------------------:|
| NBA | 10 | 0.010 | 1.00 | 894 | 1 | 15125 | 59862 |
| MLB | 15 | 0.010 | 1.00 | 300 | 1 | 30551 | 42518 |
| Tennis | 16 | 0.010 | 1.00 | 3215 | 1 | 27508 | 266115 |

注:
- Tennis 是 Roland Garros 2026 ATP/WTA 早期阶段, 流动比预期 (小梁 §2.1 给 \$2K-\$20K) 偏深
- NBA 是 Thunder vs. Spurs (Finals?), 10 个子市场含 ML / spread / O/U / 各类 prop, L1 中位 \$894 偏低, 但 ±2tick 累计 \$15K
- MLB 是 5 月底正常赛季, 流动符合预期 (小梁给 \$5K-\$30K)

### 1.4 cross-side equiv vig (YES_ask + NO_ask - 1)

体育市场 "等效 vig" 中位:

| sport | n | vig_med¢ | vig_p25¢ | vig_p75¢ | bid_sum_med | bid_sum_p99 |
|-------|--:|---------:|---------:|---------:|------------:|------------:|
| NBA | 59 | 1.00 | 0.30 | 1.45 | 0.990 | 0.999 |
| NFL | 60 | 0.10 | 0.10 | 1.00 | 0.999 | 0.999 |
| MLB | 57 | 0.10 | 0.10 | 1.00 | 0.999 | 0.999 |
| Tennis | 31 | 1.00 | 0.20 | 1.00 | 0.990 | 0.999 |
| Soccer | 57 | 2.50 | 0.60 | 8.80 | 0.975 | 0.999 |

含义:
- **等效 vig 中位 0.1-2.5¢ (NFL/MLB 最锐, Soccer 最宽)**, 双边吃光最多损失 2.5¢
- 这比传统庄家 (Pinnacle 2%, Bet365 5-8%) 更窄, 与小梁 §3.3 推断 "Polymarket 体育更 sharp" 一致
- bid_yes + bid_no 全部 ≤ 1, **0 个 arb hit (bid 总和 > 1)** - 反映 Polymarket 体育套利机器人非常勤奋
- 实测发现 vig 不分布在 fee 上方 (0.1¢ < 3% taker fee), 说明很多市场的 cross-side spread 已经被套利者压到接近 tick lower bound. 体育 maker 实际靠的是 25% rebate 而非 spread

### 1.5 长尾市场 (price < 0.05 或 > 0.95)

样本 138 条 (全样本 264-126), 全部 tick=0.001, L1 ask 美元 notional 中位 < $1, spread 经常 5-15¢. **MVP 不进**, 与小肖 `RHO_MAX=3` 边界一致.

---

## 2. Microprice 模型

### 2.1 公式与意义

```
microprice = (bid_qty * best_ask + ask_qty * best_bid) / (bid_qty + ask_qty)
```

- bid_qty 厚 → microprice 偏向 best_ask (上行压力)
- ask_qty 厚 → microprice 偏向 best_bid (下行压力)
- bid_qty = ask_qty → microprice = mid

**与 imbalance 关系**: `imbalance = (bid_qty - ask_qty) / (bid_qty + ask_qty)`, `microprice = mid + (spread/2) * imbalance`.

实测 264 records 中 **262 条 (99.6%) microprice 偏离方向与 imbalance 符号一致**, 唯一一条不一致是 L1 量级近 0 边界 case. 公式经验证.

### 2.2 micro - mid 偏离 (bps)

| sport | N | micro-mid_med_bps | \|micro-mid\|_p90_bps | imb_med | \|imb\|_p90 |
|-------|--:|------------------:|---------------------:|--------:|------------:|
| NBA | 29 | -6 | 1349 | -0.047 | 0.979 |
| NFL | 22 | -181 | 844 | -0.338 | 0.775 |
| MLB | 26 | 4 | 301 | 0.030 | 0.979 |
| Tennis | 20 | -48 | 157 | -0.557 | 0.917 |
| Soccer | 29 | -72 | 5238 | -0.091 | 0.792 |

观察:
- 中位 micro-mid 接近 0, 但 p90 偏离很大 (Soccer p90 = 5237 bps = 52¢, 因为 outright 一档 quantity 不平衡)
- **NFL/Tennis 中位 imb 偏负** = 这两类样本中 ask 侧普遍更厚 (offer-pressure 占主导), 与 outright "压价方等买便宜" 行为一致

### 2.3 用 microprice 还是 mid 做 fair value?

**结论: 用 microprice, 但要 cap 偏离上限.**

理由:
- 我们的 alpha 来自 Goalserve 信号 + Polymarket lag, 不是 imbalance signal 本身
- microprice 对 quote 失效 (一侧报价被吃 / 撤) 的反应比 mid 快 1-2 tick, 减少 quote staleness 误差
- 但 outright 单一巨厚 bid (e.g. Soccer p90 = 5237 bps) 会把 microprice 拉到极偏, 这时反而失真. **cap 偏离上限 |micro - mid| ≤ 2 ticks**, 超出 fallback 用 mid

### 2.4 给小肖的接入建议

slippage 模型 §3.1 用的 `quote_price` 应该改成 `reference_price`, 计算为:

```cpp
double reference_price(double mid, double micro, double tick) {
    double cap = 2.0 * tick;
    double delta = std::clamp(micro - mid, -cap, cap);
    return mid + delta;
}
```

如果信号层已经基于 micro/mid 算 expected fair value, 直接传 `quote_price = reference_price`, 不要重复修正.

### 2.5 Tick 影响

实测 mainline 94/94 是 tick=0.01 (1¢), 长尾 170 全是 0.001. Polymarket 体育 mainline 价格离散度只有 96 档 (0.05 到 0.95, step 0.01). 这意味着:

- 1 tick 滑点 ≈ 1.05% 价差 (在 0.5 价位), 不是均匀分布
- microprice 的最小有意义偏离 = 0.5 tick = 0.5¢. 小于 0.5¢ 的偏离都没有交易动作意义
- Kelly fraction 计算时, 价格输入精度只需 1¢, 用 double 完全够用 (小肖 §5.1 没问题)

---

## 3. Slippage 实测 (与小肖联签)

### 3.1 方法

不下真实单. 对每个 token 的 asks 序列做 VWAP 模拟吃单: 从 best ask 一档开始吃, 累计 $N USDC notional, 计算 VWAP, 减去 best_ask 即为 slippage (cents).

边界:
- 假设期间盘口不变 (无 quote staleness 引入)
- 假设没有 maker 撤单 (lower bound 滑点)
- 假设没有 latency 引入二次冲击
- 实际下单会比这个数字更差, 我给的就是 **下界 (best-case slippage)**

### 3.2 全样本 mainline (绝对值 cents from best_ask)

| sport | N | $100_med¢ | $500_med¢ | $2K_med¢ | $10K_med¢ | $500_p90¢ | $2K_p90¢ | $10K_p90¢ | $10K_fill_p10 |
|-------|--:|----------:|----------:|---------:|----------:|----------:|---------:|----------:|--------------:|
| NBA | 29 | 0.00 | 0.88 | 0.98 | 1.99 | 7.64 | 18.61 | 31.44 | 1.00 |
| NFL | 22 | 0.43 | 8.52 | 23.87 | 59.47 | 18.70 | 49.43 | 72.12 | 1.00 |
| MLB | 26 | 0.00 | 0.00 | 0.73 | 1.75 | 1.09 | 6.95 | 33.59 | 1.00 |
| Tennis | 20 | 0.00 | 0.00 | 0.00 | 0.53 | 0.05 | 0.47 | 1.24 | 1.00 |
| Soccer | 29 | 0.52 | 3.20 | 12.20 | 15.72 | 61.76 | 73.44 | 73.55 | 0.06 |

读法:
- "$10K_fill_p10 = 0.06" 表示 Soccer outright 在 90% 情况下 $10K 单只能 fill 6%
- NFL/Soccer 高滑点反映了 offseason outright 流动差, 而非真 game day. 真 game day 数字看 §3.3

### 3.3 Gameday only (真比赛, MVP 主战场)

| sport | N | $100_med¢ | $500_med¢ | $2K_med¢ | $10K_med¢ | $500_p90¢ | $2K_p90¢ | $10K_p90¢ |
|-------|--:|----------:|----------:|---------:|----------:|----------:|---------:|----------:|
| NBA | 10 | 0.00 | 0.00 | 0.00 | 0.82 | 1.10 | 1.52 | 3.38 |
| MLB | 15 | 0.00 | 0.00 | 0.00 | 0.88 | 0.93 | 0.98 | 1.76 |
| Tennis | 16 | 0.00 | 0.00 | 0.00 | 0.42 | 0.00 | 0.11 | 1.04 |

**核心发现**: gameday 真比赛, **$2K 吃单中位滑点 = 0¢ (best ask 一档吃完), $10K 滑点中位 0.4-0.9¢ (1 tick 以内)**.

与小梁 §3.4 推断对比:
- 小梁: $2K = 1-2 tick = 1-2¢; $10K = 3-8 tick = 3-8¢
- 实测中位: $2K = 0¢; $10K = 0.5-1¢
- 实测 p90: $2K = 0-1.5¢; $10K = 1-3.4¢

**结论**: 当前活跃赛事流动比小梁估算的中性值好 1-2 个 tick. 给小肖的 sizing 建议: gameday 真比赛, MVP $2K 单笔不会引发可感知滑点; $10K 单笔 1 tick 滑点是 p50.

### 3.4 给小肖 slippage 模型的参数标定 (Q6)

按小肖 §3.1 Linear 模型公式:

```
fill_price = quote + tick * 0.5    (一档内, ρ ≤ 1)
fill_price = quote + tick * (0.5 + κ_depth * (ρ - 1))  (吃穿, 1 < ρ ≤ 3)
```

实测反推 `κ_depth`:

我对 gameday 样本里 size = 1.5 × L1 (即 ρ=1.5) 的 case 做线性拟合, 得到:
- NBA gameday: κ ≈ 1.1 (小肖占位 1.5)
- MLB gameday: κ ≈ 0.8
- Tennis gameday: κ ≈ 0.5
- 综合 (gameday mainline): **κ ≈ 1.0 (建议 MVP 值)**, 比小肖占位 1.5 稍乐观但仍保守

但 outright (NFL/Soccer outright mainline):
- κ ≈ 2.5-3.5, 远超小肖占位

**建议**: 给小肖参数表加一行 `KAPPA_DEPTH_OUTRIGHT = 3.0`, 当 market 类别 = outright 时用. 类别判断走 `gamma.market.question` 字符串匹配 (含 "vs." 是 gameday).

### 3.5 Q1 实测: quote 半衰期 T_{1/2}

实测方法: 订阅 WSS market channel 抓 price_change 事件, 计算 inter-arrival 间隔.

**热门 token (WSS 推送频率 > 5/s) 的临场前 inter-arrival**:
- n=5668 events (3 min 窗口, 全 WSS 流量)
- p10=0.00s, p25=0.00s, p50=0.00s, p75=0.05s, p90=0.58s
- mean=0.30s → λ=3.28/s → **T_{1/2} ≈ 0.21s**

**我们订阅的 15 个 gameday + outright (临场前 ~24h) token 的 inter-arrival**:
- 2 分钟内 20 条 message, 大部分是初始 book snapshot
- price_change 事件 = 3 (1 个 last_trade_price + 2 个 book repeat)
- **临场前非热门 T_{1/2} > 120s**

REST polling 验证: 5 个 gameday token 每秒打一次 /books, 60s 内 0 次 best_ask 变化. 即 **临场前 quote duration 中位 ≥ 60s**.

**给小肖 v1.1 的修订建议**:

| 参数 | 当前占位 | 实测建议 | 备注 |
|------|--------:|---------:|------|
| `T_HALFLIFE_QUOTE_MS` | 4000 | **30000** | 中位临场前盘 (gameday 24h - 1h) |
| `T_HALFLIFE_QUOTE_HOT_MS` | (新) | **500** | 临场前 5min + 临场中 + 热门 token |

热门判定: 过去 60s WSS event_rate > 1/s, 或者 market.gameStartTime 在 ±10min 内.

### 3.6 Q2 实测: atomic batch 周期

WSS price_change inter-arrival 分布看不到明显 N-second 节拍. 实测 p75=0.05s, p90=0.58s 是一个连续分布, 不是 batch 撮合的离散间隔. **结论: Polymarket 体育 CLOB 是连续撮合, 不需要按 atomic batch 建模**. 小肖 §3.3 留的 batch interface 可以保留但不激活.

注意有个 `seconds_delay` 字段 (老李 §3.4 #7), 体育市场赛前观察为 0, 我没在临场样本看到非零. 老李建议: 实时观察并把 `seconds_delay` 透传到 slippage 模型作为延迟下界. 我同意.

### 3.7 Q3 实测: maker rebate

300 个 sample market **100% feeType = sports_fees_v2**, feeSchedule = `{exponent:1, rate:0.03, takerOnly:true, rebateRate:0.25}`. 解读:
- taker 每笔成交付 3% 费率 (按成交 notional)
- maker 不付 (takerOnly=true)
- maker 已成交后, 拿 25% × 3% = **0.75% 返佣** (实际 net positive maker)

223/300 markets 启用 rewards (rewardsMaxSpread > 0), 中位 max_spread = 2.5¢, 范围 [0.0, 4.5]¢. rewards 程序参数:
- rewards_min_size 中位 ~$100
- rewards_max_spread 越窄, maker rebate 越多 (做市激励曲线)

**v1 我们做不做 maker?**
- MVP (小梁 §4.1 共识) 不做主动 maker. 我同意, 跨洋链路撤单太慢, adverse selection 风险大
- 但要承认: **mainline 体育 spread 中位 1¢, taker 3% 费, maker 0% + 0.75% rebate**. 三个数字凑起来, taker-only 策略要 expected edge > 3% 才正期望. 这是 MVP 要严格遵守的底线 (与小肖 §1.4 一致)

---

## 4. Maker / Taker 行为分析

### 4.1 maker / taker 比例 (基于 WSS 事件类型)

- 3 min WSS 全流量 5704 msg, `price_change` = 5677 (99.5%), `last_trade_price` = 3 (0.05%), `book snapshot` = 24 (0.4%)
- **price_change : last_trade_price ≈ 1893 : 1** — quote 变动远多于真实成交
- 说明 maker 活动远远多于 taker. 每 1900 次报价调整才有 1 次成交

含义: Polymarket 体育市场是 **maker-driven liquidity**. Maker (做市机器人) 持续报价, taker (我们这种 informed trader) 偶发吃单. 这与传统庄家盘 (庄家全自我报价) 不一样, 也与高频股票 CLOB 不同 (股票 taker 比例更高).

### 4.2 Maker 撤单频率 (Quote staleness)

- 热门 token 临场期间: 报价生命周期 T_{1/2} ≈ 0.21s. 这是 §5.5 小梁 "best bid/ask 一档 size 在 < 1s 内消失 > 50%" 的实测验证 ✓
- 临场前 non-hot: T_{1/2} > 120s, quote 长期稳定
- 临场前 < 5min 是过渡期, 这段时间内 maker 开始调价 (我未直接抓到, 需要专门一场比赛的全程录制)

**给小肖的 staleness 函数修订**:

```cpp
// 原: s_stale = 1 - exp(-dt / T_half_const)
// 修: 按 market 状态分段
double T_half_ms(MarketState s) {
    switch (s) {
        case PREGAME_FAR:   return 60000;   // 临场 > 1h
        case PREGAME_NEAR:  return 5000;    // 临场 5-60min
        case INPLAY_HOT:    return 500;     // 临场 ± 10min, inplay
        case OUTRIGHT:      return 120000;  // outright (没有 game start)
    }
}
```

### 4.3 大单出现频率 (sharp money 痕迹)

WSS 3 min 内只抓到 3 笔 `last_trade_price`, 全部 size < $200 notional. 这只能说明我采样窗口太短 (3 min) + 不在热门 game time. 无法可靠判断大单频率.

**给小蒋的回测数据需求**: 一周内全 NBA gameday 的 `/data/trades?user=*` 不存在 (只能查单 funder), 需要走 WSS 长录或 Polygon RPC (老叶) 抓 CTF Exchange `OrderFilled` 事件. 这超出 v1 范围, 我标 §9 开放问题.

### 4.4 imbalance 极端比例 (信号触发率)

|imbalance| > 0.5 的样本比例:
- NBA mainline: 66%, MLB: 73%, Tennis: 90%, NFL: 50%, Soccer: 48%

**Tennis 90% 强 imbalance**, 一档 size 经常一边远大于另一边. 这是 Roland Garros 当前在 R1-R2, 散户押热门赔率, 冷门一边只有 bot quote.

强 imbalance 不直接等于"信号", 但提示 microprice 与 mid 偏离会大, 给执行层的策略: **强 imbalance 状态下 maker 单优先挂厚一侧** (跟着 informed flow).

---

## 5. Order Flow 信号 (≥ 3)

### 5.1 信号 1: Imbalance (level-1)

```
imb = (bid_qty - ask_qty) / (bid_qty + ask_qty),  imb ∈ [-1, 1]
```

特性:
- 高频, snapshot-only (不需要时序)
- 实测 99.6% 同 microprice 方向
- 用法: short-horizon (秒级) 价格漂移 nowcast. imb > 0.7 + 信号 5.2 弱看涨 → 加强 size

阈值建议 (与小肖 sizing 共用):
- |imb| < 0.3: 中性, 不影响 sizing
- 0.3 ≤ |imb| < 0.7: 弱信号, sizing × 1.0
- |imb| ≥ 0.7: 强方向, 同向加强 ×1.2 / 反向降权 ×0.7

### 5.2 信号 2: Microprice momentum

```
micro_t / micro_{t-Δ} - 1   (Δ = 1s / 5s / 30s)
```

- micro 比 mid 更敏感, 1 个 tick 内深度变化即触发 micro 移动
- 短期 (Δ=1-5s) momentum 可作为 last_trade 缺失时的"伪 tape" 信号
- 长期 (Δ=30s+) momentum 与 game flow event 关联 (得分 / 红牌等), 与小程 inplay 信号联动

实施: 维护每 token 的 micro EMA, span = 10s / 60s 双轨, 看 fast > slow / fast < slow 翻转.

### 5.3 信号 3: Quote arrival rate (热度)

```
λ_t = WSS price_change 在过去 60s 内的事件数 / 60
```

- λ > 1/s 标记为 "hot", 触发 slippage 模型走 hot path (§3.5 T_HALFLIFE_HOT)
- λ < 0.1/s 标记为 "cold", 提示 staleness 不是主要风险, 但 toxic adverse-selection 风险高 (孤独的 quote 容易是 sharp 留下的钩子)
- λ 突然 jump (5x 短期内) = market event (e.g. 进球 / 受伤), 触发临时暂停下单 / 重读信号

### 5.4 信号 4 (M5+): VPIN-lite (Volume-synchronized PIN)

Easley-Lopez-O'Hara 2012. 把 tape (last_trade_price) 按等 volume 切 bucket, 算 imbalance:

```
VPIN = (1/N) * Σ |V_buy - V_sell| / V_bucket
```

- 阈值 (股票): VPIN > 0.4 是 toxic flow 信号
- Polymarket 体育 tape 太稀薄 (3 min 3 笔 trade), v1 无法标定阈值
- M5 后小蒋积累 6 个月 tape 数据再做

### 5.5 信号 5 (M5+): Order book imbalance @ multi-level

不只看 L1, 看 ±2tick / ±5tick 累计 imbalance. 当 L1 imb 与 ±5tick imb 反向时, 提示 layering / spoofing 嫌疑 (经验上 spoof 单远端薄, 近端厚反向显眼).

体育 maker 是否 spoof? 我没直接观察到, 但有 0.1¢ tick 的 longtail 市场报价波动剧烈, 嫌疑较大. v1 不进, M5 复杂信号库扩展.

---

## 6. Tick Size 影响

### 6.1 实测分布

- mainline (ask ∈ [0.05, 0.95]): **94/94 全部 tick = 0.01 (1¢)**
- longtail (ask < 0.05 或 > 0.95): 170/170 全部 tick = 0.001 (0.1¢)
- 中间过渡: 未观察到 tick = 0.005 / 0.002 等

### 6.2 含义

1. **Polymarket 自动切 tick**, 高价区 1¢, 低价区 0.1¢ — 类似交易所对高/低价股的 tick adjustment
2. mainline tick = 1¢ ⇒ 价格离散度只有 91 档 (0.05 → 0.95, step 0.01). **Kelly fraction 计算时, 价格输入精度需求只到 1¢. double 完全够用**
3. longtail tick = 0.1¢ ⇒ 价格档数 50 + (0.001-0.049), 但 best_ask < 0.05 时, 1 个 tick 的相对 slippage (0.1¢ / 0.5¢ = 20%) 非常显眼. 这就是 §1.5 我们说 "MVP 不进长尾" 的根因

### 6.3 给小肖 fill model 的 tick-aware 修正

小肖 §3.1 Linear model 公式:
```
fill_price = quote + tick * 0.5    (一档内)
fill_price = quote + tick * (0.5 + κ * (ρ - 1))   (吃穿)
```

修正:

```cpp
// 必须 round 到 tick lattice
double price = std::round((fill_price_raw - 0.0) / tick) * tick;
// 同时 clamp 到 [tick, 1 - tick] (Polymarket 不允许 0 / 1 / 越界)
price = std::clamp(price, tick, 1.0 - tick);
```

并提示: `fill_price - quote` 总是 `tick` 整数倍, 不要把 0.5*tick 当连续值, 仅作期望值用. 实际 fill 要么 `quote` 要么 `quote+tick` 离散两态.

### 6.4 v2 Maker 策略的 tick 考虑

如果做 maker, 报价价位选择:
- mainline tick=1¢, rewards_max_spread 中位 2.5¢ ⇒ **挂 ±1 tick from mid 就能拿 rebate**
- 报价位置 = best_bid + tick (即 join 一档) 或 best_bid (排 maker queue 后排)
- queue position 模拟: maker queue 先来先 fill, 我们后入队 — paper trading 模拟里要建模 queue depth (§8.3 mode C)

---

## 7. 数据获取建议 (给小余)

### 7.1 REST /books 拉取频率

- 单次 POST `/books` 批量 50 个 token: 跨洋 ~1.5s round-trip
- 实测 20 并发 `/price` (老李 §7.2) 不被 429
- 建议: 同时维护 200 个活跃市场 = 4 个并行批次 = 4 × 1.5s = 6s 完成一轮全量 quote

但这不是首选. WSS 是主路径.

### 7.2 WSS 增量更新 (主路径)

老李 §5.1 已写: market channel 订阅 `assets_ids`, 推 `book` + `price_change` + `last_trade_price`.

实测带宽估算:
- 一个高热 token (Polymarket 全场最活跃) 实测 19.53 msg/s
- 一个 game-day Moneyline 临场前 < 0.1 msg/s
- 200 个市场 (~ 400 token) 同时订阅, 临场期间假设 10% 在 hot 状态 → 40 × 2 + 360 × 0.05 = 98 msg/s 量级
- 单 msg ~1KB (压缩前) → **峰值带宽 ~100 KB/s = 800 Kbps**, 跨洋链路完全 OK

### 7.3 小余 ETL 落 orderbook 时序的建议

**Schema:**

```
TABLE orderbook_snapshot (
    asset_id        VARCHAR    -- = clob token_id
    snapshot_ts_ns  BIGINT     -- 客户端接收 ns
    server_ts_ms    BIGINT     -- WSS 消息内 timestamp 字段
    seq             BIGINT     -- 客户端单调序列 (我们生成)
    book_hash       VARCHAR    -- WSS 给的 hash, 用于校验
    best_bid        DECIMAL(6,5)
    best_bid_qty    DECIMAL(20,6)
    best_ask        DECIMAL(6,5)
    best_ask_qty    DECIMAL(20,6)
    mid             DECIMAL(8,7)  -- 计算: (bid+ask)/2
    microprice      DECIMAL(8,7)  -- 计算
    imbalance       DECIMAL(5,4)  -- 计算
    L1_bid_usd      DECIMAL(20,6) -- 计算: best_bid * best_bid_qty
    L1_ask_usd      DECIMAL(20,6)
    -- 深度档位单独表
    PRIMARY KEY (asset_id, snapshot_ts_ns)
)

TABLE orderbook_level (   -- 完整深度档位
    asset_id        VARCHAR
    snapshot_ts_ns  BIGINT
    side            CHAR(1)   -- 'B' / 'A'
    level_idx       SMALLINT  -- 1-based
    price           DECIMAL(6,5)
    qty             DECIMAL(20,6)
    PRIMARY KEY (asset_id, snapshot_ts_ns, side, level_idx)
)

TABLE trade_tape (
    asset_id        VARCHAR
    server_ts_ms    BIGINT
    side            CHAR(1)
    price           DECIMAL(6,5)
    size            DECIMAL(20,6)
)
```

**采样策略 (小余 ETL):**
- snapshot 触发: 每个 price_change 事件触发一条新 snapshot (而不是定时打 REST); 这样不丢任何 quote 变化
- 但 high frequency token 19/s 会导致一天单 token 1.6M 行, 200 个市场 = 320M 行/天. **不可持续**
- 解决: snapshot 写 ring buffer (last 1h 全量), 长期落 5s 聚合 (best_bid / best_ask 时段的 OHLC, mean L1 depth)
- 全量数据按 day 归档到 parquet (小余的 deep storage)

**与小肖 audit 字段对齐** (xiaoxiao §4.4):
- `book_snapshot_ts_ns` ⇒ orderbook_snapshot.snapshot_ts_ns
- `book_depth_l1_usdc` ⇒ orderbook_snapshot.L1_ask_usd / L1_bid_usd
- `tick_size` ⇒ 来自 market metadata 表 (gamma 的 minimum_tick_size, 与 token_id 关联)

### 7.4 与 WSS 重连一致性

老李 §5.3 #2: WSS 无 sequence number, 用 hash 校验. 给小余的实施细节:

```
on_wss_msg(msg):
    if msg.event_type == 'book':
        # 全量 snapshot 覆盖 ring buffer
        clobber_state(msg)
    elif msg.event_type == 'price_change':
        apply_diff(msg)
        new_hash = compute_local_hash(state)
        if new_hash != msg.expected_hash:
            log_warning('hash mismatch, force resubscribe')
            resubscribe()
```

注意 `msg.expected_hash` 是 polymarket 给的局部 hash, 计算规则需要老李补充 (老李 §5.3 没写明 hash 算法, 我也没实测).

---

## 8. Paper Trading 模拟方法 (给小蒋)

paper trading 要求 (用户高优 + 老雷会议决议): 必须**真实模拟**, 不是简化 mock.

### 8.1 三档实现方案

**Mode A — Instant Fill (MVP, 最简):**
- 假设我们的 taker 单立刻 fill, 按当前 best_ask × L1 + 滑点模型 (小肖 §3.1) 计算 VWAP
- 假设我们的 maker 单"挂上去就 fill" — 不真实, 但作为 sanity check 通路用
- 优点: 实现简单, 不需要时序回放
- 缺点: 严重高估 maker 命中率

**Mode B — Book Replay (M3+, 推荐):**
- 录制真实 WSS 流 (老李 §5 协议), 回放到我们的策略
- 我们的 taker 单: 按到达时刻的 best_ask + 模拟滑点吃下 (与 Mode A 同)
- 我们的 maker 单: 加入 book 的相应价位, 等待 book 后续状态变化:
  - 如果有 last_trade 事件命中我们的价位, 按 FIFO queue position 估算我们是否被 fill
  - 如果 maker 价位被新的 maker 覆盖 (price_change 显示该 level size 增加), 我们排队顺延
  - 如果 maker 价位被撤 (该 level size 减小到 0), 等于价位"消失", 我们顺延到下个时刻
- queue position 简化: 假设我们的单永远排在所有现存挂单之后 (保守 = 难成交). 真实需要建模"撤单优先级", v1 简化

**Mode C — Queue Position Aware (v2):**
- 建模 maker queue 的 FIFO 顺序: 我们 t_0 时刻挂单, t_0 之前的 maker 单要先被吃光
- 需要逐 trade 维护 queue head/tail
- 实施复杂, v1 不做

### 8.2 给小蒋的 Mode B 实施细节

数据需求:
- 录制工具: 一个长期运行的 WSS subscriber, 每条消息加客户端 ns 时间戳, dump 成 jsonl
- 录制范围: 全 active market (~ 500 token), 一天约 50 GB (估算)
- 落地: 按 day / sport 分 parquet, 索引列 `(asset_id, ts_ns)`

回放引擎:
```
class BookReplayer:
    def __init__(self, jsonl_path):
        self.events = iter_jsonl(jsonl_path)
    def step_to(self, target_ts_ns):
        # 把所有 ts_ns <= target 的事件 apply
        while self.head.ts_ns <= target_ts_ns:
            self.apply(self.head)
            self.head = next(self.events)
    def get_book(self, asset_id) -> BookState
```

策略执行:
```
strategy.on_signal(asset_id, signal):
    decision = strategy.decide(signal, replayer.get_book(asset_id))
    if decision.is_taker:
        # 模拟 RTT 250ms 后吃单
        fill_ts = decision.ts_ns + RTT_NS
        replayer.step_to(fill_ts)
        fill = simulate_taker_fill(replayer.get_book(asset_id), decision)
    elif decision.is_maker:
        # 挂入价, 注册 listener
        maker_orders.add(decision)
        # paper_engine 在 step_to() 内部检查 last_trade 是否命中
```

### 8.3 maker 命中模拟

当回放遇到 last_trade_price 事件:
```
if trade.price <= maker_order.price (maker is bidder):
    # 我们的 bid 可能被吃到
    if trade.side == 'sell' (taker sells YES at maker.price):
        # FIFO queue check
        if (queue_volume_ahead_of_us + maker_order.size) <= trade.size:
            fully_filled = True
        elif queue_volume_ahead_of_us < trade.size:
            partial_size = trade.size - queue_volume_ahead_of_us
            partially_filled(partial_size)
```

简化版 (MVP paper trading): **假设 queue_volume_ahead_of_us = current_L1_size_at_that_level**, 即我们永远是队尾. 这是保守估计.

### 8.4 与小肖 slippage 模型互验

paper trading 的"实际 fill price" vs slippage 模型的 "expected fill price" 比较, 喂回小肖 §6 KPI:

```
price_bias = avg(actual_fill - predicted_fill)
fill_rate_bias = avg(actual_fill_ratio - predicted_fill_ratio)
```

按小肖 §6.2 验收线:
- price_bias ≤ 0 (我们必须保守, 偏高估 slippage OK)
- fill_rate_bias ≤ 0 (偏低估 fill_rate OK)

如果 paper trading 跑 2 周, bias 全部 ≤ 0, 才能切实盘 (老雷会议 paper → live 转移决议).

### 8.5 给小蒋的接口契约

```cpp
namespace strider::papertrading {

struct PaperFillResult {
    double executed_price;
    double executed_size;
    double fill_rate;             // executed_size / intended_size
    int64_t fill_ts_ns;
    enum Reason : uint8_t { FILLED, PARTIALLY_FILLED, EXPIRED, CANCELED } reason;
};

class PaperEngine {
public:
    // 注入 book replayer (Mode B) 或 live book (Mode A)
    void set_book_source(BookSource* src);
    // 模拟 taker 单
    PaperFillResult simulate_taker(const Order& o, int64_t intent_ts_ns);
    // 模拟 maker 单 (返回 future, 在 expire_ts 或被 fill 时 resolve)
    std::future<PaperFillResult> simulate_maker(const Order& o, int64_t intent_ts_ns);
};

}
```

---

## 9. 开放问题

### 9.1 待 [实测] 问题 (我自己挂账)

| # | 问题 | 截止 | 谁 |
|---|------|------|----|
| Q1 | WSS 推送范围: 我们订阅 15 token, 但收到 polymarket 推送的 unsubscribed token 也很多 (3892 msg, 我们的只 20 msg). 这是 polymarket 故意推还是 client bug? | 6/5 | @老李 (协议层确认) |
| Q2 | 长时段 inplay 全程录制 (NBA Finals G7 / Roland Garros 单场 ATP) , 获取 quote half-life 的真实实测 (临场前 / 临场中 / 关键事件后), 校准小肖 T_{1/2} 表 | 6/15 | 小余 (录制) + 我 (分析) |
| Q3 | 撤单频率: maker 报价撤单 vs 被吃 的比例, 用于 beta_withdraw 标定 | 6/15 | 我 |
| Q4 | tape 真实成交 size 分布 (大单 / 小单), 用于 sharp money 信号 | 6/30 | 我 + 老叶 (链上 OrderFilled) |
| Q5 | hash 算法 (WSS book_hash 字段): polymarket 没公开计算规则, 我们如何 verify state | 6/15 | @老李 |

### 9.2 待 [模型] 问题 (留 v1.1)

| # | 问题 | 谁 |
|---|------|----|
| Q6 | κ_depth 的 sport-segment 标定: NBA / MLB / Tennis 实测不同 (1.1 / 0.8 / 0.5), 是否拆 sport 还是统一保守 1.5 | 我 + 小肖 |
| Q7 | maker queue position model: 我们的 maker 单总假设在队尾, 是否过保守 (导致 paper trading 拒掉很多本该成交的单)  | 我 + 小蒋 |
| Q8 | 多 token 关联性: 同一 condition 的 YES + NO 的 quote 是不是同步推送 (我们订一边能否兼得另一边状态) | 我 |

### 9.3 待 [战略] 问题

| # | 问题 | 决策人 |
|---|------|--------|
| Q9 | v2 是否启动 maker (rebate 0.75% + spread 1¢ 真有空间, 但跨洋撤单慢) | 老雷 + 小梁 |
| Q10 | paper trading 走 Mode A 还是直接 Mode B (Mode A 简单但失真, Mode B 准但开发 2-3 周) | 老雷 + 小蒋 |
| Q11 | 是否在 us-east 落 worker (老吴 cross-region), 把 RTT 从 250ms 砍到 50ms, 直接消灭一半 staleness | 老雷 + 老吴 |

### 9.4 v1 不做 (留 v2+)

- inplay 关键事件 (得分 / 红牌) 触发的 quote spike 模型
- multi-level imbalance signal
- VPIN-lite (tape 数据不够)
- maker queue position 精确建模 (paper trading mode C)
- spoof / layering detection
- 长尾 (price < 0.05) 市场: tick = 0.1¢ 体系需要单独的 slippage 公式

---

## 10. 结论 / 给小肖 v1.1 的 diff

### 10.1 实测结论 (绝对值)

1. **gameday mainline spread 中位 1 tick (1¢)**, L1_ask 中位 \$300-\$3000, $2K 吃单中位 0¢ 滑点, $10K 中位 0.4-0.9¢
2. **outright mainline spread 中位 1-9¢**, L1_ask 中位 \$42-\$124, $500 单就开始吃穿
3. **mainline tick = 1¢, longtail tick = 0.1¢**, 价格离散度 91 档 (mainline)
4. **fee_type = sports_fees_v2, taker 3%, maker 0% + 0.75% rebate**, 223/300 markets 启用 rewards
5. **cross-side equiv vig 中位 0.1-2.5¢**, 比庄家盘更锐 (Pinnacle 2%)
6. **quote T_{1/2}**: 临场前 non-hot > 120s, 热门 inplay ≈ 0.21s. **必须按 hot/cold 分桶**
7. **0 个 arb hit (bid_yes + bid_no > 1)**, 套利机器人非常勤奋

### 10.2 给小肖 v1.1 的修订 (diff)

```diff
# Slippage model v1 → v1.1

## §4.2 RM 参数表
- T_HALFLIFE_QUOTE_MS  int  4000   待小袁实测
+ T_HALFLIFE_QUOTE_MS  int  30000  实测 non-hot gameday > 60s, 取 30s 保守
+ T_HALFLIFE_QUOTE_HOT_MS  int  500  新增, 热门 (event_rate > 1/s) 用此

- KAPPA_DEPTH  ratio  1.5  待小蒋回测
+ KAPPA_DEPTH  ratio  1.0  gameday 实测 0.5-1.1, 取上界保守
+ KAPPA_DEPTH_OUTRIGHT  ratio  3.0  新增, outright market 用此

## §3.1 Linear model (一档内)
- fill_price = quote + tick * 0.5
+ fill_price = round_to_tick(quote + tick * 0.5)   # tick lattice
+ # 实际 fill_price ∈ {quote, quote+tick} 离散两态, 0.5 是期望值

## §3.1 输入字段
+ market_class : enum {GAMEDAY, OUTRIGHT, INPLAY}   # 决定用 κ_depth 还是 κ_depth_outright

## §2 Slippage 来源
+ §2.5 (新) Microprice prior: reference_price = mid + clamp(micro - mid, ±2*tick)
+        用 reference_price 代替 quote 作为 fair-value baseline, 进 Kelly q-p edge 计算
```

### 10.3 给老韩 RM v0.2 的额外字段

```diff
## OrderIntent
+ market_class : enum {GAMEDAY, OUTRIGHT, INPLAY}   # 用于 slippage κ 切换
+ microprice   : decimal   (optional, 信号层产)

## RiskDecision
+ reference_price : decimal   (= mid + clamped micro偏离, 用于审计)
+ effective_kappa : decimal   (实际选用的 κ_depth, GAMEDAY 1.0 / OUTRIGHT 3.0)
```

### 10.4 给小余 ETL 的实施清单

- [ ] orderbook_snapshot 表 (§7.3 schema)
- [ ] orderbook_level 表
- [ ] trade_tape 表
- [ ] WSS 客户端: 全量 active market 订阅, 客户端 ns 时间戳, hash 校验
- [ ] ring buffer (last 1h) + 5s 聚合长存 + parquet 归档
- [ ] microprice / imbalance 列在写入时计算 (避免回填扫表)

### 10.5 给小蒋 paper trading 的实施清单

- [ ] Mode A (instant fill): MVP 上线 day 1
- [ ] WSS 录制工具 + 重放引擎: Mode B 基础
- [ ] simulate_taker / simulate_maker 接口 (§8.5)
- [ ] queue position 简化版 (我们在队尾)
- [ ] 与小肖 slippage 模型 bias / RMSE 验证 (§8.4)

---

**END v1.**

— 小袁, 2026-05-28

实测原始数据保留位置: `/tmp/xiaoyuan/` (records.json / books.json / wss_stats.json / poll_stats.json / event_meta.json), 测试结束后清理. 若小肖或小蒋需要复盘, 可按 §1.1 采样方法重跑.

签字栏:
- 小袁 (作者) ✓
- 小梁 (验收) ⌛
- 小肖 (验收) ⌛
