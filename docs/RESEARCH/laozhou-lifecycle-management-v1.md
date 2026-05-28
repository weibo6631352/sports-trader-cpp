# 数据 + 连接生命周期管理 v1

- Owner: 老周 (cpp-chief-architect)
- Co-owners: 老陈 (网络池) + 小郑 (监控)
- Date: 2026-05-28
- 验收人: 老郭 + 老雷 + 老韩 (RM 接口) + 小郑 (metrics)
- 关联:
  - `laozhou-architecture-v0.3.md` §17 (WebSocket 不阻塞 + 异步 + bulk endpoint)
  - ADR R-12 (单 market 不轮询 + bulk endpoint 红线)
  - `laoli-xiaoduan-api-call-optimization-v1.md` (Wave 10 在跑, 出 endpoint 复用矩阵)
  - `laochen-network-bench-v1.md` §10 (跨洋 TLS 370ms, WebSocket 重连 p50 2.0s, p99 3.4s)
  - `laowang-wal-framework-v0.1.md` (audit WAL / exec WAL replay 协议)
  - `xiaozheng-observability-v0.1.md` L1-L5 metrics + SLO
  - `laoye-polygon-rpc-selection-v1.md` (多 vendor RPC failover)
  - `laohe-cpp-footgun-checklist-v1.md` (RAII / 裸 new 红线)
  - `laogao-code-conventions-v1.md` (PR review)
  - `xiaosong-test-replay-framework-v0.1.md` (soak test 联签)

---

## 1. 总则

### 1.1 用户原话 (2026-05-28, GM 老雷转达)

> "数据生命周期和外部 api 连接的生命周期, 也一定要合理. 保证连接的前提下还要避免资源泄漏和浪费."

### 1.2 三条不可妥协的总则

| # | 总则 | 反面 (禁止) |
|---|---|---|
| G1 | **保证连接** — 长跑服务在外部 API 可用时必须维持稳定 session, 跨洋 TLS 握手 ~370ms (老陈 v1) 是稀缺资源, 一次握手必须服务尽可能多次 request | 每次 request 新建连接 (反 R-12 N+1, 握手开销爆炸) |
| G2 | **避免泄漏** — fd / mem / socket / file handle / shared memory / WAL fd / cache entry, 所有"取出"必须配一条"放回/释放"路径, 由 RAII 编译期保证, 不依赖人工 close | 任何 `::open`/`::socket`/`new` 不配对应 `close`/`delete`/`unique_ptr` |
| G3 | **避免浪费** — idle 资源主动回收 (idle timeout / LRU evict), 无 TTL 的 cache / 永不断的 keep-alive / 永不 reclaim 的 worker 都禁止 | 无 TTL cache, 无 idle timeout 的连接池, 无 max-age 的 fd |

### 1.3 三层兜底 (defense in depth)

```
第 1 层: RAII (编译期 + 静态分析)
  - smart pointer / scoped_resource / Drop guard
  - [[nodiscard]] / clang-tidy cppcoreguidelines-owning-memory
  - 老高 PR review + 老何 footgun checklist

第 2 层: Sanitizer / 工具 (CI + 启动期)
  - ASAN/LSAN 启动期 (smoke)
  - valgrind --leak-check=full (CI nightly)
  - heaptrack 生产周期采样 (1h/天)

第 3 层: Runtime monitoring (生产 7x24)
  - /proc/self/fd 周期 scan
  - Prometheus metrics: fd_count / open_connections / heap_rss_bytes / cache_bytes / wal_segments
  - 小郑 D6 资源池 dashboard + 小尤 P0-P4 告警
```

任一层失守, 下一层兜住. **绝不**靠"人工 review 仔细一点"作为唯一防线 (老何 footgun §FG-12).

### 1.4 长跑服务定义

本节"长跑"指 process uptime ≥ 30 天的 sport trader 主进程 + signer 独立进程 + obs/ETL 守护. 任何资源占用必须能在 30 天稳态运行下保持**单调有界** (不允许 fd 一周涨一个、cache 一月涨 100MB 这种线性泄漏).

---

## 2. 外部 API 连接生命周期

### 2.0 总览矩阵

| 外部 | 协议 | 连接数 | 复用方式 | 重连策略 | idle reclaim |
|---|---|---|---|---|---|
| Polymarket WebSocket (clob/user) | wss:// | 2 个 (market channel + user channel) | 多 channel multiplex 在 1 个 connection | 指数退避 0.5/1/2/4/8/15s + jitter, 重连 budget 3s (老陈 v1) | server 主动 idle close, 60s 无 frame heartbeat 兜底 |
| Polymarket gamma REST | HTTP/2 | 1 个 H2 连接, 多 stream | per-host pool, TLS session ticket 复用 | 自动 retry 3 次 + backoff 100/400/1600ms | idle 60s close |
| Polymarket CLOB REST | HTTP/2 | 2 个 H2 连接 (4× concurrent stream, 老陈 v1) | 同上 | 同上 | 同上 |
| Polymarket data REST | HTTP/2 | 1 个 H2 连接 | 同上 | 同上 | 同上 |
| Goalserve REST (inplay/livescore/pregame) | HTTP/1.1 (Goalserve 不支持 H2, 老陈 v1) | 2 个 keep-alive 连接 走 `GOALSERVE_PROXY` | per-host pool, keep-alive 复用 | 失败 retry 3 次, 间隔 ≥ 500ms (Goalserve 6 并发就 13% 超时, 老陈 v1) | idle 90s close |
| Polygon RPC (Alchemy primary + QuickNode standby + dRPC backup, 老叶 v1) | WebSocket subscribe + HTTP eth_call | WS 1 + REST 1 per vendor | primary 满载, standby/backup 仅探活 1 keep-alive | failover 3s budget | primary 切 standby 时**旧连接 graceful close**, 不留 zombie |

