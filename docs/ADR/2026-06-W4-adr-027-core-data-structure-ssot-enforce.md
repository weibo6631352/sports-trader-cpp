# ADR-027 — 核心数据结构 SSOT 强 enforce 流程

- **Owner:** 老郭 (主审, 架构评审) + 老胡 (PM, 提案人)
- **Last Review:** 2026-05-29
- **状态:** 草案 (Draft) — 待 W8 W5 老郭主审确认后生效
- **触发:** GM 错 #22 — OrderIntent 漏 token_id/outcome/Side, 工程 ABI → Polymarket 一手 spec 0 cross-check
- **关联:** `docs/MEETINGS/2026-05-29-data-structure-gap-postmortem.md` §6

---

## 背景

2026-05-29 GM 审计发现 OrderIntent ABI 漏洞:

- `token_id`: 缺失 (Polymarket CLOB 下单必须字段)
- `outcome`: 缺失 (市场结果标识符)
- `Side::Sell`: 缺失 (Side enum 只有 Buy)

老李 spec v1 (polymarket-api-spec-v1.md §88-91) 和 handshake v1 (§84 SignedOrder) 均完整写明上述字段. 问题根因是工程 ABI 设计时未引 SSOT, review 流程未覆盖字段完整性 audit, GM 验收未检查核心 struct 字段.

老板 verbatim: "数据结构很重要, 快点补齐吧" + 选 B (stop + 复盘 + 追责 + 流程整改).

本 ADR 立 4 项强 enforce, 防止同类 ABI 漏洞再发.

---

## 决定 (4 项强 enforce)

### Enforce-1 — 核心数据结构 SSOT cite 强 enforce

**受约束的 struct (6 个核心业务数据结构):**

```
OrderIntent
SignedOrder
Position
MarketInfo
FairValue
OrderBookSnapshot
```

**规则:**

任何上述 6 个 struct 的新建或字段变更 PR, PR description 必含独立 `cite:` 段, 格式:

```
cite:
  - polymarket_ssot: docs/RESEARCH/laoli-polymarket-data-structure-ssot-v1.md (§<section>)
  - goalserve_ssot: docs/RESEARCH/xiaoduan-goalserve-data-structure-ssot-v1.md (§<section>)
```

**违反后果:** PR 不得合并 (老高 CI 拦, Enforce-3 实现).

**生效时间:** ADR-027 老郭主审通过后即时生效.

---

### Enforce-2 — FOM 跨域 review 4 人强 approve

**背景:** "FOM" (Field Ownership Matrix) — 核心字段的所有权跨域. OrderIntent 字段同时涉及 Polymarket spec (老李) + Goalserve spec (小段) + 架构 (老周) + 数据结构专业 reviewer.

**规则:**

OrderIntent / SignedOrder 设计 PR 合并前必须获得以下角色的 explicit approve:

