# 盈利方案 — 金融本质 / 双边仓位 / 风险 sizing / 资源盘点 / 可行性门槛 (v1)

owner: 小梁 (量化研究部主管, 金融理论顾问)
last_review: 2026-06-03
status: 设计前提文档 (老板 2026-06-03 战略转向「预测未来价 + 双边仓位 + 套利」), 供盈利方案设计会
关联代码 SSOT:
- sizing 结算 Kelly: `include/stcpp/sizing/sizing_calculator.hpp` + `src/stcpp/sizing/sizing_calculator.cpp`
- 套利连续 Kelly: `include/stcpp/risk/arb_sizing.hpp`
- 套利风控门 + 强平台账: `include/stcpp/risk/arb_risk.hpp`
- 套利信号决策核: `include/stcpp/risk/arb_signal.hpp`
- 预测标签管道: `include/stcpp/ml/mid_return_label.hpp` (Δt=2..60s 墙钟 + 事件钟)
- 预测模型接口: `include/stcpp/ml/seq_arb_model.hpp`
- edge CI 下界: `include/stcpp/strategy/edge_ci.hpp`
- 组合度量 (Sharpe/maxDD/VaR): `include/stcpp/eval/portfolio_metrics.hpp`
- CLV 尺子: `include/stcpp/eval/clv_tracker.hpp`
- 账户净值双口径: `include/stcpp/paper/paper_loop.hpp` (`AccountEquitySnapshot`)
- RiskConfig caps: `include/stcpp/risk/risk_gateway.hpp` (struct RiskConfig L305)

> **结论先行 (数字说话):**
> 1. 这套机制赚的是 **(a) 收敛套利**，不是做市价差捕获、也不是纯方向性持仓。edge 来源 = PM 散户价滞后 bet365 sharp 共识 ~12s 的**信息时滞**，不是承担库存风险的补偿。把它当方向性赌涨跌会高估精度依赖、低估 fee 出血。
> 2. **二元市场 YES+NO=1 的恒等式让"双边仓位管理"在经济上等价于单边方向仓**（buy YES @ a ≡ sell NO @ 1−a）。真正的双边价值不在"对冲锁定"（PM 上 YES/NO 是同一标的的两面，对冲掉就没敞口也没 PnL），而在 **进/出两条腿的执行路径选择**（哪边 book 深、哪边 fee 划算）。需 polymarket-protocol-expert 确认 mint/merge (split/combine) 机制是否提供第三条无风险腿。
> 3. **可行性门槛 (体育 fee=0.03, taker 往返)：** 在中价 p≈0.5，往返净成本 ≈ **2.5% (250 bps) 的价格移动**才到盈亏平衡（fee 1.5% + 半 spread 假设 1% + 滑点）。已证实的 12s 时滞错价幅度据现有备忘录约 **2–5%**，**勉强为正但安全边际薄** → 必须靠"在极价区做、把一条腿走 maker、严选大 move 事件"把门槛压到 ~1.2–1.6%。
> 4. **资源盘点：** 套利专用的 Kelly (`f*=edge/σ²`)、风控门 (RJ-ARB-1..5 含延迟红线)、强平台账 (OpenLegLedger)、预测标签管道 (Δt 多窗)、CLV 尺子、Sharpe/maxDD/VaR 度量 **全部已落地**。缺的是：① 真 ONNX 预测模型 (现 stub 恒不发单)；② σ_Δmid 的 conformal 校准；③ **库存/敞口的组合层 VaR 还没接到套利腿**；④ 半 spread / 滑点的实测分布（现 BE 门用假设值）。

---

## 1. 盈利机制的金融本质：候选机制排序

把"预测未来价 + 双边仓位 + 套利"拆成三种**经济上不同**的赚钱方式，按本策略的适配度排序。

### 排序结论

| 排名 | 机制 | edge 来源 | 期望 (单位:价格) | 主风险 | 对预测精度依赖 | 本策略适配 |
|---|---|---|---|---|---|---|
| **★1** | **(a) 收敛套利** | 信息时滞 (PM 滞后 sharp ~12s) | E[Δmid_收敛] − 往返成本 | 收敛不发生/反向 (信号错)、窗口被延迟吃掉 | **中** (要方向对 + 幅度够本，不要精确点估计) | **最适配** |
| 2 | **(c) 方向性持仓** | 同 (a)，但裸持有到更远 horizon | E[Δp] − 单边成本 | 全程市场风险、γ 暴露、γ 反转 | **高** (赌的是 drift 的均值，噪声直接吃 PnL) | 退化版，精度不够时变这个 = 危险 |
| 3 | **(b) 做市价差捕获** | 提供流动性的补偿 (赚 spread) | 半 spread × 成交率 − 逆选择损失 | 逆选择 (informed flow 专挑你)、库存累积 | 低 (不靠预测，靠报价管理) | **不适配**（见下） |