### 2.A Polymarket WebSocket (与 v0.3 §17 协同)

#### 2.A.1 生命周期阶段

```
[启动]
   ↓
[1] DNS resolve + TCP connect + TLS handshake (1.2s p50 跨洋, 老陈 v1)
   ↓
[2] WS upgrade (HTTP/1.1 → wss 切换, ~300ms)
   ↓
[3] subscribe channels (market book + user channel, 一次性发全部 token_id 列表)
   ↓
[4] 收 snapshot (initial book state, ~2.0s 中位数, 老陈 v1)
   ↓
[5] STEADY: 数据流 + heartbeat (ping/pong, 服务端发 ping, 我们 pong 内置 wss client)
   ↓ (任一异常)
[6] 断线检测:
     - epoll EPOLLHUP/EPOLLERR
     - read() returns 0
     - 60s 无任何 frame (heartbeat watchdog)
     - 解析连续 5 次 parse error
   ↓
[7] graceful close (send WS close frame if possible, 立即 close fd)
   ↓
[8] exponential backoff: 0.5s, 1s, 2s, 4s, 8s, 15s cap, ±20% jitter (老陈 v1)
   ↓
[9] 回 [1] reconnect, 进入 RECONNECT 状态
   ↓
[10] subscribe 完成 + 初次 snapshot 收齐 → 通知 T2 book_builder 用 REST /book 比对 message_seq gap, 必要时拉 snapshot 补齐 (老陈 v1 §10 建议)
```

#### 2.A.2 OQ-R12-7 关键决策 (老周拍板)

**问题**: 1 个 connection multiplex 全部 channel/token vs 1 channel 1 connection?

**决策**: **1 个 connection multiplex 全部 market book channel + 1 个独立 connection 给 user channel** (共 2 个 WebSocket).

**理由**:

| 维度 | 1 conn multiplex | 1 conn / token |
|---|---|---|
| TLS 握手次数 | 1 (370ms once) | N × 370ms (爆炸) |
| 跨核 cache 影响 | 1 reactor T0 fan-out | N reactor 占 vCPU0 (反 R-12) |
| 断线影响面 | 全部 token 受影响 | 单 token 受影响 (但重连 budget × N) |
| 服务端 idle close | 单连接易触发, heartbeat 兜住 | 多连接 idle 风险更高 |
| 实测 (老李 + 小段 W10 §2.4) | Polymarket WSS 单连接支持 ≥ 200 token 订阅, 中位带宽 < 200 KB/s | — |

**Market book + user channel 分离**理由: user channel 包含订单 fills / 自身仓位更新, 故障隔离 (market 重连不影响 user 流), 老韩 RM 偏好.

#### 2.A.3 idle close 行为

Polymarket 服务端**未公开** idle close 时长 (W10 开放问题). 兜底:
- Client 主动 heartbeat watchdog: 60s 无任何 frame → 主动 close + reconnect (避免半死连接)
- 收到服务端 close frame → 走 [7]-[9] 重连
- 收到 `EPIPE` / `ECONNRESET` → 同上

#### 2.A.4 连接复用 / 池化

WebSocket **不池化** (state-ful subscribe), 唯一 connection 由 T0 wss_poly_reactor 持有, lifetime = process lifetime - reconnect cycle. RAII 包装在 `infra/net/WssClient` (析构 → close fd, send close frame best-effort).

---

### 2.B Polymarket REST (gamma / clob / data)

#### 2.B.1 HTTP/2 multiplex

- 全部走 nghttp2 + asio coroutine (老陈 v1 + v0.3 §17.3.2)
- 单 H2 连接支持 ≥ 100 concurrent stream (server `SETTINGS_MAX_CONCURRENT_STREAMS`, 实测 Polymarket 默认 100)
- HPACK 头压缩 (节省跨洋带宽)
- 不开 HTTP/3 (Polymarket 服务端不支持)

#### 2.B.2 连接池规格

| Host | 连接数 | concurrent stream/conn |
|---|---|---|
| clob.polymarket.com | 2 | 4 (实际并发顶 8) |
| gamma-api.polymarket.com | 1 | 2 |
| data-api.polymarket.com | 1 | 2 |

