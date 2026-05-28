---
owner: 老周 (cpp-chief-architect, A 主管, ADR-009 = Opus)
last_review: 2026-06-01
scope: W5 末 Wave 26 代码 review 大会 — 架构 v0.6 vs 实际 12000 行 cpp 偏离审查
classification: input
status: final
related:
  - docs/ADR/2026-05-28-v0.6-end-to-end.md (v0.6 架构, W4 老周)
  - docs/MEETINGS/2026-06-01-input-laoguo-F-status.md (老郭 集成)
  - docs/MEETINGS/2026-06-01-manager-sync-w5-v1.md (W5 manager sync)
---

# 老周 W5 末 架构 v0.6 偏离审查 report

> **核心结论:** v0.6 架构 vs 实际代码 **5 偏离**, 其中 P0 = 1 项 (SPSC 拓扑 W5 全未落地), P1 = 3 项, P2 = 1 项. W6 派单优先级 P0 三项. v0.7 增量加 §8 启动期 11 步序列 (含 W5 新加 SingleInstanceLock).
>
> **重要 caveat:** 集成 test e2e p99 3.9us **不代表生产链路真实延迟** — 当前全 in-memory + 单线程 + mock sink, 没有 SPSC ring + 没有 vCPU pin + 没有跨洋 WSS + 没有真 WAL fsync. W6 落 SPSC + pin + 真 IO 后预计 10-100x 上涨, 但仍距 M1 G2 (1000 笔 p99 < 80ms) 余 100x+ 安全垫.

---

## Part 1: v0.6 §1 端到端链路图 vs 实际

### v0.6 设计链路

```
WSS Polymarket → MarketDataBus → Signal Engine → P0-01 → FeatureSnapshot
                                                            |
                                            +---------------+
                                            |               |
                                            v               v
                                       ML Hook         RiskGateway
                                       |               | Allowed
                                       v               v
                                shadow_audit.wal   paper_audit.wal
                                                       |
                                                       v
                                                  PaperSigner
                                                       |
                                                       v
                                                  VirtualMatcher
                                                       |
                                                       v
                                              Position Ledger (paper_position.wal)
                                                       |
                                                       v
                                              SettlementWatcher
                                                       |
                                                       v
                                              TrainingLabel (shadow_audit.wal)
                                                       |
                                                       v
                                              M4.5 Gate Evaluator (小董)
```

### 实际 W5 末模块对照

| 模块 | Owner | W5 末状态 | 备注 |
|---|---|---|---|
| WSS Polymarket | 小冯 | v0.1 ✓ | 8 sub topic, exp backoff 自愈 |
| MarketDataBus | 小石 + 小冯 | 接口 ✓, 真接 W6 | ISpscEventSink 留口子 |
| Signal Engine | 小卢 | P0-01 ✓ | 25 测试 + LiveSection |
| FeatureSnapshot | 小邓 | v0.1 ✓ | ML hook 32 feature |
| RiskGateway | 老韩 + 老沈 | v0.3.1 + ADR-004 patch ✓ | 39 测试 |
| AuditEmitter | 老唐 | v0.1 ✓ | 24 测试 |
| PaperSigner | 小蒋 | v0.1 ✓ | 27 测试 |
| VirtualMatcher | 小蒋 | v0.1 ✓ | Bernoulli sampler |
| Position Ledger | — | **未实施** | W6 派单 |
| SettlementWatcher | — | **未实施** | W6 派单, 与小段 Goalserve game_ended 接 |
| M4.5 Gate | 小董 | v1 ✓ | 28 测试 |

### 偏离 1: Position Ledger + SettlementWatcher 未实施

**严重度:** P1

**影响:** M4.5 gate 当前用 synthetic label, 真 settlement 路径 W5 不存在. paper engine 跑得起来 (VirtualMatcher 直接出 fill), 但 PnL 没有"成交→持仓→结算→label"闭环.

**W6 派单建议:**
- Position Ledger owner 待 GM 拍 (小蒋 + 老唐 候选, 与 paper_position.wal physical isolation 接)
- SettlementWatcher owner 待 GM 拍 (与小段 Goalserve game_ended event 接, polling 周期 30s 起)

---

## Part 2: v0.6 §2 vCPU 7 核拓扑 vs 实际

### v0.6 设计拓扑 (跨洋 c6a.2xlarge, 8 vCPU 留 1 给 OS)

