# ADR-027 主审决议 — 老郭 W8 W5

- owner: 老郭 (F 顾问团协调人, 架构评审)
- last_review: 2026-05-29
- status: 主审完成 — 建议 ADR-027 Status 升为 Accepted (附 2 项 push back)
- 触发: GM 错 #22 + 老胡 W8 W4 草案 + 复盘会 §4 全员共识
- 输入文档:
  - `docs/ADR/2026-06-W4-adr-027-core-data-structure-ssot-enforce.md` (草案)
  - `docs/RESEARCH/laoli-w8-polymarket-data-structure-ssot-v1.md`
  - `docs/RESEARCH/xiaoduan-w8-goalserve-data-structure-ssot-v1.md`
  - `docs/RESEARCH/laozhou-w8-engineering-abi-gap-audit-v1.md`
  - `docs/MEETINGS/2026-05-29-data-structure-gap-postmortem.md`

---

## §1 主审决议 — 草案 §4 四项逐条

### Enforce-1: 数据结构 SSOT cite 强 enforce

**决议: ACK, 补充 1 项**

草案 §4.1 cite 格式合理, 6 个受约束 struct 清单与老周 gap audit §2 完全吻合.

补充: cite 段必须同时引用老李 doc 和小段 doc 两者. 若某 struct 与 Goalserve 无关 (如 SignedOrder 纯 Polymarket ABI), 允许注明 `goalserve_ssot: N/A (struct 不涉及 Goalserve 数据路径)`, 但不得省略 cite 段本身. 理由: 省略 cite 是 GM 错 #22 根因之一, 任何豁免均需显式标注.

补充依据: 老周 gap audit §4.2 指出 `core_data_structure_ssot_check.py` C1 检查需同时 grep 两个 doc 文件名; 如果允许只引一个, C1 将出现可绕过路径.

### Enforce-2: FOM 4 人 approve

**决议: ACK with push back — 见 §2**

4 人组合 (老李 / 小段 / 数据结构 IC / 老周) 设计合理. 老李 Polymarket spec 已实证可靠 (复盘会 §2 全员公开 ack 老李 0 错), 小段 Goalserve SSOT v1 已交付, 老周 gap audit 已完成. 4 人 FOM 覆盖所有 cross-domain 维度.

Push back 要点: 数据结构 IC 8/1 前未到岗期间的代理方案草案未明确. 见 §2 详述.

### Enforce-3: ABI lock v1.7 CI grep (老高 W9 W4)

**决议: ACK, 补充 grep 4 检查清单注意事项**

草案 C1-C4 四项检查方向正确. 以下补充工程细节供老高实施参考:

- C1 (SSOT cite grep): grep 范围应包含 PR description 全文, 不限于 `cite:` 段. 理由: PR 若在正文引用 SSOT 而未写 `cite:` 头, 字面 grep 会 false-fail. 建议: 检查 PR description 中是否含 `laoli-polymarket-data-structure-ssot` 和 `xiaoduan-goalserve-data-structure-ssot` 两个字符串, 满足其一即通过 (不强求 `cite:` 格式头, 格式由 review checklist 保证).
- C2 (token_id grep): PR diff 范围内, 若 struct 定义所在文件被修改, grep struct body 确认含 `token_id`. 注意 token_id 有多个命名空间 (polymarket:: vs microstructure::), 需 grep 精确到 struct 定义块.
- C3 (Side enum grep): 需同时 grep `Buy` 和 `Sell` 两个值. 老周 gap audit §2.2 确认当前代码只有 BuyYes/BuyNo, W9-W10 实施后应出现 `Buy` + `Sell` 两个枚举值.
- C4 (handshake doc grep): handshake pattern 文件引用. 补充: `laoli-laoSun-handshake-v1.md` 是当前唯一 handshake doc. W9 老韩 + 老孙 补 `laohan-laosun-orderintent-signer-handshake-v1.md` 后, C4 的 pattern 集合应扩展到所有 `*-handshake-v*.md` 文件.

与 ADR-024 联动见 §3.

### Enforce-4: GM 验收自检升 6 题

**决议: ACK, 无异议**

第 6 题触发逻辑设计合理 (涉及 6 个核心 struct 才触发, 不扩散到全部 wave). 草案三个 audit 子项 (Polymarket SSOT / Goalserve SSOT / handshake doc 同步) 与 Enforce-1/2 联动清晰. CLAUDE.md §7 铁律 #8 由小米同步更新, 截止 ADR-027 生效后 48h, 时序合理.

---

## §2 Push Back — FOM 数据结构 IC 8/1 前缺位代理

**问题**: 草案 §4.2 写"入职前由老郭代为", 但老郭的职能定位是架构评审, 不是 struct-level 字段完整性 reviewer. 若所有 OrderIntent / SignedOrder PR 都路由到老郭, 架构评审职能将被 PR approve 工作淹没, 违反"老郭看 system, 不看 idiom"的职能边界.