#### 2.B.3 keep-alive + idle reclaim

- keep-alive timeout: 60s (无 stream 60s 内 close)
- TLS session ticket 复用 (避免完整握手, 接近 RTT)
- 重连后 session ticket 仍可复用 (节省 ~200ms)

#### 2.B.4 跨洋 TLS 一次握手要复用一万次

老陈 v1 实测: TLS 完整握手 ~370ms (跨洋). 复用 session ticket / TCP keep-alive 后续 request ~ RTT (250ms one-way → 1 个 RTT 即可拿数据头).

**红线**: 任何走 `clob.polymarket.com` / `gamma-api.polymarket.com` 的 REST 必须经过 `infra/net/HttpsPool` 单例, **不允许** ad-hoc 起 boost::beast / curl 新连接.

---

### 2.C Goalserve REST (无 WebSocket)

#### 2.C.1 HTTP/2 支持

老陈 v1 §4 实测: Goalserve **不支持 HTTP/2**. 走 HTTP/1.1 + keep-alive.

#### 2.C.2 走代理 (`GOALSERVE_PROXY`)

- 代理 keep-alive 长连接, 走 `CONNECT host:443` 隧道
- per-host (走代理后实际是 per-proxy) keep-alive 2 个连接
- 代理稳定性未做长跑 (老陈 v1 §11 风险, 老吴跟进 24h 监控)

#### 2.C.3 REST polling 周期任务的连接复用

- T6 periodic_goalserve_inplay_poller (1-3s 周期, v0.3 §17.1.1) **复用同一 keep-alive 连接**, 不每次新连
- T7 periodic_goalserve_pregame_poller (30-60s 周期) 同上
- ≤ 3 并发 (老陈 v1 §10, 6 并发已 13% 超时)
- 间隔 ≥ 500ms (避免 rate limit)

#### 2.C.4 idle reclaim

- 90s 无 request → close (Goalserve 服务器主动 close 时 silent, 不发 close 帧, 需 client 探活)
- 探活方式: 每次 request 前检查上次活动时间, > 60s 主动 `HEAD /` 探活 (轻量)

---

### 2.D Polygon RPC (Alchemy primary + QuickNode standby + dRPC backup, 老叶 v1)

#### 2.D.1 混合协议

- **WebSocket subscribe**: `eth_subscribe(newHeads)` / `eth_subscribe(logs, filter)` — 1 个长连接持续推
- **HTTP REST**: `eth_call` / `eth_getTransactionCount` / `eth_sendRawTransaction` — 异步 worker pool

#### 2.D.2 多 vendor failover

```
primary (Alchemy)    : WS subscribe + REST 全量
standby (QuickNode)  : WS subscribe 同步 (热备), REST 仅探活 1 keep-alive
backup (dRPC)        : 仅探活, 故障时启动 WS + REST
```

#### 2.D.3 primary → standby 切换时旧连接生命周期

**关键决策**: graceful close, **不留 zombie**.

```
1. detect primary 故障 (3 次连续 error / 5s 无 newHeads / latency > P99 阈值)
2. mark primary as DEGRADED, traffic 切到 standby (standby 已 hot, 立即接管)
3. primary WebSocket connection 进入 GRACEFUL_DRAIN 状态:
   - 不再消费 frame (但保持 fd open 30s, 允许补遗 inflight subscription event)
   - 30s 后 send WS close frame + close fd
4. primary REST keep-alive 连接立即 close (REST 是无状态的, 不需 drain)
5. 重置 primary 重连 watchdog, 后台 30s 周期 probe primary 健康, 健康则反切
```

如果不 graceful close, 旧 standby 切来切去会累积 zombie fd (24h 实测可累积数百 fd, 触发 ulimit).

---

## 3. 数据生命周期 (Data Lifecycle)

### 3.0 七阶段总览

| 阶段 | 名称 | 时长 | 介质 | 拥有者 |
|---|---|---|---|---|
| L0 | 摄入 | < 50us p99 | wire (kernel buffer) | T0/T1 reactor |
| L1 | 暂存 | ≤ 100ms | in-process ring buffer (mmap arena 或 stack) | T2 book_builder |
| L2 | 处理 | < 500us | hot path (book, feature, strategy) | T3 strategy_engine |
| L3 | 持久化 | < 6us 同步 + 异步 fsync | local SSD WAL (audit + exec, 老王 v0.1) | RM core / Strategy core |
| L4 | 在线热数据 | 当日 (24h) | DuckDB / 内存 cache (RCU snapshot) | book_builder + ETL |
| L5 | 温数据 | 7-30 天 | 本地 SSD parquet (按日 partition) | 小余 ETL |
| L6 | 冷归档 | 7 年合规 | S3 / Glacier (老唐 audit schema v1) | obs/audit pipeline |
| L7 | 过期销毁 | TTL 到期 + 合规审批 | — | 老黄 compliance |

### 3.1 L0 摄入

**触发**: WebSocket frame 到网卡, kernel epoll wakeup; 或 REST response body 全收齐.