### (a) 收敛套利 — 这是真正的 edge，本策略核心

**机制：** 进球/得分事件发生 → bet365 sharp 盘口在 ~毫秒–秒级重定价 → PM 散户盘口滞后 ~12s 才跟上。这 12s 窗口里 PM 的 mid 与"未来 mid（即将收敛到的 sharp 共识价）"之间存在系统性错价。我们预测 `Δmid = mid(t+Δ) − mid(t)`，在错价方向**吃单建仓**，等 PM 收敛后**平仓**兑现。

- **edge 来源：** 纯**信息优势 / 速度优势**，不是承担风险的补偿。这是关键 —— 它是一个**统计套利 (stat-arb)**，不是真无风险套利（收敛可能不发生），但 edge 的方向性由外部 sharp 共识"钉住"，比纯预测稳。
- **期望：** `E[PnL/share] = E[Δmid_收敛方向] − fee_roundtrip − 半spread − E[滑点]`。代码里就是 `arb_signal.hpp:70` 的 `be = fee_roundtrip + slip_est + (best_ask − best_bid)`，净 edge = `cons_move − be`。
- **风险：** ① 收敛不发生或反向（预测错）→ 裸方向亏损；② **窗口被延迟吃掉**（跨洋 RTT + 成交确认 > horizon 的一半 → `RJ-ARB-5 StalePrediction`，已 enforce）；③ 进得去出不来（出口深度不足 → `RJ-ARB-2`）。
- **对预测精度依赖：中。** 不需要精确点估计 mid 落点，只需要：(i) **方向对**（CI 下界同号，`RJ-ARB-1`）；(ii) **幅度够本**（`cons_move > be`）。这是个**符号 + 量级**问题，不是回归精度问题 → 比纯预测宽容得多。这也是为什么用 **CI 下界**而非点估计 sizing（`arb_sizing.hpp` 设计哲学：「微利高频靠点估计必死」）。

### (c) 方向性持仓 — (a) 的退化版，精度不够就掉进这个坑

如果预测的 horizon 拉长、或收敛锚（sharp 共识）不稳，(a) 就退化成 (c)：你只是在赌 mid 的无条件 drift。此时 edge 不再被外部共识钉住，**噪声直接进 PnL**，对预测精度要求陡升（要预测均值方向且幅度 > 成本，而 Δmid 的信噪比在长 horizon 上很低）。

**纪律：** 用 `OpenLegLedger` 的 deadline 强平（`arb_risk.hpp:113 SweepExpired`）把每条腿钉死在套利 horizon 内，**禁止让收敛套利腿"漂"成方向性持仓**。这是 (a) 不退化成 (c) 的结构保证。

### (b) 做市价差捕获 — 不适配，明确排除

PM CLOB 上挂双边 maker 单赚 spread 听起来诱人，但：
1. **逆选择致命。** 我们的 edge 是"比 PM 散户快 12s"。如果我们挂 maker 单，就成了**被快钱吃的那一方**——别的快参与者（或我们自己的 informed 判断反向时）专挑我们的挂单成交。做市要求你在信息上**不劣于**对手流，而我们的整个论点是"市场有 informed 时滞"，这与做市的盈利前提矛盾。
2. **库存风险与策略目标冲突。** 做市赚 spread 的代价是被动累积库存；而我们要的是**主动按预测方向建仓**。两者的仓位逻辑相反。
3. **maker 可作为执行优化，不作为独立 alpha。** 在收敛套利里，**出场腿**或**非时间敏感腿**可以走 maker 省 fee（Polymarket maker fee 通常低于 taker，需 polymarket-protocol-expert 确认当前 feeSchedule maker/taker 是否分档），但这是降成本，不是 (b) 那种"靠 spread 本身盈利"。

