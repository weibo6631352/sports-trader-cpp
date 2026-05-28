# Meeting β — 模块拆分 + 依赖图 + 关键技术选型

**主持**: 主 agent (Meeting β chair)
**参会**: 首席架构师 / 高频系统 / 网络协议 / 数据序列化 / 持久化 / 加密签名 / Polymarket 协议 / 风控 / 可观测性 / 现代 C++ 顾问
**议程**: F (deep dive)
**约束基线**: CLAUDE.md §0 (CN→US RTT 150–400ms, 带宽小, WS 是决策核心链路) / §4-§7 / R39 PoC (cpp_poc/main.cpp + rust_poc/src/main.rs)

---

## 1. 模块树 + CMake target

继承 Python `src/polymarket_trader/{api,app,domain,workflow,pipeline,recovery,infra,observability,runtime,sports}` 分层，C++ 重写按 **依赖单向 + Domain 零依赖** 落 CMake static lib。

| 模块 | CMake target | 职责 | 上游 | 下游 |
|---|---|---|---|---|
| `domain` | `st_domain` (STATIC) | Decimal / Price / Token / Order / Position / TradingDecision 纯值类型, **零外部依赖** (无 boost / 无 SDK) | — | 被所有人依赖 |
| `infra_net` | `st_infra_net` | Boost.Asio io_context + HTTP/2 (nghttp2) + WSS (Beast) + TLS (BoringSSL) 通用底座 | domain | infra_polymarket / infra_sports |
| `infra_polymarket` | `st_infra_pm` | gamma / clob / data-api REST client + market WS client + OrderExecutor (EIP712 sign + POST /order) | infra_net, domain, crypto | pipeline, recovery, app |
| `infra_sports` | `st_infra_sports` | goalserve inplay gz / livescore / pregame client | infra_net, domain | pipeline_ingest, sports |
| `infra_db` | `st_infra_db` | libpqxx connection pool, audit_events / fills append-only writer | domain | observability, app (operator query) |
| `crypto` | `st_crypto` | secp256k1 + EIP712 typed-data hash, 隔离不安全 C lib | domain | infra_polymarket |
| `runtime` | `st_runtime` | StateStore (orderbook / position / account snapshot 分片), scheduler, supervisor, lock-free SPSC/MPSC queues | domain | 被 pipeline/recovery/app 读 |
| `sports` | `st_sports` | 直播状态机 (LiveEventStore + 状态推导 quarter/period/serve/inning) | domain, runtime | pipeline_decision |
| `pipeline` | `st_pipeline` | ingest (WS dispatcher / inplay polling) + decision (MarketTickWorker / DecisionContextBuilder) + execution (OrderGateway) | domain, runtime, infra_*, workflow | app |
| `workflow` | `st_workflow` | TradingWorkflowConfig (编译期常量 struct), QuantDecider, estimate_signal | domain | pipeline |
| `recovery` | `st_recovery` | 周期 reconcile / settlement / orphan position 修复, 走 OrderGateway | domain, infra_*, runtime, pipeline (gateway) | app |
| `app` | `st_app` | 用例编排: TradingApp::start, OperatorService, AccountReconcileService | 上述全部 | api |
| `api` | `st_api` | HTTP server (Beast) + JSON DTO 序列化 (operator 端点), **不直接持热写锁** | app | — |
| `observability` | `st_obs` | prometheus exporter + structured log (spdlog) + audit_event outbox | domain | 被所有人 link |
| `bin/trader` | EXECUTABLE | main: 装配 io_context + 启动 app | api, app | — |

### ASCII 依赖图

```
                       +-------------------+
                       |    bin/trader     |
                       +---------+---------+
                                 |
                  +--------------+--------------+
                  |             api             |
                  +--------------+--------------+
                                 v
                  +--------------+--------------+
                  |             app             |
                  +-----+--------+--------+-----+
                        |        |        |
            +-----------v--+   +-v------+ +v---------+
            |   pipeline   |   |recovery| | workflow |
            |ingest/decide/|   |        | |  decider |
            |  execute     |   |        | |          |
            +--+--------+--+   +---+----+ +----+-----+
               |        |          |           |
        +------v---+ +--v------+   |           |
        | runtime  | |infra_*  |<--+           |
        | (stores) | |(net/pm/ |               |
        |          | | sports/ |               |
        |          | |  db)    |               |
        +----+-----+ +----+----+               |
             |            |                    |
             |   +--------v---+  +--------+    |
             |   |  crypto    |  |sports  |    |
             |   +-----+------+  +---+----+    |
             |         |             |         |
             |         v             v         |
             |    +----+-------------+----+    |
             +--->+        domain         +<---+
                  +-----------------------+
                              ^
                              |
                  +-----------+-----------+
                  |    observability      |
                  +-----------------------+
```