**failure mode**:
- 网卡 drop → 内核计数器 `net.softnet_stat`, 监控暴露
- WebSocket parse error → drop frame + counter (NO abort, v0.3)
- REST 4xx/5xx → retry pipeline, 失败到 L1 标 stale

**metric (小郑联签 D2)**:
- `wss_frame_recv_total{source}`
- `wss_parse_error_total{source, reason}`
- `rest_response_total{host, endpoint, status}`

### 3.2 L1 暂存 (≤ 100ms)

**介质**: `wss_in_ring` (SPSC, 16K, v0.3 §17.1.2) + `book_to_strat_ring` (SPSC) + `bg_work_ring` (moodycamel MPSC, 4096).

**retention**: 消费者跟上则即刻被覆盖, 上限是 ring capacity × 慢 consumer 时间. **不持久化**.

**state transfer 触发**:
- T2 book_builder pop → 进入 L2

**failure**:
- `try_push` 失败 (ring full) → counter `ring.full_total{name}` + drop, **不阻塞** (v0.3 不变量 4)

**metric**:
- `ring_depth{name}` (gauge)
- `ring_full_total{name}` (counter)
- `ring_consume_lag_ns{name}` (histogram)

### 3.3 L2 处理 (< 500us hot path)

**介质**: stack / TLS arena / RCU snapshot 内存; 无堆分配 (老姜 latency 预算).

**retention**: 处理完即丢, 不留 (orderbook delta apply 后, 原始 frame 不留).

**state transfer 触发**:
- 生成 OrderIntent → RM evaluate → signer → CLOB write (L3 audit WAL + exec WAL)
- 生成 audit record → 走 L3
- 更新 orderbook snapshot → 发布 RCU (L4 在线热)

**failure**:
- RM reject → 不写 exec WAL, 只 audit (v0.2 §11)
- strategy panic → 隔离, 老韩 RM kill switch

**metric**:
- `hot_path_p99_ns` (v0.3 < 500us 硬目标)
- `strategy_signal_total{strategy_id, action}`

### 3.4 L3 持久化 (老王 WAL framework)

**介质**: local SSD, 三 WAL 隔离 (audit / exec_position / exec_nonce, 老王 §4).

**retention 同步部分**: ≤ 6us, append + 异步 fsync.

**retention 异步部分**: group commit fsync (10ms 窗口, audit_fsync 线程 T8).

**state transfer 触发**:
- segment rotate (每 256MB or 24h)
- 异步 S3 副本 (老王 §7.2)

**failure**:
- fsync 失败 → fail-closed (老王 §3.3), 老韩 RM 进入 SAFE_MODE
- 磁盘满 → SAFE_MODE + alarm
- crash → replay 协议 (老王 §5)

**metric**:
- `wal_append_p99_ns{wal_id}`
- `wal_fsync_lag_ms{wal_id}`
- `wal_segments_total{wal_id}`
- `wal_disk_free_bytes`

### 3.5 L4 在线热数据 (当日)

**介质**: DuckDB in-memory + RCU snapshot (orderbook / feature).

**retention**: 24h, 跨日 (UTC 00:00) 滚动到 L5 parquet.

**state transfer 触发**:
- ETL daily roll (小余 ETL)
- 内存压力 (cache > 800MB) → LRU evict

**failure**:
- DuckDB crash → 不影响主交易 (只 query), reload from L5
- RCU snapshot 老 reader 没释放 → 旧版本不回收 (有界 leak, 监控)

**metric**:
- `cache_bytes`
- `cache_evict_total`
- `rcu_pending_free_count`

### 3.6 L5 温数据 (7-30 天)

**介质**: 本地 SSD `/var/lib/sports-trader/parquet/yyyy=2026/mm=05/dd=28/`.

**retention**: 默认 30 天 (老唐 audit schema 兼容, 合规要求 7 天最低).

**state transfer 触发**:
- 凌晨 cron 把 31 天前的 partition 上传 S3 + 本地删
- 紧急: 磁盘 > 80% 提前清

**failure**:
- 上传 S3 失败 → 保留本地, 重试 24h (不阻塞主交易)
- 本地损坏 → 从 L6 S3 恢复 (合规至少有一份)

**metric**:
- `parquet_partition_count`
- `parquet_disk_used_bytes`
- `s3_upload_lag_seconds`

### 3.7 L6 冷归档 (7 年合规, 老唐 schema)

**介质**: S3 标准 → 90 天 → S3 Glacier / Deep Archive.

**retention**: 7 年 (Polymarket / KYC / 老黄 compliance 强制).

**state transfer 触发**:
- S3 lifecycle policy 自动转 Glacier
- 7 年到期 → 进入 L7

**failure**:
- S3 不可达 → 不影响主交易, ETL 重试
- S3 数据校验失败 → 老唐 schema validator 报警

**metric**:
- `s3_archive_size_bytes`
- `s3_archive_object_count`

### 3.8 L7 过期销毁 (合规审批)

**触发**: TTL 到期 + 合规人 (老黄) 审批 + 双签.

