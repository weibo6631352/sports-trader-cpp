# 外部 API 速率限制 + 实测延迟 SSOT v1

- Owner: 老陈 (cpp-network-engineer)
- 数据源贡献: 老陈 (跨洋链路 bench), 老李 (Polymarket 47 endpoint), 小段 (Goalserve 219 探针 + 官方文档), 老叶 (Polygon RPC 34 入口)
- Date: 2026-05-28
- Last measured: 2026-05-28 13:30 UTC (data 汇总: `laochen-network-bench-*.csv` 跨洋 bench 主时段; 上游 owner probe: 老李 13:33 / 小段 12:30 + 13:33 + 15:29 / 老叶 13:33 UTC)
- Status: Living Document (月度 sweep 由小冯接手)
- 验收人: GM 老雷 + 老周 (架构, 性能预算) + 老姜 (perf, latency-budget v2)
- 关联文档 (原始数据, **本文不重述**, 仅交叉引用):
  `docs/RESEARCH/laochen-network-bench-v1.md` (跨洋 TTFB / 13 endpoint × 30 sample / WSS / 带宽 / traceroute),
  `docs/RESEARCH/laoli-polymarket-endpoint-matrix-v{2,3}.md` (47 endpoint + HMAC 14 vector + 4 bug 修正),
  `docs/RESEARCH/laoli-xiaoduan-api-call-optimization-v1.md` (复用率),
  `docs/RESEARCH/xiaoduan-goalserve-api-spec-v1.md` (代理 vs 直连),
  `docs/RESEARCH/xiaoduan-goalserve-endpoint-matrix-v2.md` (219 探针 17 sport),
  `docs/RESEARCH/xiaoduan-goalserve-odds-by-sport-v2.1.md` (odds 三态),
  `docs/RESEARCH/laoye-polygon-rpc-endpoint-matrix-v1.md` (**deferred 但保留**),
  `docs/GOALSERVER/full_package_feed{,_cn}.{txt,md}` + `inplay-feed-new.txt` (官方限流条款),
  原始 CSV: `docs/RESEARCH/data/laochen-network-bench-*.csv`

---

## 0. TL;DR (一表概览, 给老板一眼)

| 数据源 | 限流上限 (官方/实测取保守) | p50 延迟 (跨洋) | p99 延迟 (跨洋) | 当前月费 | 状态 |
|---|---|---:|---:|---|---|
| Polymarket gamma REST | 实测 60/min 单 IP 无限流, 推荐自我 60/min sustained | 992-1465 ms | 1973-2092 ms | $0 | ACTIVE |
| Polymarket CLOB REST 公开 | 实测 30 并发 30/30 OK, 推荐 60/min sustained, /books heavy 6/min | 927-2066 ms | 1386-3478 ms | $0 | ACTIVE |
| Polymarket CLOB REST 私有 (L2 HMAC) | 推荐 20/min sustained (HMAC 签名负担) | n/a (8 endpoint 全 200) | n/a | $0 | ACTIVE |
| Polymarket data-api REST | 实测 20 并发 20/20 OK, 推荐 60/min | 926 ms | 3339 ms | $0 | ACTIVE |
| Polymarket WSS (`/ws/market`, `/ws/user`) | 单连接 500 token (实测上限), 重连 ≤ 3 次/min | 握手 1316-1489 ms / 首条 2202-2480 ms | 握手 ~1900 ms / 首条 ~3400 ms | $0 | ACTIVE |
| Goalserve `www.goalserve.com` REST | **官方 1 req/s** (logos), **1 req/s** (settlement 按 sport) | 1787-2839 ms (代理) | 7929-13763 ms | 已购 (livescore plan) | ACTIVE |
| Goalserve `inplay.goalserve.com` (gzip) | **官方 1s 推送频率**, polling 拉 gz | [需后续实测] (小段 v3 in flight) | [需后续实测] | 已购 (含基础订阅) | ACTIVE |
| Goalserve `oddsfeed.goalserve.com` (settlement) | **官方 1 req/s per sport** | [需后续实测] | [需后续实测] | 已购 但 `getodds/*` 0 字节 = 当前 key 无 odds plan | DEGRADED |
| Polygon RPC (deferred) | dRPC free batch 3, paid 100; Alchemy 1000; QuickNode 1000 | 867-955 ms (Ankr/polygon-rpc 公共池实测) | 1572-2146 ms | $0 (defer) | DEFERRED |

