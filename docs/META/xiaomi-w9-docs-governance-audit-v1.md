# docs/ 全治理审计 v1

- **owner:** 小米 #40 (doc-curator, E 单元)
- **last_review:** 2026-05-29
- **status:** SSOT
- **wave:** W9 W1 (Wave 56 P0)

---

## §1 老板 verbatim — 约束基线

> "让文档管理员整理文档吧, 归纳为新的有用的, 缺啥文档就让人补充, 新的写好旧的可以删除"

解读为三条硬约束:
1. **归纳有用** — 当前 SSOT 文档必须可识别, 状态清晰
2. **缺啥补啥** — 识别缺失文档并派 owner 补充
3. **旧的可删** — 新 SSOT 落地后, 旧版本由 owner 在 W9 W3 清理 (本 wave 不直接删, 只出清单)

---

## §2 现状 Audit

审计日期: 2026-05-29. 以 worktree main 为基准, 全量 `find docs/ -type f | sort` 扫描.

**总文件数 (含 data/ 子目录): ~340 个文件**
(md 文档约 213 个, data/ 原始数据文件约 67 个, 其余 txt/csv/json/sh/rtf)

### docs/ADR/ — 35 个文件

| 文件 | SSOT 状态 |
|---|---|
| 2026-05-28-agent-model-tiering.md | SSOT (ADR-009, v2 校正已入 CLAUDE.md) |
| 2026-05-28-arch-and-rm-v0.1-review.md | Archive (早期评审记录) |
| 2026-05-28-department-manager-mandate.md | SSOT (ADR-005) |
| 2026-05-28-gm-commitment-sygnum-deadline.md | SSOT (GM 承诺) |
| 2026-05-28-gm-decision-defer-aws-until-profitable.md | SSOT (含 RETRACTED 区段, AWS 延期) |
| 2026-05-28-gm-decision-defer-onchain-until-profitable.md | 已删 (GM 2026-05-30 硬删; 决策事实保留: MVP 不上链) |
| 2026-05-28-gm-decision-goalserve-odds-gap.md | SSOT (Goalserve odds gap GM 决议) |
| 2026-05-28-gm-policy-api-monitoring-longterm.md | SSOT |
| 2026-05-28-gm-policy-cross-domain-listening.md | SSOT |
| 2026-05-28-gm-policy-jurisdictional-deferral.md | SSOT (含 RETRACTED 地域合规延期) |
| 2026-05-28-gm-redline-data-source-timestamping.md | SSOT (R-20 红线) |
| 2026-05-28-gm-redline-websocket-non-blocking.md | SSOT (R-12 红线) |
| 2026-05-28-gm-signoff-adr-001.md | Archive (sign-off 记录) |
| 2026-05-28-gm-signoff-adr-003-closeout.md | Archive (含 RETRACTED 注记, ADR-003 关单) |
| 2026-05-28-gm-signoff-paper-trade.md | SSOT |
| 2026-05-28-gm-signoff-rm-v0.2.md | Archive (v0.2 sign-off, v0.3 已迭代) |
| 2026-05-28-gm-signoff-sprint1-retro.md | Archive |
| 2026-05-28-r07-r08-liquidity-vs-position-cap-priority.md | SSOT |
| 2026-06-01-adr-010-test-code-grading.md | SSOT |
| 2026-06-01-adr-011-paper-live-binary.md | 已删 (GM 2026-05-30 硬删; 单 binary 架构仍是现行事实) |
| 2026-06-01-adr-012-wss-topology.md | SSOT |
| 2026-06-01-adr-013-cross-region.md | **Outdated** — 被 ADR-013-v2 supersede |
| 2026-06-01-adr-014-ml-shadow-timing.md | SSOT |
| 2026-06-01-adr-015-vcpu-pin.md | Archive (含 RETRACTED 注记) |
| 2026-06-01-adr-016-g2-ci-lower-threshold.md | SSOT |
| 2026-06-01-adr-017-spsc-ring-framework.md | SSOT |
| 2026-06-15-arch-rm-key-v0.4-v0.3-v5-review.md | Archive (评审记录) |
| 2026-06-W3-adr-018-lib-selection.md | SSOT |
| 2026-06-W3-adr-020-ic-tester-separation.md | **Retracted** (被 ADR-022 撤回, 但保留 audit log) |
| 2026-06-W3-adr-021-worktree-isolation.md | SSOT (被 ADR-024 扩展, 但 021 仍有效) |
| 2026-06-W3-adr-022-ic-self-test-tester-review.md | **Retracted** (被 ADR-023 撤回, 但保留 audit log) |
| 2026-06-W3-adr-023-ic-self-test-only.md | SSOT |
| 2026-06-W3-adr-024-worktree-standard-workflow.md | SSOT |
| 2026-06-W4-adr-013-v2-deployment-location.md | SSOT (Draft, 待 W9 W2 数据确认) |
| 2026-06-W4-adr-027-core-data-structure-ssot-enforce.md | SSOT |

