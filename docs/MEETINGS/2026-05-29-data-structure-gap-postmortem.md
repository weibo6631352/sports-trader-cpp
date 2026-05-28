# 数据结构 ABI 漏洞复盘会 — 议程 + 流程整改 spec

- **Owner:** 老胡 (E 主管, PM, 主持)
- **Last Review:** 2026-05-29
- **性质:** ADR-005 §3.2 全体争议会 / 复盘会
- **状态:** 准备中 (W8 W4 召开)

---

## §1 老板 verbatim 约束 (3 条, 不改写, 原文入约束)

1. "数据结构很重要, 快点补齐吧, 摸清楚后起码大家看到后可以对市场结构和数据源结构有个清楚的认知"
2. "我们订阅 polymarket 的市场 ws 的订单簿变动等接口" (项目方向 ack)
3. "b" — 选 B: stop + 复盘 + 追责 + 流程整改

**约束解读 (老胡 PM, 不越权解读为决策):**

- verbatim 1: 数据结构文档补全是 P0, 目的是全员对市场结构 + 数据源结构有清晰认知
- verbatim 2: 订阅方向已 ack, 数据结构必须对齐 Polymarket WS 订单簿事件 ABI
- verbatim 3: 老板亲自选 B, 本复盘会是老板授权召集, GM 老雷主导追责 + 整改, 老胡主持流程

---

## §2 事故 timeline (GM 老雷已审计, 老胡 organize)

| 时间线节点 | 事件 | 问题 |
|---|---|---|
| W4 Wave 19 | 老韩 RiskGateway v0.1 设计 OrderIntent 初版, 字段仅含 market_id, 无 token_id / outcome / Side Sell | OrderIntent 上游漏 token_id, Side enum 缺 Sell |
| W5 Wave 24 | 老孙 v5.1 patch: 加 R-20 四时间戳 (event_ts/data_source_ts/ingestion_ts/as_of_ts), 但未补 token_id | ABI patch 仅加 ts, 未对齐自己的 SignedOrder §84 ABI |
| W3 (老李工作) | 老李 polymarket-api-spec-v1.md 写明 outcomes / clobTokenIds / tokens[outcome,winner] 字段 | 无问题, spec 完整 |
| W? (老李 + 老孙) | 老李 + 老孙 handshake v1 §84 SignedOrder ABI 含 token_id (maker_amount / taker_amount / token_id / side / is_neg_risk) | 无问题, handshake 完整 |
| W? (老周 review) | 老周 firstreview-laoli-abi-handshake.md 完成, 仅 review WalletKind / fee 签名路径, 未 audit OrderIntent ↔ SignedOrder ABI 字段对齐 | review 范围窄, OrderIntent 漏字段未被捕获 |
| W8 W4 (2026-05-29) | GM 老雷系统审计, 发现 OrderIntent 缺 token_id / outcome / Side Sell, 与 SignedOrder §84 / 老李 spec 不对齐. 老板震怒, 选 B 复盘 + 追责 + 流程整改 | ABI 漏洞暴露 |

**事故结论:** OrderIntent 上游 ABI 漏洞在 W4 种下, W5 patch 机会窗口未修, W7 review 未捕获, W8 GM 审计才发现. 全程老李 spec + handshake 均完整正确, 问题在工程侧 ABI 设计 + review 流程.

---

## §3 责任不护短 (GM 老雷追责, 老胡记录, 不偏倚)

| 责任方 | 错在哪 | 严重度 | 备注 |
|---|---|---|---|
| **老李 (#07, Polymarket spec owner)** | **0 错** — spec v1 §88-91 写明 outcomes/clobTokenIds/tokens[outcome,winner]; handshake v1 §84 SignedOrder ABI 含 token_id; §86/87 trade flow 完整. 老板当场冤了老李, 复盘会 §2 环节当众澄清 | N/A | 需当众 verify 申辩 |
| **老韩 (#14, B 部风控合规主管, RM owner)** | W4 Wave 19 OrderIntent 初版漏 token_id + Side enum 缺 Sell — 下单最关键字段未引 Polymarket SSOT 设计 | P1 | 工程 ABI 设计责任 |
| **老孙 (#06, signer)** | W5 Wave 24 v5.1 patch 加 4ts 时未补 token_id, 与自己 SignedOrder §84 ABI 不对齐. 自己写了 handshake, 自己却没对齐 | P1 | 自我 ABI 不一致 |
| **老周 (#02, 架构主管, review owner)** | W7 review 老李 handshake 时范围窄到 WalletKind / 签名路径, 未 audit OrderIntent ↔ SignedOrder 字段完整性 | P1 | review 覆盖面不足 |
| **小石 (#? 数据结构 IC, 暂未到岗)** | 班底缺数据结构专家独立 reviewer, 核心数据结构 PR 无专业 IC 复核 | P2 | HR 扩招责任 |
| **老雷 GM** | **P0 核心错**: 1) 未立 ADR 锁"核心数据结构必引 Polymarket 一手 spec"; 2) 未 enforce FOM 跨域 review (老李 spec 产出后未强制工程 ABI cross-check); 3) W4/W5 wave 验收只看 ctest pass, 未 audit OrderIntent 字段完整性; 4) 班底无独立数据结构专家 IC; 5) 自检 5 题未覆盖"核心数据结构 ABI 字段完整性" | **P0** | 流程设计责任 |