| vCPU | 职责 |
|---|---|
| vCPU0 | WSS Ingest (Polymarket + Goalserve) |
| vCPU1 | Signal Engine (P0-01 + LiveSection) |
| vCPU2 | RiskGateway + AuditEmitter |
| vCPU3 | PaperSigner + VirtualMatcher + WAL fsync coordinator |
| vCPU4 | ML Hook (异步 shadow) |
| vCPU5 | SettlementWatcher + M4.5 Gate (低频) |
| vCPU6 | Reserved (M2 真签 + REST poll) |
| vCPU7 | OS / kernel |

### 实际 W5 末

**全部未实施.** paper engine 单进程 + 全 main thread + 全 in-memory queue (`std::deque`). 集成 test 跑 e2e p99 3.9us 是因为没线程切换 + 没 cache miss + 没 fsync.

### 偏离 2: vCPU 拓扑 + pin W5 未启用

**严重度:** P1

**影响:** 当前 e2e 3.9us **不能外推到生产**. 跨洋 WSS RTT 一上来 + SPSC ring 跨核 cache line bounce + WAL fsync 一上来, 预计涨 10-100x (到 40us-400us). 仍距 M1 G2 80ms 余 200x+ 安全垫, 不构成 M1 blocker, 但**对外汇报必须明确 caveat**.

**W6 派单建议:**
- 老姜 W6-A-XX: vCPU pin (pthread_setaffinity_np) + isolcpus + nohz_full kernel param 调优
- 与小石 SPSC 拓扑联调 (rigtorp SPSCQueue 跨 vCPU 性能基准)

---

## Part 3: v0.6 §3 6 core struct vs 实际

| Struct | v0.6 设计 | W5 实际 |
|---|---|---|
| TimestampQuad | 4 ts (event/data_source/ingestion/as_of) R-20 enforce | ✓ 全模块用 |
| OrderIntent | RiskGateway.evaluate(OrderIntent) 入参 | ✓ 老韩 v0.3.1 |
| RiskDecision | RM 出, 12 RejectCode | ✓ 4 类落地, 老沈 W5-T8 余 8 类 |
| SignalOutput | P0-01 出 | ✓ 小卢 25 测试 |
| VirtualFill | Matcher 出 | ✓ 小蒋 |
| FeatureSnapshot | ML hook 入 | ✓ 小邓 32 feature |
| MLSignalCandidate | ML 出 → Signal Engine 候选 | **未实施** (paper 跑通后再接) |

### 偏离 3: MLSignalCandidate 未实施

**严重度:** P2

**影响:** ML hook 当前只 capture FeatureSnapshot 写 shadow_audit.wal, 不出 signal candidate. paper engine 跑的是 rule-based P0-01 signal, ML 不进决策环. M4.5 gate 用 shadow label 评估 ML 准度, 评估通过后再升 candidate.

**W6/W7 派单建议:** W7+ 小邓 + 小卢 联调, 与 M4.5 gate 准入门槛绑.

---

## Part 4: v0.6 §4 5 SPSC 拓扑 vs 实际

### v0.6 设计 (rigtorp/SPSCQueue)

| Queue | Producer | Consumer | Capacity | 备注 |
|---|---|---|---|---|
| MarketDataBus | WSS Ingest | Signal Engine | 65536 | 高吞吐 |
| SignalQueue | Signal Engine | RiskGateway | 8192 | |
| RiskQueue | RiskGateway | PaperSigner | 4096 | Allowed 只 |
| FillQueue | VirtualMatcher | {Position Ledger, AuditEmitter} | 8192 | MPMC 2-consumer |
| WALQueue (per kind) | 多 producer | WAL writer thread | 65536 | group commit batch 64 OR 1ms |

### 实际 W5 末

**全部未实施.** 当前全 in-memory `std::deque` + 同线程 push/pop. 集成 test 用 capturing sink, 不是真 SPSC ring.

### 偏离 4: SPSC 拓扑 5 queue 全未落地 (P0)

**严重度:** **P0** — 当前 e2e 3.9us 不代表 SPSC ring 真延迟

**影响:**
- 当前测试是 in-memory mock + capturing sink, 没有跨核 cache line bounce
- 没有 group commit fsync 真路径
- 没有 ring full backpressure 路径
- 没有 producer/consumer 不同 vCPU 的真实场景

