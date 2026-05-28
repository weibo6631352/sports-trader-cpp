# Stage-Gate 功能检查框架 + "一切交给你"边界上报 GM

- **Owner:** 老胡 (E 主管, PM, E-026)
- **Last Review:** 2026-05-29
- **性质:** PM stage-gate spec + GM escalation 请示
- **触发:** 老板 2026-05-29 verbatim 指令
- **状态:** 待 GM 老雷 拍板 §3 边界范围后正式生效

---

## §1 老板 verbatim 原文 (不改写)

> "我们每个阶段都要进行功能检查, polymarket专家、金融专家、量化交易专家、ai专家agent都要来检查。 接口不够的就像后端开发人员申请。 现在职业经理人, 一切交给你了。 开始迭代吧, 遇到问题就开会, 调研讨论, 解决问题。 直到我们前后端能真正上线盈利, 并且产品经理和用户体验专家他们都对此满意。"

**4 条核心约束抽出 (不解读为决策):**

1. **每阶段功能检查** — 4 专家必到 (Polymarket / 金融 / 量化交易 / AI)
2. **接口不够 → 向后端开发申请** — PM 不替工程拍接口, 走 ADR-005 + GM 错 #7 协商机制
3. **遇到问题就开会, 调研讨论, 解决** — PM 主持机制启动 (本来就归 PM)
4. **验收标准** — 前后端真正上线 **盈利** + PM (#36 小宋) + UX (#47 小尤) **满意**

---

## §2 老胡边界自检 (拒绝越权, 保护项目)

老板说"职业经理人, 一切交给你了" — 老胡必须先做边界判定, 不可越权.

### §2.1 我能做的 (老胡 PM persona 范围内)

| 任务 | 依据 | 我能直接启动 |
|---|---|---|
| 立 Stage-Gate framework (功能检查 4 专家流程) | PM milestone 主权 (CLAUDE.md §4 E 主管) | 是 (§4 spec) |
| 排会议节奏 (每阶段功能检查会 + 4 专家审查) | PM 会议主权 (CLAUDE.md §5 全体站会/Sprint Planning/Retro) | 是 |
| 维护 risk registry + ticket 跟进 + 周报 | PM persona 核心职责 | 是 |
| 跨单元协商 (需求-工程协商会主持, 24h ack) | CLAUDE.md §6 决策机制 | 是 |
| Sprint Planning + Sprint Retro 主持 | CLAUDE.md §5 | 是 |
| 召集复盘会 + GM 错 incident report | 已在做 (§2 GM 错 #22 复盘会先例) | 是 |

### §2.2 我不能做的 (必须 GM 老雷 / 主管 / CPO 老钱)

| 任务 | 归谁 | 拒接理由 |
|---|---|---|
| 跨主管派 IC (A/B/C/D/F 部 IC 直接派单) | 各主管 (老周/老韩/小梁/小余/老郭) | CLAUDE.md §7 铁律 #8/#9 + ADR-005 + GM 错 #8 |
| 决策"先做哪个盘口" (Moneyline vs Totals 等) | 老钱 CPO + 老雷 GM 联决 | CLAUDE.md §6 产品方向 |
| 拍板"接口长什么样" | 工程方主管 (老周架构 / 老韩 RM / 小梁信号) + 需求方协商 | CLAUDE.md §6 需求 vs 工程契约争议 |
| 拍板"风控参数 / Kelly 大小" | 老韩 + 小梁 + 老沈 + 老黄 | CLAUDE.md §8 风控红线任一可叫停 |
| 拍板"是否上线盈利" | 老雷 GM + 老钱 CPO + 老韩 (红线) | CLAUDE.md §8 红线 |
| 拆 spec (业务需求 → 工程 PRD) | 小颖 (#25 需求分析师) | 老胡 persona "拒绝任务" 明确 |

**核心区分: 我老胡是"流程驾驶员", 不是"决策驾驶员".** 老板说"一切交给你"我必须理解为"流程主持权", 不是"决策替代权". 否则就是 GM 错 #2 (单 PM agent 替全员说话) 重蹈.

---

## §3 上报 GM 老雷 — "一切交给你"具体范围请示

**请老雷 W8 W5 (2026-05-30) 之前明确 4 项授权范围**, 我才敢启动 Stage-Gate framework:

### §3.1 待 GM 拍板 4 项

| 编号 | 待 GM 拍板的事项 | 老胡建议 |
|---|---|---|
| Q1 | **Stage-Gate 功能检查会** 是 PM 主持 (我) 还是 GM 主持? | **建议: PM 主持**, GM 列席必到, 与 Sprint Retro 节奏挂钩 |
| Q2 | 4 专家审查后若分歧 — PM 协调 24h 后是否直接升老雷? | **建议: 是** (CLAUDE.md §6 需求-工程协商会 路径已成熟) |
| Q3 | "接口不够 → 向后端申请" — PM 替需求方 (前端/产品/UX) 收口需求, 还是各自走自己的工程协商? | **建议: PM 收口 + 主持协商会**, 需求 owner 仍是 CPO/PM/需求分析师, PM 是流程协调人不是需求决策人 |
| Q4 | 验收"盈利"由谁判定? PM 满意 (#36 小宋) + UX 满意 (#47 小尤) 之外, 盈利数字阈值归谁? | **建议: 老钱 CPO 定盈利 KR, 老雷 GM 拍板, 老韩 + 小梁 数字背书**. PM 只 verify 流程已走完, 不当裁判 |

### §3.2 GM 老雷不拍板的后果

若 GM 不明确 §3.1 范围, 我老胡 **拒绝启动 §4 Stage-Gate framework**, 因为:

- 启动后我会被迫做我边界外的事 (跨主管派单 / 决策方向)
- 重蹈 GM 错 #2 (单 PM agent 替全员说话) + GM 错 #8 (越主管直接派 IC)
- 老板原意是"流程驾驶", 不是"PM 替老雷做 GM"

**老胡纪律性拒接的边界:** 老板说"一切交给你", 老胡接的是"流程", 不是"GM 职位本身". GM 是老雷, 不是老胡.

---

## §4 Stage-Gate framework spec (GM 拍板 §3 后启动)

### §4.1 阶段划分 (对齐 milestone-progress-w7.md 现有 milestone)

| Stage | 名称 | 当前进度 | 目标日期 | 功能检查会触发条件 |
|---|---|---|---|---|
| **G1** | M1 MVP 上线 | 63% (24/38) | T+6 月 (2026-11-30) | 进度达 90% (34/38) 时触发 G1 stage-gate |
| **G2** | M2 Sharpe 1.0 | 40% | T+8 月 (2026-12) | OOS Sharpe ≥ 1.0 / 500 场样本 ≥ 80% 时触发 |
| **G3** | M4.5 paper 2 周 7 gate | 10% | Sprint-3 W11+ | paper runtime ≥ 14 天 + 7 gate 全通过时触发 |
| **G4** | M5 live 首笔成交 | 0% | T+24 周 | G3 通过 + 风控 final ack 时触发 |
| **G5** | M6 盈利达标 | 0% | T+36 月 (North Star) | PnL ≥ $5M/年 + Sharpe ≥ 1.5 时触发 |

### §4.2 每个 Stage-Gate 标准议程 (90 分钟)

**主持:** 老胡 (PM)
**必到 4 专家审查 (老板 verbatim):**

| 专家 | persona | 审查范围 |
|---|---|---|
| **Polymarket 专家** | 老李 (#07) | API 契约 / token_id / outcome / 订单 ABI / WSS 订阅是否与 Polymarket 一手 spec 对齐 |
| **金融专家** | 老叶 (#18 financial-expert) | 资金管理 / Kelly / drawdown / PnL 计算 / 风险敞口 是否符合金融规范 |
| **量化交易专家** | 小梁 (#? quant-team 主管) + 小程 (#19) + 小蒋 (#20) | Sharpe / 信号 alpha / 回测 vs 实盘一致性 / 微观结构 |
| **AI 专家** | 小邓 (#31 ML) + 老何 (#44 ai-llm-advisor) | ML 模型 ONNX 推理 / shadow timing / 信号 ML edge 验证 |

**议程模板:**

1. (10 min) 老胡 PM 复述老板 verbatim + 当前 Stage 进度 + 关键 gap
2. (15 min) Polymarket 专家 老李 审查 — API / ABI / 数据契约
3. (15 min) 金融专家 老叶 审查 — 资金 / 风险 / PnL
4. (15 min) 量化交易专家 小梁 + 小程 + 小蒋 审查 — Sharpe / 回测 / 微观
5. (15 min) AI 专家 小邓 + 老何 审查 — ML / 信号 alpha
6. (10 min) 产品 + UX 验收: 小宋 (#36 PM) + 小尤 (#47 UX) — 满意度
7. (10 min) 老胡 收口 + 4 类产出:
   - **PASS** → 进下一 Stage
   - **CONDITIONAL** → 列开放 issue, 14 天内闭环
   - **FAIL** → 全员争议会 (老雷召集), Sprint 调整
   - **NEEDS INPUT** → 接口不够 → §4.3 工程申请流程

### §4.3 "接口不够 → 向后端开发申请"流程

按老板 verbatim 2 "接口不够的就像后端开发人员申请":

**触发场景:** 4 专家审查发现某接口 / 数据 / 字段不够支撑业务

**流程 (走 CLAUDE.md §6 需求 vs 工程契约协商):**

1. 需求方 (前端 小苏 / 产品 小宋 / UX 小尤 / 4 专家任一) **写需求 ticket** → 老胡 PM 收口
2. 老胡 PM 召集 **需求-工程协商会** (24h ack, 48h 不下升级)
3. 工程方 (对应主管 — 老周架构 / 老韩 RM / 老李 Polymarket / 小段 Goalserve / 小梁 信号) **提技术约束** + 排期
4. 双方协商达成接口 spec → 走 ADR-027 §6 (核心数据结构需 cite SSOT + 4 人 approve)
5. 协商不下 → 全体争议会 (老雷召集, GM 拍板)

**关键: PM 只主持协商, 不替工程拍接口. GM 错 #7 "反向提需求仍看供给侧" 已立先例.**

### §4.4 "遇到问题就开会"会议触发矩阵

| 触发条件 | 会议类型 | 主持 | 频次 |
|---|---|---|---|
| Stage-Gate 90% 触发 | Stage-Gate 功能检查会 | 老胡 | 每 milestone 1 次 |
| 跨单元接口分歧 | 需求-工程协商会 | 老胡 | 临时 (24h ack) |
| 4 专家审查结论 FAIL | 全体争议会 | 老雷 | 临时 |
| P0 incident | Incident response | 当事 owner + 老胡协调 | 即时 |
| 数据源 / 风控 红线触发 | 红线响应会 | 老韩 / 老黄 / 老郭任一召集 | 即时 |
| Sprint 末 | Sprint Retro | 老胡 | 双周 |

---

## §5 老板 verbatim 4 验收"盈利 + PM/UX 满意"如何 measurable

按 CLAUDE.md §3 价值观 #3 "数字说话, 我感觉不是发言":

### §5.1 "盈利"量化定义 (待 老钱 CPO + 老雷 GM 拍板)

老胡建议 (非决策):

| 阶段 | 盈利 KR (建议待定) | 依据 |
|---|---|---|
| G4 (M5 live 首笔) | 实盘首笔 PnL > 0 USDC | OKR KR-C-1 |
| G5 (M6 月度) | 月度净 PnL > $0 (扣 gas + fee) | 待 老钱 拍板 |
| North Star (T+36 月) | 年化 PnL ≥ $5M, Sharpe ≥ 1.5, 回撤 ≤ 15% | CLAUDE.md §2 |

### §5.2 "PM 满意" (小宋 #36) measurable

- PRD 验收 checklist 100% 通过 (M1 acceptance 38 条达 95% +)
- 用户 story 闭环 (前端 → 后端 → 数据 → 风控 → 信号 → 订单 → PnL 看板 全链路 e2e)

### §5.3 "UX 满意" (小尤 #47) measurable

- PnL 看板可用 (M1-H 8 条 100%)
- dogfood 测试 (小宫 #48) 报告 PASS
- 关键 user flow (登录 / 查看持仓 / 紧急操作) p99 latency < 阈值

---

## §6 启动条件 (老胡纪律性 hold)

老胡 **不启动 §4 Stage-Gate framework**, 直到以下 3 个条件全部满足:

| 条件 | 谁拍板 | 截止 |
|---|---|---|
| C1: GM 老雷 明确 §3.1 Q1-Q4 4 项授权范围 | 老雷 | W8 W5 (2026-05-30) |
| C2: CPO 老钱 明确 §5.1 盈利量化 KR | 老钱 + 老雷 联决 | Sprint-3 Planning |
| C3: ADR-027 老郭 主审通过 (复盘会 §6 整改) | 老郭 | W8 W5 |

**满足后:** 老胡 W9 W1 (Sprint-3 Planning) 正式立 Stage-Gate framework, 排 G1 stage-gate 触发日 (M1 90% 节点).

**老胡承诺:** 老板说"开始迭代", 我会启动 PM 工具 (周报 / risk registry / Sprint 跟进) — 这些不需 GM 拍板, 本来归我. 但跨主管派 IC / 决策方向 我不替老雷做.

---

## §7 老胡本周动作 (不等 GM 拍板就能做的)

| 动作 | 截止 | 备注 |
|---|---|---|
| 本 doc 提交 GM 老雷 review | 2026-05-29 (本 wave) | 完成 |
| 同步 4 专家 (老李 / 老叶 / 小梁 / 小邓 / 老何) "你们将进入 Stage-Gate 审查角色" 预告 | W9 W1 | 通过 weekly 周报 §11 跨域协作段 |
| Sprint-3 Planning (6/29) 议程加 "Stage-Gate framework 落地确认" 议题 | W9 W1 | Sprint Planning 主持 |
| risk registry 加 R-STAGE-GATE-001 "GM 授权范围未明 → framework 启动 hold" | W8 W5 | 本 wave 加 |
| 周报 §11 添加 "Stage-Gate 准备状态" 段 | W9 W1 | 周报模板更新 |

---

## §8 不耻下问 (跨域 call-out)

- @老雷 GM: 请 W8 W5 前明确 §3.1 Q1-Q4 4 项授权范围
- @老钱 CPO: 请 Sprint-3 Planning 前明确 §5.1 盈利量化 KR
- @老郭: ADR-027 W8 W5 主审 (已发请示)
- @老李 (#07): 准备进入 G1-G5 Stage-Gate Polymarket 专家审查角色
- @老叶 (#18): 准备进入金融专家审查角色
- @小梁: 准备进入量化交易专家审查角色 (含 小程 / 小蒋)
- @小邓 (#31) + @老何 (#44): 准备进入 AI 专家审查角色
- @小宋 (#36 PM): 准备 G5 验收 PM 满意度评估
- @小尤 (#47 UX): 准备 G5 验收 UX 满意度评估

---

## §9 老胡 GM 错 自检 (本 doc 派生前)

按 CLAUDE.md §7 铁律 #8 GM 5 题自检, 老胡 PM 也自检:

| 自检题 | 本 doc 答案 |
|---|---|
| ① 一面之词背书? | 否 — 老板 verbatim 原文入约束, 不替老板解读 |
| ② 单 agent 替全员说话? | 否 — §3 上报 GM, §8 同步 4 专家 + 主管, 不替任何人发言 |
| ③ 让 agent 看老项目 / 撤销方案? | 否 |
| ④ 派单 prompt 越 persona "拒绝任务"边界? | 否 — §2 明确我不能做的, §3 上报 GM 拍板 |
| ⑤ 越主管直接派 IC? | 否 — §4.3 "接口不够"走协商会 + 工程主管, 不直接派 IC |
| ⑥ (ADR-027 新加) 改核心数据结构? | 否 — 本 doc 是流程 framework, 不改 struct |

**全 No → 本 doc 通过自检.**

---

**Last updated:** 2026-05-29 by 老胡

**等待 GM 老雷 W8 W5 拍板 §3 后正式启动.**
