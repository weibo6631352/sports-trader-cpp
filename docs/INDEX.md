# docs/ 索引 (SSOT) v4 — 2026-06-08 治理重生

- **owner:** 小米 #40 (doc-curator, E 单元) · 本次重生执行: GM 老雷
- **last_review:** 2026-06-08
- **status:** SSOT

> **2026-06-08 文档治理清仓:** 删除 174 份历史/superseded 文档 (ARCHIVE/、全部会议纪要、sprint 进展报、过期/retracted ADR、旧 dashboard 截图、过期模板/快照)。
> 本索引按删后**实际存在的文件**重生。**列出的都是 live/current** —— 历史版本已物理移除，故不再标 `[Archive]/[Outdated]` 子状态。
> 细粒度 owner / last_review / 「← 最新」标注留待 doc-curator 后续按需补 (frontmatter 派单见 ADR-028)。
> 命名规范见 [`CONVENTIONS-naming.md`](CONVENTIONS-naming.md)。

---

## 一级分类

| 目录 | 用途 | Owner |
|---|---|---|
| `ADR/` | 架构决策记录 + GM 决议 (现行有效) | 老郭 + 老雷 |
| `CONVENTIONS-naming.md` | 文档命名规范 | 小米 |
| `GOALSERVER/` | Goalserve feed 文档原件 | 小段 |
| `HIRING/` | 扩招 backlog + JD + SOP + 注册 | 小林 |
| `INCIDENTS/` | 事故 post-mortem + GM 自承认错 | 老唐 + 老雷 |
| `INDEX.md` | 本文件 — 文档导航 SSOT | 小米 |
| `KPI/` | 个人 / 团队 KPI 矩阵 | 老雷 + 小林 |
| `META/` | 治理工具 + 模板 | 小米 + 老胡 |
| `OKR/` | 季度 / 半年 OKR | 老雷 + 各单元 owner |
| `RESEARCH/` | 技术预研 + 策略/路线图 | 各 owner |
| `RUNBOOKS/` | 运营 runbook + SOP | 老周 + 老胡 |
| `SPRINTS/` | 当前 Sprint backlog / plan | 老胡 |

---

## 顶级文档

- [`INDEX.md`](INDEX.md)
- [`CONVENTIONS-naming.md`](CONVENTIONS-naming.md)

---

## ADR

- [`2026-05-28-department-manager-mandate.md`](ADR/2026-05-28-department-manager-mandate.md)
- [`2026-05-28-gm-commitment-sygnum-deadline.md`](ADR/2026-05-28-gm-commitment-sygnum-deadline.md)
- [`2026-05-28-gm-decision-defer-aws-until-profitable.md`](ADR/2026-05-28-gm-decision-defer-aws-until-profitable.md)
- [`2026-05-28-gm-decision-goalserve-odds-gap.md`](ADR/2026-05-28-gm-decision-goalserve-odds-gap.md)
- [`2026-05-28-gm-policy-api-monitoring-longterm.md`](ADR/2026-05-28-gm-policy-api-monitoring-longterm.md)
- [`2026-05-28-gm-policy-cross-domain-listening.md`](ADR/2026-05-28-gm-policy-cross-domain-listening.md)
- [`2026-05-28-gm-policy-jurisdictional-deferral.md`](ADR/2026-05-28-gm-policy-jurisdictional-deferral.md)
- [`2026-05-28-gm-redline-data-source-timestamping.md`](ADR/2026-05-28-gm-redline-data-source-timestamping.md)
- [`2026-05-28-gm-redline-websocket-non-blocking.md`](ADR/2026-05-28-gm-redline-websocket-non-blocking.md)
- [`2026-05-28-gm-signoff-paper-trade.md`](ADR/2026-05-28-gm-signoff-paper-trade.md)
- [`2026-05-28-r07-r08-liquidity-vs-position-cap-priority.md`](ADR/2026-05-28-r07-r08-liquidity-vs-position-cap-priority.md)
- [`2026-05-29-adr-040-market-structure-per-token.md`](ADR/2026-05-29-adr-040-market-structure-per-token.md)
- [`2026-05-29-adr-041-frontend-stack-solidjs-vite.md`](ADR/2026-05-29-adr-041-frontend-stack-solidjs-vite.md)
- [`2026-05-29-adr-042-kelly-sizing.md`](ADR/2026-05-29-adr-042-kelly-sizing.md)
- [`2026-05-29-agent-model-tiering-v3.md`](ADR/2026-05-29-agent-model-tiering-v3.md)
- [`2026-05-29-data-model-strategy-vendor-agnostic.md`](ADR/2026-05-29-data-model-strategy-vendor-agnostic.md)
- [`2026-05-29-dev-workflow-sop.md`](ADR/2026-05-29-dev-workflow-sop.md)
- [`2026-05-29-observability-debug-api.md`](ADR/2026-05-29-observability-debug-api.md)
- [`2026-05-31-adr-binary-dual-side-decision.md`](ADR/2026-05-31-adr-binary-dual-side-decision.md)
- [`2026-05-31-redline-application-carveout.md`](ADR/2026-05-31-redline-application-carveout.md)
- [`2026-06-01-adr-010-test-code-grading.md`](ADR/2026-06-01-adr-010-test-code-grading.md)
- [`2026-06-01-adr-012-wss-topology.md`](ADR/2026-06-01-adr-012-wss-topology.md)
- [`2026-06-01-adr-014-ml-shadow-timing.md`](ADR/2026-06-01-adr-014-ml-shadow-timing.md)
- [`2026-06-01-adr-016-g2-ci-lower-threshold.md`](ADR/2026-06-01-adr-016-g2-ci-lower-threshold.md)
- [`2026-06-01-adr-017-spsc-ring-framework.md`](ADR/2026-06-01-adr-017-spsc-ring-framework.md)
- [`2026-06-W3-adr-018-lib-selection.md`](ADR/2026-06-W3-adr-018-lib-selection.md)
- [`2026-06-W3-adr-021-worktree-isolation.md`](ADR/2026-06-W3-adr-021-worktree-isolation.md)
- [`2026-06-W3-adr-023-ic-self-test-only.md`](ADR/2026-06-W3-adr-023-ic-self-test-only.md)
- [`2026-06-W3-adr-024-worktree-standard-workflow.md`](ADR/2026-06-W3-adr-024-worktree-standard-workflow.md)
- [`2026-06-W4-adr-013-v2-deployment-location.md`](ADR/2026-06-W4-adr-013-v2-deployment-location.md)
- [`2026-06-W4-adr-027-core-data-structure-ssot-enforce.md`](ADR/2026-06-W4-adr-027-core-data-structure-ssot-enforce.md)
- [`2026-06-W4-adr-028-docs-governance-standard.md`](ADR/2026-06-W4-adr-028-docs-governance-standard.md)
- [`2026-06-W4-adr-029-worktree-push-pr-flow.md`](ADR/2026-06-W4-adr-029-worktree-push-pr-flow.md)
- [`2026-06-W4-adr-030-bottom-up-escalation.md`](ADR/2026-06-W4-adr-030-bottom-up-escalation.md)
- [`2026-06-W4-adr-031-sprint-planning-rules.md`](ADR/2026-06-W4-adr-031-sprint-planning-rules.md)
- [`2026-06-W4-adr-032-local-first-ci-strategy.md`](ADR/2026-06-W4-adr-032-local-first-ci-strategy.md)
- [`2026-06-W5-adr-033-ci-automation-feedback.md`](ADR/2026-06-W5-adr-033-ci-automation-feedback.md)
- [`2026-06-W5-adr-034-full-flow-sop.md`](ADR/2026-06-W5-adr-034-full-flow-sop.md)
- [`2026-06-W5-adr-036-org-structure-escalation.md`](ADR/2026-06-W5-adr-036-org-structure-escalation.md)