**单向规则**: 箭头朝下，domain 不向上引用; `observability` 是横切 (类似 logger sink), 不能反向回调业务。`runtime` 持有共享状态、`infra_*` 持有外部协议、二者都依赖 `domain` 但互不相依——`runtime` 不知 polymarket 协议存在。

---

## 2. 关键技术选型

### 选型: Async runtime
**候选**: Boost.Asio / C++20 coroutines on Asio / Seastar
**Asio 立场** (高频系统工程师): executor model 成熟, thread-per-core 可手控, 14 年生产验证, R39 cpp_poc 已隐式用 libcurl 同步, 升级到 Asio 直接上 io_uring/kqueue.
**Seastar 反对方** (现代 C++ 顾问 主张): 真正 share-nothing thread-per-core + future/promise 性能标杆. **架构师反对**: Seastar 强制重写所有 IO, 链接生态窄 (libpqxx/nghttp2 都得 wrapper), 团队 ramp-up 6 周以上, RTT 200ms 链路下单机吞吐根本不是瓶颈.
**决议**: **Boost.Asio 1.84+ + C++20 coroutines** (`asio::awaitable<T>` + `co_spawn`). 理由: §0 瓶颈是 RTT 不是 CPU, Asio 已能 100k+ concurrent connection; coroutine 让代码读起来像 Python async, P0 路径仍可走 callback 避免 frame alloc. **复盘触发**: 单 io_context 调度延迟 P99 > 5ms 或 thread-per-core 真实需要时.

### 选型: HTTP/2 client
**候选**: nghttp2 (raw) / Boost.Beast / cpp-httplib
**网络协议工程师**: gamma + clob REST 全是 HTTP/2 (POC 已 `CURL_HTTP_VERSION_2_0`). cpp-httplib **直接淘汰** — 无 h2. Beast h2 支持是实验性 (官方文档明确说不完整).
**nghttp2 立场**: 标杆库, curl/Envoy 都用它, 真 multiplex 单 TCP 同时跑 gamma + clob 省 TLS handshake.
**反对方** (架构师): nghttp2 是 C API, callback 风格, 集成 Asio 要写 200+ 行 glue.
**决议**: **nghttp2 + 自写 Asio adapter** (~300 行, 一次性). 复用 Asio TLS stream, multiplex gamma/clob/data-api 三条 host 共 3 个 H2 connection. **复盘**: adapter 写完 1 周, 跑通 ≥ 99.5% 请求成功率才推广; 否则降级到 Beast h1.1 + keep-alive.

### 选型: WSS
**候选**: Boost.Beast / uWebSockets
**Beast 立场** (网络协议工程师): 跟 Asio 一脉, 复用 TLS stream + io_context, market_ws 单连接根本不需要 uWS 的百万连接性能.
**uWebSockets 反对**: 性能 benchmark 高 10x. **架构师**: 我们只 1 个 WSS connection (`wss://ws-subscriptions-clob.polymarket.com/ws/market`), 10x 0 = 0.
**决议**: **Boost.Beast WSS over BoringSSL stream**. 复盘: 不触发.

### 选型: TLS
**候选**: BoringSSL / LibreSSL / OpenSSL 3 / rustls-ffi
**R39 数字**: cpp_poc 用 macOS LibreSSL, 跑 1h+ RSS 平稳; rust_poc 用 rustls RSS 同样稳. Python 用 OpenSSL 3.x **观测到 RSS 泄漏** (memory.md R39 结论)
**LibreSSL 立场** (加密签名专家): API 兼容 OpenSSL, BoringSSL 没有稳定 API + ABI 持续变.
**BoringSSL 反对** (现代 C++ 顾问): Google 实战, ALPN/h2/0-RTT 一流, 但要从源码 build (vendor as submodule).
**rustls 反对**: FFI 边界 + 双语言 build 复杂度.
**决议**: **BoringSSL vendor submodule + 自管 build** (CMake `add_subdirectory(boringssl)`). 关键理由: nghttp2 + h2 + ALPN 在 BoringSSL 测试覆盖最好, OpenSSL 已被 R39 暴露泄漏风险. **复盘**: BoringSSL API 大破坏性变更时切 OpenSSL 3.2+.

