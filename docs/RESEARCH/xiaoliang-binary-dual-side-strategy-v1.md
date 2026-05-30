# 二元市场双边决策策略仲裁 v1

> **owner:** 小梁 (量化研究部主管, Sharpe/Kelly 主权)
> **last_review:** 2026-05-31
> **status:** 策略仲裁 — 供 GM 老雷拍板
> **触发:** 老板原则「进入决策时必须带入整个盘口的信息, 而不仅仅是单边」
> **关联:**
> - 老周并行架构设计: 双边快照 + TickOne 双 book 结构
> - 小袁并行微观结构设计: 双 book fair/de-vig + 选边判据 + per-book slippage
> - 老韩: per-condition exposure cap 语义 (ADR-042 §2 已联签)
> - ADR-042: Kelly sizing 跨单元数学契约
> - `src/stcpp/paper/paper_loop.cpp` TickOne 当前实现 (仅 YES side)

---

## 0. 仲裁摘要 (TL;DR — 三个决策点)

| 决策点 | 结论 | 主权 |
|---|---|---|
| **独立 alpha 还是冗余** | 同一信号换边表达, 但市场定价独立 → 可交易的 mispricing 不同; 不是翻倍机会, 是互斥选择 | 小梁 |
| **同 condition 单边 vs 双边** | **每 condition 每 tick 只选一边下单** (Kelly-optimal); 禁止同时持 YES 多 + NO 多 (隐性双重暴露) | 小梁 + 老韩联签 |
| **M1 scope** | 双边 fair/de-vig 信息用于决策 = M1 必做; 主动选边买 NO = M1 可做但需老韩 checklist 先绿 | 小梁建议, GM 拍板 |

---

## 1. YES/NO 是独立 alpha 还是冗余?

### 1.1 理论约束

Polymarket 二元市场: `fair_YES + fair_NO = 1`。我方 fair 概率满足此等式 (由 de-vig 推断)。市场价格独立: `ask_YES` 和 `ask_NO` 各自由不同 maker 报价, **不保证 ask_YES + ask_NO = 1**。

实测 (小袁 §1.4): cross-side equiv vig = ask_YES + ask_NO - 1, 中位 0.1-2.5¢。等效 vig > 0 说明:

```
ask_YES + ask_NO > 1
=> 1 - ask_NO > fair_YES - vig_component  (YES 方看涨 edge 因 ask_NO 偏高而低估)
=> ask_NO < 1 - ask_YES                   (NO 方"便宜"了)
```

当 YES 方 `fair_YES < ask_YES` (无 YES 信号) 时, NO 方 `fair_NO = 1 - fair_YES > 1 - ask_YES`。
由于 `ask_NO` 独立定价, `ask_NO` 有可能 < `1 - ask_YES`, 也就是 `ask_NO < fair_NO`。
**这时买 NO 有正 edge, 而买 YES 没有。这是同一个信号在不同 market 价格错配下的换边表达, 不是两个独立的 alpha。**

### 1.2 定性结论

**是「同一信号换边表达」, 不是翻倍机会。**

- fair 信息来源是同一个 (Goalserve score + FairValueEstimator), fair_YES 和 fair_NO 互为补数, 不携带独立信息。
- 可交易的 mispricing 来自两边 **市场价** 的独立错误定价。同一 tick 内, mispricing 通常只在一边存在 (向 YES 或向 NO)。
- 极端情况: 双边同时 mispricing (ask_YES 低估 + ask_NO 低估, 即 ask_YES + ask_NO < 1)。实测 0 个 arb hit (小袁 §1.4), 套利机器人已清除。**这种情况在正常市场中不出现。**

### 1.3 隐性双重暴露风险 (关键)

若同 condition 同时买 YES 多 + 买 NO 多:

```
设: 买 YES size=x, 买 NO size=y
结算 YES 发生: PnL = x*(1 - ask_YES) - y*ask_NO
结算 NO 发生: PnL = y*(1 - ask_NO) - x*ask_YES
```

两种结算都有输有赢, 合并看:

```
期望 PnL = x*(fair_YES - ask_YES) + y*(fair_NO - ask_NO)
```