---

## GOALSERVER

- [`baseball-data-feed.md`](GOALSERVER/baseball-data-feed.md)
- [`basketball-data-feed.md`](GOALSERVER/basketball-data-feed.md)
- [`esports-data-feed.md`](GOALSERVER/esports-data-feed.md)
- [`feeds_urls.txt`](GOALSERVER/feeds_urls.txt)
- [`full_package_feed_cn.md`](GOALSERVER/full_package_feed_cn.md)
- [`full_package_feed.txt`](GOALSERVER/full_package_feed.txt)
- [`hockey-data-feed.md`](GOALSERVER/hockey-data-feed.md)
- [`inplay-feed-new.txt`](GOALSERVER/inplay-feed-new.txt)
- [`soccer-data-feed.md`](GOALSERVER/soccer-data-feed.md)
- [`tennis-data-feed.md`](GOALSERVER/tennis-data-feed.md)
- [`ufc-data-feed.md`](GOALSERVER/ufc-data-feed.md)

---

## HIRING

- [`backlog.md`](HIRING/backlog.md)
- [`employee-registry.md`](HIRING/employee-registry.md)
- [`jd-onchain-ops-laoji-v1.md`](HIRING/jd-onchain-ops-laoji-v1.md)
- [`jd-strategy-execution-xiaoqin-v1.md`](HIRING/jd-strategy-execution-xiaoqin-v1.md)
- [`sop-v1.md`](HIRING/sop-v1.md)
- [`xiaolin-w10-w2-test-team-jds-v1.md`](HIRING/xiaolin-w10-w2-test-team-jds-v1.md)
- [`xiaolin-w9-data-structure-ic-jd-v1.md`](HIRING/xiaolin-w9-data-structure-ic-jd-v1.md)
- [`xiaolin-w9-president-onboard-and-vp-jd-v1.md`](HIRING/xiaolin-w9-president-onboard-and-vp-jd-v1.md)

---

## INCIDENTS

- [`gm-self-mistakes-log.md`](INCIDENTS/gm-self-mistakes-log.md)

---

## KPI

- [`individual-kpi-matrix.md`](KPI/individual-kpi-matrix.md)

---

## META

- [`escalate-decision-log.md`](META/escalate-decision-log.md)
- [`weekly-report-template-v3.md`](META/weekly-report-template-v3.md)

---

## OKR

- [`2026-Q2-Q3-startup-season.md`](OKR/2026-Q2-Q3-startup-season.md)
- [`laolei-2026-drive-directive-paper-profit-v1.md`](OKR/laolei-2026-drive-directive-paper-profit-v1.md)
- [`laoqian-w8-w5-profitability-kr-v1.md`](OKR/laoqian-w8-w5-profitability-kr-v1.md)

---

## RUNBOOKS

- [`aws-rtt-test-runbook-v1.md`](RUNBOOKS/aws-rtt-test-runbook-v1.md)
- [`incident-postmortem-template-v1.md`](RUNBOOKS/incident-postmortem-template-v1.md)
- [`strategy-decayed-unlock-sop-v1.md`](RUNBOOKS/strategy-decayed-unlock-sop-v1.md)

---

## SPRINTS

- [`sprint-03-backlog.md`](SPRINTS/sprint-03-backlog.md)
- [`sprint-03-w10-plan-v2.md`](SPRINTS/sprint-03-w10-plan-v2.md)

