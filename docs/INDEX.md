# docs/ 索引 (SSOT) v3

- **owner:** 小米 #40 (doc-curator, E 单元)
- **last_review:** 2026-05-29
- **status:** SSOT

> 守护: 小米 (doc-curator). 每个文档必须有 owner + last_review + status.
> 入档审批: 小米; 战略级文档 (OKR / ADR / CONVENTIONS-*) 需老雷会签.
> 命名规范: 见 [`CONVENTIONS-naming.md`](CONVENTIONS-naming.md).
> 当前版本: v3 (2026-05-29, W9 W2 全量扫描 + W5-W9 新增补录 + 状态标注)
> 上次版本: v2 (2026-05-28, Sprint-2 W2 启动)

状态枚举: `[SSOT]` `[Draft]` `[Archive]` `[Outdated]` `[Retracted]`

---

## 一级分类

| 目录 | 用途 | Owner |
|---|---|---|
| `ADR/` | 架构决策记录 + GM 决议 | 老郭 + 老雷 |
| `CONVENTIONS-naming.md` | 文档命名规范 | 小米 |
| `GOALSERVER/` | Goalserve feed 文档原件 | 小段 |
| `HIRING/` | 扩招 backlog + JD + SOP + 注册 | 小林 |
| `INCIDENTS/` | 事故 post-mortem + GM 自承认错 | 老唐 + 老雷 |
| `INDEX.md` | 本文件 — 文档导航 SSOT | 小米 |
| `KPI/` | 个人 / 团队 KPI 矩阵 | 老雷 + 小林 |
| `MEETINGS/` | 会议纪要 (按日期) | 小米 |
| `META/` | 治理工具 + 模板 + 审计 | 小米 + 老胡 |
| `OKR/` | 季度 / 半年 OKR | 老雷 + 各单元 owner |
| `RESEARCH/` | 技术预研报告 | 各 owner |
| `RUNBOOKS/` | 运营 runbook + SOP | 老周 + 老胡 |
| `SPRINTS/` | Sprint backlog / retro / final | 老胡 |

---

## 顶级文档

- [`INDEX.md`](INDEX.md) — 本文件 v3 [SSOT]
- [`CONVENTIONS-naming.md`](CONVENTIONS-naming.md) — 文档命名规范 v1 (小米) [SSOT]

---

## ADR

按时间线排列. ADR 编号 owner = 老郭 (架构评审协调人).
**注:** 所有 ADR frontmatter `last_review` 字段缺失 — 老郭 W9 W4 批量补 (见 frontmatter coverage 清单).

### 2026-05-28 (Sprint-1 收口日)

- [`ADR/2026-05-28-arch-and-rm-v0.1-review.md`](ADR/2026-05-28-arch-and-rm-v0.1-review.md) — ADR-001 老周架构 + 老韩 RM v0.1 联合评审 [Archive]
- [`ADR/2026-05-28-agent-model-tiering.md`](ADR/2026-05-28-agent-model-tiering.md) — ADR-009 v2 模型分级 (Sonnet 全员) [SSOT]
- [`ADR/2026-05-28-department-manager-mandate.md`](ADR/2026-05-28-department-manager-mandate.md) — ADR-005 主管制度 mandate [SSOT]
- [`ADR/2026-05-28-gm-commitment-sygnum-deadline.md`](ADR/2026-05-28-gm-commitment-sygnum-deadline.md) — GM 承诺 Sygnum 6/11 contact-made [SSOT]
- [`ADR/2026-05-28-gm-decision-defer-aws-until-profitable.md`](ADR/2026-05-28-gm-decision-defer-aws-until-profitable.md) — GM 决议 AWS 延后至盈利后 (含 RETRACTED 区段) [SSOT]
- `ADR/2026-05-28-gm-decision-defer-onchain-until-profitable.md` — GM 决议 延后链上深度优化至盈利后 [已删]
- [`ADR/2026-05-28-gm-decision-goalserve-odds-gap.md`](ADR/2026-05-28-gm-decision-goalserve-odds-gap.md) — GM 决议 Goalserve odds 缺口 [SSOT]
- [`ADR/2026-05-28-gm-policy-api-monitoring-longterm.md`](ADR/2026-05-28-gm-policy-api-monitoring-longterm.md) — GM 政策 API 长期监控 [SSOT]
- [`ADR/2026-05-28-gm-policy-cross-domain-listening.md`](ADR/2026-05-28-gm-policy-cross-domain-listening.md) — GM 政策 跨域听取义务 [SSOT]
- [`ADR/2026-05-28-gm-policy-jurisdictional-deferral.md`](ADR/2026-05-28-gm-policy-jurisdictional-deferral.md) — GM 政策 地域法律暂不纠缠 (含 RETRACTED 注) [SSOT]
- [`ADR/2026-05-28-gm-redline-data-source-timestamping.md`](ADR/2026-05-28-gm-redline-data-source-timestamping.md) — GM 红线 R-20 4 时间戳契约 [SSOT]
- [`ADR/2026-05-28-gm-redline-websocket-non-blocking.md`](ADR/2026-05-28-gm-redline-websocket-non-blocking.md) — GM 红线 R-12 WebSocket 不阻塞 [SSOT]
- [`ADR/2026-05-28-gm-signoff-adr-001.md`](ADR/2026-05-28-gm-signoff-adr-001.md) — GM sign-off ADR-001 Accepted [Archive]
- [`ADR/2026-05-28-gm-signoff-adr-003-closeout.md`](ADR/2026-05-28-gm-signoff-adr-003-closeout.md) — GM sign-off ADR-003 关单 (含 RETRACTED) [Archive]
- [`ADR/2026-05-28-gm-signoff-paper-trade.md`](ADR/2026-05-28-gm-signoff-paper-trade.md) — GM sign-off paper engine R-11 [SSOT]
- [`ADR/2026-05-28-gm-signoff-rm-v0.2.md`](ADR/2026-05-28-gm-signoff-rm-v0.2.md) — GM sign-off RM v0.2 [Archive]
- [`ADR/2026-05-28-gm-signoff-sprint1-retro.md`](ADR/2026-05-28-gm-signoff-sprint1-retro.md) — GM 决议总表 Sprint-1 Retro (D-01~D-18) [Archive]
- [`ADR/2026-05-28-r07-r08-liquidity-vs-position-cap-priority.md`](ADR/2026-05-28-r07-r08-liquidity-vs-position-cap-priority.md) — R-07/R-08 流动性 vs 头寸上限优先级 [SSOT]

### 2026-06-01 (Sprint-2 W5)