> **一句话定性：** 我们赚的是 **(a) 信息时滞收敛套利**。(c) 是它失控的危险态（用强平钉死防退化），(b) 与我们的 edge 前提自相矛盾（排除，仅作 (a) 的出场降本手段）。

---

## 2. 双边仓位管理的数学：二元市场 YES+NO=1

### 2.1 恒等式与"双边"的真实含义

Polymarket 二元市场：YES + NO = 1（结算时一个值 1 一个值 0）。设 YES 价 = a，NO 价 = b。无套利下 a + b = 1（实际有 spread/fee，a + b 可能略 ≠ 1）。

**核心恒等式（决定整个仓位代数）：**

```
持 YES 多头 @ 价 a  ≡  持 NO 空头 @ 价 (1−a)
持 NO  多头 @ 价 b  ≡  持 YES 空头 @ 价 (1−b)
```

含义：在一个 condition 内，**YES 和 NO 不是两个独立标的，是同一个标的的两面**。所以：

| 操作组合 | 净敞口 | PnL 来源 | 说明 |
|---|---|---|---|
| 持 YES 多 + 持 NO 多 (各 1 share) | **0** (对冲死) | 锁定 `1 − (a+b)`，一次性 | 若 a+b < 1，这是**真无风险锁差**（mint/merge 套利，见下）；否则是 0 PnL + 双份 fee = 纯亏 |
| 持 YES 多 only | +1 YES | 赌 mid 上行收敛 | 这就是收敛套利的标准腿 |
| 持 YES 多 + 持 NO 空 | **+2 YES** 等价 | 加杠杆方向 | 因为 NO 空 ≡ YES 多，叠加放大敞口 |

**结论：在单个二元市场内做"YES 多 + NO 多"去对冲，等于把敞口冲销成 0** —— 既没风险也没 PnL，还付双份 fee。所以**收敛套利的"双边仓位管理"不是在同一 condition 内对冲**，而是：

1. **进/出两条腿的边选择**（buy YES 还是 buy NO 来表达同一方向，取决于哪边 ask 更便宜、哪边 book 更深）—— `arb_signal.hpp:122` 已实现：`entry_px = is_long ? best_ask : best_bid`。
2. **真套利来自 a+b ≠ 1 的瞬间**（PM 内部 YES ask + NO ask < 1）：同时 buy YES + buy NO，锁定 `1 − (YES_ask + NO_ask) − fee` 的无风险利润。**这才是"双边仓位 = 套利锁定"的正确数学**。但这需要 mint/merge 或两边都有可吃单 → **需 polymarket-protocol-expert 确认 PM 是否支持 split/merge（USDC ↔ YES+NO 1:1）以及 gas/fee**。

### 2.2 用预测的未来价驱动目标仓位

设预测 `Δmid`（signed），CI 下界 `ci_low`、上界 `ci_high`，置信 `conf`。目标仓位逻辑（已在 `arb_signal.hpp`）：

```
方向 dir   = sign(Δmid)                          # 多头 Δmid>0；空头 Δmid<0
保守幅度    = dir>0 ? ci_low : −ci_high            # CI 下界方向 (favorable 的保守端)
净 edge     = 保守幅度 − BE                         # BE = fee_roundtrip + slip + spread
目标边      = dir>0 ? buy_YES@ask : buy_NO@bid     # 用便宜/深的那一面表达方向
目标 notional = ComputeArbSizing(edge, σ, bankroll, λ, caps)
```

- **净敞口 = 目标 notional 的单边方向**（不在 condition 内对冲，对冲=自杀）。
- **套利锁定**只在 a+b<1 的窗口里通过双 buy 实现（独立于预测的无风险腿）。
- **库存风险**：每条腿背"horizon 内必平"义务（`OpenLegLedger`，到 deadline 强平）。库存不是被动累积的（那是做市），是主动建的、有时限的。库存的真实风险是**到期时 book 深度不足以平仓**（`RJ-ARB-2` 进场前就挡，出场时 `SweepExpired` 兜底强平）。

### 2.3 一句话给量化研究做体育适配

「双边仓位管理」在金融上= **(i) 同一方向选最优执行边（YES/NO 哪面便宜深）+ (ii) 仅在 a+b<1 瞬间用双买锁无风险差**。**不是**在 condition 内 YES 多 NO 多去"对冲锁利"——那在 YES+NO=1 的恒等式下是零敞口 + 双 fee = 负期望。体育适配要把"双边"理解成执行层而非对冲层。

