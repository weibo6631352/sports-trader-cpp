# ADR-028 — 文档治理 Standard

- **owner:** 老郭 #41 (主审, F 协调人) — 小米 #40 (spec 输入)
- **last_review:** 2026-05-29
- **status:** SSOT
- **触发:** 小米 W9 W1 全量 audit (xiaomi-w9-docs-governance-audit-v1.md §7) — docs/ ~213 个 md, 状态混乱, frontmatter 缺失普遍, 无统一 review 节奏
- **关联:** ADR-027 (数据结构 SSOT 强 enforce), CLAUDE.md §7 (协作规范), CLAUDE.md §9 (文档体系)

---

## §1 老板 Verbatim — 约束基线

> "让文档管理员整理文档吧, 归纳为新的有用的, 缺啥文档就让人补充, 新的写好旧的可以删除"

三条硬约束 (老郭主审解读, W9 W2 生效):

1. **归纳有用** — 所有 docs/ 下 .md 必须有可机读的 frontmatter, status 字段清晰. 全员可在 5 秒内判断一份文档是否权威.
2. **缺啥补啥** — 小米 audit 识别的 13 个缺失文档 (xiaomi-w9-docs-governance-audit-v1.md §4), 由指定 owner 在 W9 W2-W3 补齐. 补前必须有 frontmatter.
3. **新写好旧删除** — 新 SSOT 合并 main 后, 旧版本 owner 在 W9 W3 或下一 sprint 内清理 (直接删 或 移 archive/ 子目录). 小米 W9 W3 末 verify 全状态干净.

---

## §2 五条核心规则

### 规则 1 — frontmatter 必含字段

所有 `docs/` 下 `.md` 文件, 开头必须包含以下 frontmatter (行格式, 无需 YAML block):

```
- owner: <姓名> (<工号>)
- last_review: YYYY-MM-DD
- status: SSOT | Archive | Outdated | Draft | Retracted
```

**说明:**
- `owner` = 文档内容负责人, 非创建人. 跨人交接时更新.
- `last_review` = 最近一次 owner 主动确认内容正确的日期.
- `status` 五级定义见下表.

| status | 含义 | 可引用? |
|---|---|---|
| SSOT | 当前权威版本, owner 确认最新 | 是 |
| Draft | 草案, 未经 owner 或老郭 ack | 引用须标注 Draft |
| Archive | 历史版本, 被新 SSOT 取代, 保留参考 | 仅历史查阅 |
| Outdated | 已知过时但新版未落地, 需尽快处理 | 否 |
| Retracted | 已撤回, 加 RETRACTED 区段 (见规则 3) | 否 |

**CI enforce 时间:** 老高 W9 W2 实施 `doc_governance_check.py`, 缺 owner / status 的文档输出告警列表.

### 规则 2 — SSOT 季度 Review 节奏

| 距上次 last_review | 状态 | 处理要求 |
|---|---|---|
| < 3 个月 | 正常 | 无需操作 |
| 3–6 个月 | 黄色警告 | 小米派 owner verify, W 内响应 |
| > 6 个月 | 红色 | owner 必须 W 内响应: 更新 last_review 或主动降级为 Archive |

**触发机制:** 老高 `doc_governance_check.py` 每次 PR + 每周自动跑, 以 last_review 日期计算. 输出落 `docs/META/doc-governance-report-<date>.txt` (只读报告, 不入 git).

**季度 override:** 老雷 (GM) 可 override 任意文档状态, 需在文档内加注释 + 入 docs/INCIDENTS/ 或 docs/ADR/.

### 规则 3 — Retracted Doc 处理规范

Retracted doc **不删除文件** (CLAUDE.md §7 公开失败原则: "出问题先发 incident, 不要捂").

必须操作:
1. `status` 字段改为 `Retracted`
2. 在文档正文顶部加区段:
   ```
   ## RETRACTED YYYY-MM-DD by <姓名>
   理由: <一句话>
   替代文档: <路径> (若有)
   ```
