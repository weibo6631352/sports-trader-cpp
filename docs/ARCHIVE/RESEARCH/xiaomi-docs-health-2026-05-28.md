# docs/ 健康度报告 — Sprint-1 Wave 2 (2026-05-28)

- Owner: 小米 (doc-curator)
- Last review: 2026-05-28
- 验收人: 老雷 (GM)
- 频次: Sprint 结束 + 季度末
- 状态: ACTIVE

> 本报告评估 docs/ 当前漂移情况, 给出 fix 清单. 报告本身不修代码 / 不修业务文档, 只标问题.

---

## 0. 评级 TL;DR

**总评: B+ (良好, 有局部漂移)**

| 维度 | 评级 | 说明 |
|------|------|------|
| 结构完整性 | A | 8 大目录就位, INDEX 顶层守住 SSOT |
| 索引同步 | B | 本次 review 前 INDEX 落后 RESEARCH 8 篇, 已补齐 |
| Frontmatter 一致性 | B+ | 17 篇 RESEARCH 中 1 篇缺 Owner 字段, 1 篇用 Date 代替 Last review |
| 跨文档引用健康 | A- | 主要引用都活跃, 1 处未来引用 (signer runbook 未落地), 1 处 persona 张冠李戴 |
| 命名规范 | B | 此前无形式化规范, 实际命名一致性较高; 本次发布 `CONVENTIONS-naming.md` v1 |
| 漂移监控机制 | C+ | 仅人工 review, 无自动化 lint |

**Top 3 行动项 (按优先级):**
1. **P0** — `laoxu-external-tools-inventory-v1.md` 把 doc-curator 错署为"老郑", 实际是小米. 需 owner 修正 (review-only, 不破坏内容).
2. **P1** — `laoshen-key-management-coreview-v1.md` 缺 `Owner:` 和 `Last review:` 字段, 需老沈补齐 (改 1 行).
3. **P2** — 引入文档 lint 脚本 (M3 节点之前), 自动检测 frontmatter 缺失.

---

## 1. 结构盘点

### 1.1 目录现状 (2026-05-28 14:00 快照)

| 目录 | 文件数 | 占用 | 状态 |
|------|-------|------|------|
| `MEETINGS/` | 2 | 创始会 + HR launch | 健康 |
| `OKR/` | 1 | 2026-Q2-Q3 起步季 | 健康 |
| `KPI/` | 1 | individual-kpi-matrix | 健康 |
| `HIRING/` | 2 | backlog + sop-v1 | 健康 |
| `SPRINTS/` | 1 | sprint-01 | 健康 |
| `RESEARCH/` | 17 .md + data/ | Wave 1-2 预研集中产出 | 健康 (含 1 漂移) |
| `ADR/` | 0 | 空 | 待立 (老韩 RiskManager 候选) |
| `INCIDENTS/` | 0 | 空 | 待立 (signer runbook 候选) |

### 1.2 顶级文档

| 文件 | 状态 |
|------|------|
| `INDEX.md` | 本次 review 已重新同步 |
| `CONVENTIONS-naming.md` | 本次新增 (v1, 小米) |

---

## 2. INDEX.md 同步检查

### 2.1 review 前问题

- `INDEX.md` 只用 wildcard `RESEARCH/` 模糊指向, 17 篇 RESEARCH 没有逐一索引
- Sprint-1 Wave 1-2 产出大量文档 (8 篇主要), 索引未跟上
- 缺少健康度 / 漂移报告挂载

### 2.2 review 后修正

- 17 篇 RESEARCH 文档全部入 INDEX
- 顶级文档块新增 `CONVENTIONS-naming.md`
- 健康度章节挂载本报告

### 2.3 后续机制

每个 Sprint 末小米强制更新 INDEX, 检查清单:
- 新文档全部入索引 (含 persona, 关联 ticket)
- 归档文档移出活跃列表
- INDEX `Last updated` 字段与日期对齐

---

## 3. Frontmatter 一致性检查 (17 篇 RESEARCH)

### 3.1 全字段合规 (15 篇)

