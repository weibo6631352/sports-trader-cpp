# 系统架构 v0.3 (WebSocket 不阻塞 + bulk endpoint + R-12 红线)

- Owner: 老周 (cpp-chief-architect)
- Date: 2026-05-28
- 验收人: 老郭 (架构评审) + 老韩 (RM 接口) + 老陈 (网络)
- 关联:
  - `docs/ADR/2026-05-28-gm-redline-wss-non-blocking.md` (**R-12 红线**, GM 老雷今天立)
  - `docs/RESEARCH/laozhou-architecture-v0.2.md` (v0.2 主体, **本文不重写**, 只加 §17 + 改 §6/§11/§15 相关)
  - `docs/RESEARCH/laozhou-architecture-v0.1.md` (历史 trail, 不废)
  - `docs/RESEARCH/laoli-polymarket-api-spec-v1.md` §3.1 `/books` 批量, §5 WebSocket market channel
  - `docs/RESEARCH/xiaoduan-goalserve-api-spec-v1.md` inplay 全联盟 endpoint
  - `docs/RESEARCH/xiaoshi-data-structures-selection-v1.md` §1 rigtorp SPSC / moodycamel MPSC
  - `docs/RESEARCH/laojiang-latency-budget-v1.md` §1 阶段表 / §2 外环
  - `docs/RESEARCH/laochen-network-bench-v1.md` §11.3 HTTP/2 multiplexing
  - **Wave 10 在跑**: 老李 + 小段 `api-call-optimization-v1` (endpoint 分类矩阵, 我等他们出 endpoint 表, 本文只定调用模式)
- 状态: v0.3 在 v0.2 基础上**增量**, 不重写; v0.2 主体仍然有效

---

## 0. v0.2 → v0.3 变更摘要 (R-12 红线落地)

### 0.1 一句话版

GM 老雷今天 2026-05-28 立了**红线 R-12** —— "绝对不可以阻塞 Polymarket 的 ws 事件" + "bulk endpoint 优先, 不要 per-market fan-out". v0.3 把这两条落到架构: **§17 新章 (WebSocket 不阻塞 + 异步调用 + bulk endpoint 路由)**, 同时修订 §6 通信原语 / §11 性能预算 / §15 vCPU 映射, 显式声明 vCPU0 不发任何 REST / 不打 mutex > 100us.

### 0.2 R-12 → v0.3 落地对照表

| R-12 子条 | v0.3 落地位置 | 验收口径 |
|---|---|---|
| WebSocket event loop 独立线程 + pin vCPU0 | §15.6 (新), §17.1 | systemd unit + pthread_setaffinity 显式 |
| WebSocket 收 → 解码 → SPSC → 立即返回, p99 < 50us | §17.2 入站路径 + §11.3 (新) | 老陈基准: 单 message 处理 p99 < 50us |
| REST / DB / IPC 全异步 worker pool (vCPU3) | §17.3 出站路径 | 任何 in-loop sync IO = PR reject (§17.8) |
| single-flight 同一 key 共享 inflight | §17.4 + §17.4.2 草图 | 1000 strategy 要同一 market data 实发 1 call |
| bulk endpoint 优先 (`/books` / `inplay`) | §17.5 路由表 | per-market fan-out = N+1 反模式 |
| 周期轮询独立线程 (vCPU3), 不在 WebSocket loop | §17.1.3 + §15.6 | 60s gamma poll 独立 std::thread |
| WebSocket 重连 + REST 兜底, 不阻塞别的 WebSocket 流 | §17.6 兜底机制 | 单流 stale 异步触发 `/books` snapshot |
| 红线 R-12 enforcement (老高 PR review) | §17.8 红线列表 | 4 条 PR-reject 规则 |
| 测试验收 (小宋) | §17.7 测试场景 | 3 个场景注入慢响应/并发/断线 |

### 0.3 与 v0.2 的兼容性

- v0.2 §2 5 层分层 / §3 数据流 / §4 依赖图 **不变**
- v0.2 §6 通信原语 **扩**: 增加"WebSocket loop ring 写策略 = try_push, drop on full" + MPSC 到 worker pool
- v0.2 §11 性能预算 **扩**: 加 §11.3 "WebSocket 入站路径模块级预算 (p99 < 50us)"
- v0.2 §15 4 vCPU 映射 **修订**: vCPU0 用途收口为"WebSocket event loop only", 不再"net-io + parse"杂烩, parse 单独 reactor 仍在 vCPU0 (轻负载, 同核 SPSC 省 cache miss), 显式禁止 REST IO 在该核
- v0.2 §14 SAFE_MODE / §13 部署假设 **不变**

---

## 1 ~ 16. 沿用 v0.2 (本文不重复)

本文只列**修订**的小节, 完整主体见 `laozhou-architecture-v0.2.md`. 修订点:

### §6.x 修订 — 通信原语扩展 (本文 §6-DELTA)

见本文 §17.1.2 + §17.3.2 的 ring buffer 拓扑.

| ring | 方向 | 类型 | 容量 | 写者 fail 策略 |
|---|---|---|---|---|
| `wss_in_ring` | vCPU0 WebSocket reactor → vCPU1 book builder | SPSC | 16384 frames | **try_push, drop + counter, 绝不 block** |
| `book_to_strat_ring` | vCPU1 book builder → vCPU2 strategy | SPSC | 8192 events | try_push, back-pressure log warn |
| `bg_work_ring` | vCPU0/1/2 → vCPU3 background | **MPSC** (moodycamel) | 4096 jobs | try_push, drop + alert (因为 REST job drop = 数据陈旧, 但绝不能 block hot path) |
| `bg_to_book_ring` | vCPU3 background → vCPU1 book builder | SPSC | 1024 results | block-free, REST snapshot 结果回灌 |

