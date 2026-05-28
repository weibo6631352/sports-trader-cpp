# vCPU 拓扑 v0.7 update — Tier 2 bg thread 分配决议

- Owner: 老周 (cpp-chief-architect)
- Date: 2026-06-W3 (W6 Wave 30)
- Status: Accepted (老周 A 主管决议, ADR-015 阶段性兼容)
- 关联:
  - `docs/ADR/2026-06-01-adr-015-vcpu-pin.md` — paper 阶段不强 pin (GM Wave 27 投票 4C)
  - `docs/RESEARCH/laojiang-vcpu-pin-baseline-v1.md` — 5 thread 现状 + S2-011 计划
  - `docs/ADR/2026-06-01-adr-017-spsc-ring-framework.md` — SPSC 5 queue
  - `docs/RESEARCH/laowang-wal-framework-v0.2.md` — 4 WAL bg fsync thread
  - `docs/RESEARCH/laozhou-architecture-v0.6-e2e.md` — vCPU7 线程模型
- 回答小冯 hand-over: "Tier 2 WAL bg thread core 5 — OS scheduler 还是强 pin?"

---

## 1. v0.6 → v0.7 主要变更

| 项 | v0.6 (已撤回 paper 阶段) | v0.7 (本文, paper 现状) |
|---|---|---|
| paper 阶段线程数 | 误写 7 vCPU 拓扑 (过度设计, 老周已承认) | **6 thread 实际: main + 4 WAL bg + 1 IngestRaw bg** |
| WAL bg thread core 分配 | v0.6 预设 vCPU3-6 (强 pin 占位) | **OS scheduler** (paper 阶段不 pin) |
| IngestRaw bg thread | 未单独列出 | **新增 Tier 2 bg: 小冯 `IngestRaw` SPSC consumer** |
| 跨核 cache bouncing | 未评估 | 关键路径评估: 小石 SPSC rigtorp + 老王 group commit (见 §3) |
| M5+ pin 策略 | 7 vCPU 占位但无数据驱动 | **数据驱动: 老姜 S2-011 6/22 + bench_spsc_latency 驱动** |

---

## 2. 6 thread 现状 (v0.7 实际)

### 2.1 线程清单

| # | 线程名 | 类型 | 职责 | vCPU affinity | 备注 |
|---|---|---|---|---|---|
| T0 | main (decision) | **Tier 1 — 关键路径** | WSS recv → RM → Signer → Matcher → WALQueue push | 未 pin (OS 调度) | ADR-015 C 决议 |
| T1 | wal_bg_paper_audit | **Tier 2 — WAL bg** | PaperAudit WAL group commit fsync | 未 pin | 老王 WAL v0.2 |
| T2 | wal_bg_risk_audit | **Tier 2 — WAL bg** | RiskAudit WAL group commit fsync | 未 pin | 老王 WAL v0.2 |
| T3 | wal_bg_decision | **Tier 2 — WAL bg** | Decision WAL group commit fsync | 未 pin | 老王 WAL v0.2 |
| T4 | wal_bg_pit | **Tier 2 — WAL bg** | PIT WAL group commit fsync | 未 pin | 老王 WAL v0.2 |
| T5 | ingest_raw_bg | **Tier 2 — IngestRaw bg** | `SpscEventSink<WssEvent>` consumer → BookBuilder | 未 pin | 小冯 W6 接入 |

**注:** T5 是 ADR-017 `MarketDataBus` SPSC consumer 侧线程 (vCPU0→vCPU1 逻辑链路), W6 Wave 30 小冯接入后新增第 6 个线程。与 ADR-015 描述的"main + 4 WAL bg = 5 thread" 一致更新为 6。

### 2.2 小冯 hand-over 回答: Tier 2 WAL bg core 5 — OS scheduler

**决议: W6 ~ M5 前全部 OS scheduler, 不强 pin core 5 或任何具体核。**

依据:
1. ADR-015 C 方案 (GM + 老郭 Wave 27 投票) 已明确 paper 阶段不强 pin。
2. paper 阶段 T0 main p99 = 3.9us (小宋 fixture), 余量 12821×, 跨洋 RTT 200ms 是绝对瓶颈。强 pin Tier 2 对端到端 latency 无实质改善。
3. macOS dev 机无 `sched_setaffinity`, 强 pin 会导致 dev/CI 行为差异 (ADR-017 T6 GTEST_SKIP 已处理同类问题)。
4. T5 IngestRaw bg 是 SPSC **consumer** 侧, 只要 `try_pop` 非阻塞即满足 R-12; OS scheduler 不影响 R-12 合规。

