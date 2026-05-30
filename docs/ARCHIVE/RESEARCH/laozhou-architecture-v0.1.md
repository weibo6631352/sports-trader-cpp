# 系统架构 v0.1

- Owner: 老周 (cpp-chief-architect)
- Last review: 2026-05-28
- 验收人: 老郭 (architecture-reviewer)
- 关联 ticket: S1-001
- 关联依赖: S1-002 (老李 协议) / S1-004 (老韩 RM) / S1-010 (老吴 跨洋) / S1-011 (老姜+小石 lock-free) / S1-021 (网络实测)
- 评审目标日: 2026-06-12 Sprint-1 retro 前

---

## 0. 阅读约定

- 本文不写代码, 只画边界与契约.
- 不入 RiskManager 内部规则 (老韩 S1-004), 不入 Polymarket 协议字段细节 (老李 S1-002).
- 凡标 `@老张` / `@小石` / `@老姜` / `@老郭` 是待评审决策, 见 §12.

---

## 1. 总体目标 + 约束

### 1.1 业务目标 (摘自 CLAUDE.md)

- 覆盖 Polymarket 体育全盘口 (Moneyline / Totals / Spreads / Period / Half / Series / Prop / Outright).
- MVP (M+6): Moneyline 单盘口实盘跑通, 72h 无崩溃, 零风控失效.
- 北极星: Sharpe ≥ 1.5, 年化 PnL ≥ $5M, 在线率 ≥ 99.9%.

### 1.2 硬约束

| # | 约束 | 来源 | 影响 |
|---|---|---|---|
| C1 | 热路径全 C++, 禁 GC 语言 | CLAUDE.md §1 | 排除 Go / Java / Python 在 critical path |
| C2 | 下单必经 RiskManager (红线) | D-02 | 架构层强制单一出口 |
| C3 | 热路径 p99 < 500us (signal → order intent) | CLAUDE.md 任务书 | 决定通信机制 / 锁选型 |
| C4 | 摄入到策略层 p99 < 20ms | A-01 | 决定 WSS 解析 + 状态发布机制 |
| C5 | 跨洋链路 (高延迟 + 带宽紧) | CLAUDE.md §1 | 决定就近部署 + 增量协议 |
| C6 | 数据 30s 无更新自动暂停市场 | D-06 | 架构层埋 heartbeat watchdog |
| C7 | 回测/实盘共用特征管道 | D-04 | Feature pipeline 不允许双套实现 |
| C8 | 私钥严禁明文落盘 | D-03 | Signer 模块隔离, 走 KMS/HSM |

### 1.3 非目标 (v0.1 不解)

- 多链 (只跑 Polygon).
- 多账户 / 多 wallet 并发 (V2 再说).
- 实时 ML 推理 (策略层先走 rule-based + 统计模型).

---

## 2. 5 层分层

```
┌─────────────────────────────────────────────────────────────┐
│  L5  EXECUTION    下单/撤单/状态机/链上签名/nonce            │
├─────────────────────────────────────────────────────────────┤
│  L4  RISK         RiskManager (唯一下单网关, 老韩拥有内部)    │
├─────────────────────────────────────────────────────────────┤
│  L3  STRATEGY     定价/信号/做市/对冲, 回测共用              │
├─────────────────────────────────────────────────────────────┤
│  L2  DATA         摄入/解析/normalize/orderbook/特征/state    │
├─────────────────────────────────────────────────────────────┤
│  L1  INFRA        runtime/log/metrics/config/IPC/clock/net   │
└─────────────────────────────────────────────────────────────┘
```

**规则:**
1. 只允许向下依赖 (L5 → L4 → L3 → L2 → L1), 同层间允许窄接口, 严禁向上回调 (用事件总线投递).
2. 跨层只通过 §6 定义的通信原语 (SPSC / MPSC / RCU 快照).
3. 每个模块必须有 owner (见下表), 跨 owner 改接口走 PR + 老郭评审.

### 2.1 L1 — INFRA (基础设施)

**职责:** 不带业务语义的横切能力. 任何上层都可以用, 但 L1 不知道上层存在.