3. 原有内容保留, 不修改. Retracted 区段置于所有正文之前.

**已执行示例:** ADR-020 (ic-tester-separation), ADR-022 (ic-self-test-tester-review) — 可作格式参考.

**违规后果:** 直接删除 Retracted doc = 破坏 audit trail, 视同 "数据 schema 静默变更" 处理 (CLAUDE.md §8 红线).

### 规则 4 — 版本迭代规范 (Archive 自动)

旧版本文档在新版 SSOT 合并 main 后, 旧版 status 自动降为 Archive. 流程:

1. 新版文档 PR 合并前, PR description 中明确写 "supersedes: <旧版路径>"
2. PR 合并时, 小米 (或 owner) 同步更新旧版 frontmatter: `status: Archive`
3. Archive 文档在下一 sprint 或 W9 W3 内, owner 移入对应 `archive/` 子目录 (不删)
4. **同一文档超过 3 个历史版本:** 只保留最新 2 个 + 最旧 1 个, 其余由 owner 在 W9 W3 删除
   - 例外: Rust 废弃文档 (老张系列) 作知识沉淀全部保留, 加 `status: Archive (Rust 废弃 2026-05-28)`

**老高 CI grep:** 检测 `docs/` 下是否存在同 owner + 同 topic 超过 3 个版本的文档, 输出告警 (不阻断 PR, 仅报告).

### 规则 5 — CI grep Enforce (老高 W9 W2 实施)

`doc_governance_check.py` 检查项:

| 检查项 | 阻断 PR? | 说明 |
|---|---|---|
| .md 缺 `owner:` 字段 | 告警 (不阻断) | 输出缺失列表 |
| .md 缺 `status:` 字段 | 告警 (不阻断) | 输出缺失列表 |
| status 值不在五级枚举内 | 告警 (不阻断) | 输出异常值 |
| SSOT 文档 last_review > 6 个月 | 告警 (不阻断) | 输出红色列表 |
| 同 owner+topic 历史版本 > 3 个 | 告警 (不阻断) | 输出清理候选 |

**第一期 (W9 W2): 全告警不阻断.** W9 W3 末评估告警数量, W10 决定是否升级阻断 PR.

**输出路径:** `docs/META/doc-governance-report-<YYYY-MM-DD>.txt` — 不入 git (.gitignore 加此 pattern), 仅本地 + CI artifact.

---

## §3 Owner 责任矩阵

| 角色 | 责任 |
|---|---|
| **doc owner** (各文档 frontmatter 中的 owner) | 季度 review: 确认内容正确 + 更新 last_review; 版本迭代时维护旧版 status; 新文档创建时同步登记 docs/INDEX.md |
| **小米 #40** (doc-curator) | docs/INDEX.md 自动化更新 (W9 W2); 跨 doc audit (每 sprint 末); 黄/红警告派单给 owner; W9 W3 末 verify 全 docs/ 状态干净 |
| **老高 #16** (CI / 工程规范) | `doc_governance_check.py` 实施 + 维护 (W9 W2 交付); 每次 PR + 每周自动触发; 输出 report 路径 |
| **老雷 (GM)** | Annual review override: 可强制更改任意文档 status, 需留注释 + 入 ADR 或 INCIDENTS |
| **老郭 #41** (架构评审, 本 ADR owner) | ADR 类文档架构合规审查; 季度架构健康检查时抽查 SSOT 质量 |

**新文档创建硬约束 (CLAUDE.md §10):**
- 路径格式: `docs/<分类>/<owner>-<topic>-v<N>.md`
- 创建即登记 INDEX.md (owner 责任, 小米 W9 每周抽查)
- 数据源类文档须附四维扫描记录 (CLAUDE.md §8 红线 R-33)

---

## §4 与 ADR-027 联动

