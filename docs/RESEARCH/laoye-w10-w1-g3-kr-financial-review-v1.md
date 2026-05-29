# G3 KR 金融视角独立审查 v1

- **Owner:** 老叶 (F 顾问团, financial-expert)
- **Date:** 2026-05-29
- **Last review:** 2026-05-29
- **Status:** DRAFT — 待老郭 ack + 老钱 spec v2 响应 + 老雷 final
- **触发:** Wave 94 G3 KR 审, 老郭 Wave 88 顾问意见 P0, deadline 5/31
- **输入文档:**
  - `docs/OKR/laoqian-w8-w5-profitability-kr-v1.md` (老钱 G3 spec v1)
  - `docs/RESEARCH/laopeng-w9-inplay-edge-gross-net-confirm.md` (老彭 gross/net confirm)
- **ADR 约束:** ADR-029 + ADR-032 (push 后不等 CI); ADR-027 N/A (本文非数据结构 SSOT)

---

## §1 老钱 G3 KR 数字现状

老钱 spec v1 §3 G3 gate 条件全文复述如下（五条，全部满足才放行 G4）：

| KR | 当前值 | 来源 |
|---|---|---|
| 14 日累计净 PnL > 0 | 定性 | 老板 verbatim |
| Hit rate ≥ **54%** → 老彭调上 **56-58%** | 老彭 W9 confirm | 本文审查对象 |
| OOS Sharpe ≥ **0.5** | 老钱 spec v1 | 本文审查对象 |
| MDD ≤ 10% | 老钱 spec v1 | 合理，暂不议 |
| n_trades ≥ 50，14 日 paper window | 老钱 spec v1 | 本文核心 |
| t-test p < 0.10（单尾） | 老钱 spec v1 | 伴随审查 |

**老彭 gross/net 确认结论（W9 confirm §1）：**

```
net edge = gross(1.5-2.5%) - Polymarket taker fee(3%) - slippage(~0.3%)
         = -1.8% to -0.8%   ← 中位为负
```

这是本次审查最关键的输入。所有 G3 KR 门槛必须在 net edge 中位负的约束下重新校验。

---

## §2 金融统计功效审查

### 2.1 Sharpe 标准误差

在 N 个独立观测下，Sharpe ratio 的渐近标准误差（Lo 2002）为：

```
SE(SR) = sqrt((1 + 0.5 * SR^2) / N)
```

代入 SR = 0.5，N = 50：

```
SE(SR) = sqrt((1 + 0.5 * 0.25) / 50)
       = sqrt(1.125 / 50)
       = sqrt(0.0225)
       = 0.150
```

在 N = 50 下，Sharpe 0.5 的 95% CI 宽度约为 ±0.30，即真实 Sharpe 极有可能落在 [0.20, 0.80] 区间内。

若将窗口扩至 N = 200：

```
SE(SR) = sqrt(1.125 / 200) = sqrt(0.00563) = 0.075
```

95% CI 收窄至 ±0.15，可信度显著提升。

### 2.2 H0 (Sharpe = 0) 检验功效

单尾检验：H0: SR ≤ 0，H1: SR > 0，显著性水平 α = 0.10。

**N = 50，真实 SR = 0.5：**

```
z_stat = SR / SE(SR) = 0.5 / 0.150 = 3.33
临界值 z_0.10 = 1.28

功效 = P(z > 1.28 | 真实 z = 3.33)
      = P(z > 1.28 - 3.33) = P(z > -2.05) ≈ 0.98
```

表面看功效高——但这是在"真实 SR = 0.5"的假设下。问题在于：**net edge 中位为负的情况下，真实 SR 很可能 < 0，而非 0.5**。若真实 SR = 0.1（弱 alpha）：

```
z_stat = 0.1 / 0.150 = 0.67
功效 = P(z > 1.28 - 0.67) = P(z > 0.61) ≈ 0.27
```

在弱 alpha 假设下，N=50 样本功效仅 27%，**假阳性率极高**。这与老钱 spec v1 §5 "功效约 40%" 的自评一致，甚至更悲观（净 edge 负的情景下）。

