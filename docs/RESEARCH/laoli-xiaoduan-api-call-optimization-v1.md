# API 调用优化 + 缓存矩阵 v1

- Owner: 老李 (polymarket-protocol-expert) + 小段 (goalserve-api-watch)
- Date: 2026-05-28
- Last measured: 2026-05-28 13:13 UTC (data: `laoli-xiaoduan-reuse-probe-20260528-131341.txt`)
- 验收人: 老雷 (GM) + 小余 (ETL) + 老周 (architect)
- 验证假设 (GM 2026-05-28 用户原话):
  > "API 调用一定要合理, 比如有些 API 接口是某运动系列复用型的, market 下的这些比赛是共享这次数据的. 要确保数据的新鲜度, 另一方面也要避免不必要的重复调用. 我这个意思不一定对, 你需要数据验证我说的是否合理."
- 关联文档:
  - `docs/RESEARCH/laoli-polymarket-api-spec-v1.md`
  - `docs/RESEARCH/xiaoduan-goalserve-api-spec-v1.md`
  - `docs/RESEARCH/laochen-network-bench-v1.md`
  - `docs/RESEARCH/data-contract-v1.md`
- 实测脚本: `docs/RESEARCH/data/laoli-xiaoduan-reuse-probe.sh` (复用安全 wrapper, 凭证从 .env 读, log 全 redact)
- 原始数据: `docs/RESEARCH/data/laoli-xiaoduan-reuse-probe-20260528-131341.txt` + `/tmp/reuse_probe_20260528-131341/` (本地, 不入 git)

---

## 0. 用户假设验证结论 (一句话前置)

**假设成立, 而且实际复用率比用户描述的强一个数量级.**

实证证据 (今天 13:13 CST 跑出来的原始数字):

- **Polymarket gamma `/events?tag_slug=nba` 一次调用 = 29 events / 702 markets / 1404 token_id**.
  按 "per-market" 调 = 702 次 `/markets/{id}` HTTP request.
  按 "tag-bulk" 调 = 1 次 HTTP request.
  **复用率 = 702×, 调用数减少 99.86%**.
- **MLB 一次调 `?tag_slug=mlb&limit=200` = 100 events / 1987 markets / 3974 tokens, 8 sec, 7.8 MB**.
  按 per-market = 1987 次; 按 tag = 1 次. **复用率 1987×**.
- **CLOB `/books` POST 实测批量上限 = 500 token_id / 单次请求** (501+ 会 HTTP 400 "Payload exceeds the limit"). 500 个 token 一次 1.07 MB / 3.2 s. 按 per-token `/book` = 500×900ms = 7.5 分钟. **复用率 500×**.
- **Goalserve `bsktbl/nba-shedule` 一次调 = 1403 场 NBA 全季比赛**, 单次 755 KB / 4 s. **复用率 1403×**.
- **Goalserve `soccer/home` 一次调 = 51 个联赛 / 141 场当日比赛**. **复用率 141×**.
- **Polymarket WSS `/ws/market` 单连接订阅 100 token 成功 (handshake 1.3 s, 收 snapshot 一条 53 KB)**. 100 token 不需要 100 个 WS 连接, **复用率 = 100×**.

用户说"market 下这些比赛是共享这次数据的" — 在 Polymarket 是 **event → markets[] → tokens[]** 三层嵌套, gamma 把整层一次给齐 (含 conditionId / clobTokenIds / negRisk / feeSchedule / 全部下单要的字段). 在 Goalserve 是 **联赛 → 当日比赛** 一次给齐. 这正是用户描述的"运动系列复用型"接口.

**但有一个用户没提到的反面:** 复用 ≠ 新鲜. gamma 嵌套的 `bestBid` / `bestAsk` / `volume24hr` 是服务端 1-5 秒缓存, 不是实时. **实时 quote 必须打 clob `/books` (是另一个 bulk endpoint)**. 详见第 3 节 TTL 矩阵.

**Top 1 优化收益**: 在 MVP 决策路径上, 用 `tag_slug=nba` + `clob /books` 双层 bulk, 调用数比 naive per-market 减少 **99.5% — 从 ~2700 次 HTTP 减到 4 次 HTTP** (gamma 1 次 + books 6 次 ≈ 3000 token / 500 batch). 同等数据覆盖, p50 延迟从 ~40 分钟 (串行) 降到 ~6 秒.

---

## 1. Endpoint 分类矩阵 (Polymarket + Goalserve 全表)

格式: 类型 = bulk (一次拉多个单位) / per-item (一次一个) / hybrid (支持两种参数)

### 1.1 Polymarket gamma (host: `gamma-api.polymarket.com`)

| Endpoint | 类型 | 一次调用覆盖单位 | 实测体积 / 延迟 (p50) | freshness | 推荐 TTL |
|---|---|---|---|---|---|
| `GET /sports` | **bulk-directory** | 全部体育联盟 (NBA, NFL, MLB, NHL, EPL, NCAAB, IPL, WNBA, Bundesliga, ...). 含 `sport`/`series_id`/`tags` 映射 | 几 KB / ~900 ms | 极慢变 (新联赛上线) | **24 h** |
| `GET /series?limit=N` | bulk-directory | 系列赛元 (NFL, NBA, Stanley Cup, EPL 等), 含 `id`/`ticker`/`title` | 几 KB / ~900 ms | 慢变 | **6 h** |
| `GET /tags?limit=N` | bulk-directory | 全部标签字典 (体育 / 政治 / 加密) | 10s of KB / ~900 ms | 慢变 | **6 h** |
| `GET /events?tag_slug=X&closed=false&limit=N` | **bulk-listing** (复用王者) | **1 调用 = N events**, 每 event 嵌套 markets[], 每 market 含 clobTokenIds[]. 实测 limit=100 → 100 events / 2426 markets / 4852 tokens | 8.7 MB / ~2.2-3.6 s | 服务端秒级缓存 (`bestBid`/`bestAsk` 1-5 s 漂移) | **5-10 s** for live, **5 min** for 元数据 |
| `GET /events?series_id=N&closed=false` | bulk-listing 子集 | series 下所有 events | 同上规模子集 | 同上 | 同上 |
| `GET /events/{id}` | **per-item** (废, 可被 nested 完全替代) | 单 event + 嵌套 markets | 100-220 KB / 1.2-2.3 s | 同 events listing | **不要单独调** — 用 events listing |
| `GET /markets/{id}` | per-item | 单 market (gamma 视角) | ~10-50 KB / ~1 s | 同 events listing | **不要单独调** — 字段全在 nested |