**W6 派单建议 (P0):**
- 小石 W6-A-03: rigtorp SPSCQueue 集成, 5 queue 全落
- 老姜 W6 配套: SPSC + vCPU pin 联合 benchmark
- W6 EOW 出真 e2e p99 (含跨核 + fsync), 与当前 3.9us 对比, 给老郭集成 update

---

## Part 5: v0.6 §5 M1 10 hard gate vs 实际

| Gate | 标准 | W5 末 | 备注 |
|---|---|---|---|
| G1 | 单笔决策 < 50ms | ✓ 3.9us (余 12800x) | caveat: in-memory mock |
| G2 | 1000 笔 p99 < 80ms | ⏸ | W6 老姜 benchmark, 跨核 + fsync |
| G3 | 4 wal 物理隔离 | ✓ | r11_pollution test 守门 |
| G4 | 0 R-12 违例 | ✓ | event loop p99 3.9us 远 < 100us |
| G5 | 4 ts 全链路 enforce | ✓ | R-20 全模块 |
| G6 | M4.5 跑通 | ⏸ | W6 小董 接 paper data |
| G7 | WSS 自愈 | ✓ | 小冯 exp backoff |
| G8 | 12 RejectCode 覆盖 | ⏸ 4/12 | 老沈 W5-T8 余 8 类 |
| G9 | paper 不污染真账本 | ✓ | T2 + r11_pollution test |
| G10 | W5 demo e2e smoke | ⏸ | W5 末 EOD (老李 + 老吴) |

### 偏离 5: G2 / G6 / G8 / G10 W5 余项

**严重度:** P1 (M1 验收前必须收口)

**W6 派单建议:**
- G2 → 老姜 benchmark (SPSC + pin 后真 e2e)
- G6 → 小董 接 paper_audit.wal 真数据
- G8 → 老沈 余 8 类 RejectCode
- G10 → 老李 + 老吴 W5 EOD demo smoke (本周内补)

---

## Part 6: 老板防多开 (W5 新加) 与 v0.6 兼容

W5 新加 W5-A-09 SingleInstanceLock (小赵 owner):
- flock-based PID lock 加在 `paper.cpp` main() 第一行
- 同机器同时只能跑一份 paper engine
- 与 v0.6 启动期 vCPU pin 不冲突 (启动期一次性, 不在 hot path)

**v0.6 → v0.7 必须把这一步固化进启动期序列**, 否则后人接手不知道为啥 paper engine main 第一行要 flock.

---

## Part 7: v0.7 启动期 11 步序列 (本 review 触发增量)

W6 我把 v0.6 升 v0.7, 加 §8 启动期序列:

| Step | 动作 | Owner | 备注 |
|---|---|---|---|
| 1 | SingleInstanceLock acquire | 小赵 | flock PID lock, R-7 + 防多开 |
| 2 | ExecutionContext::Init | 小马 | R-7 build-time mode (paper/live) |
| 3 | WAL writers 初始化 (4 wal) | 老唐 | R-11 物理隔离 4 dir |
| 4 | vCPU pin (7 核拓扑) | 老姜 | W6+ 落代码 |
| 5 | SPSC rings 初始化 (5 queue) | 小石 | W6 P0 |
| 6 | RM 初始化 + AuditEmitter wire | 老韩 + 老唐 | 12 RejectCode 注册 |
| 7 | Signal Engine 初始化 | 小卢 | P0-01 + LiveSection |
| 8 | PaperSigner + VirtualMatcher 初始化 | 小蒋 | Bernoulli sampler seed |
| 9 | ML Hook 初始化 | 小邓 | FeatureSnapshot capture only |
| 10 | WSS subscriber 启动 | 小冯 | Polymarket 8 sub + Goalserve |
| 11 | Main loop 进入 | — | event-driven |

**关键失败处理:**
- Step 1 失败 → exit code 11 (已有 instance)
- Step 3 失败 → exit code 22 (WAL 目录权限错)
- Step 4 失败 → warn + 降级 (无 pin 也能跑, 性能差)
- Step 10 失败 → exit code 30 (WSS 初连失败)

---

## Part 8: 架构 vs 代码 5 偏离总结表

