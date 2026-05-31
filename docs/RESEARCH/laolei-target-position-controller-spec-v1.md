# 目标仓位控制器 — 实施 Spec v1 (待老韩/小梁/老周评审)

> owner: 老雷 (GM) · last_review: 2026-05-31 · 状态: 草案, 待风控/量化/架构评审后建
> 上位: docs/RESEARCH/laolei-target-position-strategy-direction-v1.md (方向北极星)
> 性质: 把"目标仓位连续控制"范式落到 paper_loop 订单路径的完整设计, 不留缺口。

---

## 0. 范式回顾 (老板 2026-05-31)

没有进/出场/止损/止盈概念。模型输出**目标双边仓位 + 价格界限**;控制器连续把当前仓位调到目标;
Kelly 协调规模;守门只在账户级保命层硬拦,信号质量层不草率守门;出场/止损是目标变小/翻转的副产品。

---

## 1. 模型输出 (决策每次重评估产出)

`DecisionOutput { fair_value, target_signed_notional, reservation_buy_px, reservation_sell_px }`

- **fair_value**: 被选边 fair prob (现 baseline 估计器; 未来 ONNX 换源, 接口不变)。
- **target_signed_notional**: 目标净仓位 (signed pUSD)。+ = 净多 YES, − = 净多 NO(= 空 YES)。
  - 规模 = Kelly: `|target| = SizingCalculator 的 Kelly-optimal notional` (受 cap/bankroll 约束, 现成)。
  - 方向 = 被低估边 (de-vig: fair_yes ≥ devig → 目标 +; < → 目标 −)。
- **reservation_buy_px / reservation_sell_px**: 价格界限 (老板「价格涨了还硬买就赔」)。
  - `reservation_buy = fair − fee_per_unit − required_margin` (买入不超过此价; 高于它无净 edge)。
  - `reservation_sell = fair + fee_per_unit + required_margin`。
  - required_margin = 待小梁定 (edge_ci_lower_floor 同源? 或独立 margin 参数)。**评审点 Q-梁-1**。

## 2. 控制器 (order = 目标 − 现仓)

```
gap = target_signed_notional − current_signed_position   // current 来自 position_ledger (已入模型 P0)
if |gap| < min_rebalance_threshold: 不动 (避免抖动; threshold 待小梁定 — 评审点 Q-梁-2)
side  = gap > 0 ? Buy : Sell
size  = |gap|  (clamp 到 per_order_cap)
price = side==Buy ? reservation_buy_px : reservation_sell_px   // 限价, 绝不 market
is_close = (|target| < |current| 且同向)  // 减仓/平仓 = 收敛, 非新开
→ 构造 OrderIntent(side, size, price=reservation, is_close) → RM.evaluate → 限价撮合
```

- **买入增持 + 卖出减持 = 现在可做** (减持是降敞口, 在现有 cap 内安全)。
- **卖出开空 (target 从 0 转负)**: 需老韩 signed condition cap (C1) 重裁。**v1 先 clamp 空头目标=0** (只做多侧目标追踪), 文档化的保守界, 非缺口。**评审点 Q-韩-1**。

## 3. 守门 (保命留, 草率门清)

**保命硬门全留 (账户级电闸; 老板「保命门留」):**
- RM.evaluate 全部门: per_order/outcome/condition/market cap、drawdown、净 edge<0(R-fee-2)、ML-R2 advisory、
  liquidity、slippage、P0-1 去重、P0-2/c5 cap 单位。**一个不动, RM 仍是订单必经边界 (不绕过红线)。**

**清掉的旧逻辑 (信号/触发范式残留):**
- 一次性 BUY 框架 (size=raw Kelly notional, side 恒 Buy, price=exec_ask) → 换成 gap-based 限价。
- `has_real_fair` **硬 suppress**(无真 fair → 清零 edge 不产 intent) → 改为**自然涌现**: 无真 fair → edge≈0 → Kelly target≈0 → gap≈现仓反向(收敛回 0)→ 控制器自动减仓。**不再硬 gate, 但仍宁可空不可假**(target 0 = 不开新仓)。**评审点 Q-韩-2: 这是否触碰 P0-3 假阳性红线?**
- 比分新鲜度 `fresh` 门: 老板「新鲜度不守门」。v1 改为**新鲜度进模型** (joint_as_of 已入), 旧 `fresh` 硬门**降级**为模型输入。**但**: 无任何比分时 fair 仍是 stub → target 自然≈0。**评审点 Q-韩-3**。