**注:** ADR-025/026 在任务 spec 中提及为"撤回的 ADR" — 实测这两个编号的文件从未在 docs/ADR/ 中创建过. 无需操作.

**小计:** SSOT 21, Archive 7, Outdated 1, Retracted (保留) 2

### docs/GOALSERVER/ — 12 个文件

| 状态 | 文件 |
|---|---|
| SSOT | baseball-data-feed.md, basketball-data-feed.md, esports-data-feed.md, hockey-data-feed.md, soccer-data-feed.md, tennis-data-feed.md, ufc-data-feed.md, full_package_feed_cn.md |
| Archive (原始供应商文档) | feeds_urls.txt, full_package_feed.txt, inplay-feed-new.txt, 网址.rtf |

**注:** 此目录是供应商文档镜像. SSOT 是小段 W8 的 `xiaoduan-w8-goalserve-data-structure-ssot-v1.md` (在 RESEARCH/). GOALSERVER/ 的 md 文件作为原始 reference 保留, 不删.

### docs/HIRING/ — 8 个文件

| 状态 | 文件 |
|---|---|
| SSOT | employee-registry.md, sop-v1.md, backlog.md |
| SSOT | hr-pulse-check-2026-05-28.md (近期快照, 小林 owner) |
| SSOT | jd-onchain-ops-laoji-v1.md, jd-strategy-execution-xiaoqin-v1.md |
| Archive | sprint2-w2-progress.md, sprint2-w3-progress.md (历史招聘进展) |

### docs/INCIDENTS/ — 1 个文件

| 状态 | 文件 |
|---|---|
| SSOT (持续追加) | gm-self-mistakes-log.md |

**注:** 仅 1 个文件. 缺 incident post-mortem 模板 (见 §4 缺失清单).

### docs/MEETINGS/ — 46 个文件 (含 sprint1-retro/ 子目录 15 个)

- **SSOT (近期 W5-W8 会议纪要):** 2026-06-01 系列 (各 input/ vote/ laozhou-actual/ manager-sync 等), 2026-06-W3 系列 (wave 30/32 review/resolution), 2026-05-29 系列
- **Archive:** 2026-05-28 系列 (kickoff/hr-launch/sprint1-retro), sprint1-retro/ 子目录全部 15 个发言稿
- **总计:** SSOT 约 20, Archive 约 26

### docs/META/ — 3 个 (本 wave 写完变 4 个)

| 状态 | 文件 |
|---|---|
| SSOT | escalate-decision-log.md |
| Outdated | weekly-report-template-v2.md — 被 v3 替代 |
| SSOT | weekly-report-template-v3.md |
| SSOT (本文) | xiaomi-w9-docs-governance-audit-v1.md |

### docs/OKR/ — 2 个文件

| 状态 | 文件 |
|---|---|
| SSOT | 2026-Q2-Q3-startup-season.md |
| SSOT | laoqian-w8-w5-profitability-kr-v1.md |

### docs/KPI/ — 1 个文件

| 状态 | 文件 |
|---|---|
| SSOT | individual-kpi-matrix.md |

### docs/RESEARCH/ — 146 个 md 文件 (重灾区)

以下按子主题分组, 标 SSOT / Archive / Outdated / Redundant:

**架构类 (老周)**
- laozhou-architecture-v0.1~v0.5: **Archive** (历史演进, v0.6 是 latest)
- laozhou-architecture-v0.6-e2e.md: **SSOT**
- laozhou-single-instance-spec-v1.md: SSOT
- laozhou-lifecycle-management-v1.md: SSOT
- laozhou-manager-mandate-v1.md: SSOT
- laozhou-vcpu-v0.7-update.md: SSOT
- laozhou-w8-debug-rest-api-spec-v1.md: SSOT
- laozhou-w8-engineering-abi-gap-audit-v1.md: SSOT
- laozhou-firstreview-laoli-abi-handshake.md: Archive

**风控类 (老韩/老沈/老黄/老唐)**
- laohan-riskmanager-design-v0.1~v0.2: **Archive** (被 v0.3 替代)
- laohan-riskmanager-design-v0.3.md: Archive (被 v0.3.1 细化)
- laohan-riskmanager-design-v0.3.1.md: **SSOT** (latest RM design)
- laohan-w8-rm-or-assertion-audit.md: SSOT
- laohan-w8-rm-stale-data-ack.md: SSOT
- laohan-manager-mandate-v1.md: SSOT
- laohuang-compliance-redline-v1.md: Archive (被 v2 替代)
- laohuang-compliance-redline-v2.md: **SSOT**
- laohuang-compliance-signoff.md: Archive
- laohuang-shamir-jurisdiction-signoff-v1.md: SSOT
- laohuang-us-entity-feasibility-v1.md: SSOT
- laoshen-threat-model-v1.md: Archive (被 v2 替代)
- laoshen-threat-model-v2.md: **SSOT**
- laoshen-key-management-coreview-v1.md: SSOT
- laoshen-key-vendor-selection-v2.md: SSOT
- laoshen-multi-vendor-kms-v1.md: SSOT
- laotang-audit-schema-v1.md: Archive (被 v1.1 替代)
- laotang-audit-schema-v1.1.md: **SSOT**
- laotang-friend-class-firstmismatch-w7-cleanup.md: Archive (cleanup 记录)

