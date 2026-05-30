# 二元市场双边盘口决策设计 v1

owner: 小袁 (quant-microstructure, C 量化研究部)
last_review: 2026-05-31

关联文件:
- `include/stcpp/microstructure/orderbook.hpp`
- `include/stcpp/numerical/slippage_model.hpp`
- `include/stcpp/microstructure/fill_rate_model.hpp`
- `include/stcpp/pricing/fair_value_estimator.hpp`
- `src/stcpp/paper/paper_loop.cpp`
- `include/stcpp/sizing/sizing_calculator.hpp`

跨域接口受众: 老周 (结构/数据), 小梁 (Kelly/选边), 老韩 (RM/slippage 入参)

---

## 0. 背景与问题陈述

老板原话:
> 「一个二元市场应该既持有 yes 的快照，也持有 no 的快照，触发 WSS 订阅时，只刷新单边的，
> 这个没问题。但是进入决策时，我们一定要带入这个盘口的信息，而不仅仅是单单一边的信息。」

当前实现 (`paper_loop.cpp::TickAll`) 的真实缺陷：

1. 决策全程以 YES token 为主体，NO book 仅为 de-vig 提供一个标量 `no_token_mid`
   (paper_loop.cpp L251-265)，不进行独立 edge 评估。
2. YES 高估时代码路径是跳过（`!sz_in.buy_yes` 隐含不产生 intent），但没有对称地评估
   「此时 NO 是否被低估、买 NO 是否有正 edge」。
3. NO book 的 fill_rate/slippage/depth 完全未被使用 — `book_depth_l1` 始终来自
   `feat.best_ask_size()`（YES 的 L1），sizing 的 `SlippageModel` 入参用的是 YES 侧数据。
4. 现有 `devig_binary` 已正确实现双边 de-vig 核心公式（`yes_mid / (yes_mid + no_mid)`），
   但调用方没有把结果用于「NO 方向 edge」评估。

---

## 1. 双边 fair / de-vig 公式

### 1.1 记号

```
yes_mp  = YES token microprice (来自 hub_.Read(token0).microprice)
no_mp   = NO  token microprice (来自 hub_.Read(token1).microprice)
yes_ask = YES token best_ask
no_ask  = NO  token best_ask
```

### 1.2 双边 de-vig fair

现有 `devig_binary` (pricing/fair_value_estimator.hpp L235-253) 的公式：

```
fair_YES_mkt = yes_mp / (yes_mp + no_mp)      (双边有效时)
fair_NO_mkt  = no_mp  / (yes_mp + no_mp)      = 1 - fair_YES_mkt
```

这是加性去 overround（乘法等价形式的极限）。与单边退化相比，它的优势是：
- `yes_mp + no_mp > 1`（存在 vig）时，分母校正剥离了 overround。
- 单边退化 `fair_YES = clamp(yes_mp)` 包含了 vig，会系统性高估。
- 双边公式确保 `fair_YES_mkt + fair_NO_mkt = 1`（数学封闭）。

这个函数已经存在且正确，调用方需要把 `fair_NO_mkt = 1 - fair_YES_mkt` 显式保留下来，
而不是丢弃它。

### 1.3 与 Goalserve 先验混合后的 coherent fair pair

paper_loop 的 `blend_prob` 步骤（L406-407）只混了 YES 侧：

```
p_fair_YES = blend_prob(p_prior_YES, fair_YES_mkt, conf)
```

为保持 coherence：

```
fair_YES_mkt = devig_binary(yes_mp, no_mp)         # 存在值
fair_NO_mkt  = 1.0 - fair_YES_mkt

p_prior_YES  = inplay_score_prior_yes(...)
p_prior_NO   = 1.0 - p_prior_YES

p_fair_YES   = blend_prob(p_prior_YES, fair_YES_mkt, conf)
p_fair_NO    = 1.0 - p_fair_YES                    # 严格从 YES 推导，不独立计算
```

关键点: `p_fair_NO` 必须定义为 `1 - p_fair_YES`，不能对 NO 侧独立调用 `blend_prob` 再
归一化。独立混合 + 独立归一化会引入微小不一致，当两边 book 信息量不同时可能产生
`p_fair_YES + p_fair_NO != 1` 的浮点偏差，导致双边都显正 edge（见第 4 节）。

### 1.4 数值稳定性要求

- `yes_mp` 和 `no_mp` 均需通过 `isfinite && > 0 && < 1` 验证后才进入 `devig_binary`。
- 现有代码（L258-264）已做这个验证，`no_token_mid` 赋值逻辑正确。
- 要求调用方明确保存 `fair_NO_mkt = 1.0 - fair_YES_mkt`，精度由 IEEE 754 减法保证
  （两个接近 0.5 的数，减法误差在 1e-15 量级）。

