# Agent Roster — sports-trader-cpp

> 班底导航 + 战斗单元归属 + 汇报关系。维护人：老雷 + 小林。
> 详细 persona 见 `.claude/agents/NN-<name>.md`。

**当前规模：** 58 agent（45 类 + 10 IC Pool + 1 HR + 1 UX + 1 Dogfood）

---

## 战斗单元归属

### A. 系统工程部 — Owner: 老周

| # | name | persona | 职责一句话 |
|---|---|---|---|
| 1 | cpp-chief-architect | 老周 | 架构主权，模块边界 |
| 2 | cpp-hot-path-engineer | 小马 | 热路径 us 级延迟 |
| 3 | cpp-network-engineer | 老陈 | WSS / 跨洋连接 |
| 4 | cpp-serialization-engineer | 小赵 | simdjson / glaze |
| 5 | cpp-persistence-engineer | 老王 | 落库 / WAL |
| 6 | crypto-signing-expert | 老孙 | EIP-712 / 私钥 |
| 7 | polymarket-protocol-expert | 老李 | CLOB 协议 / 状态机 |
| 8 | sports-market-expert | 小田 | 盘口规则 / 结算 |
| 10 | linux-sre-devops | 老吴 | 部署 / CI/CD |
| 11 | observability-engineer | 小郑 | Prometheus / Grafana |
| 39 | performance-engineer | 老姜 | profile / benchmark |
| 41 | data-structures-expert | 小石 | lock-free / ring buffer |
| 42 | senior-algorithm-engineer-a | 小肖 | 数值 / Kelly / 定价 |
| 43 | senior-algorithm-engineer-b | 小颜 | 图 / 状态机 / 调度 |
| 35 | senior-cpp-ic-pool | 小卢×10 | 通用 C++/Rust IC |

### B. 风控合规部 — Owner: 老韩

| # | name | persona | 职责 |
|---|---|---|---|
| 9 | risk-engineer | 老韩 | RiskManager 主权 |
| 27 | security-engineer | 老沈 | 密钥 / 供应链安全 |
| 29 | compliance-legal | 老黄 | ToS / KYC / 监管 |
| 38 | audit-expert | 老唐 | 风控事件审计 |

### C. 量化研究部 — Owner: 小梁

| # | name | persona | 职责 |
|---|---|---|---|
| 18 | financial-expert | 小梁 | Sharpe / VaR / Kelly |
| 19 | quant-signal-research | 小程 | α 信号挖掘 |
| 20 | quant-backtest | 小蒋 | walk-forward 回测 |
| 21 | quant-microstructure | 小袁 | orderbook / slippage |
| 30 | betting-industry-expert | 老彭 | sharp money / line movement |

### D. 数据基础设施部 — Owner: 小余

| # | name | persona | 职责 |
|---|---|---|---|
| 22 | data-etl | 小余 | ETL / outlier / 归一化 |
| 23 | data-stats | 小董 | A/B test / Bayesian |
| 24 | data-warehouse | 小田 | Parquet / DuckDB |
| 37 | goalserve-api-watch | 小段 | Goalserve full feed 探索 |
| 34 | api-watch-general | 小冯 | Polymarket / 竞品 API |

### E. 产品业务保障部 — Owner: 老胡

| # | name | persona | 职责 |
|---|---|---|---|
| 26 | pm-project-manager | 老胡 | milestone / risk / 周报 |
| 25 | requirements-analyst | 小颖 | spec 拆解 / 验收 |
| 36 | product-manager | 小杜 | PRD / 未来规划 |
| 28 | test-replay-engineer | 小宋 | unit + sim + chaos |
| 12 | frontend-engineer | 小苏 | operator UI |
| 40 | doc-curator | 小米 | docs/ 守护 / SSOT |
| **46** | **hr-talent-manager** | **小林** | **招聘 / JD / onboarding** |
| **47** | **ux-experience-evaluator** | **小尤** | **UX 体感 / 心流 / 可读性** |
| **48** | **dogfood-tester** | **小宫** | **持续 dogfood / 全流程跑通** |

### F. 顾问团 — 老雷直属

| # | name | persona | 职责 |
|---|---|---|---|
| 13 | rust-advisor | 老张 | Rust / FFI / crate |
| 14 | modern-cpp-advisor | 老何 | C++20/23 best practice |
| 15 | cpo-product-strategy | 老钱 | 北极星 / scope |
| 16 | chief-architecture-reviewer | 老郭 | 第二意见 / 仲裁 |
| 17 | code-quality-reviewer | 老高 | PR review / 红线 |
| 31 | ml-engineer | 小邓 | online learning v2 |
| 32 | defi-onchain-advisor | 老叶 | Polygon / gas / bridge |
| 33 | ai-ops-collaboration | 老徐 | agent 班底 / RACI |
| 44 | ai-llm-advisor | 小白 | prompt / RAG / workflow |

### G. GM

| # | name | persona | 职责 |
|---|---|---|---|
| 45 | professional-manager | 老雷 | GM / 资源协调 / 事故指挥 |

---

## 汇报关系

```
老雷 (GM)
├── 老钱 (CPO, 平级)
├── 老郭 (架构评审, 跨单元一票否决)
├── 小林 (HR, 直属)
├── A. 老周 → 系统工程部 (15 人)
├── B. 老韩 → 风控合规部 (4 人)
├── C. 小梁 → 量化研究部 (5 人)
├── D. 小余 → 数据基础设施部 (5 人)
├── E. 老胡 → 产品业务保障部 (8 人, 含小林虚线)
└── F. 顾问团 (9 人)
```

---

## 扩招 Backlog（详见 `docs/HIRING/backlog.md`）

| 优先级 | 季度 | 岗位 | Persona |
|---|---|---|---|
| P1 | Q2 2026 | onchain-ops-engineer | 老冀 |
| P1 | Q2 2026 | strategy-execution-engineer | 小秦 |
| P1 | Q3 2026 | quant-engineer | 小吕 |
| P1 | Q3 2026 | risk-quant-engineer | (待命名) |
| P1 | Q3 2026 | data-engineer | (待命名) |
| P1 | Q3 2026 | devops-infra-engineer | (待命名) |
| P2 | Q3 2026 | market-data-qa-engineer | 小方 |
| P2 | Q4 2026 | data-scientist | (待命名) |
| P2 | Q4 2026 | sports-data-analyst | (待命名) |
| P2 | Q4 2026 | platform-sre | (待命名) |
| P2 | Q4 2026 | product-ops-specialist | (待命名) |
| P2 | Q4 2026 | cpp-network-engineer (扩编) | (待命名) |
| P2 | Q4 2026 | bi-analyst | 小范 |
| P3 | Q1 2027 | compliance-analyst | (待命名) |
| P3 | Q1 2027 | partner-liaison | (待命名) |

---

**最后更新：** 2026-05-28 by 老雷 + 小林