- [`ADR/2026-06-01-adr-010-test-code-grading.md`](ADR/2026-06-01-adr-010-test-code-grading.md) — ADR-010 测试代码分级 [SSOT]
- `ADR/2026-06-01-adr-011-paper-live-binary.md` — ADR-011 paper/live 单 binary 架构 [已删]
- [`ADR/2026-06-01-adr-012-wss-topology.md`](ADR/2026-06-01-adr-012-wss-topology.md) — ADR-012 WSS 拓扑 [SSOT]
- [`ADR/2026-06-01-adr-013-cross-region.md`](ADR/2026-06-01-adr-013-cross-region.md) — ADR-013 v1 跨洋选址 [Outdated — 被 ADR-013-v2 supersede; 老郭 W9 W3 加 SUPERSEDED 注记后归档]
- [`ADR/2026-06-01-adr-014-ml-shadow-timing.md`](ADR/2026-06-01-adr-014-ml-shadow-timing.md) — ADR-014 ML shadow timing [SSOT]
- [`ADR/2026-06-01-adr-015-vcpu-pin.md`](ADR/2026-06-01-adr-015-vcpu-pin.md) — ADR-015 vCPU pin (含 RETRACTED 注) [Archive]
- [`ADR/2026-06-01-adr-016-g2-ci-lower-threshold.md`](ADR/2026-06-01-adr-016-g2-ci-lower-threshold.md) — ADR-016 G2 CI 阈值下调 [SSOT]
- [`ADR/2026-06-01-adr-017-spsc-ring-framework.md`](ADR/2026-06-01-adr-017-spsc-ring-framework.md) — ADR-017 SPSC ring framework [SSOT]

### 2026-06-15 (Sprint-2 W6)

- [`ADR/2026-06-15-arch-rm-key-v0.4-v0.3-v5-review.md`](ADR/2026-06-15-arch-rm-key-v0.4-v0.3-v5-review.md) — 架构/RM/Key v0.4/v0.3/v5 联合评审记录 [Archive]

### 2026-06-W3 (Sprint-2 W7)

- [`ADR/2026-06-W3-adr-018-lib-selection.md`](ADR/2026-06-W3-adr-018-lib-selection.md) — ADR-018 C++ 库选型 [SSOT]
- [`ADR/2026-06-W3-adr-020-ic-tester-separation.md`](ADR/2026-06-W3-adr-020-ic-tester-separation.md) — ADR-020 IC tester 分离 [Retracted — 被 ADR-022 撤回; 保留 audit log]
- [`ADR/2026-06-W3-adr-021-worktree-isolation.md`](ADR/2026-06-W3-adr-021-worktree-isolation.md) — ADR-021 worktree 隔离 (被 ADR-024 扩展, 021 仍有效) [SSOT]
- [`ADR/2026-06-W3-adr-022-ic-self-test-tester-review.md`](ADR/2026-06-W3-adr-022-ic-self-test-tester-review.md) — ADR-022 IC 自测 + tester review [Retracted — 被 ADR-023 撤回; 保留 audit log]
- [`ADR/2026-06-W3-adr-023-ic-self-test-only.md`](ADR/2026-06-W3-adr-023-ic-self-test-only.md) — ADR-023 IC 只自测 [SSOT]
- [`ADR/2026-06-W3-adr-024-worktree-standard-workflow.md`](ADR/2026-06-W3-adr-024-worktree-standard-workflow.md) — ADR-024 worktree 标准工作流 [SSOT]

### 2026-06-W4 (Sprint-2 W8)

- [`ADR/2026-06-W4-adr-013-v2-deployment-location.md`](ADR/2026-06-W4-adr-013-v2-deployment-location.md) — ADR-013 v2 选址决策 [Draft — 待 W9 W2 GM ack]
- [`ADR/2026-06-W4-adr-027-core-data-structure-ssot-enforce.md`](ADR/2026-06-W4-adr-027-core-data-structure-ssot-enforce.md) — ADR-027 核心数据结构 SSOT 强 enforce [SSOT]
- [`ADR/2026-06-W4-adr-028-docs-governance-standard.md`](ADR/2026-06-W4-adr-028-docs-governance-standard.md) — ADR-028 docs 治理 standard [SSOT]

---

## GOALSERVER

> 供应商文档镜像 (小段维护). 工程 SSOT 见 RESEARCH/xiaoduan-w8-goalserve-data-structure-ssot-v1.md.

- [`GOALSERVER/baseball-data-feed.md`](GOALSERVER/baseball-data-feed.md) — 棒球 feed 文档 [SSOT]
- [`GOALSERVER/basketball-data-feed.md`](GOALSERVER/basketball-data-feed.md) — 篮球 feed 文档 [SSOT]
- [`GOALSERVER/esports-data-feed.md`](GOALSERVER/esports-data-feed.md) — 电竞 feed 文档 [SSOT]
- [`GOALSERVER/full_package_feed_cn.md`](GOALSERVER/full_package_feed_cn.md) — 完整包 feed 中文文档 [SSOT]
- [`GOALSERVER/hockey-data-feed.md`](GOALSERVER/hockey-data-feed.md) — 冰球 feed 文档 [SSOT]
- [`GOALSERVER/soccer-data-feed.md`](GOALSERVER/soccer-data-feed.md) — 足球 feed 文档 [SSOT]
- [`GOALSERVER/tennis-data-feed.md`](GOALSERVER/tennis-data-feed.md) — 网球 feed 文档 [SSOT]
- [`GOALSERVER/ufc-data-feed.md`](GOALSERVER/ufc-data-feed.md) — UFC feed 文档 [SSOT]

---

## HIRING

- [`HIRING/employee-registry.md`](HIRING/employee-registry.md) — HR 注册表 SSOT (小林) [SSOT]
- [`HIRING/backlog.md`](HIRING/backlog.md) — 扩招 backlog (小林) [SSOT]
- [`HIRING/sop-v1.md`](HIRING/sop-v1.md) — 招聘 SOP v1 (小林) [SSOT]
- [`HIRING/jd-onchain-ops-laoji-v1.md`](HIRING/jd-onchain-ops-laoji-v1.md) — JD 链上 ops 老季 v1 (小林) [SSOT]
- [`HIRING/jd-strategy-execution-xiaoqin-v1.md`](HIRING/jd-strategy-execution-xiaoqin-v1.md) — JD 策略执行小琴 v1 (小林) [SSOT]
- [`HIRING/xiaolin-w9-data-structure-ic-jd-v1.md`](HIRING/xiaolin-w9-data-structure-ic-jd-v1.md) — JD 数据结构 IC W9 (小林) [SSOT]
- [`HIRING/xiaolin-w9-president-onboard-and-vp-jd-v1.md`](HIRING/xiaolin-w9-president-onboard-and-vp-jd-v1.md) — 总裁 onboard + VP JD W9 (小林) [SSOT]
- [`HIRING/hr-pulse-check-2026-05-28.md`](HIRING/hr-pulse-check-2026-05-28.md) — HR 脉冲检查快照 2026-05-28 (小林) [Archive]
- [`HIRING/sprint2-w2-progress.md`](HIRING/sprint2-w2-progress.md) — Sprint-2 W2 招聘进展 (小林) [Archive]
- [`HIRING/sprint2-w3-progress.md`](HIRING/sprint2-w3-progress.md) — Sprint-2 W3 招聘进展 (小林) [Archive]

---

## INCIDENTS

- [`INCIDENTS/gm-self-mistakes-log.md`](INCIDENTS/gm-self-mistakes-log.md) — GM 自承认错累计 log (老雷 + 老胡, 持续追加) [SSOT]