| reviewer 角色 | 当前负责人 | review 范围 |
|---|---|---|
| Polymarket spec owner | 老李 (#07) | 字段是否对齐 Polymarket CLOB ABI |
| Goalserve spec owner | 小段 (#37) | 字段是否对齐 Goalserve inplay 数据 |
| 数据结构专家 IC | 待招聘 (8/1 入职目标); 入职前由老郭代为 | 字段完整性 + 下游 ABI 对齐 |
| 架构 review | 老周 (#02) | struct layout + ABI 兼容性 |

**review checklist (老周 W8 W5 补入 PR template):**

```
PR review checklist — 核心数据结构:
[ ] 所有字段已引 Polymarket SSOT (老李 doc) 对照
[ ] 所有字段已引 Goalserve SSOT (小段 doc) 对照
[ ] 下游 struct (e.g. OrderIntent → SignedOrder) ABI 字段一一对应
[ ] Side enum 含 Buy + Sell
[ ] token_id 字段存在且类型正确
[ ] ABI handshake doc 已更新 (或确认无需更新)
```

**违反后果:** 缺少任一 approve → PR 不得合并.

---

### Enforce-3 — ABI lock v1.7 CI grep (老高 W9 W4 上线)

**新增 CI check 脚本:** `tests/ci_grep/core_data_structure_ssot_check.py`

**检查 4 项 (PR diff 范围内):**

| 检查编号 | 检查内容 | 失败条件 |
|---|---|---|
| C1 | OrderIntent struct PR diff 包含 SSOT cite | PR diff 改 OrderIntent 但 PR description 无 `laoli-polymarket-data-structure-ssot` 或 `xiaoduan-goalserve-data-structure-ssot` 字符串 |
| C2 | OrderIntent / SignedOrder struct 定义含 token_id | grep struct body, 无 `token_id` 字段声明 |
| C3 | Side enum 含 Buy 且含 Sell | grep enum Side body, 缺 Buy 或缺 Sell |
| C4 | 跨 struct ABI handshake doc 存在引用 | PR description 或 cite 段无 `handshake` pattern 文件引用 (仿 laoli-laoSun-handshake-v1.md 命名模式) |

**脚本接口 (老高设计, 本 ADR 仅定义 spec):**

```python
# 入口
def check_pr_diff(diff_text: str, pr_description: str) -> list[CheckResult]:
    ...

# CheckResult: name, passed: bool, message: str
```

**集成:** 加入 `.github/workflows/` 或 `scripts/ci_abicheck.sh`, 在每个 PR 的 `lint` stage 运行.

---

### Enforce-4 — GM wave 验收自检升 6 题

**现有 5 题 (CLAUDE.md §7 铁律 #8):**

1. 一面之词背书?
2. 单 agent 替全员说话?
3. 让 agent 看老项目 / 撤销方案?
4. 派单 prompt 越 persona "拒绝任务"边界?
5. 越主管直接派 IC?

**新增第 6 题 (ADR-027 加):**

> 本 wave 改动涉及核心数据结构 (OrderIntent / SignedOrder / Position / MarketInfo / FairValue / OrderBookSnapshot)? **若是**, 验收时必须 audit:
> - 字段是否对齐 Polymarket SSOT (老李最新 doc)
> - 字段是否对齐 Goalserve SSOT (小段最新 doc)
> - 下游 ABI handshake doc 是否同步更新

**触发逻辑:** 6 题任一 yes → 对应的额外 audit 必须完成才允许 ack 交付. 5 题原有逻辑不变.

**CLAUDE.md 更新:** 本 ADR 生效后, 小米 (doc-curator) 同步更新 CLAUDE.md §7 铁律 #8 加第 6 题.

---

## 不采纳的方案

**方案 A: 只修复 OrderIntent, 不立 enforce**
- 拒绝理由: 根因是流程漏洞, 修 struct 不修流程, 下个 struct 还会再出. 老板选 B 明确要求流程整改.

**方案 B: 只加 review checklist, 不加 CI grep**
- 拒绝理由: 人工 checklist 依赖自觉, CI grep 是硬约束. 两者并行, 不能互相替代.

---

## 影响范围

| 影响方 | 影响内容 |
|---|---|
| 老韩 (#14) | OrderIntent v0.5 设计 PR 必须走 Enforce-1 + Enforce-2 |
| 老孙 (#06) | SignerV52 ABI align PR 必须走 Enforce-1 + Enforce-2 |
| 老周 (#02) | review checklist 模板 W8 W5 补入 PR template |
| 老高 (CI) | core_data_structure_ssot_check.py W9 W4 上线 |
| 小米 (doc-curator) | CLAUDE.md §7 铁律 #8 加第 6 题 (ADR-027 生效后同步) |
| 老李 (#07) | 成为所有核心 struct PR 的 required reviewer (Enforce-2) |
| 小段 (#37) | 成为所有核心 struct PR 的 required reviewer (Enforce-2) |
| GM 老雷 | wave 验收自检由 5 题升 6 题 |

---

## 时间线

| 节点 | 负责人 | 截止 |
|---|---|---|
| ADR-027 草案发布 | 老胡 (PM) | 2026-05-29 (本 wave) |
| 复盘会 §4 环节全员共识 | 老胡 (主持) | W8 W4 复盘会 |
| ADR-027 主审确认生效 | 老郭 | W8 W5 |
| review checklist PR template | 老周 | W8 W5 |
| CLAUDE.md §7 #8 加第 6 题 | 小米 | ADR-027 生效后 48h |
| core_data_structure_ssot_check.py 上线 | 老高 | W9 W4 |
| 数据结构 IC 入职 (补位 Enforce-2) | 小林 HR | 8/1 |

---

**Last updated:** 2026-05-29 by 老胡 (提案) — 待老郭 W8 W5 主审