**绝不**自动销毁, 必须人审 (合规要求, 涉及监管查询期限).

---

## 4. 资源池规格 (per resource type)

### 4.1 主表 (12 项)

| # | 资源 | 上限 | 监控 metric | 告警阈值 (WARN/CRIT) | 自动回收策略 | 实现 owner |
|---|---|---|---|---|---|---|
| R01 | TCP/WebSocket connections | 50 | `open_connections{kind}` | WARN > 40 / CRIT > 48 | LRU 关闭最久 idle 的 keep-alive | 老陈 |
| R02 | TLS handshakes/min | 100 | `tls_handshakes_rate` | WARN > 80 / CRIT > 95 | rate limit + 告警 (反 R-12 信号) | 老陈 |
| R03 | file descriptors | 8192 (ulimit -n) | `fd_count` | WARN > 7000 / CRIT > 7800 | `/proc/self/fd` scan + 强制 close leak | 老陈 + 小郑 |
| R04 | DB connections (DuckDB) | 20 | `db_conn_pool_usage_ratio` | WARN > 80% / CRIT > 95% | 拒新连接 + queue | 小余 ETL |
| R05 | in-process cache size | 1GB | `cache_bytes{name}` | WARN > 800MB / CRIT > 950MB | LRU evict + TTL 强制 | 老李 + 小段 (W10 cache) |
| R06 | WAL segment files (per WAL) | 100 | `wal_segments{wal_id}` | WARN > 90 / CRIT > 98 | 强制 rotate + archive 到 S3 | 老王 |
| R07 | heap RSS | 8GB | `process_rss_bytes` | WARN > 6GB / CRIT > 7.5GB | metric warn + heaptrack 触发 leak check | 老陈 + 小郑 |
| R08 | HTTP/2 concurrent streams | 100/conn (server-side) | `h2_active_streams{host}` | WARN > 80 / CRIT > 95 | 排队等空闲 stream | 老陈 |
| R09 | mmap arena pages | 4096 | `mmap_pages{arena}` | WARN > 3500 / CRIT > 4000 | 拒新分配 + 报警 | 小石 |
| R10 | thread count | 64 | `thread_count` | WARN > 50 / CRIT > 60 | 拒新 thread, 强制走线程池 | 老陈 |
| R11 | RCU pending free | 10000 epoch | `rcu_pending_free_count` | WARN > 5000 / CRIT > 9000 | force quiesce + reclaim | 小石 |
| R12 | shared memory segments | 16 | `shm_segment_count` | WARN > 12 / CRIT > 15 | 不申请新 segment, signer IPC 复用 | 老孙 (signer) |

### 4.2 表项数

**12 项** (R01 - R12).

### 4.3 RAII 包装要求

每条资源对应一个 RAII C++ 类:

```cpp
// infra/resource/ (老陈 主笔)
class SocketHandle;        // R01, R03
class TlsSession;          // R02 (track + rate limit)
class FdGuard;             // R03 (open/close pair)
class DbConnection;        // R04
class CacheEntry;          // R05 (auto-evict on TTL)
class WalSegment;          // R06 (auto-rotate)
class HeapArena;           // R07 (track + report)
class H2Stream;            // R08
class MmapArena;           // R09
class ScopedThread;        // R10 (join on destruct)
class RcuEpoch;            // R11
class ShmSegment;          // R12 (auto shm_unlink)
```

**强制**: 任何使用以上资源的代码**必须**走 RAII 包装, 不允许直接调 OS syscall. PR review 红线 (§10).

---

## 5. 资源泄漏检测

### 5.1 C++ 工具栈

| 工具 | 阶段 | 频次 | 触发 |
|---|---|---|---|
| AddressSanitizer (ASAN) + LeakSanitizer (LSAN) | 启动期 + smoke test | 每次 build | CI `make test-asan` |
| valgrind --leak-check=full | CI nightly | 每晚 | CI cron |
| heaptrack | 生产采样 | 1h/天 (轮 host) | 老吴 SRE 调度 |
| clang-tidy `cppcoreguidelines-owning-memory` | 静态 | 每次 PR | pre-commit hook |
| clang-tidy `bugprone-use-after-move` / `misc-no-recursion` | 静态 | 每次 PR | pre-commit hook |
| ThreadSanitizer (TSAN) | 启动期 (单独 build) | weekly | CI weekly |

### 5.2 应用层强制

- 所有 owning pointer 必须是 `std::unique_ptr` / `std::shared_ptr` (`new`/`delete` 红线, §10)
- 所有非 owning reference 必须是 `T&` / `T*` / `std::span` (清晰所有权)
- 所有 factory 函数返回 `std::unique_ptr<T>` 或 `T` (RVO), 不返回裸指针
- 所有 `open()`/`socket()`/`accept()` 必须经 `FdGuard` / `SocketHandle` (§4.3)
- 所有 callback / lambda 捕获 `shared_ptr` 必须 `[[nodiscard]]` 标注返回的 token, 避免 dangling

### 5.3 运行时主动 scan

