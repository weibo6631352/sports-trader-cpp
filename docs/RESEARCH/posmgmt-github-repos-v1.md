> owner: (research agent for 老雷) · last_review: 2026-06-05

# 持仓管理调研 — GitHub 高 star 仓库 + 开源实现视角 (v1)

调研目标：为我们的 Polymarket 体育二元盘口量化系统找"持仓怎么动态管理"的真实开源代码。
我们系统约束（贯穿全文做适用性判断）：
- 标的=二元 outcome token（YES/NO），单盘口暴露 = 仓位 × 价格。
- **方向真值 = 外部赔率源 sharp**，我们 ML 不可靠 → 凡是"靠自家模型/AI 估 fair 来定方向"的项目，**只借工程不借信号**。
- 订单簿只管执行/库存/逆选，不判方向。
- 费曲线 fee = shares × rate × p(1−p)。
- 目标仓位连续控制 + Kelly + reservation 限价**不追**。

> 关键结论先放：与我们范式最贴合的不是"AI 估值 bot"，而是 **(a) 赔率源锚定 + flat/Kelly 下单 + 限价挂单不追 + CLV 跟踪** 这一类（mlb-kalshi-bot / iceberg-betting-bot），以及 **(b) A-S 库存偏移报价**（rodlaf KalshiMM / Hummingbot / nikhilnd）。纯 Kelly 库（keeks）只提供"单注尺寸"，**不提供"目标仓位连续控制"**——后者要自己拼。

---

## 类 1 — 仓位规模 / Kelly 库

