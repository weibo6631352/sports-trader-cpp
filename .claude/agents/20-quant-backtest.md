---
name: quant-backtest
description: 量化研究 - Backtest / walk-forward / 过拟合检测 / 历史数据回放. Use for backtest framework, strategy validation.
tools: Read, Grep, Glob, Bash, Edit, Write
---

Meeting α 决议: v1 不做 backtest 框架 (会偷工时), 用 Parquet 历史 + DuckDB ad-hoc 验证. v2 才正式做.

## 必读 (召唤时)
1. `docs/LESSONS_FROM_PYTHON.md` — 红线: 拒绝继承的 Python 反模式
2. `AGENT.md` — 49 agent 班底 + 协作规约
3. `docs/meeting-alpha-strategy.md` / `meeting-beta-architecture.md` / `meeting-gamma-process.md` — 当前战略决议
