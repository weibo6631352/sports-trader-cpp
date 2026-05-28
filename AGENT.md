# Agent Roster — sports-trader-cpp

**55 agent** (45 类 + 10 Senior IC). 每 agent 独立 `.claude/agents/NN-<name>.md` 含:
- frontmatter (name / description / tools)
- persona 名 (中文姓, 沟通自然)
- 项目背景 / 专业领域 / 召唤场景 / 协作边界 / 输出格式 / 拒绝任务

调用方式: Claude Code sub-agent 用 `name` (kebab-case) 召唤.

| # | name | persona | 类别 |
|---|---|---|---|
| 1 | cpp-chief-architect | 老周 | 核心实施 |
| 2 | cpp-hot-path-engineer | 小马 | 核心实施 |
| 3 | cpp-network-engineer | 老陈 | 核心实施 |
| 4 | cpp-serialization-engineer | 小赵 | 核心实施 |
| 5 | cpp-persistence-engineer | 老王 | 核心实施 |
| 6 | crypto-signing-expert | 老孙 | 核心实施 |
| 7 | polymarket-protocol-expert | 老李 | 核心实施 |
| 8 | sports-market-expert | 小田 | 核心实施 |
| 9 | risk-engineer | 老韩 | 核心实施 |
| 10 | linux-sre-devops | 老吴 | 核心实施 |
| 11 | observability-engineer | 小郑 | 核心实施 |
| 12 | frontend-engineer | 小苏 | 核心实施 |
| 13 | rust-advisor | 老张 | 顾问 |
| 14 | modern-cpp-advisor | 老何 | 顾问 |
| 15 | cpo-product-strategy | 老钱 | 顾问 |
| 16 | chief-architecture-reviewer | 老郭 | 顾问 |
| 17 | code-quality-reviewer | 老高 | 顾问 |
| 18 | financial-expert | 小梁 | 金融量化 |
| 19 | quant-signal-research | 小程 | 金融量化 |
| 20 | quant-backtest | 小蒋 | 金融量化 |
| 21 | quant-microstructure | 小袁 | 金融量化 |
| 22 | data-etl | 小余 | 数据 |
| 23 | data-stats | 小董 | 数据 |
| 24 | data-warehouse | 小田 | 数据 |
| 25 | requirements-analyst | 小颖 | 业务保障 |
| 26 | pm-project-manager | 老胡 | 业务保障 |
| 27 | security-engineer | 老沈 | 业务保障 |
| 28 | test-replay-engineer | 小宋 | 业务保障 |
| 29 | compliance-legal | 老黄 | 业务保障 |
| 30 | betting-industry-expert | 老彭 | 跨域 |
| 31 | ml-engineer | 小邓 | 跨域 |
| 32 | defi-onchain-advisor | 老叶 | 跨域 |
| 33 | ai-ops-collaboration | 老徐 | 跨域 |
| 34 | api-watch-general | 小冯 | 跨域 |
| 35 | senior-cpp-ic-pool | 小卢 (× 10) | IC 池 |
| 36 | product-manager | 小杜 | 产品 |
| 37 | goalserve-api-watch | 小段 | 调研 |
| 38 | audit-expert | 老唐 | 审计 |
| 39 | performance-engineer | 老姜 | 性能 |
| 40 | doc-curator | 小米 | 文档 |
| 41 | data-structures-expert | 小石 | 数据结构 |
| 42 | senior-algorithm-engineer-a | 小肖 | 算法 (数值) |
| 43 | senior-algorithm-engineer-b | 小颜 | 算法 (状态机/图) |
| 44 | ai-llm-advisor | 小白 | AI / LLM |
| 45 | professional-manager | 老雷 | GM |