---

## KPI

- [`KPI/individual-kpi-matrix.md`](KPI/individual-kpi-matrix.md) — 个人 KPI 矩阵 (老雷 + 小林) [SSOT]

---

## MEETINGS

### 2026-05-28 (Sprint-1 收口 / 创始日)

- [`MEETINGS/2026-05-28-kickoff.md`](MEETINGS/2026-05-28-kickoff.md) — 创始全体会 [Archive]
- [`MEETINGS/2026-05-28-hr-launch.md`](MEETINGS/2026-05-28-hr-launch.md) — 人事扩招联席会 [Archive]
- [`MEETINGS/2026-05-28-sprint1-retro-all-hands.md`](MEETINGS/2026-05-28-sprint1-retro-all-hands.md) — Sprint-1 Retro 全员对齐 (STCPP-MTG-003) [Archive]
- [`MEETINGS/2026-05-28-gm-business-needs-must-haves.md`](MEETINGS/2026-05-28-gm-business-needs-must-haves.md) — GM 业务需求必要清单 [Archive]
- `MEETINGS/sprint1-retro/` — Sprint-1 Retro 15 份个人发言原件 (老周/老韩/老胡/老黄/老李/老叶/老郭/老钱/小程/小董/小蒋/小袁/小梁/小宋/小肖) [Archive]

### 2026-05-29

- [`MEETINGS/2026-05-29-data-structure-gap-postmortem.md`](MEETINGS/2026-05-29-data-structure-gap-postmortem.md) — 数据结构 gap post-mortem [SSOT]
- [`MEETINGS/2026-05-29-laoguo-advisor-pool-activation.md`](MEETINGS/2026-05-29-laoguo-advisor-pool-activation.md) — 老郭顾问团激活会议 [SSOT]
- [`MEETINGS/2026-05-29-laohu-stage-gate-framework-and-gm-escalation.md`](MEETINGS/2026-05-29-laohu-stage-gate-framework-and-gm-escalation.md) — 老胡 stage-gate + GM 升级 [SSOT]
- [`MEETINGS/2026-05-29-wangjingli-onboard-and-execution-plan.md`](MEETINGS/2026-05-29-wangjingli-onboard-and-execution-plan.md) — 王经理 onboard + 执行计划 [Archive]

### 2026-06-01 (Sprint-2 W5)

- [`MEETINGS/2026-06-01-architecture-challenge-vote-v1.md`](MEETINGS/2026-06-01-architecture-challenge-vote-v1.md) — 架构挑战投票 v1 [SSOT]
- [`MEETINGS/2026-06-01-code-review-summit-v1.md`](MEETINGS/2026-06-01-code-review-summit-v1.md) — 代码评审峰会 v1 [SSOT]
- [`MEETINGS/2026-06-01-gm-arch-vote-rulings.md`](MEETINGS/2026-06-01-gm-arch-vote-rulings.md) — GM 架构投票裁决 [SSOT]
- [`MEETINGS/2026-06-01-manager-sync-w5-v1.md`](MEETINGS/2026-06-01-manager-sync-w5-v1.md) — 主管周同步 W5 v1 [SSOT]
- [`MEETINGS/2026-06-01-laozhou-actual-architecture-w5.md`](MEETINGS/2026-06-01-laozhou-actual-architecture-w5.md) — 老周实际架构 W5 [SSOT]
- 投票原件 (5 份) [Archive]: [`vote-laohan`](MEETINGS/2026-06-01-vote-laohan-arch-challenge.md) | [`vote-laohu`](MEETINGS/2026-06-01-vote-laohu-arch-challenge.md) | [`vote-laozhou`](MEETINGS/2026-06-01-vote-laozhou-arch-challenge.md) | [`vote-xiaoliang`](MEETINGS/2026-06-01-vote-xiaoliang-arch-challenge.md) | [`vote-xiaoyu`](MEETINGS/2026-06-01-vote-xiaoyu-arch-challenge.md)
- 单元状态 input (5 份) [Archive]: [`laozhou-A`](MEETINGS/2026-06-01-input-laozhou-A-status.md) | [`laohan-B`](MEETINGS/2026-06-01-input-laohan-B-status.md) | [`xiaoliang-C`](MEETINGS/2026-06-01-input-xiaoliang-C-status.md) | [`xiaoyu-D`](MEETINGS/2026-06-01-input-xiaoyu-D-status.md) | [`laoguo-F`](MEETINGS/2026-06-01-input-laoguo-F-status.md)
- 顾问 input [Archive]: [`laogao-code`](MEETINGS/2026-06-01-input-laogao-code-quality.md) | [`laohe-cpp`](MEETINGS/2026-06-01-input-laohe-modern-cpp.md) | [`laozhou-arch-dev`](MEETINGS/2026-06-01-input-laozhou-arch-deviation.md) | [`laohan-redline`](MEETINGS/2026-06-01-input-laohan-redline-deviation.md) | [`xiaodeng-ml`](MEETINGS/2026-06-01-input-xiaodeng-ml-review.md)

### 2026-06-W3 (Sprint-2 W7)

- [`MEETINGS/2026-06-W3-laogao-wave30-quality-review.md`](MEETINGS/2026-06-W3-laogao-wave30-quality-review.md) — 老高 Wave-30 质量评审 [SSOT]
- [`MEETINGS/2026-06-W3-laoguo-wave32-arbitration.md`](MEETINGS/2026-06-W3-laoguo-wave32-arbitration.md) — 老郭 Wave-32 仲裁 [SSOT]
- [`MEETINGS/2026-06-W3-laohu-wave32-resolution.md`](MEETINGS/2026-06-W3-laohu-wave32-resolution.md) — 老胡 Wave-32 决议 [SSOT]
- [`MEETINGS/2026-06-W3-laozhou-wave30-arch-review.md`](MEETINGS/2026-06-W3-laozhou-wave30-arch-review.md) — 老周 Wave-30 架构评审 [SSOT]

---

## META

- [`META/escalate-decision-log.md`](META/escalate-decision-log.md) — 升级决策日志 (老胡 + 老雷) [SSOT]
- [`META/weekly-report-template-v3.md`](META/weekly-report-template-v3.md) — 周报模板 v3 (老胡) [SSOT]
- [`META/weekly-report-template-v2.md`](META/weekly-report-template-v2.md) — 周报模板 v2 (老胡) [Outdated — 被 v3 替代; 老胡 W9 W3 删]
- [`META/xiaomi-w9-docs-governance-audit-v1.md`](META/xiaomi-w9-docs-governance-audit-v1.md) — docs 全治理审计 W9 (小米) [SSOT]

---

## OKR

- [`OKR/2026-Q2-Q3-startup-season.md`](OKR/2026-Q2-Q3-startup-season.md) — 起步季 OKR (M1-M5 + M4.5 gate) (老雷) [SSOT]
- [`OKR/laoqian-w8-w5-profitability-kr-v1.md`](OKR/laoqian-w8-w5-profitability-kr-v1.md) — 盈利 KR v1 (老钱, W8 W5) [SSOT]