---

## RESEARCH

> 按文件名前缀 (owner/主题) 分组。全部为 live 设计 / 策略 / 路线图文档。

### `arch-*`

- [`arch-review-v3-laoguo.md`](RESEARCH/arch-review-v3-laoguo.md)
- [`arch-review-v3-laozhou.md`](RESEARCH/arch-review-v3-laozhou.md)

### `data-*`

- [`data-contract-v1.md`](RESEARCH/data-contract-v1.md)

### `frontend-*`

- [`frontend-v6-pro-dashboard-research.md`](RESEARCH/frontend-v6-pro-dashboard-research.md)
- [`frontend-v8-polymarket-portfolio-reference.md`](RESEARCH/frontend-v8-polymarket-portfolio-reference.md)

### `goalserve-*`

- [`goalserve-event-interface-research.md`](RESEARCH/goalserve-event-interface-research.md)

### `laochen-*`

- [`laochen-a-net-01-serialize-out-design-v1.md`](RESEARCH/laochen-a-net-01-serialize-out-design-v1.md)
- [`laochen-api-rate-latency-ssot-v1.md`](RESEARCH/laochen-api-rate-latency-ssot-v1.md)

### `laogao-*`

- [`laogao-code-conventions-v1.md`](RESEARCH/laogao-code-conventions-v1.md)
- [`laogao-pr-review-v1.7.md`](RESEARCH/laogao-pr-review-v1.7.md)

### `laoguo-*`

- [`laoguo-arch-deadcode-review-v1.md`](RESEARCH/laoguo-arch-deadcode-review-v1.md)
- [`laoguo-coordinator-mandate-v1.md`](RESEARCH/laoguo-coordinator-mandate-v1.md)
- [`laoguo-observability-api-review-v1.md`](RESEARCH/laoguo-observability-api-review-v1.md)
- [`laoguo-redline-rxx-audit-v1.md`](RESEARCH/laoguo-redline-rxx-audit-v1.md)
- [`laoguo-w8-w5-adr-027-main-review.md`](RESEARCH/laoguo-w8-w5-adr-027-main-review.md)

### `laohan-*`

- [`laohan-a4-p19-rm-feedliveness-slippage-unit-spec-v1.md`](RESEARCH/laohan-a4-p19-rm-feedliveness-slippage-unit-spec-v1.md)
- [`laohan-a5-dd-feed-spec-v1.md`](RESEARCH/laohan-a5-dd-feed-spec-v1.md)
- [`laohan-a5-impl-review-v1.md`](RESEARCH/laohan-a5-impl-review-v1.md)
- [`laohan-c4-review-v1.md`](RESEARCH/laohan-c4-review-v1.md)
- [`laohan-kelly-cap-cosign-v1.md`](RESEARCH/laohan-kelly-cap-cosign-v1.md)
- [`laohan-manager-mandate-v1.md`](RESEARCH/laohan-manager-mandate-v1.md)
- [`laohan-p0-1-rm-feed-contract-v1.md`](RESEARCH/laohan-p0-1-rm-feed-contract-v1.md)
- [`laohan-p0-2-c2b-bankroll-dd-spec-v1.md`](RESEARCH/laohan-p0-2-c2b-bankroll-dd-spec-v1.md)
- [`laohan-p0-2-c3-cap-truth-ssot-v1.md`](RESEARCH/laohan-p0-2-c3-cap-truth-ssot-v1.md)
- [`laohan-paperdaemon-r11-isolation-audit-v1.md`](RESEARCH/laohan-paperdaemon-r11-isolation-audit-v1.md)
- [`laohan-phaseB-buy-no-rm-checklist-v1.md`](RESEARCH/laohan-phaseB-buy-no-rm-checklist-v1.md)
- [`laohan-riskmanager-design-v0.3.1.md`](RESEARCH/laohan-riskmanager-design-v0.3.1.md)
- [`laohan-rm-v0.5-integration-spec-v1.md`](RESEARCH/laohan-rm-v0.5-integration-spec-v1.md)
- [`laohan-w8-rm-or-assertion-audit.md`](RESEARCH/laohan-w8-rm-or-assertion-audit.md)
- [`laohan-w8-rm-stale-data-ack.md`](RESEARCH/laohan-w8-rm-stale-data-ack.md)
- [`laohan-w9-orderintent-v05-spec-v1.md`](RESEARCH/laohan-w9-orderintent-v05-spec-v1.md)

### `laohe-*`

- [`laohe-cpp-footgun-checklist-v1.md`](RESEARCH/laohe-cpp-footgun-checklist-v1.md)
- [`laohe-cpp-version-selection-v1.md`](RESEARCH/laohe-cpp-version-selection-v1.md)

### `laohu-*`

- [`laohu-manager-mandate-v1.md`](RESEARCH/laohu-manager-mandate-v1.md)
- [`laohu-master-gantt-v1.md`](RESEARCH/laohu-master-gantt-v1.md)
- [`laohu-next-dev-plan-v1.md`](RESEARCH/laohu-next-dev-plan-v1.md)
- [`laohu-risk-registry-v2.4.md`](RESEARCH/laohu-risk-registry-v2.4.md)

### `laohuang-*`

- [`laohuang-compliance-redline-v2.md`](RESEARCH/laohuang-compliance-redline-v2.md)
- [`laohuang-shamir-jurisdiction-signoff-v1.md`](RESEARCH/laohuang-shamir-jurisdiction-signoff-v1.md)
- [`laohuang-us-entity-feasibility-v1.md`](RESEARCH/laohuang-us-entity-feasibility-v1.md)

