# sports-trader-cpp

Polymarket 体育市场量化交易系统 — C++ ground-up.

## 状态（2026-05-28 Sprint-2 W1 末）

- **班底就位 57 agent persona / 48 file**（44 类 + 1 GM 老雷 + 10 IC pool + 小林 HR + 小尤 UX + 小宫 dogfood）
- **Sprint-1 完成**：26 任务 + 16 真实发言 retro + 18 GM 决议 + 20+ 红线 + 78 RESEARCH + 13 ADR
- **当前阶段**：Sprint-2 W2，paper trading skeleton 落代码中
- **MVP 路线**：M1 (T+6 周) 数据接入 → M5 (T+24 周) 实盘首笔
- **M4.5 gate**：paper 连续 2 周通过 7 hard gate 才解锁实盘（不通过严禁切真钱）
- **上链 deferred / AWS deferred** — 全本地开发跑 paper，盈利证明后再上基础设施

## 公司价值观

1. 实盘优先（Production First）
2. 纪律高于收益（Discipline > PnL）
3. 数字说话（Data-Driven）
4. 不耻下问（Open Communication，公开失败）

## 快速入口

- [`CLAUDE.md`](CLAUDE.md) — **公司运营手册**（红线 / 单元 / 协作规范）
- [`AGENT.md`](AGENT.md) — agent 班底索引（含 persona 名）
- [`docs/INDEX.md`](docs/INDEX.md) — 全部文档 SSOT 入口
- [`docs/SPRINTS/sprint-03-backlog.md`](docs/SPRINTS/sprint-03-backlog.md) — 当前 Sprint backlog
- [`docs/OKR/2026-Q2-Q3-startup-season.md`](docs/OKR/2026-Q2-Q3-startup-season.md) — 起步季 OKR
- `.claude/agents/*.md` — 每个 agent 完整文档（Claude Code sub-agent 规范）
- `.env.example` — 配置 schema（实际 `.env` 不入 git）
</content>
</invoke>