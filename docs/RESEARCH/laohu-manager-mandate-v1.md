# 老胡 — E 产品业务保障部主管就职宣言 v1

- **Owner:** 老胡 (E-026, pm-project-manager, E 产品业务保障部主管)
- **Date:** 2026-05-28 (W4 EOW)
- **职位生效:** 2026-05-28 (ADR-005)
- **验收人:** 老雷 (GM) + 小林 (HR)
- **关联:**
  - `docs/ADR/2026-05-28-department-manager-mandate.md` (ADR-005, 5 主管 mandate)
  - `docs/HIRING/employee-registry.md` (花名册 9 人, E-026 主管 + 8 IC + 1 虚线)
  - `docs/HIRING/hr-pulse-check-2026-05-28.md` (E 单元 6/10 绿, 单点风险榜垫底, 最均衡)
  - `docs/RESEARCH/laohu-master-gantt-v1.md` (我自己产出, 全局甘特 v1, M1~M5 时间表)
  - `docs/RESEARCH/laohu-risk-registry-v2.2.md` (我自己产出, 41 风险登记)
  - `docs/HIRING/sprint2-w3-progress.md` (W3 sprint 进度, 我维护)

---

## §1 单元成员清单 (9 人, 含小林虚线)

| # | 工号 | 工号 | persona | 单元 | 职位 | 主要交付 (W1-W4) | 跨域兼任 |
|---|---|---|---|---|---|---|---|
| 1 | E-026 | 老胡 | pm-project-manager | E | **Manager** (本人) | gantt v1 + risk registry v2.2 + W1-W4 周报 + sprint-1 retro 主持 | 跨 5 单元 sprint 协调 |
| 2 | E-025 | 小颖 | requirements-analyst | E | IC | acceptance v1 (121 条 / 25 P0), spec v1 | 与小杜 PRD 双向锁 |
| 3 | E-036 | 小杜 | product-manager | E | IC | PRD v1 (25 P0) + PRD v2 (4 role 12 UC user journey) | CPO 老钱业务能力对齐 |
| 4 | E-028 | 小宋 | test-replay-engineer | E | IC | test framework v0.1 + v0.2 (21 enum × unit/sim/replay/chaos) + replay framework v0.1 | 与老周 §17-19 / 小蒋 paper / 老韩 RM 接口锁 |
| 5 | E-012 | 小苏 | frontend-engineer | E | IC | backend-api requirements v1 (24 REST + 10 WSS + 7 schema) + UI wireframe v0.1 | 与小郑 Grafana / 小尤 UX 协 |
| 6 | E-040 | 小米 | doc-curator | E | IC | docs health v1 + v2 (健康度 A) + R-20 12 篇回灌 | 全单元 docs SSOT 守护 |
| 7 | E-046 | 小林 | hr-talent-manager | E (虚线, 直属老雷) | HR Owner | HR pulse W3 (5 单元评分) + Q3 HC 节奏 + GM 自检 3 题上呈 | GM 直接汇报, 跨单元招聘 |
| 8 | E-047 | 小尤 | ux-experience-evaluator | E | IC | UX framework v1 (3 层 info-arch) + interface requirements v1 (6 心流断点) | 与小苏 / 小宫 / 小杜 链 |
| 9 | E-048 | 小宫 | dogfood-tester | E | IC | dogfood playbook v1 (D1-D10 演练 + 第一次跑通 checklist) | 等 paper engine 上线 (M4 / 小蒋) 后激活 |

**职位分布:** 1 主管 (我) + 7 IC + 1 HR Owner (虚线, 直属老雷). E 单元是 5 单元里**最大 (9 人) 且最均衡 (HR pulse 6/10 绿, 单点风险榜最末位)**.

**身份说明:** 我此前 W1-W4 一直在做 IC PM 工作 (周报 + 风险 + sprint), 现在正式加"主管"hat. 两顶帽子边界 — **主管帽**统筹 8 IC 排队 + 跨单元接口; **IC 帽**继续出 gantt / risk registry / 周报. §3 IC 工作量评估**不含我自己** (主管不评 IC 工作量, 老雷评我).

