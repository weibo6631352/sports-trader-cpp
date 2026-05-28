# 文档命名规范 v1

- Owner: 小米 (doc-curator)
- Last review: 2026-05-28
- 验收人: 老雷 (GM)
- 状态: ACTIVE
- 适用范围: `docs/` 全目录, 所有 agent / persona 产出文档

> 原则: 一眼看出 **谁写的 + 写什么 + 哪一版**. 不追求绝对严格, 追求人脑解码成本 ≤ 1 秒.

---

## 1. 总规则

```
docs/<CATEGORY>/<persona>-<topic-kebab>-v<n>.md
```

- 全小写
- 用 kebab-case (短横线连接)
- 文件扩展名 `.md`
- 避免空格 / 中文 / 大写

---

## 2. 字段定义

### 2.1 `<CATEGORY>` (目录, 大写)

| 目录 | 用途 | 命名要求 |
|------|------|---------|
| `MEETINGS/` | 会议纪要 | 按日期 `YYYY-MM-DD-<topic>.md`, 不带 persona |
| `OKR/` | 季度/半年 OKR | `<period>-<season-tag>.md`, 例如 `2026-Q2-Q3-startup-season.md` |
| `KPI/` | 个人/团队 KPI | `<scope>-kpi-<modifier>.md`, 例如 `individual-kpi-matrix.md` |
| `ADR/` | 架构决策记录 | `ADR-<NNN>-<topic-kebab>.md` (三位数序号) |
| `HIRING/` | 招聘相关 | `<topic>-v<n>.md` 或 `<topic>.md` (无 persona) |
| `SPRINTS/` | Sprint 文档 | `sprint-<NN>.md` 或 `sprint-<NN>-<sub>.md` |
| `RESEARCH/` | 技术预研 | `<persona>-<topic>-v<n>.md` (强制带 persona) |
| `INCIDENTS/` | 事故 post-mortem | `<YYYY-MM-DD>-<incident-tag>.md` |
| `CONVENTIONS-*.md` | 规范文档 (顶级) | `CONVENTIONS-<topic>.md` |
| `INDEX.md` | 总索引 | 单文件, 顶级 |

### 2.2 `<persona>` (RESEARCH 专用)

- 用 persona 拼音名 (与 `AGENT.md` / `.claude/agents/NN-<name>.md` 一致)
- 例: `laozhou`, `xiaoliang`, `laopeng`
- 同名异人 (例如两个"小田"): 加角色后缀, 例如 `xiaotian-sports`, `xiaotian-dw`
- IC pool: 用 `xiaolu` (不写编号)
- 联合署名: 主笔 persona 在前, 例如 `laowu-laoye-cross-region-deployment-v0.1.md` (避免, 优先单主笔 + 副笔在文档头部署名)

### 2.3 `<topic-kebab>`

- 主题名, 不超过 5 个 token
- 用业务术语, 不用代号
- 好例: `compliance-redline`, `latency-budget`, `polygon-rpc-selection`, `key-management`
- 反例: `important-stuff`, `notes`, `draft1`

### 2.4 `<n>` (版本号)

| 形式 | 含义 | 何时用 |
|------|------|-------|
| `v0.1`, `v0.2` | 草稿 / 待评审 | 文档未通过验收 |
| `v1`, `v2`, `v3` | 正式版 | 通过 owner + 验收人 review |
| 不加版本号 | 实时滚动文档 | 例如 `backlog.md`, `kpi-matrix.md` (这类文档不要 v 号, 用 last_review 字段追踪) |

升版规则:
- typo / 小补丁: 同版本 in-place 修改, 更新 `Last review`
- 内容增删 / 决议变化: 升 minor (v0.1 → v0.2 / v1 → v1.1)
- 重写 / 大幅 scope 变化: 升 major (v1 → v2)
- major 升版必须归档旧版到 `docs/<CATEGORY>/archive/<filename>.md`

---

## 3. 文档头部 (Frontmatter)

每份 docs/ 下的文档必须有头部字段, 顺序自由但下列字段必填:

```markdown
# <文档标题>

- Owner: <persona> (<role>)
- Last review: YYYY-MM-DD
- 验收人: <persona> (<role>)
- 状态: DRAFT | ACTIVE | ARCHIVED | DEPRECATED
```