### `laojiang-*`

- [`laojiang-latency-budget-w4-wave21-v1.md`](RESEARCH/laojiang-latency-budget-w4-wave21-v1.md)
- [`laojiang-observability-api-perf-budget-v1.md`](RESEARCH/laojiang-observability-api-perf-budget-v1.md)
- [`laojiang-vcpu-pin-baseline-v1.md`](RESEARCH/laojiang-vcpu-pin-baseline-v1.md)

### `laolei-*`

- [`laolei-backtest-equivalence-spec-v1.md`](RESEARCH/laolei-backtest-equivalence-spec-v1.md)
- [`laolei-feature-expansion-panel-v1.md`](RESEARCH/laolei-feature-expansion-panel-v1.md)
- [`laolei-feature-to-model-progress-ssot-v1.md`](RESEARCH/laolei-feature-to-model-progress-ssot-v1.md)
- [`laolei-inplay-clock-alignment-v1.md`](RESEARCH/laolei-inplay-clock-alignment-v1.md)
- [`laolei-phase2-label-pipeline-v1.md`](RESEARCH/laolei-phase2-label-pipeline-v1.md)
- [`laolei-pm-inplay-no-edge-decision-2026-06-04.md`](RESEARCH/laolei-pm-inplay-no-edge-decision-2026-06-04.md)
- [`laolei-position-management-synthesis-v1.md`](RESEARCH/laolei-position-management-synthesis-v1.md)
- [`laolei-results-plan-v1.md`](RESEARCH/laolei-results-plan-v1.md)
- [`laolei-short-horizon-arb-master-plan-v1.md`](RESEARCH/laolei-short-horizon-arb-master-plan-v1.md)
- [`laolei-sse-phase2-project-v1.md`](RESEARCH/laolei-sse-phase2-project-v1.md)
- [`laolei-sse-push-design-v1.md`](RESEARCH/laolei-sse-push-design-v1.md)
- [`laolei-strategic-vision-v1.md`](RESEARCH/laolei-strategic-vision-v1.md)
- [`laolei-target-position-controller-spec-v1.md`](RESEARCH/laolei-target-position-controller-spec-v1.md)
- [`laolei-target-position-strategy-direction-v1.md`](RESEARCH/laolei-target-position-strategy-direction-v1.md)
- [`laolei-timeseries-feature-substrate-v1.md`](RESEARCH/laolei-timeseries-feature-substrate-v1.md)
- [`laolei-w10-w3-industry-flow-audit-v1.md`](RESEARCH/laolei-w10-w3-industry-flow-audit-v1.md)

### `laoli-*`

- [`laoli-events-ws-mapping-spec-v1.md`](RESEARCH/laoli-events-ws-mapping-spec-v1.md)
- [`laoli-live-submitorder-plan-v1.md`](RESEARCH/laoli-live-submitorder-plan-v1.md)
- [`laoli-polymarket-api-expose-v1.md`](RESEARCH/laoli-polymarket-api-expose-v1.md)
- [`laoli-polymarket-endpoint-matrix-v3.md`](RESEARCH/laoli-polymarket-endpoint-matrix-v3.md)
- [`laoli-polymarket-market-structure-audit-v1.md`](RESEARCH/laoli-polymarket-market-structure-audit-v1.md)
- [`laoli-polymarket-reverse-sports-live-v1.md`](RESEARCH/laoli-polymarket-reverse-sports-live-v1.md)
- [`laoli-w8-polymarket-data-structure-ssot-v1.md`](RESEARCH/laoli-w8-polymarket-data-structure-ssot-v1.md)
- [`laoli-w9-w5-polymarket-market-research-update-v1.md`](RESEARCH/laoli-w9-w5-polymarket-market-research-update-v1.md)
- [`laoli-w9-wss-subscriber-impl-spec-v1.md`](RESEARCH/laoli-w9-wss-subscriber-impl-spec-v1.md)
- [`laoli-xiaoduan-api-call-optimization-v1.md`](RESEARCH/laoli-xiaoduan-api-call-optimization-v1.md)
- [`laoli-xiaoduan-cross-source-mapping-v1.md`](RESEARCH/laoli-xiaoduan-cross-source-mapping-v1.md)

### `laopeng-*`

- [`laopeng-betting-industry-analysis-v1.md`](RESEARCH/laopeng-betting-industry-analysis-v1.md)
- [`laopeng-bookmaker-history-backfill-v1.md`](RESEARCH/laopeng-bookmaker-history-backfill-v1.md)
- [`laopeng-goalserve-pregame-backfill-plan-v1.md`](RESEARCH/laopeng-goalserve-pregame-backfill-plan-v1.md)
- [`laopeng-microstructure-alpha-v1.md`](RESEARCH/laopeng-microstructure-alpha-v1.md)
- [`laopeng-multiplicative-devig-calibration-v1.md`](RESEARCH/laopeng-multiplicative-devig-calibration-v1.md)
- [`laopeng-w10-w1-p1-04-lineup-news-lag-spec-v1.md`](RESEARCH/laopeng-w10-w1-p1-04-lineup-news-lag-spec-v1.md)
- [`laopeng-w8-oq-p02-3-inplay-single-source-ack.md`](RESEARCH/laopeng-w8-oq-p02-3-inplay-single-source-ack.md)
- [`laopeng-w9-inplay-edge-gross-net-confirm.md`](RESEARCH/laopeng-w9-inplay-edge-gross-net-confirm.md)
- [`laopeng-w9-w5-betting-industry-research-update-v1.md`](RESEARCH/laopeng-w9-w5-betting-industry-research-update-v1.md)