**如果双边 edge 异号** (一边正一边负), 同时持有会部分对冲 edge → edge 降低但风险敞口不降。
**如果双边 edge 同号** (罕见, 前文已证正常市场基本不存在), 则期望 PnL > 0, 但实际暴露是 `x + y` notional, 而风险是 max(x, y) (因为结算互斥), 看似"翻倍机会"实为**风险被 cap 但成本翻倍**。

**结论: 同 condition 同时多 YES + 多 NO 不是翻倍机会, 是降 edge 或浪费资本的行为。per-condition exposure cap 必须把两边合并计。**

---

## 2. Kelly 选边公式

### 2.1 买 YES 的 Kelly (已有, ADR-042 §1.1)

```
edge_YES = fair_YES - ask_YES
f*_YES   = edge_ci_lower_YES / (1 - ask_YES)     # buy YES
```

条件: `fair_YES > ask_YES` (以 CI 下界净 edge 为准, ADR-042 §1.2)。

### 2.2 买 NO 的 Kelly (新增, 对称)

买 NO token = 买入 NO outcome share, 成本 = `ask_NO` ∈ (0,1), 结算 NO 时赢得 1。
Kelly 公式与买 YES 完全对称, 把 `fair_YES → fair_NO = 1 - fair_YES`, `ask_YES → ask_NO`:

```
fair_NO          = 1 - fair_YES
edge_NO          = fair_NO - ask_NO = (1 - fair_YES) - ask_NO

edge_ci_lower_NO = edge_NO - z * sigma(fair_NO, n_eff)
                   其中 sigma = sqrt(fair_NO*(1-fair_NO)/n_eff)

net_ci_edge_NO   = edge_ci_lower_NO - kSportsTakerFeeRate * fair_NO * (1 - fair_NO)

f*_NO            = net_ci_edge_NO / (1 - ask_NO)     # buy NO

Kelly 赔率 b_NO  = (1 - ask_NO) / ask_NO
```

**买 NO 时 Kelly 的 b (赔率) = `(1 - ask_NO) / ask_NO`, edge = `fair_NO - ask_NO`。**

> 注: ADR-042 §1.1 统一写法中 `if net_raw_edge < 0: f*_full = (-raw_edge) / c` 即为买 NO 方向 (原写法从 YES 视角看是 p < c)。本 doc 显式用 NO side 变量重写, 语义一致。

### 2.3 选边 = 选 Kelly-optimal 那边

选边准则: 选使 **geometric growth 最大** 的一边, 即选 `f*` 更大 (或在相同 bankroll 下 `suggested_notional` 更大) 那边。

```
if net_ci_edge_YES > 0 and net_ci_edge_NO > 0:
    # 双边理论上都有 edge (正常市场极罕见, 前文已证)
    # 选 f*_full 更大那边 (Kelly-optimal)
    选 argmax(f*_YES, f*_NO)

elif net_ci_edge_YES > 0 and net_ci_edge_NO <= 0:
    选 YES

elif net_ci_edge_NO > 0 and net_ci_edge_YES <= 0:
    选 NO

else:  # 双边 edge 均 <= 0
    不下单 (无 edge)
```

**选边 = 选 Kelly-optimal 那边, 对。这是 geometric growth 最大化的唯一正确判据。**

等价地: 选边等于选「市场价偏离方向与 fair 预测一致的那一边」。如果 fair_YES = 0.60, ask_YES = 0.58, ask_NO = 0.45, 则:
- YES edge = 0.60 - 0.58 = 0.02 (正)
- NO edge = (1-0.60) - 0.45 = -0.05 (负)
- 结论: 选 YES。

---

## 3. 同 condition 两边都下单? 还是只下一边?

### 3.1 结论: 每 condition 每 tick 只选一边下单

**原则: 互斥选边, 禁止同 condition 同时持 YES 多 + NO 多。**

理由 (三条):