---

## RESEARCH

### A. 系统工程部 (老周单元)

**架构 (老周)**

- [`RESEARCH/laozhou-architecture-v0.6-e2e.md`](RESEARCH/laozhou-architecture-v0.6-e2e.md) — 架构 v0.6 E2E [SSOT] **← 最新**
  - 旧版 [Archive]: [`v0.5`](RESEARCH/laozhou-architecture-v0.5.md) | [`v0.4`](RESEARCH/laozhou-architecture-v0.4.md) | [`v0.3`](RESEARCH/laozhou-architecture-v0.3.md) | [`v0.2`](RESEARCH/laozhou-architecture-v0.2.md) | [`v0.1`](RESEARCH/laozhou-architecture-v0.1.md)
- [`RESEARCH/laozhou-single-instance-spec-v1.md`](RESEARCH/laozhou-single-instance-spec-v1.md) — 单实例 spec v1 (老周) [SSOT]
- [`RESEARCH/laozhou-lifecycle-management-v1.md`](RESEARCH/laozhou-lifecycle-management-v1.md) — 模块生命周期 v1 (老周) [SSOT]
- [`RESEARCH/laozhou-manager-mandate-v1.md`](RESEARCH/laozhou-manager-mandate-v1.md) — 老周主管 mandate v1 [SSOT]
- [`RESEARCH/laozhou-vcpu-v0.7-update.md`](RESEARCH/laozhou-vcpu-v0.7-update.md) — vCPU v0.7 update (老周) [SSOT]
- [`RESEARCH/laozhou-w8-debug-rest-api-spec-v1.md`](RESEARCH/laozhou-w8-debug-rest-api-spec-v1.md) — Debug REST API spec v1 (老周, W8) [SSOT]
- [`RESEARCH/laozhou-w8-engineering-abi-gap-audit-v1.md`](RESEARCH/laozhou-w8-engineering-abi-gap-audit-v1.md) — 工程 ABI gap 审计 v1 (老周, W8) [SSOT]
- [`RESEARCH/laozhou-firstreview-laoli-abi-handshake.md`](RESEARCH/laozhou-firstreview-laoli-abi-handshake.md) — 老周首次评审老李 ABI handshake [Archive]

**Polymarket API (老李)**

- [`RESEARCH/laoli-polymarket-endpoint-matrix-v3.md`](RESEARCH/laoli-polymarket-endpoint-matrix-v3.md) — Polymarket endpoint matrix v3 [SSOT] **← 最新**
  - 旧版: [`v2`](RESEARCH/laoli-polymarket-endpoint-matrix-v2.md) [Outdated — 老李 W9 W3 删] | [`api-spec-v1`](RESEARCH/laoli-polymarket-api-spec-v1.md) [Archive]
- [`RESEARCH/laoli-polymarket-backend-requirements-v1.md`](RESEARCH/laoli-polymarket-backend-requirements-v1.md) — Polymarket 后端需求 v1 (老李) [Archive]
- [`RESEARCH/laoli-polymarket-reverse-sports-live-v1.md`](RESEARCH/laoli-polymarket-reverse-sports-live-v1.md) — Polymarket reverse 工程 sports live (老李) [SSOT]
- [`RESEARCH/laoli-w8-polymarket-data-structure-ssot-v1.md`](RESEARCH/laoli-w8-polymarket-data-structure-ssot-v1.md) — Polymarket 数据结构 SSOT v1 (老李, W8) [SSOT] **ADR-027 强 enforce**
- [`RESEARCH/laoli-w9-wss-subscriber-impl-spec-v1.md`](RESEARCH/laoli-w9-wss-subscriber-impl-spec-v1.md) — WSS subscriber 实施 spec (老李, W9) [SSOT]
- [`RESEARCH/laoli-xiaoduan-api-call-optimization-v1.md`](RESEARCH/laoli-xiaoduan-api-call-optimization-v1.md) — API call 优化 v1 (老李 + 小段) [SSOT]
- [`RESEARCH/laoli-xiaoduan-cross-source-mapping-v1.md`](RESEARCH/laoli-xiaoduan-cross-source-mapping-v1.md) — 跨数据源字段 mapping v1 (老李 + 小段) [SSOT]
- [`RESEARCH/laoli-laoSun-handshake-v1.md`](RESEARCH/laoli-laoSun-handshake-v1.md) — 老李 + 老孙 handshake v1 [Archive]

**网络基准 (老陈 / 老吴)**

- [`RESEARCH/laochen-api-rate-latency-ssot-v1.md`](RESEARCH/laochen-api-rate-latency-ssot-v1.md) — API 速率 + 延迟 SSOT v1 (老陈) [SSOT]
- [`RESEARCH/laochen-network-bench-v1.md`](RESEARCH/laochen-network-bench-v1.md) — 跨洋网络 bench v1 (老陈) [Archive]
- [`RESEARCH/laowu-cross-region-deployment-v0.1.md`](RESEARCH/laowu-cross-region-deployment-v0.1.md) — 跨洋部署 v0.1 (老吴 + 老叶) [SSOT]
- [`RESEARCH/laowu-proxy-goalserve-bandwidth-v1.md`](RESEARCH/laowu-proxy-goalserve-bandwidth-v1.md) — Goalserve 带宽专项 v1 (老吴) [SSOT]
- [`RESEARCH/laowu-toolstack-install-v1.md`](RESEARCH/laowu-toolstack-install-v1.md) — 工具栈装机报告 v1 (老吴) [SSOT]
- [`RESEARCH/laowu-w8-polymarket-origin-verification-v1.md`](RESEARCH/laowu-w8-polymarket-origin-verification-v1.md) — Polymarket origin 验证 v1 (老吴, W8) [SSOT]
- [`RESEARCH/laowu-w9-w1-aws-region-rtt-test-plan-v1.md`](RESEARCH/laowu-w9-w1-aws-region-rtt-test-plan-v1.md) — AWS region RTT 测试计划 (老吴, W9 W1) [SSOT]

**延迟 / vCPU (老姜)**

- [`RESEARCH/laojiang-latency-budget-w4-wave21-v1.md`](RESEARCH/laojiang-latency-budget-w4-wave21-v1.md) — 延迟预算 W4 wave21 v1 (老姜) [SSOT] **← 最新**
  - 旧版: [`latency-budget-v1`](RESEARCH/laojiang-latency-budget-v1.md) [Archive — 老姜 W9 W3 删]
- [`RESEARCH/laojiang-vcpu-pin-baseline-v1.md`](RESEARCH/laojiang-vcpu-pin-baseline-v1.md) — vCPU pin baseline v1 (老姜) [SSOT]

**WAL 框架 (老王)**

- [`RESEARCH/laowang-wal-framework-cpp-interface-v1.md`](RESEARCH/laowang-wal-framework-cpp-interface-v1.md) — WAL C++ interface v1 (老王) [SSOT] **← 最新**
  - 旧版 [Archive — 老王 W9 W3 删]: [`v0.2`](RESEARCH/laowang-wal-framework-v0.2.md) | [`v0.1`](RESEARCH/laowang-wal-framework-v0.1.md)