**Notes**: (1) "$0" = Polymarket 三组 API 全免费; "已购" = Goalserve livescore 基础套餐, **odds plan 未购** (xiaoduan v2 §4 实证 33 个 odds endpoint 全 0 字节); Polygon 由 GM ADR `defer-onchain` 推迟到 MVP 后. (2) Goalserve 全走 `http://127.0.0.1:7890` 代理, 直连 p95 8.5s 不可用 (xiaoduan v1 §2.4). (3) 老陈跨洋 bench 时段 2026-05-28 11:00-12:30 CST = 美东深夜, 缺峰值, p99 ≥ 100 样本待老吴 24h cron.

---

## 1. Polymarket 三组 API

### 1.1 gamma REST (`gamma-api.polymarket.com`)

**限流 (老李 v2 §7 + 老陈 bench v1 §10):**

| 维度 | 数字 | 来源 |
|---|---|---|
| 官方明示限流 | 无公开文档 | — |
| 实测压测 (20 并发 1 endpoint) | 20/20 全 200, 无 429 / 无 Cloudflare 1010 | 老李 v2 §7.1 |
| 老陈 bench 30 串行 | 60/60 全 200 | `ratelimit-gamma.csv` |
| 推荐自我限流 (保守) | **60 req/min sustained, 30 req / 5s burst** | 老李 v2 §7.2 + 老陈 v1 §10 |
| 大对象 (events listing 8 MB) 推荐 | **≤ 1 req/s** | 老李 v2 §7.2 |

**延迟 (老陈 v1 §2, n=30):**

| Endpoint | p50 ms | p95 ms | p99 ms | starttransfer p50 | 备注 |
|---|---:|---:|---:|---:|---|
| `/markets?limit=20` | 992 | 1757 | 1973 | 683 | payload ~130 KB |
| `/events?limit=20` | 1465 | 2047 | 2092 | 746 | payload ~428 KB |
| `/events?tag_slug=mlb&limit=200` | ~3580 (老李 v2 §5.1) | n/a | n/a | n/a | **复用率王: 1977 markets / 1 call / 7.6 MB** |

**复用率上限** (老李 v2 §5.1): 1 调用最高拿 1977 markets (mlb), 替代 per-market 减少 99.84%. **连接复用**: H2 multiplexing 单连接, TLS ~370 ms 一次后 keep-alive.

### 1.2 CLOB REST (`clob.polymarket.com`)

**公开端点限流 (老李 v2 §7 + 老陈 v1 §10):**

| 类型 | 推荐 RPS | 实测压测 |
|---|---|---|
| light (price/midpoint/spread/tick-size) | **60 req/min**, 30 并发瞬时 OK | 老李 v2 §7.1: 30/30 全 200 |
| heavy (`/books` 500-batch) | **6 req/min** (≤ 10s 间隔), 3 并发 | 单请求 3.2s, 500 token 上限 (501+ HTTP 400 `Payload exceeds the limit`) |
| listing (`/markets`, `/sampling-markets`) | **6 req/min** | 单请求 2.1 s, 1000 markets / page |

**私有端点限流 (L2 HMAC):**

| 维度 | 数字 |
|---|---|
| 推荐自我限流 | **20 req/min sustained**, 不建议突发 |
| 限流上限 | 未明 [需后续实测 — Sprint-2, 老李] |
| HMAC 4 bug 红线 | 见老李 v3 §A.3 R1-R7 (querystring 不进 base string / sig 保留 padding / param 名 `asset_type` / sigType=1 / 401 SOP / body 单引号→双引号 / 月度 SDK sweep) |

**延迟 (老陈 v1 §2, n=30):**

| Endpoint | p50 ms | p95 ms | p99 ms | 备注 |
|---|---:|---:|---:|---|
| `/markets` (全市场 1000/page) | 2066 | 3073 | 3478 | 大对象, 不能在 hot path |
| `/sampling-simplified-markets` | 1511 | 2291 | 2554 | **推荐主用** (做市目标池) |
| `/book?token_id=` | 927 | 1266 | 1386 | 单 token, 最轻 |
| `POST /books` 500-batch | 3200 (单请求, n=1) | n/a | n/a | 老李 v2 §2.1 / §5.1 |