**新增 R-12 约束** (vs v0.2 §6.1):
- WebSocket event loop 写 `wss_in_ring` 必须 `try_push`, 满了 drop 当前 frame + `wss.ring_full_drop_total` 计数器 + log warn; 绝不能 `push_blocking()` (这是 R-12 红线)
- 任何 worker pool 入队 (`bg_work_ring`) 同样 try_push, 满了走 single-flight 折叠或丢弃 + alert

### §11.x 修订 — 性能预算扩展 (本文 §11-DELTA)

见本文 §17.1.4 + §17.7 验收准则.

新增 §11.3:

| 阶段 | 模块 | p99 预算 | R-12 红线 |
|---|---|---|---|
| WebSocket frame recv → 用户态 | infra/net (epoll/io_uring) | < 15us | — |
| WebSocket 解帧 (mask + 拼) | infra/net/wss | < 8us | — |
| simdjson on-demand parse | data/ingest/poly_wss | < 20us | — |
| normalize + id map | data/normalize | < 5us | — |
| SPSC try_push `wss_in_ring` | infra/ipc | < 0.5us | **不允许 block 等** |
| **WebSocket loop 单 message 处理小计** | **vCPU0** | **< 50us p99** | **R-12 硬上限** |
| **vCPU3 异步 REST p99** | bg worker | < TTL_target (跨洋 < 500ms 接受) | 不影响 vCPU0 |

注: 老姜 v1 §1 阶段表第 1-4 阶段加起来 73us p99, 本表收口为 < 50us 是因为 v0.3 砍掉 vCPU0 上的 "RDTSC 测量 + 多次阶段切换" 开销, **合并为单一 reactor + simdjson on-demand** (不全解, 只解 book channel 必要字段). 老姜 ack 后写入 v0.3 验收.

### §15.x 修订 — vCPU 映射 R-12 收口 (本文 §15-DELTA)

见本文 §15.6 (新).

---

## 15.6 vCPU 映射 — R-12 收口 (替代 v0.2 §15.1 v0.2 的"net-io + parse"描述)

**v0.2 表述** (现作废):

```
vCPU 0: net-io + parse (TLS/WebSocket 收发 + simdjson 解析)
vCPU 1: book + match + feature
vCPU 2: strategy + risk + exec
vCPU 3: bg (chain-io + log fsync + metrics + config + WAL fsync)
```

**v0.3 收口 (R-12 立)**:

```
vCPU 0: WebSocket event loop (Polymarket WebSocket + Polygon WebSocket, 绝不阻塞)
        - 含 simdjson on-demand parse (轻负载, 同核省 cache miss)
        - 含 normalize + SPSC try_push 到 vCPU1
        - 不发 REST, 不打 mutex > 100us, 不 fsync
vCPU 1: book builder + matching engine + feature pipeline (热路径)
vCPU 2: strategy + risk + exec (热路径)
vCPU 3: background pool
        - REST worker pool (Polymarket REST / Polygon RPC HTTP / Goalserve REST)
        - 周期轮询线程 (gamma 60s, market list 60s, Goalserve inplay 1-3s, Goalserve pregame 30-60s)
        - audit WAL fsync / exec WAL fsync / log rotate
        - metrics exporter
        - chain-io 写
        - config watcher
```

**关键变化对比表**:

| 维度 | v0.2 | v0.3 (R-12) |
|---|---|---|
| vCPU0 用途 | net-io + parse 杂烩 | **WebSocket event loop only**, 显式禁 REST |
| WebSocket 解析位置 | 同 vCPU0 | 同 vCPU0 (轻负载, 同核 SPSC 省 cache miss; 但严格 < 50us p99) |
| REST 调用位置 | 默认在 net-io vCPU0 | **强制 vCPU3 worker pool, 异步** |
| 周期轮询位置 | 模糊 ("bg") | **明示 vCPU3 独立线程**, 60s gamma poll / 1-3s Goalserve inplay 与 WebSocket 完全解耦 |
| mutex 等待红线 | 未明示 | **vCPU0 锁等待 > 100us = R-12 violation** |
| ring 写策略 | 未明示 | **try_push 失败 drop + log warn, 绝不 block** |

**为什么仍把 parse 放 vCPU0** (不下沉到 vCPU1):
1. simdjson on-demand 单条 book update 解析 ~20us, 远小于 50us R-12 红线, 不构成阻塞威胁
2. 下沉到 vCPU1 = 多一次 SPSC + 多一次 cache 迁移 (~5-10us), 收益不明显
3. 关键是: **vCPU0 上的 parse 必须用 simdjson on-demand, 不允许 nlohmann::json**, 不允许任何 schema 不明的反序列化
4. 老姜 S1-011 v0.3 加 "vCPU0 WebSocket loop p99 < 50us" 单项基准

**pinning + 优先级** (替代 v0.2 §15.3 vCPU0 行):

| vCPU | 内容 | pin | sched | 优先级 | 网卡 IRQ |
|---|---|---|---|---|---|
| 0 | WebSocket event loop (only) | pthread_setaffinity_np | SCHED_FIFO | 60 (略高于 v0.2 的 50) | 网卡 RX IRQ pin vCPU0 |
| 1 | book + match + feature | 同上 | SCHED_FIFO | 55 | — |
| 2 | strategy + risk + exec | 同上 | SCHED_FIFO | 55 | — |
| 3 | bg (REST pool + 周期 + WAL fsync + metrics) | SCHED_OTHER | nice 0 | — | — |