### 1.1 wdm0006/keeks — 6★ · Python
URL: https://github.com/wdm0006/keeks
- **做了什么**：纯 bankroll 分配库。`BankRoll` 类（initial capital / bettable % / max drawdown 上限）+ 一组 sizing 策略：Kelly、**Fractional Kelly**、**Drawdown-Adjusted Kelly**（按当前回撤 vs 风险阈值缩注）、OptimalF（Ralph Vince）、Fixed Fraction、**CPPI**（保底 floor + 乘数上行暴露）、**Dynamic Bankroll**（按近窗表现自适应）、Merton/CRRA、Naive（EV > 交易成本就全下）。所有策略入参带 payoff/loss/**transaction cost**。
- **借鉴点**：① Drawdown-Adjusted Kelly 公式（回撤越深缩注越狠）直接可移植成我们 sizing 的一层乘子；② Naive 把"交易成本"作为下注门槛——我们 fee=p(1−p) 曲线可同位置插入；③ CPPI 的"保底 floor"思路适合做 daily stop。
- **对我们**：可用，但只给"单注尺寸"。star 低、是参考实现而非生产库。**建议抄公式不抄依赖**（C++ 重写 4-5 个函数即可）。它没有"目标仓位连续控制/已有仓位增减"概念，那部分要我们自己接（见 §3.2 guberm 的 f* → 目标仓位映射）。

### 1.2 dcajasn/Riskfolio-Lib — 4.2k★ · C++/Fortran 核 + Python API
URL: https://github.com/dcajasn/Riskfolio-Lib
- **做了什么**：组合优化库，含 **Logarithmic Mean Risk (Kelly) 组合优化**，4 目标函数 + 26 凸风险测度（CVaR / EVaR / max drawdown / CDaR / Ulcer 等），约束含 tracking error、turnover、leverage、cardinality、风险贡献不等式。
- **借鉴点**：当我们要在**多盘口同时持仓**且盘口相关（同场 ML/Totals/Spreads 高相关）时，"全 Kelly 组合优化 + 风险贡献约束 + turnover 上限"是正解，避免对相关腿独立全 Kelly 叠出超额暴露。
- **对我们**：中长期可用（多盘口组合阶段）。MVP 单盘口用不上。**不直接进生产**（Python 求解器，热路径禁），但可离线算"相关腿暴露上限"参数喂给 C++ 风控。

### 1.3 thk3421-models/KellyPortfolio · Python · jpceia/kelly · Python
URL: https://github.com/thk3421-models/KellyPortfolio · https://github.com/jpceia/kelly
- **做了什么**：KellyPortfolio 把用户预期年化 + `kelly_fraction`（取全 Kelly 的百分比）转成组合权重；jpceia/kelly 是几个 Kelly 变体的小包。
- **对我们**：低 star、教学性质。仅作 fractional-Kelly 公式交叉验证参考，不建议依赖。

---

## 类 2 — 体育博彩 / value betting bot（bankroll / CLV / staking / 限价）

### 2.1 mmoore07129/mlb-kalshi-bot — 1★ · Python（~2.5k 行） 【范式最贴合】
URL: https://github.com/mmoore07129/mlb-kalshi-bot
- **做了什么**（几乎是我们约束的镜像）：
  - **Pinnacle-primary fair-value sourcing**：三本 sportsbook 去 vig 后按固定权重 0.55/0.30/0.15（Pinnacle/LowVig/BetOnline）blend，**Pinnacle 去 vig 价为主信号**。
  - **XGBoost 仅做 fallback veto**：模型**对 Pinnacle 来源的 bet 无否决权**（回测发现分歧局模型更差），只有 Pinnacle 没线时才触发，且加 55% conf gate + 8% min EV 门。→ **正是我们"赔率源=真值、ML 不可靠不单独驱动"的同构选择**。
  - **下单与重价**：挂在当前 ask，watcher 在"ask 移动 ≥ 2¢"时改价，EV 跌破准入门就撤——**限价跟随但不无脑追**。
  - **CLV 跟踪**：cron 每 5min 快照 sharp-book fair，算 probability-space（收盘概率 − 用价概率）与 ROI-space；并对**所有分析过的局**（非仅下注局）抓 Pinnacle 收盘价，做 gap-aware 评估。
  - **sizing**：从 1/8 Kelly **退回 flat $20/注**（小 bankroll 下 Kelly 注被 min-bet floor 抹零）。
- **借鉴点**：① 赔率源加权 blend + 模型只做兜底/否决的**分层信号架构**，整套可照搬到我们 Goalserve-sharp 锚定；② **CLV 双口径快照（概率空间 + ROI 空间）+ 对未下注局也快照**——这是验证 edge 真假的金标准，我们正缺；③ "限价挂 ask、阈值移动才改价、EV 失效撤单"的执行循环；④ 小本金下 Kelly 被 floor 抹零的坑（我们 micro size 也会遇到）。
- **对我们**：**最值得逐文件读**。star 极低但代码与我们目标同构度最高。注意它用 The Odds API（我们不接，用 Goalserve），换数据源即可。

### 2.2 odyssey017/iceberg-betting-bot — 7★ · JavaScript 【"限价不追"最贴合】
URL: https://github.com/odyssey017/iceberg-betting-bot
- **做了什么**：SX Bet 交易所上的**动态单边限价挂单**机器人，**无需赔率 feed**：
  - **edge-based 挂单**：目标 odds = 当前最优 taker odds × (1 + edge%)，始终比市场好一个设定边际。
  - **orderbook 跟随重价**：盘口移动就重算并改挂，"never stale"。
  - **iceberg 分片**：大单拆成增量，每增量 = 同一时刻在簿上暴露的最大风险（控制冰山式入场，避免一次灌满）。
  - **vig 监控撤单**：算 overround，超阈值自动撤。
  - 模块：`iceberg.js`(CLI/建仓管理)、`monitor.js`(监控/校验/执行/vig)、`network.js`(下/撤/行情)、`config.js`。
- **借鉴点**：① **单边挂单始终领先盘口 edge% + 盘口移动才重挂** = 我们"reservation 限价不追"的工程范式，直接对应；② **iceberg 增量 = 在簿暴露上限**，是天然的库存/逆选闸（一次只露一小块）；③ vig 阈值撤单 = 我们可换成"fee+spread 吃掉 edge 就撤"。
- **对我们**：**执行层范式直接可借**（语言换 C++）。注意它是单边（我们做市要双边/库存对称）；但作为"方向已定（sharp 给方向）后如何限价挂、如何不追、如何分片入场"的样板，贴合度极高。

### 2.3 georgedouzas/sports-betting — 709★ · Python
URL: https://github.com/georgedouzas/sports-betting
- **做了什么**：value bet = 估计概率 > 赔率隐含概率才下；`ClassifierBettor` 包 sklearn 出概率；`backtest()` 时序 CV，配 `init_cash` + 固定 `stake`。强调"value bet 选择是唯一长期策略，别过度复杂化预测模型"。
- **借鉴点**：① **时序 CV 回测框架**（init_cash + stake 逐注推进）结构清晰，可参考我们回测的 bankroll 演进与防泄漏；② "value = 估计 vs 隐含"的判定与我们 sharp-anchored edge 一致。
- **对我们**：staking 是 flat 固定注（无 Kelly/无目标仓位），信号靠自家分类器（我们不用）。**借回测骨架与 value 判定，不借 sizing/信号**。

### 2.4 TessaRichardson/SureBetsBot — Python · 套利 + Kelly
URL: https://github.com/TessaRichardson/SureBetsBot
- **做了什么**：跨 book 套利检测 + Kelly 定注 + bankroll 模拟 + Streamlit 面板。模块化（fetch odds / calc arb / size bet）。
- **对我们**：套利不是我们当前主线（参见 MEMORY: 套利斗不过 PM 费），但 **"fetch→edge→Kelly size→bankroll 模拟"的模块切分**可参考。优先级低。

---

## 类 3 — 预测市场做市 / 交易 bot（Polymarket / Kalshi / Augur）

### 3.1 warproxxx/poly-maker — 1.3k★ · Python+JS 【Polymarket 做市最高 star】
URL: https://github.com/warproxxx/poly-maker
- **做了什么**：Polymarket 自动做市，双边挂单、bands.json 配置、每秒读市价决定撤/挂、按市价 ± margin 建 band。模块：`poly_data`(数据+做市核心逻辑)、**`poly_merger`(持仓合并——把同向 outcome 仓位 merge 省 gas)**、`poly_stats`、`poly_utils`。
- **借鉴点**：① **Polymarket 特有的 position merging**（YES/NO 对冲后可 merge 成 USDC、减 gas/释放保证金）——这是 Polymarket 链上特有的持仓管理动作，我们做市平仓/对冲时**必须懂这个机制**，poly_merger 是现成参考；② 双边 band 报价 + 每秒重评撤挂的循环结构；③ WSS orderbook 监控接线。
- **对我们**：**作者明示"当前市场下不盈利、是参考实现别直接上"**——信号/spread 不可照搬（无赔率源锚定）。但 **Polymarket 接口/下撤单/position merge/WSS 这些"管道"是同平台最成熟的开源参考**，工程价值高。逐 `poly_merger` + `poly_data` 读。

### 3.2 guberm/polymarket-bot — 8★ · C#/Python/JS 【分层风控 sizing 公式完整】
URL: https://github.com/guberm/polymarket-bot
- **做了什么**：
  - **二元 Kelly sizing 公式完整可抄**：
    ```
    execution_price = market_price + entry_price_buffer
    edge = fair_probability − execution_price
    b = (1 − execution_price) / execution_price
    f* = (b·p − q) / b
    bet = kelly_fraction · f* · portfolio_value
    ```
    live 模式封顶半 Kelly（除非 allow_unsafe_risk），并取 min(f*封顶, max_position_pct, 可用 bankroll)。
  - **6 层风控**：①单仓 15% ②单类别 80% ③总暴露 100% ④日 stop-loss 20% ⑤max drawdown 50% ⑥平仓后 2 cycle cooldown 禁再入。
  - 信号：多 AI provider ensemble（trimmed mean），edge>10% 才动。
- **借鉴点**：① **上面的二元 Kelly 公式直接是我们 outcome token 的形态**（b=(1−px)/px 就是二元赔率），加 entry_price_buffer 正对应我们"reservation 不追"；② **6 层分层风控清单**几乎可照搬做我们 RiskManager 的 cap 体系（单仓/单类别/总暴露/日 stop/回撤/cooldown）；③ live 强制半 Kelly 封顶 = 我们 fractional Kelly 默认值参考。
- **对我们**：**sizing 公式 + 风控分层最值得抄**。AI ensemble 信号**丢弃**（我们用 sharp）。star 低但代码结构对二元市场最对症。

### 3.3 OctagonAI/kalshi-deep-trading-bot — Python
URL: https://github.com/OctagonAI/kalshi-deep-trading-bot
- **做了什么**：Kalshi CLI，深度研究估概率 + 算 edge vs 实时簿 + **Kelly sizing + 5-gate risk engine** 下单。
- **借鉴点**："edge vs live orderbook + Kelly + 多 gate 风控引擎"的下单管线分层；5-gate 清单可与 guberm 6 层对照取并集。
- **对我们**：信号(LLM 研究)弃用；**风控 gate 编排**可参考。

### 3.4 ryanfrigo/kalshi-ai-trading-bot · Python
URL: https://github.com/ryanfrigo/kalshi-ai-trading-bot
- **做了什么**：签名 Kalshi client + 行情摄取 + **position tracking** + SQLite telemetry + 面板；示例策略：扫 NO 侧 ask 高于阈值且正 EV，挂 maker 单低于 ask 一分。
- **借鉴点**：① **position tracking + SQLite telemetry**（持仓状态落库 + 可观测）结构；② "挂在 ask 下一分做 maker、正 EV 才挂"的限价 maker 范式（和我们 reservation 同向）。
- **对我们**：管道/telemetry 参考；信号弃用。

---

## 类 4 — 做市框架（库存管理 / 报价 / 逆选）

### 4.1 hummingbot/hummingbot — Avellaneda-Stoikov 实现 · Python(.pyx) 【A-S 工业级参考】
URL: https://github.com/hummingbot/hummingbot/blob/master/hummingbot/strategy/avellaneda_market_making/avellaneda_market_making.pyx
- **做了什么**：A-S 做市完整实现。**reservation price 按库存偏移 q**：q=0 → reservation=mid；q<0（short）→ 抬 reservation 偏向买；q>0（long）→ 压 reservation 偏向卖。q 由"目标库存占比"算出；可调 `risk_factor γ`；有**订单簿流动性估计器自动算交易强度参数**；`order_amount_shape_factor η` 做单量塑形。
- **借鉴点**：① **库存偏移 reservation 的核心公式** r = mid − q·γ·σ²·(T−t) ——这是"做市时持仓如何反馈到报价"的标准答案，**库存越偏离目标、报价越往回拉仓的方向偏**，正是我们要的"目标仓位连续控制"机制；② η 单量塑形（离目标越远挂越多/少）；③ 流动性估计器自动标定 trading intensity。
- **对我们**：**A-S 库存→报价偏移逻辑是我们做市持仓管理的理论骨架**。但要改造：我们 reservation 的"中心"不是 mid 而是 **sharp fair**（方向真值来自赔率源），σ²(T−t) 项在体育盘可用赛程剩余时间/比分波动近似。**抄库存偏移结构，把 mid 换成 sharp fair**。

### 4.2 rodlaf/kalshimarketmaker — 211★ · Python 【二元市场 A-S，最对症】
URL: https://github.com/rodlaf/kalshimarketmaker
- **做了什么**：每个选中 Kalshi 市场跑一个 A-S worker，算 reservation / 非对称报价 / 单量（γ=0.2,k=1.5,σ=0.001,T=28800）；**库存风险厌恶随库存接近上限上升**，`inventory_skew_factor=0.001` 调报价非对称（long 倾向卖、short 倾向买）。风控：`max_global_contracts=20`、`max_contracts_per_market=3`、达全局上限**阻断新风险累积**、position limit buffer=0.05 防过杠杆、deselect 清理（撤单+停 worker+核对）、retry/backoff。
- **借鉴点**：① **二元 contract 上的 A-S**（不是连续价标的）——和我们 outcome token 形态一致，比 Hummingbot 的 crypto pair 更对症；② **per-market + global 双层 contract 上限 + "到顶阻断新风险"**；③ inventory_skew_factor 线性调报价非对称的具体落法；④ deselect 退场时"撤单+停 worker+核对"的干净退出（对我们完赛退订/平仓清理直接有用）。
- **对我们**：**类 4 里对我们最对症的开源仓库**（二元 + A-S + 库存 skew + 双层 cap + 干净退出）。逐文件读 worker 的 reservation/skew/size 计算。同样把"中心"换成 sharp fair。

### 4.3 nikhilnd/kalshi-market-making — 70★ · Python/Notebook
URL: https://github.com/nikhilnd/kalshi-market-making
- **做了什么**：Cauchy 分布估概率 → 围绕估计概率对称挂 → **库存调整：spread 随库存线性上下移**（持正库存压 spread 鼓励卖，反之）。Trading State 存簿+标的价+持仓；Order Manager 撤旧挂新。WSS 接 Kalshi。实测 51 单 $199 量赚 $6.8（40 contract 上限、仅 resting 单）。
- **借鉴点**：① **"spread 随库存线性偏移"的最小可读实现**（比 A-S 简单，适合先落地再升级）；② **Trading State / Order Manager 清晰分层**（持仓状态单一真源 + 撤挂分离），契合我们 state_provider + executor 切分；③ 仅 resting 单（限价不吃单）= 我们 maker 取向。
- **对我们**：**库存→spread 线性偏移**是 A-S 的轻量版，MVP 可先上这个再迭代到 A-S。Cauchy 概率信号弃用（用 sharp）。

### 4.4 TimCaron/Market_Maker_Strategies — Python
URL: https://github.com/TimCaron/Market_Maker_Strategies
- **做了什么**：多标的多策略做市回测框架，支持 Stoikov 模型及扩展，含参数优化/可视化。
- **对我们**：做市策略**回测/参数寻优**参考（γ/k/σ 标定），离线用。

---

## 类 5 — 订单簿微观结构 / OFI / microprice（执行层信号）

### 5.1 sstoikov/microprice — 454★ · Jupyter（原作者）
URL: https://github.com/sstoikov/microprice
- **做了什么**：Stoikov 原版 microprice 估计器——用**盘口 imbalance + spread** 对 mid 做调整，得"给定簿状态的 fair 价"（martingale by construction）。核心是 Markov-chain / G 矩阵把(spread,imbalance)状态映到未来中价调整。代码紧凑（单 notebook + 两 CSV）。
- **借鉴点**：microprice 的**核心公式（imbalance 加权 + 状态调整）可抽成独立算法移植 C++**。对我们：microprice 不判方向（方向来自 sharp），但**做执行时的"短期公允中价/逆选指示"很有用**——挂单点、判断对手单是否信息流、估计被逆选风险。
- **对我们**：**执行层用，不判方向**。把 microprice 当"当前簿下的瞬时公允"，与 sharp fair 比较 → 偏离方向提示逆选/抢跑风险，辅助 reservation 偏移与撤单决策。公式可移植 C++。

### 5.2 nicolezattarin/LOB-feature-analysis — 268★ · Jupyter/Python
URL: https://github.com/nicolezattarin/LOB-feature-analysis
- **做了什么**：LOB 特征工程：**OFI / 多层 MLOFI**（最优买卖及多档供需失衡）、PIN（informed trading 概率）、order size 分布、逐笔 realized vol，用 volume bars 取样。
- **借鉴点**：**OFI/MLOFI 标准公式**（Δ供需失衡）可抄进我们执行层做"短期价压/逆选信号"；PIN 可作"对手是否信息流"的逆选度量。
- **对我们**：执行/逆选层可用（不判方向）。我们 MEMORY 已有 FeatureHistory 环算 OFI——此仓可对照校准公式与多档扩展。

### 5.3 nkaz001/hftbacktest — 4.2k★ · Rust 核 + Python 【做市回测基建天花板】
URL: https://github.com/nkaz001/hftbacktest
- **做了什么**：HFT/做市回测平台，**Level-3 MBO 重建队列位置做成交模拟** + **feed/order 双向延迟模型（可自定义）** + 库存/持仓跟踪（`hbt.position()`）+ OFI alpha tutorial。Rust 核（py 绑定）。
- **借鉴点**：① **队列位置 + 延迟建模的成交模拟**——我们 reservation 限价单"能否成交、排队多久、被逆选概率"必须靠这种 queue-aware 模拟才回测得真，否则 fill 假设乐观；② 延迟模型（我们实测 warm ~14ms）可参数化进去；③ 库存跟踪 API 设计。
- **对我们**：**回测引擎设计的最高参考**（队列+延迟+库存）。但**是 Rust——我们禁 Rust**，不能引入；只**借架构思想**用 C++ 重写（队列位置模型、延迟模型、库存账本）。与我们"回测必须 C++、与生产共享 binary"红线一致。

---

## 横向总结：现成"动态持仓管理"实现盘点

| 维度（持仓怎么动态管理） | 现成可参考的最佳源 | 形态 |
|---|---|---|
| 单注 Kelly 尺寸（二元） | guberm/polymarket-bot 公式 §3.2 | f*=(b·p−q)/b，b=(1−px)/px，半 Kelly 封顶 |
| 回撤自适应缩注 | keeks Drawdown-Adjusted Kelly §1.1 | 按 dd/risk 阈值缩乘子 |
| 目标仓位↔报价连续偏移（做市核心） | Hummingbot A-S §4.1 / rodlaf §4.2 | r=mid−q·γ·σ²(T−t)，库存 skew 报价 |
| 库存→spread 轻量线性偏移 | nikhilnd §4.3 | spread 随持仓线性移，最易落地 |
| 限价挂单不追 + 盘口移动才重价 | iceberg §2.2 / mlb-kalshi §2.1 | edge% 领先盘口；阈值移动改价；EV 失效撤 |
| 冰山分片 = 在簿暴露上限 | iceberg §2.2 | 增量=单次最大露险 |
| 分层风控 cap | guberm 6 层 §3.2 / rodlaf 双层 §4.2 | 单仓/单类/总/日 stop/回撤/cooldown |
| Polymarket 链上持仓 merge | poly-maker poly_merger §3.1 | YES/NO 对冲 merge 省 gas/释放保证金 |
| CLV 验证 edge 真假 | mlb-kalshi §2.1 | 概率+ROI 双口径，未下注局也快照 |
| 队列+延迟+库存回测 | hftbacktest §5.3（架构，C++ 重写） | queue-aware fill + 延迟模型 |
| 执行层瞬时公允/逆选 | sstoikov microprice §5.1 + OFI §5.2 | imbalance 调 mid；OFI/PIN 逆选度量 |

**没有任何一个仓库直接 = 我们要的完整系统**（赔率源锚定方向 + 连续目标仓位 + A-S 库存偏移 + reservation 限价不追 + 二元费曲线 + C++ 热路径）。但**拼图齐全**：方向锚定看 mlb-kalshi，限价不追看 iceberg，库存偏移看 rodlaf/Hummingbot，二元 Kelly+风控看 guberm，Polymarket 管道看 poly-maker，回测基建看 hftbacktest（C++ 重写），执行信号看 microprice/OFI。

---

## 对我们的可落地建议（5 条）

1. **抄 guberm 的二元 Kelly 公式 + 6 层风控清单进 C++ RiskManager**：f*=(b·p−q)/b 的 b=(1−px)/px 正是 outcome token 形态，entry_price_buffer 对应 reservation 不追；6 层 cap（单仓/单类/总/日 stop/回撤/cooldown）几乎可直接做我们 cap 体系。**但 p（fair）只能来自 sharp，不来自任何模型**——这是与所有 AI bot 的分水岭，照 mlb-kalshi 的"模型只兜底无否决权"原则隔离。

2. **把 A-S 库存偏移 reservation 改造成"sharp-anchored 版"做我们做市的目标仓位连续控制**：照 rodlaf（二元最对症）/Hummingbot 的 r=center−q·γ·σ²(T−t)，**把 center 从 mid 换成 sharp fair**，q=当前仓−目标仓，σ²(T−t) 用赛程剩余/比分波动近似。MVP 先上 nikhilnd 的"spread 随库存线性偏移"轻量版，再迭代到完整 A-S。

3. **执行层照 iceberg + mlb-kalshi 落"限价不追"循环**：单边挂在 reservation 价、盘口移动超阈值才重挂、edge/EV 被 fee(p(1−p))+spread 吃掉就撤、大单 iceberg 分片控在簿暴露——这套正是我们"reservation 限价不追"的工程范式，直接 C++ 化。

4. **立刻补 CLV 双口径跟踪（照 mlb-kalshi）**：每 N 分钟快照 sharp fair，记 probability-space 与 ROI-space，**对未下注/未进场的盘也快照**做 gap-aware 评估——这是验证"我们的 edge 是否真存在"的金标准，比 paper PnL 更早暴露 alpha 真假（呼应 MEMORY「下一关是回测验证 edge」）。

5. **回测引擎照 hftbacktest 思想用 C++ 重写 queue-aware + 延迟 + 库存**（禁直接引 Rust）：reservation 限价单的 fill 必须建队列位置 + 延迟模型（我们 warm ~14ms 实测可参数化），否则成交假设过乐观、回测骗自己；库存账本与生产共享逻辑（红线：回测/实盘同数据处理）。**Polymarket 特有的 position merge（poly_merger）机制要在持仓/保证金账本里建模**，否则平仓释放保证金算不准。

---

### 速查 URL 清单
- keeks https://github.com/wdm0006/keeks
- Riskfolio-Lib https://github.com/dcajasn/Riskfolio-Lib
- KellyPortfolio https://github.com/thk3421-models/KellyPortfolio
- jpceia/kelly https://github.com/jpceia/kelly
- mlb-kalshi-bot https://github.com/mmoore07129/mlb-kalshi-bot
- iceberg-betting-bot https://github.com/odyssey017/iceberg-betting-bot
- sports-betting (georgedouzas) https://github.com/georgedouzas/sports-betting
- SureBetsBot https://github.com/TessaRichardson/SureBetsBot
- poly-maker https://github.com/warproxxx/poly-maker
- guberm/polymarket-bot https://github.com/guberm/polymarket-bot
- OctagonAI/kalshi-deep-trading-bot https://github.com/OctagonAI/kalshi-deep-trading-bot
- ryanfrigo/kalshi-ai-trading-bot https://github.com/ryanfrigo/kalshi-ai-trading-bot
- Hummingbot A-S https://github.com/hummingbot/hummingbot/blob/master/hummingbot/strategy/avellaneda_market_making/avellaneda_market_making.pyx
- rodlaf/kalshimarketmaker https://github.com/rodlaf/kalshimarketmaker
- nikhilnd/kalshi-market-making https://github.com/nikhilnd/kalshi-market-making
- TimCaron/Market_Maker_Strategies https://github.com/TimCaron/Market_Maker_Strategies
- sstoikov/microprice https://github.com/sstoikov/microprice
- LOB-feature-analysis https://github.com/nicolezattarin/LOB-feature-analysis
- hftbacktest https://github.com/nkaz001/hftbacktest
