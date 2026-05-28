# docs/ 索引（SSOT）v2

> 守护：小米（doc-curator）。每个文档必须有 owner + last_review。
> 入档审批：小米；战略级文档（OKR / ADR / CONVENTIONS-*）需老雷会签。
> 命名规范：见 [`CONVENTIONS-naming.md`](CONVENTIONS-naming.md)。
> 当前版本：v2 (2026-05-28, Sprint-1 全量收口 + Sprint-2 W2 启动)

## 一级分类

| 目录 | 用途 | Owner |
|---|---|---|
| `MEETINGS/` | 会议纪要（按日期） | 小米 |
| `OKR/` | 季度 / 半年 OKR | 老雷 + 单元 owner |
| `KPI/` | 个人 / 团队 KPI 矩阵 | 老雷 + 小林 |
| `ADR/` | 架构决策记录 + GM 决议 | 老郭 + 老雷 |
| `HIRING/` | 扩招 backlog + JD + SOP | 小林 |
| `SPRINTS/` | Sprint backlog / retro / final | 老胡 |
| `RESEARCH/` | 技术预研报告 | 各 owner |
| `INCIDENTS/` | 事故 post-mortem | 老唐 + 责任 owner |
| `GOALSERVER/` | Goalserve feed 文档原件 | 小段 |

## 顶级文档

- [`INDEX.md`](INDEX.md) — 本文件 (v2)
- [`CONVENTIONS-naming.md`](CONVENTIONS-naming.md) — 文档命名规范 v1（小米）

---

## MEETINGS

- [`MEETINGS/2026-05-28-kickoff.md`](MEETINGS/2026-05-28-kickoff.md) — 创始全体会
- [`MEETINGS/2026-05-28-hr-launch.md`](MEETINGS/2026-05-28-hr-launch.md) — 人事扩招联席会
- [`MEETINGS/2026-05-28-sprint1-retro-all-hands.md`](MEETINGS/2026-05-28-sprint1-retro-all-hands.md) — Sprint-1 Retro 全员对齐 (STCPP-MTG-003)
- [`MEETINGS/sprint1-retro/`](MEETINGS/sprint1-retro/) — Sprint-1 Retro 15 份个人发言原件 (老周/老韩/老胡/老黄/老李/老叶/老郭/老钱/小程/小董/小蒋/小袁/小梁/小宋/小肖)

## OKR / KPI

- [`OKR/2026-Q2-Q3-startup-season.md`](OKR/2026-Q2-Q3-startup-season.md) — 起步季 OKR (M1-M5 + M4.5 gate)
- [`KPI/individual-kpi-matrix.md`](KPI/individual-kpi-matrix.md) — 个人 KPI 矩阵

## HIRING

- [`HIRING/backlog.md`](HIRING/backlog.md) — 扩招 backlog
- [`HIRING/sop-v1.md`](HIRING/sop-v1.md) — 招聘 SOP

## SPRINTS

- [`SPRINTS/sprint-01.md`](SPRINTS/sprint-01.md) — Sprint-01 Backlog (26 ticket)
- [`SPRINTS/sprint-01-final.md`](SPRINTS/sprint-01-final.md) — Sprint-01 总结归档 (小米, 2026-05-28)
- [`SPRINTS/sprint-02.md`](SPRINTS/sprint-02.md) — Sprint-02 Backlog (28 ticket)

## ADR (按时间线)