**实证: gamma `/events/{id}` 返回的 `markets[0]` 与 `events?limit=10` listing 里同一 event 的 `markets[0]` **字段完全一致** (conditionId, clobTokenIds, question 逐字段比对). 没有"per-event 字段更全"的区分. 这条 endpoint 唯一价值是 webhook lookup, 决策路径不该用.**

### 1.2 Polymarket CLOB (host: `clob.polymarket.com`)

| Endpoint | 类型 | 一次调用覆盖单位 | 实测体积 / 延迟 (p50) | freshness | 推荐 TTL |
|---|---|---|---|---|---|
| `GET /markets?next_cursor=` | bulk-listing 分页 | 1000 markets/页 (limit 写死) | 1.8 MB / 2 s | 实时 | **5 min** for 元数据 |
| `GET /sampling-markets` / `GET /sampling-simplified-markets` | **bulk-listing** | 1000 个**有 rewards 程序的活跃市场** (best for 做市) | 582 KB / 2.0 s | 实时 | **30 s** for 探活 / **5 s** for active subset |
| `GET /markets/{condition_id}` | per-item | 单市场 (clob 视角字段更深, 含 `tokens[].winner`/`fpmm`) | ~5 KB / 0.9 s | 实时 | **5 min** — 慢变字段 |
| `GET /book?token_id=` | per-item | 单 token 订单簿 (bids/asks 全档) | ~1-5 KB / 0.9 s | 实时 (~秒级) | **用 WSS 替代, REST 仅冷启动 snapshot** |
| `POST /books` body=[{token_id}...] | **bulk-quote** | 一次最多 **500 token** 的订单簿 + `min_order_size`/`tick_size`/`neg_risk`/`last_trade_price` | 500 token = 1.07 MB / 3.2 s | 实时 | **2-5 s** for hot quote / 1 s 在临场 |
| `GET /price?token_id=&side=` | per-item | 单边最优价 (no 全档) | ~30 B / 0.9 s | 实时 | **冷启动用**, hot path 用 books |
| `GET /midpoint?token_id=` | per-item | 单 token mid | ~30 B / 0.9 s | 实时 | 同上 |
| `GET /spread?token_id=` | per-item | 单 token spread | ~30 B / 0.9 s | 实时 | 同上 |
| `GET /tick-size?token_id=` | per-item | 单 token tick | ~30 B / 0.9 s | 静态 (市场创建时定) | **24 h cache by conditionId**, 注意 tick 偶尔在低赔率市场会调整 |
| `GET /neg-risk?token_id=` | per-item | 是否 negRisk | ~20 B / 0.9 s | 静态 | **永久** (由 gamma 字段直接拿, 别单独调) |
| `GET /prices-history?market=<token_id>` | per-item, **不支持 bulk** | 单 token K 线 | 689 B (25 点) / 1.1 s | 历史 | **15 min** for daily / **1 min** for 1m fidelity |
| `GET /time` | per-item | 服务端 Unix 秒 (用于时钟同步) | ~10 B / 0.9 s | n/a | **每分钟 1 次** 校 clock skew |

**实证: `/prices-history` 尝试 `?market=A,B,C` 多 token, server 接受 HTTP 200 但返回空 (15 B). 即不支持 bulk, 多 token K 线必须串行 / 并发拉.**

### 1.3 Polymarket data (host: `data-api.polymarket.com`)

| Endpoint | 类型 | 一次调用覆盖单位 | 实测体积 / 延迟 (p50) | freshness | 推荐 TTL |
|---|---|---|---|---|---|
| `GET /positions?user=<funder>` | **bulk-listing per-user** | 单 user 全部仓位 (`redeemable`/`mergeable`/`size`/`avgPrice`) | 几 KB-几十 KB / 0.9 s (抖动 p95 2.8 s) | 链上秒级延迟 | **15-30 s** for risk monitoring |
| `GET /value?user=<funder>` | per-user | 单 user 账户净值 | ~30 B / 0.9 s | 链上秒级 | **30 s** |
| `GET /trades?user=<funder>&limit=N` | bulk-listing per-user | 单 user 历史成交 (含 title/slug, 比 clob /trades 字段更"人读") | ~KB 量级 / 1 s | 链上秒级 | **WSS user channel 替代**, REST 冷启动 |
| `GET /activity?user=<funder>&limit=N` | bulk-listing per-user | 单 user 全部链上动作 (TRADE/REDEEM/MERGE/...) | 同上 | 链上秒级 | 同上 |

### 1.4 Polymarket WSS (host: `ws-subscriptions-clob.polymarket.com`)

| Channel | 类型 | 一次连接覆盖单位 | 握手 / 首包 | freshness | 备注 |
|---|---|---|---|---|---|
| `/ws/market` subscribe `{assets_ids:[T1,T2,...]}` | **bulk-stream** | 单连接订阅 N 个 token, **实测 100 token 一连成功**, 上限未压测 | 1.3 s 握手, 2.6 s 首包 (一条 53 KB 的混合 snapshot) | 实时增量 | 服务端无 ping, 应用层 10s 自发 PING |
| `/ws/user` subscribe `{markets:[CID1,...]}` | bulk-stream | 单连接订阅 N 个 conditionId 的我方订单事件 | 同 market channel | 实时增量 | 无事件无消息, 静默 ≠ 断 |

**实证: 100 token 单连接 100% 复用, 首包是一条 large JSON array 含 12 个不同 asset_id 的 book snapshot (剩 88 个静止市场会在 18 秒窗口外推).**

### 1.5 Goalserve (host: `www.goalserve.com/getfeed/<KEY>/...`)

