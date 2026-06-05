> owner: (research agent for 老雷) · last_review: 2026-06-05

# 持仓管理外部最佳实践调研 —— 做市商库存管理 / 逆向选择规避 / 订单簿微观结构用于执行

**视角边界（本文恒定铁律）：** 方向真值 = 外部赔率源 sharp（Bet365 de-vig）。本文研究的所有订单簿/微观结构信号 **只用于"怎么执行 / 怎么管库存 / 怎么躲逆选"，绝不用于"该多还是该空"**。凡是某个做市原理隐含「用订单簿信号推断 fair / 决定方向」，本文都把它的"方向部分"剥掉、只保留"执行/库存部分"，并在「迁移性」一节显式标注边界。

我们系统现状（约束）：
- fee = shares × rate × p × (1−p)，rate ≈ 0.03；限价不追，`reservation = fair − fee − margin`，fair 来自外部赔率源。
- 目标仓位连续控制：`order = target − current`，被动限价。
- 已有 sharp 时序环：OFI / microprice / sharp velocity / convergence —— **目前只观测、没接执行**。

---

## 一、核心方法

### A. Avellaneda-Stoikov（A-S）最优做市 —— 库存惩罚 + reservation price 随库存偏移

A-S（2008）把做市建成随机最优控制问题：做市商面对价格随机游走 + 随机成交到达 + 持仓被行情反向打的风险。闭式解给出两个独立部件 [1][2][8]：

**(1) Reservation price（库存调整后的报价中心）：**

```
r(s, q, t) = s − q · γ · σ² · (T − t)
```

- `s` = 参考价（A-S 原文是 mid，**对我们 = 外部赔率源 fair**）
- `q` = 库存对目标的偏离（>0 净多 / <0 净空）
- `γ` = 库存风险厌恶
- `σ` = 波动率
- `(T − t)` = 到 horizon 的剩余时间

机制：q>0（持多过头）→ r 下移 → 卖单更靠前、买单更靠后 → 鼓励减多回到目标；q<0 对称。**关键数学性质（本文最重要的迁移钥匙）：库存项 `−q·γ·σ²·(T−t)` 是一个 *加性偏移*，它只依赖 q/γ/σ/时间，*与参考价 s 的水平无关*** [8]。也就是说 A-S 没有强制 s 必须是 mid —— 你把 s 换成任何外部 fair，库存 skew 项照样成立。这正是我们能把库存惩罚搬过来、又不动方向的根据。

**(2) 最优 spread（围绕 r 的半价差之和）：**

```
δ_a + δ_b = γ · σ² · (T − t) + (2/γ) · ln(1 + γ/κ)
```

- 第一项 `γσ²(T−t)`：波动越大 / 时间越长 → spread 越宽（库存风险定价，随收盘收窄）。
- 第二项 `(2/γ)ln(1+γ/κ)`：`κ` = 订单簿深度/成交强度参数，簿越厚（κ 大）→ spread 越紧。`κ` 由成交到达强度 `λ(δ)=A·e^(−κδ)` 拟合而来 [1][2]。

> 迁移到我们：reservation 价中心来自外部 fair，**spread 的"加宽"部分（第一项）和"簿厚"部分（第二项）是纯执行经济学，与方向无关**，可借鉴用来决定挂多远、撤不撤。

参考实现细节见 Hummingbot 两篇 guide [3][9] 与 Cornell 原始 PDF [10]。另有 2606.01477 把 A-S 与 Cartea-Jaimungal 统一为同一框架，并证明库存项的"强制唯一性" [11]。

### B. 库存 skew 报价（A-S 的工程化版本）

实务里大多数做市引擎（Hummingbot / Optiver 风格）不解全套 PDE，而是把 A-S 思想拆成两个旋钮 [3][7]：
1. **skew（中心偏移）：** 报价中心 = fair − 库存项，库存越偏目标 skew 越大，把成交往"减仓方向"拉。
2. **spread（半宽）：** 由波动 + 簿深 + 逆选风险动态定。

库存 skew 的本质：**用"哪一侧更容易成交"来被动平衡库存，而不是主动下市价单去平**。对二元盘 settle→0/1 的特性，prediction-market 实务文章也强调"shift mid-price 鼓励减仓 / 临近 settlement 更激进减仓 / 设每市场仓位上限" [来自 prediction MM guide, 见 §三]。

### C. 逆向选择识别（Glosten-Milgrom + VPIN/OFI/microprice）