**理由一: Kelly 已经内含互斥选边。**
Kelly 对 geometric growth 的最优化只针对单一方向。两边同时下注违背 Kelly 框架的假设 (单次押注互斥结果)。如果双边都有 edge (理论可能), Kelly 的正确处理是把两笔视为同一赌局的双向投注, 合并后 net 头寸 = `(YES share) - (NO share)`, 等效于净持有更少的头寸, Kelly 自然给出比单边更小的仓位。**实际效果 = 不如直接选 edge 更大那边单边下注。**

**理由二: 对冲降 edge 而非降风险。**
YES 多 + NO 多不是对冲, 是两笔方向相反的买入赌注 (两边都是 taker, 都付手续费)。在 Polymarket 体育:
- taker fee = 3% 双向 → 持双边必须两笔都胜才打平手续费, 极难
- 结算互斥 → 实质上两边中必有一笔亏损 (输掉本金), 另一笔赢 → 净期望 = edge_YES + edge_NO 减双倍 fee
- 如 edge_YES > 0, edge_NO < 0: 持双边期望 PnL < 单买 YES

**理由三: exposure cap 语义清晰优先。**
per-condition exposure cap 的风险学含义 = 单一结果不确定性带来的最大名义暴露。
两边合并后: 结算 YES 时最大亏损 = NO 仓位本金; 结算 NO 时最大亏损 = YES 仓位本金。
**风险敞口 = max(YES_notional, NO_notional), 但资本占用 = YES_notional + NO_notional**。
单边选 Kelly-optimal 边, 风险/收益比更优。

### 3.2 per-condition exposure cap 两边合并语义 (与老韩对齐)

ADR-042 §2 的 Cap 3 (CONDITION_CAP) 当前定义:

```
headroom_condition = max(0, market_exposure_cap_usdc - current_condition_exposure)
```

`current_condition_exposure` 在 `position_ledger_.get_per_condition_exposure()` 中按 condition_id 聚合。
当前账本只记 YES token, 但 PositionLedger 设计是 token-agnostic (`apply_fill` 接受任意 token_id)。

**对齐点 (需向老韩确认):** `current_condition_exposure` 必须 = 该 condition 下所有 token (YES + NO) 的 notional 之和。即:

```
cond_exposure[condition_id] = sum(token_exposure[t] for t in tokens_of_condition)
                             = yes_notional + no_notional
```

这个语义在 `risk_gateway.cpp` 的 `set_condition_exposure` + `FeedRiskGateway` 中需确认是按 condition 聚合两边, 而非 per-token 独立。**如果老韩的 cap 语义不含 NO side, 双边选边后 exposure cap 会算错 → 须接口对齐。**

---

## 4. edge_ci / fee 选边后一致性

### 4.1 选边不破 P0-6 / edge_ci 既有契约

当前 `paper_loop.cpp` TickOne 的 edge/fair/price 全部基于 YES side:

```cpp
const double edge_ci_lower = ComputeEdgeCiLower(p_fair, p_market_devig, cfg_.n_effective, cfg_.z_90);
sz_in.price = best_ask;      // YES ask
sz_in.edge_bps = std::abs(p_fair - p_market_devig) * 10'000.0;
sz_in.buy_yes = (p_fair > best_ask);
```

选边扩展后必须遵守:

**规则 A: 所有 edge/CI/fee/price 计算全部使用所选边的变量。**

```
选边 = YES:
    price_selected = ask_YES
    fair_selected  = fair_YES
    edge_ci_lower  = ComputeEdgeCiLower(fair_YES, devig_YES, n_eff, z)
    fee_per_unit   = kSportsTakerFeeRate * fair_YES * (1 - fair_YES)
    net_ci_edge    = edge_ci_lower - fee_per_unit

选边 = NO:
    price_selected = ask_NO
    fair_selected  = fair_NO = 1 - fair_YES
    edge_ci_lower  = ComputeEdgeCiLower(fair_NO, devig_NO, n_eff, z)
    fee_per_unit   = kSportsTakerFeeRate * fair_NO * (1 - fair_NO)
    net_ci_edge    = edge_ci_lower - fee_per_unit
```

注: `devig_NO` 需要从 NO book 的 microprice 做 de-vig。P1-8 已在 TickOne 读取 `no_token_mid`, 但仅用于 YES side de-vig 的"对边 mid"参数。**扩展后需对 NO side 独立执行一次 devig_binary。**