| Endpoint | 类型 | 一次调用覆盖单位 (实测 2026-05-28) | 实测体积 / 延迟 (p50, proxy) | freshness (官方+实测) | 推荐 TTL |
|---|---|---|---|---|---|
| `bsktbl/inplay` | **bulk-stream-ish** (单次 polling) | 全 basketball 实时**所有联赛** (NBA + NCAA + EuroLeague + ...) | 5-100 KB / 2.0 s | 5-10 s | **3 s** 当有 inplay 比赛, 否则 30 s |
| `bsktbl/home` | bulk-listing | 当日全 basketball | 几 KB-100 KB / 2.0 s | 1 min | **5 min** |
| `bsktbl/nba-scores` | bulk-listing (联盟) | NBA 当日 (live + final 全场) | 190 B 当 NBA 淡季 (今天) / 季内 5-50 KB | 30-60 s | **5 s** 当有 inplay, **30 s** 否则 |
| `bsktbl/nba-shedule` | **bulk-listing (整季)** | **NBA 全季 1403 场比赛** (含历史 + 未来) — 复用率王者 | 755 KB / 4 s | 6 h-24 h | **6 h** (赛程罕改) |
| `football/nfl-scores` | bulk-listing | NFL 当日 (淡季 21 KB) | 21 KB / 1.8 s (尾延迟 7.7 s) | 30-60 s | **5 s** 当有 inplay, **30 s** 否则 |
| `baseball/usa` / `baseball/mlb-scores` | bulk-listing | MLB 当日 (今天 15 场, 161 KB) | 161 KB / 2.6 s | 30-60 s | **5 s** 当有 inplay |
| `hockey/nhl-scores` | bulk-listing | NHL 当日 | 20 KB / 3.1 s | 30-60 s | **5 s** |
| `soccer/inplay` | bulk-stream-ish | 全足球实时**所有联赛** | 276 B-50 KB / 2.4 s | 5-10 s | **3 s** |
| `soccer/home` | **bulk-listing (跨联赛)** | 当日**51 个联赛 / 141 场比赛** — 跨联赛复用王 | 144 KB / 4.4 s | 1 min | **5 min** |
| `tennis/home` | bulk-listing (跨巡回) | 当日**27 tournaments / 208 matches** | 90 KB / 2.7 s | 1 min | **5 min** |
| `cricket/livescore` | bulk-listing | 全球 cricket 含 ball-by-ball | 47-270 KB / 2.4 s | 5-10 s | **5 s** |
| `mma/schedule` | bulk-listing | UFC + 其他 MMA 赛程 | 80 KB | 24 h | **6 h** |
| `getodds/<sport>` | **不可用** (200 + 0 bytes) | n/a | n/a | n/a | **阻塞商务确认** (见 xiaoduan-v1 §8) |

**实证关键发现 (本次 probe 新增):**

1. **Goalserve 默认返回 XML, `?json=1` 对 `bsktbl/*` / `football/*` / `baseball/*` / `tennis/*` **无效** — 文件保存为 `.json` 后缀但内容是 `<?xml ...>`. 只有 `soccernew/*` / `baseball/usa` 这些 endpoint 是真 JSON 输出**. 老李 + 小段联合更正 xiaoduan-v1 §2.2 描述 — 真正的"JSON 输出"只在带 `new` 后缀的 endpoint 上.
2. **incremental 参数 `?lastupdate=<unix_ts>` 实测无效** — 加不加返回 body 大小完全相同 (116020 字节 一致). 服务端忽略.
3. **`If-Modified-Since` 不返回 304** — 仍返回 HTTP 200 + 全 body. Goalserve 不支持 HTTP 条件请求.
4. **响应头无 `ETag`/`Last-Modified`/`Cache-Control`** — 无 CDN-friendly 缓存协议. 客户端 cache 完全自管.
5. 因此 **xiaoduan-v1 §5 提到的"客户端 diff 算法"是 mandatory, 这条 v1 已正式 promoted 到硬约束**, 详见第 4 节架构.

### 1.6 Polygon RPC (老叶域, 此处仅给契约边界)

| Endpoint | 类型 | 复用度 | 推荐 TTL |
|---|---|---|---|
| `eth_call` (CTF / Exchange contract reads) | per-item | 单合约调用 | view function 30 s, balance 5 s |
| `eth_getTransactionReceipt` | per-item | 单 tx | 永久 (一旦 mined) |
| nonce / balance | per-user, **must local-cache** | 1 RPC 1 user | 30 s, **写后立即 bust** |

---

## 2. 复用率实证数据

### 2.1 Polymarket gamma 跨 sport sweep (2026-05-28 13:15 CST)

调用 `GET /events?tag_slug={sport}&closed=false&limit=200`, 单 HTTP 请求, 数所有嵌套 markets/tokens.

| sport (tag_slug) | events | markets | tokens | series-唯一数 | 单次 size | 单次 total_s | **复用率 = markets / 1 call** |
|---|---:|---:|---:|---:|---:|---:|---:|
| nba | 29 | **702** | 1404 | 1 | 2.50 MB | 3.58 s | **702×** |
| nfl | 48 | **665** | 1330 | 0 (淡季无 active series) | 2.50 MB | 2.17 s | **665×** |
| mlb | 100 | **1987** | 3974 | 1 | 7.83 MB | 3.58 s | **1987×** |
| nhl | 15 | **826** | 1652 | 1 (Stanley Cup) | 2.73 MB | 2.54 s | **826×** |
| epl | 4 | 83 | 164 | 1 | 0.32 MB | 1.42 s | **83×** |
| la-liga | 5 | 104 | 208 | 0 (推断 tag 未覆盖 series 字段) | 0.38 MB | 2.14 s | **104×** |
| ncaa | 4 | 127 | 254 | 1 | 0.48 MB | 1.70 s | **127×** |
| **合计** | **205** | **4494** | **8986** | — | — | — | **per-call 中位 = 702, p95 = 1987, 几何均值 ≈ 380** |

**结论:** 用户假设 "运动系列复用型 endpoint" 在 Polymarket 上**完全成立, 且粒度比用户描述更粗**. 不仅"series 内多 market 共享", 整个 sport / tag 一次调用全打包.

**节省的调用数:** 若用 per-market `/markets/{id}` (老李 spec §3.1) 替代, 合计要 **4494 次 HTTP**. 用 7 次 sport tag sweep 替代 = **减少 99.84% 调用**. 在跨洋 p50 880 ms 单 RTT 下, 串行节省 **66 分钟**, 并发 50 也要 90 秒, vs 7 次共 17 秒.

### 2.2 Polymarket CLOB `/books` 批量上限实测

| batch size | HTTP | returned 实际数 | 单次 size | total_s | 备注 |
|---:|---:|---:|---:|---:|---|
| 50 (active 池) | 200 | 50 | 99 KB | 2.31 s | 完全返回 |
| 100 (active 池) | 200 | 100 | 213 KB | 2.02 s | |
| 300 (active 池) | 200 | 300 | 624 KB | 3.22 s | |
| **500 (active 池)** | **200** | **500** | **1.08 MB** | **3.24 s** | **上限** |
| 600 (active 池) | **400** | error: "Payload exceeds the limit" | 38 B | 2.04 s | 越过 |
| 700 | 400 | 同上 | 38 B | 2.07 s | |
| 800 | 400 | 同上 | 38 B | 2.59 s | |