| 模块 | 主要类/接口 | Owner | 备注 |
|---|---|---|---|
| `infra/runtime` | `EventLoop`, `ThreadPool`, `IOReactor`, `Scheduler` | 老周+老姜 | epoll/kqueue, 每 CPU 一个 loop |
| `infra/ipc` | `SpscRing<T>`, `MpscQueue<T>`, `RcuPtr<T>`, `Mailbox` | 小石 | lock-free 原语, S1-011 选型 |
| `infra/log` | `Logger`, `RingLogSink`, `AuditLog` | 小郑 | 异步 ring buffer, audit 走单独 fsync 流 |
| `infra/metrics` | `Counter`, `Histogram`, `Gauge`, `PromExporter` | 小郑 | Prometheus pull, S1-018 |
| `infra/config` | `ConfigStore`, `ConfigWatcher`, `HotReloadGuard` | 老陈 | inotify + RCU swap, §7 |
| `infra/clock` | `SteadyClock`, `WallClock`, `NtpMonitor` | 老姜 | TSC 校准, 不依赖系统时钟做 latency 测量 |
| `infra/net` | `TcpSocket`, `TlsSession`, `WssClient`, `HttpClient`, `BackoffPolicy` | 老陈 | 复用 connection, keep-alive, 跨洋走 §10 代理 |
| `infra/serde` | `JsonParser` (simdjson wrap), `ProtoCodec`, `MsgPack` | 小田#8 | 摄入端 zero-copy 走 simdjson |
| `infra/error` | `Result<T,E>`, `Status`, `Panic` | 老周 | 不抛异常 (见 §5 决策 D3) |

### 2.2 L2 — DATA (数据层)

**职责:** 把外部源 (Polymarket + Goalserve + Chain) 变成内部规范的事件流和状态快照, 喂给策略层.

| 模块 | 主要类/接口 | Owner | 备注 |
|---|---|---|---|
| `data/ingest/poly_wss` | `PolymarketWssIngestor` | 老李 | 协议细节 S1-002 |
| `data/ingest/poly_rest` | `PolymarketRestClient`, `GammaSnapshotter` | 老李 | REST 用于冷启快照 + 补偿 |
| `data/ingest/goalserve` | `GoalserveInplayIngestor`, `GoalservePregameIngestor` | 小董+小田#24 | S1-003 |
| `data/ingest/chain` | `PolygonRpcClient`, `OrderFillWatcher` | 老叶 | S1-009 选型 |
| `data/normalize` | `EventNormalizer`, `MarketIdMapper`, `OutcomeMapper` | 小余 | 外部 id → 内部 canonical id |
| `data/book` | `OrderBookL2`, `BookBuilder`, `BboTracker` | 小田#8 | per-market L2, 走 RCU 发布 |
| `data/match` | `MatchState`, `ScoreFeed`, `ClockFeed` | 小董 | 比赛进度 / 比分 / 时钟 |
| `data/feature` | `FeaturePipeline`, `FeatureStore` (read-only view) | 小梁+小余 | 实盘与回测同一份代码 (D-04) |
| `data/heartbeat` | `SourceHeartbeat`, `StaleDetector` | 小余 | 30s 阈值 (D-06) 硬编码触发 RM halt |
| `data/replay` | `EventRecorder`, `ReplayDriver` | 小段 | 实盘录制, 回测重放 |

**关键: FeatureStore 既是实盘消费者, 也是回测的唯一入口.**

### 2.3 L3 — STRATEGY (策略层)

**职责:** 输入 FeatureStore 快照 + 行情 + 比赛状态, 输出 `OrderIntent` (意向, 不是订单).

| 模块 | 主要类/接口 | Owner | 备注 |
|---|---|---|---|
| `strategy/pricing` | `FairValueEngine`, `MoneylinePricer`, `TotalsPricer` ... | 小梁 | MVP 只实现 Moneyline |
| `strategy/signal` | `SignalGenerator`, `LineMovementSignal`, `SharpFlowSignal` | 小程 | S1-017 信号假设 |
| `strategy/mm` | `MarketMaker`, `QuoteEngine`, `Inventory` | 小蒋 | 报价 + 库存控制 |
| `strategy/direction` | `DirectionalTrader` | 小袁 | 方向性 alpha |
| `strategy/hedge` | `HedgeSelector` | 小袁 | 跨盘对冲 |
| `strategy/portfolio` | `StrategyRegistry`, `IntentAggregator` | 小梁 | 多策略汇聚到唯一意向流 |

**输出口径**: `OrderIntent { market_id, side, size, limit_price, urgency, strategy_tag }`.
策略层不知道 wallet / nonce / gas, 那些是 L5.

