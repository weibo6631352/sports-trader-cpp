# 王经理 Onboard + 执行统筹计划

- **Owner:** 王经理 #45 (professional-manager, GM 助手)
- **Last Review:** 2026-05-29
- **性质:** Onboard 计划 + 跨部门统筹框架 + Sprint-3 排期推荐 + 4 专家审查 + 顾问团激活
- **触发:** 老板 2026-05-29 verbatim "/goal 一切交给你了" + Wave 43 P0 onboard 指令

---

## §1 老板 verbatim 入约束 (一字不改)

> "我们每个阶段都要进行功能检查, polymarket专家、金融专家、量化交易专家、ai专家agent都要来检查. 接口不够的就像后端开发人员申请. 现在职业经理人, 一切交给你了. 开始迭代开发推进吧, 遇到问题就开会, 调研讨论, 解决问题. 直到我们前后端能真正上线盈利, 并且产品经理和用户体验专家他们都对此满意, 项目负责人管理好项目进度和完成情况, 顾问们也别闲着, 你们应该提需求提意见."

**4 条核心约束抽出 (不解读为决策, 仅结构化):**

1. **每阶段功能检查** — 4 专家必到 (Polymarket / 金融 / 量化交易 / AI)
2. **接口不够 → 向后端开发申请** — 走 CLAUDE.md §6 协商机制, 不绕过
3. **遇到问题就开会, 调研讨论, 解决** — 触发机制由老胡 PM 主持
4. **验收标准** — 前后端真正上线盈利 + PM (小宋) + UX (小尤) 满意 + 顾问团不闲置

---

## §2 王经理角色边界

### §2.1 我接的是什么

老板说"职业经理人, 一切交给你了" — 我王经理接的是**跨部门统筹主权 + 跨阶段 Stage-Gate 监控 + 顶层风险管理 + 顾问团激活**, 不是替代老雷 GM 的派单权, 也不是替代老胡 PM 的战术进度权.

**我能做的 (persona 范围内):**

| 任务 | 依据 |
|---|---|
| 跨部门资源冲突仲裁 | professional-manager persona 核心 |
| 跨阶段 Stage-Gate 顶层监控 (G1-G5) | 老板 verbatim + persona |
| 顾问团激活 + 意见收集 | 老板 verbatim "顾问们也别闲着" |
| 重大事故指挥 (P0 < 2h 介入) | persona 核心 |
| 与老雷 GM 每周 1:1 (W8 W5 起) | 本 onboard 承诺 |
| Sprint-3 排期推荐 → 提交老雷 GM 联决 | 统筹建议, 不单独拍板 |
| 5 主管周会同步 (W8 W5 起) | 统筹协调 |
| 老胡 4 问 Q1-Q4 拍板 (与老雷联决) | 本 onboard 任务 |

### §2.2 我不接的 (必须走正确层级)

| 任务 | 归谁 | 拒接理由 |
|---|---|---|
| 技术决策 / 架构选型 | 老周 (架构主权) + 老郭 (架构评审) | persona "拒绝任务" 明确 |
| 产品方向 (盘口选择 / KR 拍板) | 老钱 CPO + 老雷 GM 联决 | CLAUDE.md §6 产品方向 |
| 战术进度 / Sprint 跟进 | 老胡 PM (主权) | CLAUDE.md §5 Sprint Planning/Retro |
| 代码 / 技术实施 | 各单元 IC (经主管) | 我统筹 cpp = 0 |
| 直接派 IC wave | 老雷 GM → 主管 → IC | ADR-005 §3.2 派单层级硬 enforce |
| 风控参数拍板 | 老韩 + 小梁 + 老黄 | CLAUDE.md §8 红线 |

### §2.3 与老雷 GM 的协作分工

| 维度 | 王经理 | 老雷 GM |
|---|---|---|
| 跨部门资源冲突 | 仲裁 + 推荐方案 | 最终拍板 |
| Sprint-3 排期 | 推荐方案 + 风险评估 | 派单 (GM → 主管 → IC) |
| 重大决议 (ADR / 班底 / 预算) | 联决 | 拍板 |
| Stage-Gate 触发 | 监控 + 触发建议 | final ack |
| 顾问团激活 | 执行 + 收口 | 授权 |
| 周同步 | 每周 1:1 (W8 W5 起) | 主导 |