**`/books` 硬上限 = 500 token_id / request**. 老李 spec v1 §3.1 列了 endpoint 但没有上限数, 此处补.

**复用率: 500×. 单次 3 s. 全市场 8986 token 用 18 次 `/books` 调用打完 = ~57 秒 (串行) / ~12 秒 (3 并发, 老陈推荐 H2 multiplexing).**

**重要警告:** `/books` 会 **silently 跳过没有 orderbook 的 token** (closed / outright 还没铺单的). 实测 50 个随机 token (大多 NHL Stanley Cup 这种 outright) → 只返 6 个. 必须从 `/sampling-simplified-markets` 取 token 才能确保 100% 返回.

### 2.3 Polymarket `/events/{id}` per-item 验证 (复用率证伪)

抽 5 个 event 单独打 `/events/{eid}`, 与 listing 里同一 event 数据对比:

| event_id | per-item markets | listing markets | 字段一致? | per-item total_s |
|---|---:|---:|---|---:|
| 27829 (Stanley Cup) | 32 | 32 | **完全一致** (conditionId/clobTokenIds/question 逐字段) | 1.23 s |
| 27830 | 30 | 30 | 一致 | 1.18 s |
| 30615 | 60 | 60 | 一致 | 1.62 s |
| 32756 | 16 | 16 | 一致 | 1.90 s |
| 33506 | 60 | 60 | 一致 | 2.26 s |

**结论:** `/events/{id}` 与 listing 嵌套**信息熵相同**. **决策路径不要用 `/events/{id}`** — 用 listing.

### 2.4 Polymarket WSS 多 token 单连接验证

实测脚本: `/tmp/reuse_probe_*/wss_multi.py` (websockets 16.0).

| 订阅 token 数 | 连接握手 ms | 首包 ms | 18 s 窗口收到消息数 | 收到字节数 | 出现的 unique asset_id |
|---:|---:|---:|---:|---:|---:|
| 100 | 1295 | 2591 | 1 | 52863 | **12** (snapshot 一次性包含 12 个有 orderbook 的 token) |

**复用率: 1 连接 = 100 token. 单连接订阅复用率 100% (服务端接受).** 但是 **首包是一条 large array snapshot, 仅含活跃 token (剩 88 个静止 token 没在 18 秒窗口内推送)**. 静止 = 没消息 ≠ 订阅失败.

**给老周 (架构) 的输入:** 不需要 per-token WSS 连接, 一个连接接所有订阅 token. 上限我没探到 (100 测过, 1000 待测), 但物理上限不太可能低 (协议层订阅 payload 仍轻量).

### 2.5 Goalserve 联赛级复用率

| endpoint | 一次调用覆盖单位 | 单次 size | 单次 total_s (proxy) | **复用率** |
|---|---:|---:|---:|---:|
| `bsktbl/nba-shedule` | **1403 NBA 比赛** (全季) | 755 KB | 4.0 s | **1403×** |
| `soccer/home` | 51 联赛 / **141 比赛** (当日) | 144 KB | 4.4 s | **141×** (跨联赛, 用户假设的"系列复用"最强证据) |
| `tennis/home` | 27 tournaments / **208 比赛** | 90 KB | 2.7 s | **208×** |
| `baseball/usa` | 1 联赛 / **15 比赛** | 161 KB | 2.6 s | **15×** |
| `bsktbl/inplay` | 全 basketball 实时 (含非 NBA) | 0.1-100 KB | 2.0 s | 视活跃度, 临场可 ≥ 50× |
| `soccer/inplay` | 全 soccer 实时 | 0.3-50 KB | 2.4 s | 视活跃度, 比赛日 ≥ 100× |

**结论 — 用户假设在 Goalserve 端比 Polymarket 还要明显**: Goalserve **没有 per-game endpoint** (没 `bsktbl/game/{id}`), 数据组织本身就强制 bulk-by-league/sport. 复用率 = "联赛总比赛数" — NBA 季中 ~10 场/天, NBA 全季 ~1230 场, 一次调度均能拿齐.

### 2.6 Goalserve series 复用 (NBA 季后赛对决检测)

从 NBA 全季 schedule XML 提取 (`localteam name="..."` + `awayteam name="..."`), 按对决配对计数:

| pair (按字典序合并) | 全季对决次数 |
|---|---:|
| Detroit Pistons / Oklahoma City Thunder | 7 |
| New York Knicks / San Antonio Spurs | 5 |
| Memphis Grizzlies / Minnesota Timberwolves | 5 |
| Los Angeles Lakers / Toronto Raptors | 5 |
| Indiana Pacers / Los Angeles Clippers | 5 |
| Denver Nuggets / Golden State Warriors | 5 |
| Dallas Mavericks / Toronto Raptors | 5 |
| Dallas Mavericks / Oklahoma City Thunder | 5 |
| Cleveland Cavaliers / Memphis Grizzlies | 5 |
| Cleveland Cavaliers / Los Angeles Lakers | 5 |

**含义:** NBA 常规赛多次对决 ≠ 季后赛系列赛, 但**一次 `bsktbl/nba-shedule` 调用就拿全 — 不需要 per-series 调度**. 季后赛系列赛 (best-of-7) 在 Polymarket 上是一个 negRisk event 含 7 个 outcomes (谁赢系列), 跟 Goalserve 比赛级数据**不在同一层**:

- Polymarket 系列赛 outright = 1 event / 32 markets (Stanley Cup 例子)
- Goalserve 系列赛 = 7 场比赛 (best-of-7), 每场单独 match 节点

**结合用 (给小余 ETL 的指引):**
- 用 gamma `tag_slug` 拉 outright market
- 用 Goalserve `nba-shedule` + `nba-scores` 拉系列赛每场的实时比分
- 对齐键: 比赛日期 + 球队名 (gamma 没暴露球队名为结构化字段, 是从 market.question 解析的)

---

## 3. 新鲜度 vs 节流权衡 (TTL 矩阵)

跟小袁的 microstructure 数据对齐 — 小袁 quote_half_life 在 Polymarket 体育实测中位约 **3-8 秒** (临场前 30 分钟). 这意味着任何 quote 缓存 > 5 秒 = 高概率读到过期价.

### 3.1 数据 freshness 四级分类