### 2.4 L4 — RISK (风控层)

**职责:** 唯一下单网关. 所有 OrderIntent 必经此层. 内部规则由老韩拥有 (S1-004).

| 模块 | 主要接口 | Owner | 备注 |
|---|---|---|---|
| `risk/manager` | `RiskManager::check(intent) → Decision` | 老韩 | 红线 D-02, 老郭一票否决 |
| `risk/limits` | (内部) | 老韩 | 不在本文展开 |
| `risk/audit` | `RiskAuditLog` | 老韩+小郑 | 每个 decision 落 audit |
| `risk/halt` | `MarketHaltSwitch` | 老韩 | 接 §6 heartbeat watchdog |

**架构强约束:**
- `risk/manager` 是 L4 的**唯一对外符号** (`RiskGateway`).
- L3 不能 link L5; L5 不能被 L3 直接调; 两边都只看见 RiskGateway.
- 任何尝试绕过 RiskGateway 的 PR 由老郭直接 reject (D-02).

### 2.5 L5 — EXECUTION (执行层)

**职责:** 拿到 RM 放行的 `Order`, 完成签名 + 上链 + 跟单 + 撤单 + 状态机管理.

| 模块 | 主要类/接口 | Owner | 备注 |
|---|---|---|---|
| `exec/router` | `OrderRouter` | 老周 | 路由到 CLOB / fallback |
| `exec/clob` | `ClobOrderClient`, `OrderStateMachine` | 老李 | Polymarket CLOB |
| `exec/signer` | `Eip712Signer`, `KmsAdapter` | 老孙 | 私钥隔离, S1-005 |
| `exec/nonce` | `NonceManager`, `GasOracle` | 老孙+老叶 | nonce 单调, gas 自适应 |
| `exec/fill` | `FillTracker`, `PositionLedger` | 小肖 | 成交回写 + 持仓账本 |
| `exec/recon` | `Reconciler` | 老彭+老韩 | 与链上 / 与 RM 账本对账 |

---

## 3. 数据流图

```
                  跨洋链路 (高延迟, 带宽紧)
   ┌──────────────────────────────────────────────────────┐
   │                                                      │
   ▼                                                      │
┌──────────────────────┐    ┌──────────────────────┐      │
│  Polymarket WSS/REST │    │  Goalserve  inplay   │      │
│  + Gamma snapshot    │    │  livescore / pregame │      │
└────────┬─────────────┘    └─────────┬────────────┘      │
         │ raw json                   │ raw json          │
         ▼                            ▼                   │
   ┌────────────────────────────────────────────────┐     │
   │  L2  data/ingest/*   (TLS + WSS + backoff)     │     │
   │       simdjson 解析 → 内部 event POD            │     │
   └─────┬──────────────┬───────────────────┬───────┘     │
         │              │                   │             │
         ▼              ▼                   ▼             │
   ┌────────┐    ┌──────────────┐    ┌──────────────┐     │
   │normalize│   │ book builder │    │ match state  │     │
   │  L2     │   │   L2         │    │   L2         │     │
   └────┬────┘   └──────┬───────┘    └──────┬───────┘     │
        │  SPSC          │ RCU snapshot     │ RCU         │
        ▼                ▼                  ▼             │
   ┌──────────────────────────────────────────────────┐   │
   │  L2  FeatureStore  (实盘 + 回测共用入口)          │   │
   │      heartbeat watchdog → halt switch ──────┐    │   │
   └────────────────────┬─────────────────────────│────┘   │
                        │ RCU<FeatureSnapshot>    │        │
                        ▼                         │        │
   ┌──────────────────────────────────────────────│────┐   │
   │  L3 STRATEGY  (pricing + signal + mm + dir)  │    │   │
   │      → OrderIntent (MPSC)                    │    │   │
   └────────────────────┬─────────────────────────│────┘   │
                        │ OrderIntent             │ halt   │
                        ▼                         ▼        │
   ┌────────────────────────────────────────────────────┐  │
   │  L4 RiskManager  (唯一网关, 老韩)                   │  │
   │     check → Approved Order | Reject (audit)        │  │
   └────────────────────┬───────────────────────────────┘  │
                        │ Approved Order                   │
                        ▼                                  │
   ┌────────────────────────────────────────────────────┐  │
   │  L5 EXECUTION   router → signer → CLOB             │  │
   │                 fill tracker ←──── chain watcher   │  │
   └────────┬───────────────────────┬───────────────────┘  │
            │ tx                    │ rpc                  │
            ▼                       ▼                      │
   ┌──────────────────────┐    ┌──────────────────────┐    │
   │   Polymarket CLOB    │    │     Polygon RPC      │────┘
   └──────────────────────┘    └──────────────────────┘
```