vCPU0 优先级提升到 60: 因为 R-12 红线"绝对不阻塞", 抢占 vCPU3 是允许的 (vCPU3 bg 慢一点没事, vCPU0 慢一点 = book stale).

---

## 17. WebSocket 不阻塞 + 异步调用 + bulk endpoint 架构 (新章)

### 17.0 章节定位

本章是 v0.3 主体新增, 落地 GM **R-12 红线**. 与 Wave 10 老李 + 小段 `api-call-optimization-v1` (在跑) 协同: **他们出 endpoint 分类矩阵 (哪些 endpoint 是 bulk, 哪些是 per-market, 哪些是 WebSocket-only)**, **本章定 endpoint 调用模式** (单核单线程跑哪个 / 异步 worker pool 跑哪个 / 周期轮询线程跑哪个 / single-flight 折叠哪个).

---

### 17.1 线程模型

#### 17.1.1 线程清单 (4 vCPU × 多线程映射)

| # | 线程 | pin vCPU | 类型 | 职责 | 进/出 ring |
|---|---|---|---|---|---|
| T0 | `wss_poly_reactor` | 0 | SCHED_FIFO 60 | Polymarket WebSocket reactor: epoll → recv → unmask → simdjson on-demand → normalize → try_push wss_in_ring | out: wss_in_ring |
| T1 | `wss_polygon_reactor` | 0 | SCHED_FIFO 60 | Polygon WebSocket reactor (eth_subscribe nonce/gas): 同上 | out: wss_in_ring |
| T2 | `book_builder` | 1 | SCHED_FIFO 55 | 消费 wss_in_ring, 增量更新 orderbook + match state + feature pipeline, 发布 RCU snapshot | in: wss_in_ring + bg_to_book_ring; out: book_to_strat_ring |
| T3 | `strategy_engine` | 2 | SCHED_FIFO 55 | 消费 book_to_strat_ring + RCU snapshot, 跑 pricing/signal/mm, 生成 OrderIntent → RiskGateway::evaluate → signer → CLOB write | in: book_to_strat_ring; out: bg_work_ring (audit/log async) |
| T4 | `bg_rest_worker_pool` (3-4 个 thread) | 3 | SCHED_OTHER | 异步 REST worker: 消费 bg_work_ring, 跑 HTTP/2 connection pool, single-flight 折叠, retry + backoff | in: bg_work_ring; out: bg_to_book_ring (snapshot 回灌) |
| T5 | `periodic_gamma_poller` | 3 | SCHED_OTHER | 周期 60s 拉 gamma `/sports/events` + market list | out: bg_work_ring (直接发 REST job) |
| T6 | `periodic_goalserve_inplay_poller` | 3 | SCHED_OTHER | 周期 1-3s 拉 Goalserve inplay 全联盟 (bulk) | out: bg_to_book_ring (内部直接更新, 不二次入队) |
| T7 | `periodic_goalserve_pregame_poller` | 3 | SCHED_OTHER | 周期 30-60s 拉 Goalserve pregame odds | out: bg_to_book_ring |
| T8 | `audit_fsync` | 3 | SCHED_OTHER | 老韩 audit WAL group commit fsync | — |
| T9 | `exec_fsync` | 3 | SCHED_OTHER | exec WAL (nonce/position) fsync | — |
| T10 | `metrics_exporter` | 3 | SCHED_OTHER | Prometheus pull + push | — |
| T11 | `config_watcher` | 3 | SCHED_OTHER | TOML hot reload via RCU | — |

**总计**: 12 个线程跑在 4 vCPU 上. vCPU0 = 2 线程 (Poly + Polygon WebSocket); vCPU1 = 1 线程; vCPU2 = 1 线程; vCPU3 = 8 线程 (但全是 IO bound, OS 调度).

#### 17.1.2 ring buffer 拓扑

```
                vCPU 0                    vCPU 1                  vCPU 2
     ┌─────────────────────┐      ┌──────────────────┐    ┌──────────────────┐
     │ T0 wss_poly         │      │ T2 book_builder  │    │ T3 strategy      │
     │ T1 wss_polygon      │      │                  │    │                  │
     └────────┬────────────┘      └────────┬─────────┘    └──────────────────┘
              │ SPSC try_push             │ SPSC try_push          ▲
              │ wss_in_ring (16K)          │ book_to_strat_ring     │
              └──────────────────────────► ──────────────────────────┘
                                                    │ (also reads RCU snapshot)
                                                    ▼
                                          (snapshot via RCU)
                                                    │
                                                    ▼ (T3 writes audit/log)
              ┌─────────────────────────────────────┐
              │            bg_work_ring (MPSC, moodycamel, 4096)         │
              │            writers: T2, T3 (audit/log/REST trigger)      │
              └─────────────────────────┬───────────┘
                                        │ vCPU 3
                              ┌─────────▼──────────┐
                              │ T4 bg_rest_pool    │
                              │ T5..T7 periodic    │
                              │ T8..T11 fsync etc. │
                              └─────────┬──────────┘
                                        │ SPSC bg_to_book_ring (1024)
                                        ▼
                                  T2 book_builder (vCPU 1)
                                  (REST snapshot 回灌)
```