**规则 B: SizingInput.buy_yes 字段需扩展为 SizingInput.side (Buy YES / Buy NO)。**

当前 ADR-042 §4 `SizingInput` 有 `buy_yes: bool` (隐含在 `sz_in.buy_yes = (p_fair > best_ask)` 判断中)。选边逻辑成熟后此字段需显式化, 否则 sizing 方向与 RM `outcome = strategy::Outcome::Yes/No` 不一致会产生 intent 错误。

**规则 C: QuoteSnapshot 发布用所选边的 edge/fair/price, 不是固定 YES side。**

当前 `PublishQuoteSnapshot` 发布的是 YES side 的 edge/kelly/notional。选边后需把 quote 的语义改为"该 condition 当前最优边的 quote", 并加字段标识选了哪边。

**规则 D: 选边不影响 has_real_fair gate 和 advisory gate。**

这两个 gate 是 condition 级别的 (有无真实 Goalserve fair, 是否 advisory 市场), 与选边无关。两道 gate 仍然在选边判断之前执行, 不需要改。

---

## 5. M1 scope 仲裁 (关键)

### 5.1 老板原则的两层含义

老板原话: 「进入决策时, 我们一定要带入这个盘口的信息, 而不仅仅是单单一边的信息。」

解析两层:

**层一 (信息层): 决策时读取双边 book 数据用于计算 fair/de-vig。**
这在当前 TickOne 中已经部分实现 (P1-8: 读 `no_token_mid` 用于 `devig_binary`)。但目前 NO side 的 fair/edge 没有被独立计算。**完整实现 = 对两边分别计算 fair + edge_ci + Kelly, 再选边。**

**层二 (执行层): 允许实际买入被选的那一边 (可能是 NO)。**
当前只允许买 YES。如果选边结果是 NO, 需要构造 `intent.outcome = strategy::Outcome::No` + `intent.token_id = token1` 的下单意图。

### 5.2 M1 必做: 双边信息用于决策

**结论: 「双边信息进决策 (层一)」是 M1 必做项, 与老板原则强绑定。**

理由: 如果 fair/de-vig 只用 YES side 的 microprice, 则 de-vig 精度有损 (双边中性才最准, 见 ADR-042 §1 和 `pricing::devig_binary` 实现逻辑)。当前 P1-8 已经读了 `no_token_mid` 作为 de-vig 输入参数, 但 NO side 的 edge 没有独立计算。

**M1 最小实现:**

```
TickOne 扩展:
  1. 读 YES book: ask_YES, fair_YES, devig_YES, edge_ci_YES, f*_YES  (已有)
  2. 读 NO book:  ask_NO, fair_NO = 1 - fair_YES, devig_NO, edge_ci_NO, f*_NO  (新增)
  3. 选边: max(f*_YES, f*_NO), 无 edge 则不下单  (新增)
  4. 用选边的 price/fair/edge 构造 intent  (扩展)
```

步骤 1-3 是纯计算 (只读两个 hub 快照), 无结构性风险。这是老板「决策带双边信息」的直接实现。

### 5.3 M1 可做: 主动选边买 NO

**结论: 「主动选边买 NO (层二)」是 M1 可做但有前置条件的项, 不能无条件放行。**

技术条件 (必须全绿才放行):

**C1: RM token-agnostic 已确认。**
风控 `OrderIntent.outcome = strategy::Outcome::No` + `intent.token_id = token1` 路径已有代码 (old韩 v0.3+ 已 token-agnostic), 但 paper_loop 从未真正走过 NO intent。需要老韩确认风控 cap 在 NO side 的语义 (§3.2 对齐)。

**C2: PositionLedger NO side apply_fill 已验证。**
`position_ledger_.apply_fill(condition_id, token1, strategy::Outcome::No, fill)` 路径需要单测覆盖。当前 paper loop 只 apply YES fill。