**关键路径延迟预算**: 见 §11.

---

## 4. 依赖图

(箭头表示 "依赖于", 不允许反向或环路)

```
                       ┌───────────────────────┐
                       │  L5  exec/*           │
                       │  router/clob/signer/  │
                       │  nonce/fill/recon     │
                       └──────────┬────────────┘
                                  │
                                  ▼
                       ┌───────────────────────┐
                       │  L4  RiskGateway      │   ← 唯一对外符号
                       └──────────┬────────────┘
                                  │
                                  ▼
                       ┌───────────────────────┐
   ┌──────────────────►│  L3  strategy/*       │
   │                   └──────────┬────────────┘
   │                              │
   │                              ▼
   │                   ┌───────────────────────┐
   │  ┌───────────────►│  L2  data/feature     │
   │  │                └──────────┬────────────┘
   │  │                           │
   │  │            ┌──────────────┼─────────────┐
   │  │            ▼              ▼             ▼
   │  │       book builder   normalize     match state
   │  │            ▲              ▲             ▲
   │  │            └──────────────┴─────────────┘
   │  │                           │
   │  │                           ▼
   │  │                ┌────────────────────┐
   │  │                │ L2  data/ingest/*  │
   │  │                └─────────┬──────────┘
   │  │                          │
   │  │                          ▼
   │  │                ┌────────────────────┐
   │  └────────────────│  L1  infra/*       │ ◄── L2/L3/L4/L5 全可用
   └─────────────────► │ runtime/ipc/log/   │
                      │ metrics/config/net │
                      └────────────────────┘
```

**禁止边 (老郭一票否决):**
- L1 → 任何 (L1 不许知道业务).
- L2 → L3/L4/L5 (数据不许调策略).
- L3 → L5 (策略不许直接下单).
- L4 → L3 (风控不许回调策略).
- L5 → L3/L2 (执行不许逆向触发策略, 成交回报走事件总线).

**唯一允许的"反向"是事件投递** (`MPSC<Event>`), 非函数调用, 不构成符号依赖.

---

## 5. 关键技术决策

### D1. C++ 标准 = C++20, 不上 C++23

**为什么:** C++20 已经覆盖我们需要的 concepts / ranges / coroutines / `std::span` / `std::atomic_ref` / `<bit>`. C++23 (`std::expected`, `std::flat_map`) 编译器支持参差 (clang 17 仍不完整), 跨洋部署节点可能要锁旧工具链. 选 C++20 = 工具链充裕 + ABI 稳定 + 招聘门槛低.

**反方:** "expected 好用". → 我们自己实现 `Result<T,E>` 等效, 不为糖锁工具链.

@老郭 评审.

### D2. 构建系统 = CMake + Ninja + Conan (vcpkg 备选)

**为什么:** Bazel 强但学习曲线陡 + 跨平台 toolchain 配置成本高; Buck2 还不够成熟. CMake 是 C++ 行业事实标准, Conan 解决三方依赖锁版本. Ninja 拿到亚秒级增量.

**反方:** "Bazel 远期更好". → MVP 阶段优先 onboarding 速度, M+12 可再评估.

@老张 (rust-advisor) 后续如果引入 Rust 子模块, Conan + corrosion 集成评估.

### D3. 错误处理 = `Result<T, Status>`, 禁异常贯穿核心路径

**为什么:**
- 异常在热路径的不可预测开销 (unwind, 编译器 inlining 抑制) 与 C3 (p99 < 500us) 冲突.
- 异常使得控制流隐式, 风控审计难以 trace.
- 三方库可能抛异常 → 在 L1 `infra/net` 等适配层用 `try/catch` 转 `Status`, 不让异常跨边界.

**反方:** "STL 抛异常怎么办" → noexcept 标注 + 预分配 + 不用会 throw 的 STL 接口 (string ctor 等), 启动期检查.

### D4. 序列化 / 解析 = simdjson (摄入) + 自研 POD (内部)