### 2.3 14 日 50 笔的现实约束

**问题一：独立性假设。** Sharpe SE 公式假设收益独立同分布。Inplay 场景中，同一场比赛的多笔 trade 高度相关（赔率漂移共同驱动），有效独立样本数 N_eff < 50。若 N_eff = 30：

```
SE(SR) = sqrt(1.125 / 30) = 0.194
95% CI = SR ± 0.38  → [0.12, 0.88]
```

CI 宽度近乎等于 Sharpe 本身，统计结论毫无意义。

**问题二：14 日是否足够 reject H0？** 若净 edge 中位负（-1.3%），14 日 paper 的期望 PnL < 0。能通过 "14 日累计净 PnL > 0" gate 的概率，主要来自正态左尾的小概率事件（运气）——这恰恰是 G3 门禁要防止的假阳性通过。

**结论：Sharpe ≥ 0.5 + N=50 + 14 日，在 net edge 中位负的约束下，无法可靠 reject H0。当前门槛过宽松。**

---

## §3 Alpha Decay 风险

### 3.1 Paper → Live 的 Alpha 衰减机制

Polymarket 体育市场作为新兴预测市场，当前做市商数量有限，信息套利窗口较宽。但以下机制会在 G3 通过后压缩 alpha：

**机制 1：学习效应。** 每一笔 paper trade（或 G4 实盘成交后的公开 order flow）都向市场传递信息。Moneyline 盘口流动性薄，单笔 $2K 已可移动赔率 0.3-0.8%（老袁微观结构估算）。市场做市商会根据 order flow 调整 quote，信号有效性下降。

**机制 2：竞争入场。** G3 paper 通过后 G4 上线，若盈利信号泄露（例如大量单向 taker flow 被观察到），会吸引其他套利者进入，压缩 edge。新进入者的 decay tau 约为 25s（老彭 W8 alpha v2 估算），意味着超过 25s 的信息优势几乎清零。

**机制 3：Gross → Net 压缩。** Paper 阶段不支付真实 taker fee 与 gas。G4 实盘后 3% taker fee + slippage 的叠加效应，使得在 paper 阶段"勉强为正"的 edge 实盘归负。

### 3.2 量化影响

```
paper alpha (gross)  =  1.5-2.5%
paper → live 衰减    =  -0.5% to -1.0%（竞争 + order flow 信号泄露）
live taker fee       =  -3.0%
live slippage        =  -0.3%
─────────────────────────────────────
live net edge        =  -2.3% to -0.8%
```

G3 paper 通过的策略，G4 实盘净 edge 大概率仍为负，**除非 gross alpha 持续达到 3.5%+ 并维持**。

### 3.3 对 G3 KR 的直接含义

G3 gate 若设置过宽（Sharpe ≥ 0.5 在 N=50），则极有可能出现：

- G3 靠运气（正态右尾）通过 → G4 实盘净 PnL 持续为负 → M4.5 持续失败 → 信心损耗 + 资本损耗

这是老钱 spec v1 §4 "G3 跑 inplay 会导致 M4.5 持续失败"警告的金融机理。

---

## §4 G3 KR 调整推荐

### 4.1 三个选项

**Option A：Sharpe ≥ 0.8 + 维持 14 日 50 笔**

```
SE(SR=0.8, N=50) = sqrt((1 + 0.5*0.64)/50) = sqrt(0.0264) = 0.163
95% CI = [0.48, 1.12]
```

CI 下界 0.48 > 0，勉强区分于噪声，但仍依赖独立性假设。若真实 SR = 0.8，功效约 90%（优）；若真实 SR = 0.3，功效仅约 35%（不足）。

**优点：** 不延长 paper 窗口，符合时间节点压力（T+22 周）。  
**缺点：** N=50 统计功效对弱 alpha 场景不足；14 日同场比赛 corr 问题未解决。

**Option B：Sharpe ≥ 0.5 + n ≥ 200 + window ≥ 30 日**

```
SE(SR=0.5, N=200) = sqrt(1.125/200) = 0.075
95% CI = [0.35, 0.65]
```