---

## 2. 选边逻辑（核心）

### 2.1 两个候选交易

给定 coherent fair pair `(p_fair_YES, p_fair_NO)` 和双边盘口：

```
edge_buy_YES = p_fair_YES - yes_ask          (买 YES 的毛 edge)
edge_buy_NO  = p_fair_NO  - no_ask           (买 NO 的毛 edge)
```

注意：买 NO = 做多 NO token（一笔 BUY 单，不是卖空）。风控/MtM 已 token-agnostic 支持。

### 2.2 每边独立的净 edge（扣除 slippage + fee）

选边判断不能用毛 edge，必须经过各自 book 的微观结构调整：

```
# YES 侧
slip_YES = SlippageModel::compute({order_size, yes_ask, yes_book_depth_l1, ...})
fill_YES = FillRateModel::compute_maker(yes_book_snapshot, yes_probe, intent_YES)
net_edge_YES = edge_buy_YES - slip_YES.slippage_rate - fee_per_unit(yes_ask)

# NO 侧（用 NO 自己的 book depth，不能借用 YES 的）
slip_NO  = SlippageModel::compute({order_size, no_ask,  no_book_depth_l1,  ...})
fill_NO  = FillRateModel::compute_maker(no_book_snapshot, no_probe, intent_NO)
net_edge_NO  = edge_buy_NO  - slip_NO.slippage_rate  - fee_per_unit(no_ask)
```

`fee_per_unit(p) = kSportsTakerFeeRate * p * (1 - p)` — 与现有 `SizingCalculator::compute_net_ci_edge` 同公式。

### 2.3 选边判据

```
候选集 = {}
if net_edge_YES > 0 and fill_YES.fill_rate >= FILL_RATE_FLOOR:
    候选集.add(YES, net_edge_YES, fill_YES)
if net_edge_NO  > 0 and fill_NO.fill_rate  >= FILL_RATE_FLOOR:
    候选集.add(NO,  net_edge_NO,  fill_NO)

case 候选集为空:
    → 不下单（fail-closed）

case 候选集有唯一候选:
    → 下那边

case 两边均正 edge（罕见但可能）:
    → 取 risk-adjusted net edge 较大的一边（见下方）
    → 不同时下两边（一致性 enforce，见第 4 节）
```

### 2.4 risk-adjusted 选边：每边期望净值

当两边均正 edge 时，比较各自的期望成交净值：

```
expected_pnl_YES = net_edge_YES × fill_YES.fill_rate
expected_pnl_NO  = net_edge_NO  × fill_NO.fill_rate

选 expected_pnl 较大的一边
```

这里不直接比毛 edge 的原因：两边 book 厚度可能差异极大（常见：YES book 深，NO book 薄），
薄边的 slippage 高、fill_rate 低，毛 edge 即使稍大也可能 expected_pnl 更差。

### 2.5 CI 下界要求（与现有 sizing gate 对齐）

选边后，所选边的 `edge_ci_lower`（= `net_edge - z * sigma`）必须 > 0，与现有
`ComputeEdgeCiLower` + `sizing_out.valid` gate 对齐。这一步由 SizingCalculator 处理，
选边逻辑在 CI gate 之前做即可（先选边，再对选定边走 SizingCalculator）。

---

## 3. Per-book 微观结构要求

### 3.1 不可共用的原则

YES 侧和 NO 侧的以下入参必须来自各自 book，绝对不能互用：

| 入参字段 | 来源要求 |
|---|---|
| `SlippageInput.book_depth_l1_usdc` | 各边 `best_ask_size()`（或 top3 depth） |
| `SlippageInput.quote_price` | 各边 `best_ask` |
| `SlippageInput.book_snapshot_ts_ns` | 各边 `ingestion_ts_ns` |
| `FillIntent.price` | 各边 `best_ask` |
| `FillRateModel` 入参 `OrderBookSnapshot` | 各边独立快照 |
| `Microprobe.quote_half_life_ms` | 各边独立（YES/NO 的 QHL 可能不同） |

当前代码错误（paper_loop.cpp L417-419）：
```cpp
const double book_depth_l1 = feat.best_ask_size();  // feat 是 YES token 快照
// ... 传给 RM intent.book_depth_l1_usdc (L538)
// NO 方向如果以 YES 的 L1 deep 做 slippage 计算，结果错误
```

修正要求：如果选边为 NO，`book_depth_l1` 必须用 `no_feat.best_ask_size()`，
`SlippageModel` 的 `book_snapshot_ts_ns` 必须用 `no_feat.ingestion_ts_ns`。