---

## 3. 风险与 sizing

### 3.1 Kelly：两套物理分开的公式（不可混用）

代码里已经**正确地物理分开**了两种 Kelly（`arb_sizing.hpp` 注释明确）：

**(A) 结算 Kelly（赌 0/1 赔付）— `sizing_calculator.cpp`：**
```
f*_full = net_ci_edge / (1 − c)     (buy YES, 赢赔 $1)   [L155]
net_ci_edge = edge_ci_lower − fee_rate·c·(1−c)            [compute_net_ci_edge]
f_fractional = λ · f*_full,   λ = kLambdaBase = 0.35      [L168, sizing_calculator.hpp L140]
```
这是赌**结算结果**的。**新策略不该用这个**——我们不赌结算，赌 Δmid。

**(B) 套利连续 Kelly（赌 mid 移动）— `arb_sizing.hpp`：**
```
f* = λ · edge_arb / σ²_Δmid          [L51]
edge_arb = E[Δmid] − fee_roundtrip − E[滑点]  (用 CI 下界算)
σ_Δmid  = (ci_high − ci_low) / (2·z),  z(80%)=1.2816    [arb_signal.hpp:80]
λ = 0.10 起 (套利微利, 估计误差占比高)                    [arb_sizing.hpp L22]
clamp f* ∈ [0,1], 再过 per-order cap + exit_depth cap   [L52-56]
```

> **连续 Kelly 推导（备会上有人问）：** 把单笔套利的 log-wealth 增长对仓位 f 求极值，赌注是均值 μ=edge、方差 σ² 的近似高斯收益（小幅 Δmid），一阶得 `f* = μ/σ²`。这是 Merton 连续时间 / 高斯 Kelly 的标准结果，与结算 Kelly 的 `edge/赔率` 是两个世界。**严禁把 (A)(B) 互换或 blend。**

### 3.2 分数 Kelly λ 的理论依据 + 本策略建议值

| 场景 | λ | 理由 |
|---|---|---|
| 结算 Kelly (`sizing_calculator`) | 0.35 | 老板 2026-06-01 定 (原 0.25 quarter)；paper 灰度看实测 maxDD |
| **套利连续 Kelly (新策略)** | **0.10 起** | 套利是微利高频，σ_Δmid 估计误差大；λ=0.10 把"估 σ 偏小 2x"造成的过押风险压住。**稳定后（σ 校准误差实测 < 20%）可升 0.15–0.25，不建议越 0.25** |

**为什么套利 λ 必须更小（数字）：** 分数 Kelly 的几何增长率 ≈ `λ(2−λ)·G_full`，λ=0.10 拿满 Kelly 增长的 19%，但**方差只有 λ²=1% 的全 Kelly 方差**。微利套利的致命点是估计误差——如果真 σ 是估计 σ 的 1.5x，full Kelly 会过押 2.25x，几何增长率转负。λ=0.10 给 10x 安全垫。这就是 `arb_sizing.hpp` 注释「微利高频靠点估计必死」的量化版。

### 3.3 稳健 sizing：预测置信 → 仓位（噪声下的收缩）

预测有噪声时，**三重收缩**（前两重已落地）：

1. **CI 下界代替点估计**（已落地）：`edge_arb` 用 `cons_move = ci_low`（多头）而非 `dmid`。点估计 0.04 的 edge，若 CI 下界只有 0.01，按 0.01 sizing。这是对 σ 的**自动惩罚**——CI 越宽，可用 edge 越小，仓位越小。
2. **σ² 在分母**（已落地）：`f* = λ·edge/σ²`。预测越不确定（σ 大），仓位越小，二次衰减。
3. **conf 进信号质量排序**（已落地，`arb_signal.hpp:113`）：`quality = conf · |edge| · depth`，多 horizon 里选 conf 高的。**建议增强：把 conf 也乘进 λ** → `λ_eff = λ_base · conf`，让校准置信度直接缩 sizing（现仅用于排序不用于缩仓 → §3.6 follow-up）。

### 3.4 VaR / CVaR

`portfolio_metrics.hpp` 已实现历史 VaR_95 / VaR_99（从权益曲线单期收益分位）。**套利策略的 VaR 特性与结算策略不同：**

