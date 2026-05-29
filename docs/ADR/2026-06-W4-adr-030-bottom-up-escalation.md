# ADR-030 — 员工主动上报制度 (Bottom-up Escalation)

- **owner:** 老郭 (F 协调人, 主审) + 老胡 (PM, escalation-inbox 周报)
- **last_review:** 2026-05-29
- **status:** ACCEPTED
- **触发:** 老板 verbatim 2026-05-29 — Wave 83 P0
- **关联:**
  - `CLAUDE.md` §3 核心价值观 (铁律 #5 新增来源)
  - `docs/ADR/2026-06-W4-adr-029-worktree-push-pr-flow.md` (ADR-029, worktree 流程)
  - `docs/ADR/2026-06-W4-adr-027-core-data-structure-ssot-enforce.md` (ADR-027, cite)
  - `docs/ADR/2026-05-28-department-manager-mandate.md` (ADR-005, 决策层级基线)
  - `docs/META/escalation-inbox.md` (本 ADR 配套, 新建)
  - `docs/META/laogao-w9-w4-dispatch-prompt-template-v2.md` (派单模板 v2, 升 v3 触发)

---

## §1 老板 Verbatim — 约束基线

> "员工在做事时发现不合理的制度或逻辑要主动上报, 让领导层知道"

**老板原话 (2026-05-29)** — 直接约束本 ADR 全部决定方向, 不可被后续决策覆盖.

三条硬约束 (老郭解读, W9 W5 生效):

1. **员工主动** — 不是等领导发现, 不是等出事故再报, 发现即上报.
2. **上报对象明确** — "让领导层知道" = 直接主管 → GM → 总裁 → 老板, 逐级有 SLA.
3. **制度与逻辑两类** — 制度 (ADR / 流程 / 派单模板 / FOM) 和逻辑 (spec / 接口 / 数据 / 红线) 均在范围内.

---

## §2 与现有铁律的关系

CLAUDE.md §3 原有四条铁律:

| 编号 | 铁律 | 方向 |
|------|------|------|
| #1 | 实盘优先 (Production First) | 决策标准 |
| #2 | 纪律高于收益 (Discipline > PnL) | 行为边界 |
| #3 | 数字说话 (Data-Driven) | 沟通规范 |
| #4 | 不耻下问 (Open Communication) | 横向: 主动求助 |

**本 ADR 新增第五铁律:**

| 编号 | 铁律 | 方向 |
|------|------|------|
| #5 | **员工主动上报不合理 (Bottom-up Escalation)** | 纵向: bottom-up 反馈 |

铁律 #4 与 #5 互补: #4 是横向 (peer-to-peer 求助), #5 是纵向 (employee-to-management 上报). 两者均属 Open Communication 文化的组成部分, 但 #5 针对制度与逻辑层面的系统性问题, 具有强制性.

**本 ADR 不修改 CLAUDE.md — 由老雷在下次月末全体 Review 前通过 PR 正式写入 §3.**

---

## §3 上报范围 — 员工 Trigger 条件

员工在执行任务过程中, 发现以下任一情况, **必须上报**, 不可默默按错误的方式执行:

### 3.1 制度问题

- ADR 条款互相矛盾或存在执行漏洞
- 流程步骤不完整 (如 ADR-029 worktree 流程缺某一步导致 PR 无法创建)
- 派单 prompt 格式错误 / 引用缺失 / 边界越界
- FOM (Fix-or-Merge) 决策逻辑有盲区
- 跨域 review 缺口 (某类变更没有指定 reviewer)

### 3.2 逻辑问题

- spec 条款与实际 API 行为矛盾
- 接口契约不一致 (调用方与实现方理解不同)
- 数据结构字段 gap (如 ADR-027 触发的 OrderIntent 漏字段事故)
- 红线遗漏 (某类 P0 场景未被现有红线覆盖)

### 3.3 派单 prompt 错误

- persona 边界被越界 (被要求做 "拒绝任务" 范围内的事)
- cite 缺失 (prompt 要求引用的文件不存在或路径错误)
- 实际不可执行 (指令逻辑上相互矛盾, 无法同时满足)
- 语言纪律违反 (被要求写 Rust / Python 进生产等)

### 3.4 班底问题

- owner 缺位 (某模块或文档无明确 owner, 出问题无人负责)
- 知识盲点 (某专业领域全班底无人覆盖, 存在单点风险)
- 工作量超负荷 (某 IC 或主管连续多个 sprint 被分配超出合理范围的任务)

**不在范围内 (不属于本 ADR 上报):**
- 对业务方向的个人意见 → 走 §4 的产品方向渠道 (老钱 + 老雷)
- 对其他员工个人行为的投诉 → 走 HR 小林渠道
- 技术实现细节分歧 → 走 ADR-005 需求-工程协商会

---

## §4 上报路径 — ADR-005 §3.2 升级

基于 ADR-005 决策层级, 本 ADR 定义 bottom-up 对应路径:

```
员工发现问题
   ↓
[Step 1] 员工 → 直接主管 (24h ack SLA)
   ├── 主管 ack + 同意上报 → 主管入 Sprint backlog (老胡 PM 跟进)
   ├── 主管 ack + 认为无需升级 → 主管给员工书面说明, 员工可接受或继续升级
   └── 主管 24h 无响应 → 自动升 Step 2
   ↓
[Step 2] 员工/主管 → GM 老雷 (48h ack SLA)
   ├── GM ack + 拍板 → 入 ADR / Sprint backlog
   ├── GM ack + 认为无需处理 → GM 给书面说明, 可接受或继续升级
   └── GM 48h 无响应 → 自动升 Step 3
   ↓
[Step 3] 员工/GM → 总裁 (72h ack SLA)
   ├── 总裁 ack + 拍板 → 入公司宪法级变更
   └── 总裁 72h 无响应 → 升 Step 4
   ↓
[Step 4] 老板 retainer 期周报通道 (异步, 无硬 SLA)
```

**SLA 违约处理:** 上级 SLA 超时视为默认同意员工上报内容进入下一级. 老胡 PM 在 escalation-inbox.md 中追踪 SLA 状态, 每周报 §3 呈现 SLA 达成率.

**重要约束:** 员工可跨级上报, 但必须同时抄送中间层级 (不可静默绕过). 直接越级不抄送 = 违反 ADR-005 §3.2 协作规范.

---

## §5 上报机制 — escalation-inbox.md

**新建文件:** `docs/META/escalation-inbox.md`
**owner:** 老郭 (架构评审 + 顾问团协调人)
**co-维护:** 老胡 (PM, 周报 §3 数据源) + 小米 (doc-curator, 归档监督)

文件格式:

```
| ID | 日期 | 上报人 | 类别 | 问题描述 | 影响 | 建议 | 状态 | 主管 ack | GM ack |
```

**员工填报要求 (三项必填):**

1. **问题描述** — 具体说明发现了什么不合理, 引用具体 ADR / 文件路径 / wave 编号
2. **影响** — 量化或定性说明不处理的后果 (铁律 #3: 数字说话)
3. **建议** — 员工自己的改进方案 (即使不完整也要写, 体现主动性)

**主管处理要求:** 24h 内在 escalation-inbox.md 对应行填写 ack 状态 (同意 / 不同意 + 说明).

**与 escalate-decision-log.md 的区别:**
- `escalate-decision-log.md` — sub-agent 拒接决议 (C1-C6 分类, 老徐 owner)
- `escalation-inbox.md` — 员工主动上报制度/逻辑问题 (本 ADR, 老郭 owner)
两者并行, 互不替代.

---

## §6 已有 Push Back 范例 — 员工 Trigger 实证

以下历史案例均为铁律 #5 的**正例**, 公司鼓励此类行为, 归档作为 SOP reference:

| 案例 | Wave | 上报人 | 类别 | 处理结果 |
|------|------|--------|------|----------|
| 边界拒 cpp wave | W9 W3 | 老沈 | 3.3 派单 prompt 错 (语言纪律越界) | 转老孙处理, 合理 |
| 跨源 mapping 3 push back (Goalserve 队名 + delta_ts + 小冯依赖) | W9 W3 | 小段 | 3.1 制度问题 (依赖未就绪) | 依赖链梳理, 排期调整 |
| ADR-027 主审 2 push back (FOM 代理方案 老周 layer 1) | W9 W3 | 老郭 | 3.2 逻辑问题 (接口契约不一致) | ADR-027 修订, 主审确认后生效 |
| sigType=1 修 vs v5.1 错填 2 (HMAC bug #2) | W9 W2 | 老孙 | 3.2 逻辑问题 (spec 矛盾) | CI grep 加固, ADR 修订 |
| PR #3 follow-up confirm main 遗留 fail (不揽责) | W9 W4 | 老沈 | 3.1 制度问题 (owner 边界) | 确认责任归属, 老沈不揽责合理 |

**模式总结:** 五个案例均发生在执行层 (IC / 顾问), 发现问题后主动标记而非默默接受. 这正是铁律 #5 要规范化和激励的行为模式.

---

## §7 派单 Prompt 模板 v3 — W9 W5 起

在现有派单 prompt 模板 v2 (`docs/META/laogao-w9-w4-dispatch-prompt-template-v2.md`) 基础上, 新增以下强制约束块:

```
## BOTTOM-UP CHECK (ADR-030 强制, W9 W5 起)

本 wave 执行过程中, 如发现以下任一情况:
- 制度问题: ADR / 流程 / 派单模板 / FOM 逻辑缺口
- 逻辑问题: spec 矛盾 / 接口不一致 / 数据结构 gap / 红线遗漏
- 派单 prompt 错: persona 越界 / cite 缺失 / 实际不可执行
- 班底问题: owner 缺位 / 知识盲点 / 工作量超负荷

**必须在 PR body 中显式写出 "PUSH_BACK:" 区段**, 格式:

PUSH_BACK:
- 类别: [制度/逻辑/派单/班底]
- 描述: <具体问题>
- 影响: <不处理的后果>
- 建议: <改进方向>

无问题时写: PUSH_BACK: N/A (已检查, 无发现)

不可默默按错误的方式执行, 不可省略本区段.
```

**配套 CI 检查 (派老高 W10 W2):** `tests/ci_grep/bottom_up_check.py`
- PR body 含 "PUSH_BACK:" → INFO log, 写入员工绩效记录
- PR body 缺 "PUSH_BACK:" 区段 → CI warning (非 block, 第一个 sprint 宽限)
- W10 W4 起: 缺 "PUSH_BACK:" → CI fail (block merge)
- 主管 24h 未 ack escalation-inbox.md 新条目 → 触发 GM / 总裁邮件通知

---

## §8 与 ADR-005 决策机制联动

ADR-005 是 top-down 决策框架, ADR-030 是 bottom-up 反馈框架. 两者互补, 构成完整的双向沟通机制:

| 维度 | ADR-005 (top-down) | ADR-030 (bottom-up) |
|------|-------------------|-------------------|
| 方向 | GM → 主管 → IC | IC → 主管 → GM → 总裁 → 老板 |
| 触发 | GM 有任务要派 | 员工发现问题要上报 |
| 机制 | 派单 / 验收 / 拍板 | 上报 / ack / 入 backlog |
| SLA | 派单即时, 验收 wave 周期 | 24h / 48h / 72h 逐级 |
| 工具 | 派单 prompt 模板 | escalation-inbox.md + PR PUSH_BACK 区段 |
| owner | GM + 5 主管 | 老郭 (协调) + 老胡 (PM 跟进) |

**ADR-030 不修改 ADR-005 的派单层级和验收机制.** ADR-030 只在 ADR-005 之外增加一条 bottom-up 通道.

---

## §9 协同通知 (本 ADR 配套行动)

| 角色 | 行动 | 截止 |
|------|------|------|
| **总裁** | ack 本 ADR 立项 | W9 W5 内 |
| **老胡 (PM)** | 接收 escalation-inbox.md 周报监控责任, 纳入 Sprint 周报 §3 | W9 W5 站会 |
| **老高 (顾问)** | W10 W2 落地 `tests/ci_grep/bottom_up_check.py` | W10 W2 结束前 |
| **小米 (doc-curator)** | 新建并维护 `docs/META/escalation-inbox.md` 格式规范 | W9 W5 内 |
| **5 主管** (老周/老韩/小梁/小余/老胡) | W9 W5 周会向各部门 IC 传达铁律 #5 + PUSH_BACK 格式要求 | W9 W5 主管周同步 |
| **9 顾问** (F 顾问团全员) | review 本 ADR, 有异议走架构评审渠道 (老郭 24h ack) | W9 W5 + W10 W1 架构月会 |

---

## §10 ADR-027 Cite Block

> **本 ADR 不涉及核心数据结构变更 (无新增 C++ struct / API 字段 / schema 修改), ADR-027 SSOT enforce 流程: N/A.**

如后续本 ADR 的 CI 工具 (`bottom_up_check.py`) 涉及数据结构解析, 届时老高需补写 ADR-027 cite block.

---

## §11 生效与变更

- **生效:** W9 W5 起 (2026-05-30), PUSH_BACK 区段宽限至 W10 W4, W10 W4 后 CI block.
- **变更:** 本 ADR 内容变更须走 PR + 老郭 review + 老雷拍板.
- **废弃条件:** 铁律 #5 写入 CLAUDE.md §3 后, 本 ADR 转为执行细则附录, 不废弃.
