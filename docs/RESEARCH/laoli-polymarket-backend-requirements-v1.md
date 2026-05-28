# Polymarket 协议层后端接口需求 v1

- Owner: 老李 (polymarket-protocol-expert, #07)
- Date: 2026-05-28
- Last review: 2026-05-28
- Wave: W4 → W5 联调前置
- 状态: DRAFT v1, 等 5 个 hard ask 反向 owner 会签 (老周架构 / 小蒋 paper / 老唐 audit / 小冯 PM WSS / 老沈 security)
- 关联:
  - `docs/RESEARCH/laoli-polymarket-endpoint-matrix-v3.md` (v3 wire 契约, HMAC 4 bug §A + 14 test vector §D)
  - `docs/RESEARCH/laoli-polymarket-endpoint-matrix-v2.md` (gamma/clob/data REST + WSS market/user 实测)
  - `docs/RESEARCH/laoli-polymarket-reverse-sports-live-v1.md` (sports/live SSR + 第 5 host 发现)
  - `docs/RESEARCH/xiaojiang-paper-trading-engine-v0.2-cpp.md` (paper engine, VirtualMatcher / PaperSigner / PaperLedger 现状)
  - `docs/RESEARCH/laotang-audit-schema-v1.md` (12 AET) + `laotang-audit-schema-v1.1.md` (14 AET +30/+31)
  - `docs/RESEARCH/laozhou-architecture-v0.6-e2e.md` (W5-T1 / W5-T2 wss_client + reconnect 任务)
- 验收人: 老周 (PMClient 接口契约 ack) + 小蒋 (paper engine 接 PMClient 抽象) + 老唐 (AET +3 扩到 15) + 小冯 (WSS subscribe schema) + 老沈 (SecureBuffer + HMAC 不入日志)

---

## 0. 目的 + 一页纸

**目的:** W4 后端已落 paper engine (小蒋), 但 paper engine 现在跟 PM 协议层是脱钩的 (mock 数据). 本文反向给后端提清单, 告诉他们 **W5 联调时真实 PM 协议层需要哪些数据 / 哪些状态 / 怎么 fake (paper) 怎么真接 (live)**.

**一页纸结论:**

1. **paper / live 同接口契约, backend 二实现** — 走 `PMClient` 抽象类 + paper / live 编译期 CMake 分流 (与 paper engine v0.2 PR-7 同款 signer 隔离)
2. **WSS 第 5 host `wss://sports-api.polymarket.com/ws` 公开免鉴权 sports channel** — paper / live 双 mode 都走真实流, 不 mock (sports channel 无私钥, 不影响 paper)
3. **CLOB 私有 WSS (`/ws/user`) HMAC + paper 不需要** — paper 用 VirtualMatcher emit 虚拟 fill event, live 才接 `/ws/user` 真 fill
4. **4 ts 进 audit / WAL** — payload 自带 `timestamp` / `last_update` 字段, 严禁本地 `now()` 替代 (R-20 红线)
5. **HMAC 4 bug enforce** — v3 §A 4 个 bug + 14 wire vector 给老孙单测, backend live 实现强制 byte-equal
6. **15 AET (audit)** — 老唐 v1.1 已 14 (+30/+31 strategy decay), 我提议再扩 +3 (BOOKED/EXPIRED/SETTLED) → 总 15 个, 覆盖 PM 7 enum
7. **paper mode 不打 Polygon RPC** — gas / nonce / block 全部 mock 内存, 老叶 v1 设计的 chain 抽象层负责 (但 paper engine 走 mock provider)

---

## 1. PaperEngine ↔ PM 协议层契约 (mock 阶段 + live 接口对照)

paper mode 不真上链, 但必须**伪造 PM-side 状态**让上层逻辑 (RM / Signal / Position Ledger) 一致.

### 1.1 PM-side 概念 paper / live 双 mode 矩阵

| # | PM-side 概念 | paper mode 怎么伪造 | live mode 接什么 endpoint | owner |
|---|---|---|---|---|
| C-1 | OrderBook (yes_bid / yes_ask L2) | **从 PM WSS sports channel 真接** (read-only, paper 不下单不影响 book) | 同 paper, 共用 `WssClient::sports` | 老李 + 小冯 |
| C-2 | Order submit | mock `SignResponse` (小蒋 PaperSigner 走 VirtualMatcher) | `POST /clob/orders` + HMAC + EIP-712 Order | 老李 + 老孙 |
| C-3 | Order status (booked → matched → filled) | mock 走 VirtualMatcher (60s maker timeout fallback taker, 小袁 microstructure C++) | WSS `/ws/user` 推 `order` + `trade` event | 老李 |
| C-4 | Position (用户持仓) | mock 自维护 (paper_position.wal, 老王 WAL framework) | `GET /data/positions?user=<funder>` + WSS user channel bust | 老李 |
| C-5 | Settlement (比赛结果) | mock 走 Goalserve `game_state.ended` (小段) 触发 paper ledger 结算 | PM 自动 settle event + WSS data, **CTF `redeemPositions` 链上** | 老李 + 小段 |
| C-6 | Fee / Rebate | mock 0% (paper 简化, 但 metric 记录"虚拟 fee" 供后续 calibrate) | **3% taker / 0% maker** (PM 当前真值), feeRateBps 字段从 listing.markets 取 | 老李 + 小肖 |
| C-7 | Nonce | mock monotonic counter (paper_nonce.wal) | `POST /clob/orders` 拿 server-issued nonce; 我方维护 nonce_ledger | 老孙 |
| C-8 | Gas (Polygon) | mock 30 gwei fixed | Polygon RPC `eth_gasPrice` (alchemy / quicknode), 老叶 v1 设计 | 老叶 |
| C-9 | Confirm block | mock `~2s + jitter` 内存 timer 触发 | Polygon block `~2s` + RPC `eth_getTransactionReceipt` poll | 老叶 |
| C-10 | balance / allowance | mock infinite (paper 永远不爆) | `GET /balance-allowance?asset_type=COLLATERAL&signature_type=1` (v3 §A R3/R4) | 老李 |
| C-11 | market info (tick / negRisk / outcomes) | both real (从 gamma listing 拉, 静态 24h cache, paper / live 同源) | 同 paper (静态字段, 不区分 mode) | 老李 + 小田 #08 |
| C-12 | api-key derive (L1 EIP-712) | paper 不需要 (paper 不调私有 REST) | `GET /auth/derive-api-key` idempotent (同 EOA 同结果, v2 §0.4) | 老孙 |

### 1.2 接口物理隔离 (CMake 编译期 + PR-7 红线)

与 paper engine v0.2 §2.4 signer 隔离同款. `include/stcpp/polymarket/` 放抽象 (`pm_client.h` / `pm_types.h` / `pm_endpoints.h` / `pm_errors.h`). `src/exchange/polymarket/live/` (HMAC + EIP-712 + clob WSS, 只链 live target), `src/exchange/polymarket/paper/` (mock client, 只链 paper target), `src/exchange/polymarket/common/` (sports WSS / orderbook L2 / gamma REST, paper + live 共享).

CI 隔离校验: `nm stcpp_signer_live | grep -E "pm_client_paper" && exit 1` / `nm stcpp_signer_paper | grep -E "hmac_l2|eip712_order" && exit 1`.

### 1.3 paper mode 哪些走真实流, 哪些走 mock — 决策表

| 行为 | paper 走真 | paper 走 mock | 理由 |
|---|---|---|---|
| 拉 gamma /events listing | ✓ | | 静态元数据, paper / live 共用 cache |
| 拉 clob /sampling-simplified-markets | ✓ | | 同上 |
| WSS `/ws/market` (sports channel 第 5 host) | ✓ | | 公开免鉴权, read-only, paper 不影响 book |
| WSS `/ws/user` (clob HMAC) | | ✓ | paper 不下单, 私有 channel 不需要 |
| `POST /clob/orders` | | ✓ (VirtualMatcher) | 副作用最大, paper 必须 mock |
| `DELETE /clob/orders/{id}` | | ✓ (VirtualMatcher cancel) | 同上 |
| `GET /data/positions` | | ✓ (PaperLedger 自维护) | paper 持仓是虚拟的 |
| `GET /balance-allowance` | | ✓ (永远返回 infinite) | paper 不爆余额 |
| Polygon RPC `eth_*` | | ✓ (内存 mock) | paper 不上链 (见 §7) |
| Goalserve game_state (settle 触发) | ✓ | | settle 用真实比赛结果, paper 简化也走真 |

---

## 2. WSS subscribe matrix (sports channel 第 5 host)

### 2.1 第 5 host 全表

| 维度 | 值 |
|---|---|
| URL | `wss://sports-api.polymarket.com/ws` (公开免鉴权) |
| 协议 | WebSocket (RFC 6455), JSON text frame |
| 心跳 | 应用层 10s 发 `PING` 字符串, 30s 无 pong 强重连 |
| 来源 | W3 R-33 流程红线触发的发现 (官方 portal + py-clob-client + ts-client + 实测 RTT + 同行 SSOT 四维扫描) |
| paper / live | **paper 也走真实流** (read-only, 不影响订单簿状态) |

### 2.2 sports channel sub topic 全表

| Topic | Payload schema | 频率 | 谁消费 (后端 owner) | 4 ts 位置 |
|---|---|---|---|---|
| `market:{condition_id}` | `{condition_id, timestamp, yes_bids[[p,s,t]...], yes_asks[[p,s,t]...], no_bids, no_asks, mid, spread, last_trade_price, tick_size, neg_risk}` | ~10 Hz 临场 / 1 Hz pregame | 老李 PM client → MarketDataBus → Signal | `timestamp` = data_source_ts (ms epoch, ms → ns) |
| `game:{event_id}` | `{event_id, timestamp, sport, period, score_home, score_away, game_state, time_remaining_s, last_update}` | ~1 Hz | 老李 PM client → LiveSection → Signal trigger | `last_update` = data_source_ts; `timestamp` = event_ts (比赛事件触发, R-20 UPSTREAM_PAYLOAD) |
| `outcomes:{condition_id}` | `{condition_id, timestamp, outcomes:[{name, price, last_price, volume_24h}], resolution_status: 'open'/'resolving'/'resolved'}` | event 触发 (settle 时) | 老李 PM client → Settlement → PaperLedger / LiveLedger | `timestamp` = data_source_ts |
| `book:{token_id}` | (与 clob `/ws/market` book event 同 schema) `{asset_id, market, timestamp, hash, bids[[p,s]...], asks[[p,s]...]}` | snapshot 订阅时 + 偶尔重传 | 老李 PM client (L2 book builder) | `timestamp` = data_source_ts (ms) |
| `price_change:{token_id}` | `{asset_id, market, timestamp, changes:[{price, side, size}]}` | 增量, book 后高频 | 老李 PM client (L2 delta apply) | `timestamp` = data_source_ts |
| `last_trade_price:{token_id}` | `{asset_id, market, timestamp, price, side, size}` | 成交触发 | 老李 PM client (last trade cache) | `timestamp` = data_source_ts (成交 server 时刻) |
| `tick_size_change:{token_id}` | `{asset_id, market, timestamp, old_tick, new_tick}` | 罕见 (市场参数变更) | 老李 PM client (静态字段 bust) | `timestamp` = data_source_ts |
| `system:status` | `{status: 'healthy'/'degraded'/'maintenance', timestamp, message}` | 偶发 | 老李 PM client → 健康监控 → 小郑 metric | `timestamp` = data_source_ts |

### 2.3 condition_id ↔ event_id ↔ token_id 关联

| ID | 类型 | 含义 | 关联 |
|---|---|---|---|
| `condition_id` | bytes32 hex `0x...` (66 char) | CTF condition (1 market = 1 condition_id) | 1 condition → 2 outcome → 2 token_id (Yes/No) |
| `event_id` | string slug (PM 内部) | 一场比赛 = 1 event | 1 event → N market (Moneyline / Spreads / Totals / Props ...) |
| `token_id` | uint256 string | ERC1155 outcome token | 1 token_id ↔ 1 outcome 的 Yes 或 No |
| `asset` (data-api) | = `token_id` | 同上 (字段别名) | data-api positions 用 `asset` |

**订阅策略 (推荐):**
- `market:{condition_id}` 一次订阅拿全 market (Yes/No 双 outcome book), 不需要分 token 订阅
- `game:{event_id}` 1 event 订一次, 不用按 market 重复订
- `book:{token_id}` 仅在需要单 token 高频时订 (我们决策路径不用, 走 market topic 就够)

### 2.4 4 ts 在 sports channel payload 的位置 (R-20 enforce)

`market:{cid}` 顶层 `timestamp` (ms) = data_source_ts; `yes_bids[i][2]` 内层 `t` = trade-level data_source_ts (该价位最后更新 ms).

`game:{eid}` 顶层 `timestamp` (ms) = data_source_ts (PM server emit); `last_update` (ms) = event_ts (比赛事件发生时刻, 如"进球时刻", R-20 §2.2 enum = UPSTREAM_PAYLOAD).

**后端 (老李 PM client) 必须做的:**
- 从 payload `timestamp` 解出 `data_source_ts` (ms × 1e6 → ns)
- 本地 `clock_realtime_now_ns()` 作为 `ingestion_ts`
- 上游业务 ts (如 game.last_update) 作为 `event_ts` (R-20 §2.2 enum = UPSTREAM_PAYLOAD)
- `as_of_ts` 由 signal / RM 决策时计算 (链路下游)
- 4 ts 进老王 WAL v0.2 64B header (老唐 audit v1.1 §2.x Δ1 已对齐)

**严禁 (R-20 + R-12 + CI grep enforce):**
- 用本地 `now()` 替代 PM payload 的 `timestamp` (= 默认 INFERRED_FROM_INGESTION, 月度 sweep 必查)
- 拿不到 `timestamp` 静默 fallback (必须显式标 `data_source_ts_source = INFERRED_FROM_INGESTION` 进 audit 备查)
- WSS event loop 同步 IO / 阻塞锁 > 100us (R-12 红线)

---

## 3. REST endpoint 反向需求 (PMClient 抽象给后端)

### 3.1 PMClient 接口契约 (老周架构入 `include/stcpp/polymarket/pm_client.h`)

| # | 后端方法 | 输入 | 输出 | paper 实现 | live 实现 | endpoint |
|---|---|---|---|---|---|---|
| F-01 | `PMClient::get_orderbook(condition_id)` | condition_id | OrderBookSnapshot (yes/no L2) | 从 sports WSS 真接 (paper 也 read-only) | 同 paper + clob `POST /books` 兜底 | WSS `market:{cid}` + REST `clob /books` |
| F-02 | `PMClient::submit_order(SignedOrder)` | SignedOrder (含 EIP-712 sig, sigType=1) | OrderAck `{order_id, status:BOOKED, timestamp}` | mock (返虚拟 order_id, 走 VirtualMatcher) | `POST /clob/orders` + HMAC L2 | (live) `POST /order` |
| F-03 | `PMClient::cancel_order(order_id)` | order_id | CancelAck `{order_id, status:CANCELED}` | mock | `DELETE /clob/orders/{id}` + HMAC | (live) `DELETE /order` |
| F-04 | `PMClient::cancel_all()` | (无) | `{canceled_count}` | mock (paper VirtualMatcher 撤所有挂单) | `POST /cancel-all` + HMAC | (live) `POST /cancel-all` |
| F-05 | `PMClient::get_market_info(condition_id)` | condition_id | MarketInfo `{outcomes, tick_size, neg_risk, fee_rate_bps, accepting_orders, game_start_time, clob_token_ids[]}` | both real (gamma listing 静态 24h cache) | 同 paper | `GET /gamma/markets/{cid}` 或 listing |
| F-06 | `PMClient::get_user_positions(funder_addr)` | wallet_addr | `Position[]` (含 size / curPrice / redeemable / mergeable) | mock (PaperLedger 内 BTreeMap) | `GET /data/positions?user=<F>` + L1 cache 15s | (live) `GET /data/positions` |
| F-07 | `PMClient::get_balance(asset_type, token_id?)` | asset_type ∈ {COLLATERAL, CONDITIONAL} | `{balance_usdc, allowance[]}` | mock infinite | `GET /balance-allowance?asset_type=X&signature_type=1` (sigType=1, v3 §A R4) + HMAC | (live) `GET /balance-allowance` |
| F-08 | `PMClient::get_order_status(order_id)` | order_id | `{status:BOOKED/FILLED/...}` | mock (VirtualMatcher 状态机) | `GET /data/order/{id}` + HMAC | (live) `GET /data/order/{id}` |
| F-09 | `PMClient::get_my_open_orders()` | (无, by HMAC apiKey) | `Order[]` (含 next_cursor 翻页) | mock (PaperLedger 内挂单表) | `GET /data/orders?next_cursor=` + HMAC | (live) `GET /data/orders` |
| F-10 | `PMClient::get_my_trades(limit?)` | optional limit | `Trade[]` | mock (VirtualMatcher 成交流) | `GET /data/trades?limit=N` + HMAC | (live) `GET /data/trades` |
| F-11 | `PMClient::derive_api_key()` | (L1 EIP-712 signature) | `{api_key, secret, passphrase}` (idempotent) | not used (paper 不签 L2) | `GET /auth/derive-api-key` + L1 EIP-712 | (live only) |
| F-12 | `PMClient::list_api_keys()` | (HMAC) | `string[] api_keys` | not used | `GET /auth/api-keys` + HMAC | (live only) |
| F-13 | `PMClient::get_prices_history(token_id, interval, fidelity, startTs?, endTs?)` | token_id + 时间区间 | `Point[]` (K 线) | both real (公开 endpoint, paper / live 同) | 同 paper | `GET /clob/prices-history` |
| F-14 | `PMClient::subscribe_sports_wss(condition_ids[])` | condition_id 列表 | callback (book / price_change / last_trade_price ...) | both real (WSS 第 5 host) | 同 paper | `wss://sports-api.polymarket.com/ws` |

### 3.2 错误模型 (PMError 抽象)

PMErrorKind 9 个: `NotAuthenticated` (HMAC 401, 走 v3 §B SOP 不准 5min 内说"key 失效") / `RateLimited` (429, 退避 + 自我限流) / `ServerError` (5xx, 老韩 STALE 判定) / `BadRequest` (400, signing 错) / `NotFound` (404) / `NetworkError` (断 / 超时, 老姜 STALE 5 档) / `Stale` (payload data_source_ts 落后超阈, R-20) / `InvariantViolation` (语义违反, 如 outcome_index=999) / `Unknown`.

`struct PMError { PMErrorKind kind; int http_status; std::string body; int64_t observed_ts_ns; }`. **老沈 enforce**: PMError.body 不含 HMAC sig / apiKey / passphrase, log 前 redact (`****`).

---

## 4. 4 时间戳 (R-20) 在 PM payload 中的完整位置表

### 4.1 字段映射 (按 endpoint 分)

| Endpoint / channel | event_ts 来源 | data_source_ts 来源 | ingestion_ts | as_of_ts |
|---|---|---|---|---|
| WSS sports `market:{cid}` | inner trade `t` (price level last update) 或 fallback = data_source_ts | payload `timestamp` (ms) × 1e6 → ns | local `clock_realtime_now_ns()` | RM / Signal 决策时刻 |
| WSS sports `game:{eid}` | payload `last_update` (比赛事件) × 1e6 → ns | payload `timestamp` × 1e6 → ns | local | 同上 |
| WSS sports `outcomes:{cid}` | INFERRED_FROM_DS_TS (settle 事件本身 = data_source_ts) | payload `timestamp` × 1e6 → ns | local | 同上 |
| WSS clob `/ws/market` book | (无) = INFERRED_FROM_DS_TS | payload `timestamp` × 1e6 → ns | local | 同上 |
| WSS clob `/ws/user` trade | payload `match_time` × 1e6 → ns (撮合时刻) | payload `timestamp` × 1e6 → ns | local | 同上 |
| WSS clob `/ws/user` order | (无) = INFERRED_FROM_DS_TS | payload `timestamp` × 1e6 → ns | local | 同上 |
| REST gamma `/events` | INFERRED_FROM_INGESTION (静态元数据无 event_ts) | response header `Date` 解出 (不可靠, fallback INFERRED_FROM_INGESTION) | local | 同上 |
| REST clob `/books` | (无) | payload 每个 orderbook 项 `timestamp` × 1e6 → ns | local | 同上 |
| REST data `/positions` | INFERRED_FROM_INGESTION | INFERRED_FROM_INGESTION (data-api 不带 server ts) | local | 同上 |
| REST data `/trades` | trade 内 `match_time` (ms) | trade 内 `timestamp` (ms) | local | 同上 |
| REST data `/activity` | activity 内 `timestamp` (ms) | 同 event_ts | local | 同上 |
| Polygon RPC (live) | (无) | block timestamp (秒级, × 1e9 → ns) | local | 同上 |

### 4.2 `data_source_ts_source` enum 在 PM 上的取值规则 (R-20 §2.2 + audit v1.1 §2.x Δ1)

| 场景 | enum 取值 | CI 月度 sweep 阈值 |
|---|---|---|
| payload 自带 `timestamp` 字段 | `UPSTREAM_PAYLOAD` | 期望 ≥ 95% (大多数 PM endpoint 自带) |
| HTTP response header `Date` 解出 | `UPSTREAM_HEADER` | ≤ 5% (gamma listing 走这条) |
| 从同 batch 内别的字段推断 | `INFERRED_FROM_DS_TS` | < 1% (outcomes settle 走这条) |
| 实在没办法用本地 ingestion_ts | `INFERRED_FROM_INGESTION` | < 1%, **任何 endpoint > 5% → 月度 sweep 报警** |

**老唐 audit v1.1 §6.2 (R-20 映射) 已 enforce 月度 sweep, 小冯接 PM WSS 反演.**

### 4.3 严禁本地 `now()` 替代 data_source_ts (R-20 红线 P0)

CI grep 反模式见 §5.2. 例外白名单必须显式注释 + 标 `INFERRED_FROM_INGESTION`; 单文件 > 3 处需 ADR.

---

## 5. HMAC 签名 4 bug 永久 enforcement (v3 §A 复述给后端 owner)

### 5.1 4 bug 摘要 (W1-W3 我吃下的 4 次坑, 给老周 / 老沈 / 老唐 顶针看)

| bug# | 错处 | 错误写法 (CI 拦) | 正确写法 | 半径 |
|---|---|---|---|---|
| 1 | HMAC base string `request_path` 含 querystring | `path + "?" + query` | path only (`/balance-allowance` 而非 `/balance-allowance?asset_type=...`) | 任何带 cursor / next_cursor / asset_type 的 L2 GET 全 401 |
| 2 | `/balance-allowance` 参数名 | `param_type=COLLATERAL` (CI grep 拦) | `asset_type=COLLATERAL` | endpoint 全 400 |
| 3 | `Order.signatureType` 字段 | `sigType=2` (Polymarket proxy) | `sigType=1` (Magic 1-of-1 Safe — 我们的形态) | 下单全签错; balance-allowance sigType=2 返回 0 → 风控假阳性"余额 0" |
| 4 | HMAC sig base64 padding | `urlsafe_b64encode(sig).rstrip(b"=")` (CI grep 拦) | `urlsafe_b64encode(sig).decode()` 保留 padding | sig 少 1 char `=`, server 401, 被误判为"key 失效" (浪费 1 个 W) |

### 5.2 CI grep 反模式 (老练 + 老沈 + 小米 联签)

已 enforce (v3 §B.5): `rstrip(b"=")` / `replace("=","")` / `param_type` / `sigType=2` / `signatureType=2` 任一命中 → CI 拒. 本文新增: `request_path + "?"` (querystring 混进 base string) / `data_source_ts = now()|clock_realtime|system_clock` 在 `src/exchange/polymarket/` 下命中 → CI 拒.

### 5.3 14 wire vector (v3 §D) 给后端 live 实现 byte-equal 单测

老孙 PaperSigner C++ 单测**必须** byte-equal pass v3 §D 14 vector (5 GET + 4 GET-with-query + 1 GET-path-param + 4 POST/DELETE). 任一不过 = backend live 实现 reject merge.

vector 配套脚本权威值: `docs/RESEARCH/data/laoli-hmac-vectors-v3.json` (v3 §D.3).

---

## 6. CLOB Order 7 状态机 + audit AET 扩展

### 6.1 PM order_status 7 enum (v3 §B 实测 + 官方 SDK 源码)

| # | order_status | 含义 | 上游触发 | 下游 AET (audit) | 终态? |
|---|---|---|---|---|---|
| 1 | `BOOKED` | 订单已上 book, 等待撮合 (maker 状态) | `POST /clob/orders` ack | `AET_ORDER_BOOKED` (**新, +30 提议**) | 否 |
| 2 | `PARTIALLY_FILLED` | 部分成交, 余量在 book | WSS `/ws/user` trade event (size < order_size) | `AET_ORDER_FILLED` (v1 已有, payload 加 `is_partial=true`) | 否 |
| 3 | `FILLED` | 全成交 | WSS trade event (size = order_size) | `AET_ORDER_FILLED` (v1 已有, `is_partial=false`) | 是 |
| 4 | `CANCELED` | 用户主动撤单 | `DELETE /clob/orders/{id}` ack 或 `POST /cancel-all` | `AET_ORDER_CANCELLED` (v1 已有) | 是 |
| 5 | `EXPIRED` | 订单到期 (expiration 字段触发) | WSS user channel 或定期 sweep | `AET_ORDER_EXPIRED` (**新, +31 提议**) | 是 |
| 6 | `REJECTED` | 订单不合法被拒 (余额不够 / tick 错 / 市场已 close) | `POST /clob/orders` 直接 4xx 或 ack 后立即 reject | `AET_ORDER_DECISION` (v1 已有, `decision=REJECTED` + reject_reason) | 是 |
| 7 | `SETTLED` | 比赛结束, CTF 已 settle (token 转 USDC) | PM 自动 settle event + WSS data-api `/activity?type=REDEEM` | `AET_ORDER_SETTLED` (**新, +32 提议**) | 是 |

### 6.2 状态机转移 (文字版)

`POST /clob/orders` → (sigType=1 + HMAC OK) → `BOOKED` (非终态) → match part → `PARTIALLY_FILLED` (非终态) → match full → `FILLED` (终态). `BOOKED` / `PARTIALLY_FILLED` 任一态可 → `CANCELED` / `EXPIRED` (终态). `FILLED` → (settle 后 ~4h) → `SETTLED` (终态). `POST /clob/orders` ack 失败 → `REJECTED` (终态, 走 `AET_ORDER_DECISION` reject_reason).

### 6.3 派老唐 AET 12 → 15 (audit v1.1 14 + 本文 +1)

audit v1 = 12 AET, v1.1 = 14 (+30 STRATEGY_DECAYED, +31 STRATEGY_UNLOCK). 本文反向需求**再扩 +1 → 15**: `+32 AET_ORDER_BOOKED`.

**为何不扩 3 个反而扩 1 个 (BOOKED only) — 与 audit v1 OQ-2 "减 enum 膨胀" 思路一致:**
- `EXPIRED` → 复用 `AET_ORDER_CANCELLED` + reason="EXPIRED"
- `SETTLED` → 复用 `AET_RECON_DRIFT` + drift_type="SETTLEMENT" (老彭对账)
- `PARTIALLY_FILLED` → 复用 `AET_ORDER_FILLED` + `is_partial=true` flag
- `REJECTED` → 复用 `AET_ORDER_DECISION` + reject_reason
- `BOOKED` → **没有现成 AET 表达**, paper VirtualMatcher / live `POST /clob/orders` ack 都需要, 必须扩 +32

**派老唐 audit v1.2 schema:**
```protobuf
AET_ORDER_BOOKED = 32;

message OrderBookedPayload {
  string order_id = 1;             // PM server 返的 order_id (UUID)
  bytes  parent_decision_audit_id = 2;  // 指回 AET_ORDER_DECISION
  string condition_id = 3;
  string token_id = 4;
  string side = 5;                 // "BUY" / "SELL"
  double limit_price = 6;
  double size_usdc = 7;
  int64  expiration_unix_s = 8;    // 0 = GTC
  int32  signature_type = 9;       // = 1 (sigType=1, v3 §A R4)
  string tx_hash = 10;             // optional (clob 不上链时空)
  int64  booked_at_ns = 11;        // PM ack 时刻 = data_source_ts
}
```

### 6.4 已知坑 (协议 enforce 给后端)

| 坑 | 解 |
|---|---|
| `outcomeIndex` = 999 = 全市场 REDEEM, 不是 outcome 索引 (v2 §3.4) | data-api `/activity?type=REDEEM` parse 时, `outcomeIndex == 999` → 全 outcome redeem, 不要按 index 取 outcomes[i] |
| `/data/orders` 空响应 next_cursor = `LTE=` (= base64("-1") = END_CURSOR), 不是 base64("0") | client 翻页判停按 `LTE=`, 不按空数组 (v2 §2.5 #6) |
| `/books` 500-batch 静态 token 静默跳过 (271/500 返, 229 silent drop) | 必须用 `/sampling-simplified-markets` 取活跃池, 不能随便塞 500 token (v2 §2.5 #8) |
| `/ws/user` auth 失败不立刻断 (静默无消息) | 启动期必须 REST `GET /auth/api-keys` 自检, 不能用"WSS 消息数"判健康 (v2 §4.3) |
| feeRateBps 字段在 gamma listing 内 markets[i] | 不要单独调 `/markets/{cid}` 拿, 用 listing.markets[i].feeRateBps |

---

## 7. paper mode 不上链, 但要 fake 哪些 Polygon RPC

### 7.1 mock Polygon RPC 表 (老叶 v1 chain 抽象层 + 小蒋 paper 接)

| RPC | live 真实行为 | paper mock 行为 | owner |
|---|---|---|---|
| `eth_gasPrice` | alchemy / quicknode RPC, 实时 gas 价 (~30-100 gwei) | mock 固定 30 gwei | 老叶 + 小蒋 |
| `eth_estimateGas` | RPC 估算 (~80000 for CTF transfer) | mock 80000 fixed | 老叶 + 小蒋 |
| `eth_chainId` | Polygon = 137 | mock 137 | 老叶 |
| `eth_blockNumber` | 实时 block | mock 内存 counter, 每 2s + jitter ±200ms +1 | 老叶 + 小蒋 |
| `eth_sendRawTransaction` | submit 上链, 返 tx_hash | mock 内存生成假 tx_hash (e.g. `keccak(audit_id)` 16 bytes), 不真打 RPC | 老叶 + 小蒋 |
| `eth_getTransactionReceipt` | 轮询直到 status: 1 | mock 内存定时器 ~2s 后返 status: 1 + 假 logs | 老叶 + 小蒋 |
| `eth_call` (read-only) | RPC 读链上状态 | mock 按 PaperLedger 内存返 | 老叶 + 小蒋 |
| CTF `redeemPositions` | RPC tx 调 0x4D97DCd97eC945f40cF65F87097ACe5EA0476045 (Polygon CTF) | mock 直接 PaperLedger.apply_redeem(condition_id), 不发 tx | 老叶 + 小蒋 |

### 7.2 paper engine 调用链 (与 live 同接口)

```
PaperSigner (paper) / RealSigner (live)
       |
       v
ChainProvider 抽象接口 (老叶 v1 设计)
       |
       +----- LiveChainProvider (alchemy / quicknode RPC + 真 EIP-712 + eth_sendRawTransaction)
       |
       +----- PaperChainProvider (mock RPC, 内存生成 tx_hash + 模拟 confirm event ~2s)
       |
       +----- BacktestChainProvider (无 RPC, settle 事件由历史数据驱动)
```

**为什么 paper 也要走 ChainProvider 抽象**: 让 paper / live / backtest 三 mode 走同一份链路代码 (PR-1 红线, paper engine v0.2 §1.2), 唯一差异在 provider 注入. 不允许 paper engine 直接 hardcode "30 gwei" — 必须从 PaperChainProvider 拿, 即便后者是 mock.

### 7.3 paper mode 不要的链上事

不真打 RPC (省 quota + 跨洋), 不真支付 gas (paper 不爆余额), 不真等 confirm (mock ~2s+jitter 但保留 confirm 事件 emit 让 RM/Audit 链一致), 不真触发 CTF redeem (PaperLedger 直接结算).

---

## 8. WSS reconnect + heartbeat (R-12 enforce)

### 8.1 reconnect 策略 (给小冯 PM WSS subscriber W5-T2)

| 项 | 值 | 理由 |
|---|---|---|
| backoff | exponential, 1s / 2s / 4s / 8s / 16s / cap 30s | v2 §4.5 "重连退避 ≤ 3 次/分" |
| 最大尝试 | 无上限 (但 metric emit `wss_reconnect_attempts_total`) | 长期失败由 RM STALE 5 档接管, WSS 客户端永不放弃 |
| 重连握手期 | 实测 1.3-1.9s (v2 §4.5) | budget 给老姜 latency, 不入 hot path SLA |
| 重连后状态恢复 | 按 `timestamp` + book `hash` 比对 (无 sequence num) | v2 §4.5 |
| 重连期 STALE 标记 | last_msg_ts > 5s 进 STALE-LIGHT (老韩 INPLAY_HOT) | 见 §8.3 |

### 8.2 heartbeat (应用层)

| 项 | 值 |
|---|---|
| send PING | 每 10s 应用层发 `PING` 字符串 |
| pong timeout | 30s 无 pong → 强重连 (即关 socket + reconnect 状态机) |
| heartbeat 不进 audit | 仅 metric, 高频低价值 |
| WSS event loop 非阻塞 | R-12 红线 P0: 同步 IO / 阻塞锁 > 100us → CI 拦 |

### 8.3 STALE 5 档 (老韩 RM 接 WSS heartbeat 时序)

| 档位 | last_msg_ts 超阈值 | RM 动作 | 适用场景 |
|---|---|---|---|
| INPLAY_HOT (临场比赛中) | 500ms warning / 2s halt | 触发 RM 撤单 + 不开仓 | game.period > 1 + market.volume_1min > $1K |
| INPLAY_COLD (临场冷市场) | 2s warning / 10s halt | 同上但容忍度高 | game.period > 1 + 冷市场 |
| PREGAME (赛前 < 30min) | 5s warning / 30s halt | 撤单 + 暂停下新单 | game.start_time - now < 30min |
| FAR_PREGAME (赛前 > 30min) | 30s warning / 5min halt | 仅 metric, 不影响交易 | game.start_time - now > 30min |
| SETTLED (赛后) | 1h warning / 24h halt | 等 CTF redeem 完成即可 | game.state == ended |

**老韩 RM v0.3 §16 已 enforce.** 本文给后端的 input: WSS client 必须暴露 `last_msg_ts_ns(condition_id)` getter, RM 周期检测.

### 8.4 back-pressure (SPSC ring 满处理)

WSS in_ring 满 → **不阻塞 WSS event loop** (R-12 红线), drop oldest frame + emit `wss_dropped_frames_total{cid}`. drop > 5%/min → metric 告警 (小郑) RM 进 WARNING; drop > 50%/min → RM HALTED 撤所有单. 老周 v0.4 §17.2 SPSC 1 万 slot 满即 drop, 不做 disk spill.

---

## 9. 给后端 owner 的 5 个 hard ask

### 9.1 @小冯 (PM WSS subscriber, W5-T2)

**必须用第 5 host `wss://sports-api.polymarket.com/ws`**:
- 公开免鉴权, 不需要 HMAC L2
- paper / live 双 mode 都接真实流 (read-only, paper 不影响 book)
- 订阅协议见本文 §2.2
- **不要走 `wss://ws-subscriptions-clob.polymarket.com/ws/market`** (后者也公开但是 clob 旧路径, 第 5 host 是 W3 R-33 流程红线触发的新发现, 信息更新 + sports 专属 topic)

**clob `/ws/user` (HMAC + 私有)**:
- 仅 live mode 接, paper mode 不需要 (paper 通过 VirtualMatcher emit 虚拟 fill event)
- live 接入时, **必须先 REST `GET /auth/api-keys` 自检 apiKey 真在线**, 不能用"WSS 消息数"判健康 (v2 §4.3 已述坑)

**会签截止:** W4 EOW (2026-06-06)

### 9.2 @老周 (架构, cpp-chief-architect)

**PMClient::* 接口必须 forward declare 在 `include/stcpp/polymarket/`**:
- 抽象层定义见本文 §3.1 F-01 ~ F-14
- 错误模型见 §3.2 PMError
- paper / live 实现物理隔离 (CMake `STCPP_EXEC_MODE` 守门)
- CI 隔离校验 (本文 §1.2): `nm` grep paper / live symbol 不互越界

**v0.6 §17 (WSS 4-5 conn 拓扑) 接 sports-api 第 5 host:**
- T0a market_hot conn 改接 `wss://sports-api.polymarket.com/ws` (sports channel)
- T0b market_cold conn 同
- T0c user conn 仍接 `wss://ws-subscriptions-clob.polymarket.com/ws/user` (HMAC L2, live only)
- T1 polygon conn 仅 live (paper mock, 不真打 RPC)

**会签截止:** W4 EOW (2026-06-06)

### 9.3 @小蒋 (paper engine, quant-backtest)

**VirtualMatcher 当前用 fixed 80000 gas + ~2s confirm, 真应该从 mock ChainProvider 拿** (即便 paper mode):
- 见本文 §7.2, paper / live / backtest 三 mode 走同一份 ChainProvider 抽象 (老叶 v1)
- VirtualMatcher 不要 hardcode 数字, 调 `provider.get_gas_price()` / `provider.simulate_confirm(audit_id)`
- 让逻辑统一 (PR-1 红线 paper engine v0.2 §1.2)

**PaperLedger 接 PMClient::get_user_positions 抽象**:
- paper mode 内部 PaperLedger BTreeMap → 实现 PMClient::get_user_positions paper 版
- live mode 接 data-api `/data/positions` → 实现 live 版
- RM / Signal 上层不感知差异 (依赖注入)

**会签截止:** W5 起 (paper engine v0.2 联调期)

### 9.4 @老唐 (audit, audit-expert)

**PM order_status 7 enum 必须有对应 AuditEventType:**
- 当前 audit v1.1 = 14 AET (+30 STRATEGY_DECAYED, +31 STRATEGY_UNLOCK)
- 本文 §6.3 提议扩 **+32 AET_ORDER_BOOKED** → 总 15 AET (audit v1.2)
- `EXPIRED` / `SETTLED` / `REJECTED` / `PARTIALLY_FILLED` 用现有 AET + payload flag 表达 (减少 enum 膨胀, 与 v1 OQ-2 思路一致)
- payload schema 见本文 §6.3 `OrderBookedPayload`

**4 ts 进 audit (R-20 enforce):**
- audit v1.1 §2.x Δ1 已对齐 (4 ts 进老王 WAL v0.2 64B header)
- 本文 §4.1 给出 PM 各 endpoint 4 ts 来源对照表, 老唐 sweep 时按表查 `data_source_ts_source` enum 分布

**会签截止:** W4 EOW (2026-06-06)

### 9.5 @老沈 (security)

**HMAC key 永不入日志 + 永不入 git:**
- `.env` 已配齐 (POLY_API_KEY / POLY_API_SECRET / POLY_PASSPHRASE / WALLET_PRIVATE_KEY)
- signer cpp 必须用 SecureBuffer (老孙 v5 已实现 + paper engine v0.2 PR-9 共享)
- PMError.body 不能含 HMAC sig / apiKey / passphrase, log 前 redact (本文 §3.2)
- HMAC base string 不进 audit (含 path + body, 但 sig 本身不入)

**CI grep 反模式 enforce (本文 §5.2):**
```bash
rg "log.*POLY_API_(KEY|SECRET|PASSPHRASE)" --type cpp --type cc && exit 1
rg "log.*WALLET_PRIVATE_KEY" --type cpp --type cc && exit 1
rg "printf.*signature.*[\"']%s[\"']" --type cpp && exit 1  # sig 不进 printf log
```

**会签截止:** W4 EOW (2026-06-06)

---

## 10. 完成汇报 (一句话给老雷)

**PM 协议层后端需求 v1 + paper/live 契约矩阵 (12 PM-side 概念 × 2 mode) + WSS subscribe schema (sports-api 第 5 host 8 topic) + 4 ts 位置 (12 endpoint × 4 ts 来源对照) + HMAC 4 bug enforce (v3 §A R1-R6 + 14 wire vector) + 5 hard ask 后端 owner (小冯 WSS 第 5 host / 老周 PMClient 接口物理隔离 / 小蒋 VirtualMatcher 走 ChainProvider 抽象 / 老唐 AET 扩到 15 / 老沈 SecureBuffer + HMAC 不入日志). 反向需求 W4 EOW 会签, W5 联调可开始.**

---

## 附录 A — 4 bug 教训 + R-33 (永久知识沉淀, 复述自 v3 §A + 公司 R-33)

**4 bug verbatim:** 见本文 §5.1 表 (v3 §A 完整版). 根因不是知识是纪律, "凡协议 fact 必附 wire-level test vector" — v3 §D 14 vector 强制 byte-equal.

**R-33 流程红线:** 数据源文档四维扫描 (官方 portal / SDK 源码 / 实测 RTT / 同行 SSOT). v2 漏第 5 host = 维度 1+4 漏扫. 本文 §2 来自四维联扫.

---

## 附录 B — PM endpoint host 总览 (5 个)

| # | host | 用途 | 鉴权 | paper / live |
|---|---|---|---|---|
| 1 | `gamma-api.polymarket.com` | events / markets / series / tags / sports listing | 公开 | both real |
| 2 | `clob.polymarket.com` | 订单簿 + 私有 order 操作 + auth (derive-api-key / api-keys) | 私有走 HMAC L2 + L1 EIP-712 | paper 不调私有, live 全调 |
| 3 | `data-api.polymarket.com` | positions / value / trades / activity | 公开 (?user= 即可) | paper mock /positions, live 真调 |
| 4 | `polymarket.com` | privy 鉴权 (登录态 cookie) + BFF (`/api/tags/filtered` 等) | cookie | 我们不用 (我们走纯 API, 不走 web cookie) |
| 5 | **`sports-api.polymarket.com`** | **WSS sports channel (公开)** + REST sports-specific endpoint (待 W5-T2 实测) | 公开 | **both real** (W3 R-33 流程红线发现) |

**拓扑:** v3 §C vCPU0 4-5 conn 拓扑里 T0a/T0b market 改接第 5 host (sports channel), T0c user 仍接 host 2 clob `/ws/user` (HMAC L2 私有, live only), T1 polygon 仅 live (paper mock).

---

## 附录 C — 待 Sprint-2 W3 联跑后补

| # | 待补 | 截止 | owner |
|---|---|---|---|
| D-1 | sports-api 第 5 host REST endpoint 全表 (除 WSS 外是否有 GET /sports/live 等) | W5 EOW | 老李 + 小段 |
| D-2 | WSS sports channel `market:{cid}` payload 实测 schema (与本文 §2.4 推断对比, 字段名 / 顺序) | W5 EOW | 老李 |
| D-3 | sports channel rate limit (单连接 token 数上限, 与 clob /ws/market 实测 500 是否一致) | W5 EOW | 老李 + 老姜 |
| D-4 | sports channel reconnect 行为 (与 clob /ws/market 1.3-1.9s 握手是否一致) | W5 EOW | 小冯 + 老姜 |
| D-5 | PM order_status 7 enum 实测 transition (live mode 真下单跑一遍) | live D1 前 (M4.5 gate 通过后) | 老李 + 老韩 |

---

(完)