**Glosten-Milgrom（1985）—— 为什么 informed flow 让持仓变毒** [GM 搜索结果, 见 Sources]：
- 风险中性做市商，面对"知情交易者 + 噪声交易者"混合 flow。做市商看不出谁是谁，但**成交方向本身泄露信息**：被买走（吃你 ask）边际上意味着价格可能要涨，你刚卖出的这手在变亏。
- 结论：即使做市商风险中性、零期望利润，**信息不对称也强制产生正的 bid-ask spread** —— 这就是 spread 的"逆选成分"。informed 占比越高 / 不确定性越大 → spread 越宽。
- 对我们的含义：**逆选成分恒在，且与"我们 fair 准不准"无关** —— 即使方向锚定外部赔率源是对的，吃我们单的人可能比赔率源更快（news 先于 Bet365、或抢在 Goalserve 2s feed 之前）。这部分必须用 spread/撤单防御，不能靠"我们方向对"豁免。

**VPIN（订单流毒性，Easley-López de Prado-O'Hara）** [VPIN sources]：
- 实时估计 order flow toxicity = informed 逆选 uninformed 的概率，按"成交量桶"算买卖不平衡。
- 阈值实务：VPIN 高（如 >0.7，单边 flow 很重）→ 做市商应**加宽报价或减敞口**；toxicity 飙升时流动性提供者集体撤出（2010 flash crash 前一小时 VPIN 创历史新高）。
- **注意反馈环：** MM 撤出 → VPIN 更高 → 更多 MM 撤 → 流动性塌。我们撤单要有"集体踩踏"意识，但作为单一 LP，自保优先。

**OFI / microprice / book imbalance —— 逆选的事前/事后探测器** [OFI sources, Stoikov microprice SSRN 2970694]：
- **OFI（order flow imbalance）：** 买卖订单净差，刻画近端方向压力。市价单 OFI 可作"事后逆选"的估计；OFI 的预测分布可作"未来逆选"的指标。OFI 编码的就是"toxicity"。
- **microprice（Stoikov 2017）：** = mid 基于 spread + imbalance 的调整，是比 mid / weighted-mid 更好的短期未来价预测器；引入了"接近逆选规避"的效应。
- **book imbalance：** 簿两侧深度比，短期价格移动的预测特征。

> **这三者天然有"方向味"（它们预测短期价格往哪走）。这正是我们边界最吃紧的地方** —— 见 §二「不能迁移的坑」。本文的处理：**只取它们的"幅度/触发"信号（逆选压力有多大 → 撤不撤 / 缩不缩 / 扩不扩），丢弃它们的"符号/方向"含义（往上还是往下 → 这归外部赔率源）**。

### D. 订单簿微观结构用于执行（queue position + 撤单/重挂）

被动限价的执行质量由**排队位置（queue position）**决定 [queue/adverse-selection sources, Moallemi 2016; arXiv 1610.00261]：
- 队首的单 vs 队列平均位置的价值差，可与整个 bid-ask spread 相当 —— queue position 在做市/执行控制里不可忽略。
- **逆选 vs 排队的两难：** 排在队首成交快，但更容易被逆选（价格要反向时你先成交、成了"接刀"的那一手）。
- **撤单是躲逆选的武器，但有代价：** 撤掉再重挂 → 丢失排队位置（回到队尾），可能再也成交不了。
- **latency 决定撤单是否有用：** 能预测即将到来的"吃单流"才有时间撤；撤得不够快，imbalance 信号的价值被延迟吃光 —— "做市商要尽量快以减少逆选"。
  - 我们体质：实测 warm 往返 ~14ms、网络 RTT 3-6ms（CLAUDE.md §1），**撤单能跑赢逆选**，这是我们做市可行的物理前提。

---

## 二、迁移性 —— 怎么只用于执行/库存/逆选、不越界判方向

### 能干净迁移的（只动执行/库存/spread，方向不碰）

| 原理 | 迁移形态 | 为什么不越界 |
|---|---|---|
| **A-S 库存项 `−q·γ·σ²·(T−t)`** | reservation 在外部 fair 基础上叠加库存偏移：`reservation = fair − fee − margin − q·γ·σ²·(T−t)` | 库存项与参考价水平无关 [8]，纯粹是"持仓越偏目标越想减"，方向中心仍是外部 fair。**这一项不创造方向观点，只创造"减仓压力"** |
| **A-S spread 第一项 `γσ²(T−t)`** | 波动/临近 settlement 时加宽 margin | 对称加宽，不偏向任何一边 = 不表达方向 |
| **A-S spread 第二项 `(2/γ)ln(1+γ/κ)`** | 簿薄时挂更保守、簿厚时挂更靠近 | 纯执行质量（深度），无方向 |
| **GM 逆选成分** | margin 里常驻一块"逆选费"，可随 toxicity 放大 | 对称 spread，防御性，不判方向 |
| **VPIN / OFI 幅度** | 触发"撤单 / 缩量 / 扩 spread"，**只用 |OFI| 大小，不用符号** | 只回答"现在逆选压力大不大"，不回答"价格往哪" |
| **queue position + 撤单** | 逆选压力上来时撤掉暴露的那侧、重挂更保守 | 执行动作，方向中心不动 |