- 套利单笔损失是**有界且短**的（horizon 内强平），但**高频**。VaR 该看**单期（如 1 分钟）聚合所有 open legs 的潜在损失**，不是单笔。
- **建议门槛（供老韩 RM enforce）：**
  - 单期（1 min）VaR_95 ≤ **0.5% bankroll**
  - 单期 VaR_99 ≤ **1.0% bankroll**
  - CVaR_99（尾部均值，现未实现 → §3.6 follow-up）≤ **1.5% bankroll**
- **组合层缺口（重要）：** 现 `arb_risk.hpp` 的 horizon_cap / open_leg cap 是**逐腿/逐窗**的，没有**跨 condition 的相关性 VaR**。多个 condition 同时是"进球后错价"时高度相关（同一信息冲击）→ 简单求和低估真实 VaR。**需补组合层 VaR（见 §4 缺口）。**

### 3.5 最大回撤 + 敞口上限（建议阈值表，供老韩签）

| 约束 | 建议值 | 现状 | enforce 点 |
|---|---|---|---|
| 单注 ≤ bankroll | **5%**（套利比结算的 10% 更严，因高频累加） | 结算用 10% (`kMaxBankrollFraction`) | 新增 arb 专用 cap |
| 单 condition 敞口 | ≤ 2000 pUSD (`per_outcome_cap`) | 已有 | RiskConfig |
| 单 condition×双向 | ≤ 5000 pUSD (`market_exposure_cap`) | 已有 | RiskConfig |
| **并发未平腿** | ≤ 20 腿（防同时多窗暴露） | `max_open_legs` 有字段，需设值 | `RJ-ARB-3` |
| **窗口累计 taker 敞口** | ≤ 10% bankroll | `horizon_cap_usdc` 有字段 | `RJ-ARB-4` |
| **最大回撤硬停** | maxDD ≥ **15%** → 全平 + 停新仓 | `portfolio_metrics` 采 maxDD，但**没接 kill-switch** | **缺口，见 §4** |
| 单日亏损 | daily_loss cap | RiskConfig 有 `daily_loss` | RM |

> **北极星对齐：** maxDD ≤ 15% 是公司北极星硬指标。`portfolio_metrics.hpp` 已采 maxDD，但**回撤触发自动停机的链路还没接**——这是真钱上线前必须补的（paper 期可先观测）。

### 3.6 sizing follow-up（量化研究做体育适配）

1. λ_eff = λ_base · conf（置信缩仓，§3.3 第 3 点）
2. CVaR_99 实现（`portfolio_metrics` 加尾部均值）
3. σ_Δmid 的 conformal 校准（现 σ 从 CI 宽度反推，假设 CI 校准良好——上线前必须用 walk-forward 验 CI coverage ≈ 80%）

---

## 4. 手上的金融资源盘点 + 缺口

### 4.1 已落地（可直接支撑新策略）

| 能力 | 载体 | 成熟度 | 对新策略的作用 |
|---|---|---|---|
| **套利连续 Kelly** `f*=λ·edge/σ²` | `arb_sizing.hpp` | ✅ 纯函数+多封顶 | 直接是 (B) 的 sizing 核 |
| **套利风控门** RJ-ARB-1..5 | `arb_risk.hpp` | ✅ 含延迟红线 RJ-ARB-5 | 方向确定性/出口深度/延迟/并发/窗口敞口全覆盖 |
| **强平台账** OpenLegLedger | `arb_risk.hpp` | ✅ deadline 强平 | 防 (a) 退化成 (c) 的结构保证 |
| **套利信号决策核** | `arb_signal.hpp` | ✅ 多 horizon 选优 | 预测→门→sizing→选 horizon 全串好 |
| **预测标签管道** Δt=2..60s + 事件钟 | `mid_return_label.hpp` | ✅ PIT 安全/禁外推 | 训练 (B) 模型的监督标签，正是"预测未来价" |
| **预测模型接口** | `seq_arb_model.hpp` | ⚠️ **仅 stub（恒不发单）** | 接口齐，**等真 ONNX** |
| **edge CI 下界（源感知）** | `edge_ci.hpp` | ✅ sharp 不扣二项噪声 | 修了 sharp 套利"一笔不成交"病根 |
| **组合度量** Sharpe/maxDD/VaR | `portfolio_metrics.hpp` | ✅ 采集 | 北极星 KPI + 风控阈值监控 |
| **CLV 尺子** | `clv_tracker.hpp` | ✅ 离线评估 | **edge 验证最快最低方差的尺子**——用它先确认 alpha 真存在 |
| **账户净值双口径** | `paper_loop.hpp AccountEquitySnapshot` | ✅ equity_bid(保守,喂Kelly) + equity_mark(展示) | bankroll 口径已对接 Kelly（best_bid 保守清算价喂分母） |
| **5-cap 链 + RiskConfig** | `sizing_calculator.cpp` + `risk_gateway.hpp` | ✅ | per-order/outcome/condition/bankroll cap |
| **fee 真值源** | gamma feeSchedule.rate (体育 0.03) | ✅ 接 RM+sizing+回测 | BE 计算用真 fee 非硬编码 |

