# GM 业务需求 must-have 收口 — 老雷 v1

> **Owner:** 老雷 (GM, 兼老板代理人)
> **建立:** 2026-05-28
> **触发:** 用户原话 "更重要的是你需要什么, 而不是看他有什么。 接口可以他来定, 需求可是一定要你提的。"
> **作用:** GM 视角的"我们要什么"权威输入, 给 CPO (老钱) / PM (小杜) / 需求分析师 (小颖) 做 PRD 之前的**正向需求基线**
> **原则:** 不看 Polymarket / Goalserve / 现有代码, 从战略 + 老板视角倒推
> **GM 错 #7 触发文档:** 之前 Wave 21 让前端/UX/PM/性能"反向提需求"仍是看供给侧, 用户纠正后立此 SSOT

---

## 1. 业务本质 (一句话)

**我们要赚钱 — 通过 Polymarket 体育市场, 用 C++ 严谨 + 量化理性, 做可预期的正期望游戏.**

(不是做交易所, 不是做数据平台, 不是做 SaaS — 是**操盘自己的钱**, 赚 PnL.)

## 2. GM must-have (老板视角不可砍)

### M-01 我必须每天能看清今天赚了还是亏了 (P0)

- 不需要复杂, 一个数字: 今日 paper PnL = $X (绿/红)
- 趋势: 7 天 / 30 天 / Sprint / 季度
- 出处: 系统自带, 不需要我登服务器 grep log

### M-02 我必须确信风控没失效 (P0 红线)

- 任何 RM 绕过 → 立刻通知 GM (邮件 / 浏览器 / 声音)
- 所有 reject 决策可复盘 (audit chain hash 不可篡改)
- HALTED / SAFE_MODE 进入时全员告警

### M-03 我必须能复盘任何一笔决策 (P0)

- 给一个 fill / reject, 我必须能拉出完整链路:
  - 触发的 signal (P0-01 哪个 condition?)
  - signal 当时看的 PM book + Pinnacle quote (PIT snapshot)
  - RM 决策 (allow / reject + 原因)
  - signer (paper / live)
  - matcher (filled / partial / canceled)
  - settle (Win / Loss)
- 这是 ML 训练数据的基础, 也是事故追责的基础

### M-04 我必须知道公司 M4.5 解锁了没 (P0)

- 7 hard gate 当前状态一目了然
- 哪个 gate 卡住 + 卡多久 + 还差多少
- paper 跑到 live 的 readiness, 不靠人工拍脑袋

### M-05 我必须能紧急 halt 整个系统 (P0)

- 一个按钮, 二次确认, 立刻 RM HALTED
- 不需要 ssh / 不需要部署
- 真 halt 后, 已 open position 怎么处理 (自动 close? 等到期? 操盘手手动?) — **这是产品决策**, 待 CPO 拍

### M-06 我必须能看到策略在赚什么钱 (P1)

- 分拆: 哪个 signal 贡献多少 PnL
- 分拆: 哪个 sport / market type 赚 / 亏
- 分拆: maker vs taker
- 这是策略迭代的依据 (alpha decay 检测)

### M-07 我必须看到系统健康度 (P1)

- WSS 连接状态 (跨洋链路是公司命脉)
- 决策 latency p99 (端到端 / 各阶段)
- WAL fsync 频率 + 积压
- vCPU 利用率 (7 核分配是否健康)

### M-08 我必须能换信号配方 (P1)

- 调 Kelly fraction (0.25 默认, 想试 0.15 / 0.30)
- 暂停 / 启用某 signal (P0-01 vs 未来 P0-02..12)
- 调 RM 阈值 (在 老韩/老郭联签后, 不允许 GM 单方面调)

### M-09 我必须能"操盘手不在岗" (P2)

- 7×24h 自动运行, 不需要操盘手值守
- 异常自动 SAFE_MODE (不需要人决断"是否暂停")
- 但**重要决策**必须 escalate 到操盘手 (strategy decay / 大额 reject 流)

### M-10 我必须能给投资人看 (P2)

- 一键导出: 季度 PnL + Sharpe + DD + 笔数 + 净收益
- PDF / CSV 都行
- 内部数据不出公司, 但**业绩可独立第三方审计**

---

## 3. GM must-NOT-have (老板视角不要的)

### N-01 复杂的 UI dashboard (不要 fancy)