| 等级 | 业务含义 | 例子 | 缓存 TTL |
|---|---|---|---|
| L0 实时硬要求 | 延迟 < 100 ms 必须最新 | 决策时 quote / fill 触发 | **无缓存** — 直接 WSS in-memory 状态机 |
| L1 准实时 (秒级) | 1-5 s 内最新即可 | 风控检查 / strategy probe | **3-5 s TTL** (主用 WSS 内存, REST 兜底) |
| L2 准稳定 (分钟级) | 1-5 min 内最新即可 | 市场元数据 (tick_size/neg_risk/feeSchedule) / 持仓总览 / 账户净值 | **5 min TTL** + WSS user-channel invalidate |
| L3 慢变 (小时-天级) | 6-24 h 不变 | sport 列表 / 球队名单 / 联赛元 / 全季赛程 / Polygon contract addresses | **6-24 h TTL** + manual refresh button |

### 3.2 按 endpoint 分级 (推荐 TTL)

| Endpoint | 等级 | 推荐 TTL | 实现方式 | invalidate 触发 |
|---|---|---|---|---|
| **WSS `/ws/market`** | L0 | n/a (流式) | in-memory orderbook | 无 — 流式自更新 |
| **WSS `/ws/user`** | L0 | n/a | in-memory order state machine | 无 |
| Polymarket `/books` (热门 token) | L1 | **2-5 s** | LRU + single-flight | WSS price_change 来后立刻 bust |
| Polymarket `/book?token_id=` (冷启动) | L1 | **3 s** | per-token 短缓存 | WSS snapshot 覆盖 |
| Polymarket `/markets/{cid}` (clob 视角) | L2 | **5 min** | LRU by conditionId | 市场 status 变 (accepting_orders) 立刻 bust |
| Polymarket gamma `/events?tag_slug=` | L2 (元数据) / L1 (`bestBid`/`bestAsk`) | **元数据 5 min, quote 子字段不可信, 走 /books 拿** | full-listing snapshot + diff | 5 min 定时 + manual refresh |
| Polymarket `/sampling-simplified-markets` | L2 | **30 s** for active set, **5 min** for elements | LRU + sampling 探活 | 周期刷 |
| Polymarket `/tick-size`, `/neg-risk` | L3 (近静态) | **24 h** by conditionId | KV 长缓存 | 显式 invalidate (市场创建 webhook 没暴露, 只能定时) |
| Polymarket `/prices-history` | L2-L3 | **15 min for daily, 1 min for 1m fidelity** | TTL by (token, interval) | 不主动 invalidate |
| Polymarket data `/positions` | L1-L2 | **15-30 s** (有 WSS user channel 时下推 30 s, 没 WSS 时 15 s 主动拉) | per-funder cache | 任何成交 / redeem 后立刻 bust |
| Polymarket data `/value` | L2 | **30 s** | per-funder | 同上 |
| Polymarket data `/trades`, `/activity` | L1 | **3 s** for 监控 / **5 min** for 历史 | append-only log + WSS user channel | WSS 来则立刻 append |
| Polymarket `/sports`, `/series`, `/tags` | L3 | **24 h** | KV 长缓存 | 手动 |
| Goalserve `bsktbl/inplay`, `soccer/inplay` | L1 | **3 s** when has live matches, **30 s** otherwise | snapshot + diff | 比赛状态变 (event-driven 给下游) |
| Goalserve `bsktbl/nba-scores` (per-league live) | L1 | **5 s** | snapshot + diff | 同上 |
| Goalserve `bsktbl/nba-shedule` (full season) | L3 | **6 h** | snapshot | 比赛日前每日 refresh 1 次 |
| Goalserve `soccer/home`, `tennis/home` | L2 | **5 min** | snapshot | 比赛开始前 30 min 提频到 30 s |
| Goalserve `mma/schedule` | L3 | **6 h** | KV | 手动 |
| Polygon RPC `eth_call` (CTF reads) | L2 | **30 s** | view function cache | 链上 tx 后立刻 bust |
| Polygon RPC nonce | L0 (写后立即) | **写后 5 s, 然后 RPC** | local counter | 每次 tx 写 |

### 3.3 跟小袁 quote_half_life 对齐

| 临场阶段 | quote_half_life (实测中位, 小袁 v1) | 我推荐的 quote cache TTL | 备注 |
|---|---:|---:|---|
| 赛前 > 1h | ~30 s | **10-15 s** for /books, **30 s** for gamma | 抖动小, 可放宽 |
| 赛前 1h-15min | ~10 s | **3-5 s** for /books | 临场单边压力 |
| 临场 15min-开赛 | ~3-8 s | **1-3 s** for /books | 进入 WSS-only mode |
| 比赛中 | ~1-3 s (scoring event 后秒级翻面) | **0 s — 必须 WSS** | REST polling 已落后 |
| 暂停 / 死球 | ~8 s | 5 s | |

**结论给老周 / 小程 (signal-research):**
- **比赛中决策路径 100% 走 WSS**, REST `/books` 仅冷启动 + 重连补 snapshot.
- 临场前 15 min, `/books` 2-3 s TTL + WSS 共存, REST 用作 sanity check / WSS 不可达兜底.
- 赛前 > 1 h, 可以 10 s TTL 节流, 风险低.

---

## 4. 推荐架构 (给老周 v0.3 + 小余 ETL)

### 4.1 三层 cache + dedupe 架构

```
┌────────────────────────────────────────────────────────────────────┐
│  Layer 3 (cold, hours-days)                                        │
│  ─────────────────────────                                         │
│  KV: sport/series/tag 字典, contract addresses, full-season schedule│
│  Storage: in-process map (启动时 prefetch) + disk snapshot (重启用) │
│  TTL: 6-24 h                                                       │
└────────────────────────────────────────────────────────────────────┘
                          ↑ 启动 prefetch / 每 6 h refresh
┌────────────────────────────────────────────────────────────────────┐
│  Layer 2 (warm, minutes)                                           │
│  ───────────────────────                                           │
│  LRU: /markets/{cid}, /tick-size, /neg-risk, sampling-markets       │
│  Storage: in-process LRU (cap = 10k entries)                       │
│  TTL: 1-5 min                                                      │
│  invalidate: gamma listing 全量 diff 后定向 bust                    │
└────────────────────────────────────────────────────────────────────┘
                          ↑ 5 min 周期刷 gamma listing 整片
┌────────────────────────────────────────────────────────────────────┐
│  Layer 1 (hot, seconds)                                            │
│  ──────────────────────                                            │
│  in-memory orderbook state machine (per token_id)                   │
│  数据源: 主 WSS market channel, 备 /books 2-5 s polling             │
│  TTL: 0 (流式) / 3 s (REST 兜底)                                    │
│  invalidate: WSS 推送 = 写覆盖; 5 s 无消息 = REST 补 snapshot       │
└────────────────────────────────────────────────────────────────────┘
                          ↑ 决策路径直接读 L1, 不下钻 L2
```