**2026-05-28 (Sprint-1 收口日, 共 13 份):**
- [`ADR/2026-05-28-arch-and-rm-v0.1-review.md`](ADR/2026-05-28-arch-and-rm-v0.1-review.md) — ADR-001 老周架构 + 老韩 RM v0.1 联合评审
- [`ADR/2026-05-28-gm-decision-defer-onchain-until-profitable.md`](ADR/2026-05-28-gm-decision-defer-onchain-until-profitable.md) — GM 决议 延后链上深度优化至盈利后
- [`ADR/2026-05-28-gm-decision-goalserve-odds-gap.md`](ADR/2026-05-28-gm-decision-goalserve-odds-gap.md) — GM 决议 Goalserve odds 缺口
- [`ADR/2026-05-28-gm-policy-cross-domain-listening.md`](ADR/2026-05-28-gm-policy-cross-domain-listening.md) — GM 政策 跨域听取义务
- [`ADR/2026-05-28-gm-policy-jurisdictional-deferral.md`](ADR/2026-05-28-gm-policy-jurisdictional-deferral.md) — GM 政策 地域法律暂不纠缠
- [`ADR/2026-05-28-gm-policy-api-monitoring-longterm.md`](ADR/2026-05-28-gm-policy-api-monitoring-longterm.md) — GM 政策 API 长期监控
- [`ADR/2026-05-28-gm-commitment-sygnum-deadline.md`](ADR/2026-05-28-gm-commitment-sygnum-deadline.md) — GM 承诺 Sygnum 6/11 contact-made
- [`ADR/2026-05-28-gm-redline-websocket-non-blocking.md`](ADR/2026-05-28-gm-redline-websocket-non-blocking.md) — GM 红线 R-12 WebSocket event loop 不阻塞
- [`ADR/2026-05-28-gm-redline-data-source-timestamping.md`](ADR/2026-05-28-gm-redline-data-source-timestamping.md) — **GM 红线 R-20 所有数据源使用必须标时间信息 (4 时间戳契约)** [新, 老雷 2026-05-28 立]
- [`ADR/2026-05-28-gm-signoff-adr-001.md`](ADR/2026-05-28-gm-signoff-adr-001.md) — GM sign-off ADR-001 升 Accepted (final)
- [`ADR/2026-05-28-gm-signoff-paper-trade.md`](ADR/2026-05-28-gm-signoff-paper-trade.md) — GM sign-off paper engine 共享 binary multi-mode (R-11)
- [`ADR/2026-05-28-gm-signoff-rm-v0.2.md`](ADR/2026-05-28-gm-signoff-rm-v0.2.md) — GM sign-off 老韩 RM v0.2 (KELLY 0.25 / fill_rate 0.50 / PER_ORDER $5K/$2K)
- [`ADR/2026-05-28-gm-signoff-sprint1-retro.md`](ADR/2026-05-28-gm-signoff-sprint1-retro.md) — GM 决议总表 Sprint-1 Retro (D-01 ~ D-18, 18 条决议)

## RESEARCH (Sprint-1 主体, 共 78 篇)

### A. 系统工程部 (老周单元, 16 篇)

- [`RESEARCH/laozhou-architecture-v0.4.md`](RESEARCH/laozhou-architecture-v0.4.md) — 架构 v0.4 (老周, **ACTIVE**, ADR-001 final)
  - 旧版 (Superseded → v0.4): [`v0.3`](RESEARCH/laozhou-architecture-v0.3.md) | [`v0.2`](RESEARCH/laozhou-architecture-v0.2.md) | [`v0.1`](RESEARCH/laozhou-architecture-v0.1.md)
- [`RESEARCH/laozhou-lifecycle-management-v1.md`](RESEARCH/laozhou-lifecycle-management-v1.md) — 模块生命周期 v1 (老周)
- [`RESEARCH/laoli-polymarket-endpoint-matrix-v3.md`](RESEARCH/laoli-polymarket-endpoint-matrix-v3.md) — Polymarket endpoint matrix v3 (老李, **ACTIVE**)
  - 旧版 (Superseded → v3): [`v2`](RESEARCH/laoli-polymarket-endpoint-matrix-v2.md) | [`api-spec-v1`](RESEARCH/laoli-polymarket-api-spec-v1.md)