**推荐代理方案**:

8/1 前数据结构 IC 代理分两层:

- 层 1 (字段完整性 review): 由**老周**代理 (作为 A 单元架构主权 + gap audit 报告作者). 老周已完整读过老李 + 小段 SSOT, 具备字段级 cross-check 能力. Enforce-2 checklist 由老周在 PR 内逐项 sign-off.
- 层 2 (架构级 veto): 由**老郭**保留. 仅当 PR 涉及 struct 整体重设计 (如方案 B/C 级别的 ABI 重构) 或跨 3 个以上模块影响时, 才需老郭介入. 日常字段补齐 PR 不经老郭.

**与草案的差异**: 草案"入职前由老郭代为"改为"入职前层 1 由老周代理, 层 2 保留老郭 veto". 这样老周承担日常 review 负担, 老郭保持架构监督而不陷入 PR 队列.

8/1 数据结构 IC 到岗后: 老周将 Enforce-2 review 职责移交 IC. 老郭退出日常 approve 路径, 仅保留架构 veto.

**请老胡 W8 W5 在 ADR-027 §4.2 中更新代理方案文字**.

---

## §3 与 ADR-024 (worktree 标准流程) 联动

ADR-024 §3.1 pwd verify 5 次重犯 (GM 错 #19) 根因是 sub-agent 在 main tree 而非 worktree 内 Edit. ADR-027 的 C1-C4 CI grep 是 PR diff 层检查, 与 ADR-024 的 worktree 隔离是两个独立防线, 需明确协同边界.

**联动规则**:

ADR-027 grep 检查在 `scripts/ci_abicheck.sh` 集成, 与 ADR-024 的 `scripts/ci_worktree_check.sh` (若有) 并列在 `lint` stage 运行. 两者不互相替代:
- ADR-024 保障: PR diff 来源于 worktree branch, 文件路径合法 (防 GM 错 #19 重犯).
- ADR-027 保障: PR diff 内容包含 SSOT cite + 关键字段 (防 GM 错 #22 重犯).

**对老高的具体要求**: `core_data_structure_ssot_check.py` 脚本的 `diff_text` 输入须来自 `git diff origin/main...HEAD`, 不依赖本地 working tree. 这样可正确处理 worktree branch 的 PR diff, 与 ADR-024 worktree 流程天然对齐.

---

## §4 立项流程 Timeline

| 节点 | 负责人 | 截止 | 状态 |
|---|---|---|---|
| ADR-027 草案 | 老胡 (PM) | W8 W4 | 完成 |
| 复盘会 §4 全员共识 | 老胡 (主持) | W8 W4 | 完成 |
| 老郭主审 + 决议 | 老郭 | W8 W5 | 本文档完成 |
| ADR-027 §4.2 代理方案文字更新 | 老胡 | W8 W5 EOD | 待老胡处理 §2 push back |
| ADR-027 Status: Accepted | 老雷 GM ack | W8 W5 EOD | 待老雷 ack |
| review checklist 补入 PR template | 老周 | W8 W5 EOD | 按草案时间线 |
| CLAUDE.md §7 铁律 #8 加第 6 题 | 小米 | ADR-027 生效后 48h | 待老雷 ack 后触发 |
| core_data_structure_ssot_check.py 上线 | 老高 | W9 W4 | 已排期 |
| 5 主管周会同步 §4 流程 | 老胡 (主持周会) | W9 W1 主管周会 | 按节奏 |
| 数据结构 IC 入职 | 小林 HR | 8/1 | 招聘 P0 已授权 |

---

## §5 不耻下问 (@call-out)

**@老雷 GM**: 请 ack ADR-027 立项. 本文 §1 四项主审决议供参考. §2 push back (老周代理层 1, 老郭保留层 2 veto) 需老雷在复盘会 §6 环节或事后书面确认后, 老胡更新 ADR-027 §4.2 文字. ADR-027 生效条件: 老郭主审完成 (本文) + 老雷 GM ack.

**@老胡**: §2 push back 请处理: ADR-027 §4.2 "入职前由老郭代为"改为"入职前层 1 老周代理 (字段完整性), 层 2 老郭保留 veto (架构重设计级)". 截止 W8 W5 EOD. 同步更新 §4.2 表格中"数据结构专家 IC / 当前负责人"一列.

**@老高**: W9 W4 `core_data_structure_ssot_check.py` 实施时, 请注意 §1 Enforce-3 补充的 3 点: C1 grep 范围含 PR description 全文 (不只 cite 头); C2 grep 精确到 struct body (区分命名空间); C4 handshake pattern 集扩展到 `*-handshake-v*.md` 通配. diff_text 来源用 `git diff origin/main...HEAD`, 与 ADR-024 worktree 流程对齐 (§3).

---

**Last updated:** 2026-05-29 by 老郭 (W8 W5 主审, Wave 46)