**关键不变量**:
1. vCPU0 只**写** `wss_in_ring`, 不读任何 ring (除内部小 buffer)
2. vCPU0 不**写** `bg_work_ring` (任何 REST 触发都从 vCPU1/2 走, vCPU0 只管 WebSocket frame)
3. vCPU3 所有线程不**写** `wss_in_ring` 或 `book_to_strat_ring` (热路径 ring 只能由热路径写)
4. 任何 ring `try_push` 失败 → 计数器 + log, **绝不** block 等

#### 17.1.3 周期轮询线程独立 (R-12 第二条用户原话落地)

GM 原话: "某些直播源一次周期调用是会覆盖某市场下所有的盘口的, 并不需要每个盘口都去拉取外部 api"

落地:
- **T6 periodic_goalserve_inplay_poller**: 跑 `https://www.goalserve.com/getfeed/<KEY>/<sport>/inplay` 一次拉全联盟 (Goalserve inplay XML 一次几十 KB, 含全联盟所有正在进行的赛事 + 球员实时数据). 1-3s 周期. 拉回后**一次性**更新所有市场的"分节比分 / 球员数据"内部状态.
- **T7 periodic_goalserve_pregame_poller**: 30-60s 周期拉 pregame odds 全联盟.
- **T5 periodic_gamma_poller**: 60s 周期拉 Polymarket gamma `/sports/events?closed=false&limit=500` 一次性拿 N 个 events.
- **绝对禁止**: 每个 market 起一个 timer 去拉自己的 endpoint (反 R-12, N+1 反模式).

**这些线程跑在 vCPU3, 与 vCPU0 WebSocket event loop 物理隔离**. Goalserve inplay 一次解析 ~50ms 也不影响 vCPU0 一根头发.

#### 17.1.4 vCPU0 红线 (R-12 enforcement)

vCPU0 上的 T0 / T1 线程**绝对禁止**:
- 调用任何 `::open()` `::read()` `::write()` (除 socket recv/send 走 epoll / io_uring 非阻塞)
- 调用任何 `::fsync()` `::fdatasync()`
- 调用任何 `std::mutex::lock()` 等待超过 100us (用 `try_lock + degrade`)
- 调用任何 HTTP client (curl / asio HTTP / boost::beast HTTP)
- 调用任何 DB client / gRPC client / IPC blocking IO
- 调用任何 `std::cout` / `printf` (异步 log 走 `bg_work_ring`)
- 持有任何会被其他线程长期持有的锁

**允许**:
- epoll_wait / io_uring_wait (非阻塞 IO 多路复用)
- simdjson on-demand parse (无堆分配, 已 review)
- SPSC ring try_push (lock-free)
- 计数器 fetch_add (atomic, < 100ns)

---

### 17.2 入站路径 (WebSocket → book builder)

#### 17.2.1 路径全图

```
[Polymarket WebSocket frame 到达网卡 vCPU0]
        ↓ (网卡 IRQ pinned vCPU0)
[kernel epoll/io_uring]
        ↓ (5-15us)
[T0 wss_poly_reactor recv()]
        ↓ (zero-copy frame buffer)
[WebSocket 解帧 (mask + 拼)]  (2-8us)
        ↓
[simdjson on-demand parse]  (10-20us, 只解 book channel 必要字段)
        ↓
[normalize (token_id → market_id)]  (<5us, ankerl::unordered_dense_map)
        ↓
[SPSC try_push wss_in_ring]  (<0.5us)
        ↓
[T0 立即返回 recv 下一帧]
        ↓
                    ──── 跨核 SPSC ────
        ↓
[T2 book_builder 消费 wss_in_ring (vCPU1)]
        ↓ (3-12us SPSC + cache load)
[orderbook 增量 apply]
        ↓
[发布 RCU snapshot]  (<1us swap)
```

**老姜 v1 阶段表对照**: 第 1-5 阶段 = 25 + 5 + 50 + 0.5 + 12 = 92.5us p99 全程; 本表收口为 < 50us 是因为:
1. 第 3 阶段 simdjson schema-aware 只解必要字段, 不解全 message: 50us → 20us
2. 第 5 阶段 (orderbook apply) 不算 vCPU0 时间, 在 vCPU1
3. 第 1 阶段 ~15us 是物理常数 (网卡 → epoll wakeup)

**实测验收**: 老陈 S1-021 加测 "WebSocket 单 frame 处理 p99 < 50us" 一栏, 老姜 S1-011 v0.3 加该基准.

#### 17.2.2 simdjson on-demand 约束

- 只解 book channel: `event_type`, `asset_id`, `timestamp`, `hash`, `bids`, `asks`
- 不解全 message (不要无脑 simdjson document parse)
- 解析失败 → drop + `wss.parse_error_total` counter + 不 abort (parse 错误是 NO abort, 见 v0.2 §9.3.2)
- 复用 simdjson::ondemand::parser (TLS, 启动期预分配)

---

### 17.3 出站路径 (异步 worker pool)

#### 17.3.1 出站类型