**为什么:**
- 摄入端面 JSON, simdjson 几个 GB/s 吞吐, zero-copy DOM, 直接 C++20.
- 内部模块间不要 JSON, POD struct + SPSC 拷贝, cache-friendly + 无解析开销.
- 序列化协议 (审计 / 录制) 用 msgpack / 自研 framed, 不上 protobuf (大量小消息 protobuf header 开销不划算; 且我们不跨语言).

**反方:** "上 FlatBuffers" → 内存模型适合 RPC, 我们是进程内多线程, 用裸 POD 更直接.

### D5. 并发模型 = "每物理核 1 个 reactor + SPSC pin" 而非全局线程池

**为什么:**
- 全局线程池让任务在核间漂, cache miss + NUMA penalty 直接吃掉延迟预算.
- 每条 critical path 绑定一组核心 (e.g. core0: WSS-ingest, core1: book-build+feature, core2: strategy, core3: risk+exec), 用 SPSC 串联, 无锁无抢占.
- 非关键路径 (REST / metrics / log fsync) 走 background 线程池.

**反方:** "用 io_uring / 协程 runtime (asio / folly coro)" → 协程对延迟敏感的小阶段不一定优于 reactor + state machine, 且 ABI 不稳. 内部任务调度先用裸 reactor, 必要时局部上 stackless coroutine.

@老姜 + @小石 在 S1-011 给出 lock-free 原语和 reactor 实现细节.

### D6. 内存 = 启动期预分配 + 池化 + 无 std::shared_ptr 在热路径

**为什么:**
- malloc 的 p99 是延迟杀手, 跨洋部署节点内存不一定足够大, 早分配早保证.
- `shared_ptr` 的原子引用计数在多核 cache line ping-pong 严重.
- 热路径用 arena / object pool / `unique_ptr` + 移动; `shared_ptr` 只在非关键管理代码.

**反方:** "shared_ptr 安全" → 安全代价是延迟, 用所有权清晰的 SPSC 队列 + 池更安全.

### D7. 事件总线 = SPSC ring (热路径) + MPSC queue (汇聚) + RCU (状态快照)

**为什么:** 见 §6 详述. 不同 pattern 用不同原语, 不强求一统.

@小石 选型见 S1-011.

### D8. Polygon RPC 接入 = 多 provider + 健康检查 + 故障切换

**为什么:** 跨洋 + 单一 RPC 节点 SPOF 不可接受. 至少 2 个 provider, gas oracle 独立, 关键 RPC 走自有节点 (S1-009 老叶给方案).

### D9. 配置 = TOML + RCU 热加载, 红线参数不允许热改

**为什么:** YAML 太多歧义, JSON 没注释; TOML 在 C++ 有成熟 parser (toml++). 热加载机制详见 §7. 风控红线参数 (敞口上限, 30s 停盘阈值) 编入二进制, 改要走部署, 不能运行时改 (D-02 / D-06 一致).

### D10. 部署 = 单进程多线程 (主交易) + 旁路进程 (录制 / metrics / reconciler)

详见 §8.

---

## 6. 通信机制

(本节最终方案以 @小石 在 S1-011 输出为准, 此处为架构层口径)

### 6.1 三种模式

| Pattern | 原语 | 用途 | 备注 |
|---|---|---|---|
| **1:1 串行流水线** | `SpscRing<T>` (cache-line padded) | ingest → normalize → book → feature → strategy 这条主路径 | 容量预分配, 满则丢老的 + metric 告警, 不阻塞 |
| **N:1 汇聚** | `MpscQueue<T>` | 多策略 → IntentAggregator → RM | hazard pointer 或 ticket lock-free, latency 优先 |
| **读多写少状态** | `RcuPtr<Snapshot>` (epoch-based) | FeatureSnapshot, OrderBook L2 view, ConfigStore | 读端零拷贝零等待, 写端 swap 指针, 回收异步 |

### 6.2 跨进程

主交易进程与旁路进程通过 SHM ring (`infra/ipc/shm_ring`) + UDS 控制信道. 跨机器 (跨洋) 走 TCP + 协议 framed, 不用任何 RPC 框架 (gRPC 在 critical path 不可接受).

### 6.3 反压策略

- SPSC 满: drop oldest + bump drop counter + alert (摄入端容忍, 因为有补偿快照).
- MPSC 满: 上游 (策略) 直接放弃本次 intent + audit (不阻塞).
- RCU: 永不阻塞读者, 写者 epoch 推进慢则回收滞后, 用 watermark 监控.