**Key Management (老孙)**
- laosun-key-management-v1~v3: **已删** (历史 Rust 版本, GM 2026-05-30 硬删)
- laosun-key-management-v4-cpp.md: Archive (C++ 版, 已被 v5 替代)
- laosun-key-management-v5-simplified.md: Archive (被 v5.1 替代)
- laosun-key-management-v5.1.md: **SSOT**
- laosun-libsodium-fetchcontent-w7-plan.md: SSOT
- laoshan-monocypher-vs-libsodium-w7-ack.md: SSOT
- laoli-laoSun-handshake-v1.md: Archive

**Rust 废弃文档 (老张)**
- laozhang-rust-engineering-stack-v1.md: **已删** (GM 2026-05-30 硬删)
- laozhang-rust-signer-crates-v1.md: **已删** (同上)

**Polymarket 数据 (老李)**
- laoli-polymarket-api-spec-v1.md: Archive (被 endpoint-matrix + SSOT 替代)
- laoli-polymarket-backend-requirements-v1.md: Archive
- laoli-polymarket-endpoint-matrix-v2.md: **Outdated** (被 v3 替代)
- laoli-polymarket-endpoint-matrix-v3.md: **SSOT**
- laoli-polymarket-reverse-sports-live-v1.md: SSOT (reverse 工程记录)
- laoli-w8-polymarket-data-structure-ssot-v1.md: **SSOT (ADR-027 强 enforce)**
- laoli-xiaoduan-api-call-optimization-v1.md: SSOT

**Goalserve 数据 (小段)**
- xiaoduan-goalserve-api-spec-v1.md: Archive (被 official-doc v3 替代)
- xiaoduan-goalserve-endpoint-matrix-v2.md: **Outdated** (被 SSOT 替代)
- xiaoduan-goalserve-odds-by-sport-v2.1.md: SSOT (保留, 独立 odds spec)
- xiaoduan-goalserve-official-doc-v3.md: SSOT
- xiaoduan-w8-goalserve-data-structure-ssot-v1.md: **SSOT (ADR-027 强 enforce)**
- xiaoduan-w8-goalserve-inplay-node-verification-v1.md: SSOT

**网络基准 (老陈/老吴)**
- laochen-network-bench-v1.md: Archive (早期 W2 bench 记录)
- laochen-api-rate-latency-ssot-v1.md: **SSOT** (supersedes bench-v1)
- laowu-proxy-goalserve-bandwidth-v1.md: SSOT
- laowu-cross-region-deployment-v0.1.md: SSOT
- laowu-toolstack-install-v1.md: SSOT
- laowu-w8-polymarket-origin-verification-v1.md: SSOT

**量化/策略 (小梁/小程/小蒋/老彭/小袁/小晓)**
- xiaoliang-market-structure-v1.md: SSOT
- xiaoliang-manager-mandate-v1.md: SSOT
- xiaocheng-signal-catalog-v1.md: SSOT
- xiaocheng-p0_02-signal-spec-v0.1.md: SSOT
- laopeng-betting-industry-analysis-v1.md: SSOT
- laopeng-bookmaker-history-backfill-v1.md: SSOT
- laopeng-multiplicative-devig-calibration-v1.md: SSOT
- laopeng-w8-oq-p02-3-inplay-single-source-ack.md: SSOT
- xiaoxiao-kelly-slippage-model-v1.md: SSOT
- xiaoxiao-slippage-model-lib-v1.md: SSOT
- xiaoyuan-fill-rate-model-v0.1.md: SSOT
- xiaoyuan-microstructure-v1.md: SSOT

**回测/Paper (小蒋)**
- xiaojiang-backtest-framework-v0.1.md: Archive (被 v0.2 替代)
- xiaojiang-backtest-framework-v0.2-cpp.md: **SSOT**
- xiaojiang-paper-trading-engine-v0.1.md: Archive (被 v0.2 替代)
- xiaojiang-paper-trading-engine-v0.2-cpp.md: **SSOT**
- xiaojiang-paper-engine-skeleton-v1.md: SSOT

**数据基础设施 (小余/小董/小田/小段/小冯)**
- xiaotian-parquet-partition-v1.md: SSOT
- xiaodeng-ml-data-infra-v1.md: SSOT
- xiaodeng-ml-data-pipeline-v0.1.md: Archive (被 ml-data-infra 替代)
- xiaodeng-ml-roadmap-v2.md: SSOT
- xiaodeng-ml-roadmap-data-needs-v1.md: Archive (被 v2 吸收)
- data-contract-v1.md: SSOT

**延迟/vCPU (老姜)**
- laojiang-latency-budget-v1.md: Archive (被 w4-wave21 版替代)
- laojiang-latency-budget-w4-wave21-v1.md: **SSOT**
- laojiang-vcpu-pin-baseline-v1.md: SSOT