**无规则止损/止盈/出场**: 不写任何独立 exit/stop 逻辑。立硬红线: 出场路径绝不加新鲜度/信号质量 gate。
账户级极端回撤 halt (RM drawdown) 仍在 = 不是止损, 是"模型可能错别赌光"电闸。**评审点 Q-韩-4: 确认此立场。**

## 4. 执行 (限价, 防追价)

- OrderIntent.price = reservation (非 exec_ask)。VirtualMatcher 按限价撮合: 市场价优于保留价才成交,
  否则挂着不追。**评审点 Q-周-1: VirtualMatcher 现支持限价语义吗? 还是需扩?**
- 减仓/平仓单 (is_close=true) 的撮合 + 账本应用 (signed 减仓) — **评审点 Q-周-2: PositionLedger 支持 signed 减仓/反向吗?**

## 5. re-eval 触发

- v1: 维持固定 tick (现状)。每 tick 对每盘口重算 target + gap + rebalance。
- v2: 事件驱动 (book/score 更新触发)。本 spec 不含, 列 backlog。

## 6. 回测/实盘同逻辑 (BR-1)

控制器逻辑 (target/gap/reservation) 必须回测实盘共用一份 C++ — 与现有 ComputeEdgeCiLower/sizing 同源原则一致。

## 7. 分步落地 (评审通过后)

1. 输出抽象: 决策算 target_signed_notional + reservation_px, 进 QuoteFeatures (观测/训练) — 纯加性安全。
2. 控制器: gap-based 限价 intent (买增/卖减; 空头 clamp 0) — 替换一次性 BUY。RM 边界不动。
3. 清旧: has_real_fair 硬 suppress → target-0 涌现; fresh 门降输入。
4. 测试: 多/平/减仓/限价不追 各路径 + 回归 (现有 1133 不破)。
5. 账户级电闸 + 无规则止损立场 = 老韩签字后生效。

## 8. 评审点汇总

| ID | 问题 | owner |
|---|---|---|
| Q-韩-1 | signed condition cap (卖出开空) 语义 — C1 重裁 | 老韩 |
| Q-韩-2 | has_real_fair 硬 suppress → target-0 涌现, 是否触 P0-3 假阳性红线 | 老韩 |
| Q-韩-3 | 比分新鲜度门降为输入 — R-20 立场 | 老韩 |
| Q-韩-4 | 无规则止损 / 账户级电闸唯一硬边界 — 立场确认 | 老韩 |
| Q-梁-1 | reservation_px 的 required_margin 公式 | 小梁 |
| Q-梁-2 | min_rebalance_threshold (防抖) | 小梁 |
| Q-周-1 | VirtualMatcher 限价语义 | 老周 |
| Q-周-2 | PositionLedger signed 减仓/反向支持 | 老周 |

---

## 9. 评审结论 (2026-05-31, 老韩 RM主权 + 小梁 Kelly主权 + 老周 架构主权)

**总裁定: v1 (买增 + 卖减 + 限价不追, 不反向) 三主权有条件批准。1 个真红线漏洞 (H-1) 是放行硬阻塞。**