@小石 + @老姜 在 S1-011 输出: SPSC 容量, padding 策略, RCU epoch 实现 (folly RCU vs urcu vs 自研).

---

## 7. 配置 / 状态管理

### 7.1 配置三档

| 档位 | 例子 | 改法 |
|---|---|---|
| **A 编译期** | 风控红线阈值 (敞口上限), 30s heartbeat 阈值, 主交易 wallet 白名单 | 修代码 + 走部署 (D-02 / D-06 红线兜底) |
| **B 启动期** | 上游 endpoint, 线程亲和性, 容量参数, MVP 盘口范围 | 改 TOML + 重启 |
| **C 运行期 (热加载)** | 报价 spread / 库存上限 (软) / 信号开关 / 单策略 enable 标志 | inotify watch + RCU swap, 不重启 |

### 7.2 热加载机制

1. `ConfigStore` 启动时载入 `config/runtime.toml`, 构建 immutable `ConfigSnapshot`, 用 `RcuPtr<ConfigSnapshot>` 发布.
2. `ConfigWatcher` 线程 inotify 监听文件变更.
3. 变更触发: 解析新文件 → schema 校验 → diff 出 dirty key → 调用各模块注册的 `validate(new) → Status` 钩子 → 全过则 RCU swap, 全员下次读已是新值.
4. 任何 validate 失败: 拒绝整批变更 + alert + audit, 不允许 partial apply.
5. 红线档 A 参数即使出现在文件里也会被 schema 拒绝 (defense in depth).

### 7.3 运行时状态

- **不持久化的 (RAM-only):** OrderBook L2, FeatureStore, 报价状态.
- **必须持久化:** PositionLedger, NonceManager state, FillTracker undelivered, audit log.
- 持久化层: WAL (append-only file, fsync per critical record) + 周期 snapshot. 不上 RocksDB (依赖太重, 跨洋部署运维负担), 自研轻量 WAL + 内存 hash + 启动重放.

@老张 评估: 是否值得用 sled (Rust) 嵌入做 WAL. **倾向不用**, FFI 边界 + 运维复杂.

---

## 8. 进程模型

### 8.1 主进程: `stcpp-trader`

**单进程多线程**, 内部按核绑定:

```
core 0: net-io      (TLS / WSS / REST 收发, 跨洋长连接)
core 1: parse       (simdjson → POD event)
core 2: book+match  (book builder + match state + normalize)
core 3: feature     (feature pipeline + heartbeat watchdog)
core 4: strategy    (pricing + signal + mm)
core 5: risk+exec   (RM + signer + router + nonce)
core 6: chain-io    (Polygon RPC + fill watcher)
core 7: bg          (log fsync / metrics / config watcher / WAL)
```

具体核数与 NUMA 策略以部署节点物理规格为准 (S1-010 老吴方案).

**为什么单进程:** critical path 跨核已经付了 cache 代价, 跨进程再加一层 SHM 没必要; 单进程也方便用 RCU 共享只读快照.

### 8.2 旁路进程

| 进程 | 职责 | 失联影响 |
|---|---|---|
| `stcpp-recorder` | 订阅 SHM, 落盘原始事件 + 决策日志 | 不影响主交易, 仅影响回放质量 |
| `stcpp-recon` | 周期对账 (链上 vs ledger) | 不影响主交易, 离线告警 |
| `stcpp-metrics-agent` | Prometheus exporter + alertmanager push | 不影响主交易 |

主交易进程崩溃 ≠ 旁路进程崩溃, 解耦.

### 8.3 边界外服务

- **KMS / HSM** (S1-005 老孙): 独立部署, 主进程通过 TLS + mTLS 调签名服务, 私钥不落主进程 RAM.
- **Polygon 自有节点** (S1-009 老叶): 独立运维, 主交易作为 client.

---

## 9. 失败恢复

### 9.1 故障域分级

| 级别 | 例子 | 恢复策略 |
|---|---|---|
| L0 单事件丢失 | 一条 WSS 消息 parse 失败 | drop + counter, 后续快照纠正 |
| L1 上游断流 | Polymarket WSS 断 / Goalserve 超时 | 自动重连 + 退避; 超 30s 触发 RM halt (D-06) |
| L2 单模块崩溃 | book builder 段错误 | 整进程 fail-fast (见下) |
| L3 主进程崩溃 | 任意线程 abort | systemd 拉起, 启动重放 WAL + chain 同步 nonce + 资金对账完成前禁止下单 |
| L4 主机失联 | 跨洋链路断, 节点宕 | 不做自动 failover (V1 不上多活, 老钱 D-01 范围内); 人工介入 + 撤所有挂单 (链上侧) |