| 触发源 | 类型 | 跑哪 | 路径 |
|---|---|---|---|
| T3 strategy 决定下单 | CLOB POST `/order` | **vCPU2 inline** (热路径, 不走 worker pool) | strategy → RiskGateway → signer → infra/net HTTPS send |
| T3 strategy 决定撤单 | CLOB DELETE `/order/{id}` | 同上 | 同上 |
| T2 book_builder 发现 stale (heartbeat) | REST `/book` 兜底 snapshot | vCPU3 worker | T2 → bg_work_ring(MPSC) → T4 → HTTPS → bg_to_book_ring → T2 |
| T0 WebSocket 断线重连成功 | REST `/book` snapshot N 个 market | vCPU3 worker, **bulk** | T0 → bg_work_ring(BulkBookFetchJob, N tokens) → T4 → `POST /books` → bg_to_book_ring |
| T2 book_builder 发现新 market | REST `/books` 拉初始 snapshot | vCPU3 worker, **bulk** | T2 触发, 折叠到下一次 bulk fetch 窗口 |
| T5 周期 gamma poller | REST `/sports/events?limit=500` | vCPU3 周期线程**直接发** | T5 直接 HTTPS, 不入 bg_work_ring (周期任务自己跑) |
| T6 Goalserve inplay | REST 全联盟 inplay | 同上 | T6 直接 HTTPS |
| T8 audit_fsync | local file fsync | vCPU3 | 老韩 RM v0.2 group commit |
| T3 audit 入队 | append audit ring | vCPU2 直接调 audit ring writer (不阻塞) | RM 内部 ring + group commit |

**关键原则**:
- **下单/撤单**走 inline (vCPU2 热路径), 因为 RM evaluate + signer + CLOB write 全程 < 500us 是老姜内环预算的硬目标 (v0.2 §11.1). 这些**不**走 worker pool.
- **数据补偿** (book stale / WebSocket 断线兜底 / 初始 snapshot) 走 vCPU3 worker pool, 异步 fire-and-forget + 回灌
- **周期任务**自己跑 vCPU3 线程, 不挤 worker pool

#### 17.3.2 vCPU3 REST worker pool 设计

| 项 | 选型 |
|---|---|
| 数量 | 4 个 thread (足够并发 Polymarket + Polygon + Goalserve 同时 inflight, 老陈 §11.3 HTTP/2 multiplexing 后单连接已能多路复用) |
| HTTP client | boost::beast (HTTP/1.1) + nghttp2 (HTTP/2 multiplexing for Polymarket gamma/clob); asio coroutine 风格 |
| connection pool | per-host keep-alive, TLS session 复用 (老陈实测跨洋 TLS 握手 ~370ms, 一次握手多次复用是命) |
| rate limiter | per-endpoint governor (e.g. gamma 5 req/s, clob 10 req/s), token bucket |
| retry | 3 次 exponential backoff (100ms / 400ms / 1.6s), 429 / 5xx 触发 |
| timeout | 单 request 5s (跨洋), 总 budget 8s 含 retry |
| single-flight | 见 §17.4, 同 key inflight 共享 |
| 失败处理 | 全部失败 → bg_to_book_ring 投递 `BookFetchFailed{token_id, last_err}`, T2 标 stale 但不 abort |

---

### 17.4 single-flight

#### 17.4.1 适用场景

| 场景 | key | 说明 |
|---|---|---|
| 多 strategy 同时要同一 market data | `token_id` | 1000 strategy 注册同一 token → 实发 1 次 `POST /books?token_ids=[T]` |
| WebSocket 多次断线重连同一 market | `(token_id, reconnect_epoch)` | 同 epoch 内重复请求折叠 |
| 多 RM 决策同时要 nonce 查询 | `wallet_addr` | RPC `eth_getTransactionCount` 折叠 |

不适用场景 (绝对不能 single-flight):
- 下单 (每单 idempotency_key 不同)
- 撤单 (每单 client_order_id 不同)
- nonce 写 (每次递增不同)

#### 17.4.2 C++ 实现草图

```cpp
// infra/net/single_flight.hpp (header-only, vCPU3 background only)
//
// Owner: 老周 主笔 + @小石 lock-free review + @小段 实现
//
// 约束:
//   - 只能在 vCPU3 worker thread 调用, vCPU0/1/2 禁用
//   - Result 必须 trivially copyable POD (老姜 latency 友好)
//   - 单个 SingleFlight 实例服务一个 endpoint type (e.g. PolymarketBookFlight)

template <typename Key, typename Result>
class SingleFlight {
  struct Call {
    std::promise<Result> p;
    std::shared_future<Result> sf;
  };

  std::unordered_map<Key, std::weak_ptr<Call>> inflight_;
  std::mutex mu_;  // 只锁 map 增删, fn() 执行不在锁内

 public:
  // Do(): 多个 caller 传同 key, 实际只跑 fn() 一次
  // fn() 必须可 noexcept (内部捕获错误用 Result encoding)
  std::shared_future<Result> Do(Key k, std::function<Result()> fn) {
    std::shared_ptr<Call> sp;
    bool is_leader = false;

    {
      std::unique_lock lk(mu_);
      if (auto existing = inflight_[k].lock()) {
        return existing->sf;  // 已有 inflight, 直接拿 future
      }
      sp = std::make_shared<Call>();
      sp->sf = sp->p.get_future().share();
      inflight_[k] = sp;  // weak_ptr, 自动清理
      is_leader = true;
    }

    if (is_leader) {
      // 注: 这里不 lk.unlock(); 锁已经在 scope 结束 release
      // fn() 跑出锁, 不 block 其他 key
      Result r = fn();
      sp->p.set_value(std::move(r));

      // 显式清理 (weak_ptr 自动也会, 这里早一步释放 map slot)
      std::unique_lock lk2(mu_);
      auto it = inflight_.find(k);
      if (it != inflight_.end() && it->second.expired()) {
        inflight_.erase(it);
      }
    }

    return sp->sf;
  }
};

// 使用示例 (vCPU3 worker T4):
//   static SingleFlight<TokenId, BookSnapshot> g_book_flight;
//   auto fut = g_book_flight.Do(token_id, [&]() {
//     return http_client.post_books({token_id}).snapshot();
//   });
//   BookSnapshot snap = fut.get();  // 多 worker 并发, 只一次实发
```