可选字段:
- `Co-review:` — 共审人
- `Co-consult:` — 咨询人
- `协作:` — 协作人列表
- `关联 ticket:` — 关联 Sprint ticket (例如 `S1-006`)
- `关联 OKR:` — 关联 KR (例如 `KR-C-1`)
- `关联依赖:` — 上下游文档
- `版本节奏:` — review 周期

---

## 4. 跨文档引用

- 用相对路径 + 反引号: `` `docs/RESEARCH/laohuang-compliance-redline-v1.md` ``
- 不用绝对路径 (`/Users/...`)
- 不用 URL 链接到 github (项目内部引用)
- 引用 persona: 用 `@<persona>` (例如 `@老周`, `@小米`)

---

## 5. 归档 (Archive)

- 归档触发: major 升版 / 决议被推翻 / 持有人离场超 90 天
- 归档路径: `docs/<CATEGORY>/archive/<原文件名>.md` (保持原文件名以便检索)
- 归档时在文件头加一行: `状态: ARCHIVED (归档日期 YYYY-MM-DD, 原因: <reason>)`
- 归档文档 **不再修改**, 只保留备查
- INDEX.md 不索引 archive 子目录

---

## 6. 反模式 (禁止)

| 反模式 | 例子 | 改成 |
|--------|------|------|
| 大写文件名 | `LaoHuang-Compliance.md` | `laohuang-compliance-redline-v1.md` |
| 空格 | `lao huang compliance.md` | `laohuang-compliance-redline-v1.md` |
| 中文文件名 | `老黄合规红线.md` | `laohuang-compliance-redline-v1.md` |
| 下划线 | `laohuang_compliance_redline_v1.md` | `laohuang-compliance-redline-v1.md` |
| 无 persona (RESEARCH 下) | `compliance.md` | `laohuang-compliance-redline-v1.md` |
| 无版本号 (RESEARCH 下) | `laohuang-compliance.md` | `laohuang-compliance-redline-v1.md` |
| 数字开头 | `1-compliance.md` | `laohuang-compliance-redline-v1.md` |
| 过长主题 | `laohuang-compliance-and-also-kyc-and-aml-and-monitoring-v1.md` | 拆成多份文档 |
| 用 emoji | `laohuang-compliance.md` | (本项目全程禁 emoji) |

---

## 7. 例外清单 (历史遗留)

下列文件因历史原因不完全符合 v1 规范, 但允许保留:

- `docs/INDEX.md` — 总索引, 顶级单文件, 无 category 前缀
- `docs/RESEARCH/laohuang-compliance-signoff.md` — 签收表, 无 `-v<n>` 后缀 (跟随主文档版本)
- `docs/RESEARCH/data/laochen-network-bench-*.csv` — 数据快照, 非 markdown
- `docs/KPI/individual-kpi-matrix.md`, `docs/HIRING/backlog.md` — 实时滚动文档, 无版本号

后续若新增类似文件, 在本章节列出并说明原因.

---

## 8. 检查清单 (产文档前自检)

提交 PR 前, 文档 owner 自查:

- [ ] 文件名全小写 + kebab-case
- [ ] 在正确的 `<CATEGORY>/` 目录下
- [ ] 头部有 Owner / Last review / 验收人 / 状态
- [ ] 主题清晰, 不超过 5 token
- [ ] 版本号符合 §2.4 规则
- [ ] 跨文档引用用相对路径
- [ ] 通知小米 (doc-curator) 在 INDEX.md 索引 (P0 文档强制)

---

## 9. 小米的入档审批

- 所有新文档入 docs/ 必须经小米最终命名核对
- 战略级 (OKR / ADR / CONVENTIONS-*) 需老雷会签
- 紧急 RESEARCH (Sprint 内) 可先入档后核对, 但 14 天内必须合规
- 不合规文档: 小米发 review 请 owner 改名, 不删除原文档

---

## 附录 A: 版本历史

| 版本 | 日期 | 变更 | 作者 |
|------|------|------|------|
| v1 | 2026-05-28 | 首版, Sprint-1 Wave 2 交付 | 小米 |
