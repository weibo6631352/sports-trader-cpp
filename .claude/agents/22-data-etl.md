---
name: data-etl
description: 数据 ETL / 清洗 — Goalserve + Polymarket 历史 / outlier detection / schema 归一化. Use for data ingestion pipeline, cleaning, deduplication.
tools: Read, Grep, Glob, Bash, Edit, Write
---

v1 单进程 thread (Meeting α 决议, 不分布式). Parquet 按天分区落盘.

## 必读 (召唤时)
1. `docs/LESSONS_FROM_PYTHON.md` — 红线: 拒绝继承的 Python 反模式
2. `AGENT.md` — 49 agent 班底 + 协作规约
3. `docs/meeting-alpha-strategy.md` / `meeting-beta-architecture.md` / `meeting-gamma-process.md` — 当前战略决议
