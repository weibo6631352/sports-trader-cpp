# Live 看板/API 量化决策可用性反馈 — 小梁 (量化研究部)

- **owner:** 小梁 (C 量化研究部主管)
- **last_review:** 2026-05-30
- **场景:** GM 令实地使用 live 系统 (后端 `http://127.0.0.1:8080`, 真实 Polymarket book + Goalserve)
- **方法:** curl `/api/v1/events` 拿真实 condition → 逐个 curl `quote / book_pair / market / gate/paper / metrics`, 比对前端截图 `docs/dashboard-final-live-honest.png` + 端点/paper_loop/fair_value 源码
- **结论一句话:** **book 微观结构是真值且够用; 但 fair/edge/Kelly 是 stub 伪信号, de-vig fair prob、组合风险指标、fill/PnL 全空 — 当前看板量化上只能"看盘", 不能"做决策"。**

---

## 0. 实测取证 (真实 condition, 非 demo)

`/api/v1/events` 返回 10 个真实 outright (NHL Stanley Cup / NBA Finals / FIFA WC), `data_source:"live"`, sport 字段空。

| 端点 | 取证 (真实 cid) | 判定 |
|---|---|---|
| `book_pair/<cid>` | token0/token1 双边 5 档深度、best_bid/ask、microprice、spread、imbalance、cross_spread=0.01、4 时间戳齐全、wss_state=CONNECTED | **真值, 可用** |
| `quote/<cid>` (NHL CAR) | fair=0.512, mid=0.560, **edge_bps=0, kelly=0, suggested_notional=0, signal=0**, model_kind=`stub`, model_calibrated=false | **stub, 不可决策** |
| `quote/<cid>` (Spain WC) | fair=0.434, mid=0.169, **edge_bps=1076, kelly=0.032, notional=32, signal=1** | **假信号 (见 §1)** |
| `market/<cid>` | **全部 found:false**, tick_size/fee_bps/sports_market_type 全 null | **空, 盘口无法区分** |
| `gate/paper` | has_data=false, n_trades=0, sharpe=0, prelim_pass=false | **无交易历史, 空** |
| `/metrics` | net_edge_bps=0, cum_net_pnl=0, fill_total=0, rm_decision=0, loop_latency_p99=0, wss_connected=0 | **全 0, 无运行时真值** |

---

## 1. 核心问题: fair_value 是 stub, edge/Kelly 是结构性假信号 (P0)

源码链 (`src/stcpp/paper/paper_loop.cpp` + `src/stcpp/pricing/fair_value_estimator.cpp`):

- paper_loop L183-186: **"M1: 无 Goalserve game_row → 退化纯订单簿先验 (score_diff=0, time_frac=0)"**。即 game_row 是空壳。
- fair_value_estimator: `log_odds_prior = alpha*score_diff + beta*time_frac = 0` → `p_yes_prior = sigmoid(0) = 0.5`。
- 然后 microprice 以**固定 kappa=0.20** 混合: `fair = 0.8*0.5 + 0.2*microprice`。

**数学后果: fair_value 恒等于把 microprice 朝 0.5 收缩 80%。** 它不含任何体育胜率先验, 只是 mid 的衰减版。

- 验证: NHL CAR microprice=0.560 → fair=0.8*0.5+0.2*0.560=0.512 ✓ 完全吻合。
- **这导致两类系统性错误:**
  1. **接近 0.5 的盘** (NHL/NBA mid≈0.42-0.56): fair 被拉回 mid 附近, edge≈0, kelly=0 → 永不出信号 (假阴性, 漏掉所有真机会)。
  2. **极端价 outright** (Spain WC mid=0.169): fair 被强拉到 0.434, 凭空造出 26 个点 edge → edge_bps=1076, kelly=0.032, **suggested_notional=32 pUSD 真给了下单建议**。这是纯结构性假信号 (假阳性), 量化上是**反向有害** — 收缩算子把"低概率冷门"系统性高估, 照此 sizing 会持续买入被高估的尾部 outright。