| # | 偏离 | 严重度 | W6 修复? | Owner |
|---|---|---|---|---|
| 1 | Position Ledger + SettlementWatcher | P1 | W6 派单 | 待 GM 拍 |
| 2 | vCPU 7 核拓扑 + pin | P1 | W6 派单 | 老姜 |
| 3 | MLSignalCandidate 未接 | P2 | W7+ | 小邓 + 小卢 |
| 4 | SPSC 5 queue 拓扑 | **P0** | W6 P0 | 小石 |
| 5 | M1 G2/G6/G8/G10 余项 | P1 | W6 | 老姜/小董/老沈/老李+老吴 |

---

## Part 9: 决议建议 (给老郭集成)

### W6 P0 派单 3 项

1. **小石 W6-A-03 SPSC 拓扑落代码** (rigtorp/SPSCQueue, 5 queue 全落) — P0
2. **老姜 W6-A-XX vCPU pin** (pthread_setaffinity_np + isolcpus 调优) — P0
3. **Position Ledger + SettlementWatcher** (owner 待 GM 拍, 候选小蒋 + 老唐 + 小段) — P0

### W6 P1 派单 4 项

4. 老姜 G2 e2e benchmark (SPSC + pin 后真 1000 笔 p99)
5. 小董 G6 M4.5 接 paper_audit.wal 真数据
6. 老沈 G8 余 8 类 RejectCode
7. 老李 + 老吴 G10 W5 EOD demo smoke (本周内)

### W7 复评

- v0.7 架构 vs 实际偏离复评, 目标 < 3 偏离
- 跨洋链路真 RTT 数据 (小邓 实测) 进 v0.7 §2 (vCPU 拓扑章节)
- MLSignalCandidate W7+ 接入计划

### 给老郭集成 update 关键点

- **e2e 3.9us caveat 必须写进集成 report** — 当前是 in-memory mock + 单线程 + capturing sink, 不是真生产链路
- W6 EOW 出真 e2e p99 (含跨核 SPSC + WAL fsync) 后才能给老郭做 M1 G2 验收
- v0.7 启动期 11 步序列我 W6 出 ADR draft, 老郭 W6 评审

---

## Part 10: 我自己的反思 (老周 ADR-009 主权)

1. **v0.6 架构 W4 出的时候过乐观** — 把 SPSC + vCPU pin 当 W5 必交付, 实际 W5 全员先把业务逻辑 + 测试覆盖率打通, SPSC 这种"底层基础设施"反而滑到 W6. 这不是某个 owner 的错, 是我架构 sequencing 没排好. v0.7 我会把 "infra ready" 作为业务模块上接的 hard precondition.

2. **集成 test e2e 3.9us 数据的对外引用必须带 caveat** — W5 manager sync 我看到老李给小米的 status update 里写 "e2e p99 3.9us" 没带 caveat, 容易让 GM / 老钱误以为 M1 G2 已经稳了. 实际上这个数字是 mock 数据. 我 W6 第一件事是给老雷 + 老郭单独 brief, 把 caveat 摆清楚.

3. **W5 新加的 SingleInstanceLock 没进 v0.6 文档就上了代码** — 流程上不规范. 虽然这个改动小 + 不影响 hot path, 但严格按 ADR 流程应该先改 v0.6 → v0.7 启动期序列再写代码. W6 我把 v0.7 §8 启动期序列固化, 后续启动期任何增量必须先改 ADR.

4. **不耻下问** — vCPU pin 调优我不熟 isolcpus + nohz_full 的 kernel 配置, W6 找老姜 (B 主管, 硬件 + 内核) 单独过一遍. SPSC 跨核 cache line 性能 corner case 找小石 (B 主管, lock-free) 过. RM 启动序列幂等性找老韩.

---

## 完成汇报

**架构 v0.6 vs code 5 偏离 + v0.7 启动期 11 步 + W6 P0 派单 3 项**

- 5 偏离: Position/Settlement (P1), vCPU pin (P1), MLSignalCandidate (P2), **SPSC 拓扑 (P0)**, M1 G2/G6/G8/G10 余项 (P1)
- v0.7 启动期 11 步 (W6 ADR draft, 老郭评审)
- W6 P0 派单 3 项: 小石 SPSC / 老姜 vCPU pin / Position+Settlement owner 待 GM 拍

**对外引用 caveat:** 集成 test e2e 3.9us = in-memory mock 数据, W6 SPSC + pin + 真 IO 落地后预计 10-100x 上涨, 但距 M1 G2 80ms 仍余 100x+ 安全垫.

— 老周 (cpp-chief-architect, A 主管, ADR-009 = Opus)
2026-06-01