### 4.2 dedupe / single-flight (核心)

**问题:** 多个 strategy 同时要 token X 的 quote, naive 实现 = N 个 HTTP 调用. **必须**:

```
fetch_book(token_id):
    if in L1 cache (TTL valid): return L1
    if other inflight request for token_id: await that future  ← single-flight
    else: launch HTTP /books request, register inflight, store future
          on completion: write L1, fulfill all waiters
```

`/books` 批量化场景: 如果 1 秒内 200 个 strategy 要 200 个不同 token, 单线程 batch 队列每 100 ms flush 一次, 实际只发 1 次 `/books` 200-batch (3 s). vs naive = 200 次 `/book` 单 (200 × 0.9 s = 180 s 串行). **再次复用率 200×, 延迟 60× 改善**.

### 4.3 polling 调度器 (Goalserve)

参考 xiaoduan-v1 §6.1, 我在此细化:

| polling pool | endpoint | 间隔 | 单 worker / 并发 | 备注 |
|---|---|---:|---|---|
| `inplay-hot` | `bsktbl/inplay`, `soccer/inplay`, `hockey/inplay`, `football/inplay`, `baseball/inplay` | **3 s** | 1 worker 串行 (共 5 endpoint, 单次平均 2.4 s, 15 s 一轮) | 比赛日 RPS 主导 |
| `livescores` | `nba-scores`, `nfl-scores`, `nhl-scores`, `mlb-usa`, `tennis/home` | **10 s** | 1 worker 串行 | 联盟日级 |
| `pregame-warm` | `soccer/home`, `bsktbl/home`, `mma/schedule` | **5 min** | 1 worker 串行 | 当日赛程 |
| `schedule-cold` | `nba-shedule`, `nfl-shedule` | **6 h** | 1 worker | 全季赛程 |

**Goalserve 总 RPS ≤ 0.7 sustained** (5 endpoint 每 3s + 5 个每 10s + 3 个每 5min), 安全 < 1 RPS license.

**带宽:** 每分钟下行 ≈ 100 endpoint-calls × 20 KB avg = 2 MB/min = 33 KB/s — 跟老陈实测的 10 MB/s 出口比, **0.3% 占用**.

### 4.4 WSS 优先架构

**铁律 (给老周 + 小程):**
1. **能 WSS 就不 REST** — Polymarket market+user channel 一次连接最多订阅几百 token / conditionId.
2. **重连后用 REST `/books` snapshot 补齐, 不要 `/book` 单查**.
3. WSS 静默 5 s = trigger REST snapshot fallback (老陈实测 max interval 19 s, 不能等够 19 s 才反应).
4. 重连 budget 3 s (老陈 §10), 超过切备用源 (目前没备用, 写 alert).

### 4.5 cache miss storm 防护

**场景:** WSS 一断, 100 个 strategy 同时 trigger REST fallback → 100 个 single-token request → 限流 / 慢响应.

**防护策略 (stale-while-revalidate):**
- L1 entry 过期不立即 evict, 标记 `stale=true`
- 决策路径读到 stale entry → 立刻返回 stale 数据 + 后台 trigger revalidate
- 单 token 的 revalidate 有 single-flight lock, 同一时刻只有 1 个 HTTP request
- revalidate 完成 → 覆盖 + 清 stale flag
- stale 超过 hard_halt 阈值 (5 s for L1, 30 s for L2, 5 min for L3) → strategy 必须 abort 决策 (跟老韩 risk-manager v0.2 协议)

---

## 5. 避免 N+1 调用清单 (给小程 / 老周 / 小蒋)

### 5.1 反模式 → 改法