CI 下界 0.35 > 0，统计意义显著提升，独立性问题部分缓解（更多不同场次）。30 日窗口同时测试策略稳定性（跨多赛事周期）。

**优点：** 统计功效大幅提升；覆盖更多市场情景（不同联赛 / 赛季节点）。  
**缺点：** window 30 日推迟 G4 约 2 周（T+22 → T+24），与 G4 M5 首笔实盘（T+24 周）时间冲突，需 M4.5 时间轴重排。

**Option C：两阶段门禁（推荐）**

```
Prelim Gate（第 14 日）：
  - n_trades ≥ 50
  - Hit rate ≥ 56%（老彭方案一）
  - 14 日净 PnL > 0（不看 Sharpe）
  - MDD ≤ 10%
  - 通过 → 进入 Confirm Phase（不停止，继续跑）

Confirm Gate（第 30 日）：
  - n_trades ≥ 150（累计）
  - OOS Sharpe ≥ 0.8（30 日窗口）
  - Hit rate ≥ 56%（30 日维持）
  - MDD ≤ 10%（30 日维持）
  - t-test p < 0.05（单尾，更严格）
  - 通过 → 放行 G4
```

**金融理论依据：**

序贯检验（Sequential Testing）在金融策略评估中是标准实践。第一阶段（Prelim）排除明显失败策略，节约资本；第二阶段（Confirm）提供足够统计功效。这与老钱 spec v1 §5 "累计窗口 + 末窗双判定"逻辑一致，将其形式化为两个独立 gate。

**优点：** 不强行延长时间轴（14 日初筛仍有意义）；30 日 Confirm 统计功效显著提升；符合 M4.5 双判定设计。  
**缺点：** 管理复杂度略高（两 gate 状态机）；G4 最早推迟至 T+24 周（与老钱 spec v1 G4 T+24 周时间节点吻合，无额外损失）。

### 4.2 老叶推荐

**推荐 Option C（两阶段）**，配合老彭方案一（hit rate 升 56%）。理由：

1. Prelim 14 日仍有意义，排除净 PnL 明显为负的失败场景
2. Confirm 30 日 Sharpe ≥ 0.8 + n ≥ 150 提供足够统计功效（SE ≈ 0.11，功效 > 85%）
3. 时间轴与 M4.5 G4 T+24 周不冲突（Confirm 在 G4 启动前完成）
4. 与老钱 spec v1 双判定设计对齐，不引入新概念

**若时间节点不允许 Option C，则选 Option A（Sharpe ≥ 0.8 + 维持 14 日 50 笔）作为最低可接受门槛。**

---

## §5 G4/G5 金融背书 Input

### G4 M5 首笔实盘

G4 目标是"验证执行链路"而非"验证 alpha"，金融层面风险主要在执行成本：

- **Taker fee 3%：** 首笔实盘必须确认 fee 扣除逻辑正确落入 PnL 核算（避免 paper 时不扣 fee 导致 G3 通过 G4 实盘立刻为负）
- **Slippage ≈ 0.3%：** 微观结构 L1_ask 模型（小袁 microstructure v0.1）需在 G4 实盘中校准；paper 阶段若用 mid price 执行则 slippage 低估
- **RM 拒单率 ≤ 10%（G4 KR）：** 金融合理。拒单率过高说明信号 CI 频繁低于 G2 门槛（bootstrap CI < 0.3），策略有效性存疑；拒单率过低说明 RM 阈值过松

**G4 金融建议：** 在 G4 首周（24h 观察窗口后）出具一份 execution quality report，记录：平均成交价 vs mid price（滑点实测）、fee 实际扣除、RM 拒单分布。这是 G5 Sharpe ≥ 1.0 校准的基础数据。

### G5 上线盈利门禁

G5 要求连续 30 日净 PnL > 0 + 实盘 Sharpe ≥ 1.0（30 日）。金融层面：