**WAL 框架 (老王)**
- laowang-wal-framework-v0.1.md: Archive (被 v0.2 替代)
- laowang-wal-framework-v0.2.md: Archive (被 cpp-interface 替代)
- laowang-wal-framework-cpp-interface-v1.md: **SSOT**
- laowang-tomarketidarray-todo-w7-cleanup.md: Archive (cleanup 完成后归档)

**测试框架 (小宋)**
- xiaosong-test-framework-v0.2.md: SSOT
- xiaosong-test-framework-cpp-skeleton-v1.md: SSOT
- xiaosong-test-replay-framework-v0.1.md: SSOT
- xiaosong-adr010-wno-check-grep-spec.md: SSOT
- xiaosong-grandfather-cleanup-v1.md: Archive (cleanup 完成归档)
- xiaosong-wno-cleanup-extension-w7-plan.md: SSOT

**工程规范 (老高/老何/小白)**
- laogao-code-conventions-v1.md: SSOT
- laogao-pr-review-v1.1~v1.4: **Archive** (被 v1.5 替代)
- laogao-pr-review-v1.5.md: **SSOT**
- laohe-cpp-footgun-checklist-v1.md: SSOT
- laohe-cpp-version-selection-v1.md: SSOT
- xiaobai-llm-dev-conventions-v1.md: SSOT

**顾问团/协调 (老郭/老徐)**
- laoguo-coordinator-mandate-v1.md: SSOT
- laoguo-w6-gm-commit-audit.md: Archive (W6 审计记录)
- laoguo-w8-w5-adr-027-main-review.md: SSOT
- laoxu-external-tools-inventory-v1.md: SSOT
- laoxu-r39-escalate-flow-v0.3.md: SSOT
- laoxu-subagent-escalate-flow-v0.2.md: Archive (被 v0.3 替代)
- laoxu-w6-persona-boundary-dryrun-v1.md: Archive

**产品/PM (老胡/老钱/小杜/小宋/小颖/小尤/小宫)**
- laohu-manager-mandate-v1.md: SSOT
- laohu-master-gantt-v1.md: SSOT
- laohu-risk-registry-v1.md: Archive (被 v2 替代)
- laohu-risk-registry-v2.md: Archive (被 v2.1 替代)
- laohu-risk-registry-v2.1.md: Archive (被 v2.2 替代)
- laohu-risk-registry-v2.2.md: Archive (被 v2.3 替代)
- laohu-risk-registry-v2.3.md: Archive (被 v2.4 替代)
- laohu-risk-registry-v2.4.md: **SSOT**
- laoqian-business-capabilities-v1.md: SSOT
- laoqian-mvp-scope-rejection-v1.md: SSOT
- xiaodu-mvp-prd-v1.md: Archive (被 v2 替代)
- xiaodu-prd-v2-user-journey.md: SSOT
- xiaoying-acceptance-criteria-v1.md: Archive (被 spec v1 替代)
- xiaoying-acceptance-spec-v1.md: SSOT
- xiaoyou-ux-framework-v1.md: Archive (被 interface-requirements 替代)
- xiaoyou-ux-interface-requirements-v1.md: SSOT
- xiaosu-backend-api-requirements-v1.md: SSOT
- xiaosu-ui-wireframe-v0.1.md: SSOT
- xiaogong-dogfood-playbook-v1.md: SSOT

**数据结构 (小石)**
- xiaoshi-data-structures-selection-v1.md: SSOT

**观测 (小郑)**
- xiaozheng-observability-v0.1.md: SSOT

**M45 Gate (小董)**
- xiaodong-m45-gate-framework-v1.md: SSOT
- xiaodong-stats-validation-framework-v1.md: SSOT

**小米历史 doc**
- xiaomi-docs-health-2026-05-28.md: Archive (被 v2 替代)
- xiaomi-docs-health-2026-05-28-v2.md: Archive (被本文 w9 版替代)
- xiaomi-r20-backfill-2026-05-28.md: Archive (backfill 完成)

**其他**
- laowu-w8-polymarket-origin-verification-v1.md: SSOT (see 网络基准)
- laojiang-latency-budget-v1.md: (见 延迟 section)

**RESEARCH/ 小计 (约):** SSOT ~90, Archive ~45, Outdated ~8, Redundant 0

### docs/RUNBOOKS/ — 1 个文件

| 状态 | 文件 |
|---|---|
| SSOT | strategy-decayed-unlock-sop-v1.md |

缺失严重, 见 §4.

### docs/SPRINTS/ — 12 个文件

| 状态 | 文件 |
|---|---|
| SSOT | sprint-02.md, sprint-02-w8-w5-progress.md, sprint-03-backlog.md, milestone-progress-w7.md |
| Archive | sprint-01.md, sprint-01-final.md, sprint-02-w1~w7 系列 (7 个历史进度) |