### 9.2 主进程崩溃恢复链

```
systemd restart
  ↓
load config (TOML)
  ↓
replay WAL → 重建 PositionLedger / NonceManager / open orders 缓存
  ↓
启动 RM (注入红线参数 + halt switch)
  ↓
连 Polygon RPC → 拉链上 nonce & 持仓 → 与 ledger 对账
  ↓
连 Polymarket REST → 拉 open orders 实况 → 与 ledger 对账
  ↓
对账不一致 → 进入 SAFE_MODE (只撤不开仓), 告警, 等人工
  ↓
对账一致 → 连 WSS → 等待 first heartbeat → 解锁交易
```

### 9.3 fail-fast 而非 try-survive

任意热路径线程出非预期错误, **整进程立即 abort + core dump + alert**, 不做线程级 try-recover. 理由: 热路径线程持有 nonce / position 状态, 局部恢复极易留下不可见状态污染, 风控审计无法追溯; 进程级重启 + WAL 重放是确定性的.

### 9.4 链上 nonce 治理

- NonceManager 启动时与链上同步, 取 max(local, on-chain).
- 每笔下单 nonce 递增前先 WAL fsync, 防止崩溃后 nonce 复用导致 tx 替换.
- 详见老孙 S1-005 + 老叶 S1-009 联合设计.

---

## 10. 外部边界 (协议接入点)