### `laoqian-*`

- [`laoqian-business-capabilities-v1.md`](RESEARCH/laoqian-business-capabilities-v1.md)
- [`laoqian-mvp-scope-rejection-v1.md`](RESEARCH/laoqian-mvp-scope-rejection-v1.md)

### `laoshan-*`

- [`laoshan-monocypher-vs-libsodium-w7-ack.md`](RESEARCH/laoshan-monocypher-vs-libsodium-w7-ack.md)

### `laoshen-*`

- [`laoshen-abi-v2-coordination-plan-v1.md`](RESEARCH/laoshen-abi-v2-coordination-plan-v1.md)
- [`laoshen-key-management-coreview-v1.md`](RESEARCH/laoshen-key-management-coreview-v1.md)
- [`laoshen-key-vendor-selection-v2.md`](RESEARCH/laoshen-key-vendor-selection-v2.md)
- [`laoshen-multi-vendor-kms-v1.md`](RESEARCH/laoshen-multi-vendor-kms-v1.md)
- [`laoshen-rm-v0.5-field-freeze-spec-v1.md`](RESEARCH/laoshen-rm-v0.5-field-freeze-spec-v1.md)
- [`laoshen-threat-model-v2.md`](RESEARCH/laoshen-threat-model-v2.md)

### `laosun-*`

- [`laosun-key-management-v5.1.md`](RESEARCH/laosun-key-management-v5.1.md)
- [`laosun-libsodium-fetchcontent-w7-plan.md`](RESEARCH/laosun-libsodium-fetchcontent-w7-plan.md)
- [`laosun-live-order-signing-plan-v1.md`](RESEARCH/laosun-live-order-signing-plan-v1.md)
- [`laosun-paper-keypair-isolation-fix-v1.md`](RESEARCH/laosun-paper-keypair-isolation-fix-v1.md)
- [`laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md`](RESEARCH/laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md)
- [`laosun-w9-signer-v53-abi-align-spec-v1.md`](RESEARCH/laosun-w9-signer-v53-abi-align-spec-v1.md)

### `laotang-*`

- [`laotang-audit-schema-v1.1.md`](RESEARCH/laotang-audit-schema-v1.1.md)
- [`laotang-rm-audit-live-integration-v1.md`](RESEARCH/laotang-rm-audit-live-integration-v1.md)

### `laowang-*`

- [`laowang-wal-framework-cpp-interface-v1.md`](RESEARCH/laowang-wal-framework-cpp-interface-v1.md)

### `laowu-*`

- [`laowu-cross-region-deployment-v0.1.md`](RESEARCH/laowu-cross-region-deployment-v0.1.md)
- [`laowu-paper-runtime-binary-deploy-alignment-v1.md`](RESEARCH/laowu-paper-runtime-binary-deploy-alignment-v1.md)
- [`laowu-paper-runtime-deploy-plan-v1.md`](RESEARCH/laowu-paper-runtime-deploy-plan-v1.md)
- [`laowu-proxy-goalserve-bandwidth-v1.md`](RESEARCH/laowu-proxy-goalserve-bandwidth-v1.md)
- [`laowu-toolstack-install-v1.md`](RESEARCH/laowu-toolstack-install-v1.md)
- [`laowu-w10-w2-rtt-3region-results-v1.md`](RESEARCH/laowu-w10-w2-rtt-3region-results-v1.md)
- [`laowu-w8-polymarket-origin-verification-v1.md`](RESEARCH/laowu-w8-polymarket-origin-verification-v1.md)
- [`laowu-w9-w1-aws-region-rtt-test-plan-v1.md`](RESEARCH/laowu-w9-w1-aws-region-rtt-test-plan-v1.md)

### `laoxu-*`

- [`laoxu-external-tools-inventory-v1.md`](RESEARCH/laoxu-external-tools-inventory-v1.md)
- [`laoxu-r39-escalate-flow-v0.3.md`](RESEARCH/laoxu-r39-escalate-flow-v0.3.md)

### `laoye-*`

- [`laoye-nonce-manager-design-v1.md`](RESEARCH/laoye-nonce-manager-design-v1.md)
- [`laoye-polygon-rpc-endpoint-matrix-v1.md`](RESEARCH/laoye-polygon-rpc-endpoint-matrix-v1.md)
- [`laoye-polygon-rpc-selection-v1.md`](RESEARCH/laoye-polygon-rpc-selection-v1.md)
- [`laoye-receiver-whitelist-v1.md`](RESEARCH/laoye-receiver-whitelist-v1.md)
- [`laoye-w10-w1-g3-kr-financial-review-v1.md`](RESEARCH/laoye-w10-w1-g3-kr-financial-review-v1.md)

### `laozhou-*`

