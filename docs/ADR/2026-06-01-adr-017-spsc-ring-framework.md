---
owner: 小石 (data-structures-expert, #41)
last_review: 2026-06-01
status: accepted
wave: W6 Wave 28
related:
  - docs/RESEARCH/xiaoshi-data-structures-selection-v1.md
  - docs/RESEARCH/laozhou-architecture-v0.6-e2e.md §4
  - docs/MEETINGS/2026-06-01-input-laozhou-arch-deviation.md §4 (P0 偏离 #4)
  - ADR/2026-05-28-gm-redline-websocket-non-blocking.md (R-12)
reviewers: 老周 (first review) → 老高 (PR v1.2) → GM ack
---

# ADR-017 — SPSC/MPMC Ring Framework

> **GM 重命名 (commit 时)**: 原小石命名 "ADR-009 v2" 与 W5 已立 ADR-009 (Agent Model 分级) 冲突. ADR 编号 cascade: ADR-009 = Model 分级 (W5), ADR-010~016 已用 (test grading / arch challenge / G2 阈值), 本 ADR = ADR-017.

## 背景

老周 W5 末偏离审查 (2026-06-01-input-laozhou-arch-deviation.md §4) 确认:

> **偏离 #4 P0**: 5 个 SPSC ring 全未落地, 当前用 in-memory `std::deque` + 同线程 push/pop.
> 集成 test e2e 3.9us 是 mock 数据, 不代表真实跨核延迟.

W6 Wave 28 本 ADR 记录 SPSC/MPMC ring framework v0.1 选型决策与实施.

## 决策

### D1: 采用 rigtorp/SPSCQueue + 双 SPSC fanout (FillQueue)

**不用单体 MPMC 实现 FillQueue.**

理由:
- FillQueue 是 1-producer (vCPU3 VirtualMatcher) + 2-consumer (Position + ML), 实质 SPMC
- 真 SPMC 最优解 = 每 consumer 一条独立 SPSC + producer fanout copy
- Position 永不丢 / ML 可丢 的语义在双 SPSC 设计里自然分离, 单 MPMC 无法表达这个差异
- rigtorp::MPMCQueue 2-consumer 竞争 CAS: p99 ~25-60ns vs SPSC p99 ~8ns, 3-7x 差距
- 双 SPSC fanout copy 成本: sizeof(VirtualFill) ≈ 96B × 2 = 192B/fill, 可接受

### D2: Capacity 编译期 2^n enforce

所有 capacity 用 `consteval enforce_power_of_two<N>()` 编译期死亡, 不允许运行时绕过.

### D3: cache-line 隔离策略

- SpscQueue 内部: rigtorp 自带 head/tail cache-line 隔离 (padded_head/padded_tail)
- drop_count_ 放在独立 `alignas(CACHE_LINE_SIZE)` cache line
- FillQueue ml_queue_ 不加额外 alignas (rigtorp SPSCQueue 自然对齐 > CACHE_LINE_SIZE)
- 平台: Apple Silicon 128B, x86 64B, 通过 `CACHE_LINE_SIZE` 常量统一

### D4: macOS vs Linux 行为差异

- macOS 无 sched_setaffinity, T6 latency bench 结果含 OS jitter
- dev 机 p99 阈值放宽到 100ns (生产 pinned 预期 < 10ns)
- T6 测试用 `GTEST_SKIP()` 而非 `EXPECT_*` 失败, 避免 CI 在 dev 机上误报

## 5 个 Queue 配置汇总

| Queue | 类型 | Capacity | 生产 → 消费 | Back-pressure |
|---|---|---|---|---|
| MarketDataBus | SpscQueue | 65536 | vCPU0 → vCPU1 | drop + mdb_drop_total (R-12) |
| SignalQueue | SpscQueue | 8192 | vCPU1 → vCPU2 | drop + signal_drop_total |
| RiskQueue | SpscQueue | 4096 | vCPU2 → vCPU3 | REJECT(SYSTEM_BACKPRESSURE) |
| FillQueue | FillQueue (双 SPSC) | 8192 | vCPU3 → {Position, ML} | Position: P0; ML: drop |
| WALQueue (per kind) | SpscQueue | 65536 | multi → vCPU3 group commit | P0 (永不丢) |

## 实施文件

```
include/stcpp/infra/spsc/
  queue_capacities.hpp   — 5 capacity constexpr + 2^n enforce_power_of_two
  spsc_queue.hpp         — SpscQueue<T,N> + SpscEventSink<T,N>
  mpmc_queue.hpp         — FillQueue<T,N> + RigtorpMpmcQueue<T,N> (备用)

src/stcpp/infra/spsc/
  CMakeLists.txt         — INTERFACE target stcpp_infra_spsc
                           FetchContent rigtorp/SPSCQueue v1.1
                           FetchContent rigtorp/MPMCQueue master

tests/unit/
  test_spsc_queue.cpp    — 7 test cases (T1-T6 + Bonus)
```

## 测试结果 (W6 Wave 28, macOS Apple Silicon M-series, no vCPU pin)

| Test | 结果 |
|---|---|
| T1 单线程 FIFO | PASSED |
| T2 跨线程 1000 笔不丢 | PASSED |
| T3 满 → false + drop_count | PASSED |
| T4 2^n + cache-line align | PASSED |
| T5 FillQueue 双 SPSC 2-consumer | PASSED |
| T6 latency p99 | p50=42ns p99=42ns max=1333ns (< 100ns 阈值) |
| Bonus drop_count 累加 | PASSED |

T6 p99=42ns on Apple Silicon dev 机 (无 vCPU pin). 生产 Linux + pinned vCPU 预期 p99 < 10ns.
W6 落地后老姜 bench: 跨核 SPSC + vCPU pin 联合 benchmark, 更新真实 p99 数据.

## R-12 back-pressure 合规声明

- `SpscQueue::try_push` 非阻塞, 满时 return false + drop_count++, 不阻塞调用方线程
- `FillQueue::try_push` 非阻塞 fanout, Position 满时 position_ok=false (调用方 P0), ML 满时 ml_drop++
- 任何 consumer 线程消费慢不影响 producer vCPU (R-12 严守)

## 后续接入 (W6 + W7)

- MarketDataBus: 小冯 PMWssSubscriber → `SpscEventSink<WssEvent, 65536>` 实现 ISpscEventSink (W6)
- SignalQueue: 小卢 Signal engine → RiskGateway (W6)
- RiskQueue: 老韩 RiskGateway → 老蒋 PaperSigner (W6)
- FillQueue: 老蒋 VirtualMatcher → Position + 小邓 ML hook (W6)
- WALQueue: 老王 WalWriter group commit (W6)

— 小石 (data-structures-expert, #41), 2026-06-01