> 量化裁定: **当前 edge/Kelly 字段不仅是"空", 是"错的非零"。比空更危险 — 看板会诱导对低价 outright 下注。** 在真 fair 模型 (Goalserve 实时比分/Elo/poisson 接入) 落地前, edge/kelly/signal/suggested_notional **必须前端灰显 + API 标 `model_calibrated:false` 时不渲染数值**, 防止任何人 (含 paper) 据此决策。当前 model_calibrated=false 已标, 但前端未据此屏蔽。

---

## 2. 量化决策必需但当前 API 没有/是空的字段 (按重要性)

| 缺口 | 现状 | 量化为何必需 | 优先级 |
|---|---|---|---|
| **de-vig fair prob (去抽水市场隐含概率)** | **完全缺失。** quote 只有原始 fair 和 microprice mid, 无去 vig 后的市场隐含真概率 | 做市/方向的基准 anchor 是"市场共识概率", 必须先用 cross_spread(vig) 把 bid/ask 还原成无抽水隐含概率, 才能比对自有 fair 算真 edge。现在拿带 vig 的 mid 当基准, edge 本身就偏 | **P0** |
| **真 fair 模型 (体育先验)** | stub, 见 §1 | 没有它整条 edge/kelly/signal 链全是噪声 | **P0** |
| **net edge 分桶分布** | 只有单点 edge_bps, /metrics net_edge_bps=0 | 需要 edge 直方图/分位 (>0 / >50bps / >100bps 占比) 判断策略容量与机会密度, 单点无法评估 | **P1** |
| **滚动 Sharpe / Sortino / 真 PnL 时序** | gate/paper sharpe=0 has_data=false, metrics cum_net_pnl=0 | 北极星是 Sharpe≥1.5; 无 PnL 时序无法算滚动 Sharpe/Sortino/DD, gate 永远 prelim_pass=false | **P1** |
| **敞口 (exposure) 聚合** | paper_loop 里 current_*_exposure 恒置 0 (L240-241), API 无敞口端点 | Kelly sizing 必须扣减已有敞口 (per-token / per-condition / per-neg_risk_market 组), 现在恒 0 → sizing 不知已持仓, 会重复加仓; neg_risk 同组互斥更需聚合 | **P1** |
| **fill rate (真实成交率)** | paper_loop 硬编码 fill_rate=0.65 (L237), metrics fill_total=0 | net edge = gross edge × fill_rate − fee − slippage; fill_rate 是 net edge 的乘子, 用常数等于 sizing 失真。需真实成交回填 | **P1** |
| **slippage (真实滑点)** | 硬编码 8bps (L238) | 同上, net edge 直接受影响 | **P1** |
| **fair CI 真值** | fair_ci 是 fair±0.05 写死 (paper_loop L520-521), model_confidence=0 | Kelly fractional 应按 CI 宽度缩放; 写死 ±5% 使 fractional Kelly 失去意义 | **P2** |
| **VaR / CVaR / 组合相关性** | 无 | 同一 neg_risk_market (如 NBA Finals 8 队互斥) 持仓高度负相关, 单腿 VaR 严重高估真实组合风险; 跨盘口相关性影响组合 Kelly | **P2** |

---

## 3. 逐项回答 GM 命题

**Q: 真实 book 数据够做市/方向决策吗?**
A: **book 本身够** — 双边 5 档深度、microprice、spread、imbalance、cross_spread、4 时间戳、wss_state 全是真值, 微观结构层面可支撑做市。**但决策层 (fair/edge/kelly) 不够**, 是 stub。

**Q: edge/fair/Kelly 是真值还是空? paper 循环 publish 的 quote 有真数据吗?**
A: paper 循环**确实 publish 了真 quote 快照** (每 token 都发), book 部分真; 但 fair 是 microprice 收缩伪值, edge/kelly 要么 0 要么结构性假信号 (§1)。**有数据, 但不是有效信号。**