| 文档 | Owner | Last review | 验收人 | 状态字段 |
|------|-------|-------------|--------|---------|
| `laohan-riskmanager-design-v0.1.md` | 老韩 | 2026-05-28 | 小梁+老郭 | DRAFT |
| `laohe-cpp-version-selection-v1.md` | 老何 | 2026-05-28 | 老郭+老周 | (隐含) |
| `laohuang-compliance-redline-v1.md` | 老黄 | 2026-05-28 | 老雷 | ACTIVE |
| `laohuang-compliance-signoff.md` | 老黄 | (用截止日 2026-06-11) | 老雷 | (活跃) |
| `laojiang-latency-budget-v1.md` | 老姜 | 2026-05-28 | 老周 | (隐含) |
| `laoli-polymarket-api-spec-v1.md` | 老李 | (需复检) | (需复检) | (需复检) |
| `laopeng-betting-industry-analysis-v1.md` | 老彭 | 2026-05-28 | 小梁 | (隐含) |
| `laoqian-mvp-scope-rejection-v1.md` | 老钱 | 2026-05-28 | 老雷 | 锁定 |
| `laoshen-threat-model-v1.md` | 老沈 | 2026-05-28 | 老韩+老雷 | (隐含) |
| `laosun-key-management-v1.md` | 老孙 | 2026-05-28 | 老雷 | v1 待 review |
| `laowu-cross-region-deployment-v0.1.md` | 老吴+老叶 | 2026-05-28 | 老周 | (隐含) |
| `laoxu-external-tools-inventory-v1.md` | 老徐 | 2026-05-28 | 老雷 | (隐含) |
| `laoye-polygon-rpc-selection-v1.md` | 老叶 | 2026-05-28 | 老孙 | (隐含) |
| `laozhou-architecture-v0.1.md` | 老周 | 2026-05-28 | 老郭 | (隐含) |
| `xiaoliang-market-structure-v1.md` | 小梁 | 2026-05-28 | 老钱 | v1 草稿 |
| `xiaoshi-data-structures-selection-v1.md` | 小石 | 2026-05-28 | 老周 | (隐含) |

### 3.2 漂移项 (2 篇)

| 文档 | 问题 | 建议 fix |
|------|------|---------|
| `laoshen-key-management-coreview-v1.md` | 缺 `Owner:` 和 `Last review:` 字段, 用 `Co-reviewer:` + `Date:` 替代 | 加 `Owner: 老沈 (security-engineer)` + `Last review: 2026-05-28` |
| `laoli-polymarket-api-spec-v1.md` | 未抽样检查头部 | 小米下个 review window 复检 |

### 3.3 状态字段缺失

约 11 篇文档没有显式 `状态:` 字段 (DRAFT / ACTIVE / ARCHIVED), CONVENTIONS-naming v1 §3 要求必填. 后续新文档强制, 历史文档不追溯, 但在升 minor 时补。

---

## 4. 跨文档引用 / 死链检查

### 4.1 引用检查 (sample)

| 引用源 | 引用目标 | 状态 |
|--------|---------|------|
| `laohuang-compliance-redline-v1.md` §10 | `docs/RESEARCH/laohuang-compliance-signoff.md` | 活 |
| `laohuang-compliance-signoff.md` | `laohuang-compliance-redline-v1.md` | 活 |
| `laosun-key-management-v1.md` line 433 | `docs/INCIDENTS/runbook-signer.md` | **死链** (未来引用, 文档未落地) |
| `laohan-riskmanager-design-v0.1.md` line 508 | `docs/ADR/ADR-XXX-riskmanager-as-sole-gate.md` | **未来引用** (待立, 不算死链) |
| `laoshen-threat-model-v1.md` 头部 | S1-006, S1-009, S1-010, S1-005 | 活 (ticket 引用) |
| `laojiang-latency-budget-v1.md` 头部 | `laozhou-architecture-v0.1.md` | 活 |

### 4.2 持有人错署 (1 处)