**关键 review 项 (派 @小石)**:
1. `std::mutex` 在 vCPU3 上 OK (不在热路径), 不需要换 spinlock
2. `weak_ptr` 自动清理保证不漏内存, 但需测试 leader 异常 (fn() throw) 时 promise 是否 broken
3. `fn()` 必须 noexcept-like (内部 try/catch 把 error 包进 Result)
4. 跨 vCPU 调用: 严禁 vCPU0/1/2 直接调 g_book_flight.Do(), 必须通过 bg_work_ring 派单到 vCPU3

#### 17.4.3 测试用例 (派给小宋, §17.7)

- 1000 个 vCPU2 strategy 模拟器同时要 token X 的 book → bg_work_ring 折叠 → 实测 HTTPS request 数 = 1
- 注: vCPU2 不直接调 SingleFlight, 而是入 bg_work_ring; T4 内部用 SingleFlight 折叠. 因此 bg_work_ring 上仍有 1000 个 job, 但 T4 出口只 1 个 REST.

---

### 17.5 bulk endpoint 路由

#### 17.5.1 路由原则

| 数据需求 | 反模式 (per-market) | 正模式 (bulk) | endpoint |
|---|---|---|---|
| 初始拉 N market book snapshot | for each m: `GET /book?token_id=Tm` (N+1 反模式) | 一次 `POST /books` body=[{T1},...,{TN}] | Polymarket CLOB `/books` |
| 拉 event/market 列表 | for each event: `GET /events/{slug}` | `GET /sports/events?closed=false&limit=500` | gamma `/sports/events` |
| 拉 inplay 比分 | for each market: REST poll | 一次拉全联盟 inplay | Goalserve `/<sport>/inplay` |
| 拉 pregame odds | for each match: REST | 一次拉全联盟 pregame | Goalserve `/<sport>/odds` |
| 拉 nonce | 每次单独 RPC | WebSocket `newPendingTransactions` + 5s 兜底批 RPC | Polygon WebSocket |

#### 17.5.2 协同 Wave 10 老李 + 小段

Wave 10 在跑 `api-call-optimization-v1`. 他们出 endpoint **分类矩阵**:
- 哪些是 bulk endpoint (一次覆盖 N 个)
- 哪些是 per-market only (无 bulk 版)
- 哪些是 WebSocket 取代 (不应该 REST)
- 哪些有 rate limit (限流配额)

本章 v0.3 是**调用模式**: 给定一个数据需求, 优先级:
1. 有 WebSocket subscribe? → 走 WebSocket (T0/T1 reactor)
2. 有 bulk REST? → 走 vCPU3 周期 / 批量 worker
3. 只有 per-market REST? → 走 vCPU3 worker + single-flight 折叠 + 合并 batch 窗口

合并 batch 窗口: 若 5ms 内连续来 10 个不同 token 的 fetch 请求, T4 合并成一次 `POST /books` body 10 个 token. 实现: bg_work_ring 消费侧不是 1:1 dispatch, 而是带 batch buffer + 5ms timeout.

#### 17.5.3 反模式 PR-reject (老高 R-12 enforcement)

任何 PR 出现以下代码模式 → 直接 reject:
- `for (auto& m : markets) { http.get("/book?token_id=" + m.token); }` (N+1)
- `for (auto& m : markets) { spawn_thread([&](){ /*fetch m*/ }); }` (N 个 thread)
- 任何在 hot path 直接发 HTTP request

---

### 17.6 REST 兜底机制 (WebSocket 断线时)

#### 17.6.1 触发条件

| 触发 | 检测线程 | 响应 |
|---|---|---|
| WebSocket 30s 无任何 message | T0 (heartbeat) | T0 标记 reconnect, 同时通知 T2 (via SPSC) 当前所有市场 stale |
| WebSocket reconnect 成功 | T0 | T0 入 bg_work_ring 一个 BulkBookFetchJob, 列出所有订阅的 token_id |
| heartbeat watchdog 单 market stale > 阈值 | T2 (book_builder) | T2 入 bg_work_ring 单 token 兜底 (走 single-flight) |
| 启动期初始化 | T2 启动逻辑 | 入 bg_work_ring BulkBookFetchJob, 一次拉全部 |

#### 17.6.2 BulkBookFetchJob 处理

```
T4 worker 收到 BulkBookFetchJob{token_ids: [T1,...,TN]}
  ↓ split by batch_size=50 (Polymarket /books 单次 batch 上限, 待 Wave 10 老李确认)
  ↓ 每 batch 起一个 HTTPS request (concurrent inflight = 4, 限流 token bucket)
  ↓ 每 batch 结果 push bg_to_book_ring (T2 消费)
  ↓ 任意 batch 失败 → 重试 (max 3) → 失败上报 stale + alert
```

#### 17.6.3 不影响别的 WebSocket 流

R-12 红线: WebSocket 断线只影响断的那一条流. 其他 WebSocket 流 (Polygon WebSocket / 其他 market channel) 继续跑.
- T0 重连用 boost::asio coroutine, 不 block 自己; 即使 reconnect 卡 3s, T0 reactor 仍跑 epoll_wait
- 等等, T0 只有一个 reactor — 多 WebSocket 流怎么共存? 答: T0 是 Polymarket 单 WebSocket connection (multi-channel multiplex), Polygon WebSocket 是 T1 单独 connection. 单个 reactor 内不同 channel 用 channel_id 区分, 任一 channel 断不影响别的 channel; 整个 connection 断时 T0 reconnect 用非阻塞 connect + retry timer.