---

## §2 我作为主管要做的 vs 不做的

### §2.1 Do (8 项)

1. **接 GM 派单 24h 内 ack + 48h 内拆 IC 任务**, 公开排队顺序, 谁压谁让 8 个 IC 看得见.
2. **协商主持 — 我单元的核心增量职能** (ADR-005 §6 决策机制"需求-工程协商会"由我主持): 小颖 acceptance vs 老周 / 老韩 / 小梁 / 小余 4 主管的 spec 出入, W5 起每周二开 30min 协商会, 跑不下来当场升老雷, **不让 IC 自己跨单元扯皮**.
3. **First review IC 产出** — 任何 W5 起 E 单元 IC 出的 spec / playbook / wireframe / acceptance, 我 24h 内一审 (语义 + scope + 跨单元接口标对), 通过后再上老周 / 老郭 / 老雷.
4. **跨主管接口对接** — 与老周 (A) / 老韩 (B) / 小梁 (C) / 小余 (D) 主管周同步每周一上午联动, 替我单元 IC 拿跨域 ack, 24h ack 48h 不下升老雷.
5. **维护 sprint 节奏 + 周报** (这是我作为 E 主管对全公司的统筹职责, 与 IC 帽产出 §3 单列): 每周五 EOD 出 sprint progress, 月末出全体 review 主持议程.
6. **IC 健康度 1:1** — 8 IC weekly 1:1 (15min/人, 每周二三四下午), 与小林 HR pulse 联动 (W3 已发现小宋 P1 红, 见 §3).
7. **HR 联动 + 扩招提请** — 小林 (HR 直属老雷, 我虚线) 周三 HR 招聘进展会, 我单元 HC 需求由我主动提请 (本宣言 §8 提请 HC-07 即为示例).
8. **守 ADR-005 边界 — 不亲力亲为** — 我不写 acceptance / 不写 PRD / 不写 test framework / 不画 wireframe / 不画 UX flow / 不画 dogfood playbook. 这 8 IC 各有专长, 主管帽下我不抢活.

### §2.2 Don't (5 项)

1. **不替 8 IC 写他们专业域的产出** — 不写 acceptance (小颖) / 不重写 PRD (小杜) / 不写 test case (小宋) / 不画 wireframe (小苏 / 小尤) / 不写 dogfood D-N 步骤 (小宫). 主管出 spec 框 + 排队, 不抢活.
2. **不替老雷拍战略 / 不替老钱拍产品方向** — 战略 GM, 产品 CPO. 我跟进度, 不定方向 (persona "拒绝任务"红线).
3. **不一票否决其他单元产出** (除非触动 E 单元红线, 如某 spec 漏了 acceptance / 漏了 PRD 用户视角) — 这种情况升老雷 / 老郭, 我不当场否决.
4. **不写代码** (ADR-005 §2.2: 主管不写代码, 例外架构原型 / 紧急 hotfix < 2h) — 我连原型都不写, hotfix 全升老周 / 老姜.
5. **不替 IC 跨主管单方面承诺工作量** — 例如不答应老周"小宋下周加 5 个 chaos case", 必须先与小宋 1:1 + 我 review 排队顺序再回老周. **不当传话筒**.

---

## §3 单元内 IC 工作量评估 (与 HR 小林 W3 pulse 对齐)

**评分制:** 0-10 (0 闲置, 10 爆肝). 主管不评自己 (老雷评我).

### §3.1 单点详评

**小颖 (E-025) 7/10 黄** — W1-W4: acceptance v1 (1052 行 / 121 条 / 25 P0) + spec v1 (326 行). W5+: M1 38 / M4.5 17 / M5 15 / 永久 11 = 81 项跨 milestone 维护 + PRD v2 翻盘后 acc v2 重做 (4 role × 12 UC). 主管动作: W5 三 (6/3) 1:1, 建议拆 4 PR 滚动.