**说明:** 责任表不是为了处分, 是为了定位流程漏洞. 每个 P1 都对应一个流程改善项 (见 §6 ADR-027).

---

## §4 GM 错 #22 立 (累计第 22 项)

**标题:** 核心数据结构 ABI 跨域 review 失效 — OrderIntent 漏 token_id/outcome/Side, 工程 ABI → Polymarket 一手 spec 0 cross-check

**时间:** 2026-05-29 W8 W4 GM 审计发现

**根因 (4 层):**
1. 派单 prompt 设计 OrderIntent 时未强 cite 老李 spec v1 + handshake v1
2. wave 验收 checklist 只检 ctest pass, 未 audit 核心 struct 字段完整性
3. 无 ADR 锁"核心业务数据结构必含 cite: Polymarket/Goalserve SSOT 链接"
4. 班底无独立数据结构 IC reviewer, 跨域 review FOM 依赖个人自觉而非制度

**永久 enforcement:** ADR-027 (§6 流程整改) — 老郭 W8 W5 主审, 本复盘会 §4 环节立项

**补 gm-self-mistakes-log.md:** #22 条目由老胡补入 (本 wave 完成)

---

## §5 复盘会议程

**主持:** 老胡 (E 主管, PM)
**召集依据:** ADR-005 §3.2 全体会议 + 老板 verbatim 3 授权
**时长:** 1 小时
**地点:** 全员 remote

**必到参会人 (9 人):**