### 4.2 缺口（按上线阻塞度排序）

| # | 缺口 | 阻塞度 | 谁补 |
|---|---|---|---|
| 1 | **真 ONNX seq_arb 模型**（现 stub 恒 ok=false 不发单） | **P0**（没它整条套利分支沉默） | ML (小邓训练) + 量化研究 |
| 2 | **σ_Δmid 的 conformal 校准**（CI coverage 验证）| **P0**（σ 没校准 → Kelly 分母错 → 过押/欠押） | 量化研究 walk-forward |
| 3 | **组合层相关性 VaR**（多 condition 同信息冲击高度相关，逐腿 cap 低估真风险） | P1 | 我 (小梁) 定模型 + 老韩 enforce |
| 4 | **maxDD → kill-switch 自动停机链路**（现只采不停） | P1（真钱前必须） | 老韩 RM |
| 5 | **半 spread / 滑点实测分布**（现 BE 门用假设值，§5 粗算靠它） | P1 | 微观结构专家 + 数据 |
| 6 | **CVaR_99 实现** | P2 | 量化研究 |
| 7 | **λ_eff = λ_base·conf 缩仓**（置信进 sizing 而非仅排序） | P2 | 量化研究 |

### 4.3 一句话

**金融骨架（Kelly/风控门/强平/度量/CLV/bankroll 口径）齐全且设计正确**——老板 6-01 那波"短时套利引擎"已经把 (B) 套利的脚手架搭好了。**真正缺的是"肉"**：真模型 + σ 校准 + 实测成本分布。在 paper 期，**先用 CLV 尺子确认 12s 时滞 alpha 真实存在**（最低方差验证），再投模型训练资源。

---

## 5. 盈利可行性判断（数字说话）：BE 门槛粗算

### 5.1 往返成本结构（taker/taker，中价 p≈0.5）

收敛套利的盈亏平衡（`arb_signal.hpp:70` 的 BE）：
```
BE = fee_roundtrip + 半spread(进) + 半spread(出) + 滑点
```

**逐项（体育 fee_rate = 0.03，gamma feeSchedule）：**

Polymarket 体育 fee 公式（项目记忆 polymarket-fee-source）：`fee_per_share = rate · p · (1−p)`（**注意是 p(1−p) 不是 p**，这是 PM 体育的特殊 fee 结构，对锁价区间友好）。

| 成分 | 中价 p=0.5 | 极价 p=0.1 或 0.9 | 说明 |
|---|---|---|---|
| fee 单边 = 0.03·p·(1−p) | 0.03·0.25 = **0.0075** (75 bps) | 0.03·0.09 = **0.0027** (27 bps) | **极价区 fee 暴跌**（p(1−p) 结构）→ 套利该往极价区做 |
| fee 往返 (×2) | **150 bps** | **54 bps** | |
| 半 spread 进 + 出 | 假设 spread=1% → **100 bps** | 极价区 spread 通常更宽 → 假设 1.5% → 150 bps | **需微观结构实测** |
| 滑点 (吃单 + 出场) | 假设 **30 bps** | 极价深度薄 → 50 bps | **需微观结构实测** |
| **BE 合计** | **≈ 280 bps (2.8%)** | **≈ 254 bps (2.5%)** | |

### 5.2 门槛判断

**需要多大"预测→实际"价格移动才正净期望？**

> **中价 p=0.5：需要 Δmid 收敛 ≥ 约 2.8%（280 bps）才到 BE，正期望要 > 3% 留安全边际。**
> **极价 p=0.1/0.9：fee 降到 54 bps，但 spread/滑点恶化 → BE ≈ 2.5%。**

**对照已证实的 alpha：** 现有备忘录称 12s 时滞错价幅度约 **2–5%**。