- [`RESEARCH/laowang-tomarketidarray-todo-w7-cleanup.md`](RESEARCH/laowang-tomarketidarray-todo-w7-cleanup.md) — toMarketIdArray TODO W7 cleanup (老王) [Archive]

**数据结构 (小石)**

- [`RESEARCH/xiaoshi-data-structures-selection-v1.md`](RESEARCH/xiaoshi-data-structures-selection-v1.md) — 数据结构选型 v1 (小石) [SSOT]

**观测栈 (小郑)**

- [`RESEARCH/xiaozheng-observability-v0.1.md`](RESEARCH/xiaozheng-observability-v0.1.md) — 观测栈 v0.1 (小郑) [SSOT]

**Operator UI (小苏)**

- [`RESEARCH/xiaosu-ui-wireframe-v0.1.md`](RESEARCH/xiaosu-ui-wireframe-v0.1.md) — Operator UI wireframe v0.1 (小苏) [SSOT]
- [`RESEARCH/xiaosu-backend-api-requirements-v1.md`](RESEARCH/xiaosu-backend-api-requirements-v1.md) — Backend API requirements v1 (小苏) [SSOT]

**小卢 IC (REST API skeleton)**

- [`RESEARCH/xiaolu-w9-rest-api-skeleton-implementation-spec-v1.md`](RESEARCH/xiaolu-w9-rest-api-skeleton-implementation-spec-v1.md) — REST API skeleton 实施 spec (小卢, W9) [SSOT]

### B. 风控合规部 (老韩单元)

**RiskManager (老韩)**

- [`RESEARCH/laohan-riskmanager-design-v0.3.1.md`](RESEARCH/laohan-riskmanager-design-v0.3.1.md) — RiskManager v0.3.1 (老韩) [SSOT] **← 最新**
  - 旧版: [`v0.3`](RESEARCH/laohan-riskmanager-design-v0.3.md) [Archive] | [`v0.2`](RESEARCH/laohan-riskmanager-design-v0.2.md) [Archive — 老韩 W9 W3 删] | [`v0.1`](RESEARCH/laohan-riskmanager-design-v0.1.md) [Archive — 老韩 W9 W3 删]
- [`RESEARCH/laohan-manager-mandate-v1.md`](RESEARCH/laohan-manager-mandate-v1.md) — 老韩主管 mandate v1 [SSOT]
- [`RESEARCH/laohan-w8-rm-or-assertion-audit.md`](RESEARCH/laohan-w8-rm-or-assertion-audit.md) — RM OR assertion 审计 (老韩, W8) [SSOT]
- [`RESEARCH/laohan-w8-rm-stale-data-ack.md`](RESEARCH/laohan-w8-rm-stale-data-ack.md) — RM stale data ack (老韩, W8) [SSOT]
- [`RESEARCH/laohan-w9-orderintent-v05-spec-v1.md`](RESEARCH/laohan-w9-orderintent-v05-spec-v1.md) — OrderIntent v0.5 spec (老韩, W9) [SSOT]

**Key Management (老沈)**

- [`RESEARCH/laoshen-key-vendor-selection-v2.md`](RESEARCH/laoshen-key-vendor-selection-v2.md) — Key vendor 选型 v2 (老沈) [SSOT]
- [`RESEARCH/laoshen-threat-model-v2.md`](RESEARCH/laoshen-threat-model-v2.md) — 威胁模型 v2 (老沈) [SSOT]
  - 旧版: [`v1`](RESEARCH/laoshen-threat-model-v1.md) [Archive — 老沈 W9 W3 删]
- [`RESEARCH/laoshen-key-management-coreview-v1.md`](RESEARCH/laoshen-key-management-coreview-v1.md) — Key management co-review v1 (老沈) [SSOT]
- [`RESEARCH/laoshen-multi-vendor-kms-v1.md`](RESEARCH/laoshen-multi-vendor-kms-v1.md) — 多 vendor KMS v1 (老沈) [SSOT]

**合规 (老黄)**

- [`RESEARCH/laohuang-compliance-redline-v2.md`](RESEARCH/laohuang-compliance-redline-v2.md) — 合规红线 v2 (老黄) [SSOT]
  - 旧版: [`v1`](RESEARCH/laohuang-compliance-redline-v1.md) [Archive]
- [`RESEARCH/laohuang-compliance-signoff.md`](RESEARCH/laohuang-compliance-signoff.md) — 合规红线签收表 (老黄) [Archive]
- [`RESEARCH/laohuang-shamir-jurisdiction-signoff-v1.md`](RESEARCH/laohuang-shamir-jurisdiction-signoff-v1.md) — Shamir + 司法管辖签收 v1 (老黄) [SSOT]
- [`RESEARCH/laohuang-us-entity-feasibility-v1.md`](RESEARCH/laohuang-us-entity-feasibility-v1.md) — US entity 可行性 v1 (老黄, D-15 Escalated GM) [SSOT]

**Audit Schema (老唐)**

- [`RESEARCH/laotang-audit-schema-v1.1.md`](RESEARCH/laotang-audit-schema-v1.1.md) — Audit schema v1.1 (老唐) [SSOT] **← 最新**
  - 旧版: [`v1`](RESEARCH/laotang-audit-schema-v1.md) [Archive — 老唐 W9 W3 删]
- [`RESEARCH/laotang-friend-class-firstmismatch-w7-cleanup.md`](RESEARCH/laotang-friend-class-firstmismatch-w7-cleanup.md) — friend-class firstmismatch W7 cleanup (老唐) [Archive]

**Signer / Key Impl (老孙)**

- [`RESEARCH/laosun-key-management-v5.1.md`](RESEARCH/laosun-key-management-v5.1.md) — Key management v5.1 C++ (老孙) [SSOT] **← 最新**
  - 旧版 [Archive]: [`v5-simplified`](RESEARCH/laosun-key-management-v5-simplified.md) | [`v4-cpp`](RESEARCH/laosun-key-management-v4-cpp.md) | v3/v2/v1 (Rust 设计 trail, 已删)
- [`RESEARCH/laosun-libsodium-fetchcontent-w7-plan.md`](RESEARCH/laosun-libsodium-fetchcontent-w7-plan.md) — libsodium FetchContent W7 plan (老孙) [SSOT]
- [`RESEARCH/laosun-w9-signer-v53-abi-align-spec-v1.md`](RESEARCH/laosun-w9-signer-v53-abi-align-spec-v1.md) — Signer v5.3 ABI align spec (老孙, W9) [SSOT]
- [`RESEARCH/laoshan-monocypher-vs-libsodium-w7-ack.md`](RESEARCH/laoshan-monocypher-vs-libsodium-w7-ack.md) — Monocypher vs libsodium W7 ack (老山) [SSOT]

### C. 量化研究部 (小梁单元)