**建议:** sprint-02-w1~w7 归入 `docs/SPRINTS/archive/` 子目录 (不删, 维护历史)

### docs/ 根目录 — 2 个文件

| 状态 | 文件 |
|---|---|
| SSOT (小米 owner) | INDEX.md |
| SSOT | CONVENTIONS-naming.md |

**INDEX.md 现状:** 202 行, 手工维护, 未覆盖 W8 以来新增文件, 估计遗漏 ~30+ 条目.

---

## §3 归纳: 新 SSOT 清单

以下文档为当前最权威、全员应读 SSOT. 来源: W4-W9 新立 + 已升 SSOT 验证.

| # | 文档路径 | Owner | 说明 |
|---|---|---|---|
| 1 | `CLAUDE.md` | 老雷 + 小米 | 公司宪法, 最高 SSOT |
| 2 | `AGENT.md` | 老雷 + 小林 | 班底索引 SSOT |
| 3 | `docs/INDEX.md` | 小米 | doc 导航 SSOT (W9 W2 升级) |
| 4 | `docs/ADR/2026-06-W4-adr-027-core-data-structure-ssot-enforce.md` | 老郭 | 数据结构 SSOT 流程红线 |
| 5 | `docs/ADR/2026-06-W4-adr-013-v2-deployment-location.md` | 老郭 | 选址 Draft (待 W9 W2 GM ack) |
| 6 | `docs/ADR/2026-06-W3-adr-024-worktree-standard-workflow.md` | 老郭 | worktree 工作流 SSOT |
| 7 | `docs/ADR/2026-06-W3-adr-023-ic-self-test-only.md` | 老郭 | IC 自测 SSOT |
| 8 | `docs/RESEARCH/laoli-w8-polymarket-data-structure-ssot-v1.md` | 老李 | Polymarket 数据结构 SSOT (ADR-027 强 enforce) |
| 9 | `docs/RESEARCH/xiaoduan-w8-goalserve-data-structure-ssot-v1.md` | 小段 | Goalserve 数据结构 SSOT (ADR-027 强 enforce) |
| 10 | `docs/RESEARCH/laozhou-w8-engineering-abi-gap-audit-v1.md` | 老周 | 工程 ABI gap 审计 SSOT |
| 11 | `docs/RESEARCH/laozhou-architecture-v0.6-e2e.md` | 老周 | 最新架构 SSOT |
| 12 | `docs/RESEARCH/laohan-riskmanager-design-v0.3.1.md` | 老韩 | RM 设计 latest SSOT |
| 13 | `docs/RESEARCH/laosun-key-management-v5.1.md` | 老孙 | Key Mgmt SSOT (C++ 版) |
| 14 | `docs/RESEARCH/laotang-audit-schema-v1.1.md` | 老唐 | Audit schema SSOT |
| 15 | `docs/RESEARCH/laogao-pr-review-v1.5.md` | 老高 | PR review checklist SSOT |
| 16 | `docs/RESEARCH/laochen-api-rate-latency-ssot-v1.md` | 老陈 | API rate/latency SSOT |
| 17 | `docs/RESEARCH/laojiang-latency-budget-w4-wave21-v1.md` | 老姜 | 延迟预算 SSOT |
| 18 | `docs/RESEARCH/laowang-wal-framework-cpp-interface-v1.md` | 老王 | WAL C++ interface SSOT |
| 19 | `docs/RESEARCH/laohu-risk-registry-v2.4.md` | 老胡 | 风险登记 latest SSOT |
| 20 | `docs/OKR/laoqian-w8-w5-profitability-kr-v1.md` | 老钱 | 盈利 KR SSOT |
| 21 | `docs/OKR/2026-Q2-Q3-startup-season.md` | 老雷 | 季度 OKR SSOT |
| 22 | `docs/HIRING/employee-registry.md` | 小林 | HR 注册 SSOT |
| 23 | `docs/INCIDENTS/gm-self-mistakes-log.md` | 老雷 + 老胡 | GM 错误日志 (持续追加) |
| 24 | `docs/META/weekly-report-template-v3.md` | 老胡 | 周报模板 SSOT |

---

## §4 缺失 Doc 清单

以下文档当前不存在, 需要 owner 在指定 wave 内补充.