- operator 是专业操盘手, 不是普通用户
- 不需要拖拽 / 不需要美化主题 / 不需要 mobile responsive
- 黑底白字 + 关键数字大字号即可

### N-02 多用户多租户 (不要 SaaS 化)

- 这是公司自己用, 不卖给别人
- operator / GM / analyst 3 个 role 就够
- 不要 RBAC 复杂权限矩阵

### N-03 自动调参 (M4.5 前不要)

- ML 不进生产 (ML-R5)
- if-else 起步, 人工调参 + 复盘
- M4.5 后再说 online learning

### N-04 跨平台多链 (不要)

- 只做 Polymarket
- 只做体育
- 只做 Polygon 链 (M4.5 后)
- 任何"扩展到 X 平台" / "扩展到 Y 资产类" 提案在 M5 之前一律拒绝

### N-05 实时 < 1ms 决策 (M4.5 前不要)

- 我们不是高频做市商 (HFT 不需要)
- 跨洋链路 200ms RTT 是现实
- 端到端 50ms 已足够 (远低于 HFT us 级)

---

## 4. 给 CPO (老钱) / PM (小杜) / 需求分析师 (小颖) 的指令

**你们 PRD / user story / 验收标准 写作时必须先看本文件, 然后:**

1. **老钱 (CPO)** 写 `business-needs-v1.md`: 从公司战略 (mission + 北极星 + M1/M4.5/M5) 出发, 列业务能力清单 (business capabilities), **不能引用任何已有代码 / endpoint / 模块**. 业务能力包括但不限于: 价格发现 / 信号生成 / 风控决策 / 订单执行 / 持仓管理 / PnL 核算 / 结算对账 / 审计回放 / 性能监控 / 异常处理 / 多用户协同.

2. **小杜 (PM)** 写 `prd-v2.md`: 4 个 user role (Operator / Analyst / GM / 未来 Trader) 各自 user journey + use case. **每个 use case 必须以"用户想做什么"开头**, 不能以"系统能做什么"开头. e.g. ❌ "查询 risk_audit.wal" ✅ "复盘昨天 14:32 那笔被拒的订单为什么".

3. **小颖 (需求分析师)** 写 `acceptance-spec-v1.md`: M1 / M4.5 / M5 各 stage 验收标准, 从本文件 M-01 ~ M-10 倒推. 每个 acceptance criteria 必须 testable + measurable.

**禁忌:**
- 不准看现有的 `include/stcpp/risk/risk_gateway.hpp` 等代码
- 不准看 Polymarket / Goalserve 官方文档
- 不准看 `docs/RESEARCH/` 现有 80+ 报告
- 只看本文件 + CLAUDE.md + AGENT.md

如果你们写出来的需求与现有代码 / 接口契约对不上:

1. **第一步: 双方协商** — 需求方解释为什么这么要 (业务价值), 工程方解释约束在哪 (latency / 复杂度 / 风险). 各退一步是常态.
2. **第二步: 协商有结论** — 入 ADR (架构决议记录), 双方签字, owner 接 patch.
3. **第三步: 协商不下** — 升级到老雷 (GM), GM 拉**全体会议**, 全员投票/共识 + GM 拍板.

**用户原话 (2026-05-28 verbatim):** "当然接口契约也是需要商量的，不全是听需求方的，可以协商。 双方协商不定的就开全体会议一起商量。"

**禁忌:**
- 需求方不能"以战略名义"碾压工程约束
- 工程方不能"以延迟 / 复杂度名义"拒绝业务需求
- 双方僵持 > 48h 必须 escalate, 不允许私下消极怠工

---

## 5. GM 自检

**老雷 W3-W4 反思 (错 #7):**
即使 Wave 21 派"反向提需求"也仍然让小苏/小尤/老李/老姜从"后端已经落地的代码"出发反推. 用户立刻识别"还是在看供给侧". 真正的需求收口必须**不看任何技术供给侧** — 这一条以前没人提过, GM 自己一开始没意识到, 用户兜底.

写到 `docs/INCIDENTS/gm-self-mistakes-log.md` 错 #7.

---

**最后更新:** 2026-05-28 by 老雷
**下次 review:** Wave 22 三 sub-agent (老钱/小杜/小颖) 交付后, GM 整合 + 与现有代码做 gap 分析