- [`RESEARCH/xiaoliang-market-structure-v1.md`](RESEARCH/xiaoliang-market-structure-v1.md) — 体育市场结构 v1 (小梁) [SSOT]
- [`RESEARCH/xiaoliang-manager-mandate-v1.md`](RESEARCH/xiaoliang-manager-mandate-v1.md) — 小梁主管 mandate v1 [SSOT]
- [`RESEARCH/xiaocheng-signal-catalog-v1.md`](RESEARCH/xiaocheng-signal-catalog-v1.md) — 信号 catalog v1 (小程) [SSOT]
- [`RESEARCH/xiaocheng-p0_02-signal-spec-v0.1.md`](RESEARCH/xiaocheng-p0_02-signal-spec-v0.1.md) — P0-02 信号 spec v0.1 (小程) [SSOT]
- [`RESEARCH/xiaoyuan-microstructure-v1.md`](RESEARCH/xiaoyuan-microstructure-v1.md) — 微观结构 v1 (小袁) [SSOT]
- [`RESEARCH/xiaoyuan-fill-rate-model-v0.1.md`](RESEARCH/xiaoyuan-fill-rate-model-v0.1.md) — Fill rate 模型 v0.1 (小袁) [SSOT]
- [`RESEARCH/xiaoxiao-kelly-slippage-model-v1.md`](RESEARCH/xiaoxiao-kelly-slippage-model-v1.md) — Kelly + slippage 模型 v1 (小肖) [SSOT]
- [`RESEARCH/xiaoxiao-slippage-model-lib-v1.md`](RESEARCH/xiaoxiao-slippage-model-lib-v1.md) — SlippageModel C++ lib v1 (小肖) [SSOT]
- [`RESEARCH/laopeng-betting-industry-analysis-v1.md`](RESEARCH/laopeng-betting-industry-analysis-v1.md) — 体育博彩行业分析 v1 (老彭) [SSOT]
- [`RESEARCH/laopeng-bookmaker-history-backfill-v1.md`](RESEARCH/laopeng-bookmaker-history-backfill-v1.md) — Bookmaker 历史回填 v1 (老彭) [SSOT]
- [`RESEARCH/laopeng-multiplicative-devig-calibration-v1.md`](RESEARCH/laopeng-multiplicative-devig-calibration-v1.md) — 乘法去 vig 校准 v1 (老彭) [SSOT]
- [`RESEARCH/laopeng-w8-oq-p02-3-inplay-single-source-ack.md`](RESEARCH/laopeng-w8-oq-p02-3-inplay-single-source-ack.md) — OQ P02-3 inplay 单源 ack (老彭, W8) [SSOT]
- [`RESEARCH/laopeng-w9-inplay-edge-gross-net-confirm.md`](RESEARCH/laopeng-w9-inplay-edge-gross-net-confirm.md) — Inplay edge gross/net confirm (老彭, W9) [SSOT]

**回测 / Paper (小蒋)**

- [`RESEARCH/xiaojiang-backtest-framework-v0.2-cpp.md`](RESEARCH/xiaojiang-backtest-framework-v0.2-cpp.md) — Backtest framework v0.2 C++ (小蒋) [SSOT] **← 最新**
  - 旧版: [`v0.1`](RESEARCH/xiaojiang-backtest-framework-v0.1.md) [Archive — 小蒋 W9 W3 删]
- [`RESEARCH/xiaojiang-paper-trading-engine-v0.2-cpp.md`](RESEARCH/xiaojiang-paper-trading-engine-v0.2-cpp.md) — Paper trading engine v0.2 C++ (小蒋) [SSOT] **← 最新**
  - 旧版: [`v0.1`](RESEARCH/xiaojiang-paper-trading-engine-v0.1.md) [Archive — 小蒋 W9 W3 删]
- [`RESEARCH/xiaojiang-paper-engine-skeleton-v1.md`](RESEARCH/xiaojiang-paper-engine-skeleton-v1.md) — Paper engine skeleton v1 (小蒋) [SSOT]

**统计验证 (小董)**

- [`RESEARCH/xiaodong-stats-validation-framework-v1.md`](RESEARCH/xiaodong-stats-validation-framework-v1.md) — 统计验证 framework v1 (小董) [SSOT]
- [`RESEARCH/xiaodong-m45-gate-framework-v1.md`](RESEARCH/xiaodong-m45-gate-framework-v1.md) — M45 gate framework v1 (小董) [SSOT]

### D. 数据基础设施部 (小余单元)

- [`RESEARCH/data-contract-v1.md`](RESEARCH/data-contract-v1.md) — 数据契约 v1 (小余 + 小邓, 4 时间戳契约, R-20) [SSOT]
- [`RESEARCH/xiaoduan-goalserve-official-doc-v3.md`](RESEARCH/xiaoduan-goalserve-official-doc-v3.md) — Goalserve official doc v3 (小段) [SSOT] **← 最新**
  - 旧版: [`endpoint-matrix-v2`](RESEARCH/xiaoduan-goalserve-endpoint-matrix-v2.md) [Outdated] | [`api-spec-v1`](RESEARCH/xiaoduan-goalserve-api-spec-v1.md) [Archive]
- [`RESEARCH/xiaoduan-goalserve-odds-by-sport-v2.1.md`](RESEARCH/xiaoduan-goalserve-odds-by-sport-v2.1.md) — Goalserve 各 sport odds v2.1 (小段) [SSOT]
- [`RESEARCH/xiaoduan-w8-goalserve-data-structure-ssot-v1.md`](RESEARCH/xiaoduan-w8-goalserve-data-structure-ssot-v1.md) — Goalserve 数据结构 SSOT v1 (小段, W8) [SSOT] **ADR-027 强 enforce**
- [`RESEARCH/xiaoduan-w8-goalserve-inplay-node-verification-v1.md`](RESEARCH/xiaoduan-w8-goalserve-inplay-node-verification-v1.md) — Goalserve inplay node 验证 (小段, W8) [SSOT]
- [`RESEARCH/xiaodeng-ml-roadmap-v2.md`](RESEARCH/xiaodeng-ml-roadmap-v2.md) — ML roadmap v2 (小邓) [SSOT] **← 最新**
  - 旧版: [`data-needs-v1`](RESEARCH/xiaodeng-ml-roadmap-data-needs-v1.md) [Archive — 小邓 W9 W3 删]
- [`RESEARCH/xiaodeng-ml-data-infra-v1.md`](RESEARCH/xiaodeng-ml-data-infra-v1.md) — ML data infra v1 (小邓) [SSOT]
  - 吸收: [`ml-data-pipeline-v0.1`](RESEARCH/xiaodeng-ml-data-pipeline-v0.1.md) [Archive — 小邓 W9 W3 删]