| 参会人 | 职责 | 出席理由 |
|---|---|---|
| 老雷 GM | GM, 追责人 | 承担 P0 责任 + final ack |
| 老钱 CPO | CPO | 产品方向 ABI 影响确认 |
| 老李 (#07) | Polymarket spec owner | 当众 verify 0 错 + spec 走查 |
| 老韩 (#14) | B 部主管, RM owner | 承担 P1 工程责任 |
| 老孙 (#06) | signer | 承担 P1 ABI 不对齐责任 |
| 老周 (#02) | A 部主管, 架构 review | 承担 P1 review 范围窄责任 |
| 老郭 (F 协调) | 顾问团协调人, 架构评审 | 主审 ADR-027 |
| 小林 (HR) | HR 直属老雷 | 数据结构 IC 招聘 P0 授权 |
| 老高 (F 顾问, CI) | CI 负责人 | ABI lock v1.7 grep spec 接收 |

---

### 议程详情 (60 分钟)

#### 环节 1 — GM 复述事故 + verbatim 责任承担 (10 分钟)

**主讲:** 老雷 GM

内容:
- 逐条读老板 verbatim 1/2/3 原文
- 复述 §2 事故 timeline (6 个节点)
- GM 主动承担 P0 责任 (§3 责任表 GM 一行)
- 宣布本复盘会目的: 不是处分个人, 是修复流程

预期产出: 全员对事故有共识基线

---

#### 环节 2 — 老李 spec v1 + handshake 走查 (10 分钟)

**主讲:** 老李 (#07)

内容:
- 当场展示 polymarket-api-spec-v1.md §88-91 (outcomes / clobTokenIds / tokens[outcome,winner])
- 当场展示 laoli-laoSun-handshake-v1.md §84 SignedOrder (token_id 字段存在)
- 展示 §86/87 trade flow 完整
- 明确结论: spec + handshake 均完整, 老李 0 错

预期产出: 全员公开 ack 老李 0 错; 消除误会; 这是本会议最重要的澄清动作

---

#### 环节 3 — 老韩 / 老孙 / 老周 各自承担工程 ABI 设计责任 (10 分钟)

**主讲:** 老韩 + 老孙 + 老周 各 3 分钟 + 1 分钟 Q&A

内容:
- 老韩: 承认 W4 Wave 19 OrderIntent 初版设计漏字段, 说明当时为何未引 spec
- 老孙: 承认 v5.1 patch 仅加 ts 未补 token_id, 说明 self-review 为何未对齐 handshake §84
- 老周: 承认 review 范围窄, 说明未来 review checklist 如何扩展

**不护短规则:** 不允许"我以为别人会检查"的推辞. 每人只说自己那一段的根因.

预期产出: 三人各自 ack 责任, 工程 P1 责任定位清晰

---

#### 环节 4 — 老郭 + 老胡 立 ADR-027 流程整改 (10 分钟)

**主讲:** 老郭 (主审) + 老胡 (PM, spec 提案人)

内容:
- 老胡宣读 ADR-027 草案 4 项强 enforce (见 §6)
- 老郭提架构评审意见 (是否遗漏技术约束)
- 全员对 4 项逐一举手确认 (ADR-005 §3.2 共识机制)
- 老郭宣布 ADR-027 W8 W5 主审完成截止

预期产出: ADR-027 草案全员共识; 老郭 W8 W5 主审排期确认

---

#### 环节 5 — 小林 HR backlog: 数据结构专家 IC 招聘 P0 (10 分钟)

**主讲:** 小林 (HR)

内容:
- 宣布"数据结构专家 IC"岗位 P0 开招 (见 §7)
- 老雷 GM 在场 ack 8/1 入职目标
- 明确 onboarding checklist: 入职第一周读老李 + 小段 + 老周 W8 W4 SSOT doc + 历史 ABI handshake doc

预期产出: 招聘 P0 授权; 小林接 backlog ticket; HR 隔周三进展汇报节奏启动

---

#### 环节 6 — 老雷 GM final ack + Sprint-3 排期调整 (10 分钟)

**主讲:** 老雷 GM

内容:
- GM final ack: 复盘会结论 + ADR-027 立项 + 招聘 P0 + Sprint-3 调整
- 宣布 Sprint-3 W9-W10 排期调整 (见 §8)
- 对老李当众道歉 (冤了)
- 关闭本复盘会

预期产出: GM 书面 ack; Sprint-3 排期调整正式生效; 老李冤情公开平反

---

## §6 流程整改 spec (ADR-027 草案, 老郭 W8 W5 主审)

**ADR 编号:** ADR-027
**文件路径:** `docs/ADR/2026-06-W4-adr-027-core-data-structure-ssot-enforce.md`
**提案人:** 老胡 (PM)
**主审人:** 老郭 (架构评审)
**截止:** W8 W5 (2026-05-30)
**触发原因:** GM 错 #22 — OrderIntent 漏 token_id, 工程 ABI 0 cross-check Polymarket SSOT

### 强 enforce 4 项:

**项 1 — 数据结构 SSOT 引用 (cite 强 enforce)**

- 受约束 struct: OrderIntent / SignedOrder / Position / MarketInfo / FairValue / OrderBookSnapshot
- 规则: 任何上述 struct 新建 / 变更 PR, PR description 必含 `cite:` 段, 引用:
  - Polymarket SSOT: 老李 W8 W4 ssot doc (laoli-polymarket-data-structure-ssot-v1.md 或更新版)
  - Goalserve SSOT: 小段 W8 W4 ssot doc (xiaoduan-goalserve-data-structure-ssot-v1.md 或更新版)
- 缺 cite → PR 不得合并 (老高 CI grep 拦)

**项 2 — FOM 跨域 review 强 enforce (4 人 approve)**

- OrderIntent / SignedOrder 设计 PR 必须获得以下 4 人 approval 才可合并:
  - 老李 (#07, Polymarket spec owner)
  - 小段 (#37, Goalserve spec owner)
  - 数据结构专家 IC (HR 招聘入职后补位; 入职前由老郭代为)
  - 老周 (#02, 架构 review)
- review 范围: 必须包含 "字段完整性 vs SSOT" + "下游 ABI 对齐" 两项
- review checklist 模板由老周 W8 W5 补入 PR template

**项 3 — ABI lock v1.7 grep (老高 W8 W5 实施)**

新增 CI check `core_data_structure_ssot_check.py`:
- 检查 1: OrderIntent struct PR diff 必引用 SSOT doc (文件名 pattern: `laoli-polymarket-data-structure-ssot` 或 `xiaoduan-goalserve-data-structure-ssot`)
- 检查 2: OrderIntent / SignedOrder struct 定义必含 `token_id` 字段 (grep struct body, 绝对约束)
- 检查 3: Side enum 定义必含 `Buy` 且含 `Sell` (绝对约束, 缺一 fail)
- 检查 4: 跨 struct ABI handshake doc 必存在 (grep `handshake` pattern 文件引用, 仿 laoli-laoSun-handshake-v1.md)

**项 4 — wave 验收 GM 自检升 6 题**

现有 5 题 (CLAUDE.md §7 铁律 #8) 新增第 6 题:

> "本 wave 改核心数据结构? 若是, 验收时 audit: ① 字段是否对齐 Polymarket SSOT; ② 字段是否对齐 Goalserve SSOT; ③ 下游 ABI handshake doc 是否同步更新"

5 题任一 yes + 第 6 题触发 → 必须 audit 才允许 ack 交付

---

## §7 班底 backlog (小林 HR, W8 W5 接)

### 数据结构专家 IC 招聘 P0

| 字段 | 内容 |
|---|---|
| 岗位名 | data-structures-expert (暂定工号待 HR 登记) |
| 优先级 | P0 (老板 verbatim 1 + GM 错 #22 直接触发) |
| 入职目标 | 8/1 (2026-08-01) |
| 隶属单元 | A 系统工程部 (老周主管) 或 F 顾问团 (老郭协调) — 小林 + 老周 + 老郭 W8 W5 协商 |
| 职责 | 核心业务数据结构独立 reviewer; 新 struct PR §6-项2 必经此岗位 approve |
| onboarding 必读 | 1) 老李 W8 W4 Polymarket SSOT doc; 2) 小段 W8 W4 Goalserve SSOT doc; 3) 老周 W8 W4 ABI gap audit 报告; 4) 历史 handshake doc (laoli-laoSun-handshake-v1.md + 所有 §84+ 版本) |
| HR 注册前置 | 必须先在 employee-registry.md 登记 + 小林签字, 才能建 persona file (CLAUDE.md §7 铁律 #7) |
| 汇报节奏 | 小林 HR 招聘进展: 隔周三 (首次 W9 W3) |

---

## §8 Sprint-3 排期调整 (GM 老雷 final ack 后生效)

**背景:** 老板选 B (stop + 复盘 + 追责 + 流程整改), Sprint-3 整改优先于原计划功能开发.

### 调整前 (原 Sprint-3 W9-W10 计划)

- W9-W10: PositionManager IC 实施 (小蒋主导)

### 调整后 (Sprint-3 W9-W10 替换为 ABI 修复 + 流程整改)

| 负责人 | 任务 | 截止 |
|---|---|---|
| 老韩 (#14) | OrderIntent v0.5: 补 token_id + Outcome struct + Side enum 解耦 (Buy/Sell 完整) | W9 W3 |
| 老孙 (#06) | SignerV52 ABI align: 对齐 handshake §84 SignedOrder, 补 token_id 链路验证 | W9 W3 |
| 老唐 (#, audit) | audit schema v1.3: 对齐 OrderIntent v0.5 字段变更 | W9 W4 |
| 老高 (F 顾问, CI) | abi_lock v1.7 + core_data_structure_ssot_check.py 上线 | W9 W4 |
| 老李 (#07) | Polymarket SSOT doc v1 (laoli-polymarket-data-structure-ssot-v1.md) — 已 W8 W4 并行 | W8 W4 (本 wave) |
| 小段 (#37) | Goalserve SSOT doc v1 (xiaoduan-goalserve-data-structure-ssot-v1.md) — 已 W8 W4 并行 | W8 W4 (本 wave) |
| 老周 (#02) | ABI gap audit 报告 (现有 struct vs SSOT 全量 diff) — 已 W8 W4 并行 | W8 W4 (本 wave) |
| 小林 (HR) | 数据结构 IC 招聘启动 + employee-registry.md 预登记 | W9 W1 |

### Sprint-3 W11 评估

- paper runtime 启动 (M4.5 硬节点) — **评估是否推后**
- 风险: ABI 修复 W9-W10 完成后, W11 才能安全启动 paper runtime. 若 W9-W10 有 blocker → M4.5 推后风险 P1
- 老胡 W10 末出 M4.5 风险评估报告, 老雷 + 老钱 联决是否推后

---

## §9 不耻下问 (跨域协作 call-out)

| 被 call 人 | 动作 | 截止 |
|---|---|---|
| @老雷 GM | final ack: 复盘结论 + ADR-027 + Sprint-3 调整 + 向老李道歉 | 复盘会当场 |
| @老郭 | ADR-027 主审完成 | W8 W5 |
| @老李 (#07) | Polymarket SSOT doc v1 | W8 W4 (本 wave 并行) |
| @小段 (#37) | Goalserve SSOT doc v1 | W8 W4 (本 wave 并行) |
| @老周 (#02) | ABI gap audit 全量 diff 报告 | W8 W4 (本 wave 并行) |
| @老韩 (#14) | OrderIntent v0.5 spec | W8 W5 (复盘后) |
| @老孙 (#06) | SignerV52 ABI align spec | W8 W5 (复盘后) |
| @老高 | abi_lock v1.7 + ssot grep CI 实施 | W9 W4 |
| @小林 (HR) | 数据结构 IC 招聘 P0 启动 | W9 W1 |

---

**Last updated:** 2026-05-29 by 老胡