### 选型: JSON
**候选**: simdjson / nlohmann/json / RapidJSON / glaze
**数据序列化工程师**: gamma `/events?live=true` 单次 payload 50KB–2MB (R39 实测), 每 2s 拉一次. nlohmann **直接淘汰** — DOM 拷贝重, 同样数据吃 3-5x RAM.
**simdjson 立场**: 零 alloc on-demand API, 比 nlohmann 快 25x. 但只读 — 写出 JSON 需要别的库.
**glaze 反对方 (现代 C++ 顾问)**: 编译期反射 → struct 直接绑定, 比 simdjson 易用, 性能接近.
**决议**: **simdjson 解析 + glaze 序列化** 双栈. 入站 (gamma / clob / WS) 走 simdjson on-demand zero-copy → 直接填 domain struct; 出站 (operator API response, audit_event JSON) 走 glaze. **复盘**: glaze 出 stability 问题切 RapidJSON Writer.

### 选型: DB driver
**候选**: libpqxx / SOCI / 裸 libpq
**持久化工程师**: 我们只用 PostgreSQL (Python 现状), DB 只写 audit_events / fills, **零运行时读** (CLAUDE.md §3 状态真相). SOCI 跨 DB 抽象是无效成本.
**裸 libpq 反对** (架构师): 重复造 connection pool 轮子.
**决议**: **libpqxx 7.x + 自建 outbox worker** (单独 thread, MPSC queue 接 audit_event). 复盘: 不触发.

### 选型: Build
**候选**: CMake / Bazel / xmake
**首席架构师**: BoringSSL / nghttp2 / Beast / simdjson **全部 CMake 一等**. Bazel 跨语言强但本项目纯 C++, 引入 Bazel 团队学习成本高.
**Bazel 反对** (现代 C++ 顾问): hermetic + remote cache 香. **架构师**: hermetic 用 vcpkg manifest 就够, 我们 30 人内不需要 RBE.
**决议**: **CMake 3.27+ + vcpkg manifest mode**. 全部依赖锁版本进 `vcpkg.json`. 复盘: build time > 10 min 或多平台兼容崩溃时评估.

### 选型: C++ standard
**候选**: C++17 / C++20 / C++23
**现代 C++ 顾问**: C++20 coroutine + concepts + ranges + `std::span` 是写 Asio 异步链路的最大生产力跳跃; `std::expected` 在 C++23, 暂用 `tl::expected` 兜底.
**C++17 反对方** (架构师): Apple Clang 对 C++20 module / coroutine 支持仍有坑.
**决议**: **C++20 主线 + 选用 C++23 库特性 (`std::expected` via tl::expected polyfill)**. 编译器: Clang 17+ / GCC 13+. **复盘**: 触发 module 卡 build 时退回 header-only.

### 选型: Memory allocator
**候选**: system malloc / mimalloc / jemalloc / tcmalloc
**R39 关键数字**: Python 用 OpenSSL+glibc malloc **RSS 持续增长**; Rust PoC 用 **mimalloc + rustls RSS 稳定**; C++ PoC 用系统 malloc + LibreSSL 短期稳定.
**高频系统工程师**: 长跑 (>24h) 数据必须重测; 但 R39 已强提示 allocator 选择影响 RSS.
**jemalloc 立场**: 久经考验 (Facebook/Rust 一度默认), arena 隔离好.
**mimalloc 反对/支持**: 微软, 多核 scaling 更现代; Rust PoC 验证有效.
**决议**: **mimalloc as override** (`LD_PRELOAD` + CMake link). 与 R39 Rust PoC 同栈, 数据可对比. **复盘**: 24h 长跑 RSS 增长 > 50MB 切 jemalloc 重测.

---

## 3. 核心数据结构

### Orderbook (token_id → book)
Python `dict[price, size]` per level 是反例 — 每 tick 重新 dict alloc.
**C++ 形态**:
```cpp
struct Level { Price px; Size sz; };  // 16 byte 紧凑
struct Book {
    std::array<Level, 32> bids;  // 固定 32 档, 栈分配
    std::array<Level, 32> asks;
    uint8_t bid_n, ask_n;
    uint64_t seq;                // sequence_gap 检测
    std::atomic<uint64_t> version;  // RCU 读端无锁
};
```
WS `book` / `price_change` 增量 → in-place 更新 + version++. 读端 (decision) 通过 `version` snapshot 读, 不持锁.