```
SE(SR=1.0, N_30day) 取决于每日笔数:
  若日均 5 笔 → N=150: SE = sqrt((1+0.5)/150) = 0.10
  若日均 3 笔 → N=90:  SE = sqrt(1.5/90) = 0.129
  95% CI 下界: SR - 1.96 * SE ≈ 0.80 (日均5) 或 0.75 (日均3)
```

Sharpe ≥ 1.0 在 N=90-150 下统计可信度可接受，**但前提是 net edge 必须扭正**。G3→G4 之间需解决 gross alpha 是否覆盖 fee 的问题，否则 G5 将无法实现。

**G5 金融建议：** $100K 本金下月净 PnL ≥ $10K（月收益率 10%）在 Sharpe 1.0、策略 vol 约 8-12%/月的假设下，对应年化 alpha ≈ 24%。这依赖于 gross edge 稳定 ≥ 3.5% 且 fee 结构不变。若 Polymarket 调整 fee 结构或流动性下降，G5 目标将自动失效，建议 G5 KR 加"fee 不超过 3.5% 的条件触发"保护子句。

---

## §6 不耻下问

**@老钱 (CPO) — spec v2 update，截止 5/31：**

本文 §4 推荐 Option C（两阶段 prelim + confirm）作为 G3 KR 替代方案。请在 spec v2 中：

1. 确认采用 Option C / Option A / 维持 Option 原始方案（其一）
2. 若采用 Option C，prelim 14 日 gate 不看 Sharpe，仅看净 PnL > 0 + hit rate ≥ 56% + MDD ≤ 10%；confirm 30 日 gate 才引入 Sharpe ≥ 0.8
3. hit rate 56% 采用老彭方案一（§2 已确认 gross → net 影响要求 56% 而非 54%）
4. 请明确 G3 paper run 是 inplay 还是 pregame（本文 §3.1 机制 3 影响到实盘 fee 是否真实扣除）

**@老郭 (顾问协调人) — Wave 88 顾问意见 follow up：**

本文是老叶对 G3 KR 的独立金融审查。核心结论：

- Sharpe ≥ 0.5 + N=50 在 net edge 中位负约束下，统计功效不足，存在高假阳性通过风险
- 推荐两阶段 gate（Option C），或最低门槛 Sharpe ≥ 0.8
- G4/G5 需解决 paper 阶段 fee 不真实扣除导致的 alpha 高估问题

请 ack 本文是否满足 Wave 88 顾问意见 P0 要求，或补充架构层约束。

**@老雷 (GM) — final ack，截止 6/2 主管周同步：**

G3 KR 门槛调整影响 M4.5 时间轴。本文推荐的 Option C（30 日 confirm）与老钱 spec v1 G4 T+24 周时间节点不冲突（见 §4.1 分析），但需 GM 确认：

1. Option C 两阶段 gate 是否符合"上线盈利"老板 verbatim 的验收意图
2. 若 Option A（最低门槛）被选择，GM 是否接受统计功效不足的风险并明确 audit log

---

## 附录：关键公式汇总

**Sharpe SE（Lo 2002，独立同分布）：**

```
SE(SR) = sqrt((1 + 0.5 * SR^2) / N)
```

**Hit rate 盈亏平衡（Polymarket Moneyline near-even 盘）：**

```
breakeven_hit = 50% + fee / (2 * gross_edge)
以 fee=3%, gross_edge=2%: breakeven ≈ 50% + 75% = 52.5% (理想)
含滑点 0.3%: breakeven ≈ 50% + (3.3%)/(2*2%) ≈ 53.5-55%
```

**两阶段 Confirm Gate Sharpe SE：**

```
SE(SR=0.8, N=150) = sqrt((1 + 0.5*0.64)/150) = sqrt(0.00960) = 0.098
功效 (真实 SR=0.8, α=0.05): z_stat = 0.8/0.098 = 8.2 >> z_0.05=1.65  → 功效 ≈ 100%
功效 (真实 SR=0.3, α=0.05): z_stat = 0.3/0.098 = 3.06; 功效 = P(z > 1.65-3.06) ≈ 92%
```

---

*文档维护：老叶 (F 顾问团) | Next review：老钱 spec v2 发出后同步*