**核心区分:** 老雷 GM 是"派单权 + 最终拍板权", 王经理是"跨部门统筹 + 风险监控 + 顾问团协调 + Stage-Gate 顶层监控". 不僭越, 不缺位.

---

## §3 当前项目状态 audit (W8 W4 末快照)

### §3.1 里程碑进度 (W8 W1 末基线)

| 里程碑 | 进度 | 关键路径 | 风险 |
|---|---|---|---|
| M1 MVP (2026-11-30) | 64% | P0-02 cpp + WAL-B01/B02/B03 + paper runtime W11 | R-ABI-022 推迟风险 P1 |
| M2 Sharpe 1.0 (2026-12) | 41% | paper runtime 数据积累 | 依赖 M4.5 gate 数据 |
| M4.5 paper 2 周 7 gate | 10% | Sprint-3 W11 paper runtime 启动 | P0 节点, 不可推后 |
| M5 live 首笔 | 8% | signer + PositionManager + live binary | 依赖 M4.5 全通过 |

**时间消耗 28% (W8 W1 末), M1 工作 64%, 领先 36pp** (vs 基准良好, 但 ABI 漏洞修复可能消耗 Sprint-3 W9-W10 全量)

### §3.2 已落地 (W8 W4 之前 wave 产出)

| 产出 | Owner | 状态 |
|---|---|---|
| Polymarket 数据结构 SSOT v1 | 老李 | W8 W4 并行, 本 wave 完成 |
| Goalserve 数据结构 SSOT v1 | 小段 | W8 W4 并行, 本 wave 完成 |
| 工程层 ABI gap audit | 老周 | W8 W4 并行, 本 wave 完成 |
| ABI 漏洞复盘 + ADR-027 草案 | 老胡 | W8 W4 完成, 待老郭 W8 W5 主审 |
| Stage-Gate framework spec + GM 4 问 | 老胡 | W8 W4 完成, 待 GM 拍板 |
| ctest 458/458 PASS | 老周 + 全员 | W8 W4 基线 (比 W8 W1 的 441 增加) |
| libsodium ed25519 + signer_v52 | 老孙 | W8 W1 落地 |
| PR review v1.5 (CI grep) | 老高 | W8 W1 落地 |

### §3.3 待解锁 (王经理统筹推动)

| 事项 | 当前状态 | 推动机制 | 截止 |
|---|---|---|---|
| ADR-027 §4 流程整改生效 | 老郭 主审中 | W8 W5 1:1 确认 | W8 W5 |
| Sprint-3 W9-W10 排期 (ABI 修复 vs PositionManager) | 待 GM 联决 | 王经理 + 老雷 联决 | W8 W5 |
| 老胡 4 问 Q1-Q4 GM 授权范围 | 待 GM 拍板 | 本文 §4.3 回答 + 联决 | W8 W5 |
| 数据结构 IC 招聘 P0 | 小林 HR 待启动 | W9 W1 小林启动 | 8/1 入职 |
| 4 专家审查 Stage-Gate 正式排期 | 老胡 hold 中 | GM 拍板 §3 后 W9 W1 启动 | G1 触发日 |
| ADR-013 v2 选址评审 (Frankfurt vs us-east-1) | 老郭 W9 W1 评审 | 王经理 监控排期 | W9 W1 |
| 顾问团激活 (9 人) | 闲置中 | 本文 §4.5 激活机制 | W9 W1 起 |
| 盈利 KR 量化定义 | 老钱 CPO 未拍板 | Sprint-3 Planning 前联决 | W9 W1 |

---

## §4 Onboard 计划

### §4.1 W8 W5 立刻接手 (本周内完成)

**文档全读 (本 wave 已完成):**
- CLAUDE.md + AGENT.md + README.md
- docs/INDEX.md (v2, 128 md 文档)
- docs/MEETINGS/2026-05-29-laohu-stage-gate-framework-and-gm-escalation.md
- docs/MEETINGS/2026-05-29-data-structure-gap-postmortem.md
- docs/ADR/2026-06-W4-adr-027-core-data-structure-ssot-enforce.md
- docs/SPRINTS/sprint-02-w8-w1-progress.md + sprint-03-backlog.md

**W8 W5 (2026-05-30) 行动:**