**例外条件 (触发升级):** S2-011 6/22 实测若 p99 > 5ms 且归因为 Tier 2 bg 抢占 T0 main → 升级到仅 pin T0 main (2-3 vCPU 方案, 老姜 laojiang-vcpu-pin-baseline-v1.md §2.2)。

---

## 3. 跨核 cache bouncing 关键路径评估

### 3.1 SPSC ring 跨核 cache line 影响 (小石 rigtorp + 老王 group commit)

| SPSC Queue | 生产侧 | 消费侧 | Cache bouncing 路径 | paper 阶段影响 |
|---|---|---|---|---|
| MarketDataBus | T0 main (try_push) | T5 IngestRaw (try_pop) | head/tail cache line 跨 T0↔T5 | 低 — 无 pin, OS 可能同核调度 |
| WALQueue (per kind) | T0 main (group commit enqueue) | T1-T4 WAL bg (fsync dequeue) | head/tail 4 队列 × 跨 T0↔T1-T4 | 低 — 老王 WAL v0.2 batch=64 OR 1ms 聚合, bouncing 频率低 |
| RiskQueue | T0 main | T0 main (同线程) | 无跨核 bouncing (paper 阶段 inline) | 零 |

**结论:** rigtorp SPSC 内部 head/tail 已 cache-line 隔离 (padded_head/padded_tail, ADR-017 D3)。paper 阶段无 vCPU pin, OS 有概率将 T0/T5 调度到同物理核, cache bouncing 实际影响 < 估算上界。**无需在 M5 前专门处理。**

### 3.2 老王 group commit cache 路径

老王 WAL v0.2 `commit_batch=64 OR 1ms`: T0 push WALQueue → T1-T4 drain → fsync。关键路径中 WALQueue `try_push` 非阻塞 (R-12 严守), cache line bouncing 只影响 T1-T4 fsync latency, 不影响 T0 决策 p99。老姜 S2-011-E 压测会给出真实数据。

---

## 4. M5+ live 前 vCPU pin 路径

**触发条件:** 老姜 S2-011 6/22 EOD 数据 + bench_spsc_latency 真 SPSC 跨核 p99 数据。

**7 vCPU pin spec (老姜驱动, 老周 v0.7 §5 实施):**

| vCPU | 线程 | 理由 |
|---|---|---|
| vCPU0 | T0 main (decision) | 关键路径, 独占 |
| vCPU1 | T5 IngestRaw bg | MarketDataBus SPSC consumer, 独占降 cache bouncing |
| vCPU2 | Signal Engine (W7 小卢 SPSC 接入后) | SignalQueue consumer |
| vCPU3 | RiskGateway + PaperSigner + Matcher | RiskQueue consumer |
| vCPU4 | ML Hook | FillQueue ml_queue consumer |
| vCPU5-6 | T1-T4 WAL bg (2 核分享 4 thread) | fsync 不在关键路径, 2 核够用 |

**实施前提:** 老姜出 pin spec (基于 S2-011) → 老周 v0.7 §5 11 步启动期 → 老高 PR v1.3 → GM ack。

---

## 5. W7 起重测计划

| 任务 | Owner | 触发时机 |
|---|---|---|
| bench_spsc_latency 换真 SPSC (小石 ADR-017 接入) | 老姜 + 小石 | 小石 W6 SPSC framework 就位后 |
| 老王 group commit WALQueue 真接 bench | 老姜 + 老王 | 老王 WAL group commit W7 集成后 |
| 老唐 BLAKE3 pool 哈希热路径 bench | 老姜 + 老唐 | 老唐 BLAKE3 pool W7 接入后 |
| S2-011 (1k/10k/100k 笔 E2E) | 老姜 | 6/22 EOD (已计划) |
| bench_e2e 换真 WSS transport (小冯 beast 接入后) | 老姜 + 小冯 | boost.beast W7 接入后 |

**重测产出:** 更新 `laojiang-vcpu-pin-baseline-v1.md` → v1.1, 提供 M5+ 7 vCPU pin 数据依据。

---

**老周 (cpp-chief-architect):** v0.7 决议。小冯 Tier 2 IngestRaw bg OS scheduler, paper 阶段不强 pin。M5 前老姜数据驱动升级。

**last_review:** 2026-06-W3 by 老周
