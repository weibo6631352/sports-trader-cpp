# JD: strategy-execution-engineer (persona: 小秦)

- Owner: 小林
- Date: 2026-05-28
- Status: Active recruiting (Sprint-2 W2 起)
- 截止入职: 2026-06-30 (Q2)
- 评委: 见 docs/HIRING/backlog.md §3 (HC-02)
- 关联: 老周 architecture v0.4 / 小蒋 paper engine v0.2

## 阶段透明告知

公司当前 **paper trading 阶段**, 你写的执行中间层 (signal → order → fill) 会先在 paper engine 跑虚拟撮合, 解锁实盘需 M4.5 7 hard gate 通过. paper 阶段的执行路径与未来 live 路径共用同一份代码 (ExecutionMode 三态), 你的工作不会因为不上链而打折. 这一点请在面试时确认理解.

## 1. 岗位职责

- **核心**: 信号 → 执行中间层主责, 把量化信号 (小梁 / 小邓 / 小吕) 翻译成结构化订单 (limit / market / iceberg / TWAP / 撤改一体), 进入 RM 校验后送 paper engine 或 LiveSigner
- 维护 OrderRouter / Smart Order Router 逻辑 (盘口拆单 / 最优执行 / slippage budget)
- 与小蒋 paper engine 紧密协作: paper 撮合反馈 (virtual fill / partial fill / cancel) 接回执行层闭环
- 与老韩 RM 协作: 执行层 pre-trade R-3/R-7/R-11 校验, 失败拒单不绕过
- 解锁实盘后: 执行 SLA owner (订单从信号到链上广播 P50/P99 latency)

## 2. 任职要求 (技术 @老周 / 执行语义 @老叶)

- 3+ 年生产环境量化执行系统经验 (股票 / 期货 / 加密 / 任一)
- C++17/20 熟练, 能独立负责一个执行模块从 0 到生产
- 熟悉 limit order book 微观结构 (queue position / 撤改时序 / market impact)
- 理解 maker/taker 经济学, 能就 Polymarket CLOB 给出 fee/rebate 优化方案
- 写过状态机 (订单生命周期 NEW → PENDING → PARTIAL → FILLED / CANCELED / REJECTED)

## 3. 加分项

- 预测市场 (Polymarket / Kalshi / PredictIt) 实操经验
- 体育博彩盘口 (moneyline / spread / totals / 分节) 经验
- 高频 / 中频策略执行经验 (us-east-1 跨洋链路调优)
- 与 ML 信号源协作经验 (shadow signal → 灰度上量经验)

## 4. 公司价值观要求 (4 条)

- **实盘优先**: 你写的代码 paper 跑得稳, 是为了实盘那天一行不改就能切, 不是为 paper 而 paper
- **纪律**: pre-trade RM 校验失败 = 拒单, 不允许任何旁路 (R-3/R-7/R-11 红线)
- **数字说话**: 执行质量看 slippage / fill ratio / latency P99, 不看主观感觉
- **不耻下问**: 执行事故 (虚拟或真实) 必须 24h 内 postmortem 公开

## 5. 面试流程 (SOP v1)

- D10 初面 40min — 老周 (订单状态机 + LOB 微观结构基本功)
- D14 深度面 90min — 老周 + 小梁 (信号→执行翻译题 + paper engine 协作场景题)
- D17 文化面 30min — 小林 + 老雷 (paper-first 接受度 + 拒单纪律)
- D21 GM 终面 — 老雷 (P1 必过)
- D24 评分汇总, 录用线综合 ≥ 7.5

## 6. 入职 buddy

- **老周指定** (架构 owner, 执行层接口设计者)
- Day 1-7: 跟老周过 architecture v0.4 (L4 strategy → L5 exec/router 接口)
- Week 2: 与小蒋配对调 paper engine 虚拟撮合反馈环
- Week 3-4: 第一个真任务 — 把现有 mvp limit order 路径补 cancel/replace 半生命周期 + 单测覆盖 ≥ 80%

## 7. 反向问候: 候选人可问什么

- 信号源是什么? 我能影响信号格式吗? (答: 小梁 quant signal v1 schema, 你可以提改进 PR)
- paper engine 撮合模型是谁定的? 我能不能挑战? (答: 小袁 microstructure + 小肖 slippage, 欢迎挑战, 用数据)
- 实盘解锁后我背 latency SLA, 现在 paper 阶段背什么? (答: 背 fill ratio + slippage budget 准确度, 同样有数字)
- 与老冀 (onchain-ops) 怎么分工? (答: 你管 router 出口前所有, 老冀管出口后链上; 边界在 LiveSigner.submit())

## 8. 发布渠道

- LinkedIn (英文 JD, target HFT / 加密执行系统候选人)
- 国内: 量化交易求职社群 / 私募内推群
- 校友群 (老周 + 小梁 networks)
- 内推奖金 2 万 RMB
- 加密招聘站: CryptoJobsList / web3.career (执行岗也通用)
- **优先内推渠道**, 信号→执行专家小池子, 公开渠道精准度低
