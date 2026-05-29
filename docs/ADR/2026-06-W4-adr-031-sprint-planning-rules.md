---
name: adr-031-sprint-planning-rules
description: Sprint plan 制度 — 多人讨论 + 项目状态 audit + 市场调研 + 必覆盖每部门 (老板 2026-05-29 verbatim)
owner: P-00 (总裁, 草案) → 老郭 W10 W1 主审
last_review: 2026-05-29
status: Draft
metadata:
  type: ADR
  id: ADR-031
---

# ADR-031: Sprint plan 制度 — 多人讨论 + 状态 audit + 市场调研 + 覆盖每部门

- **ID:** ADR-031
- **Date:** 2026-05-29 (W9 W5)
- **Status:** Draft (总裁 P-00 草案, 老郭 W10 W1 主审 → Accepted)
- **触发:** 老板 5/29 verbatim 4 条要求

---

## §1 老板 verbatim 入约束 (一字不改)

> "每个阶段的目标计划, 需要多人讨论后定, 避免一个人考虑不周, 很多部门都没安排活就很浪费, 定目标计划必须知道当前的项目状态, 结合新一轮的市场调研, 再制定目标计划"

> "至少每轮计划都覆盖到每个部门"

(2026-05-29, GM 错 触发: 老胡 Wave 82 W10 plan 1 人定 + W8-W9 多部门没安排活)

## §2 Sprint plan 4 必要条件 (W10 W1 起强 enforce)

任何 Sprint plan / W X plan 决议**必须**同时满足 4 条, 否则视为无效:

### 条件 1: 多人讨论 (非 1 人定)

- 主持: 老胡 PM
- 必到 (实到 ≥ 6 人): 总裁 P-00 + 5 主管 (老周 A / 老韩 B / 小梁 C / 小余 D / 老胡 E) + 老郭 F 协调
- 必列席 (可缺): 副总裁 P-01 (7/1 入职后) / 老钱 CPO / 9 顾问 (老郭通知)
- 决议方式: 多人发言 + 共识 / 投票 / 升老板拍板
- 议程纪要 (老胡 owner): doc/MEETINGS/<date>-sprint-W X-planning-meeting.md

### 条件 2: 项目状态 audit (前置 input)

- owner: 老胡 PM
- 内容: 全 57 persona W X 状态 + 里程碑进度 + critical path + KPI 红绿灯 + 没安排活部门 list
- doc: `docs/SPRINTS/laohu-w X-full-project-status-audit-v N.md`
- 必含: 各部门 idle/active/overloaded 标注

### 条件 3: 新一轮市场调研 (前置 input)

- owner: 老李 (Polymarket) + 小段 (Goalserve) + 老彭 (betting industry + 竞品)
- 内容: W X 期间外部协议 / 体育市场 / 竞品 / 监管 新动向
- doc 三份, WebFetch 官网 + cite @ date (GM 错 #23 enforce)
- 触发条件: 每月至少 1 次 (Sprint 启动前)

### 条件 4: 每部门必有 ticket (无漏)

- 5 单元 (A/B/C/D/E) + 顾问团 F + 总裁办 P-00/P-01 — **每个单元至少 1 个 ticket**
- 漏 1 部门 → plan 视为无效, 老胡 退回多人讨论会
- enforce: 老高 W10 W2 加 CI grep `sprint_plan_coverage_check.py`
  - 检 Sprint plan doc 含 "## A 单元", "## B 单元", ..., "## 顾问团 F"
  - 每节至少含 1 个 ticket (e.g. `- W X-Yyy: <persona> <task>`)
  - 缺任一 → FAIL block PR

## §3 与现有 ADR 联动

- **ADR-005 (决策机制)**: 派单层级 GM → 主管 → IC 不变. 但 **Sprint plan 决议必经多人讨论会**, 非 GM/总裁 1 人定.
- **ADR-029 (worktree PR 流程)**: Sprint plan doc 也走 ADR-029, 多人讨论会主持人 push PR.
- **ADR-030 (员工主动上报)**: 员工发现 plan 漏部门 / 1 人定 → PUSH_BACK 上报.

## §4 实施 timeline

- W9 W5 末 (5/29-5/31): 5 wave 完成 (老胡 audit / 老李/小段/老彭 调研 / 老郭 顾问)
- W10 W1 Mon: 老胡主持多人讨论会, 议程 W10 plan v2 (废弃 Wave 82 PR #7 W10 plan v1)
- W10 W1 Tue: 总裁 P-00 ack v2, 老胡 push PR
- W10 W2: 老高 落地 sprint_plan_coverage_check.py CI grep
- W10 W3 起: Sprint plan PR 必过 4 条件 + CI grep

## §5 历史正反例

### 反例 (W82 W10 plan v1 1 人定)

老胡 Wave 82 PR #7 写 W10 plan 1 人定 (无多人讨论 / 无市场调研 / 无全部门 audit / E 单元小苏/小尤/小宫 + C 单元 / F 顾问团 没覆盖). → 老板批评 → 标 Draft 废弃.

### 反例 (W8-W9 多部门没安排活)

W8-W9 期间:
- C 单元 (小梁 / 小蒋 / 小袁) — 几乎闲 (除老彭 G3 KR 配合)
- F 顾问团 (老张/老何/老钱顾问/小邓/老叶/老徐/小白) — 几乎闲 (除老郭/老高)
- E 单元 (小颖/小杜/小宋/小苏/小尤/小宫) — 几乎闲

总裁 P-00 失职 — 老板 verbatim "很多部门都没安排活就很浪费".

### 正例 (本 ADR-031 立 + W9 W5 5 wave 调研)

5/29 W9 W5 末派 5 wave (Wave 84/85/86/87/88):
- 老胡 audit 全 57 persona 状态
- 老李 PM 调研 update
- 小段 Goalserve 调研
- 老彭 betting + 竞品
- 老郭 顾问 9 人意见箱

→ 多人讨论会 W10 W1 完整 input, W10 plan v2 期望符合 4 条件.

## §6 不耻下问

- @老郭 W10 W1 主审 ACCEPTED
- @老胡 多人讨论会主持 W10 W1
- @老高 CI grep W10 W2 落地
- @5 主管 + 老郭协调 W10 W1 必到
- @老板 retainer 期周报通道, 本 ADR 总裁 P-00 草案 ack 后正式入约束

## §7 总裁 P-00 自检 (ADR-030 §3 PUSH_BACK 视角)

我作为总裁 P-00 在 W8-W9 期间:
- 漏派 C/F/E 部分单元 → 失职 (Wave 82 PR #7 1 人定 老胡 plan 是 symptom)
- 老板批评后立 ADR-031 是 ack + 制度修复
- 永久 enforcement: §2 4 条件 + §4 timeline + §5 历史反例 audit

PUSH_BACK: 无 (本 ADR 是总裁 self-correction).

---

**最后更新:** 2026-05-29 by 总裁 P-00 (草案, 老郭 W10 W1 主审)