- [`laozhou-a1-positionledger-micro-migration-v1.md`](RESEARCH/laozhou-a1-positionledger-micro-migration-v1.md)
- [`laozhou-architecture-v0.6-e2e.md`](RESEARCH/laozhou-architecture-v0.6-e2e.md)
- [`laozhou-binary-dual-side-arch-v1.md`](RESEARCH/laozhou-binary-dual-side-arch-v1.md)
- [`laozhou-lifecycle-management-v1.md`](RESEARCH/laozhou-lifecycle-management-v1.md)
- [`laozhou-manager-mandate-v1.md`](RESEARCH/laozhou-manager-mandate-v1.md)
- [`laozhou-market-structure-contract-fix-v1.md`](RESEARCH/laozhou-market-structure-contract-fix-v1.md)
- [`laozhou-microusd-c2c5-integration-spec-v1.md`](RESEARCH/laozhou-microusd-c2c5-integration-spec-v1.md)
- [`laozhou-microusd-unit-contract-spec-v1.md`](RESEARCH/laozhou-microusd-unit-contract-spec-v1.md)
- [`laozhou-observability-api-arch-v1.md`](RESEARCH/laozhou-observability-api-arch-v1.md)
- [`laozhou-p0-1-rm-feed-architecture-v1.md`](RESEARCH/laozhou-p0-1-rm-feed-architecture-v1.md)
- [`laozhou-paperdaemon-refactor-arch-review-v1.md`](RESEARCH/laozhou-paperdaemon-refactor-arch-review-v1.md)
- [`laozhou-single-instance-spec-v1.md`](RESEARCH/laozhou-single-instance-spec-v1.md)
- [`laozhou-vcpu-v0.7-update.md`](RESEARCH/laozhou-vcpu-v0.7-update.md)
- [`laozhou-w8-debug-rest-api-spec-v1.md`](RESEARCH/laozhou-w8-debug-rest-api-spec-v1.md)
- [`laozhou-w8-engineering-abi-gap-audit-v1.md`](RESEARCH/laozhou-w8-engineering-abi-gap-audit-v1.md)

### `ml-*`

- [`ml-calibration-and-edge-reality-v1.md`](RESEARCH/ml-calibration-and-edge-reality-v1.md)
- [`ml-engineer-fairvalue-alpha-design-v1.md`](RESEARCH/ml-engineer-fairvalue-alpha-design-v1.md)
- [`ml-feature-validity-audit-and-plan-v1.md`](RESEARCH/ml-feature-validity-audit-and-plan-v1.md)

### `mm-*`

- [`mm-risk-assessment-data-v1.md`](RESEARCH/mm-risk-assessment-data-v1.md)

### `polymarket-*`

- [`polymarket-mechanics-verified-2026-06-03.md`](RESEARCH/polymarket-mechanics-verified-2026-06-03.md)

### `posmgmt-*`

- [`posmgmt-equities-v1.md`](RESEARCH/posmgmt-equities-v1.md)
- [`posmgmt-github-repos-v1.md`](RESEARCH/posmgmt-github-repos-v1.md)
- [`posmgmt-marketmaking-v1.md`](RESEARCH/posmgmt-marketmaking-v1.md)
- [`posmgmt-predmarket-fee-v1.md`](RESEARCH/posmgmt-predmarket-fee-v1.md)
- [`posmgmt-sportsbetting-v1.md`](RESEARCH/posmgmt-sportsbetting-v1.md)

### `profit-*`

- [`profit-scheme-DRAFT-v1.md`](RESEARCH/profit-scheme-DRAFT-v1.md)
- [`profit-scheme-finance-v1.md`](RESEARCH/profit-scheme-finance-v1.md)
- [`profit-scheme-legacy-and-future-v1.md`](RESEARCH/profit-scheme-legacy-and-future-v1.md)
- [`profit-scheme-polymarket-mechanics-v1.md`](RESEARCH/profit-scheme-polymarket-mechanics-v1.md)
- [`profit-scheme-quant-v1.md`](RESEARCH/profit-scheme-quant-v1.md)
- [`profit-scheme-round2-finance.md`](RESEARCH/profit-scheme-round2-finance.md)
- [`profit-scheme-round2-polymarket.md`](RESEARCH/profit-scheme-round2-polymarket.md)
- [`profit-scheme-round2-quant.md`](RESEARCH/profit-scheme-round2-quant.md)
- [`profit-scheme-round2-trader.md`](RESEARCH/profit-scheme-round2-trader.md)
- [`profit-scheme-trader-v1.md`](RESEARCH/profit-scheme-trader-v1.md)
- [`profit-scheme-v2-FINAL.md`](RESEARCH/profit-scheme-v2-FINAL.md)
- [`profit-scheme-v3-arb-only.md`](RESEARCH/profit-scheme-v3-arb-only.md)

### `quant-*`

- [`quant-microstructure-eventdelay-arb-feasibility-v1.md`](RESEARCH/quant-microstructure-eventdelay-arb-feasibility-v1.md)

### `shorthorizon-*`

- [`shorthorizon-locking-feasibility-v1.md`](RESEARCH/shorthorizon-locking-feasibility-v1.md)

### `xiaobai-*`

- [`xiaobai-live-external-data-security-v1.md`](RESEARCH/xiaobai-live-external-data-security-v1.md)
- [`xiaobai-live-order-key-security-checklist-v1.md`](RESEARCH/xiaobai-live-order-key-security-checklist-v1.md)
- [`xiaobai-llm-dev-conventions-v1.md`](RESEARCH/xiaobai-llm-dev-conventions-v1.md)
- [`xiaobai-observability-api-security-v1.md`](RESEARCH/xiaobai-observability-api-security-v1.md)
- [`xiaobai-observability-security-audit-v2.md`](RESEARCH/xiaobai-observability-security-audit-v2.md)
- [`xiaobai-security-preaudit-gap-list-v1.md`](RESEARCH/xiaobai-security-preaudit-gap-list-v1.md)

### `xiaocheng-*`

