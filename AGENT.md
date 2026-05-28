# Agent Roster — sports-trader-cpp

49 agent (39 类 + 10 Senior IC). 每个 agent 按 Claude Code 规范独立文件在 `.claude/agents/`.

调用方式: 用 Claude Code 的 sub-agent 调用机制, 用 agent name (kebab-case) 召唤.

详细职责见 `.claude/agents/NN-<name>.md`. AGENT.md 仅作索引 + 协作规约入口.

---

## 核心实施 C++ (12)
| # | name | file |
|---|---|---|
| 1 | cpp-chief-architect | [01](.claude/agents/01-cpp-chief-architect.md) |
| 2 | cpp-hot-path-engineer | [02](.claude/agents/02-cpp-hot-path-engineer.md) |
| 3 | cpp-network-engineer | [03](.claude/agents/03-cpp-network-engineer.md) |
| 4 | cpp-serialization-engineer | [04](.claude/agents/04-cpp-serialization-engineer.md) |
| 5 | cpp-persistence-engineer | [05](.claude/agents/05-cpp-persistence-engineer.md) |
| 6 | crypto-signing-expert | [06](.claude/agents/06-crypto-signing-expert.md) |
| 7 | polymarket-protocol-expert | [07](.claude/agents/07-polymarket-protocol-expert.md) |
| 8 | sports-market-expert | [08](.claude/agents/08-sports-market-expert.md) |
| 9 | risk-engineer | [09](.claude/agents/09-risk-engineer.md) |
| 10 | linux-sre-devops | [10](.claude/agents/10-linux-sre-devops.md) |
| 11 | observability-engineer | [11](.claude/agents/11-observability-engineer.md) |
| 12 | frontend-engineer | [12](.claude/agents/12-frontend-engineer.md) |

## 顾问 (5)
| 13 | rust-advisor | [13](.claude/agents/13-rust-advisor.md) |
| 14 | modern-cpp-advisor | [14](.claude/agents/14-modern-cpp-advisor.md) |
| 15 | cpo-product-strategy | [15](.claude/agents/15-cpo-product-strategy.md) |
| 16 | chief-architecture-reviewer | [16](.claude/agents/16-chief-architecture-reviewer.md) |
| 17 | code-quality-reviewer | [17](.claude/agents/17-code-quality-reviewer.md) |

## 金融 / 量化研究 (4)
| 18 | financial-expert | [18](.claude/agents/18-financial-expert.md) |
| 19 | quant-signal-research | [19](.claude/agents/19-quant-signal-research.md) |
| 20 | quant-backtest | [20](.claude/agents/20-quant-backtest.md) |
| 21 | quant-microstructure | [21](.claude/agents/21-quant-microstructure.md) |

## 数据分析 (3)
| 22 | data-etl | [22](.claude/agents/22-data-etl.md) |
| 23 | data-stats | [23](.claude/agents/23-data-stats.md) |
| 24 | data-warehouse | [24](.claude/agents/24-data-warehouse.md) |

## 业务保障 (5)
| 25 | requirements-analyst | [25](.claude/agents/25-requirements-analyst.md) |
| 26 | pm-project-manager | [26](.claude/agents/26-pm-project-manager.md) |
| 27 | security-engineer | [27](.claude/agents/27-security-engineer.md) |
| 28 | test-replay-engineer | [28](.claude/agents/28-test-replay-engineer.md) |
| 29 | compliance-legal | [29](.claude/agents/29-compliance-legal.md) |

## 跨域 (5)
| 30 | betting-industry-expert | [30](.claude/agents/30-betting-industry-expert.md) |
| 31 | ml-engineer | [31](.claude/agents/31-ml-engineer.md) |
| 32 | defi-onchain-advisor | [32](.claude/agents/32-defi-onchain-advisor.md) |
| 33 | ai-ops-collaboration | [33](.claude/agents/33-ai-ops-collaboration.md) |
| 34 | api-watch-general | [34](.claude/agents/34-api-watch-general.md) |

## IC 池 + 产品 + 调研 + 审计 + 性能 (5)
| 35 | senior-cpp-ic-pool | [35](.claude/agents/35-senior-cpp-ic-pool.md) |
| 36 | product-manager | [36](.claude/agents/36-product-manager.md) |
| 37 | goalserve-api-watch | [37](.claude/agents/37-goalserve-api-watch.md) |
| 38 | audit-expert | [38](.claude/agents/38-audit-expert.md) |
| 39 | performance-engineer | [39](.claude/agents/39-performance-engineer.md) |

---

## 协作规约 (v0, Meeting γ 已 propose v1 见 `docs/meeting-gamma-process.md`)

- **RACI 决策权矩阵**: 见 meeting-gamma-process.md 节 H
- **召唤 trigger**: P0 路径改动 24h SLA review / 安全合规相关超时 block 而非默认通过
- **冲突仲裁**: 2 agent 分歧 → 第 3 agent → CPO+PM → 用户
- **会议节奏**: 异步周报 + 事件触发 + milestone 评审
- **红线**: 必读 `docs/LESSONS_FROM_PYTHON.md` (拒绝继承的 Python 反模式)