| ADR | 锁定范围 | 互补关系 |
|---|---|---|
| **ADR-027** (`2026-06-W4-adr-027-core-data-structure-ssot-enforce.md`) | 核心数据结构字段完整性 (OrderIntent / MarketSnapshot 等) 的 SSOT enforce 流程 | ADR-027 锁 "内容正确性": 工程 ABI 必须 cross-check 一手 spec; 字段不可漏 |
| **ADR-028 (本文)** | 所有 docs/ 文档的元数据规范 (frontmatter / status / review 节奏 / 版本迭代) | ADR-028 锁 "文档可信度": 任意文档必须有 owner + status + review 日期, 才能被 ADR-027 的 SSOT 流程引用 |

**依赖关系:** ADR-027 的 "一手 spec SSOT" 引用有效性, 依赖 ADR-028 的 frontmatter `status: SSOT` 字段. 两者互为前提.

**冲突解决:** ADR-027 与 ADR-028 规则冲突时, 升级老郭主审, 24h ack.

---

## §5 实施 Timeline

### W9 W2 (小米 + 老高, 截止本周末)

| 任务 | Owner | 产出 |
|---|---|---|
| docs/INDEX.md 全面更新: 补遗漏 30+ 条目, 按目录分区, 加 `[SSOT]`/`[Archive]`/`[Outdated]` 标注 | 小米 | docs/INDEX.md v2 |
| 全 docs/ .md 补 frontmatter (owner / last_review / status) — 优先补 ADR + RESEARCH SSOT 层 | 小米 + 各 owner | frontmatter 覆盖率 > 80% |
| `doc_governance_check.py` 实施: grep frontmatter, 输出缺失/告警列表 | 老高 | CI 脚本 + 首次 report |
| 缺失文档 §4 清单中 W9 W2 项 (共 7 个) 各 owner 提交 | 小林/老李/老唐/老彭/老胡/老郭 | 7 个新文档落 main |

### W9 W3 (各 owner, 截止本周末)

| 任务 | Owner | 产出 |
|---|---|---|
| 缺失文档 §4 清单中 W9 W3 项 (共 6 个) 补齐 | 老彭/老周/老周+老胡 | 6 个新文档落 main |
| 旧 doc 删除 §5.1 清单 25 条 — 新 SSOT 确认落 main 后执行 | 见 audit §5.1 表格各 owner | 25 个文档删除 |
| 旧 doc 归档 §5.2 清单 — 移 archive/ 子目录 | 见 audit §5.2 表格各 owner | sprint/meetings/research 历史版本归档 |
| 同 owner+topic 超 3 版本 doc 清理 (老高 report 输出候选) | 各 owner | 冗余版本删除 |
| 小米 verify: docs/ 全状态干净, INDEX.md 覆盖率 ≥ 95% | 小米 | verify 报告 (发 §8 不耻下问列表) |

**前置条件 (不可跳过):** W9 W3 删除操作必须等对应新 SSOT 已合并 main + INDEX.md 已更新后才执行.

---

## §6 不耻下问 (ack 列表)

本 ADR 立项后, 老郭向以下人员发出 ack 请求:

| 对象 | 请求内容 | 截止 |
|---|---|---|
| **@老雷 (GM)** | ack ADR-028 立项方向 + §5.1 旧 doc 删除清单 sign-off | W9 W2 |
| **@小米 #40** | INDEX.md W9 W2 升级实施计划确认; frontmatter 补全优先级 | W9 W2 |
| **@老高 #16** | `doc_governance_check.py` W9 W2 交付确认; 告警 vs 阻断策略 ack | W9 W2 |
| **@全 doc owner** (各 RESEARCH/ADR 文档 owner) | W9 W2-W3 frontmatter 补全 + 版本清理配合 | W9 W3 |
| **@9 顾问 (老郭转发)** | ADR-028 治理 standard 意见征集 — 有异议 48h 内回复老郭, 无回复视为 ack | W9 W2 |

**意见征集说明 (顾问团):** 本 ADR 为 doc 治理流程标准, 非技术架构变更. 顾问团对五条核心规则有调整意见的, W9 W2 内发老郭. 老郭 W9 W3 初版入 main.

---

**最后更新:** 2026-05-29 by 老郭 #41