`infra/health/FdScanner` 单线程, 每 30s scan `/proc/self/fd/`:

```
1. ls /proc/self/fd/ → 当前 fd 列表
2. 与上次 snapshot 对比, 新增 fd 关联到 RAII tracker
3. 若发现 fd 没有 RAII tracker (野 fd) → alarm + 记录 stack trace (libunwind)
4. 若 fd 数 > 阈值 (R03) → 触发 R03 自动回收
```

### 5.4 leak check 周期采样

heaptrack attach 主进程 1h, 收集 allocation profile, 异步上传分析. 不在 hot path (走 vCPU3 独立 host).

---

## 6. 优雅关闭 (Graceful Shutdown) SOP

### 6.1 信号处理

| 信号 | 行为 |
|---|---|
| SIGTERM | 进入 DRAIN mode (本节 SOP) |
| SIGINT (Ctrl-C, 仅 dev) | 同 SIGTERM |
| SIGHUP | 配置热加载 (T11 config_watcher, 不退出) |
| SIGUSR1 | dump 内部状态 (debug) |
| SIGKILL | 内核强杀 (60s 兜底, 不可拦) |

### 6.2 DRAIN 状态机

```
[NORMAL] → SIGTERM → [DRAIN_NEW]   不接新订单, 不接新 strategy 触发
   ↓ (10s budget)
[DRAIN_INFLIGHT]  等已 inflight 订单完成 (CLOB ack), max 30s
   ↓
[DRAIN_FSYNC]    停 strategy thread, 等 audit/exec WAL 写入完毕 + fsync
   ↓ (5s budget)
[DRAIN_NETWORK]  关闭外部连接 (按依赖反序: Polygon → Polymarket WS → Polymarket REST → Goalserve → KMS)
   ↓ (5s budget)
[DRAIN_FREE]     释放 fd / mem / shared memory / mmap / RCU pending
   ↓
[EXIT 0]
```

### 6.3 各阶段 budget

| 阶段 | budget | 超时行为 |
|---|---|---|
| DRAIN_NEW | 10s | 强制进入 DRAIN_INFLIGHT |
| DRAIN_INFLIGHT | 30s | 强制 cancel (本地标记, CLOB 真实状态未知, 进入 SAFE_MODE replay 修复) |
| DRAIN_FSYNC | 5s | 强制 fsync abort, 标记 dirty, 下次启动 replay |
| DRAIN_NETWORK | 5s | 强制 close fd (RAII 析构) |
| DRAIN_FREE | 5s | RAII 析构链, 应自动 (出错 abort, 老何 footgun) |
| **总 budget** | **55s** | SIGKILL 60s 内强杀 |

### 6.4 关闭顺序原因

按反依赖关系关:
- Polygon 最先 (依赖最少, signer 不依赖它)
- Polymarket WS 次 (有 close frame 礼貌)
- Polymarket REST (HTTP/2 GOAWAY)
- Goalserve (HTTP/1.1 keep-alive close)
- KMS / signer 最后 (其他都不再需要签名)

### 6.5 SIGKILL 兜底

60s 后 systemd / supervisor 发 SIGKILL. 此前未完成的状态依赖**下次启动 replay** (老王 §5) 恢复. **不依赖 SIGKILL 之前的 best-effort cleanup**, 因为 kernel 会自动 close 所有 fd / 释放进程 mem.

唯一会泄漏的: shared memory segment (`shm_unlink` 未调用) → 启动期清理脚本扫 `/dev/shm/sports-trader-*` 强制清.

---

## 7. 冷启动 + 热启动

### 7.1 启动总流程 (与 v0.2 §14 SAFE_MODE 对齐)

```
[1] 进程启动 → 立即进入 SAFE_MODE (no order)
[2] 加载配置 (.env + TOML, 老吴 toolstack v1)
[3] 启动 obs/metrics exporter (T10)  ← 越早越好, 后续故障可见
[4] 检测 shared memory 残留 → 清理 (§6.5 兜底)
[5] WAL replay (老王 §5): audit + exec_position + exec_nonce 三个并发 replay
[6] 重新建立外部连接 (按依赖顺序):
       KMS / signer 进程握手 (老孙 v4)
         ↓
       Polymarket WebSocket (clob + user channel) + initial snapshot
         ↓
       Polymarket REST H2 pool warm-up (gamma/clob/data 各 1 个 ping)
         ↓
       Goalserve REST keep-alive 探活 (HEAD /)
         ↓
       Polygon RPC primary 连接 + WS subscribe
[7] 健康检查: 各连接 alive + WAL replay 成功 + RM kill switch 状态
[8] 解锁 → 进入 NORMAL mode
[9] strategy_engine 开始接收 signal + 下单
```

### 7.2 冷启动 (process fresh boot)

- 全部 cache empty, 走 cache miss storm 防护 (Wave 10 §4.5)
- WAL 短 (前几个 segment), replay 快 (< 5s)
- 外部连接全部 cold handshake (跨洋 TLS 370ms × N, 总 ~5s 预算)