### 3.2 SlippageModel 入参规范（对接老韩 RM）

两边各自独立构造 `SlippageInput`：

```
# YES 侧
SlippageInput si_yes;
si_yes.order_size_usdc     = intended_notional;
si_yes.quote_price         = yes_feat.best_ask();
si_yes.book_depth_l1_usdc  = yes_feat.best_ask_size();   // YES L1 ask 深度
si_yes.book_snapshot_ts_ns = yes_feat.ingestion_ts_ns;
si_yes.wall_now_ns         = NowNs();
si_yes.tick_size           = 0.01;

# NO 侧
SlippageInput si_no;
si_no.order_size_usdc      = intended_notional;
si_no.quote_price          = no_feat.best_ask();
si_no.book_depth_l1_usdc   = no_feat.best_ask_size();    // NO L1 ask 深度（不借 YES 的）
si_no.book_snapshot_ts_ns  = no_feat.ingestion_ts_ns;
si_no.wall_now_ns          = NowNs();
si_no.tick_size            = 0.01;
```

### 3.3 RM OrderIntent 的 book_depth_l1_usdc 字段

当前 paper_loop.cpp L538 把 YES book depth 乘 1e6 传给 RM：
```cpp
intent.book_depth_l1_usdc = book_depth_l1 * 1'000'000.0;
```
选边为 NO 时，此字段必须改为 NO book 的 L1 深度（同样乘 1e6 转 micro）。
RM 的 `check_liquidity_` 校验的是「订单相对于该边 book 的深度比」，用错边的数据会给出
错误的流动性通过/拒绝判断。

---

## 4. 一致性 / 防套利自打架

### 4.1 coherent fair 的数学保证

本设计要求 `p_fair_NO = 1 - p_fair_YES`（严格，不独立计算）。
若两边各自独立 blend 再各自 clamp，可能出现：
```
p_fair_YES = 0.52  (clamp 后)
p_fair_NO  = 0.49  (独立计算，clamp 后，不是 1 - 0.52)
edge_YES = 0.52 - 0.51 = +0.01  (YES ask = 0.51)
edge_NO  = 0.49 - 0.48 = +0.01  (NO ask  = 0.48)
```
两边都显正 edge，且 sum(p_fair) = 1.01 ≠ 1，说明 fair 本身不 coherent，
边缘是假信号。

保证方式：`p_fair_NO` 永远从 `p_fair_YES` 推导，`clamp_prob` 仅对 `p_fair_YES` 单次施加。

### 4.2 禁止同时下两边

即使两边均显正 net edge（理论上是 coherent fair 构造下不可能同时发生的，但防代码 bug），
也只能选一边下单，不能同时下两边。同时买 YES 和买 NO 在二元市场中等价于锁仓（两边都持有，
净敞口归零），支付了双倍 taker fee 而无盈利。这违反铁律 #2（纪律高于收益）。

选边规则在前（2.3 节），选定后只构造一个 `OrderIntent`。

### 4.3 两边都负 edge 时

两边 net_edge 均 <= 0 → 不下单，与现有 fail-closed 逻辑一致。不因为「市场有 vig 所以总有一边正」
而强行选边——vig 存在说明市场报价在 fair 两侧都有缓冲，没有信号时不该交易。

---

## 5. 陷阱与 fail-safe

### 5.1 WSS 单边刷新导致快照不同步

问题：WSS 对 YES 和 NO 是各自独立的 token channel，一个事件到达时只刷新一边。在跨洋高延迟
链路下，两边快照的 `data_source_ts_ns` 可能相差数百毫秒甚至更多。

fail-safe 规则：
```
ts_age_YES = now_ns - yes_feat.data_source_ts_ns
ts_age_NO  = now_ns - no_feat.data_source_ts_ns

freshness_threshold = score_staleness_limit_ns  (现有配置，约 5-30 秒)

if ts_age_YES > freshness_threshold:
    跳过整个盘口（不仅仅是 YES 方向），fail-closed
if ts_age_NO  > freshness_threshold:
    only_use_yes_side = true
    → 只能评估 YES 方向（单边退化）
    → devig 退化为 clamp(yes_mp)（单边去 vig 精度低，但可用）
    → 不评估 NO 方向（NO book 数据陈旧，slippage/depth 不可信）
```

原则：信息不全时宁可只信新鲜那边，fail-closed 优先（铁律 #2）。
不许「用 YES 的 fair 对着 NO 的陈旧 ask 算 NO edge」——陈旧 ask 不代表现在的成交价。