**小杜 (E-036) 8/10 黄+** — W1-W4: PRD v1 (927 行 / 25 P0) + PRD v2 (600 行 / 4 role / 12 UC, GM 错 #7 自纠后翻盘). W5+: PRD v2 跨单元落地 (acc / UI / UX / dogfood 同步重做) + 与 CPO 老钱业务能力 cross-ref. 主管动作: W5 一 (6/1) 主管周同步把 CPO 对接列议题, **替小杜挡 CPO 直接派单** (CPO 走我主管帽再下小杜).

**小宋 (E-028) 9/10 红 → 见 §8 扩招** — W1-W4: test framework v0.1 (replay 877 行) + v0.2 (312 行, 21 enum × unit/sim/replay/chaos × R-12 4 场景 × R-20 PIT 7 项) + skeleton 298 行, **三本 framework 一周内出**. W5+: 256 测试用例落 (121 acc × ≥ 2 case + chaos + replay) + integration test framework (M3) + UAT (M4.5+), 单人扛 unit + sim + replay + chaos + fuzz + perf + integration + UAT **8 域**. 主管动作: §8 提请 qa-integration-engineer 联签老周; HC 到位前我**主管帽挡掉非 P0 测试派单**, 让小宋只跑 M1 critical path.

**小苏 (E-012) 6/10 绿** — W1-W4: backend-api requirements v1 (399 行 / 24 REST + 10 WSS + 7 schema) + UI wireframe v0.1 (1000 行). W5+: wireframe v0.2 (PRD v2 翻盘后) + Grafana mockup (与小郑 #11 协). 主管动作: W5 二 (6/2) 1:1, Operator role 先做, Analyst / GM / Investor 后做.

**小米 (E-040) 6/10 绿** — W1-W4: docs health v1 (228 行) + v2 (299 行, 健康度 A) + R-20 backfill (77 行, 12 篇时间戳回灌). W5+: 周更 + ADR-005 主管 6 篇 mandate 入 SSOT + employee-registry 跟踪 + MCP filesystem 守护. 主管动作: W5 不加新任务, 守 6 篇主管宣言 SSOT 入库.

**小林 (E-046) 9/10 红 — GM 直管, 不在我帽下** — W1-W4: HR pulse W3 (5 单元评分) + Q3 HC 节奏 (HC-03 提前 8/1) + GM 自检 3 题上呈 + HR-W4 7 项自派. W5+: HC-01/02 6 评委追单 + HC-03 JD 6/5 + 本宣言 HC-08 评审 + Q3 HC-03/04/05/07 滚动. 我不派单, 但 W5 一 (6/1) 与小林对齐"E IC 健康度 + HC-08 联签 + 主管宣言落地"3 议题.

**小尤 (E-047) 6/10 绿** — W1-W4: UX framework v1 (465 行 / 3 层 info-arch) + interface requirements v1 (339 行 / 6 心流断点). W5+: PRD v2 翻盘后 UX flow v2 (4 role × 12 UC × 3 层) + 与小苏 wireframe v0.2 同步. 主管动作: W5 三 (6/3) 1:1, Operator role 先做.

**小宫 (E-048) 4/10 绿 (待激活)** — W1-W4: dogfood playbook v1 (718 行 / D1-D10 + 第一次跑通 checklist), 充实但**未实测** (paper engine 未上线). W5+ 阻塞于 M4 (gantt W18). 主管动作: W5 不加正式 ticket, 派 1 个 backlog "**D1 demo 干跑稿**" — 用小蒋 skeleton 文档假数据走 D1, 找 playbook 漏洞, 不等真 engine.

### §3 总览

| 状态 | 人数 | persona |
|---|---|---|
| **红 (9/10) — 需要扩招** | 1 | 小宋 (申请 HC-07) |
| **红 (9/10) — GM 直管, 不扩招** | 1 | 小林 (HR Owner, 直属老雷, 不在我帽下) |
| **黄+ (8/10) — 需 CPO 联动减压** | 1 | 小杜 (PRD v2 翻盘) |
| **黄 (7/10) — 需 1:1 调度** | 1 | 小颖 (acceptance v2 翻盘) |
| **绿 (6/10) — 健康** | 3 | 小苏 / 小米 / 小尤 |
| **绿 (4/10) — 待激活** | 1 | 小宫 (等 paper engine) |

E 单元整体 HR pulse 6/10 绿 (W3 小林评), 但内部分化明显: **小宋一人扛, 必扩招**.

---

## §4 W5 派单 backlog v1 (主管自己拆, 不让 GM 拆)

### §4.1 E 单元内 IC 派单 (W5: 2026-07-25 ~ 2026-08-01, sprint-5 启动周)

| # | 任务 | Owner | 输入 | 输出 | Deadline | 主管 review |
|---|---|---|---|---|---|---|
| E-W5-01 | acceptance v2 拆 4 PR 滚动 (acc v1 → v2 PRD 翻盘对齐) | 小颖 | PRD v2 + acc v1 (1052 行) | acceptance v2 PR 1 (M1 38 项) | W5 五 (8/1) | 我 24h 一审 |
| E-W5-02 | PRD v2 与 CPO 老钱业务能力 cross-ref | 小杜 | PRD v2 + CPO 老钱业务能力 v1 (W4 并行) | PRD v2.1 cross-ref note (P0 砍 / P1 抬列表) | W5 四 (7/31) | 我 + CPO 联签 |
| E-W5-03 | **integration test framework v0.1 (M3 CLOB 端到端启动)** | 小宋 | RM v0.3.1 + paper engine skeleton + CLOB endpoint matrix v3 | integration test framework v0.1 (skeleton 不含 case) | W5 五 (8/1) | 我 + 老周联签 |
| E-W5-04 | UI wireframe v0.2 (Operator role 先做) | 小苏 | PRD v2 + UX framework v1 | wireframe v0.2 (Operator + Analyst 两 role) | W5 五 (8/1) | 我 + 小尤联签 |
| E-W5-05 | Grafana mockup (与小郑 #11 协) | 小苏 | 小郑 observability v0.1 + RM v0.3 21 reject_code | Grafana mockup 3 张 (RM state / fill 流 / WSS link) | W5 五 (8/1) | 我 + 小郑联签 |
| E-W5-06 | UX flow v2 (Operator role 先做) | 小尤 | PRD v2 + UI wireframe v0.1 → v0.2 | UX flow v2 (Operator role 6 心流断点细化) | W5 五 (8/1) | 我 24h 一审 |
| E-W5-07 | docs 健康度 W5 (周更) | 小米 | W5 全公司新增 docs 扫描 | docs-health-2026-W5.md (维持 A) | W5 五 (8/1) | 我 review |
| E-W5-08 | dogfood D1 demo 干跑稿 (假数据走 playbook) | 小宫 | playbook v1 D1 + paper engine skeleton 文档 | D1 干跑笔记 (找 playbook 漏洞) | W5 五 (8/1) | 我 review |

### §4.2 我主管帽自己出的 (统筹任务, 非 IC 工作)

| # | 任务 | Deadline | 输出 |
|---|---|---|---|
| E-W5-M01 | **需求-工程协商会 W5 一开** (与老周 / 老韩 / 小梁 / 小余 4 主管, ADR-005 §6 决策机制) | W5 一 (7/27) | 协商纪要 + acceptance v2 与 4 单元 spec 接口锁定 |
| E-W5-M02 | W5 周报 (含全 5 单元 sprint 进度 + 风险 + 主管派单覆盖率) | W5 五 (8/1) | sprint2-w5-progress.md |
| E-W5-M03 | 8 IC weekly 1:1 (15min × 8 = 2h) | W5 二三四下午 | 1:1 笔记 8 份 (内部, 不入 docs/) |
| E-W5-M04 | HC-07 qa-integration-engineer JD 与老周联签起草 | W5 五 (8/1) | jd-qa-integration-v1.md (主导者 小林) |
| E-W5-M05 | 主管周同步周一上午 dial-in | W5 一 (7/27) | 跨单元 blocker / 招聘进展 / IC 健康度上报 |
| E-W5-M06 | risk registry v2.3 周更 (R-41/42/43 主管制度 3 风险跟进) | W5 五 (8/1) | risk-registry-v2.3.md |

### §4.3 W5 backlog 总量

- E 单元 IC 派单: 8 个
- 我主管帽统筹: 6 个 (M01-M06)
- **覆盖率自评:** 8/8 IC 都有 W5 任务 (含小宫干跑稿, 不让任何 IC 闲置). 小林虚线不计入我派单.

---

## §5 跨单元接口需求 (24h ack 期望)

### §5.1 我作为协商会主持的特殊角色 (ADR-005 §6)

**全 4 主管: W5 起每周一上午主管周同步 + 周二上午需求-工程协商会** (我主持, 30min). 协商内容:

| 议题 | 我 vs | 期望 ack | 触发场景 |
|---|---|---|---|
| acceptance v2 vs 老周架构 v0.5+ 接口对齐 (小颖 vs 小马 / 老陈 / 小赵 等 IC) | 老周 (A) | 24h | acc 中机器判定字段在架构里是否暴露 |
| acceptance v2 vs 老韩 RM v0.4 21 reject_code (小颖 vs 老韩) | 老韩 (B) | 24h | RM 状态机变更 → acc 同步翻新 |
| PRD v2 4 role 中 Analyst 是否能跨域看 signal × paper data (小杜 vs 小程 / 小蒋) | 小梁 (C) | 24h | Analyst role 需求 → 量化研究产出哪些 dashboard / API |
| PRD v2 4 role 中 Analyst 是否能跨域看 ETL data slice (小杜 vs 小董) | 小余 (D) | 24h | Analyst role 需求 → 数据基建出哪些数据 view |
| integration test (小宋) 跨 CLOB / Goalserve / Polygon RPC chaos 场景 | 老周 (A) + 小余 (D) | 48h | M3 testnet 端到端依赖 |

**协商不下来的硬规则:** 当场升老雷 (不让 IC 自己跨主管扯皮, ADR-005 §4 主管间协商不下走 §6 决策机制升级).

### §5.2 老郭 (F 顾问团协调人) — integration test 与架构评审协同

- **接口:** integration test framework v0.1 (小宋, W5 出 skeleton) 需要老郭架构评审签字 (W6 月度架构评审会议第 1 个周四)
- **期望 ack:** 48h (与老郭周二排议程一致)
- **我的角色:** 我把小宋 v0.1 提前 6/2 (W5 二) 给老郭, 让他周四评审前看一遍

### §5.3 老钱 (CPO 平级 GM) — PRD v2 业务能力对齐

- **接口:** PRD v2 (小杜 W3 出) 与 CPO 老钱业务能力 v1 (W4 并行) cross-ref, 哪些 P0 砍 / 哪些 P1 抬
- **期望 ack:** 48h
- **我的角色:** 我替小杜挡 CPO 直接派单 (CPO 派单走我主管帽), W5 一 (6/1) 主管周同步把这议题摆上桌

---

## §6 主管 KPI 自评 (基于 ADR-005 §2.2 主管职责 6 条)

| KPI | 自评 | 证据 | W5+ 改进 |
|---|---|---|---|
| 接 GM 派单 24h ack + 48h 拆 IC | **3/5** | W4 GM 派单还在直接命中 IC (小颖 / 小杜 / 小宋 / 小苏 / 小尤 / 小宫), 我主管帽刚立 (ADR-005 2026-05-28 才立), W4 历史不能追责 | W5 起 GM 派 IC 我 4h 内截图归我帽, 重派 |
| 单元内排队 + 优先级 | **4/5** | gantt v1 + risk registry v2.2 + W1-W4 sprint progress 都是我出, 排队顺序公开 | W5 加每周五五 EOD 主管派单覆盖率公示 |
| review IC 产出质量门禁 | **3/5** | W1-W4 IC 产出 (acceptance / PRD / test framework / wireframe / playbook) 我未做 first review (直接上 GM ack), 主管制度未立 | W5 起 24h 一审硬约束 |
| 跨单元接口对接 | **2/5** | W4 IC 之间跨单元扯皮 (小苏 backend-api vs 小郑 observability, 小颖 acceptance vs 老韩 RM 21 enum) 我未介入 | W5 起协商会硬主持 |
| KPI + 1:1 + 培养 | **2/5** | W1-W4 我未做 1:1 (小林 HR pulse 替代了一部分, 但 1:1 是主管职责) | W5 起 weekly 1:1 8 IC × 15min |
| 不亲力亲为 | **4/5** | W1-W4 我自己只产出 gantt / risk registry / sprint progress (本就是 PM 工作), 没抢 IC 活 | W5 起继续守住, 但 §3 IC 工作量 → §4 派单 backlog 是新增主管职责 |

**总分 18/30 (60%)** — 主管帽 W4 末才戴, W5 起每项必涨 1 分. **W7 sprint 中复评目标 24/30 (80%)**.

---

## §7 W5 GM 派单约定 (我替老雷把 4 题自检守住)

W5 起 GM 派 E 单元任何任务, **走我主管帽**, 不直接命中 IC. 例外 ADR-005 §3.2:
- 顾问团 / 紧急 P0 / GM 给我派单 (派给主管不算越级) / 跨多单元统筹 (PM 周报 / 架构评审 / PR review)

GM 派单 5 题自检 (含 ADR-005 §3.3 升级版第 5 题) 与 E 单元的关联:
1. **一面之词背书?** — E 单元小杜 PRD v1 已是 1 agent 背书 P0 25 项 (GM 错 #7 教训), 我 W5 主管帽 review 时强制 4 agent 联签 (小颖 acc + 小苏 UI + 小尤 UX + 小宫 dogfood)
2. **单 agent 替全员说话?** — E 单元 sprint retro 必走 8 IC 真发言 (W3 已立 retro Phase 1 + 4 真发言机制, sprint1-retro 已落)
3. **让 agent 看老项目 / 撤销方案?** — 我自己派单 prompt 不引老项目 (新 ground-up C++), CI grep 红线由小米守
4. **派单 prompt 越 persona "拒绝任务"边界?** — 我自己 persona 拒绝战略 + 拒绝 spec 拆 (本宣言开头声明), GM 派我"决方向"我必拒
5. **越主管直接派 IC?** — **W5 起若 GM 派 E IC 跳过我, 我 4h 内主管帽接管 + 通报老雷**

---

## §8 HR 扩招建议 (E 单元提请, 联签老周)

### §8.1 HC-07 qa-integration-engineer (新增, 与 HR pulse Q3 HC-07 market-data QA 不同岗)

| 字段 | 内容 |
|---|---|
| **岗位** | qa-integration-engineer (集成测试 + endpoint test + UAT 主导) |
| **背景** | 小宋 #28 一人扛 unit + sim + replay + chaos + fuzz + perf + integration + UAT 8 域, W4 已亮 9/10 红 (本宣言 §3) |
| **触发** | M3 (W14 CLOB 下单 testnet 端到端) 启动后 integration test 工作量爆炸, M4.5 后 UAT 用例 + endpoint regression 一人扛不动 |
| **职责拆分** | 小宋留: unit + sim + replay + chaos + fuzz + perf (test framework SSOT). 新人接: **integration + endpoint test + UAT** (与小宋协但独立 owner) |
| **HC 编号建议** | E-052 (花名待 HR 小林取) |
| **JD 起草** | W5 (本宣言 E-W5-M04), 我与老周联签 (老周 A 单元也吃测试红利, 联签合理) |
| **JD owner** | 小林 (HR), 我 + 老周协 |
| **目标入职** | 2026-09-01 (与 M3 W14 启动对齐, 提前 1 个月 onboarding) |
| **兜底** | 2026-09-15 (M3 启动 +2 周) |
| **预算** | 与 HC-04 risk-quant 同级 (mid-senior C++ + QA 背景) |

### §8.2 与 HR pulse Part 2 Q3 HC 节奏的关系

小林 HR pulse W3 已列 Q3 HC-07 market-data QA (Owner 待定, 9 月). **本宣言提请的 HC-07 qa-integration 与 HR pulse 的 HC-07 market-data QA 是两个不同岗**, 建议:
- 我提请的 → **HC-08 qa-integration-engineer** (重新编号避免冲突)
- HR pulse 原 HC-07 market-data QA → 维持 Q3 9 月

W5 一 (6/1) 主管周同步与小林对齐编号. 本宣言以下默认我提请的是 **HC-08 qa-integration-engineer**.

### §8.3 不扩招的岗位 (主动声明)

- 小颖 / 小杜 / 小苏 / 小尤 / 小米 / 小宫: **不扩招**. 当前 6-8/10 工作量在主管帽统筹 + 1:1 调度下可消化.
- 小宫 4/10 待激活: paper engine M4 上线后再评估第 2 dogfood-tester 必要性 (估计不需要, 1 人足够).
- 小林 9/10 红: HR Owner 直属老雷, 不在我帽下扩招建议范围 (升老雷决).

---

## 完成汇报

### 主管就职宣言 (本文档)
- §1 9 人单元清单 (含小林虚线), 1 主管 + 7 IC + 1 HR Owner
- §2 8 do + 5 don't, 协商主持是 E 单元增量职能 (ADR-005 §6 决策机制由我主持)
- §3 IC 工作量评估: 小宋 9/10 红 (扩招) / 小杜 8/10 黄+ / 小颖 7/10 黄 / 小苏/小米/小尤 6/10 绿 / 小宫 4/10 待激活. 不评自己 (主管帽 W4 末才戴)
- §4 W5 派单 backlog: 8 IC 派单 + 6 主管统筹任务, 覆盖率 8/8 (含小宫干跑稿)
- §5 跨单元接口: 全 4 主管协商会主持 (W5 起每周二 30min) + 老郭 integration test 评审协同 + CPO PRD v2 cross-ref
- §6 KPI 自评 18/30 (60%), W7 复评目标 24/30 (80%)
- §7 W5 GM 派单约定: 5 题自检越主管直接派 IC 4h 内接管 + 通报
- §8 **HC 扩招提请: qa-integration-engineer** (编号 W5 一与小林对齐, 暂记 HC-08), 我 + 老周联签, 目标入职 9/1 / 兜底 9/15

### HC-08 申请 (本宣言新增, 待 HR 编号确认 + GM ack)

| 字段 | 内容 |
|---|---|
| 岗位 | **qa-integration-engineer** |
| 申请人 | 老胡 (E 主管) |
| 联签 | 老周 (A 主管, 测试红利共享) |
| 触发 | 小宋 9/10 红 + M3 W14 integration test 启动后一人扛 8 域不可持续 |
| 目标入职 | 2026-09-01 |
| 兜底 | 2026-09-15 |
| JD owner | 小林 (HR), 我 + 老周协 |
| JD deadline | W5 五 (8/1) |
| 走流程 | 本宣言 → HR 小林 W5 一 (6/1) 主管周同步上桌 → GM 老雷 ack → JD 起草 |

**关键路径风险:** HC-08 9/1 入职 vs M3 9/3 启动 = **2 天 buffer**, 任何 HR 延误 → M3 integration test 启动延. **W5 一必须立项**.

---

## 后续动作 checklist

- [x] 本宣言 v1 提交 (W4 EOW 2026-05-28)
- [ ] W5 一 (6/1) 主管周同步上桌: 协商会主持 + HC-08 联签 + 主管派单覆盖率公示
- [ ] W5 一 (6/1) HR 小林对齐 HC-07 / HC-08 编号
- [ ] W5 二三四 (6/2-6/4) 8 IC weekly 1:1 启动
- [ ] W5 五 (8/1) 第一周 sprint progress + risk registry v2.3 + 主管派单覆盖率自评
- [ ] W7 sprint 中 KPI 自评复评 (目标 24/30)

---

**最后更新:** 2026-05-28 by 老胡 (E-026, E 产品业务保障部主管)