### 不能迁移 / 会越界判方向的坑（红线）

1. **microprice / OFI 符号当 fair 用 = 越界。** microprice 是"短期未来价预测器"，book imbalance 偏向哪侧就预测往哪走 —— 这是 *方向信号*。**严禁**用 microprice 替代或修正 fair 中心、严禁用 OFI 符号决定"该挂买还是挂卖"。方向中心永远 = 外部赔率源 fair。我们只许用它们的 **绝对值/不平衡强度** 触发执行动作。
   - 工程护栏：sharp 时序环里 microprice/OFI 进入决策时，**先取 abs() 或映射成 toxicity 标量**，物理上切断符号传到方向逻辑的路径。
2. **A-S 把 reservation 当"我们的 fair 估计" = 越界。** A-S 原文 `s` 是做市商*自己*估的 mid，且模型假设"做市商无信息优势"[2508.20225]。我们不是 —— **我们的 fair 是外包给赔率源的**。所以只能搬"库存项 + spread 项"这两个 *偏移量*，**不能搬"用成交方向反推 fair 该往哪调"** 那套（那是 Glosten-Milgrom 式 Bayesian 更新 mid，会让订单簿信号悄悄进方向）。
3. **库存 skew 不是方向押注。** skew 让一侧更易成交，但这是"为了减库存"，不是"因为我觉得要涨/跌"。若 q=0（在目标上），skew=0，报价对称围绕外部 fair —— 必须保持这个不变量，否则 skew 会被误用成方向。
4. **"informed flow 预测方向"诱惑。** GM/VPIN 文献天然导向"flow 知情 → 跟着 flow 做方向"。**我们明确放弃这条**：flow 知情对我们意味着"躲"（撤/缩/扩），不意味着"跟"（顺着 flow 加仓）。跟 flow = 用订单簿判方向 = 越界。
5. **Cartea-Wang「Market Making with Alpha Signals」的 alpha 项** [alpha-signals sources]：该框架把 alpha 信号直接进报价中心做"投机性 market order + roundtrip"。**对我们，唯一合法的 alpha = 外部赔率源**；订单簿衍生的 alpha 一律不进方向。可借鉴的是它"库存风险容忍度低时 alpha 收益很小"的结论 —— 提醒我们库存惩罚 γ 别调太狠，否则连合法的赔率源 alpha 都吃不到。

### 边界共存的一句话总结

> **方向中心 = fair（外部赔率源）。订单簿信号只产生三类东西：① 库存 skew 偏移（A-S 库存项）② spread 半宽（A-S spread + GM 逆选费 + VPIN 放大）③ 撤单/缩量触发（OFI/VPIN 幅度）。三类全是"偏移量/标量",没有一类是"方向中心"。符号信息（往上/往下）在进入执行逻辑前被物理丢弃。**

---

## 三、对我们 reservation / 执行 / 撤单的具体建议

### 建议 1：reservation 价引入库存项（最高价值，直接可落）

现状 `reservation = fair − fee − margin`，**没有库存反馈** —— 仓位越偏目标也不会主动让报价帮你减仓。建议改为：

```
reservation = fair − fee − margin − q · γ · σ² · (T − t)
```

- `q = current − target`（>0 持多过头）。
- `σ` 用 sharp 时序环里已算好的近端波动（复用现有环，无需新管线）。
- `(T−t)` 对体育盘 = 到比赛结束/settle 的剩余时间 → 临近终场库存项自然增大（A-S 原意），逼着收敛库存，正好契合二元盘 settle→0/1 + prediction-MM 实务"临近 settlement 激进减仓"。
- `γ` 从小起步（库存惩罚弱），按 Cartea-Wang 提醒别调太狠（否则吃不到赔率源 alpha）。
- **不变量护栏：q=0 时库存项=0，reservation 对称围绕 fair —— 单测锁死这条，防 skew 漂成方向。**

### 建议 2：spread / margin 动态化（GM 逆选费 + A-S 波动项）

`margin` 不该是常数。建议拆三块（全对称、全无方向）：
```
margin = margin_base                              (固定)
       + k_vol · σ² · (T−t)                       (A-S 波动项)
       + k_tox · toxicity                          (GM 逆选 / VPIN 放大)
```
- `toxicity` = VPIN 或 |OFI| 标量化（**取绝对值，符号丢弃**）。
- toxicity 高 → margin 自动加宽 → 我们挂得更保守、被逆选概率下降。这是 GM「逆选成分」的工程落地。