| # | 文档路径 | Owner | 补充时间 | 说明 |
|---|---|---|---|---|
| 1 | `docs/HIRING/wave-a-onboard-checklist.md` | 小林 | W9 W1 | Wave A* (新招人员) onboard SOP + employee-registry 更新流程 |
| 2 | `docs/RESEARCH/laoli-xiaoduan-cross-source-mapping-v1.md` | 老李 + 小段 | W9 W2 | 跨数据源字段 mapping (Polymarket event_id ↔ Goalserve fixture_id 等), ADR-027 要求 |
| 3 | `docs/RESEARCH/laoli-wss-subscriber-impl-spec-v1.md` | 老李 | W9 W2 | WSS subscriber 实施 spec (W8 W2 ack 双订阅, 但工程实施 spec 缺失) |
| 4 | `docs/RESEARCH/laopeng-nba-signal-spec-v1.md` | 老彭 | W9 W2 | NBA sport-specific signal spec (OQ 系列已有 NFL/Soccer, 缺 NBA) |
| 5 | `docs/RESEARCH/laopeng-tennis-signal-spec-v1.md` | 老彭 | W9 W3 | Tennis signal spec |
| 6 | `docs/RESEARCH/laopeng-volleyball-signal-spec-v1.md` | 老彭 | W9 W3 | Volleyball signal spec |
| 7 | `docs/RESEARCH/laozhou-w9-rest-api-user-doc-v1.md` | 老周 | W9 W2 | 后端 REST API 用户文档 (老周 W8 W4 spec + 小卢 W9 实施 spec 落地后补 user-facing doc) |
| 8 | `docs/RUNBOOKS/m5-live-cutover-v1.md` | 老周 + 老胡 | W9 W3 | M5 live 上线 runbook (paper runtime → M5 live 切换流程, W11 前必须就绪) |
| 9 | `docs/RUNBOOKS/audit-schema-migration-v1.md` | 老唐 + 小冯 | W9 W2 | 数据迁移 plan (audit schema v1.1 → v1.2 ABI breaking 关联, 下游 impact) |
| 10 | `docs/META/laoguo-advisory-board-opinions-v1.md` | 老郭 | W9 W2 | 顾问团意见箱 historical archive (老郭 W9 W1 启动) |
| 11 | `docs/SPRINTS/sprint-03-w9-abi-fix-timeline.md` | 老胡 | W9 W1 | Sprint-3 W9 启动, ABI 修复 timeline (W9-W10-W11) 明确 |
| 12 | `docs/RUNBOOKS/incident-postmortem-template-v1.md` | 老胡 | W9 W1 | incident post-mortem 模板 (INCIDENTS/ 目前只有 GM log, 缺通用模板) |
| 13 | `docs/META/adr-028-doc-governance-standard.md` | 老郭 (小米 spec) | W9 W2 | doc 治理 standard ADR (见 §7) |

**合计: 13 个缺失文档, 分配给 8 个 owner**

---

## §5 旧 Doc 删除清单

以下文档在对应新 SSOT 已稳定落 main 后, 由 owner 在 **W9 W3** 执行删除或归档操作. 小米本 wave (W9 W1) 不直接删.

**重要前置条件: 删除前必须新 SSOT 已合并 main, 且 INDEX.md 已更新.**

### 5.1 可直接删除 (内容完全被新版取代, 无独立历史价值)

| 文件 | 理由 | 新 SSOT | 删除 owner |
|---|---|---|---|
| `docs/RESEARCH/laoli-polymarket-endpoint-matrix-v2.md` | 完全被 v3 取代, v3 明确说"沿用 v2 probe" | v3 | 老李 W9 W3 |
| `docs/RESEARCH/laohan-riskmanager-design-v0.1.md` | 设计演进早期稿, v0.3.1 完整覆盖 | v0.3.1 | 老韩 W9 W3 |
| `docs/RESEARCH/laohan-riskmanager-design-v0.2.md` | 同上 | v0.3.1 | 老韩 W9 W3 |
| `docs/RESEARCH/laoshen-threat-model-v1.md` | 被 v2 完全替代 | v2 | 老沈 W9 W3 |
| `docs/RESEARCH/laotang-audit-schema-v1.md` | 被 v1.1 取代 | v1.1 | 老唐 W9 W3 |
| `docs/META/weekly-report-template-v2.md` | 被 v3 取代 | v3 | 老胡 W9 W3 |
| `docs/RESEARCH/xiaodeng-ml-data-pipeline-v0.1.md` | 被 ml-data-infra-v1 吸收 | infra-v1 | 小邓 W9 W3 |
| `docs/RESEARCH/xiaodeng-ml-roadmap-data-needs-v1.md` | 被 ml-roadmap-v2 吸收 | v2 | 小邓 W9 W3 |
| `docs/RESEARCH/laohu-risk-registry-v1.md` | 五轮迭代, v1 已无独立内容 | v2.4 | 老胡 W9 W3 |
| `docs/RESEARCH/laohu-risk-registry-v2.md` | 同上 | v2.4 | 老胡 W9 W3 |
| `docs/RESEARCH/laohu-risk-registry-v2.1.md` | 同上 | v2.4 | 老胡 W9 W3 |
| `docs/RESEARCH/laohu-risk-registry-v2.2.md` | 同上 | v2.4 | 老胡 W9 W3 |
| `docs/RESEARCH/laohu-risk-registry-v2.3.md` | 同上 | v2.4 | 老胡 W9 W3 |
| `docs/RESEARCH/xiaomi-docs-health-2026-05-28.md` | 被 v2 替代, v2 被本文替代 | 本文 | 小米 W9 W3 |
| `docs/RESEARCH/xiaomi-docs-health-2026-05-28-v2.md` | 被本文替代 | 本文 | 小米 W9 W3 |
| `docs/RESEARCH/laoxu-subagent-escalate-flow-v0.2.md` | 被 v0.3 替代 | v0.3 | 老徐 W9 W3 |
| `docs/RESEARCH/xiaojiang-backtest-framework-v0.1.md` | 被 v0.2-cpp 替代 | v0.2-cpp | 小蒋 W9 W3 |
| `docs/RESEARCH/xiaojiang-paper-trading-engine-v0.1.md` | 被 v0.2-cpp 替代 | v0.2-cpp | 小蒋 W9 W3 |
| `docs/RESEARCH/xiaodu-mvp-prd-v1.md` | 被 prd-v2-user-journey 替代 | v2 | 小杜 W9 W3 |
| `docs/RESEARCH/xiaoying-acceptance-criteria-v1.md` | 被 acceptance-spec-v1 替代 | spec-v1 | 小颖 W9 W3 |
| `docs/RESEARCH/xiaoyou-ux-framework-v1.md` | 被 interface-requirements-v1 替代 | req-v1 | 小尤 W9 W3 |
| `docs/RESEARCH/laojiang-latency-budget-v1.md` | 被 w4-wave21-v1 替代 | wave21-v1 | 老姜 W9 W3 |
| `docs/RESEARCH/laowang-wal-framework-v0.1.md` | 被 cpp-interface-v1 替代 | cpp-interface | 老王 W9 W3 |
| `docs/RESEARCH/laowang-wal-framework-v0.2.md` | 同上 | cpp-interface | 老王 W9 W3 |
| `docs/ADR/2026-06-01-adr-013-cross-region.md` | 被 ADR-013-v2 supersede, 需加 SUPERSEDED 注记后归档或删 | ADR-013-v2 | 老郭 W9 W3 |