### 5.2 NO book 薄或空

问题：NO token 流动性通常比 YES 差，可能 best_ask_size 极小甚至 L1 为空。

fail-safe 规则：
```
if no_feat.best_ask_size() < MIN_LIQUIDITY_USDC:
    # NO book 太薄，fill_rate 极低，或 SlippageModel 返回 ExceedBookDepth
    # 不评估 NO 方向 edge；YES 方向仍可独立评估
    skip_no_side = true
```
`MIN_LIQUIDITY_USDC = 2000.0`（来自 `orderbook.hpp L43`，实测中位 NBA $124、Tennis $5017，
取保守底线）。

注意：NO book 薄意味着即使 NO 显正 edge，实际能成交的量极少，expected_pnl 很低。
SlippageModel 的 `FILL_RATE_FLOOR = 0.50` 会自然拦截大多数薄 book 场景，
不需要额外 gate。但需要保证 `SlippageInput.book_depth_l1_usdc` 用的是 NO 自己的数据。

### 5.3 两边 spread 差异大

Polymarket 二元市场 YES/NO 的 spread 通常不对称：临近 1.0 一侧的 spread 更宽（极端概率）。
如果 YES ask = 0.98，NO ask = 0.01，两边的 fee/slippage 特性完全不同。

处理：无特殊逻辑，每边独立的 `SlippageModel` 和 `fee_per_unit = k * p * (1-p)` 已经
天然处理了这一不对称——极端价格下 fee_per_unit 趋近 0，但 slippage 可能因 spread 宽而更大。
`SlippageModel.validate` 会拒绝 `quote_price >= 1 - EPS` 或 `<= EPS` 的边界情况。

### 5.4 devig 单边退化路径的诚实标注

`devig_binary` 的三条退化路径（来自 `fair_value_estimator.hpp L239-253`）：
- 双边无效 → nullopt → 不产生任何 intent（fail-closed，正确）
- 仅 YES 有效 → `clamp(yes_mp)`（单边裸 mid，精度低但可用，只评估 YES 方向）
- 仅 NO 有效 → `clamp(1 - no_mp)`（NO 隐含 YES，只评估 YES 方向）

退化时：不评估 NO 方向 edge（因为没有双边 de-vig fair，无法得到 coherent `p_fair_NO`）。
`p_fair_NO` = `1 - clamp(yes_mp)` 不可用于 NO 方向选边，因为 `clamp(yes_mp)` 含 vig，
`1 - clamp(yes_mp)` 也含 vig，不是 NO 的 fair 概率。

---

## 6. M1 vs M2 诚实边界

### 6.1 M1 可以做的（选边买入方向做多）

- 买 YES 多头（现有实现）
- 买 NO 多头（本设计扩展，新增 NO 方向选边）

两者都是「BUY token」单向开仓，不涉及卖空。风控（`RiskGateway`）已 token-agnostic，
`position_ledger` 按 `token_id` 记账，YES 和 NO 各自是独立 token，持仓逻辑已支持。

`OrderIntent.outcome` 需要从 `strategy::Outcome::Yes` 扩展为根据选边设置
`strategy::Outcome::Yes` 或 `strategy::Outcome::No`，这是代码层需要修改的地方（交给实施方）。

### 6.2 M2 才能做的（做空 / sell-to-open）

- Sell YES（空 YES = 开空仓，需要对手方成交，CLOB sell-to-open）
- Sell NO（同上）
- Market making（双边挂单，同时挂 YES/NO 的 bid/ask）

以上需要更复杂的 inventory management、风控和签名逻辑，延 M2。

本设计（选边买入做多）全部在 M1 范围内，不触及 M2 边界。

---

## 7. 与跨域接口的对接说明

### 7.1 与老周（双边快照架构 / 数据结构）

老周已定义 `BinaryMarketBookView`（`state_provider.hpp L400-407`），包含：
- `token0`: YES token book snapshot（含 `OrderBookFeatures`）
- `token1`: NO token book snapshot
- `cross_spread = token0.best_ask + token1.best_ask - 1.0`（等效 vig）

老周需要确保：
1. `OrderBookSnapshotHub` 中两个 token 的快照均可独立 `Read()`，已满足（现有实现）。
2. 决策层（`TickAll`）在进入 `TickOne` 前同时读取两边快照（现有 P1-8 路径已读 NO，
   但仅取 `no_token_mid` 标量，需扩展为传入完整 `no_feat`）。
3. 决策层需要访问 `no_feat.best_ask()`、`no_feat.best_ask_size()`、
   `no_feat.ingestion_ts_ns` 等字段，从现有 `Optional<OrderBookFeatures>` 中取即可，
   无需新的数据结构。