### WS message buffer
**对象池**: `boost::pool<>` 预分配 1024 个 4KB chunk; WS frame 完整 ≥ 4KB 才升级到 heap. R39 cpp_poc `buffer.reserve(4MB)` 是一次性 reserve, 复用同 std::string.

### Position / Order in-memory cache
按 `token_id` 分 64 个 shard, 每 shard `std::shared_mutex` (读多写少). 读 (decision / operator) 拿 shared lock; 写 (fill_recorded / reconcile_applied) 拿 unique. 跨 shard 操作禁止 (CLAUDE.md §7 "无全局大锁").

### Account snapshot (single writer)
`UserAccountPoller` 是唯一 writer (live mode), `PaperBalanceSyncer` 是唯一 writer (paper mode). 双 writer 已是 R39 之前的事故根因 (CLAUDE.md §17.3). C++ 用 `std::atomic<std::shared_ptr<AccountSnapshot>>` (C++20 atomic shared_ptr), writer swap, reader load — RCU 风格零锁.

---

## 4. 关键 invariant (写代码前必须冻结)

1. **P0 热路径零 heap alloc**: `MarketTickWorker::on_tick` → `quant_decide` → `OrderGateway::review_intent` → `OrderExecutor::submit` 路径上**禁用 std::string 拼接 / std::vector push_back / 新 shared_ptr 构造**. 临时数据走 thread-local arena (`std::pmr::monotonic_buffer_resource`, frame 末尾 `release`).
2. **同步 / 异步边界**: io_context thread = network + ws decode + json parse; decision thread = quant_decide + risk + executor.submit (同步入队 outbox); persistence thread = libpqxx write. 三者通过 lock-free SPSC/MPSC queue 通信, **不共享 lock**.
3. **状态读写并发模型**: orderbook = RCU (atomic version); position/order = shard + shared_mutex; account = atomic shared_ptr. **禁用全局 `std::mutex`**.
4. **logger 必须 lazy 守门** (CLAUDE.md §7): `if (spdlog::should_log(level)) log(...)` — 不写 `LOG_DEBUG(fmt::format(...))` 这种 eager format. P0 路径 audit 走 outbox queue, 不走 logger.
5. **Domain 零依赖**: `st_domain` CMake target `target_link_libraries` 必须为空 — 编译期阻挡 boost/spdlog/pqxx 渗透.
6. **Operator endpoint < 2s** (CLAUDE.md §17.9): `/runtime` 大聚合 ≤ 50KB; 内部 cold cache fetch (gamma profile 等) 必须 ≤ 0.3s timeout.
7. **OrderExecutor 唯一性**: `OrderExecutor::submit` 是私有 friend-only 接口, `OrderGateway` 是唯一调用方; 静态断言 `static_assert(std::is_same_v<Caller, OrderGateway>)` 风格 (用 CRTP token 强制).

---

## 5. 下一步落地动作

| # | 动作 | 负责 | 验收 |
|---|---|---|---|
| 1 | 创建 CMake 骨架 + vcpkg.json 锁版本 (boringssl/nghttp2/beast/asio/simdjson/glaze/libpqxx/spdlog/mimalloc/tl-expected) | Senior IC #1 | `cmake --build` 跑空 main |
| 2 | helloworld: Asio + nghttp2 + BoringSSL GET gamma `/events?live=true` 跑通 1h, 对照 R39 cpp_poc RSS 曲线 | Senior IC #2 (网络) | RSS 1h 平稳 < 100MB, P99 latency < 500ms |
| 3 | helloworld: Beast WSS + BoringSSL 订阅 market WS, 解 frame 计 msg/s | Senior IC #3 (WS) | 与 Python 同 token_ids 订阅, msg/s ≥ Python 95% |
| 4 | domain 骨架: Price / Token / Order / Position struct + Decimal (boost::multiprecision::cpp_dec_float<10>) | Senior IC #4 (domain) | header-only, link 测试为空 |
| 5 | crypto: secp256k1 + EIP712 typed-data hash, 对照 py-clob-client 生成同 order hash 字节级 byte-equal | 加密签名专家 + Senior IC #5 | hash 输出 bit-identical 100/100 fixture |
| 6 | mimalloc 24h 长跑 (1+2+3 合并 binary), RSS 曲线落 grafana | 可观测性 + Senior IC #6 | 24h RSS 增长 < 50MB |

**Meeting γ 触发**: 上述 1–3 全部通过后召开, 议程 = P0 决策热路径详细时序 + RCU/shard 模型代码评审.

---

**字数**: ~2350
**完整决议生效**, 任何改动需新会议覆盖.