- [`RESEARCH/laoli-xiaoduan-api-call-optimization-v1.md`](RESEARCH/laoli-xiaoduan-api-call-optimization-v1.md) — API call 优化 v1 (老李+小段)
- [`RESEARCH/laosun-key-management-v5-simplified.md`](RESEARCH/laosun-key-management-v5-simplified.md) — 私钥管理 v5 简化版 (老孙, **ACTIVE**)
  - 旧版 (Superseded → v5): [`v4-cpp`](RESEARCH/laosun-key-management-v4-cpp.md) | [`v3`](RESEARCH/laosun-key-management-v3.md) | [`v2`](RESEARCH/laosun-key-management-v2.md) | [`v1`](RESEARCH/laosun-key-management-v1.md)
- [`RESEARCH/laochen-network-bench-v1.md`](RESEARCH/laochen-network-bench-v1.md) — 跨洋网络 bench v1 (老陈)
- [`RESEARCH/laochen-api-rate-latency-ssot-v1.md`](RESEARCH/laochen-api-rate-latency-ssot-v1.md) — API 速率 + 延迟 SSOT v1 (老陈)
- [`RESEARCH/laowu-cross-region-deployment-v0.1.md`](RESEARCH/laowu-cross-region-deployment-v0.1.md) — 跨洋部署 v0.1 (老吴+老叶)
- [`RESEARCH/laowu-proxy-goalserve-bandwidth-v1.md`](RESEARCH/laowu-proxy-goalserve-bandwidth-v1.md) — Goalserve 带宽专项 v1 (老吴)
- [`RESEARCH/laowu-toolstack-install-v1.md`](RESEARCH/laowu-toolstack-install-v1.md) — 工具栈装机报告 v1 (老吴)
- [`RESEARCH/laojiang-latency-budget-v1.md`](RESEARCH/laojiang-latency-budget-v1.md) — 延迟预算拆解 v1 (老姜)
- [`RESEARCH/laowang-wal-framework-v0.1.md`](RESEARCH/laowang-wal-framework-v0.1.md) — WAL framework v0.1 (老王)
- [`RESEARCH/xiaoshi-data-structures-selection-v1.md`](RESEARCH/xiaoshi-data-structures-selection-v1.md) — 数据结构选型 v1 (小石)
- [`RESEARCH/xiaozheng-observability-v0.1.md`](RESEARCH/xiaozheng-observability-v0.1.md) — 观测栈 v0.1 (小郑)
- [`RESEARCH/xiaosu-ui-wireframe-v0.1.md`](RESEARCH/xiaosu-ui-wireframe-v0.1.md) — Operator UI wireframe v0.1 (小苏)

### B. 风控合规部 (老韩单元, 11 篇)

- [`RESEARCH/laohan-riskmanager-design-v0.3.md`](RESEARCH/laohan-riskmanager-design-v0.3.md) — RiskManager v0.3 (老韩, **ACTIVE**, Sprint-2 W2 截止)
  - 旧版 (Superseded → v0.3): [`v0.2`](RESEARCH/laohan-riskmanager-design-v0.2.md) | [`v0.1`](RESEARCH/laohan-riskmanager-design-v0.1.md)
- [`RESEARCH/laoshen-key-vendor-selection-v2.md`](RESEARCH/laoshen-key-vendor-selection-v2.md) — Key vendor 选型 v2 (老沈, **ACTIVE**)
  - 旧版 (Superseded → v2): [`coreview-v1`](RESEARCH/laoshen-key-management-coreview-v1.md) | [`multi-vendor-kms-v1`](RESEARCH/laoshen-multi-vendor-kms-v1.md)
- [`RESEARCH/laoshen-threat-model-v2.md`](RESEARCH/laoshen-threat-model-v2.md) — 威胁模型 v2 (老沈, **ACTIVE**)
  - 旧版 (Superseded → v2): [`v1`](RESEARCH/laoshen-threat-model-v1.md)