`laoxu-external-tools-inventory-v1.md` 第 25, 66 行把 doc-curator 写成"老郑", 实际 doc-curator 是小米 (#40 in AGENT.md). 老郑 (#11) 是 observability-engineer.

**建议**: 老徐下一版本 v1.1 时修正, 不阻断当前 v1.

### 4.3 未来引用清单 (5 条)

这些是"指向未来要做的事"的链接, 不算死链, 但要追踪:

| 源文档 | 未来引用 | 责任人 | 期望落地 |
|--------|---------|-------|---------|
| `laosun-key-management-v1.md` | `INCIDENTS/runbook-signer.md` | 老孙 + 老吴 | Sprint-2 |
| `laohan-riskmanager-design-v0.1.md` | `ADR/ADR-XXX-riskmanager-as-sole-gate.md` | 老韩 + 老郭 | Sprint-2 |
| (INDEX.md) | `RESEARCH/xiaomi-docs-health-2026-05-28.md` | 小米 | **本次落地** |
| (多处) | Sprint-1 ticket S1-022 / S1-023 输出 | 小尤 / 小宫 | Sprint-1 末 |

---

## 5. 命名规范一致性

发布 `CONVENTIONS-naming.md` v1 后, 现存文档评估:

### 5.1 完全合规 (16 / 17 RESEARCH)

按 `<persona>-<topic-kebab>-v<n>.md` 格式. 例如:
- `laohuang-compliance-redline-v1.md` ✓
- `xiaoshi-data-structures-selection-v1.md` ✓

### 5.2 例外清单 (已在 CONVENTIONS §7 登记)

- `laohuang-compliance-signoff.md` — 跟随主文档版本, 无 `-v<n>` 后缀
- `HIRING/backlog.md`, `KPI/individual-kpi-matrix.md` — 实时滚动文档
- `data/laochen-network-bench-*.csv` — 非 markdown 数据快照

### 5.3 待规范 (本季度内补)

无 ADR / INCIDENTS 实例, 命名规范 §2.1 已列, 等老韩 / 老唐落地第一份样板。

---

## 6. 健康度评级方法 (内部参考)

| 维度 | 权重 | A 标准 | B 标准 | C 标准 |
|------|------|--------|--------|--------|
| 结构完整性 | 20% | 全部 8 目录就位 + 顶级文档齐 | 1-2 目录空但有候选 | 缺少核心目录 |
| 索引同步 | 25% | INDEX 列出全部活跃文档 | 90% 覆盖 | < 80% 覆盖 |
| Frontmatter 一致性 | 20% | 100% 4 字段齐 | 90% 齐 | < 80% 齐 |
| 跨文档引用 | 15% | 无死链 | ≤ 2 死链/未来引用 | > 5 死链 |
| 命名规范 | 10% | 全部合规 | ≤ 2 例外 | 无规范 |
| 漂移监控 | 10% | 自动 lint + Sprint review | 仅 Sprint review | 仅 ad-hoc |

本次得分:
- 结构: 20 × 0.95 = 19.0
- 索引: 25 × 0.85 (修正后 0.95) = 23.75
- Frontmatter: 20 × 0.88 = 17.6
- 引用: 15 × 0.90 = 13.5
- 命名: 10 × 0.95 = 9.5
- 漂移监控: 10 × 0.60 = 6.0

**总分: 89.4 / 100 → B+**

---

## 7. 行动清单 (本报告输出)

| # | 优先级 | 动作 | Owner | 截止 |
|---|--------|------|-------|------|
| H1 | P0 | 修正 `laoxu-external-tools-inventory-v1.md` 中 doc-curator 错署 | 老徐 | 2026-06-04 |
| H2 | P1 | 补全 `laoshen-key-management-coreview-v1.md` 的 Owner / Last review | 老沈 | 2026-06-04 |
| H3 | P1 | 复检 `laoli-polymarket-api-spec-v1.md` 头部 | 小米 | 2026-06-04 |
| H4 | P2 | 历史 11 篇文档补 `状态:` 字段 (升 minor 时顺便) | 各 owner | Sprint-2 末 |
| H5 | P2 | 引入文档 lint 脚本 (检测 frontmatter) | 老吴 + 小米 | M3 节点前 |
| H6 | P3 | 立第一份 ADR (老韩 RiskManager) | 老韩 + 老郭 | Sprint-2 |
| H7 | P3 | 立第一份 INCIDENT runbook (signer SOP) | 老孙 + 老吴 | Sprint-2 |

---

## 8. 下次 review 触发条件

- Sprint-1 retro (2026-06-12) 后
- 或 RESEARCH/ 新增 ≥ 5 篇文档后 (临时触发)
- 或季度末 (2026-08-31) 强制

---

## 附录 A: 本次 review 检查清单 (供后续复用)

- [x] 列举所有 docs/ 子目录文件数
- [x] 比对 INDEX 与实际 RESEARCH 文件 (找漏)
- [x] 抽样 frontmatter 字段 (Owner / Last review / 验收人 / 状态)
- [x] grep 跨文档引用 (找死链 + 未来引用)
- [x] 持有人字段与 AGENT.md persona 对齐
- [x] 命名规范一致性扫描
- [x] 输出评级 + 行动清单
- [x] 更新 INDEX.md `Last updated`

---

**本次 review 由小米完成. 任何对评级或行动项的异议: 老雷仲裁.**