- [`RESEARCH/xiaotian-parquet-partition-v1.md`](RESEARCH/xiaotian-parquet-partition-v1.md) — Parquet partition v1 (小田 #24) [SSOT]

### E. 产品业务保障部 (老胡单元)

- [`RESEARCH/laohu-master-gantt-v1.md`](RESEARCH/laohu-master-gantt-v1.md) — 全局甘特图 v1 (老胡) [SSOT]
- [`RESEARCH/laohu-manager-mandate-v1.md`](RESEARCH/laohu-manager-mandate-v1.md) — 老胡主管 mandate v1 [SSOT]
- [`RESEARCH/laohu-risk-registry-v2.4.md`](RESEARCH/laohu-risk-registry-v2.4.md) — 风险登记 v2.4 (老胡) [SSOT] **← 最新**
  - 旧版 [Archive — 全部老胡 W9 W3 删]: [`v2.3`](RESEARCH/laohu-risk-registry-v2.3.md) | [`v2.2`](RESEARCH/laohu-risk-registry-v2.2.md) | [`v2.1`](RESEARCH/laohu-risk-registry-v2.1.md) | [`v2`](RESEARCH/laohu-risk-registry-v2.md) | [`v1`](RESEARCH/laohu-risk-registry-v1.md)
- [`RESEARCH/laoqian-business-capabilities-v1.md`](RESEARCH/laoqian-business-capabilities-v1.md) — 业务能力 v1 (老钱) [SSOT]
- [`RESEARCH/laoqian-mvp-scope-rejection-v1.md`](RESEARCH/laoqian-mvp-scope-rejection-v1.md) — MVP scope 拒绝清单 v1 (老钱) [SSOT]
- [`RESEARCH/xiaodu-prd-v2-user-journey.md`](RESEARCH/xiaodu-prd-v2-user-journey.md) — MVP PRD v2 user journey (小杜) [SSOT]
  - 旧版: [`mvp-prd-v1`](RESEARCH/xiaodu-mvp-prd-v1.md) [Archive — 小杜 W9 W3 删]
- [`RESEARCH/xiaoying-acceptance-spec-v1.md`](RESEARCH/xiaoying-acceptance-spec-v1.md) — 验收 spec v1 (小颖) [SSOT]
  - 旧版: [`acceptance-criteria-v1`](RESEARCH/xiaoying-acceptance-criteria-v1.md) [Archive — 小颖 W9 W3 删]
- [`RESEARCH/xiaoyou-ux-interface-requirements-v1.md`](RESEARCH/xiaoyou-ux-interface-requirements-v1.md) — UX interface requirements v1 (小尤) [SSOT]
  - 旧版: [`ux-framework-v1`](RESEARCH/xiaoyou-ux-framework-v1.md) [Archive — 小尤 W9 W3 删]
- [`RESEARCH/xiaogong-dogfood-playbook-v1.md`](RESEARCH/xiaogong-dogfood-playbook-v1.md) — Dogfood 剧本 v1 (小宫) [SSOT]
- [`RESEARCH/xiaosong-test-framework-v0.2.md`](RESEARCH/xiaosong-test-framework-v0.2.md) — Test framework v0.2 (小宋) [SSOT]
- [`RESEARCH/xiaosong-test-framework-cpp-skeleton-v1.md`](RESEARCH/xiaosong-test-framework-cpp-skeleton-v1.md) — Test framework C++ skeleton v1 (小宋) [SSOT]
- [`RESEARCH/xiaosong-test-replay-framework-v0.1.md`](RESEARCH/xiaosong-test-replay-framework-v0.1.md) — Test + replay + chaos framework v0.1 (小宋) [SSOT]
- [`RESEARCH/xiaosong-adr010-wno-check-grep-spec.md`](RESEARCH/xiaosong-adr010-wno-check-grep-spec.md) — ADR-010 wno-check grep spec (小宋) [SSOT]
- [`RESEARCH/xiaosong-wno-cleanup-extension-w7-plan.md`](RESEARCH/xiaosong-wno-cleanup-extension-w7-plan.md) — wno cleanup extension W7 plan (小宋) [SSOT]
- [`RESEARCH/xiaosong-grandfather-cleanup-v1.md`](RESEARCH/xiaosong-grandfather-cleanup-v1.md) — grandfather cleanup v1 (小宋) [Archive]

### F. 顾问团 (老郭协调)

**工程规范 (老高)**

- [`RESEARCH/laogao-pr-review-v1.7.md`](RESEARCH/laogao-pr-review-v1.7.md) — PR review checklist v1.7 (老高) [SSOT] **← 最新**
  - 旧版 [Archive — 老高 W9 W3 删]: [`v1.5`](RESEARCH/laogao-pr-review-v1.5.md) | [`v1.4`](RESEARCH/laogao-pr-review-v1.4.md) | [`v1.3`](RESEARCH/laogao-pr-review-v1.3.md) | [`v1.2`](RESEARCH/laogao-pr-review-v1.2.md) | [`v1.1`](RESEARCH/laogao-pr-review-v1.1.md)
- [`RESEARCH/laogao-code-conventions-v1.md`](RESEARCH/laogao-code-conventions-v1.md) — Code conventions v1 (老高) [SSOT]

**C++ 标准 (老何)**

- [`RESEARCH/laohe-cpp-version-selection-v1.md`](RESEARCH/laohe-cpp-version-selection-v1.md) — C++ 标准选型 v1 (老何) [SSOT]
- [`RESEARCH/laohe-cpp-footgun-checklist-v1.md`](RESEARCH/laohe-cpp-footgun-checklist-v1.md) — C++ footgun checklist v1 (老何) [SSOT]

**顾问团协调 (老郭)**

- [`RESEARCH/laoguo-coordinator-mandate-v1.md`](RESEARCH/laoguo-coordinator-mandate-v1.md) — 老郭协调人 mandate v1 [SSOT]
- [`RESEARCH/laoguo-w6-gm-commit-audit.md`](RESEARCH/laoguo-w6-gm-commit-audit.md) — GM commit 审计 W6 (老郭) [Archive]
- [`RESEARCH/laoguo-w8-w5-adr-027-main-review.md`](RESEARCH/laoguo-w8-w5-adr-027-main-review.md) — ADR-027 main review (老郭, W8 W5) [SSOT]

**外部工具 (老徐)**

- [`RESEARCH/laoxu-external-tools-inventory-v1.md`](RESEARCH/laoxu-external-tools-inventory-v1.md) — 外部工具 + MCP 盘点 v1 (老徐) [SSOT]
- [`RESEARCH/laoxu-r39-escalate-flow-v0.3.md`](RESEARCH/laoxu-r39-escalate-flow-v0.3.md) — 升级流程 v0.3 (老徐) [SSOT] **← 最新**
  - 旧版: [`v0.2`](RESEARCH/laoxu-subagent-escalate-flow-v0.2.md) [Archive — 老徐 W9 W3 删]
- [`RESEARCH/laoxu-w6-persona-boundary-dryrun-v1.md`](RESEARCH/laoxu-w6-persona-boundary-dryrun-v1.md) — Persona boundary dry-run v1 (老徐, W6) [Archive]

**Polygon / Nonce (老叶)**

- [`RESEARCH/laoye-polygon-rpc-endpoint-matrix-v1.md`](RESEARCH/laoye-polygon-rpc-endpoint-matrix-v1.md) — Polygon RPC endpoint matrix v1 (老叶) [SSOT]
- [`RESEARCH/laoye-polygon-rpc-selection-v1.md`](RESEARCH/laoye-polygon-rpc-selection-v1.md) — Polygon RPC 选型 v1 (老叶) [SSOT]
- [`RESEARCH/laoye-nonce-manager-design-v1.md`](RESEARCH/laoye-nonce-manager-design-v1.md) — Nonce manager v1 (老叶) [SSOT]
- [`RESEARCH/laoye-receiver-whitelist-v1.md`](RESEARCH/laoye-receiver-whitelist-v1.md) — Receiver whitelist v1 (老叶) [SSOT]

**LLM 开发规范 (小白)**

- [`RESEARCH/xiaobai-llm-dev-conventions-v1.md`](RESEARCH/xiaobai-llm-dev-conventions-v1.md) — LLM 辅助开发规范 v1 (小白) [SSOT]

### G. 小米历史 (doc-curator) [均待 W9 W3 删]

- `RESEARCH/xiaomi-docs-health-2026-05-28.md` [Archive]
- `RESEARCH/xiaomi-docs-health-2026-05-28-v2.md` [Archive]
- `RESEARCH/xiaomi-r20-backfill-2026-05-28.md` [Archive]

---

## RUNBOOKS

- [`RUNBOOKS/strategy-decayed-unlock-sop-v1.md`](RUNBOOKS/strategy-decayed-unlock-sop-v1.md) — 策略衰减解锁 SOP v1 (老周 + 老胡) [SSOT]
- [`RUNBOOKS/incident-postmortem-template-v1.md`](RUNBOOKS/incident-postmortem-template-v1.md) — Incident post-mortem 模板 v1 (老胡) [SSOT]

---

## SPRINTS

**当前 active:**

- [`SPRINTS/sprint-03-backlog.md`](SPRINTS/sprint-03-backlog.md) — Sprint-03 Backlog (老胡) [SSOT]
- [`SPRINTS/sprint-03-w9-abi-fix-timeline.md`](SPRINTS/sprint-03-w9-abi-fix-timeline.md) — Sprint-03 W9 ABI 修复 timeline (老胡) [SSOT]
- [`SPRINTS/sprint-02-w8-w5-progress.md`](SPRINTS/sprint-02-w8-w5-progress.md) — Sprint-02 W8 W5 最新进展 (老胡) [SSOT]
- [`SPRINTS/milestone-progress-w7.md`](SPRINTS/milestone-progress-w7.md) — Milestone 进展 W7 (老胡) [SSOT]
- [`SPRINTS/sprint-02.md`](SPRINTS/sprint-02.md) — Sprint-02 Backlog (28 ticket) (老胡) [SSOT]

**历史 [Archive — 老胡 W9 W3 归入 archive/ 子目录]:**

- [`sprint-01.md`](SPRINTS/sprint-01.md) | [`sprint-01-final.md`](SPRINTS/sprint-01-final.md)
- [`sprint-02-w1-progress.md`](SPRINTS/sprint-02-w1-progress.md) | [`sprint-02-w3-progress.md`](SPRINTS/sprint-02-w3-progress.md) | [`sprint-02-w4-midweek-progress.md`](SPRINTS/sprint-02-w4-midweek-progress.md) | [`sprint-02-w6-w2-progress.md`](SPRINTS/sprint-02-w6-w2-progress.md) | [`sprint-02-w7-w3-progress.md`](SPRINTS/sprint-02-w7-w3-progress.md) | [`sprint-02-w8-w1-progress.md`](SPRINTS/sprint-02-w8-w1-progress.md)

---

## 健康度 (W9 W2 快照)

- 治理审计 SSOT: [`META/xiaomi-w9-docs-governance-audit-v1.md`](META/xiaomi-w9-docs-governance-audit-v1.md)
- ADR-028 SSOT: [`ADR/2026-06-W4-adr-028-docs-governance-standard.md`](ADR/2026-06-W4-adr-028-docs-governance-standard.md)
- **frontmatter coverage: 23/280 = 8.2%** (W9 W2 基准)
- **W9 W4 目标: > 80%** — 见 frontmatter 派单清单 (§ frontmatter 补齐)
- 评级: **C** (frontmatter 严重缺失; W9 W3 批量补 frontmatter 后升级)

---

## frontmatter 补齐派单 (W9 W4 deadline)

> 规则: 小米不直接改别人 doc. 下列 owner 须在 **W9 W4** 前为自己名下文档补 frontmatter (owner / last_review / status 三字段).
> 补完后通知小米抽检, 小米 W9 W3 末 verify coverage > 80%.

| Owner | 需补文档数 (估算) | 高优先 (SSOT 类) |
|---|---|---|
| 老郭 | ADR 全部 35 篇 `last_review` + 部分 `owner` | ADR-027/028/024/023 |
| 老胡 | SPRINTS 12 篇, MEETINGS 近期系列, RUNBOOKS 2 篇, META 3 篇 | sprint-03, weekly-report-v3 |
| 老周 | laozhou-* RESEARCH 8 篇 | architecture-v0.6, abi-gap-audit |
| 老韩 | laohan-* RESEARCH 5 篇 | riskmanager-v0.3.1, w9-orderintent |
| 老李 | laoli-* RESEARCH 7 篇 + MEETINGS 关联 | polymarket-data-structure-ssot, w9-wss-spec |
| 小段 | xiaoduan-* RESEARCH 5 篇, GOALSERVER 8 篇 | goalserve-data-structure-ssot |
| 小林 | HIRING 10 篇 | employee-registry, sop-v1 |
| 老彭 | laopeng-* RESEARCH 5 篇 | w9-inplay-edge |
| 小梁 | xiaoliang-* 2 篇 | manager-mandate, market-structure |
| 小宋 | xiaosong-* 5 篇 | test-framework 系列 |
| 老孙 | laosun-* 6 篇 | key-management-v5.1, w9-signer |
| 老唐 | laotang-* 2 篇 | audit-schema-v1.1 |
| 其余 owner | 各自名下文档 | 自检 |
| **小米自己** | INDEX.md (本文 v3 已补) + CONVENTIONS-naming.md | 已补 INDEX; CONVENTIONS W9 W3 补 |

---

## 文档统计 (W9 W2 快照)

| 目录 | md 文件数 |
|---|---|
| ADR/ | 35 |
| CONVENTIONS-naming.md + INDEX.md | 2 |
| GOALSERVER/ | 8 |
| HIRING/ | 10 |
| INCIDENTS/ | 1 |
| KPI/ | 1 |
| MEETINGS/ (含 sprint1-retro/ 15 份) | 46 |
| META/ | 4 |
| OKR/ | 2 |
| RESEARCH/ | ~170 |
| RUNBOOKS/ | 2 |
| SPRINTS/ | 12 |
| **总计** | **~280 md** |

v2 (2026-05-28) 记录 128 md. v3 新增录入约 152 条目, 主要来自 W5-W9 新建文档.

---

**Last updated:** 2026-05-29 by 小米 (v3, W9 W2 全量扫描 + W5-W9 新增 + 状态标注 + frontmatter 派单)