### 老韩 (RM):
- **Q-韩-1 否决 clamp-only, 批准"只做多侧"保守界。挖出真红线漏洞 H-1**: RM condition/outcome cap 是 **signed 比较** (risk_gateway.cpp:519/532), 卖出减仓产负 delta → `cur+size` 变小 → cap 永不咬 → **caps 被静默架空** (§8.1 #3 同型事故, 符号维度)。**卖减仓今天就触发, 不是 M2。**
- Q-韩-2 批准 (target-0 涌现 ≡ 不产假信号), 但 gate **非对称撤**: 撤减仓侧、**留开仓侧 stub-fair→0** (防 P0-3 假阳性原型)。
- Q-韩-3 批准降 fair `fresh` 先验门为输入; **但 RM 的 stale 60s/4ts/recon 门属保命层全留** (与 fair 先验门是两回事)。
- Q-韩-4 **签**无规则止损立场。钉死: ①DD 电闸不可被策略 override ②减仓单不绕 RM 保命门 (无止损 ≠ 减仓免风控) ③净edge<0 是保命层留。

### 小梁 (Kelly) — 给了公式:
- **Q-梁-1**: `required_margin = max(edge_ci_lower_floor, z_90 × sqrt(fair×(1−fair)/n_eff))`;
  `reservation_buy = fair − fee_per_unit(exec_ask) − required_margin`; sell 侧对称。同源现有 cfg 参数, 不引新参。
- **Q-梁-2**: `min_rebalance_threshold = max(1.0 pUSD, 0.10 × |target|)`; `|fair_new−fair_old| > 0.02` 强制穿越防抖 (比分大跳不堵)。
- Kelly 重诠释成立 (偏差方向保守); λ=0.25 理论 max_drawdown ≈ 3.1% ≪ 15% OKR。

### 老周 (架构) — 签 v1 范围:
- **抽独立 `Controller::Decide` 纯函数** (新模块, 回测/实盘共用 BR-1, 不嵌 TickOne)。gap/reservation限价门/is_close/min_rebalance 全收进去。
- **Q-周-1 限价 = 控制器前置门 (落点B)**: `best_ask ≤ reservation_buy` 才构造 intent, 否则 skip + not_marketable stat。**matcher/VirtualFill ABI 零改** (守 1119 ctest bit-identical)。
- **Q-周-2**: 账本 signed 算术已支持买增+卖减+平仓 (avg_entry 正确)。**sell 负 delta 定在 apply_fill 入口** (FillEvent.filled_size_micro 喂负, 账本一行不改)。反向(多→空) avg_entry 错 + 空仓 VWAP 缺 → **M2**; v1 clamp 空头=0 + `assert(new_size>=0)` fail-closed。
- R-12 不动 (Controller 在 loop_thread_ 纯函数)。

## 10. 放行硬条件 (build 前置)

| # | 条件 | 阻塞级 | owner |
|---|---|---|---|
| H-1 | RM condition/outcome cap signed→magnitude (减仓放行/升敞口才比 cap), **与控制器同批落** | **硬阻塞** | 老韩+老周 |
| H-2 | 反向穿零兜底拒 (M1 sign flip 跨0 → RM 拒, clamp 失效也兜住) | **硬阻塞** | 老韩 |
| H-3 | stub-fair gate 非对称撤 (撤减仓侧/留开仓侧 stub→0) | **硬阻塞** | 老韩+老雷 |
| H-4 | stub fair 路径 sizing 的 edge_ci 下界恒 ≤0 (小梁 cross-sign) | 前置 | 小梁 |
| H-5 | spec 划线: RM stale/4ts 保命门全留; 减仓单不绕 RM | 文档 | 老雷 |
| H-6 | M2 staleness→模型置信衰减 (降 fresh 门配套) | M2 验收 | 小梁/小邓 |

## 11. 落地步骤 (评审通过, 按此建)

1. **`Controller::Decide` 纯函数模块** (src/stcpp/control/) — ControlInput{target,current,reservation_buy/sell,best_ask/bid,cfg} → ControlAction{act,side,size,limit_price,is_close,no_act_reason}。含 gap/limit门/is_close/min_rebalance/空头clamp0。纯函数 + 穷举单测。
2. **H-1+H-2: RM cap magnitude + 反向穿零拒** (risk_gateway.cpp, 老韩 spec) + 测。
3. **reservation 公式** (小梁) 算进决策 → 进 QuoteFeatures (观测/训练) + 喂 Controller。
4. **wire**: TickOne 退化为 组装 ControlInput → Decide → 按 action 构造 intent(限价)/skip; sell 负 delta 定 apply_fill 入口; H-3 stub 非对称。清掉一次性 BUY。
5. **测试**: 多/平/减/限价不追/穿零拒 各路径 + 回归 1133 不破 + matcher 1119 bit-identical。
6. 反向/空仓 VWAP/sell-to-open → M2 (老韩 C1 + 老周会签)。