### 建议 3：OFI/VPIN 触发撤单/缩量（接通"只观测的 sharp 时序环"）

把已有但只观测的 OFI/microprice/velocity 环**接到执行侧**（不接方向侧）：
- **撤单触发：** 当 toxicity（|OFI| 或 VPIN）越过阈值（文献参考 VPIN>0.7 单边重 [VPIN]），**撤掉当前暴露那一侧的限价单**，等 toxicity 回落再重挂。利用我们 ~14ms warm 往返跑赢逆选（CLAUDE.md §1）。
- **缩量替代全撤：** 不必非黑即白 —— toxicity 中等先**缩小挂单 size**（降低单次被逆选损失），高才全撤。
- **符号护栏：** 触发只看 |OFI| / VPIN 幅度。**严禁**"OFI 偏买就把买单撤了留卖单"这种按符号偏一侧的逻辑 —— 那等于用订单簿判方向。撤就两侧对称地撤/缩，方向中心由外部 fair 决定该挂哪侧的目标。

### 建议 4：queue position 感知的重挂纪律

- 撤单有代价（丢排队位 [Moallemi/1610.00261]）。撤前估一下"还在不在队首/前段" —— 已被推到队尾且 toxicity 不高的单，撤的边际收益小。
- 重挂默认回到队尾，所以**别频繁撤重挂**（churn 既丢 queue 又费 fee）。把撤单留给 toxicity 真正越阈值的时刻。

### 建议 5：临近 settlement / 高波动的全局收紧（A-S `(T−t)` 内生效果）

- `(T−t)→0`（终场临近）时，库存项和 spread 波动项都该让库存收敛、报价收紧 —— 与我们"终态即退订/退活证锚点"的现有生命周期一致，可把 A-S 的 `(T−t)` 衰减直接挂到比赛剩余时间上，自然驱动"临近终场不再背大库存"。

---

## Sources

- [1] Avellaneda-Stoikov 概览（QuantLabs）: https://www.quantlabsnet.com/post/ultra-low-latency-high-frequency-market-making-a-comprehensive-analysis-of-the-avellaneda-stoikov-f
- [2] Hummingbot 综合 guide: https://medium.com/hummingbot/a-comprehensive-guide-to-avellaneda-stoikovs-market-making-strategy-102d64bf5df6
- [3] Hummingbot A-S guide: https://hummingbot.org/blog/guide-to-the-avellaneda--stoikov-strategy/
- [7] Optiver A-S 库存优化（Medium）: https://medium.com/@navnoorbawa/optivers-3-5b-market-making-engine-avellaneda-stoikov-inventory-optimization-at-scale-a28fede5a85a
- [8] A-S 库存项与 mid 水平无关（搜索佐证 + Udit Samani）: https://uditsamani.com/avellaneda-stoikov/
- [9] Hummingbot 技术深潜: https://hummingbot.org/blog/technical-deep-dive-into-the-avellaneda--stoikov-strategy/
- [10] A-S 原始论文（Cornell PDF）: https://people.orie.cornell.edu/sfs33/LimitOrderBook.pdf
- [11] A-S 与 Cartea-Jaimungal 统一框架: https://arxiv.org/html/2606.01477
- Glosten-Milgrom 模型（GMU 学习做市商论文）: https://cs.gmu.edu/~sanmay/papers/das-qf-rev3.pdf
- Glosten-Milgrom 原文（Columbia）: https://business.columbia.edu/faculty/research/bid-ask-and-transaction-prices-specialist-market-heterogeneously-informed-traders
- VPIN（quantresearch.org PDF）: https://www.quantresearch.org/VPIN.pdf
- VPIN 与 Flash Crash + 阈值实务（VisualHFT）: https://www.visualhft.com/post/volume-synchronized-probability-of-informed-trading-vpin
- OFI / toxicity 概览（EmergentMind）: https://www.emergentmind.com/topics/order-flow-imbalance
- Stoikov microprice（SSRN 2970694）: https://papers.ssrn.com/sol3/papers.cfm?abstract_id=2970694
- 限价单逆选与 latency（arXiv 1610.00261）: https://arxiv.org/pdf/1610.00261
- Queue position valuation（Moallemi 2016）: https://moallemi.com/ciamac/papers/queue-value-2016.pdf
- Market Making with Alpha Signals（Cartea-Wang, Oxford）: https://ora.ox.ac.uk/objects/uuid:c2ba6656-8eab-4b2e-a24a-e9e842d1378f/files/s41687h481
- Optimal Quoting under Adverse Selection and Price Reading（arXiv 2508.20225）: https://arxiv.org/html/2508.20225v1
- Prediction market making guide（NYC Servers）: https://newyorkcityservers.com/blog/prediction-market-making-guide