### 5.2 归档 (移入 archive/ 子目录, 不删除, 保留历史)

| 类别 | 操作 |
|---|---|
| `docs/SPRINTS/sprint-01.md`, `sprint-01-final.md`, `sprint-02-w1~w7 系列` | 移入 `docs/SPRINTS/archive/` |
| `docs/MEETINGS/sprint1-retro/` 子目录全部 15 个发言稿 | 移入 `docs/MEETINGS/archive/sprint1-retro/` |
| laozhou-architecture-v0.1~v0.5 | 移入 `docs/RESEARCH/archive/` |
| laogao-pr-review-v1.1~v1.4 | 移入 `docs/RESEARCH/archive/` |
| laohan-riskmanager-design-v0.3 (非 v0.3.1) | 移入 `docs/RESEARCH/archive/` |
| laosun-key-management-v1~v3 | 已删 (GM 2026-05-30 硬删, Rust 废弃版); v4 移入 ARCHIVE/ |
| laohuang-compliance-redline-v1.md | 移入 `docs/RESEARCH/archive/` |

### 5.3 保留但标注状态的文档 (不删, 不移)

| 文件 | 操作 |
|---|---|
| ADR-020 (ic-tester-separation) | 保留, 已有 RETRACTED 内容, 是 audit log |
| ADR-022 (ic-self-test-tester-review) | 保留, 已标 Supersedes ADR-020 |
| `laozhang-rust-engineering-stack-v1.md` | 已删 (GM 2026-05-30 硬删) |
| `laozhang-rust-signer-crates-v1.md` | 已删 (同上) |

**ADR-025/026 实查结论:** 这两个编号的文件从未在 docs/ADR/ 中创建. 无操作.

**PositionManager 撤回 spec:** 实查无独立的 laoqian-w8-position-manager-prd-v1.md 文件存在. OKR 目录下 laoqian-w8-w5-profitability-kr-v1.md 是 KR 文档, 非 PositionManager PRD. 无需删除操作.

---

## §6 docs/INDEX.md Update Plan

**现状:** 202 行, 手工维护, W8 以来新增文档未录入, 估计遗漏 30+ 条目.

**升级目标 (W9 W2):**

1. **frontmatter 标准化:** 每条目标注 `status` (SSOT / Archive / Outdated) + `owner` + `last_review`
2. **自动化辅助:** 老高 #16 在 W9 W2 实施 `doc_governance_check.py` (见 §7), 可 grep 生成 SSOT 列表草稿, 小米 review 后手工入 INDEX.md
3. **分区重组:** 当前平铺改为按目录分区索引, 每目录一节
4. **状态标记颜色约定 (文本):** `[SSOT]` / `[Archive]` / `[Outdated]` / `[Draft]`

**W9 W2 派单:** 小米执行 INDEX.md update, 老高 W9 W2 提供 `doc_governance_check.py` 输出作为草稿输入.

---

## §7 Doc 治理 Standard (ADR-028 候选)

**立项:** 小米 spec (本节), 老郭 W9 W2 主审, 落入 `docs/META/adr-028-doc-governance-standard.md`.

### 核心规则