---

### 17.7 测试验收准则 (小宋接口)

派单: @小宋 在 `xiaosong-test-replay-framework-v0.2` 加测试 case.

#### 17.7.1 场景 1: REST 慢响应不阻塞 WebSocket event loop

```
Setup:
  - 注入 mock HTTPS server, 对 /book/* 返回延迟 5s
  - 启动 stcpp-trader
  - vCPU2 strategy 模拟器主动触发 100 个 REST /book 兜底
Run:
  - 同时灌入 1000 个 WebSocket book update message (10 ms/条) 到 vCPU0
Assert:
  - vCPU0 WebSocket message → wss_in_ring p99 < 50us
  - vCPU0 在 REST 慢响应整个 5s 窗口内 CPU 利用率不饱和, 处理速度不降
  - bg_work_ring 阻塞 / 满 → drop + counter 增长, 但 vCPU0 不受影响
  - 没有任何 sync REST 调用在 vCPU0
Pass criteria:
  - p99 < 50us, 100% pass
  - 老陈 cpu_pin profiling 显示 vCPU0 时间不耗在 syscall / mutex
```

#### 17.7.2 场景 2: single-flight 折叠

```
Setup:
  - 1000 个 vCPU2 strategy 模拟器, 同时要 token X 的 book snapshot
  - mock HTTPS server 统计 /books 接收次数
Run:
  - 同一 ms 内 1000 个 strategy 触发 fetch
Assert:
  - mock server 实收 /books request 次数 = 1 (single-flight 完全折叠)
  - 1000 个 strategy 都拿到同一个 BookSnapshot 数据
  - 折叠耗时 < 10ms (跨洋 RTT 5ms + processing)
Pass criteria:
  - request count == 1
  - 全部 callback 返回时间差 < 1ms
```

#### 17.7.3 场景 3: WebSocket 断线 + REST 兜底 bulk

```
Setup:
  - 订阅 5 个 market (token T1..T5)
  - 注入 WebSocket server 断线 (close socket from server side)
Run:
  - WebSocket 重连成功后, 观察 REST /books 调用
Assert:
  - 实发 REST 调用 1 次 (bulk), body = [T1, T2, T3, T4, T5]
  - 不出现 5 次单独 /book?token_id=Tn 调用 (N+1 反模式)
  - bg_to_book_ring 收到 1 个 batch result, T2 一次性更新 5 个 market
  - 重连 + 兜底总耗时 < 500ms (跨洋 RTT * 2 + processing)
Pass criteria:
  - request count == 1
  - 数据一致性: 5 market 全有新 book, hash 匹配
```

#### 17.7.4 场景 4: vCPU0 红线违例检测

```
Setup:
  - PR 模拟: 在 T0 reactor 内插入 std::ofstream out("/tmp/x"); out << "..." (违例)
Run:
  - CI 跑 R-12 静态扫描 (老练 S1-024 v0.3)
Assert:
  - CI 在 PR check 阶段 reject, 列出违例文件 + 行号
  - PR 不能 merge
Pass criteria:
  - reject rate 100% (false negative = 0)
```

---

### 17.8 红线 R-12 enforcement (老高 PR review)

派单: @老高 在 `code-conventions-v1.1` 加 R-12 检查清单.

#### 17.8.1 静态扫描规则 (CI step, owner 老练 S1-024 v0.3)

| 规则 | 检查 | 工具 | 违例处理 |
|---|---|---|---|
| R-12-1 | vCPU0 文件 (`src/data/ingest/poly_wss/*` / `src/data/ingest/polygon_wss/*`) 禁止 include `<curl/*>` `<boost/beast/http*>` `<grpcpp/*>` `<rocksdb/*>` | grep + AST | CI reject |
| R-12-2 | vCPU0 文件禁止调用 `::fsync` `::fdatasync` `::open` (除 `O_NONBLOCK`) `::write` (除 socket fd) | clang AST matcher | CI reject |
| R-12-3 | vCPU0 文件禁止持 `std::mutex` `std::shared_mutex` 时间 > 100us (静态: 禁止 mutex; 运行时: 加 latency assertion) | clang grep + RDTSC wrapper | CI reject (静态) + abort (运行时) |
| R-12-4 | 任何 `for (m : markets) http.get(...)` 模式 | clang AST matcher | CI reject (N+1 反模式) |
| R-12-5 | ring `try_push` 失败必须有 counter + log, 不允许 `push_blocking()` | grep "push_blocking" + AST | CI reject |
| R-12-6 | vCPU0 文件禁止 `std::cout` `printf` (走异步 log) | grep | CI reject |

#### 17.8.2 运行时检查

| 检查 | 实现 | 触发动作 |
|---|---|---|
| vCPU0 WebSocket message → ring buffer p99 监控 | infra/metrics histogram `wss.recv_to_ring_us` | > 50us 持续 10s = alert P1 |
| vCPU0 mutex hold time | RDTSC wrapper around lock/unlock (debug build only) | > 100us = abort + core (dev), > 100us = alert (prod) |
| ring drop counter | `wss.ring_full_drop_total` | rate > 1/min = alert P1 |
| single-flight 折叠率 | counter pair: (Do 调用数 / fn 执行数) | 折叠率 < 95% = alert P2 (调查异常分散) |
| vCPU0 syscall profile | perf trace (cron) | 出现 write/fsync = alert P0 |