**私有 8 endpoint 全 200** (老李 v2 §2.4): `/auth/api-keys`, `/auth/derive-api-key`, `/data/orders`, `/data/trades`, `/trades`, `/balance-allowance` (×3 sigType), `/data/order/{id}`. 单 endpoint p50 ~1 s 跨洋 (与公开端点同链路).

### 1.3 WebSocket (`wss://ws-subscriptions-clob.polymarket.com`)

**连接容量 (老李 v2 §4.2 + 老陈 v1 §3):**

| 维度 | 数字 | 来源 |
|---|---|---|
| 单连接订阅 token 上限 | **实测 ≥ 500** (老李 v2 N=1/50/100/300/500 五档全通) | 老李 v2 §4.2 |
| 单连接订阅 token 上限 (压测未碰到) | 1000+ [需后续实测 — Sprint-2, 老李 v3 §9 #17] | — |
| 500 token 稳态 msg 率 | **63 msg/s** (1014 msg / 16s window), 临场 5-10× = 300-600 msg/s | 老李 v2 §4.2 |
| 单 message size | 600-9400 B (snapshot 大, delta 小) | 老陈 v1 §3 |
| price_change : book 比例 | ~4:1 (price_change 远多于 book reset) | 老李 v2 §4.2 |

**握手 / 首条 / 重连 (老陈 v1 §3 + §9):**

| 阶段 | p50 ms | max ms | 备注 |
|---|---:|---:|---|
| WSS 握手 | 1316-1489 | 1489 | TLS + WS upgrade 跨洋 |
| 首条 snapshot | 2202-2480 | 4160 (500 token 满载) | 服务端订阅注册 + 推 snapshot |
| 完整重连 (含 TLS + WS + 订阅 + 首条) | 2574 | 3948 | 老陈 5 次循环, 中位数 |

**心跳 / sequence_gap 兜底 (老陈 v1 §3 给老李)**: 服务端**不主动**发 ping; 客户端 15s 无消息触发 client ping, 30s 无 frame 主动 close + reconnect; 无 sequence number, 重连后用 REST `/book` 取 snapshot 补齐 gap; 重连 budget **3 s** (含首次 backoff), 超过视为持续故障切备用源; Exponential backoff 0.5/1/2/4/8/15s cap + ±20% jitter.

**WSS 推荐拓扑 (老李 v3 §C, vCPU0 4-5 conn 中间方案)**: T0a `poly_market_hot` 1 conn × 500 hot token (临场 ±10min, 150-300 msg/s); T0b `poly_market_cold` 1-2 conn × 2500-3500 cold (50 msg/s); T0c `poly_user` 1 conn × user 全 conditionId (<5 msg/s 平稳, 20 burst); T1 `polygon` (deferred) 1 conn × 3 sub.

---

## 2. Goalserve

### 2.1 `www.goalserve.com` (现用主域, REST polling)

**限流 (官方文档 + 小段 v1 + 老陈 bench):**

| 来源 | 数字 |
|---|---|
| 官方明示 (`full_package_feed.txt` line 1444) | **logos endpoint: 1 req/s** |
| 官方明示 (`full_package_feed.txt` line 1483) | **settlement endpoint: 1 req/s per sport** |
| 全局 RPS 限制 | **官方未明示**, 待商务确认 (xiaoduan v1 §8 P0 待办 #2) |
| 老陈 6 并发压测 | **6 并发 → 13.3% 超时 (4/30 timeout)** `ratelimit-goalserve.csv` |
| 小段 219 探针 (代理, 单线程) | 219/219 全 200 |
| 推荐自我限流 | **≤ 1 RPS sustained, ≤ 3 并发**, 间隔 ≥ 500 ms | 老陈 v1 §10 |

**延迟 (代理模式, 小段 v1 §3, n=20):**

| Endpoint | 体积 | TTFB p50 | total p50 | total p95 | total max |
|---|---|---:|---:|---:|---:|
| `bsktbl/inplay` | 185 B | 2.04 s | 2.04 s | 9.99 s | 10.6 s |
| `bsktbl/nba-shedule` | 130 KB gz | 2.79 s | 3.51 s | 7.70 s | 8.8 s |
| `soccer/inplay` | 327 B | 2.44 s | 2.44 s | 6.25 s | 8.5 s |
| `soccernew/inplay` | 327 B | 1.89 s | 1.89 s | 5.43 s | 6.5 s |
| `soccer/home` | 21 KB gz | 2.40 s | 2.44 s | 5.04 s | 7.8 s |
| `football/nfl-scores` | 4.3 KB | 1.81 s | 1.81 s | 7.71 s | 12.6 s |
| `baseball/usa` | 30 KB | 2.90 s | 3.05 s | 8.50 s | 12.7 s |
| `tennis/home` | 12 KB | 2.72 s | 2.72 s | 7.73 s | 8.3 s |
| `hockey/nhl-scores` | 4 KB | 2.35 s | 2.35 s | 6.95 s | 12.0 s |
| `cricket/livescore` | 47 KB | 2.44 s | 2.56 s | 7.16 s | 7.7 s |

**核心结论**: TTFB 主导, total ≈ TTFB (跨洋 RTT + 服务端响应, 不是带宽); p95 全部 5-10 s, max 偶到 12 s, 决策 budget 给 **10 s 上游延迟**; 老陈跨洋 bench 与小段独立采样吻合.

**复用率 (老李+小段 v1 §2.5)**: `bsktbl/nba-shedule` 全季 1403 场 / 1 call (1403×); `soccer/home` 51 联赛 / 141 比赛当日 / 1 call (141×); 全部 endpoint 强制 bulk-by-league/sport, 无 per-game endpoint.

**字段刷新频率**: 官方 `inplay-feed-new.txt` inplay odds gz **每秒刷新**; 小段 spec v1 §6.2 文档值 inplay xml 5-10s / scores 30-60s / schedule 6-24h; 老陈 2 分钟 / 10s 周期实测: 淡季无 inplay 比赛, 0 hash 变化 (1/12 unique) — **实测刷新率 [需后续实测 — 小段 v3, 赛事密集时段 UTC 18:00-23:00]**

### 2.2 `inplay.goalserve.com` (官方 inplay odds 域)

**官方协议 (`inplay-feed-new.txt`):**

| 维度 | 数字 |
|---|---|
| 协议 | HTTP polling, **gzip 压缩 JSON** |
| 路径 | `inplay-{sport}.gz` (soccer / basket / tennis / volleyball / amfootball / esports / hockey / baseball) |
| 推送频率 | **每秒刷新** (官方明示 "refresh every second") |
| 字典 endpoint | `dictionaries/odds-markets/{sport}` + `dictionaries/states/{sport}` |
| 结果 endpoint | `results/{yyyyMM}/{MATCH_ID}.json` (赛后存档) |
| time_status enum | 0-9 + 99 (Not Started / InPlay / TBF / Ended / Postponed / Cancelled / Walkover / Interrupted / Abandoned / Retired / Removed) |

**实测状态**: 小段 v3 in flight, **暂占位 [需后续实测 — 小段 v3 出来后补 p50/p99 + 代理需求 + IP 白名单]**. 限流官方未明示, 但 "1s 推送" 暗示客户端 polling 1 RPS/sport, 8 sport 同拉 = 8 RPS 是否与 `www` 共享配额待小段 v3 验证.

### 2.3 `oddsfeed.goalserve.com` (settlement 域)

**官方限流 (`full_package_feed.txt` line 1483):**
- `api/v1/odds/pre-game/settlements` — **1 request per second per sport**
- 单次 `dateTime=` 拉取最近 30 min 内 settled 全部 odds
- `matchesIds=` 批量上限 **50 match ids per call**

**实测状态**: 当前 key 无 odds plan (xiaoduan v2 §4), 33 个 `getodds/*` endpoint 全 0 字节. settlement endpoint 行为 [需后续实测 — 升级 plan 后, owner 老雷商务].

**预期 sport 映射**: soccer=4, basketball=7, tennis=5 (官方文档 line 1465).

### 2.4 代理 vs 直连 (小段 v1 §2.4 + 老陈 v1 §4)

**实测对比 (n=10/10 直连/代理, `bsktbl/inplay`)**:

| 通道 | avg | p50 | p95 | max |
|---|---:|---:|---:|---:|
| 直连 | 2.726 s | 2.081 s | **8.495 s** | 8.495 s |
| 代理 `127.0.0.1:7890` | 1.927 s | 1.888 s | **2.325 s** | 2.325 s |

**老陈跨 endpoint 30-sample 对比 (`network-bench-v1` §4):**

| Endpoint | 模式 | p50 | p95 | 差值 |
|---|---|---:|---:|---|
| `bsktbl/nba-scores` direct | n=30 | 1787 ms | 4403 ms | baseline |
| `bsktbl/nba-scores` proxy | n=30 | 1919 ms | 6610 ms | +132ms p50 / +2207ms p95 (此端点直连尚可) |
| `soccernew/inplay` direct | n=30 | 1899 ms | 7720 ms | baseline |
| `soccernew/inplay` proxy | n=30 | 2012 ms | 6546 ms | +113ms p50 / **-1174ms p95** (inplay 走代理反而稳) |

**结论**: 代理只加 ~110-130 ms p50 开销; 直连 p95 抖动巨大 (5-15s) 是 Goalserve 服务端 + 跨洋骨干特性; **生产铁律: 全 Goalserve 走代理** (inplay IP 白名单 + p95 收益); 30 样本里 1 个 8s outlier, 24h 监控 **[需后续实测 — 老吴 SRE]**.

---

## 3. Polygon RPC (deferred 但数据保留)

GM ADR `defer-onchain` 推迟 Polygon 主链接入到 MVP 后. 老叶实测数据本节保留, **不进入 MVP 决策路径预算**.

### 3.1 三 vendor 实测 (Alchemy / QuickNode / dRPC + 公共池, 老叶 v1 §0.2 + §8)

| Vendor | HTTP URL | WSS URL | 可用度 | batch 上限 |
|---|---|---|---|---|
| Alchemy demo | `polygon-mainnet.g.alchemy.com/v2/demo` | 同 wss | 大部分 429 (demo 共享) | **paid 1000** (官方) |
| QuickNode docs demo | `docs-demo.matic.quiknode.pro/` | — | 200 全通 (含 debug_trace*) | **paid 1000** (官方) |
| dRPC public | `polygon.drpc.org` | `wss://polygon.drpc.org` | 200 全通 (本文主数据源) | **free 3 (硬拒) / paid 100** |
| polygon-rpc.com 公共 | `polygon-rpc.com` | — | **全 401** ("API key disabled") | n/a |
| LlamaRPC | `polygon.llamarpc.com` | — | **0 字节 (静默拒绝)** | n/a |

**老陈 v1 §6 跨洋 bench**:
- `rpc.ankr.com/polygon` n=30: p50 867 / p95 1203 / p99 1886 / max 2146 ms (无 auth, SG 节点)
- `polygon-rpc.com` n=30: p50 955 / p95 1350 / p99 1572 / max 1642 ms (公共池, 探活 401 但 burst 通过)

**结论 (老叶 v1 §0.2)**: 公共池 + Ankr 公共池 2026-05 时点已停服 / 强制鉴权, 私有托管 vendor 是唯一可行路径.

### 3.2 WebSocket subscription 复用率 (老叶 v1 §3)

**实测 1 个 WSS 连接 × 4 subscription × 30s 窗口 (dRPC):**

| Subscription | 30s msg 数 | 首包 ms | 平均推送间隔 | 平均 msg size |
|---|---:|---:|---:|---:|
| `newHeads` | 18 | 110 | 1.76 s (Polygon 出块) | 2480 B |
| `logs(CTFExchange)` | 0 | n/a | n/a (无成交) | n/a |
| `logs(NegRiskCtfExchange)` | 0 | n/a | n/a | n/a |
| `logs(USDC.e)` | **4256** | 170 | 0.01 s (142 msg/s burst) | 731 B |

**关键发现 (回答老李 v2 开放问题 #3)**: (1) 单连接 N sub 100% 不互相阻塞 (USDC.e 142 msg/s 与 newHeads 1.76s 同步无堆积); (2) 1 个 logs sub + `address: [N contracts]` 数组 = N contract 全覆盖 (server-side filter); (3) dRPC 不支持 `newPendingTransactions` (不需要 mempool, 影响小).

**WSS 推荐拓扑 (老叶 v1 §3.3)**: 1 conn × 3 sub = newHeads (gas/时钟/finality) + logs({address:[CTFExchange,NegRiskCtfExchange,Funder,ConditionalTokens]}) + logs({address:[USDC.e], topics:[Transfer,null,padded(our_funder)]}).

### 3.3 JSON-RPC batch 上限 (老叶 v1 §4)

| Vendor | free | paid | 备注 |
|---|---:|---:|---|
| Alchemy | — | 1000 | 实测 demo batch=500 偶 200 (跨用户共享池) |
| QuickNode | — | 1000 | docs-demo 全通 |
| dRPC | **3 (硬拒)** | 100 | free tier batch>3 返 server 仍 200 batch array, 每 entry error object |

**生产推荐**: primary Alchemy 100/batch, fallback dRPC paid 50/batch. `eth_getLogs` 1 调用 1000 块 = 156 events / 2.5 s / 100 KB; ≤ 1000 块/调用 (Alchemy 默认 10000 上限).

### 3.4 Polygon RPC TTL 矩阵摘要 (老叶 v1 §7)

| Method | TTL |
|---|---|
| `eth_chainId` | 进程生命周期 (const) |
| `eth_blockNumber` | L0 不缓存 (WSS newHeads 替代) |
| `eth_feeHistory(20 blocks)` | L1 5 s |
| `eth_getBalance` (funder) | L1 5 s hot / L2 30 s cold, 写后 bust |
| `eth_getTransactionCount` (nonce) | 本地 nonce mgr 主, RPC 30 s sanity check |
| `eth_call` (view, latest) | L1 3 s (balance-class) / L2 30 s (config) / 永久 (常量) |
| `eth_getTransactionReceipt` (mined) | L3 永久 |
| `eth_getLogs` (toBlock<latest-128) | L3 永久 |
| WSS subscriptions | L0 流式 |

---

## 4. 跨洋链路物理基线

### 4.1 TLS 握手 (老陈 v1 §2)

全部目标 TLS 握手稳定 **~370 ms** (跨洋 ~140-180 ms RTT × 2-3 round-trip). 连接复用后单请求底线 = `starttransfer - appconnect ≈ 350-450 ms`. **决定性优化**: 连接池 keep-alive, 一次握手多次复用; nghttp2 + Asio HTTP/2 client 开 H2 multiplexing, 同一 host 1 个连接.

### 4.2 traceroute (跨洋 RTT, 老陈 v1 §7)

| 目标 | 末跳 RTT | 路径特征 | Hop 数 |
|---|---:|---|---:|
| `gamma-api.polymarket.com` (174.36.196.242) | * (ICMP 屏蔽) | ChinaTelecom AS4134 → AS4837, 后续 ICMP 不通 | 7+ |
| `clob.polymarket.com` (172.64.153.51 / Cloudflare) | 76 ms | CT → CN9 → Cloudflare 边缘 | 13 |
| `www.goalserve.com` (69.64.69.90) | 192 ms | CT → Cogent (LAX → PHX) → Codero 单点 | 17 |
| `rpc.ankr.com` (109.94.99.87) | 343 ms | CT → AS3491 → SG 边缘 | 15 |

**关键观察**: Polymarket = Cloudflare 边缘 (RTT 76 ms 但服务端处理慢, starttransfer 600+ ms); Goalserve = Phoenix Codero 单点 (17 hops, 192 ms, 服务端慢); mtr 不可用, **双向 packet-loss [需后续实测 — 老吴]**.

### 4.3 带宽实测 (老陈 v1 §8)

| 目标 | 平均 payload | 下行均值 | 下行最低 |
|---|---|---|---|
| Polymarket gamma /events (limit=50) | 2.75 MB | **10.46 MB/s** | 6.40 MB/s |
| Goalserve | < 100 KB (常态) | 太小不可估 | — |

**结论**: 跨洋单连接 10 MB/s 出口足以撑 Polymarket gamma 全量轮询; Goalserve 瓶颈是请求频率 + 服务端延迟, 不是带宽.

### 4.4 H2 multiplexing 收益估算

连接池容量 cap (老陈 v1 §10 + 老李 v3 §C 协调):

| Host | 连接数 | concurrent stream | 备注 |
|---|---:|---:|---|
| `clob.polymarket.com` | 2 H2 | 4× | 单连接 max-concurrent-streams 服务端给 256, 我方 cap 4 防慢响应阻塞 |
| `gamma-api.polymarket.com` | 1 H2 | 4× | 单连接 |
| `data-api.polymarket.com` | 1 H2 | 4× | 单连接 |
| `www.goalserve.com` | 2 HTTP/1.1 keep-alive | 1× | Goalserve 不支持 H2 |
| `inplay.goalserve.com` | [需后续实测 — 小段 v3] | n/a | — |
| `oddsfeed.goalserve.com` | [需后续实测 — 升级 plan 后] | n/a | — |
| Polygon RPC primary (Alchemy) | 1 (HTTP) + 1 (WSS) | n/a | deferred |

**超时配置**: Polymarket REST connect 5s / total 8s; Goalserve connect 5s / total **15s** (p99 13s + safety); Polygon RPC connect 3s / total 5s (deferred).

---

## 5. 限流红线 (任一接近 80% 触发告警)

| API | 红线 (80% 触发告警) | 100% 上限 | metric |
|---|---|---|---|
| Polymarket gamma sustained | 48 req/min | 60 req/min | `polymarket_gamma_rps` 1min window |
| Polymarket gamma listing (大对象) | 0.8 req/s | 1 req/s | 同上 + size > 100 KB filter |
| Polymarket CLOB public light | 48 req/min | 60 req/min | `polymarket_clob_public_rps` |
| Polymarket CLOB public heavy (`/books`) | 4.8 req/min | 6 req/min | `polymarket_clob_books_rps` |
| Polymarket CLOB private (L2 HMAC) | 16 req/min | 20 req/min | `polymarket_clob_private_rps` |
| Polymarket data-api | 48 req/min | 60 req/min | `polymarket_data_rps` |
| Polymarket WSS 连接数 (per host) | n/a | 5 conn (老李 v3 §C) | `polymarket_wss_conn_count` |
| Polymarket WSS 重连频率 | 2.4 / min | 3 / min | `polymarket_wss_reconnect_rate` |
| Goalserve `www` sustained | 0.8 RPS | **1 RPS** (官方 logos/settlement, 假定全局同) | `goalserve_www_rps` |
| Goalserve `www` 并发 | 2.4 并发 | 3 并发 (老陈 6 并发 13% 超时, 退保守) | `goalserve_www_concurrent` |
| Goalserve `inplay` per sport | 0.8 RPS | 1 RPS (假设, 待小段 v3 确认) | `goalserve_inplay_rps_per_sport` |
| Goalserve `oddsfeed` settlement per sport | 0.8 RPS | 1 RPS (官方) | `goalserve_oddsfeed_rps_per_sport` |
| Goalserve `matchesIds=` batch | 40 ids | 50 ids (官方) | `goalserve_settlement_batch_size` |
| Polygon RPC (deferred) batch size | n/a | Alchemy 1000 / dRPC paid 100 | — |

**Alert routing**: 80% → 小冯 + 老陈 PD; 90% → 加 GM 老雷; 100% (429 / Cloudflare 1010 / TCP RST burst) → 全员 + 自动 circuit breaker open 5 min.

---

## 6. 月度 sweep 节奏 (小冯接 owner, 与 GM long-term policy 对齐)

参考 GM ADR `2026-05-28-gm-policy-api-monitoring-longterm.md`.

| 任务 | cadence | owner | 输出 |
|---|---|---|---|
| Polymarket 官方 SDK diff (`py-clob-client` + `clob-client-ts` tag-to-tag) | 月度 第 3 周周三 | 老李 (首期 6/19, 老李 v3 §A.3 R7) | ADR + endpoint matrix vN+1 |
| Goalserve endpoint 219 sweep (全 sport × endpoint matrix) | 周度 | 小段 → 小余 ETL (赛季 retest 触发) | v2 增量 + 0B → non-0B 转换告警 |
| Goalserve odds 三态矩阵 (inplay base feed 含/不含 odds) | 月度 (在赛季) | 小段 (v2.1 已立) | 三态 diff |
| Polymarket 限流 + 延迟 baseline 复测 (跨洋, 单 IP) | 周度 | 老陈 → 老吴 SRE 排 24h cron | bench v2 |
| Polygon RPC vendor paid tier 复测 (主节点 ready 后) | 一次性 (Sprint-1 W12) → 月度 | 老叶 + 老吴 | matrix v2 |
| 代理稳定性 24h 长跑 | 持续, 月度报表 | 老吴 SRE | uptime + outlier 次数 |
| 凭证 derive idempotent 自检 (Polymarket) | 每次启动 | 老孙 signer | 启动日志 dump |
| 本 SSOT 文档刷新 | 月度 | **小冯** (接 owner) | v1.1+ |

**Sprint-2 W3 (6/22) 联跑**: 老李 + 老周 + 老姜 测 4-5 conn WSS 拓扑 p99 (老李 v3 §C.4 承诺).

---

## 7. 未实测的 (诚实标注 [需后续实测])

| # | 项目 | owner | 触发条件 / 截止 |
|---:|---|---|---|
| 1 | Polymarket CLOB 私有 (L2) 限流真实上限 | 老李 | Sprint-2 paper-trade 联跑时实压 |
| 2 | Polymarket WSS 单连接 1000 / 2000 / 5000 token 上限 | 老李 (v3 §9 #17) | Sprint-2 加压测 |
| 3 | Polymarket `seconds_delay` 体育市场临场实际值 | 老李 (v2 §9 #2) | 临场实测 (赛事密集时段) |
| 4 | Goalserve `inplay.goalserve.com` p50 / p99 + IP 白名单要求 | 小段 (v3 in flight) | v3 出来后补本 SSOT |
| 5 | Goalserve `oddsfeed.goalserve.com` settlement endpoint 行为 | 老雷 (商务升级 plan) + 小段 (实测) | plan 升级后 |
| 6 | Goalserve `www` 全局 RPS 限制 (logos/settlement 之外) | 老雷 (商务文档查) | Sprint-1 P0 |
| 7 | Goalserve 字段刷新频率实测 (赛事密集时段, UTC 18-23) | 小段 + 老陈 | 任一 sport 旺季时段 |
| 8 | Goalserve 代理 24h 长跑稳定性 (1 个 8s outlier 是否常态) | 老吴 SRE | Sprint-2 |
| 9 | Polymarket 跨时段限流 (峰值 vs 平峰, 24h cron) | 老吴 SRE | 老陈 v1 §11 #1 |
| 10 | Polymarket p99 ≥ 100 样本 (当前 30 样本仅边界参考) | 老吴 cron 收集 ≥ 1000 样本 | Sprint-2 |
| 11 | Polygon RPC paid tier 主节点同区域 p50 (预期 50-150 ms) | 老叶 + 老吴 (主节点 ready 后) | Sprint-1 W12 |
| 12 | Polygon WSS 单连 sub N 上限 (Alchemy 官方 100/conn, dRPC 未明) | 老叶 (v1 §10 #4) | Sprint-2 加压测 |
| 13 | Polymarket `sports-events` 协议路径 (老陈 v1 §2 / §11 #3 404, 已纠正为 `/events?tag_slug=`) | 已关闭 (老李+小段 §10) | — |
| 14 | mtr 双向 packet-loss 统计 (本机仅 traceroute) | 老吴 SRE | 上线后 |

---

## 8. 给 GM 老雷的一句话汇报

**SSOT v1 已完成. 涵盖 4 个数据源 (Polymarket 三组 + Goalserve 三子域 + Polygon 5 vendor), 47 (Polymarket) + 219 探针 16 endpoint family (Goalserve) + 34 RPC 入口点 (Polygon, deferred) = 总实测 endpoint 数 ≥ 300. 限流红线 14 条, 月度 sweep 7 项, 待补测 [需后续实测] 14 项. 数据全部交叉引用原文档不重述, 任何数字均可追溯到原始 CSV 或老李/小段/老叶的 v 文档原文.**

---

(完)