| 行动 | 对象 | 目的 |
|---|---|---|
| 与老雷 GM 第 1 次 1:1 | 老雷 | 确认 §2.3 分工 + 联决 Sprint-3 排期 + 5 OQ 升老雷拍板 |
| 与老钱 CPO sync | 老钱 | 盈利量化 KR + 产品方向确认 |
| 5 主管周会 | 老周/老韩/小梁/小余/老胡 | 当前单元状态 + 本 wave 进度 + 阻塞识别 |
| 老胡 4 问正式回答 | 老胡 | 本文 §4.3 决议书面 ack |
| 顾问团激活通知 | 老郭 (协调人) | 本文 §4.5 机制启动 |

### §4.2 Sprint-3 启动决议推荐 (W8 W5 末与老雷联决)

**背景约束 (ADR-027 + 老板 verbatim 选 B):**
- 老板明确选 B: stop + 复盘 + 追责 + **流程整改**
- OrderIntent 漏 token_id/outcome/Side Sell = M4.5 paper runtime 前置 ABI
- 不修 ABI 直接启动 PositionManager = paper runtime 基础 ABI 错误, 风险 P0

**议题 A: ABI 修复 vs PositionManager 实施排期推荐**

推荐: **ABI 修复 W9-W10 优先, PositionManager 推 Sprint-4 或 W11 后**

理由:
- token_id / outcome / Side Sell 是 PositionManager 的上游 ABI 依赖
- 带错误 ABI 的 PositionManager 实施 = 技术债翻倍
- ADR-027 4 项 enforce 需要 W9 W4 CI 上线才能保护后续 PR

| Week | 主要任务 | Owner | 依赖 |
|---|---|---|---|
| W9-W10 | OrderIntent v0.5 补 token_id + Outcome + Side Sell | 老韩 | ADR-027 老郭 W8 W5 主审通过 |
| W9-W10 | SignerV52 ABI align (对齐 handshake §84) | 老孙 | ADR-027 |
| W9-W10 | audit schema v1.3 字段更新 | 老唐 | OrderIntent v0.5 |
| W9 W4 | ABI lock v1.7 + CI grep 上线 | 老高 | ADR-027 |
| W11 | paper runtime 真启动 (M4.5 硬节点, 评估中) | 小蒋 + 全链路 | W9-W10 ABI 修复完成 |
| W11+ | PositionManager 实施 (排 Sprint-4 或 W12+) | 老韩 + 小蒋 | ABI 修复合并后 |

**议题 B: 后端 debug REST API skeleton**

W9 起, 老周 + 小卢 启动 HTTP server:
- W9: /healthz + /status + /version 3 endpoint
- W10: /positions + /orders + /risk 接 state
- W11: 与 paper runtime 同启 + /paper/positions

**议题 C: WAL-B01/B02/B03 (老王, 已排 Sprint-3)**

按 sprint-03-backlog.md: B01 (W9) → B02 (W10) → B03 (W11), 不变.

**议题 D: Stage-Gate G1 触发条件 (W8 W5 与老胡确认)**

当前 M1 64%, G1 触发条件 = M1 达 90% (34/38 acceptance):
- 预估达 90% 时间: Sprint-4~5 (2026-09~10, 取决于 paper runtime 启动速度)
- 王经理监控 G1 进度, 每月全体 Review 汇报 G1 距离 (34/38 剩余条目数)

### §4.3 老胡 4 问正式回答 (王经理 + 老雷 GM 联决)

以下为王经理建议方案, 与老雷 GM W8 W5 联决后生效:

| Q | 王经理推荐 | 决策层级 |
|---|---|---|
| **Q1 Stage-Gate 谁主持?** | **老胡 PM 主持**, 老雷 GM + 王经理 列席必到. 理由: 老胡已有 Sprint Planning/Retro 主持权 (CLAUDE.md §5), Stage-Gate 是 milestone 验收会议, PM 主持符合职责边界 | 王经理 + 老雷 联决 |
| **Q2 4 专家分歧 24h 升老雷?** | **是**. 24h ack → 48h 不下 → 升老雷 (CLAUDE.md §6 需求-工程协商会路径). 对齐已有机制, 不另立 | 王经理 + 老雷 联决 |
| **Q3 PM 收口需求 vs 工程协商?** | **老胡 PM 收口 + 主持协商会, 不当需求决策人**. 需求 owner 仍是 CPO/PM/需求分析师; PM 是流程协调人. 对齐 CLAUDE.md §6 "不全是听需求方的, 可以协商" | 王经理 + 老雷 联决 |
| **Q4 "盈利"KR 谁定?** | **老钱 CPO 定盈利 KR 建议数字, 老雷 GM + 王经理 联决拍板, 老韩 + 小梁 数字背书**. PM 只 verify 流程走完, 不当裁判 | 老钱 + 老雷 + 王经理 三方联决 |