接口变更请求（给老周）：`TickOne` 签名从
```cpp
void TickOne(const std::string& condition_id, const std::string& token_id,
             const OrderBookFeatures& feat, double no_token_mid);
```
扩展为
```cpp
void TickOne(const std::string& condition_id, const std::string& token_id,
             const OrderBookFeatures& yes_feat,
             const std::optional<OrderBookFeatures>& no_feat);  // 完整 NO 快照
```
`no_feat` 为 optional，没有 NO book 时退化为现有单边路径，不破坏现有逻辑。

### 7.2 与小梁（Kelly / 选边策略 / sizing）

小梁持有 Kelly sizing 主权。本设计确定：
- 选边逻辑（选 YES 还是 NO）由微观结构层决定（risk-adjusted net edge 比较）。
- 选边结果作为单一候选传给 `SizingCalculator`，后者只需处理一个方向。
- `SizingInput.buy_yes` 字段根据选边结果设置（YES 选 `true`，NO 选 `false`）。
- Kelly 分母已区分方向：`buy_yes=true → denom = 1 - ask_yes`；`buy_yes=false → denom = ask_no`，
  小梁确认这两个路径在 `SizingCalculator::kelly_denom_` 中已正确实现。

小梁需要确认的问题：
1. 当选边为 NO 时，`SizingInput.price` 应填 `no_ask`，`fair_value` 应填 `p_fair_NO`，
   `edge_ci_lower` 应用 NO 方向的 CI 下界。这与现有接口完全兼容，无需改动 `SizingCalculator`。
2. 两边均正 edge 时，本设计选 `expected_pnl = net_edge × fill_rate` 较大的一边。
   小梁可在策略评审中确认此判据是否需要调整为 Kelly-weighted expected_pnl（引入 bankroll
   比例），或当前简单期望值比较已足够 M1。

### 7.3 与老韩（RM / SlippageModel 入参）

关键接口要求（第 3 节已详述）：
1. `OrderIntent.book_depth_l1_usdc` 必须来自被选边的 book depth，不能始终用 YES。
2. 选边为 NO 时，`intent.token_id` = NO token，`intent.outcome` = `Outcome::No`，
   `intent.price` = `no_ask`，`intent.size_pUSD_micro` 由 NO 方向的 `SizingCalculator` 给出。
3. RM 的 `check_liquidity_` 校验 `size_pUSD_micro vs book_depth_l1_usdc`，
   用错误边的 depth 会导致流动性判断错误（可能放行实际无法成交的 NO 方向大单）。

---

## 8. 实施优先级建议（交给 GM 决定）

本文件是微观结构设计规范，不含代码实施。以下按复杂度顺序排列，供 GM 参考：

1. 最小改动（风险最低）：将 `TickOne` 中 `no_token_mid` 参数替换为完整 `no_feat`，
   增加 NO 方向 net_edge 计算，选边后对被选边构造 `OrderIntent`。估计影响文件：
   `paper_loop.cpp`，约 80 行。

2. 同步修改：`OrderIntent.outcome` 根据选边设置（目前硬编码 `Outcome::Yes`，见 L511）。

3. 后续优化（M1+ 非阻塞）：`FillRateModel` 完整入参替换（当前 sizing 用固定 `fill_rate=0.65`，
   paper_loop.cpp L429，两边均应替换为对应 book 的 `FillRateModel::compute_maker` 结果）。

---

## 附录 A：现有代码中 NO book 利用现状核查

| 位置 | 字段 | 当前用法 | 缺陷 |
|---|---|---|---|
| paper_loop.cpp L251-265 | `no_opt->microprice` | 只取标量用于 devig | 完整快照被丢弃 |
| paper_loop.cpp L267 | `TickOne(..., no_token_mid)` | 只传一个 double | NO book depth/ts/ask 丢失 |
| paper_loop.cpp L299 | `devig_binary(yes_mid, no_token_mid)` | 双边 de-vig 正确 | 结果未用于 NO 方向 edge |
| paper_loop.cpp L417-419 | `book_depth_l1 = feat.best_ask_size()` | 始终用 YES L1 | NO 方向选边时应用 NO L1 |
| paper_loop.cpp L511 | `intent.outcome = Outcome::Yes` | 硬编码 YES | NO 方向时需改为 Outcome::No |
| paper_loop.cpp L519 | `intent.price = best_ask` | YES ask | NO 方向时需用 no_ask |
| paper_loop.cpp L538 | `intent.book_depth_l1_usdc = book_depth_l1 * 1e6` | YES L1 depth | NO 方向时需用 NO L1 |