- [`xiaocheng-alpha-path-to-first-fill-v1.md`](RESEARCH/xiaocheng-alpha-path-to-first-fill-v1.md)
- [`xiaocheng-p0_02-signal-spec-v0.1.md`](RESEARCH/xiaocheng-p0_02-signal-spec-v0.1.md)
- [`xiaocheng-p0-02-alpha-v2-backtest-spec-v0.2.md`](RESEARCH/xiaocheng-p0-02-alpha-v2-backtest-spec-v0.2.md)
- [`xiaocheng-pnl-attribution-calc-spec-v1.md`](RESEARCH/xiaocheng-pnl-attribution-calc-spec-v1.md)
- [`xiaocheng-pnl-calc-integration-v1.md`](RESEARCH/xiaocheng-pnl-calc-integration-v1.md)
- [`xiaocheng-signal-catalog-v1.md`](RESEARCH/xiaocheng-signal-catalog-v1.md)
- [`xiaocheng-w10-w1-p0-02-spec-v02.md`](RESEARCH/xiaocheng-w10-w1-p0-02-spec-v02.md)
- [`xiaocheng-w10-w1-p0-03-cross-platform-arb-spec.md`](RESEARCH/xiaocheng-w10-w1-p0-03-cross-platform-arb-spec.md)

### `xiaodeng-*`

- [`xiaodeng-dashboard-ai-params-spec-v1.md`](RESEARCH/xiaodeng-dashboard-ai-params-spec-v1.md)
- [`xiaodeng-ml-data-infra-v1.md`](RESEARCH/xiaodeng-ml-data-infra-v1.md)
- [`xiaodeng-ml-roadmap-v2.md`](RESEARCH/xiaodeng-ml-roadmap-v2.md)

### `xiaodong-*`

- [`xiaodong-data-quality-validation-framework-v1.md`](RESEARCH/xiaodong-data-quality-validation-framework-v1.md)
- [`xiaodong-m45-gate-framework-v1.md`](RESEARCH/xiaodong-m45-gate-framework-v1.md)
- [`xiaodong-stats-validation-attestation-spec-v1.md`](RESEARCH/xiaodong-stats-validation-attestation-spec-v1.md)
- [`xiaodong-stats-validation-framework-v1.md`](RESEARCH/xiaodong-stats-validation-framework-v1.md)

### `xiaodu-*`

- [`xiaodu-prd-v2-user-journey.md`](RESEARCH/xiaodu-prd-v2-user-journey.md)
- [`xiaodu-totals-market-prd-v1.md`](RESEARCH/xiaodu-totals-market-prd-v1.md)

### `xiaoduan-*`

- [`xiaoduan-goalserve-adapter-schema-v1.md`](RESEARCH/xiaoduan-goalserve-adapter-schema-v1.md)
- [`xiaoduan-goalserve-odds-by-sport-v2.1.md`](RESEARCH/xiaoduan-goalserve-odds-by-sport-v2.1.md)
- [`xiaoduan-goalserve-official-doc-v3.md`](RESEARCH/xiaoduan-goalserve-official-doc-v3.md)
- [`xiaoduan-w8-goalserve-data-structure-ssot-v1.md`](RESEARCH/xiaoduan-w8-goalserve-data-structure-ssot-v1.md)
- [`xiaoduan-w8-goalserve-inplay-node-verification-v1.md`](RESEARCH/xiaoduan-w8-goalserve-inplay-node-verification-v1.md)
- [`xiaoduan-w9-w5-goalserve-research-update-v1.md`](RESEARCH/xiaoduan-w9-w5-goalserve-research-update-v1.md)

### `xiaofeng-*`

- [`xiaofeng-clob-orderbook-keying-check-v1.md`](RESEARCH/xiaofeng-clob-orderbook-keying-check-v1.md)

### `xiaogong-*`

- [`xiaogong-dogfood-playbook-v1.md`](RESEARCH/xiaogong-dogfood-playbook-v1.md)
- [`xiaogong-v3-dashboard-dogfood.md`](RESEARCH/xiaogong-v3-dashboard-dogfood.md)

### `xiaojiang-*`

- [`xiaojiang-backtest-framework-v0.2-cpp.md`](RESEARCH/xiaojiang-backtest-framework-v0.2-cpp.md)
- [`xiaojiang-paper-engine-skeleton-v1.md`](RESEARCH/xiaojiang-paper-engine-skeleton-v1.md)
- [`xiaojiang-paper-trading-engine-v0.2-cpp.md`](RESEARCH/xiaojiang-paper-trading-engine-v0.2-cpp.md)

### `xiaoliang-*`

- [`xiaoliang-binary-dual-side-strategy-v1.md`](RESEARCH/xiaoliang-binary-dual-side-strategy-v1.md)
- [`xiaoliang-kelly-sizing-spec-v1.md`](RESEARCH/xiaoliang-kelly-sizing-spec-v1.md)
- [`xiaoliang-manager-mandate-v1.md`](RESEARCH/xiaoliang-manager-mandate-v1.md)
- [`xiaoliang-market-structure-v1.md`](RESEARCH/xiaoliang-market-structure-v1.md)
- [`xiaoliang-phaseB-select-side-algo-v1.md`](RESEARCH/xiaoliang-phaseB-select-side-algo-v1.md)

### `xiaolu-*`

- [`xiaolu-w9-rest-api-skeleton-implementation-spec-v1.md`](RESEARCH/xiaolu-w9-rest-api-skeleton-implementation-spec-v1.md)

### `xiaoshi-*`

- [`xiaoshi-data-structures-selection-v1.md`](RESEARCH/xiaoshi-data-structures-selection-v1.md)

### `xiaosong-*`