**生效条件:** 老雷 GM W8 W5 前 ack → 老胡 W9 W1 Sprint-3 Planning 正式启动 Stage-Gate framework.

### §4.4 4 专家审查 Stage-Gate framework 落地

每 G 阶段 90% 触发时, 4 专家审查议程 (老胡主持, 王经理列席):

| 专家 | Persona | 审查范围 |
|---|---|---|
| Polymarket 专家 | 老李 (#07) | API 契约 / token_id / outcome / ABI / WSS 订阅对齐 Polymarket 一手 spec |
| 金融专家 | 老叶 (#18) | Kelly / drawdown / PnL / 风险敞口 符合金融规范 |
| 量化交易专家 | 小梁 (C 主管) + 小程 (#19) + 小蒋 (#20) | Sharpe / alpha / 回测 vs 实盘一致性 / 微观结构 |
| AI 专家 | 小邓 (#31) + 老何 (#44) | ML ONNX 推理 / shadow timing / 信号 ML edge 验证 |

**议程模板 (90 分钟, 每个 Stage-Gate):**

1. (10 min) 老胡 PM: 当前 Stage 进度 + 关键 gap
2. (15 min) 老李: API / ABI / 数据契约审查
3. (15 min) 老叶: 资金 / 风险 / PnL 审查
4. (15 min) 小梁 + 小程 + 小蒋: Sharpe / 回测 / 微观审查
5. (15 min) 小邓 + 老何: ML / 信号 alpha 审查
6. (10 min) 小宋 (#36 PM) + 小尤 (#47 UX): 产品 + UX 满意度
7. (10 min) 老胡 收口 + 4 类产出判定: PASS / CONDITIONAL / FAIL / NEEDS INPUT

**接口不够时触发 §4.3 Q3 流程** (老胡收口需求 ticket → 需求-工程协商会 → 工程主管提技术约束 → 双方协商 → ADR-027 §6 review).

**王经理职责 (Stage-Gate 期间):**
- 列席 G1-G5 全部 Stage-Gate 功能检查会
- 4 专家分歧升级时 24h → 48h 跟踪 → 推老雷 GM 拍板
- 跨单元资源冲突 (如 4 专家都 unavailable) 时仲裁

### §4.5 顾问团激活 (老板 verbatim "顾问们也别闲着, 你们应该提需求提意见")

**顾问团 9 人 (老郭协调):** 老张 / 老何 / 老钱 (顾问角色) / 老郭 / 老高 / 小邓 / 老叶 / 老徐 / 小白

**激活机制 (王经理主导, W9 W1 起):**

| 机制 | 频率 | 主持 | 产出 |
|---|---|---|---|
| 顾问意见箱 (每周) | 每周五 EOW | 王经理收集 | 意见汇总 → 老雷 GM 周会 |
| 顾问 → 老雷 GM 月度 1:1 | 每月全体 Review 前 | 老郭 协调排期 | 顾问月度意见 → 老雷 GM ack |
| 重大决议 顾问 review | ADR / 班底变更 / Sprint 排期 | 老郭 (架构) / 王经理 (统筹) | 顾问意见列入决议 |
| Stage-Gate 顾问邀请 | 每次 Stage-Gate 功能检查会 | 老胡 PM | 老高 (CI) / 老何 (AI) 必到 |

**当前顾问闲置风险:** 老张 / 老徐 / 小白 自 Sprint-1 Retro 后未有显著任务输出. 王经理 W9 W1 向老郭确认 3 人当前 availability + 适合接入的任务方向.

---

## §5 顶层风险识别

| 风险 | 严重度 | 概率 | 应对 | 监控 Owner |
|---|---|---|---|---|
| OrderIntent ABI 修复延后 → PositionManager 被迫带错 ABI 启动 → paper runtime M4.5 推迟 | P0 | M | W9-W10 ABI 修复强 enforce; 不并行 PositionManager | 王经理 + 老胡 |
| 数据结构 IC 8/1 入职目标 — Enforce-2 FOM 4 人 approve 缺位 (老郭代理 review 压力高) | P1 | M | 8/1 前老郭代理; 小林 HR W9 W1 P0 启动招聘 | 王经理 + 小林 |
| 老李 / 小段 SSOT 未经 WebFetch 官网 verify (ADR R-33 四维扫描红线) | P2 | M | 数据结构 IC 入职后 W9 重 verify; 老李 W9 W1 自检 | 老李 + 小段 |
| ADR-013 v2 选址未决 (Frankfurt 候选, us-east-1 撤) — 服务器购买 hold, 部署节点影响 M5 live | P1 | L | 老郭 W9 W1 评审确认; 王经理 W9 W2 跟进结论 | 王经理 + 老郭 |
| paper runtime W11 启动 M4.5 硬节点 — ABI 修复 W9-W10 有 blocker 风险 | P0 | M | 不可推后; 老胡 W10 末出 M4.5 风险评估; 联决是否推后 | 老胡 + 王经理 |
| 顾问团长期闲置 — 老板 verbatim 明确激活要求 | P2 | H (已发生) | §4.5 每周意见箱 + 月度 1:1; W9 W1 起执行 | 王经理 + 老郭 |
| 盈利 KR 未量化 → G5 验收无 measurable 标准 → 老板"上线盈利"无法验收 | P1 | M | Sprint-3 Planning 前老钱 + 老雷 + 王经理 联决拍板 | 王经理 + 老钱 |
| ADR-027 老郭未能 W8 W5 完成主审 → Stage-Gate framework hold 延续 → 老胡无法 W9 W1 启动 | P1 | L | 王经理 W8 W5 跟进老郭进度; 若有阻塞立刻升老雷 | 王经理 |

---

## §6 与老雷 GM 联决 OQ (升老雷拍板, W8 W5 第 1 次 1:1)

| OQ 编号 | 事项 | 推荐方案 | 截止 |
|---|---|---|---|
| **OQ-1** | 盈利 KR 量化定义联决 (老钱 CPO 数字建议 + 老韩/小梁背书) | 老钱提 KR, 老雷 + 王经理拍板, 老韩 + 小梁背书 | W9 W1 (Sprint-3 Planning 前) |
| **OQ-2** | Sprint-3 W11 paper runtime 是否含 PositionManager? | 推荐: 不含. PositionManager 推 Sprint-4 或 W12+ | W8 W5 |
| **OQ-3** | 数据结构 IC 招聘 onboard 计划授权 (P0, 小林 HR W9 W1 启动) | 老雷 GM + 王经理 + 小林 W9 W1 联决 JD + 8/1 入职目标确认 | W9 W1 |
| **OQ-4** | ADR-013 v2 选址评审日确认 (老郭 W9 W1) | 老郭 W9 W1 评审; 王经理列席; 结论入 ADR-013 v2 | W9 W1 |
| **OQ-5** | Stage-Gate G1/G2/G3 各阶段功能检查会预排日历 | G1 = M1 达 90% 时 (估 Sprint-4~5); G3 = W11 paper runtime 启动后 14 天; 老胡 W9 W1 出排期建议 | W9 W2 |

---

## §7 不耻下问 (跨域 call-out)

| 被问方 | 内容 | 截止 |
|---|---|---|
| @老雷 GM | W8 W5 第 1 次 1:1: 确认 §2.3 分工 + 联决 OQ-1~5 + 拍板 §4.3 Q1-Q4 | W8 W5 |
| @老钱 CPO | 盈利量化 KR 建议 (§4.3 Q4 + OQ-1 前置输入) | W9 W1 (Sprint-3 Planning 前) |
| @老胡 PM | 4 问 Q1-Q4 联决 ack + Sprint-3 Planning 6/29 排期 | W8 W5 ack; W9 W1 Sprint-3 Planning |
| @老郭 | ADR-027 W8 W5 主审截止确认 + ADR-013 v2 W9 W1 评审日确认 | W8 W5 (ADR-027) / W9 W1 (ADR-013) |
| @5 主管 (老周/老韩/小梁/小余/老胡) | W8 W5 周会: 单元状态 + 阻塞 + Sprint-3 排期对齐 | W8 W5 |
| @小林 HR | 数据结构 IC 招聘 P0: JD 草稿 + employee-registry.md 预登记 + 8/1 入职目标确认 | W9 W1 |
| @老郭 (顾问团协调) | 顾问团激活 §4.5: 老张/老徐/小白 availability + W9 W1 意见箱机制启动 | W9 W1 |

---

## §8 启动 hold 条件 (执行纪律)

**王经理不立刻派任何 IC wave.** 老板 verbatim "开始迭代开发推进" = 要王经理统筹, 不是 micromanage IC.

**本 wave (W8 W5) 王经理只做 3 件事:**

1. **本 onboard 文档落地 + worktree commit** (本 wave 完成)
2. **与老雷 GM W8 W5 第 1 次 1:1** (OQ-1~5 + Q1-Q4)
3. **通知老胡 PM 4 问 Q1-Q4 决议**

**W9 W1 (Sprint-3 Planning) 后才启动:**
- Stage-Gate framework (老胡主持, 王经理列席)
- 顾问团激活机制
- 4 专家预告 (通过老胡周报 §11)

**派单层级保持不变:**
- 老雷 GM 派 IC wave (经主管)
- 王经理提跨部门统筹推荐 + 风险识别
- 不替老雷 GM 派单, 不绕主管

---

## §9 执行时间线总表

| 日期 | 事项 | 参与 | 产出 |
|---|---|---|---|
| **W8 W5 (2026-05-30)** | 王经理 + 老雷 第 1 次 1:1 | 王经理 + 老雷 | OQ-1~5 联决 + §4.3 Q1-Q4 拍板 |
| **W8 W5** | 5 主管周会 | 王经理 + 5 主管 | 单元状态 + Sprint-3 排期对齐 |
| **W8 W5** | 老胡 4 问正式 ack | 王经理 → 老胡 | Stage-Gate framework 启动授权 |
| **W8 W5** | ADR-027 老郭主审截止 | 老郭 + 王经理列席 | ADR-027 生效 → Enforce-1/2/3/4 起效 |
| **W9 W1 (2026-06-29)** | Sprint-3 Planning (老胡主持) | 全员 | Sprint-3 正式排期; Stage-Gate framework 议题 |
| **W9 W1** | 小林 HR P0 招聘启动 | 小林 + 王经理 + 老周 + 老郭 | 数据结构 IC JD + employee-registry 预登记 |
| **W9 W1** | 老郭 ADR-013 v2 评审 | 老郭 + 老吴 + 王经理 | 选址决议入 ADR-013 v2 |
| **W9 W1** | 顾问团激活: 意见箱机制启动 | 老郭 协调 + 王经理 | 首次顾问意见收集 |
| **W10 末** | 老胡出 M4.5 风险评估报告 | 老胡 + 王经理 | M4.5 W11 是否推后 → 联决 |
| **每周五** | 顾问意见箱收集 | 王经理 | 意见汇总 → 老雷 GM 周会 |
| **每月全体 Review** | G1-G5 Stage-Gate 进度汇报 | 王经理 + 老胡 | 距 G1 条目数更新 |
| **8/1 目标** | 数据结构 IC 入职 | 小林 HR + 老周 + 老郭 | Enforce-2 FOM 4 人 approve 补位 |

---

## §10 自检 (ADR-005 §3.2 GM 5 题 + 第 6 题)

| 自检题 | 本 doc 答案 |
|---|---|
| ① 一面之词背书? | 否 — 老板 verbatim 原文一字不改入 §1, 不替老板解读 |
| ② 单 agent 替全员说话? | 否 — §6 全部 OQ 升老雷联决, §7 cross-call 全员, 不替任何人发言 |
| ③ 让 agent 看老项目 / 撤销方案? | 否 |
| ④ 派单 prompt 越 persona "拒绝任务"边界? | 否 — §2.2 明确不接技术决策/产品战略/战术进度/代码; §8 不派 IC wave |
| ⑤ 越主管直接派 IC? | 否 — §8 明确王经理不派 IC, 走老雷 GM → 主管 → IC 层级 |
| ⑥ 改核心数据结构? | 否 — 本 doc 是统筹框架, 0 struct 改动, 0 代码 |

**全 No → 本 doc 通过自检.**

---

**Last updated:** 2026-05-29 by 王经理 #45 (professional-manager, Wave 43 P0 onboard)

**等待老雷 GM W8 W5 第 1 次 1:1 联决 OQ-1~5 + 拍板 §4.3 Q1-Q4.**