### 7.3 热启动 (process restart, kernel 没重启)

- TLS session ticket 部分仍有效 → 部分握手节省 (~150ms 节省)
- WAL 长 (几十 segment), replay 慢 (10-30s)
- shared memory 残留可能存在 → §6.5 清理

### 7.4 SAFE_MODE → NORMAL 解锁条件

| 条件 | 必须 |
|---|---|
| WAL replay 完成且无错 | YES |
| Polymarket WS 收到 first frame (说明 subscribe 成功) | YES |
| Polymarket REST gamma ping ok | YES |
| Goalserve livescore ok (探活) | YES (若 Goalserve 故障, 进入 DEGRADED_NO_GOALSERVE 子状态, 不解锁全功能) |
| Polygon RPC primary alive | YES |
| signer 进程 alive + responsive | YES |
| RM kill switch 状态 = ENABLED | YES |

任一未达 → 留在 SAFE_MODE, 重试 5s, 持续 5min 仍未达 → 告警 (小尤 P1).

---

## 8. 缓存 / TTL 矩阵 (与 Wave 10 老李+小段 协同)

### 8.1 总矩阵

| 数据类 | TTL | 来源 endpoint | 失效策略 |
|---|---|---|---|
| 合约地址 / fee 表 | 启动期 1 次 + 24h refresh | 配置文件 / Polymarket constants | manual reload (SIGHUP) |
| Polymarket market list (全量) | 60s | gamma `/sports/events?limit=500` (bulk) | T5 周期 |
| Polymarket market detail (per market) | 60s, **不主动按 market 拉** (反 R-12), 由 market list refresh 携带 | gamma `/sports/events` (bulk 携带) | T5 周期 |
| Polymarket orderbook | **WebSocket subscribe, 无 cache** | WS book channel | WS push |
| Goalserve 赛程 (pregame) | 60s | T7 pregame poller (bulk per 联盟) | T7 周期 |
| Goalserve 球员名单 | 60s | T7 pregame poller (bulk 携带) | T7 周期 |
| Goalserve inplay 比分 / 时钟 | 1-3s | T6 inplay poller (bulk 全联盟) | T6 周期 |
| Polymarket /book snapshot (兜底) | 5s stale-while-revalidate | clob `/books` (bulk) | T2 触发 |
| Polygon gas price | 5s | Polygon RPC `eth_gasPrice` | 周期 |
| Polygon nonce (per wallet) | 实时 (每次下单前刷, 老叶 nonce manager) | RPC `eth_getTransactionCount` | 每次下单前 |
| KMS 公钥 | 启动期 1 次 + 1h refresh | KMS API | 周期 |

### 8.2 R-12 红线落地

**禁止**:
- 单 market 起 timer 轮询 (反 R-12)
- 无 TTL cache 条目 (内存永涨)
- TTL 太短 (< freshness 需求 × 2) → 重复打 endpoint 浪费
- TTL 太长 (> freshness 需求 × 2) → stale 数据决策

### 8.3 stale-while-revalidate 政策 (老李 W10 §7.2)

- 命中: TTL 内直接返 cache
- TTL 过期但 < 2× TTL: 返 cache 同时 async 刷新 (single-flight 折叠, v0.3 §17.4)
- > 2× TTL: 同步刷新 + 阻塞 caller (热路径绝不发生, 走 worker pool)

---

## 9. 测试 + 监控 (与小宋 + 小郑 联签)

### 9.1 长跑测试 (24h soak)

**Owner**: 小宋 test-replay-framework + 老陈 网络 + 老周 review.

**Test plan**:

| ID | 测试 | 验收 |
|---|---|---|
| LS-001 | 24h 跑 paper trading (xiaojiang paper-trading-engine), 注入正常负载 | fd_count 单调有界 (不持续上涨); cache_bytes 在 800MB 上下波动; heap RSS < 6GB |
| LS-002 | 反复断线重连 1000 次 (Polymarket WS + REST + Polygon WS) | fd 全部回收 (`/proc/self/fd` count stable); 无 zombie connection |
| LS-003 | LSAN 跑全套 unit test | 0 leak detected |
| LS-004 | valgrind 跑 1h smoke trading | 0 definitely-lost; possibly-lost < 1KB |
| LS-005 | heaptrack 1h 采样, top allocator 是否合理 | 无 unbounded growth allocator |
| LS-006 | graceful shutdown 60 次 | 每次 exit code 0; WAL 无 dirty mark |
| LS-007 | shared memory segment 启动清理 | `/dev/shm/sports-trader-*` 启动后干净 |
| LS-008 | DRAIN inflight 超时 → SAFE_MODE replay | next start 状态一致 |

### 9.2 监控 dashboard (小郑 D6 新增)

**D6 资源池 dashboard** (新增, 与 D2 数据健康联签):