- [`xiaosong-adr010-wno-check-grep-spec.md`](RESEARCH/xiaosong-adr010-wno-check-grep-spec.md)
- [`xiaosong-chaos-replay-framework-spec-v1.md`](RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md)
- [`xiaosong-paperdaemon-refactor-test-plan-v1.md`](RESEARCH/xiaosong-paperdaemon-refactor-test-plan-v1.md)
- [`xiaosong-test-framework-cpp-skeleton-v1.md`](RESEARCH/xiaosong-test-framework-cpp-skeleton-v1.md)
- [`xiaosong-test-framework-v0.2.md`](RESEARCH/xiaosong-test-framework-v0.2.md)
- [`xiaosong-test-gap-report-v1.md`](RESEARCH/xiaosong-test-gap-report-v1.md)
- [`xiaosong-test-replay-framework-v0.1.md`](RESEARCH/xiaosong-test-replay-framework-v0.1.md)
- [`xiaosong-wno-cleanup-extension-w7-plan.md`](RESEARCH/xiaosong-wno-cleanup-extension-w7-plan.md)

### `xiaosu-*`

- [`xiaosu-backend-api-requirements-v1.md`](RESEARCH/xiaosu-backend-api-requirements-v1.md)
- [`xiaosu-frontend-api-needs-v1.md`](RESEARCH/xiaosu-frontend-api-needs-v1.md)
- [`xiaosu-ui-wireframe-v0.1.md`](RESEARCH/xiaosu-ui-wireframe-v0.1.md)
- [`xiaosu-w10-w1-frontend-ui-v1-spec.md`](RESEARCH/xiaosu-w10-w1-frontend-ui-v1-spec.md)

### `xiaotian-*`

- [`xiaotian-feature-store-schema-v1.md`](RESEARCH/xiaotian-feature-store-schema-v1.md)
- [`xiaotian-parquet-partition-v1.md`](RESEARCH/xiaotian-parquet-partition-v1.md)

### `xiaoxiao-*`

- [`xiaoxiao-kelly-slippage-model-v1.md`](RESEARCH/xiaoxiao-kelly-slippage-model-v1.md)
- [`xiaoxiao-paper-runtime-integration-design-v1.md`](RESEARCH/xiaoxiao-paper-runtime-integration-design-v1.md)
- [`xiaoxiao-slippage-model-lib-v1.md`](RESEARCH/xiaoxiao-slippage-model-lib-v1.md)

### `xiaoying-*`

- [`xiaoying-acceptance-spec-v1.md`](RESEARCH/xiaoying-acceptance-spec-v1.md)
- [`xiaoying-acceptance-spec-v2.md`](RESEARCH/xiaoying-acceptance-spec-v2.md)
- [`xiaoying-m1-acceptance-spec-v2.md`](RESEARCH/xiaoying-m1-acceptance-spec-v2.md)
- [`xiaoying-paper-gate-harness-v1.md`](RESEARCH/xiaoying-paper-gate-harness-v1.md)

### `xiaoyou-*`

- [`xiaoyou-dashboard-declutter-options-v1.md`](RESEARCH/xiaoyou-dashboard-declutter-options-v1.md)
- [`xiaoyou-frontend-v6-design-v1.md`](RESEARCH/xiaoyou-frontend-v6-design-v1.md)
- [`xiaoyou-frontend-v8-collapse-design-v1.md`](RESEARCH/xiaoyou-frontend-v8-collapse-design-v1.md)
- [`xiaoyou-solidjs-v52-ux-review.md`](RESEARCH/xiaoyou-solidjs-v52-ux-review.md)
- [`xiaoyou-ux-interface-requirements-v1.md`](RESEARCH/xiaoyou-ux-interface-requirements-v1.md)
- [`xiaoyou-v3-dashboard-ux-review.md`](RESEARCH/xiaoyou-v3-dashboard-ux-review.md)
- [`xiaoyou-w10-w1-ux-framework-spec.md`](RESEARCH/xiaoyou-w10-w1-ux-framework-spec.md)

### `xiaoyu-*`

- [`xiaoyu-goalserve-score-integration-plan-v1.md`](RESEARCH/xiaoyu-goalserve-score-integration-plan-v1.md)
- [`xiaoyu-manager-mandate-v1.md`](RESEARCH/xiaoyu-manager-mandate-v1.md)

### `xiaoyuan-*`

- [`xiaoyuan-binary-dual-side-decision-v1.md`](RESEARCH/xiaoyuan-binary-dual-side-decision-v1.md)
- [`xiaoyuan-fill-rate-model-v0.1.md`](RESEARCH/xiaoyuan-fill-rate-model-v0.1.md)
- [`xiaoyuan-microstructure-v1.md`](RESEARCH/xiaoyuan-microstructure-v1.md)

### `xiaozheng-*`

- [`xiaozheng-dev-observability-page-spec-v1.md`](RESEARCH/xiaozheng-dev-observability-page-spec-v1.md)
- [`xiaozheng-observability-endpoints-v1.md`](RESEARCH/xiaozheng-observability-endpoints-v1.md)
- [`xiaozheng-observability-v0.1.md`](RESEARCH/xiaozheng-observability-v0.1.md)

---

## 文档统计 (2026-06-08 清仓后)

| 目录 | 文件数 |
|---|---|
| ADR/ | 39 |
| GOALSERVER/ | 12 |
| HIRING/ | 8 |
| INCIDENTS/ | 1 |
| KPI/ | 1 |
| META/ | 2 |
| OKR/ | 3 |
| RESEARCH/ | 229 |
| RUNBOOKS/ | 3 |
| SPRINTS/ | 2 |
| **总计 (md+txt)** | **307** |

**Last updated:** 2026-06-08 (v4, 治理清仓重生 — 删 174 历史文档后按实际文件重建)