**C3: MtM / DD 在 NO side 语义正确。**
`FeedRiskGateway` 中 `pnl_pusd` 用 `hub_.Read(pv.token_id)` 取 best_bid mark。NO token 的 best_bid mark 语义 = NO share 当前清算价, `PnL = (best_bid_NO - avg_entry_NO) * qty_NO`。这与 YES side 完全对称。A5 spec (老韩) 已是 token-agnostic, 理论上已覆盖, 需实测确认。

**C4: per-condition exposure cap 两边合并 (§3.2)。**
确保买 NO 后 cond_exposure 正确累加, 不绕过 cap。

**C5: 老韩 D4 checklist 扩展版 — 加 NO side 路径。**
M1-D4 原 8 条 checklist (2026-05-30-m1-route-review.md §4 D4) 针对 YES side 设计。买 NO 需补充:
- intent.outcome=No + token_id=token1 下 RM evaluate() 不误判
- paper fill 写 paper account NO token 不污染 YES ledger
- NO side audit emit 正确标注 outcome=No

### 5.4 M1 scope 最终建议 (小梁 Sharpe/Kelly 主权拍板)

| 项目 | M1 建议 | 理由 |
|---|---|---|
| 双边 fair/edge 计算 (层一) | **必做** | 老板原则直接要求; 无结构风险; 纯计算扩展 |
| 选边逻辑 (max Kelly) | **必做** | 是「双边信息进决策」的自然延伸; 即使选边结果仍是 YES, 逻辑完整 |
| 主动买 NO intent (层二) | **有条件做, 老韩 checklist C1-C5 绿后放行** | 风险路径需验证; 但不是遥远 M2, 条件清晰可快速绿 |
| NO side 同时持 YES 多 | **禁止** | 降 edge + 双重 fee, Kelly 框架下无意义 |

**给 GM 的建议:** 先落「双边计算 + 选边逻辑」, 这是 0 风险的计算扩展, 完全覆盖老板原则。买 NO intent 的放行由老韩 checklist 驱动, 预计不需要等 M2, M1 内就能绿。

---

## 6. 与 A5 DD / exposure 的交互

### 6.1 买 NO 后 MtM 语义

A5 DD spec (老韩, `laohan-a5-dd-feed-spec-v1.md`) 定义:

```
daily_pnl = sum over positions: (best_bid_mark - avg_entry) * qty - cum_fee
```

NO side: `best_bid_NO` 是 NO token 的买方最优报价, 即 NO token 的即时清算价。
若我方持有 NO share, MtM = `(best_bid_NO - avg_entry_NO) * qty_NO`。

**语义: 完全正确, token-agnostic。** `FeedRiskGateway` 中 `hub_.Read(pv.token_id)` 对 NO token_id 读取 NO book 的 best_bid, 与 YES side 等价计算。**A5 DD 无需修改。**

### 6.2 per-token / per-condition exposure

per-token cap (Cap 2 in ADR-042 §2):
- YES token 敞口: `tok_exp[token0] = yes_notional_micro`
- NO token 敞口: `tok_exp[token1] = no_notional_micro`

per-condition cap (Cap 3):
- `cond_exp[condition_id]` 须 = `tok_exp[token0] + tok_exp[token1]`

**现状确认:** `PositionLedger.get_per_condition_exposure()` 和 `get_per_outcome_exposure()` 的实现需要确认 condition 聚合是否包含 NO token。`apply_fill` 写入 `size_usdc` 时用的是 `token_id` 维度 (per-outcome), condition 聚合是 `condition_id` 维度。如果 NO fill 用 `condition_id` 同 key 写入, 则聚合自动包含两边。**这是需要向老周确认的架构问题 (见 §7 接口对齐清单)。**

### 6.3 无新单位/红线坑

- NO token `ask_NO` 同为 pUSD 价格 ∈ (0,1), 与 YES 完全对称, 无单位问题。
- `size_pUSD_micro = notional_usdc * 1e6` 在 NO side 同公式, 无变化。
- MicroPUSD 单位契约不受影响 (token-agnostic, 量的单位只有 micro pUSD, 与 token 侧无关)。
- R-20 四时间戳: NO book 快照的 4ts 从 `hub_.Read(token1)` 透传, 与 YES 完全对称, 不引入新来源。