- [`RESEARCH/laohuang-compliance-redline-v2.md`](RESEARCH/laohuang-compliance-redline-v2.md) — 合规红线 v2 (老黄, **ACTIVE**)
  - 旧版 (Superseded → v2): [`v1`](RESEARCH/laohuang-compliance-redline-v1.md)
- [`RESEARCH/laohuang-compliance-signoff.md`](RESEARCH/laohuang-compliance-signoff.md) — 合规红线签收表 (老黄, 截止 2026-06-11)
- [`RESEARCH/laohuang-shamir-jurisdiction-signoff-v1.md`](RESEARCH/laohuang-shamir-jurisdiction-signoff-v1.md) — Shamir + 司法管辖签收 v1 (老黄)
- [`RESEARCH/laohuang-us-entity-feasibility-v1.md`](RESEARCH/laohuang-us-entity-feasibility-v1.md) — US entity 可行性 v1 (老黄, **D-15 Escalated GM**)
- [`RESEARCH/laotang-audit-schema-v1.md`](RESEARCH/laotang-audit-schema-v1.md) — Audit schema v1 (老唐, BLAKE3 hash chain)

### C. 量化研究部 (小梁单元, 10 篇)

- [`RESEARCH/xiaoliang-market-structure-v1.md`](RESEARCH/xiaoliang-market-structure-v1.md) — 体育市场结构 v1 (小梁) (v1.1 Sprint-2 W3 升)
- [`RESEARCH/xiaocheng-signal-catalog-v1.md`](RESEARCH/xiaocheng-signal-catalog-v1.md) — 信号 catalog v1 (小程, P0-01 5¢ 阈值 D-02 待改)
- [`RESEARCH/xiaoyuan-microstructure-v1.md`](RESEARCH/xiaoyuan-microstructure-v1.md) — 微观结构 v1 (小袁, INPLAY_HOT_CRIT 新增)
- [`RESEARCH/xiaoxiao-kelly-slippage-model-v1.md`](RESEARCH/xiaoxiao-kelly-slippage-model-v1.md) — Kelly + slippage 模型 v1 (小肖)
- [`RESEARCH/xiaoxiao-slippage-model-lib-v1.md`](RESEARCH/xiaoxiao-slippage-model-lib-v1.md) — SlippageModel C++ lib v1 (小肖, D-10 命名联签)
- [`RESEARCH/xiaodong-stats-validation-framework-v1.md`](RESEARCH/xiaodong-stats-validation-framework-v1.md) — 统计验证 framework v1 (小董, M4.5 7 gate)
- [`RESEARCH/xiaojiang-paper-engine-skeleton-v1.md`](RESEARCH/xiaojiang-paper-engine-skeleton-v1.md) — Paper engine skeleton v1 (小蒋, R-21 闸 1)
- [`RESEARCH/xiaojiang-backtest-framework-v0.2-cpp.md`](RESEARCH/xiaojiang-backtest-framework-v0.2-cpp.md) — Backtest framework v0.2 C++ (小蒋, **ACTIVE**)
  - 旧版 (Superseded → v0.2): [`v0.1`](RESEARCH/xiaojiang-backtest-framework-v0.1.md)
- [`RESEARCH/xiaojiang-paper-trading-engine-v0.2-cpp.md`](RESEARCH/xiaojiang-paper-trading-engine-v0.2-cpp.md) — Paper trading engine v0.2 C++ (小蒋, **ACTIVE**)
  - 旧版 (Superseded → v0.2): [`v0.1`](RESEARCH/xiaojiang-paper-trading-engine-v0.1.md)
- [`RESEARCH/laopeng-betting-industry-analysis-v1.md`](RESEARCH/laopeng-betting-industry-analysis-v1.md) — 体育博彩行业市场分析 v1 (老彭)

### D. 数据基础设施部 (小余单元, 6 篇)