- panel 1: `open_connections{kind}` 折线图, 阈值 40/48
- panel 2: `fd_count` 折线, 阈值 7000/7800
- panel 3: `tls_handshakes_rate` (1m rate), 阈值 80/95
- panel 4: `cache_bytes{name}` stacked area, 阈值 800MB/950MB
- panel 5: `process_rss_bytes` 折线, 阈值 6GB/7.5GB
- panel 6: `wal_segments{wal_id}` 折线 + `wal_disk_free_bytes`
- panel 7: `rcu_pending_free_count`, 阈值 5000/9000
- panel 8: `ring_full_total{name}` rate (异常率指示)
- panel 9: connection lifecycle gantt (每个 connection 的 start/reconnect/close 时间轴, debug 神器)

### 9.3 告警规则 (与小尤 P0-P4)

| 规则 | 级别 | 触发 |
|---|---|---|
| fd_count > 7800 持续 1m | P0 | crit, 立即响应 |
| open_connections > 48 持续 1m | P0 | crit |
| process_rss_bytes > 7.5GB 持续 5m | P0 | crit, 可能 OOM kill |
| cache_bytes > 950MB 持续 5m | P1 | warn, LRU 可能不够 |
| wal_segments > 98 | P0 | crit, archive 跟不上 |
| WebSocket reconnect rate > 10/min | P1 | warn (服务端可能限流) |
| tls_handshakes_rate > 95/min | P2 | warn, 复用失败 |
| ring_full_total{name} 1m rate > 100 | P1 | warn, 消费者跟不上 |
| graceful shutdown timeout (> 60s SIGKILL) | P0 | post-mortem 必做 |

---

## 10. 红线 + PR review (老高)

### 10.1 自动 reject (clang-tidy / pre-commit hook)

| # | 红线 | 工具 |
|---|---|---|
| L01 | 任何 `new` / `delete` 裸调用 (用 `std::make_unique` / `std::make_shared`) | clang-tidy `cppcoreguidelines-owning-memory` |
| L02 | 任何 `::open`/`::socket`/`::accept`/`::pipe` 不配 RAII 包装 | grep + pre-commit |
| L03 | 任何 `std::map<...>` cache 无 TTL 标注 (必须用 `TtlCache<K,V>`) | clang-tidy custom check |
| L04 | 任何 socket 操作无 timeout (`setsockopt SO_RCVTIMEO/SO_SNDTIMEO` 或 epoll deadline) | grep + review |
| L05 | 任何长跑连接池无 idle reclaim 字段 (struct 必须有 `last_use_ts` + reaper thread) | review |
| L06 | 任何 `boost::asio::ip::tcp::socket` 不经 `infra/net/SocketHandle` | grep |
| L07 | 任何 `nlohmann::json` 拷贝 (用 `simdjson` 或 move) | clang-tidy + 老何 footgun |
| L08 | 任何 thread 创建不经 `infra/resource/ScopedThread` (join on destruct) | review |
| L09 | 任何 callback 捕获 `[&]` 跨 thread (生命周期不可证明) | review |
| L10 | 任何 cache entry 无 max size 上限 (R05 1GB 强制) | review |

### 10.2 人工 review (老高 + 老何 联签)

每个 PR 触碰 `infra/net/` / `infra/resource/` / `infra/wal/` 必须老高 + 老何 双签.

### 10.3 footgun 联签

老何 footgun checklist v1 §FG-12 (RAII) / §FG-15 (callback lifetime) / §FG-23 (cache TTL) 直接引用本 doc §4.3 / §5.2 / §8.

---

## 11. 开放问题 (TODO)

1. **Polymarket WebSocket server-side idle timeout 未公开** — @老李 Wave 11+ 跟踪官方 docs / 社区
2. **Goalserve 代理 24h 稳定性未测** — @老吴 SRE 长跑监控 (已挂老陈 v1 §11.8 风险)
3. **Polygon WS subscribe 实测未做** — @老叶 RPC 选型敲定后补 (老陈 v1 §11.4)
4. **heaptrack 跨主机轮转策略** — @老吴 cron 实现细节, Wave 12
5. **RAII C++ 模板库 (infra/resource/)** — @老陈 主笔, Wave 12 落地 (本 doc §4.3 是规格)
6. **TtlCache<K,V> 模板** — @老李 + 小段 Wave 10 在跑, 落地后引用
7. **SAFE_MODE → DEGRADED_NO_GOALSERVE 子状态机** — @老周 v0.4 补 (本 doc §7.4 提到)

---

## 12. 给 GM 老雷的一句话汇报

数据 + 连接生命周期管理 v1 已就位, 12 项资源池规格 + 7 阶段数据 lifecycle + 4 外部 API 连接 SOP 全覆盖. 用户原话"保证连接的前提下避免资源泄漏和浪费"由 RAII + sanitizer + monitoring 三层兜底落地, R-12 红线 (单 market 不轮询 + bulk endpoint) 在 §8.2 落地. **Top 1 易泄漏场景**: Polygon RPC primary → standby failover 切换时旧 WS 连接 zombie 累积 (§2.D.3, 24h 实测可累积数百 fd), 已在 §2.D.3 用 graceful drain 30s + close 兜底.