| 边界 | 接入点模块 | 协议 | Owner | 跨洋影响 |
|---|---|---|---|---|
| **Polymarket WSS** (行情 + 订单状态) | `data/ingest/poly_wss` | WSS + JSON | 老李 | 长连接 + 心跳, 断线全量重订阅, S1-002 |
| **Polymarket REST gamma/clob/data** | `data/ingest/poly_rest`, `exec/clob` | HTTPS + JSON | 老李 | 冷启快照 + 下单, 关键路径 RTT 直接吃 §11 预算 |
| **Goalserve inplay / livescore / pregame** | `data/ingest/goalserve` | HTTPS poll + XML/JSON | 小董 | poll 节奏 + 限流 + 代理 (GOALSERVE_PROXY), S1-003 |
| **Polygon RPC (读)** | `data/ingest/chain` | JSON-RPC over HTTPS | 老叶 | 多 provider, S1-009 |
| **Polygon RPC (写, send raw tx)** | `exec/clob` (经 router) | JSON-RPC | 老李+老叶 | 自有节点优先 |
| **KMS / HSM** | `exec/signer` | gRPC over mTLS (或 PKCS#11) | 老孙 | 同区域部署, RTT 内部网络 < 1ms |
| **Prometheus pull** | `infra/metrics` | HTTP /metrics | 小郑 | 旁路 |

**全部外部 IO 必须经 §2.1 `infra/net` 适配层**, 不允许任何业务模块直接 syscall socket; 这样跨洋代理 / TLS pinning / 流量统计能统一管.

---

## 11. 性能预算 (拆到模块级)

热路径 p99 < 500us (signal → order intent → RM放行 → CLOB 提交前最后一字节出网卡):

| 阶段 | 模块 | p99 预算 |
|---|---|---|
| FeatureSnapshot 读取 | RCU snapshot deref | < 1 us |
| 信号 + 定价 | strategy/pricing + signal | < 80 us |
| 做市报价生成 | strategy/mm | < 50 us |
| IntentAggregator + MPSC 入队 | strategy/portfolio | < 10 us |
| RiskManager 检查 | risk/manager (老韩) | < 50 us (硬上限, 老韩需对此承诺) |
| Router + 选 endpoint | exec/router | < 10 us |
| EIP-712 签名 (本地 cache 后) | exec/signer | < 100 us (KMS hot path 走预签或异步) |
| CLOB 序列化 + TLS write | exec/clob + infra/net | < 100 us |
| **小计 (本地)** |  | **~ 400 us** |
| 余量 |  | ~ 100 us |

**注**: 跨洋网络 RTT 不在 500us 预算内 (那是不可控物理时延), 预算只覆盖**本地 CPU 处理**. 跨洋 RTT 由 §10 协议接入点 + S1-010 部署方案最小化.

摄入 → 策略可用 p99 < 20ms:

| 阶段 | p99 预算 |
|---|---|
| 跨洋包到达 net-io | 不计 (物理时延) |
| TLS 解密 + WSS 分帧 | < 2 ms |
| simdjson 解析 → POD | < 1 ms |
| normalize + id map | < 1 ms |
| book builder 增量 | < 3 ms |
| feature pipeline 更新 | < 5 ms |
| RCU snapshot 发布 | < 1 ms |
| **小计** | **~ 13 ms** |
| 余量 | ~ 7 ms |

---

## 12. 开放问题

> 这些是本架构 v0.1 故意未拍板, 需要小专题或评审决议的项. 老郭评审时请逐条点名.

| # | 议题 | 待定 | 负责跟进 |
|---|---|---|---|
| OQ-1 | C++20 vs C++23 | D1 是否锁 C++20 | @老郭 评审 |
| OQ-2 | lock-free 原语具体实现 (SPSC 大小, RCU 库) | §6 待 S1-011 给出 | @小石 + @老姜 |
| OQ-3 | 配置文件格式 TOML vs JSON-with-comments | §7 倾向 TOML | @老陈 |
| OQ-4 | 是否引入局部 Rust 子模块 (e.g. WAL / 加解密) | 倾向暂不引入 | @老张 (rust-advisor) |
| OQ-5 | KMS 调用模式 (同步阻塞 vs 异步预签) 对 D5 reactor 模型的影响 | §11 签名预算 100us 依赖此 | @老孙 |
| OQ-6 | 跨洋失联时的"撤单优先"机制是否需要链上 dead-man switch | §9 L4 故障域 | @老韩 + @老叶 |
| OQ-7 | 主进程 NUMA 策略 (单 socket vs 双 socket pinning) | §8 取决于实际硬件 | @老吴 (S1-010) |
| OQ-8 | 回测引擎是否复用 `stcpp-recorder` 的事件流格式 | §2.2 data/replay 需对齐小梁 | @小梁 |
| OQ-9 | 私钥从 KMS 拉签名 vs 进程内派生 (HSM) 对延迟影响实测 | 依赖 S1-005 | @老孙 |
| OQ-10 | metrics 抽样率 vs 精度 (热路径埋点不能本身成为瓶颈) | §2.1 infra/metrics | @小郑 |

---

## 附录 A — 命名与目录约定 (建议, 待老郭评审)

```
src/
  infra/{runtime,ipc,log,metrics,config,clock,net,serde,error}
  data/{ingest,normalize,book,match,feature,heartbeat,replay}
  strategy/{pricing,signal,mm,direction,hedge,portfolio}
  risk/                 # 老韩拥有内部, 对外只暴露 RiskGateway
  exec/{router,clob,signer,nonce,fill,recon}
include/stcpp/...       # 公共头, 按层分子目录
tests/{unit,integration,replay}
tools/{recorder,recon,bench}
```

每个目录配 `OWNERS` 文件, 改其下文件 PR 自动 cc owner.

---

## 附录 B — 与 ticket 的对应

- S1-002 (老李 Polymarket): 落到 §2.2 `data/ingest/poly_*` + §2.5 `exec/clob` + §10.
- S1-003 (小余 Goalserve): 落到 §2.2 `data/ingest/goalserve` + §10.
- S1-004 (老韩 RM): 落到 §2.4, 内部接口只暴露 `RiskGateway`.
- S1-005 (老孙 私钥): 落到 §2.5 `exec/signer` + §10 KMS 边界.
- S1-009 (老叶 RPC): 落到 §2.2 `data/ingest/chain` + §10 + §11.
- S1-010 (老吴 部署): 落到 §8 进程模型 + §11 NUMA.
- S1-011 (老姜+小石 lock-free): 落到 §6 + §11.
- S1-018 (小郑 Prom): 落到 §2.1 `infra/metrics`.
- S1-021 (网络实测): 直接喂 §11 摄入预算校准.

---

**评审请求 (老郭):** 重点请评 D1 (C++20 锁版本) / 红线层 §2.4 边界设计 / §9.3 fail-fast 政策. 拍板后我会按评审意见出 v0.2 + 进 ADR.