- [`RESEARCH/data-contract-v1.md`](RESEARCH/data-contract-v1.md) — 数据契约 v1 (小余/小邓, 4 时间戳契约, R-20 关联)
- [`RESEARCH/xiaoduan-goalserve-official-doc-v3.md`](RESEARCH/xiaoduan-goalserve-official-doc-v3.md) — Goalserve official doc v3 (小段, **ACTIVE**)
  - 旧版 (Superseded → v3): [`endpoint-matrix-v2`](RESEARCH/xiaoduan-goalserve-endpoint-matrix-v2.md) | [`api-spec-v1`](RESEARCH/xiaoduan-goalserve-api-spec-v1.md)
- [`RESEARCH/xiaoduan-goalserve-odds-by-sport-v2.1.md`](RESEARCH/xiaoduan-goalserve-odds-by-sport-v2.1.md) — Goalserve 各 sport odds v2.1 (小段)
- [`RESEARCH/xiaodeng-ml-roadmap-v2.md`](RESEARCH/xiaodeng-ml-roadmap-v2.md) — ML roadmap v2 (小邓, **ACTIVE**)
  - 旧版 (Superseded → v2): [`data-needs-v1`](RESEARCH/xiaodeng-ml-roadmap-data-needs-v1.md)
- [`RESEARCH/xiaodeng-ml-data-infra-v1.md`](RESEARCH/xiaodeng-ml-data-infra-v1.md) — ML data infra v1 (小邓)
- [`RESEARCH/data/`](RESEARCH/data/) — 数据快照 (网络 bench + Goalserve samples + Polymarket samples)

### E. 产品业务保障部 (老胡单元, 9 篇)

- [`RESEARCH/laohu-master-gantt-v1.md`](RESEARCH/laohu-master-gantt-v1.md) — 全局甘特图 v1 (老胡)
- [`RESEARCH/laohu-risk-registry-v1.md`](RESEARCH/laohu-risk-registry-v1.md) — 风险登记 v1 (老胡, v2 Sprint-2 W1 升)
- [`RESEARCH/xiaoying-acceptance-criteria-v1.md`](RESEARCH/xiaoying-acceptance-criteria-v1.md) — 验收 criteria v1 (小颖)
- [`RESEARCH/xiaodu-mvp-prd-v1.md`](RESEARCH/xiaodu-mvp-prd-v1.md) — MVP PRD v1 (小杜)
- [`RESEARCH/xiaosong-test-replay-framework-v0.1.md`](RESEARCH/xiaosong-test-replay-framework-v0.1.md) — Test + replay + chaos framework v0.1 (小宋)
- [`RESEARCH/xiaoyou-ux-framework-v1.md`](RESEARCH/xiaoyou-ux-framework-v1.md) — UX 体感评估 framework v1 (小尤)
- [`RESEARCH/xiaogong-dogfood-playbook-v1.md`](RESEARCH/xiaogong-dogfood-playbook-v1.md) — Dogfood 剧本 v1 (小宫)
- [`RESEARCH/xiaomi-docs-health-2026-05-28.md`](RESEARCH/xiaomi-docs-health-2026-05-28.md) — docs/ 健康度 v1 (小米)
- [`RESEARCH/xiaomi-docs-health-2026-05-28-v2.md`](RESEARCH/xiaomi-docs-health-2026-05-28-v2.md) — **docs/ 健康度 v2 (小米, 2026-05-28 Sprint-2 W2)** [新]

### F. 顾问团 (老雷直属, 顾问归档)