**结论: 买 NO 不引入新的单位/红线坑。**

---

## 7. 接口对齐清单 (与老周/小袁/老韩的对齐需求)

### 7.1 与老周 (架构) 的对齐

| 问题 | 需确认内容 | 优先级 |
|---|---|---|
| **PositionLedger condition 聚合语义** | `get_per_condition_exposure()` 是否对同一 condition_id 下的所有 token (YES + NO) 做 sum? 若否, Cap 3 会漏计 NO 敞口 | P0, 选边 M1 前必确认 |
| **TickOne 接口扩展** | TickOne 当前签名: `(condition_id, token_id, feat, no_token_mid)`。扩展为双边计算需传入 NO book features 完整结构 (`OrderBookFeatures`), 不只是 `no_token_mid` | M1 实现前确认 |
| **双边快照读取** | 老周并行设计的 `BinaryMarketBookView` 是否提供双边 OrderBookFeatures (含 ask_NO, microprice_NO, best_bid_NO, etc.)? 若只有 cross_spread 不够用 | M1 实现前确认 |

### 7.2 与小袁 (微观结构) 的对齐

| 问题 | 需确认内容 | 优先级 |
|---|---|---|
| **SizingInput.buy_yes 字段** | 需扩展为显式 side 字段 (或 `outcome: YES/NO`), 否则 sizing 无法正确传递选边结果给 RM intent 构造 | M1 选边实现前 |
| **NO side de-vig** | `devig_binary(yes_mid, no_mid)` 当前返回去 overround 后的 YES 概率。NO side 的 devig 是否直接 = `1 - devig_binary(yes_mid, no_mid)`, 还是需要独立调用? 语义须明确 | M1 实现前 |
| **per-book slippage** | NO book 的 slippage_bps 应从 NO book L1 depth 独立估算 (小袁 §3.3 gameday 数据: YES/NO L1 depth 不对称时 slippage 不同)。SizingInput 需支持传入所选边的 slippage_bps | M1 sizing 时 |

### 7.3 与老韩 (风控) 的对齐

| 问题 | 需确认内容 | 优先级 |
|---|---|---|
| **per-condition cap 两边合并** | §3.2 中确认: `current_condition_exposure` 必须包含 YES + NO 两边 notional 之和。若当前 RM 侧 `check_position_caps_` 只看 YES token 的 condition, 需修正 | P0, 选边放行前必绿 |
| **NO intent 路径 RM evaluate 验证** | `intent.outcome = No, intent.token_id = token1` 通过 RM 的全链路 (signal/liquidity/caps/state 各 check) 需有专项单测覆盖。当前测试仅覆盖 YES intent | 老韩 D4 checklist C1 |
| **audit emit outcome 字段** | `reject/approve` audit log 中 `outcome` 字段在 NO intent 时是否正确打印 No (非默认 Yes)? | 老韩 D4 checklist 扩展 |

---

## 8. 小结

老板原则「决策带双边信息」在策略层的正确实现是:

1. **读双边 book** (已有基础, 需完善为完整 OrderBookFeatures)
2. **双边独立计算 fair/edge_ci/Kelly** (当前缺 NO side 独立计算)
3. **Kelly 选边, 每 condition 每 tick 只选一边** (Kelly-optimal, 禁双边同时持多)
4. **用选边的 price/fair/edge 构造 intent** (选边后全链路一致)
5. **per-condition cap 两边合并** (与老韩 exposure 语义对齐)

这不是「翻倍机会」, 是让系统不因 YES 端无 edge 就盲目放弃整个盘口 — 有时 NO 端才是 edge 所在。买 NO 与买 YES 在代码/风控/sizing 层面完全对称, 风控已 token-agnostic, 主要是接口对齐和测试覆盖的问题。

M1 路径清晰: 先落双边计算 + 选边逻辑 (层一, 纯计算), 再绿老韩 checklist 后放行买 NO intent (层二, 有条件)。不需要等 M2。

---

**最后更新:** 2026-05-31 by 小梁 (策略仲裁 v1, 供 GM 老雷拍板)