| 错价幅度 | 中价 BE=2.8% | 结论 |
|---|---|---|
| 2% | < BE | **亏**（成本吃光）|
| 3% | 略 > BE | 薄利，安全边际 ~0.2% |
| 5% | > BE | 有肉，净 ~2.2% |

### 5.3 这意味着什么（可落地策略约束）

BE ≈ 2.8% 在中价、错价 2–5% 的环境下 **勉强为正但边际薄**。要做成稳定正期望，**必须用以下杠杆把 BE 压到 ~1.2–1.6%**：

1. **严选大 move 事件**（只在进球/得分/红牌等会造成 ≥3% 错价的事件后开仓，过滤小波动）—— 信号质量门 `quality = conf·|edge|·depth` 已支持，提高 |edge| 阈值。
2. **出场腿走 maker** 省一半 fee + 省半 spread → BE 砍 ~75 bps（**需 polymarket-protocol-expert 确认 maker fee 是否更低 / 是否 0**）。若 maker fee≈0，往返 fee 从 150→75 bps。
3. **优先极价区做**（p<0.2 或 p>0.8）：fee 从 75→27 bps/单边。但要权衡极价区 book 薄（`RJ-ARB-2` 出口深度门会拦掉一部分）。
4. **CI 下界门已经在做安全过滤**（`net_edge = ci_low − BE > 0` 才发），把 §5.2 的"3% 才薄利"自动 enforce 成"CI 下界 > BE 才发单"。

**乐观 vs 保守净期望（单笔，名义 $1000）：**

| 情景 | 错价 | BE | 净 edge | 单笔净 PnL | 备注 |
|---|---|---|---|---|---|
| 保守（中价 taker/taker） | 3% | 2.8% | 0.2% | **$2** | 太薄，扣方差后可能为负 |
| 现实（大 move + 出场 maker） | 4% | 1.8% | 2.2% | **$22** | 可行 |
| 乐观（大 move + 极价 + maker） | 5% | 1.2% | 3.8% | **$38** | 甜区 |

> **可行性结论：** **理论上可行，但安全边际取决于能否把 BE 从 2.8% 压到 < 2%。** 关键杠杆是 **maker 出场 + 严选大 move + 极价区**。**纯中价 taker/taker 打小错价 = 慢性 fee 失血**（这正是项目记忆「why-no-trades fee 流血」教训的金融解释）。下一步该做的不是急上模型，而是**先用 CLV 尺子在 paper 实测 12s 时滞错价的真实幅度分布 + 半 spread/滑点实测**，把 §5.1 的假设值换成真值，再定 |edge| 门阈值。

---

## 6. 给设计会的三条结论

1. **机制定性：(a) 信息时滞收敛套利**。不是做市（与 edge 前提矛盾，排除），不是纯方向（是失控危险态，用强平钉死）。「双边仓位」= 执行层选最优边 + a+b<1 时双买锁无风险差，**不是 condition 内对冲**（那是零敞口+双 fee 自杀）。
2. **sizing：连续 Kelly `f*=λ·edge/σ²`，λ=0.10 起，CI 下界 + σ² 分母 + conf 三重收缩**。结算 Kelly 与套利 Kelly 物理分开，严禁 blend（代码已正确分开）。
3. **可行性：理论正，边际薄。** 中价 taker/taker BE≈2.8%，错价 2–5% → 勉强正。**靠 maker 出场 + 严选大 move + 极价区把 BE 压到 <2% 才有稳定肉**。上线前先用 CLV 尺子验 alpha 真实存在 + 实测成本，别急上模型。

---

## 附：需 polymarket-protocol-expert / 微观结构专家确认项

- [ ] **maker/taker fee 是否分档**（feeSchedule 是否对 maker 减免甚至 0）—— 直接决定 §5 能否把 BE 砍半
- [ ] **split/merge 机制**（USDC ↔ YES+NO 1:1 mint/redeem，gas/fee）—— 决定 §2.1 "a+b<1 双买锁差"是否有第三条无风险腿
- [ ] **半 spread + 滑点实测分布**（按 p 区间、按运动、按 book 深度）—— §5.1 现用假设值，是 BE 门槛的最大不确定性来源
- [ ] **极价区（p<0.2/p>0.8）实际 book 深度** —— 决定 §5.3 极价区杠杆能用多少（`RJ-ARB-2` 出口深度门会拦）