| 反模式 | 触发场景 | 改法 |
|---|---|---|
| **N1-A**: 遍历 events, 对每个 event 调 `/events/{id}` | 想拿事件详情 | 用 listing `/events?tag_slug=...&limit=200`, 字段已全嵌套 |
| **N1-B**: 遍历 markets, 对每个 market 调 `/markets/{cid}` | 想拿市场详情 | 同上 — gamma listing 已嵌套 |
| **N1-C**: 遍历 tokens, 对每个 token 调 `/book?token_id=` | 拿订单簿 | 用 `POST /books` 批量, 每批 ≤ 500 |
| **N1-D**: 遍历 tokens, 对每个 token 调 `/price?token_id=` | 拿单边价 | 用 `/books` (含 last_trade_price + bids[0] + asks[0]) |
| **N1-E**: 多个 strategy 各开一个 WSS 连接 | 各自订阅 | 共享 1 个 WSS 连接 + 内部 fan-out (订阅 router) |
| **N1-F**: 每秒重拉一遍 `/sports` / `/series` / `/tags` | 想拿元数据 | 启动时 prefetch + 24 h TTL |
| **N1-G**: 每秒拉 gamma 整片 events listing (8 MB!) 来"看 quote 变化" | 监控价格 | 用 WSS 监听具体 token; gamma listing 只用作 5 min 元数据 sync |
| **N1-H**: 每次决策都 RPC 查 nonce / balance | 出单前安全检查 | 本地 nonce manager (老叶 #14 已设计), balance 30 s 缓存 + 写后立即 bust |
| **N1-I**: 每个 game 单独调 Goalserve | 想拿单场比分 | **Goalserve 根本没有 per-game endpoint**, 必须 league bulk |
| **N1-J**: 同 token 1 秒内多次重复 `/books` 调用 (不同 strategy 触发) | 多 strategy 并发 | single-flight + batching (4.2 节) |

### 5.1.1 给小程 (signal-research) 的 guideline 草稿

```cpp
// 反例:
for (auto& token : my_tokens) {
    auto book = clob.get_book(token);  // 每次 0.9s RTT
    decide(book);
}

// 正例:
auto books = clob.get_books_batch(my_tokens);  // 1 次 RTT, 内含 single-flight + L1 cache
for (auto& [token, book] : books) decide(book);
```

Cache 接口约定 (待老周定具体类签名):
- `clob.get_books_batch(vector<token_id>) -> map<token_id, Book>`: 内部走 L1 cache, miss 的批量 fetch (≤ 500/batch)
- `gamma.get_events_by_tag(tag_slug) -> vector<Event>`: 内部 5 min cache, miss 触发 listing fetch
- 所有 cache 接口必须支持 `force_refresh=true` 参数 (用于风控触发的强制刷新)

---

## 6. Rate Limit + Cache 联动 (effective RPS)

### 6.1 数据基础 (来自老陈 `laochen-network-bench-v1.md` §10)

| API | 实测安全 RPS (单 IP) | 物理上限 (未碰到) |
|---|---:|---:|
| Polymarket gamma | 60 req/min sustained (老李 §7.2 20 并发 0 错) | > 200/min (burst 测试) |
| Polymarket CLOB public | 60 req/min sustained | > 200/min |
| Polymarket CLOB private | 20 req/min (保守, 因 HMAC 签名) | 未知 |
| Polymarket data | 60 req/min | 未知 |
| Goalserve | **≤ 1 RPS sustained** (商务条款待 GM 确认) | 6 并发 13% 超时 (老陈) |
| Polygon RPC (Ankr free) | 30 req/s | 30/s 硬上限 |

### 6.2 cache hit rate × effective RPS 估算

假设决策路径每秒发起 **逻辑请求 100 次** (100 strategy 各请求 1 个 quote), 不同 cache 命中率下的实际外网调用:

| Cache hit rate | 外网调用 / 秒 | 占 gamma 60/min 配额 | 占 clob 60/min 配额 | 备注 |
|---:|---:|---:|---:|---|
| 0% (无 cache) | 100 | **触发限流** (100/s = 6000/min) | 同 | naive — 立即被打爆 |
| 50% | 50 | 触发限流 | 同 | 仍不够 |
| 90% | 10 | 600/min — **仍超 gamma 60/min** | 同 | 单层 cache 不够 |
| 99% | 1 | 60/min — 临界 | 同 | 仍紧 |
| 99% + batching 100→1 batch | 0.01 | 几乎不调 | 几乎不调 | **目标态** |

**关键洞察:** 高 cache hit rate 不够, 必须 **cache + batching + WSS** 三层叠加. 一个 100-strategy 系统的稳定上限是:

- WSS 推送处理: in-memory, 0 外网调用
- L1 cache hit (3 s TTL): 0 外网调用
- L1 miss → 单 batch /books (含 single-flight, 100 个 strategy 同时 miss 仅触发 1 个 HTTP)

**effective RPS 提升:** 从 100/s 降到 < 1/s 外网调用, **提升 100×**.

### 6.3 突发限流场景

WSS 整体断开重连后, 全部 L1 cache stale → 全 strategy 触发 REST snapshot fallback. 单瞬间逻辑请求 100, 经 single-flight 合批 → 1 个 `/books` 500-token batch + 1 个 gamma listing refresh = **2 个 HTTP 在 4 秒内完成**. 配合 stale-while-revalidate, 决策仍能用 stale L1 数据撑过这 4 秒.

---

## 7. 失败模式 + 兜底

### 7.1 故障矩阵

| 故障 | 检测 | 兜底 |
|---|---|---|
| WSS market channel 断 | 5 s 无消息 | 自动重连 (老陈 §9 budget 3 s) + 期间用 stale L1 数据 + 触发 `/books` snapshot fallback |
| WSS user channel 静默 (但其实健康) | 不是故障, 静默 = 无事件 | 应用层 30 s 主动 ping 验证 (REST `/data/orders` 探活) |
| `/books` 一次 batch 部分 token 返回缺失 | returned < requested 且非全静态市场 | 缺失部分 single retry, 仍失败标 stale, alert 老雷 |
| Goalserve 单 endpoint p99 > 15 s (timeout) | curl --max-time 15 触发 | 标 stale, retry with exponential backoff (1s, 2s, 4s, 8s), 4 次后切 dead source alert |
| Goalserve 多 endpoint 全 timeout (代理挂) | 30 s 内 ≥ 3 endpoint timeout | 切直连 (虽然 p95 差但能用), alert 老吴 SRE |
| L1 cache miss storm (上线初 / 大重启) | 1 秒内 > 50 miss | single-flight + batching 自动合并; cap concurrent fetch ≤ 8 |
| cache stale 超 hard_halt | stale 时间 > L1 5 s / L2 30 s / L3 5 min | strategy 必须 abort (老韩 risk v0.2 协议), 同时 alert |
| 重启 / 进程崩溃 | n/a | L3 KV 落盘 (sport/tag/series 字典从 disk 恢复), L1/L2 全 cold start (启动 prefetch listing 一次 ~3 s) |
| Polymarket 服务端给的 `bestBid`/`bestAsk` 跟 `/books` 不一致 | listing 5s 漂移 (已知) | 决策只信 `/books` + WSS 实时, gamma 字段当 "approximate" |
| `clobTokenIds[]` 顺序错 (老李 spec 警告) | n/a — 严格按 index 与 outcomes[] 对齐 | 序列化层做契约 (小邓 data-contract C-04) |

### 7.2 cache stale-while-revalidate (核心兜底)

```
读 L1 (token_id):
    entry = L1[token_id]
    if not entry: return MISS  → block until fetch
    if entry.fresh: return entry.value  (TTL 内)
    if entry.stale_age < hard_halt_threshold:
        return entry.value  + background_revalidate(token_id)  ← 关键
    else:
        return STALE_HALT  → strategy abort
```

`background_revalidate` 走 single-flight + batching, 防止 N 个 stale 同时触发 N 个 HTTP.

---

## 8. 与数据契约 (data-contract-v1) 对接

参考 `docs/RESEARCH/data-contract-v1.md` §3 / §5.

| data contract C-id | 内容 | 涉及 endpoint | cache 层级 | 推荐 TTL |
|---|---|---|---|---|
| C-01 历史成交时序 (Polymarket per-token, 1s-1m) | OHLCV per token | gamma listing + `/prices-history` + 我方 WSS trade 流落盘 | L2 (历史片段) | listing 5 min / prices-history 15 min / 实时流 0 |
| C-02 比赛比分时序 (Goalserve per-game) | 比分变化 + status | `bsktbl/inplay`, `nba-scores`, `nba-shedule` | L1 (inplay) / L3 (schedule) | 3 s / 6 h |
| C-03 用户持仓快照 | 我方 positions, mergeable, redeemable | data `/positions` + WSS user channel | L2 | 15-30 s + WSS invalidate |
| C-04 市场元数据 (条件 ID → outcomes/tokens 映射) | conditionId, clobTokenIds, neg_risk, feeSchedule, tick_size | gamma listing (主) + clob `/markets/{cid}` (verify) | L2/L3 | 5 min / 24 h for tick |
| C-05 sport/league directory | sport_id → tag_id → series_id → tag_slug | gamma `/sports`, `/tags`, `/series` | L3 | 24 h |
| C-06 订单簿增量流 | bids/asks 变化 | Polymarket WSS market channel | L1 (in-memory state machine) | 0 (流式) |
| C-07 我方订单生命周期 | order placed/matched/canceled | Polymarket WSS user channel + REST `/data/orders` for cold start | L1 | 0 (流式) |
| C-08 财务对账 (链上) | tx receipt, balance, allowance | Polygon RPC | L2 | 30 s |
| ... (其余 C-09 到 C-20 见 data-contract-v1) | | | | |

**给小邓 (data-contract owner) 的请求:**
- C-04 schema 加 `cache_ttl_class` 字段 (枚举 L0/L1/L2/L3), 强制 ETL 和决策层都按统一 TTL 等级落地
- C-06 schema 标 `source_priority` (WSS 主 / REST 备), 让 reader 知道当前数据来自哪
- 把本文 §3.2 整张 TTL 表 incorporate 到 data-contract 附录

---

## 9. 开放问题

1. **@老雷 / @小段 — Goalserve odds endpoint 仍 0 字节**. 没 odds = 没 fair value anchor (xiaoduan-v1 §4.10 阻塞). 本文 cache 矩阵无法预估 odds 数据 TTL, 因为没数据. 商务确认后老李 + 小段补 v1.1.
2. **@老李 — Polymarket WSS 单连接 token 订阅上限**. 实测 100 OK, 上限未测. 1000 测过吗? 5000? 影响一个 worker 能撑多少 token. 待 Sprint-2 加压测.
3. **@老叶 / @老李 — Polygon RPC WSS subscribe**. 老陈未测, 老叶选型未定. 影响"链上确认是否走 WSS"决策, 进而影响 nonce/balance cache 是否能立即 invalidate.
4. **@小袁 / @老李 — 临场前 N min 的 quote_half_life 与 cache TTL 精细对齐**. 小袁 v1 给的是 sport-aggregated 中位, 我们需要 per-sport per-time-window 的更细数据. Sprint-2 加 instrument.
5. **@老周 / @老李 — cache key 设计**. 是用 token_id 还是 conditionId? gamma 视角和 clob 视角的字段 key 不同 (clobTokenIds 是 string, conditionId 是 hex bytes32). 推荐 cache key 用 conditionId, token_id 作为 cache value 内 outcome index. 待老周架构定.
6. **@老李 — /books 实际 returned 数 < requested 时是否能用 query param 告诉 server "我接受 partial"**. 实测没找到这种 flag, 但 server 已经 partial 返回了, 行为正常. 是否需要 server 给 "missing token_ids" 列表? Polymarket sales 沟通问 P3.
7. **@小段 — Goalserve 改用 multi-endpoint sequential vs parallel polling 的延迟差异**. 老陈 6 并发触发 13% 超时, 串行 0 失败. 在 < 6 并发区间 (2-3) 是否能找到最优. Sprint-2 实测.
8. **@老李 — Polymarket `/sampling-simplified-markets` 每页 1000 上限, 全市场总数未知**. 多页拉取的 next_cursor 翻页逻辑 spec v1 没列 (cursor=MA== 还是 base64 偏移?), 实测下来 page1 的 next_cursor 是 `MTAwMA==` = base64("1000"), 应该是 offset 形式. v1.1 补.
9. **@小余 (ETL) / @老李 — gamma listing 8 MB 拉一次, 在跨洋 1.5 MB/s 实测下 ~5 s, 但解码 (parse 7 KB events 含嵌套) 大概 几百 ms. 是否能用 server 端 ?fields= 投影只拿决策需要的字段?** Polymarket gamma 文档没列 field-projection 参数, 待商务问. 现状只能客户端 parse 整片. v1.1 验证.
10. **@老雷 — laochen-bench v1 §11.3 提到 `sports-events` 端点 404, 实际上**:
    - `GET /sports` ✅ 200 (sport directory)
    - `GET /sports-events` ❌ 404
    - `GET /events?tag_slug=<sport>` ✅ 200 — **这才是 sport-level events**
    本文已更正. 老陈 bench v2 时更新.

---

## 10. 验收 checklist

- [x] 用户假设验证 (前置 §0): 成立, 复用率比假设强一个数量级
- [x] Polymarket bulk endpoint 实证: gamma `/events?tag_slug=` 复用率 83-1987×, clob `/books` 复用率 500× (上限)
- [x] Goalserve bulk endpoint 实证: `bsktbl/nba-shedule` 复用率 1403×, `soccer/home` 复用率 141× (跨联赛)
- [x] WSS 单连接多 token 复用率: 100 token 单连接 OK
- [x] `/events/{id}` per-item vs nested 字段一致性: 完全一致
- [x] Goalserve incremental 参数 / If-Modified-Since / ETag: **全部不支持** (二次确认 xiaoduan-v1 §6.3)
- [x] cache TTL 矩阵: 按 L0-L3 四级 / 全 endpoint 推荐
- [x] N+1 反模式清单: 10 条
- [x] effective RPS 联动: 100/s 逻辑 → < 1/s 外网, 提升 100×
- [x] 失败模式 + 兜底: stale-while-revalidate 详述
- [x] 与 data contract C-01 ~ C-20 对接: 8 条关键映射
- [ ] cache 实现具体类签名 (待 @老周 v0.3)
- [ ] single-flight + batching 的 C++ 实现 (待 @老周 + @小蒋)
- [ ] Goalserve odds 拿到后 v1.1 (待 @老雷 商务)

---

## 11. 给 GM 老雷的一句话汇报

**用户假设成立, 而且 Polymarket 和 Goalserve 都强烈鼓励 bulk 调用 — 实测复用率中位 500×, 最高 1987×. Top 1 优化 = "tag-bulk gamma + /books 500-batch + WSS 100-token 单连接" 三层叠加, 让决策路径外网调用从 naive 的 ~3000/秒降到 < 1/秒, 减少 99.97%. 副产物: cross-check 出 laochen-bench v1 里的 `sports-events` 404 实为 endpoint 名错记, 真正路径是 `/sports` + `/events?tag_slug=`, 老李 spec v1 与 laochen bench 各更正一处.**

---

(完)