**Q: cross_spread(vig)、microprice/imbalance 对做市够用吗?**
A: 单点上够 (都是真值)。但缺两样: (1) **没有用 cross_spread 反推 de-vig 隐含概率** (P0); (2) imbalance 只有瞬时值, 缺滚动/OFI (order flow imbalance) 时序, 做市报价偏移和库存管理需要趋势而非快照。

**Q: sports_market_type=unknown(outright), 量化上够区分盘口吗?**
A: **不够。** market 端点 found:false → sports_market_type 全 null。量化对 Moneyline / Totals / Spreads / 分节 / outright 的 fair 模型、相关性结构、sizing cap 完全不同 (outright 多腿互斥 neg_risk, Totals 是连续区间)。无 market_type 无法路由到正确模型, 也无法做 neg_risk 组聚合。当前 10 个全是 outright 且 type 缺失, 量化无法区分盘口。

**Q: gate/paper (门禁) 现状对量化决策有意义吗?**
A: 框架对 (prelim/confirm 双门 + sharpe_se + p_value + hit_rate + DD 字段齐全, 这是对的统计门禁设计)。但 has_data=false n_trades=0 → 永远 prelim_pass=false, **当前无意义** (无交易历史喂入)。一旦 §2 的 PnL 时序回填, 这个 gate 就能用 — 是架子好但没数据。

---

## 4. 优先级汇总 + 建议

### P0 (阻断量化决策, 必须先做)
1. **接入真 fair 模型**: paper_loop 喂入真实 Goalserve game_row (比分/时钟), 或对 pregame/outright 接 Elo/market-consensus 先验。当前 stub 不可作任何决策依据。
2. **de-vig fair prob 端点字段**: quote 增 `devig_implied_prob` (用 cross_spread 还原 bid/ask 无抽水隐含概率, 二元用 `p = bid/(bid+(1-ask))` 归一), 作为 edge 基准 anchor。
3. **前端据 `model_calibrated:false` 屏蔽 edge/kelly/signal/notional 数值** (灰显或 "—"), 杜绝据假信号决策。当前 API 已标 false, 前端未消费。

### P1 (sizing/绩效正确性)
4. 真敞口聚合端点 (per-token / per-condition / per-neg_risk_market 组), 回填 sizing 的 current_exposure。
5. PnL 时序落账 + 滚动 Sharpe/Sortino/DD (喂活 gate/paper)。
6. fill_rate / slippage 用真实成交回填, 替换硬编码 0.65 / 8bps。
7. net edge 分桶分布 (直方图/分位) 上 /metrics 和看板。
8. market 端点接 gamma catalog 快照 → tick/fee/sports_market_type 出真值。

### P2 (组合/精细化)
9. fair CI 真值 (替换 ±0.05 写死) + model_confidence 驱动 fractional Kelly。
10. VaR/CVaR + neg_risk 组内相关性 → 组合 Kelly (我出公式 + 阈值, 风控 enforce)。

---

## 5. 给上级 (GM) 一句话

**看板的"管道"通了 (真 book + 真时间戳 + 对的 gate 架子), 但"决策大脑"还是 stub —— fair/edge/Kelly 不仅是空, 对低价 outright 是会诱导下注的假信号; 量化最缺的两件事是真 fair 模型 (接 Goalserve 比分) 和 de-vig 隐含概率基准, 这俩不补齐, 看板只能看盘不能做单, 谁据现在的 edge/kelly 下注都是踩坑。**

---

## 6. 边界声明

本反馈定量化字段需求 + 数学缺口 + 优先级 (我的主权: Sharpe/Kelly/de-vig/组合风险)。
- 具体盘口 fair 模型适配 → 量化研究 IC (小程/小蒋/小袁/老彭)。
- 实现/端点编码 → 系统工程部 (小卢/老李/小冯)。
- 风控阈值 enforce → 风控部 (老韩)。
- 我不写代码、不做体育专精、不碰微观结构实现。