1. **frontmatter 必含字段** (所有 docs/ 下 .md):
   ```
   - owner: <姓名> (<工号>)
   - last_review: YYYY-MM-DD
   - status: SSOT | Archive | Outdated | Draft | Retracted
   ```

2. **SSOT 季度 review 强 enforce:**
   - 3 个月未 update → 黄色警告, 派 owner verify
   - 6 个月未 update → 红色, owner 必须 W 内响应或降级为 Archive

3. **Retracted doc 处理规范:**
   - 必须添加 `## RETRACTED 2026-XX-XX by <姓名>\n理由: ...` 区段
   - 不删除文件 (CLAUDE.md §7 "公开失败" 原则)
   - status 字段改为 `Retracted`

4. **版本迭代规范:**
   - 旧版本在新版落 main 后 status 自动降为 Archive
   - Archive 文档由 owner 在 W9 W3 或下一 sprint 清理至 archive/ 子目录
   - 同一文档超过 3 个历史版本, 只保留最新 2 个 + 最旧 1 个 (其余删除)

5. **新文档创建规范:**
   - 路径遵循 `docs/<分类>/<owner>-<topic>-v<N>.md`
   - 创建即登记 INDEX.md (owner 责任, 小米 W9 每周抽查)
   - 数据源类文档须附四维扫描记录 (CLAUDE.md §8 红线 R-33)

6. **CI 自动检查 (老高 W9 W2 实施):**
   - `doc_governance_check.py`: grep 所有 .md frontmatter, 报告缺 owner / status 的文档
   - 输出: `docs/META/doc-governance-report-<date>.txt` (只读报告, 不入 git)
   - 触发: 每次 PR + 每周自动跑

---

## §8 W9 W1-W3 派单

### W9 W1 (本 wave 完成)

| 任务 | Owner | 状态 |
|---|---|---|
| docs/ 全量 audit + 本文 §1-9 | 小米 | 完成 |
| 老周 / 老李 / 小段 review SSOT 列表 | 老周, 老李, 小段 | 待 ack |

### W9 W2

| 任务 | Owner | 截止 |
|---|---|---|
| docs/HIRING/wave-a-onboard-checklist.md | 小林 | W9 W2 |
| docs/RESEARCH/laoli-xiaoduan-cross-source-mapping-v1.md | 老李 + 小段 | W9 W2 |
| docs/RESEARCH/laoli-wss-subscriber-impl-spec-v1.md | 老李 | W9 W2 |
| docs/RESEARCH/laopeng-nba-signal-spec-v1.md | 老彭 | W9 W2 |
| docs/RUNBOOKS/audit-schema-migration-v1.md | 老唐 + 小冯 | W9 W2 |
| docs/META/laoguo-advisory-board-opinions-v1.md | 老郭 | W9 W2 |
| docs/META/adr-028-doc-governance-standard.md (ADR-028) | 老郭 (小米 spec 输入) | W9 W2 |
| docs/INDEX.md 全面更新 + 状态标注 | 小米 | W9 W2 |
| doc_governance_check.py CI 脚本 | 老高 | W9 W2 |
| docs/SPRINTS/sprint-03-w9-abi-fix-timeline.md | 老胡 | W9 W1 末 |
| docs/RUNBOOKS/incident-postmortem-template-v1.md | 老胡 | W9 W1 末 |

### W9 W3

| 任务 | Owner | 截止 |
|---|---|---|
| docs/RESEARCH/laopeng-tennis-signal-spec-v1.md | 老彭 | W9 W3 |
| docs/RESEARCH/laopeng-volleyball-signal-spec-v1.md | 老彭 | W9 W3 |
| docs/RESEARCH/laozhou-w9-rest-api-user-doc-v1.md | 老周 | W9 W3 |
| docs/RUNBOOKS/m5-live-cutover-v1.md | 老周 + 老胡 | W9 W3 |
| §5.1 可删文档: 各 owner 执行删除 | 见 §5.1 表格 | W9 W3 |
| §5.2 归档操作: 各 owner 移入 archive/ | 见 §5.2 表格 | W9 W3 |
| 小米 verify docs/ 全状态干净 | 小米 | W9 W3 末 |

---

## §9 不耻下问

- **@总裁 (Claude):** ack 治理方向 + §5 删除清单 sign-off, W9 W1
- **@老郭:** ADR-028 主审 W9 W2; ADR-013-v2 处理 adr-013-cross-region 旧文件
- **@老高:** doc_governance_check.py W9 W2 实施
- **@老李:** laoli-w8-polymarket-data-structure-ssot-v1.md 自审 (owner 确认 SSOT 正确性)
- **@小段:** xiaoduan-w8-goalserve-data-structure-ssot-v1.md 自审
- **@老周:** 工程 doc archive 清单确认 (laozhou-architecture v0.1~v0.5 归档)
- **@老胡:** PM 周报 doc 状态指标 (sprint-03 周报加 §doc-health 一栏)
- **@小林:** employee-registry W9 W1 补新员工登记 (wave A*)
- **@9 顾问 (老郭转):** ADR-028 治理 standard 意见征集 W9 W2