- [`RESEARCH/laohe-cpp-version-selection-v1.md`](RESEARCH/laohe-cpp-version-selection-v1.md) — C++ 标准选型 v1 (老何)
- [`RESEARCH/laohe-cpp-footgun-checklist-v1.md`](RESEARCH/laohe-cpp-footgun-checklist-v1.md) — C++ footgun checklist v1 (老何)
- [`RESEARCH/laogao-code-conventions-v1.md`](RESEARCH/laogao-code-conventions-v1.md) — Code conventions v1 (老高)
- [`RESEARCH/laoqian-mvp-scope-rejection-v1.md`](RESEARCH/laoqian-mvp-scope-rejection-v1.md) — MVP scope 拒绝清单 v1 (老钱, v1.1 Sprint-2 W3)
- [`RESEARCH/laoye-polygon-rpc-endpoint-matrix-v1.md`](RESEARCH/laoye-polygon-rpc-endpoint-matrix-v1.md) — Polygon RPC endpoint matrix v1 (老叶)
- [`RESEARCH/laoye-polygon-rpc-selection-v1.md`](RESEARCH/laoye-polygon-rpc-selection-v1.md) — Polygon RPC 选型 v1 (老叶, v1.1 Sprint-2 W1)
- [`RESEARCH/laoye-nonce-manager-design-v1.md`](RESEARCH/laoye-nonce-manager-design-v1.md) — Nonce manager v1 (老叶)
- [`RESEARCH/laoye-receiver-whitelist-v1.md`](RESEARCH/laoye-receiver-whitelist-v1.md) — Receiver whitelist v1 (老叶)
- [`RESEARCH/laoxu-external-tools-inventory-v1.md`](RESEARCH/laoxu-external-tools-inventory-v1.md) — 外部工具 + MCP 盘点 v1 (老徐)
- [`RESEARCH/xiaobai-llm-dev-conventions-v1.md`](RESEARCH/xiaobai-llm-dev-conventions-v1.md) — LLM 辅助开发规范 v1 (小白)

### G. 已废弃 (Rust 知识沉淀, DEPRECATED)

> GM 2026-05-28 决议: 全公司禁 Rust, 生产代码 100% C++20. 下列文档保留作历史知识沉淀, 不再 active.

- [`RESEARCH/laozhang-rust-engineering-stack-v1.md`](RESEARCH/laozhang-rust-engineering-stack-v1.md) — Rust 工程栈 v1 (老张, **DEPRECATED**, 改 C++)
- [`RESEARCH/laozhang-rust-signer-crates-v1.md`](RESEARCH/laozhang-rust-signer-crates-v1.md) — Rust signer crate 选型 v1 (老张, **DEPRECATED**, 改 老孙 v5-cpp)

## GOALSERVER

- [`GOALSERVER/`](GOALSERVER/) — Goalserve 官方 feed 文档 (basketball/baseball/hockey/soccer/tennis/ufc/esports + full package + inplay) — 小段维护

## INCIDENTS

- [`INCIDENTS/gm-self-mistakes-log.md`](INCIDENTS/gm-self-mistakes-log.md) — **GM 自承认错累计 log（首次启用 INCIDENTS 目录）** — 3 错 + 共性教训 + 后续机制
- 待立：首份技术 runbook 候选 — signer 崩溃 SOP（老孙 + 老吴 Sprint-2）

---

## 健康度

- 最近季度漂移报告 v2: [`RESEARCH/xiaomi-docs-health-2026-05-28-v2.md`](RESEARCH/xiaomi-docs-health-2026-05-28-v2.md)
- 评级: A- (从 Sprint-1 末 B+ 提升, 详见 v2 报告)

---

## 文档总数 (2026-05-28 v2 快照)

| 目录 | 文件数 |
|---|---|
| MEETINGS/ | 3 + sprint1-retro/ 15 份发言 = 18 |
| OKR/ | 1 |
| KPI/ | 1 |
| HIRING/ | 2 |
| SPRINTS/ | 3 (sprint-01 / sprint-01-final / sprint-02) |
| ADR/ | 13 |
| RESEARCH/ | 78 md + data/ 子目录 |
| INCIDENTS/ | 0 |
| GOALSERVER/ | 10 |
| 顶级 | 2 (INDEX + CONVENTIONS-naming) |
| **总计** | **128 md + data 快照** |

---

**Last updated:** 2026-05-28 by 小米 (v2, Sprint-2 W2 sweep)