#### 17.8.3 PR review checklist (老高)

任何 touching `src/data/ingest/poly_wss/` 或 `src/data/ingest/polygon_wss/` 的 PR, review 必经:
- [ ] R-12-1 ~ R-12-6 全部静态扫描 pass
- [ ] 新增任何函数调用 → 列出该函数 transitive 是否 sync IO / mutex / fsync
- [ ] 新增任何 ring push → 必须 try_push + counter
- [ ] 老高 + 老周 双签 (R-12 红线区)

---

### 17.9 残留 OPEN (派单)

| # | 议题 | 期限 | owner |
|---|---|---|---|
| OQ-R12-1 | Polymarket `/books` 批量上限 (single request 多少 token?) | Sprint-1 末 | @老李 + @小段 (Wave 10) |
| OQ-R12-2 | Goalserve inplay 单次响应字节数 + 解析延迟 (vCPU3 是否吃得下 1-3s 周期) | Sprint-1 末 | @小段 |
| OQ-R12-3 | bg_work_ring 容量 4096 是否够 (峰值场景模拟) | Sprint-2 | @老姜 + @小石 |
| OQ-R12-4 | Polygon WebSocket subscribe 模型 (newPendingTx + 5s 兜底批 RPC) 实测 | Sprint-2 | @老叶 |
| OQ-R12-5 | SingleFlight std::mutex 是否需换 spinlock (vCPU3 高并发场景) | Sprint-2 | @小石 |
| OQ-R12-6 | 周期轮询 T5/T6/T7 内部直接发 HTTPS, 不走 bg_work_ring, 是否需 single-flight 保护 | Sprint-1 末 | @老周 复核 + @老李 |
| OQ-R12-7 | T0 reactor 多 channel multiplex 时的 channel-level isolation (一个 channel 卡死不影响别的) | Sprint-2 | @老陈 + @老周 |

---

### 17.10 与 Wave 10 协同口径

老李 + 小段 `api-call-optimization-v1` (在跑) 出 endpoint 矩阵后, 本章 §17.5 路由表会**用他们的矩阵填充**. 现在的 §17.5 是骨架, 具体 endpoint 名 + bulk 上限 + rate limit 由 Wave 10 supply.

具体接力点:
1. 老李给 Polymarket endpoint 列表 (含 bulk 上限 + rate limit + WebSocket 等价物) → 本文 §17.5 路由表 v0.3.1 填实
2. 小段给 Goalserve endpoint 列表 (inplay / pregame 全联盟 batch 大小 + 限流) → §17.5 + §17.6 填实
3. 老叶给 Polygon RPC 列表 (WebSocket 优先 vs RPC 兜底 + 限流) → §17.5 填实

我 (老周) 不抢这部分, Wave 10 出我就把矩阵贴进来.

---

## 18. 附录 (沿用 v0.2 §18)

### 18.1 命名 / 目录约定

v0.2 §18.1 不变, **新增**:

```
src/
  data/ingest/
    poly_wss/        # vCPU0 only, R-12 红线区 (老高 PR 双签)
    polygon_wss/     # vCPU0 only, R-12 红线区
  infra/net/
    http_client/     # vCPU3 only, HTTP/2 connection pool + retry + rate limit
    single_flight/   # vCPU3 only, header-only
  bg/                # 新目录, vCPU3 background pool
    rest_worker/     # T4
    periodic/        # T5/T6/T7 周期轮询线程
    fsync_worker/    # T8/T9
```

### 18.2 与 ticket 的对应 (v0.3 增量)

- S1-R12-001: vCPU0 WebSocket event loop 实现 + R-12 静态扫描接入 (老周 + 老练)
- S1-R12-002: SingleFlight C++ 实现 + 单测 (老周 + 小段 + 小石)
- S1-R12-003: bg_work_ring 设计 + batch dispatch 实现 (老周 + 小石)
- S1-R12-004: vCPU3 worker pool + HTTP/2 multiplexing 接入 (老陈 + 小段)
- S1-R12-005: 周期轮询 T5/T6/T7 实现 (小段 + 老李)
- S1-R12-006: 测试 3 场景 (小宋)
- S1-R12-007: R-12 PR review SOP (老高)

---

**评审请求**:

- **老郭 (架构评审)**: §17 章节完整性, R-12 → §17 对照表 (§0.2), 线程模型 §17.1.1 12 线程在 4 vCPU 是否合理 (尤其 vCPU3 8 线程是否会内部 starvation)
- **老韩 (RM 接口)**: §17.3.1 表中 RM evaluate 在 inline (vCPU2) 仍然 < 200us 是否成立 (audit WAL 群提交在 vCPU3 T8 是否引入跨核)
- **老陈 (网络)**: §17.2 vCPU0 WebSocket p99 < 50us 是否可达 (老姜原 73us, 我砍到 50us 靠 simdjson on-demand schema-aware, 你 S1-021 帮验)
- **老姜 (latency)**: §11.3 新预算表是否覆盖你的内环 + 外环 budget, 是否需要调

**会签**:
- 老周 (主笔): __签 (2026-05-28)__
- 老郭 (评审): ____
- 老韩 (RM 接口对齐): ____
- 老陈 (网络验证): ____
- 老姜 (latency 验证): ____
- 老雷 (GM, R-12 立人, 终签): ____
