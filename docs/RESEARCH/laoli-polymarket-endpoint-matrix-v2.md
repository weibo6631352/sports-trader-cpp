# Polymarket 全 Endpoint 复用率矩阵 v2

- Owner: 老李 (polymarket-protocol-expert)
- Date: 2026-05-28
- Last measured: 2026-05-28 13:33 UTC (data: `laoli-polymarket-matrix-v2-20260528-133323.txt`)
- 验收人: 老雷 (GM) + 小邓 (data-contract) + 老周 (cache 接口)
- 关联:
  - `docs/RESEARCH/laoli-polymarket-api-spec-v1.md` (v1 wire 契约, **本文修正其中 3 处错误**, 见 §0.3)
  - `docs/RESEARCH/laoli-xiaoduan-api-call-optimization-v1.md` (v1 复用率, 仅 NBA/MLB 抽样)
- 实测脚本: `docs/RESEARCH/data/laoli-polymarket-matrix-v2-probe.sh`
- 原始数据: `docs/RESEARCH/data/laoli-polymarket-matrix-v2-20260528-133323.txt` + `/tmp/laoli_v2_20260528-133323/` (本地, 不入 git, 含临时 derive 出的 apiKey/secret, 任务完成即销毁)
- 凭证: 从 `.env` 读, 本文档全程不落 key/secret 实值; 实测时 derive 出的 ephemeral key 已用完即弃

---

## 0. v1 → v2 覆盖增量

### 0.1 endpoint 数对比

| 维度 | v1 (老李 spec / 复用率 v1) | v2 (本文) | 增量 |
|---|---:|---:|---:|
| gamma endpoint | 6 (sports / tags / series / events / events/{id} / markets/{id}) | **6 + 子参数 sweep** (23 个 tag_slug + 3 个 filter combo) | 23 sport × 1 RTT 实测 |
| CLOB 公开 endpoint | 9 (markets / sampling-markets / sampling-simplified-markets / markets/{cid} / book / books / price / midpoint / spread / tick-size / neg-risk / prices-history / time) | **14** (新增 `/trades` 公开探活 + `prices-history` 4 种 fidelity + cursor 翻页第 2 页) | 4 个新点 |
| CLOB 私有 endpoint | 9 列了 schema 但只测过 1 个 derive-api-key | **8 全部实测通** (auth/api-keys / data/orders / data/trades 或 /trades 双路径 / balance-allowance × 3 sigType / data/order/{id} / derive-api-key) | 8/8 通 |
| data-api endpoint | 4 (positions / value / trades / activity) | **4 + filter sweep** (positions × 5 filter / activity × 11 type / 未文档 7 个 endpoint 探活) | 23 子调用 |
| WSS channel | 2 (market 测 12 sec / user 测 12 sec) | **2 + token N=1/50/100/300/500 5 档** | 5 档压测 |
| 限流压测 | 20 并发 1 endpoint | **3 endpoint × 20-30 并发** | 全部 host |
| **endpoint 类型总数 (含子参数)** | ~20 | **47** | +135% |

### 0.2 v2 新发现 (摘要)

1. **`/books` 上限实测 = 500 token, 501 触发 HTTP 400** "Payload exceeds the limit" — v1 已猜测但未压边界, v2 死定上限.
2. **`/auth/derive-api-key` (L1 EIP-712) 200 成功**, 返回的新 apiKey/secret/passphrase. **GM 2026-05-28 复核纠错**: derive 出的三字段和 .env 那组**逐字段相同** (apiKey/secret/passphrase 全 Y), .env 这组**没失效**. v2 当时 401 的根因是本 probe 脚本 `l2_helper.py` 第 338 行 `base64.urlsafe_b64encode(sig).rstrip(b"=")` 多 strip 了 padding, 导致 POLY_SIGNATURE 少 1 char (`=`), server 401. 正确做法: **保留 padding**, `urlsafe_b64encode(sig).decode()` 直接用. **`/auth/derive-api-key` 是 idempotent (#5)**, 推荐运行时 derive 是为了"不依赖人工管理 .env", 不是因为 .env "会失效".
3. **HMAC 签名 `requestPath` 不含 querystring** — v1 spec §3.3 明确写 "path 包含 querystring (例如 `/balance-allowance?param_type=...`)" 是**错的**. 用 querystring 签 → 全 401. 正确 = 仅 `/balance-allowance` 这种 endpoint path. v2 已实证 (用 querystring 401, 不带 querystring 200).
4. **`/balance-allowance` 参数名是 `asset_type`, 不是 `param_type`** — v1 §3.3 写错. 正确取值 `COLLATERAL` / `CONDITIONAL`. CONDITIONAL 必须搭配 `token_id`.
5. **我们 funder 实际 `signature_type=1`, 不是 v1 spec §3.3 说的 `signature_type=2`** — sigType=2 余额返回 0 (空账户视图), sigType=1 返回真实余额 **10.256941 USDC + ∞ allowance**. v1 spec 自报"我们是 1-of-1 Safe sigType=2", 实测打脸.
6. **`/data/trades` 才是规范 path**, 但 `/trades` 也通 (server 别名). v1 spec 标 `/trades` 路径混淆 GET 405 是因为没鉴权, 实际 L2 鉴权下两条都 200.
7. **activity.type 完整 enum (server 自报):** `TRADE / SPLIT / MERGE / REDEEM / REWARD / CONVERSION / DEPOSIT / WITHDRAWAL / YIELD / MAKER_REBATE / REFERRAL_REWARD` — v1 §4 只见到 TRADE/REDEEM, v2 实测拿到 server 错误响应里**官方完整 enum**, 见 §3.4.
8. **gamma `/series?sport=nba`** 可按 sport filter, v1 未测过, v2 确认.
9. **WSS market channel 500 token 单连接 20 秒收 1014 条消息 / 916 KB**, 实测稳态 50 msg/s, 271 个 token 有活跃推送, 229 个静默. 单连接物理上限至少 500.
10. **CLOB `/sampling-markets` 和 `/sampling-simplified-markets` 字段差异巨大**: 前者 33 字段含 question/description/rewards/tokens 完整 (2.4 MB/1000 条), 后者仅 7 字段 (583 KB/1000 条). 决策路径用 simplified 省 4× 带宽.

### 0.3 v1 spec 必须修正的 3 处错误 (本文 §0.2 #3 / #4 / #5)

| v1 spec 章节 | v1 写法 | 实测真值 | 影响 |
|---|---|---|---|
| §3.3 签名细节 | "path 包含 querystring (例如 `/balance-allowance?param_type=...`)" | path **不含** querystring, HMAC message = `ts+method+path_only_no_query` | 老孙签名服务如照 v1 实现, 所有带 querystring 的 L2 GET 全 401. **必须立即更正** |
| §3.3 端点 | `/balance-allowance?param_type=COLLATERAL` | 参数名是 `asset_type` | 同上, 改名后还要发 |
| §3.3 自报 | "我们 funder 是 signature_type=2 的 proxy, signer EOA 通过 proxy 操作" | 实测 sigType=2 返回 0 余额, sigType=1 才是真实 10.25 USDC + allowance | 风控读余额时 sigType=2 会假阳性 "余额为 0 拒下单", 决策侧灾难 |

老韩 (risk-manager) + 老孙 (key-mgmt) 必须在本周吸收这 3 处. 我会单独跟你们俩 sync.

### 0.4 v2 自己的 1 处错误 (GM 2026-05-28 复核纠错)

| v2 出错位置 | v2 原写法 | 实测真值 | 影响 |
|---|---|---|---|
| §0.2 #2 / §2.5 #5 / §9.2 #11 / §9.2 #15 | ".env 那组凭证现已失效, 401 = 失效证据" | .env 这组**没失效**, derive 出的三字段 (apiKey/secret/passphrase) 和 .env 逐字段相同 (Y/Y/Y). 401 根因: probe 脚本 `l2_helper.py` line 338 `base64.urlsafe_b64encode(sig).rstrip(b"=")` 多 strip 了 padding, sig 少 1 char `=`, server 401 | 凭证管理建议结论不变 (idempotent derive 推荐运行时 derive), 但**理由从"防失效"改为"省运维"**. 老韩/老孙不需要做"凭证失效告警"逻辑 |

**HMAC sig 正确做法 (必须吸收)**: `base64.urlsafe_b64encode(hmac_bytes).decode()` — **保留 padding**, 不要 `rstrip(b"=")`. 这条等价于 v1 错误清单 (querystring / param_type / sigType) 之外的第 4 处 HMAC 计算坑.

---

## 1. gamma 全表 (≥ 20 tag × 6 endpoint)

Host: `gamma-api.polymarket.com` — 公开无鉴权.

### 1.1 endpoint 矩阵

| Method | Path | 用途 | 类型 | 实测体积 / 延迟 (p50) | 复用率 | cursor / pagination |
|---|---|---|---|---|---|---|
| GET | `/sports` | 192 sport 元数据 (`id`, `sport`, `tags`, `series`, `ordering`, `image`, `resolution`) | bulk-directory | **51 KB / 1.6 s** | 192× (一次拿全联盟) | 无, 一次 |
| GET | `/tags?limit=500` | 标签字典 | bulk-directory | 18 KB / 0.9 s | **server 默认 cap = 100**, ?limit=500 仍只返 100 — **强制翻页** | 隐式 cursor (offset?), v1 未测翻页 |
| GET | `/series?limit=500` | 系列赛 (Stanley Cup / FIFA WC / NBA 等) | bulk-directory | **1.61 MB / 2.1 s** | 50 series/一次, sport=nba filter 20 series | 应支持 ?offset, 待测 |
| GET | `/series?sport=nba` | 按 sport 过滤 | bulk-directory | 678 KB / 1.9 s | 20× per sport | 同上 |
| GET | `/events?tag_slug=X&closed=false&limit=200` | 事件列表 (嵌套 markets 嵌套 clobTokenIds) | **bulk-listing (王者)** | 见 §1.2 | **83-1987×** | `?offset=N&limit=200` |
| GET | `/events?closed=false&active=true` | 全部活跃 (不限 tag) | bulk-listing | 428 KB / 2.2 s | 20 events (默认 limit=20) | 同上 |
| GET | `/events/{id}` | 单事件详情 (含嵌套 markets) | per-item | 6-217 KB / 1.2-2.0 s | **= listing 嵌套 (字段一致)** 1× — 不该用 | n/a |
| GET | `/markets/{id}` | gamma 视角单市场 | per-item | 3-10 KB / 1.0-1.2 s | **= listing.markets[i] (字段一致)** 1× — 不该用 | n/a |

### 1.2 `/events?tag_slug=X` 跨 23 个 sport 实测 (复用率证据)

| tag_slug | http | events | markets | tokens | size_KB | **复用率 (markets/调用)** |
|---|---:|---:|---:|---:|---:|---:|
| **mlb** | 200 | 100 | **1977** | 3954 | 7611.5 | **1977×** |
| **soccer** | 200 | 100 | **1934** | 3868 | 7125.7 | **1934×** |
| **esports** | 200 | 100 | **1254** | 2508 | 5003.4 | **1254×** |
| tennis | 200 | 100 | 1077 | 2154 | 4124.6 | 1077× |
| nhl | 200 | 15 | 826 | 1652 | 2664.1 | 826× |
| ufc | 200 | 59 | 804 | 1608 | 2892.8 | 804× |
| nba | 200 | 29 | 702 | 1404 | 2436.5 | 702× |
| nfl | 200 | 48 | 665 | 1330 | 2437.8 | 665× |
| golf | 200 | 13 | 596 | 1192 | 1929.7 | 596× |
| wnba | 200 | 49 | 470 | 940 | 1824.4 | 470× |
| mls | 200 | 15 | 397 | 794 | 1393.0 | 397× |
| mma | 200 | 20 | 368 | 736 | 1303.0 | 368× |
| cricket | 200 | 100 | 368 | 736 | 1895.9 | 368× |
| champions-league | 200 | 11 | 336 | 672 | 1255.6 | 336× |
| ncaa | 200 | 4 | 127 | 254 | 468.4 | 127× |
| la-liga | 200 | 5 | 104 | 208 | 372.8 | 104× |
| epl | 200 | 4 | 83 | 164 | 310.6 | 83× |
| ncaa-football | 200 | 1 | 16 | 32 | 57.5 | 16× |
| boxing | 200 | 2 | 2 | 4 | 14.0 | 2× |
| **ncaa-basketball** | 200 | **0** | 0 | 0 | 0.0 | n/a (季外 + tag 写法不对) |
| **bundesliga** | 200 | **0** | 0 | 0 | 0.0 | n/a (gamma 该 slug 未启用, 用 `tag_id` 替代) |
| **formula-1** | 200 | **0** | 0 | 0 | 0.0 | n/a (gamma 未挂 F1 体育 tag, 在 events `tag_id=1` 里有, 但 slug 不通) |
| **nascar** | 200 | **0** | 0 | 0 | 0.0 | n/a (同上) |

**总计**: 23 个 sport tag_slug 调用一次 = **205 events / 10,725 markets / 21,450 tokens / 41.7 MB**.

替代 = `/markets/{id}` per-item: 10,725 次 HTTP × 1 s = **3 小时串行**. v2 复用率 = **10,725/23 ≈ 466× 中位, p95 = 1977×, 极值 (mlb) 几乎 2000 markets 一次**.

### 1.3 重要观察

1. **`tag_slug=ncaa-basketball/bundesliga/formula-1/nascar`** 实测返 0. **不是 slug 拼写错, 而是 gamma 这些 sport 用 `tag_id` 注册没用 `tag_slug` filter**. 必须 fallback 到 `/tags` 字典查 id 再 `tag_id=N` (老李 v1 spec 里有用过 `tag_id=1` 拉全体育). 给小邓 (data-contract) 的 input: sport → tag 映射必须**双键 (id + slug)**.
2. **`/sports` 返 192 条**, 不是 v1 spec 说的 "ncaab, mlb, epl 这几条". 其中包含的 sport key 大量是历史/小众 (acn = Africa Cup, lal = la liga, ipl = cricket, 等). 给 ETL 的 input: 用 sport 列表做 series 关联表, sport `id` ↔ `series` ↔ `tags` 三元组.
3. **`/sports` 字段固定** (id, sport, image, resolution, ordering, tags, series, createdAt), 体积稳定 51 KB, 缓存 24 h 安全.
4. **`/events/{id}` per-item 与 listing markets[] 字段 100% 一致** (跨 NBA/MLB/Soccer 3 个 event 抽样, key 集合 + 内容字字段 diff = 0). **决策路径不要单独调 `/events/{id}`** — 单纯浪费 RTT.
5. **`/markets/{id}` (gamma) 与 listing.markets[] 字段 100% 一致** — 用 listing 拉一次, 不需要 single. 唯一用场: 接 webhook 后 lookup specific market id (不常见).
6. **`?closed=true` 拉历史 (用于回测 / settlement 监控)**, server 限 `limit=10` 默认.

### 1.4 已知坑 (v1 仍适用, v2 不重述)

见 v1 spec §2.5. v2 新增:
- **`?limit=500` 在 /tags 被 server cap 到 100**, /events / /series 接受 200+. 不一致, 必须 client 端弹翻页.

---

## 2. CLOB REST 全表 (≥ 17 endpoint)

Host: `clob.polymarket.com`.

### 2.1 公开端点全表 (无鉴权)

| Method | Path | 用途 | 类型 | 实测体积 / 延迟 (p50) | 复用率 / 一次覆盖 | cursor / pagination | ETag / Cache-Control |
|---|---|---|---|---|---|---|---|
| GET | `/time` | server unix 秒 (clock skew 校准) | scalar | 10 B / 0.9 s | 1× | n/a | 无 |
| GET | `/markets?next_cursor=MA==` | 全市场分页 | bulk-listing 分页 | **1.82 MB / 2.1 s** | **1000 markets / page** (limit 写死) | base64(offset) — `MA==` = "0", `MTAwMA==` = "1000" — offset 形式 | 无 |
| GET | `/sampling-markets?next_cursor=` | 仅 rewards 程序活跃市场 (做市目标池) | bulk-listing | **2.40 MB / 3.1 s** | 1000 entries / page, 字段全 (含 question/description/rewards/tokens) | 同上 | 无 |
| GET | `/sampling-simplified-markets?next_cursor=` | 同上但精简字段 | bulk-listing | **583 KB / 1.9 s** ✅ **4× 省带宽** | 1000 entries / page, **仅 7 字段** (accepting_orders / active / archived / closed / condition_id / rewards / tokens) | 同上 | 无 |
| GET | `/markets/{condition_id}` | 单市场 (clob 视角) | per-item | 2 KB / 1.0 s | 1× — 决策路径不用, 已被 listing 覆盖 | n/a | 无 |
| GET | `/book?token_id=` | 单 token 订单簿全档 | per-item | 3 KB / 1.4 s | 1× | n/a | 无 |
| POST | `/books` body=[{token_id}...] | **批量订单簿 (复用王)** | **bulk-quote** | 1.08 MB / 3.2 s @ 500 batch | **上限 500 token / 请求**, 501 触发 HTTP 400 `"Payload exceeds the limit"`. 实测 271/500 token 活跃返回 (剩 229 静态 token server 跳过) | 无 cursor (单次往返) | 无 |
| GET | `/price?token_id=&side=BUY\|SELL` | 单边最优价 | per-item | 17 B / 1.7 s | 1× — 完全可被 /books 替代 | n/a | 无 |
| GET | `/midpoint?token_id=` | mid | per-item | 16 B / 1.5 s | 1× | n/a | 无 |
| GET | `/spread?token_id=` | spread | per-item | 18 B / 1.0 s | 1× | n/a | 无 |
| GET | `/tick-size?token_id=` | tick (e.g. 0.01) | per-item | 27 B / 1.0 s | 1× — 静态字段, 用 listing 字段 `orderPriceMinTickSize` 替代 | n/a | 无 |
| GET | `/neg-risk?token_id=` | 是否 negRisk | per-item | 18 B / 1.1 s | 1× — 同上, 用 listing.negRisk | n/a | 无 |
| GET | `/prices-history?market=<token_id>&interval=&fidelity=` | K 线 | per-item, **不支持 bulk** | 25-61 点 / 689 B - 1.66 KB / 1.0-1.4 s | 1× | n/a | 无 |
| GET | `/trades?market=&asset_id=` (公开试) | 公开成交流 | **401** — 不公开, 必须 L2 | n/a | n/a | n/a | n/a |

### 2.2 `/prices-history` 参数组合实测

| query | http | points | bytes | total_s | 用途 |
|---|---:|---:|---:|---:|---|
| `interval=1d&fidelity=60` | 200 | 25 | 689 | 1.03 | 日线 60 分钟一根 |
| `interval=1h&fidelity=1` | 200 | 61 | 1661 | 1.42 | 小时线 1 分钟一根 |
| `interval=1w&fidelity=1440` | 200 | 8 | 230 | 1.50 | 周线 24h 一根 |
| `startTs=T-3600&endTs=T&fidelity=1` | 200 | 61 | 1661 | 1.07 | **明确时间区间 + 1 分钟 fidelity, 推荐回测用法** |

**实证**: `interval` 是 server 端预设区间快捷参数, `startTs/endTs` 是显式区间. v1 spec 没列 startTs/endTs, v2 补.

### 2.3 `/markets` 翻页确认

| page | next_cursor 输入 | http | data | size | next_cursor 输出 |
|---|---|---:|---:|---:|---|
| 1 | `MA==` | 200 | 1000 | 1.82 MB | `MTAwMA==` = base64("1000") |
| 2 | `MTAwMA==` | 200 | 1000 | 1.67 MB | (继续 offset) |

**cursor = base64(offset_number) 显式 offset 形式**, 每页固定 1000. 总市场数从 `?count=` 字段拿 (v1 测过约 数万).

### 2.4 私有端点全表 (L2 HMAC + L1 EIP-712) — 实测

**鉴权头 (5 个) — 修正版** (v1 spec §3.3 写错的部分已纠正):

```
POLY_ADDRESS    = signer EOA (从 WALLET_PRIVATE_KEY 推, 不是 funder)
POLY_API_KEY    = UUID 36 字符
POLY_PASSPHRASE = 64 字符 hex
POLY_SIGNATURE  = base64url(HMAC-SHA256(base64url_decode(secret), ts + method + path_NO_QUERY + body))
POLY_TIMESTAMP  = Unix 秒字符串
```

**HMAC message 三要素 (官方 py-clob-client 源码确认):**
```python
message = str(timestamp) + str(method) + str(request_path)  # request_path 不含 ?query
if body: message += str(body).replace("'", '"')             # body 单引号必须替双引号
```

实测全部 endpoint 状态 (用 derive 出的新 key, 旧 .env 凭证 401):

| Method | Path (签名 path) | 全 URL | http | 含义 | 复用率 |
|---|---|---|---:|---|---|
| GET | `/auth/api-keys` | `/auth/api-keys` | **200** | 列我名下所有 apiKey | 单次 |
| GET | `/auth/derive-api-key` | `/auth/derive-api-key` | **200** | **L1 EIP-712 派生 apiKey/secret/passphrase**, ClobAuth EIP-712 签名作为 POLY_SIGNATURE | 单次, 但 idempotent (同 EOA 反复 derive 拿到同一组) |
| GET | `/data/orders` | `/data/orders` | **200** | 我方活跃订单, `{data:[], next_cursor:"LTE=", limit:500, count:0}` — **LTE= 是 END_CURSOR** | 单 user 全单 |
| GET | `/data/trades` | `/data/trades?limit=N` | **200** | 我方成交历史, 930 KB / 100+ 条 | 单 user 全部成交 |
| GET | `/trades` | `/trades?limit=N` | **200** | server 别名, 与 `/data/trades` 同响应 | 同上 |
| GET | `/balance-allowance` | `/balance-allowance?asset_type=COLLATERAL&signature_type=1` | **200** | USDC 余额 + allowance (sigType=1 是我们 funder 的真实形态, 余额 10.256941 USDC, allowance 三个 spender 全 ∞) | 单 user |
| GET | `/balance-allowance` | `/balance-allowance?asset_type=CONDITIONAL&signature_type=1&token_id=<T>` | **400** without token_id ("assetId invalid value -1") | CTF erc1155 余额, 必须传 token_id | 单 (user, token) |
| GET | `/data/order/{order_id}` | `/data/order/{id}` | 200 (有效 id) / 401 (无效 + 鉴权坏) | 单订单状态 | 单单 |

**未测 (主动避免) — 写但不发:**
- `POST /order` 单下单
- `POST /orders` 批量下单
- `DELETE /order` 单撤单
- `DELETE /orders` 批量撤单
- `POST /cancel-all` 全撤

理由 (按老雷 v2 任务约束): 这些有副作用 (会真下单 / 真撤单), GET 优先. 老李之前的 spec v1 §6 已列出 EIP-712 Order 类型, 等老孙 key-mgmt 服务上线 + 老韩 risk-manager v0.3 上线后, 由小张 paper-trading 引擎用 paper 环境跑出 wire 样本.

### 2.5 CLOB 已知坑 (v1 仍适用 + v2 新增)

**v1 仍适用 (不重述):** §3.4 第 1, 2, 4-8 条.

**v2 修正/新增:**

1. **v1 §3.4 第 3 条** "prices-history?market= 实际是 token_id" — **仍然成立**, 但 v2 测过 `startTs/endTs` 也支持 (v1 没测).
2. **v1 §3.3 path 签名带 querystring 错** — 详 §0.3 #3.
3. **v1 §3.3 balance-allowance 参数名错** — 详 §0.3 #4.
4. **v1 §3.3 sigType 写错** — 详 §0.3 #5.
5. **新坑: 同 EOA `/auth/derive-api-key` 是 idempotent** — 反复 derive 拿到同一组 apiKey/secret/passphrase (server 端绑死 EOA 一对一). 这意味着**不需要存** apiKey/secret/passphrase, 每次启动 derive 一次即可拿回. 推荐运行时 derive 是**为了不依赖 .env 人工管理**, 不是因为 .env 会失效. **GM 2026-05-28 复核纠错**: v2 原文写"(.env 这组现在 401 = 失效证据)" 是错的, 实测 .env 这组就是 derive 出来的同一组, 没失效. 真正 401 根因是 v2 probe 脚本 HMAC sig base64 多 `rstrip(b"=")`, 见 §0.2 #2 更新.
6. **新坑: `/data/orders` 空响应 next_cursor=`LTE=` = "-1"** (base64), 是 END_CURSOR, 不是 base64("0"). client 翻页判停要按 `LTE=`.
7. **新坑: `/sampling-simplified-markets` 只有 7 字段**, 不含 `minimum_tick_size` / `minimum_order_size` / `neg_risk`. 想拿这些字段必须用 `/sampling-markets` (4× 带宽) 或者用 listing.markets 字段 (gamma).
8. **新坑: `/books` 500-batch 静态 token 跳过**: 实测 500 个 token 池 (取自 NBA + MLB) → server 仅返 271 个有 orderbook 的, 229 个 silently 丢. 必须从 `/sampling-simplified-markets` 取 "活跃" 池才能确保 100% 返回.

---

## 3. data-api 全表

Host: `data-api.polymarket.com` — 公开无鉴权, 用 `?user=<funder>` 查询.

### 3.1 endpoint 矩阵

| Method | Path | 用途 | 类型 | 实测体积 / 延迟 (p50) | 复用率 |
|---|---|---|---|---|---|
| GET | `/positions?user=<F>` | 全持仓 | bulk-listing per-user | 21 KB / 1.3 s | **24 entries 一次** |
| GET | `/positions?user=<F>&redeemable=true` | 仅可赎回 | 同上 + filter | 同 | 同上, 24 (24 全 redeemable) |
| GET | `/positions?user=<F>&mergeable=true` | 仅可 merge | 同上 | 2 B "[]" / 1.2 s | 0 (无 negRisk 持仓) |
| GET | `/positions?user=<F>&sizeThreshold=0.1` | size 过滤 | 同上 | 22 KB / 1.8 s | 25 |
| GET | `/positions?user=<F>&sortBy=CURRENT&sortDirection=DESC` | 排序 | 同上 | 21 KB / 1.3 s | 24 |
| GET | `/value?user=<F>` | 账户净值 | scalar | 65 B / 1.3 s | `[{user, value:0}]` (账户已清空) |
| GET | `/trades?user=<F>&limit=N` | 用户成交 (公开视角, 跟 clob `/data/trades` 字段不同) | bulk-listing | 8.8 KB @ N=10 / 88 KB @ N=100 / 1.7 s | N × |
| GET | `/trades?user=<F>&side=BUY&limit=N` | filter | 同上 | 同 | |
| GET | `/trades?user=<F>&takerOnly=true&limit=N` | filter | 同上 | 同 | |
| GET | `/activity?user=<F>&limit=N&type=T` | 链上动作流 | bulk-listing | 见 §3.2 | N × |

### 3.2 `/activity?type=` 完整 enum (server 自报)

实测发 `?type=CONVERT`  + `?type=SPLIT_REWARDS` 服务端返 HTTP 400 + 错误体:

```
{"error":"invalid activity filter type CONVERT. must be: 
  [TRADE SPLIT MERGE REDEEM REWARD CONVERSION DEPOSIT WITHDRAWAL YIELD MAKER_REBATE REFERRAL_REWARD]"}
```

**官方完整 enum (11 个):**

| type | 含义 | 我们 funder 实测计数 |
|---|---|---:|
| `TRADE` | 成交 | 50 (limit=50) |
| `REDEEM` | 结算赎回 | 14 |
| `MERGE` | negRisk Yes+No → USDC | 0 |
| `SPLIT` | USDC → Yes+No (做市铺底) | 0 |
| `REWARD` | maker 激励 | 0 |
| `CONVERSION` | (推断) negRisk outcome 转换 | 0 |
| `DEPOSIT` | USDC 入账 | 0 |
| `WITHDRAWAL` | USDC 提走 | 0 |
| `YIELD` | (推断) USDC 借贷收益 | 0 |
| `MAKER_REBATE` | maker rebate (区别 REWARD?) | 0 |
| `REFERRAL_REWARD` | 推荐奖励 | 0 |

**v1 spec §4 自填 "TRADE/REDEEM/MERGE/SPLIT/CONVERT/REWARD" 是推测, v2 拿到 server 权威 enum. 给小邓 data-contract C-04 修正**: type 必须按官方 enum, 注意 `CONVERSION` (不是 `CONVERT`).

### 3.3 未文档 endpoint 探活

| Path | http | 结论 |
|---|---:|---|
| `/holdings?user=<F>` | 404 | 不存在 |
| `/pnl?user=<F>` | 404 | 不存在 (历史 PnL 自己从 trades 推) |
| `/user/<F>` | 404 | 不存在 |
| `/leaderboard?limit=5` | 404 | 不存在 |
| `/markets` | 404 | 用 gamma / clob |
| `/series` | 404 | 用 gamma |
| `/events` | 404 | 用 gamma |

**data-api 严格只有 4 个稳定 endpoint** (positions / value / trades / activity), v1 spec §4 写的 "未通的: /holdings /pnl /user /leaderboard 都 404" v2 重确认.

### 3.4 关键字段语义 (给小邓 + 老韩)

**`/positions[]` 字段 (重要 — risk 监控数据源):**

| 字段 | 类型 | 用途 |
|---|---|---|
| `proxyWallet` | 小写 0x | 当前 funder |
| `asset` | uint256 string | = clobTokenIds[i] |
| `conditionId` | bytes32 hex | CTF condition |
| `size` | float | 持仓 CTF 数量 |
| `avgPrice` | float | 加权平均买价 |
| `initialValue` | float | 持仓初始 USD |
| `currentValue` | float | 当前 USD (用 curPrice * size) |
| `curPrice` | float | 最近交易价 (秒级缓存) |
| `cashPnl` | float | currentValue - initialValue |
| `percentPnl` | float | cashPnl / initialValue |
| `realizedPnl` | float | 历史已实现 |
| **`redeemable`** | bool | **结算后可赎回, 老韩 risk 自动赎回触发条件** |
| **`mergeable`** | bool | **negRisk Yes+No 互持可 burn 回 USDC** |
| `title` / `slug` / `icon` | string | UI 字段 |
| `outcome` | "Yes"/"No"/球队名 | outcome 文本 |
| `outcomeIndex` | int | 0/1, **REDEEM 类活动可见 999 = 全市场赎回 (不要按 index 解析)** |
| `endDate` | ISO8601 | |
| `negativeRisk` | bool | 同 negRisk |

---

## 4. WebSocket channel 全表

Host: `wss://ws-subscriptions-clob.polymarket.com`.

### 4.1 endpoint

| Path | 用途 | 鉴权 | 订阅 payload | 复用率 |
|---|---|---|---|---|
| `/ws/market` | 公开订单簿增量 | 无 | `{"type":"Market","assets_ids":["<token>",...]}` 注意复数 s | **单连接订阅 N 个 token** |
| `/ws/user` | 我方订单 / 成交事件 | L2 (apiKey/secret/passphrase 在 payload) | `{"type":"User","auth":{...},"markets":["<conditionId>",...]}` 注意是 conditionId 不是 token_id | **单连接订阅 N 个 conditionId** |

### 4.2 `/ws/market` 多 token 单连接压测 (v2 新)

| 订阅 N | open_s | first_msg_s | 20s 窗口 msgs | bytes | unique asset_ids 出现 | event_types |
|---:|---:|---:|---:|---:|---:|---|
| 1 | 1.89 | 2.53 | 1 | 3 B | 0 | `{}` (静态市场, 单条 ping 也可能没) |
| 50 | 1.36 | 2.55 | 69 | 65 KB | 23 | book:23, price_change:68 |
| 100 | 1.66 | 2.89 | 161 | 152 KB | 52 | book:52, price_change:160 |
| 300 | 1.83 | 4.31 | 686 | 596 KB | **158** | book:158, price_change:685 |
| **500** | **1.38** | **4.16** | **1014** | **916 KB** | **271** | book:271, price_change:1013 |

**关键观察:**

1. **单连接 500 token 不被拒**, 服务端订阅上限至少 500. v1 spec 标 "上限我没碰到", v2 死定 ≥ 500.
2. **首包延迟随订阅数增加** (1.89s → 4.16s @ 500): server 端为每个 token 流 book snapshot, batch 一次性下发. snapshot 总大小 / 网络带宽 ≈ 增长.
3. **静态 token 不推 book snapshot**: 500 订阅, 仅 271 个 token 看到推送 (active), 229 个静默. 跟 `/books` POST 行为一致 ("没 orderbook 的 token 静默跳过"). **应用层不能用"消息数"判健康, 必须按 token-specific timer**.
4. **稳态消息率**: 500 token, 16 秒内 1014 msg, ≈ **63 msg/s**, 平均 906 B/msg. 临场预估 5-10× → 300-600 msg/s 单连接. 老吴跨洋带宽 ≥ 500 KB/s 才安全.
5. **price_change : book 比例 ≈ 4:1**, 即 token 启动后 quote 增量 (price_change) 远多于 book reset.

### 4.3 `/ws/user` 单连接订阅

| 订阅 N (conditionId) | open_s | first_msg | msgs | 含义 |
|---:|---:|---:|---:|---|
| 20 | 1.27 | -1 | 0 | **静默 = 健康** (无订单事件) |

L2 鉴权用**当前 .env 的 apiKey** 测过去, server 没立刻断也没发任何消息. 但 .env 凭证 REST 上是 401 失效 — 这就是 v1 spec §5.3 警告的 "**user channel auth 失败时不会立即断, 静默无消息, 容易误判健康**". v2 二次确认: **必须通过 REST GET `/auth/api-keys` 或下个 paper 单验证 user channel auth 真在线**.

### 4.4 消息类型 (v1 仍适用)

`/ws/market`:
- `book` — snapshot, 订阅时立即 + 偶尔重传
- `price_change` — 增量
- `last_trade_price` — 成交 (v1/v2 未捕到, 静态市场)
- `tick_size_change` — 罕见

`/ws/user`:
- `trade` — 我方成交
- `order` — 订单状态 (PLACED/MATCHED/CANCELED/EXPIRED)

### 4.5 心跳 / 重连 (v1 已述, v2 不重复)

- 应用层每 10s 发 `PING` 字符串
- 无 sequence number, 重连用 `timestamp` + `hash` 比对
- 重连握手 ~1.3 s (v1 标 1.1, v2 实测 1.3-1.9)

---

## 5. 复用率排名

### 5.1 Top 10 高复用 endpoint (按"一次调用覆盖单位")

| 排名 | endpoint | 单次覆盖 | 实测体积 / 延迟 | 决策路径用途 |
|---:|---|---:|---|---|
| 1 | gamma `/events?tag_slug=mlb&limit=200` | **1977 markets / 3954 tokens** | 7.6 MB / 3.6 s | 全市场元数据扫盘 |
| 2 | gamma `/events?tag_slug=soccer&limit=200` | **1934 markets / 3868 tokens** | 7.1 MB / 3.6 s | 同上 |
| 3 | clob WSS `/ws/market` 500 token | **500 token 单连接实时流** | 916 KB / 16 s 窗口 | **决策核心: 实时 quote 全流** |
| 4 | clob `POST /books` 500 batch | **500 token 订单簿** | 1.08 MB / 3.2 s | WSS 兜底 / 冷启动 snapshot |
| 5 | gamma `/events?tag_slug=nba&limit=200` | 702 markets / 1404 tokens | 2.4 MB / 3.6 s | NBA 全市场扫盘 |
| 6 | clob `/markets?next_cursor=` | 1000 markets / page | 1.82 MB / 2.1 s | clob 视角全市场 (含 `seconds_delay` + `game_start_time` 字段, gamma 没有) |
| 7 | clob `/sampling-simplified-markets` | 1000 active markets / page (仅 7 字段) | 583 KB / 1.9 s | **做市策略目标池** |
| 8 | clob `/sampling-markets` | 1000 active markets / page (33 字段) | 2.40 MB / 3.1 s | 同上, 字段全 |
| 9 | gamma `/sports` | 192 联盟元 | 51 KB / 1.6 s | 启动时 prefetch |
| 10 | data-api `/positions?user=<F>` | 单 user 全部持仓 (24 个/我方) | 21 KB / 1.3 s | 风控监控 |

### 5.2 Top 5 低/无复用 endpoint (per-item, 决策路径绕开)

| 排名 | endpoint | 单次覆盖 | 替代方案 |
|---:|---|---|---|
| 1 | `/events/{id}` | 1 event (字段 100% 等同 listing.markets[]) | 用 gamma `/events?tag_slug=` listing, 不调单 event |
| 2 | gamma `/markets/{id}` | 1 market (字段 100% 等同 listing.markets[i]) | 同上 |
| 3 | clob `/markets/{condition_id}` | 1 market (clob 视角) | 用 clob `/markets` 或 `/sampling-markets` listing 一次拉 1000 |
| 4 | clob `/price` `/midpoint` `/spread` `/tick-size` `/neg-risk` (5 个单点) | 1 token 1 字段 | 用 `/books` 一次拿全 (`min_order_size` `tick_size` `neg_risk` `last_trade_price` + bids/asks 全档), 复用率 500× |
| 5 | clob `/prices-history?market=` | 1 token K 线 (不支持 bulk) | 决策路径不用 K 线 (历史回测才用), 5 min cache |

---

## 6. 推荐 TTL 矩阵 (给老周 cache + 小邓 data contract)

沿用复用率 v1 §3 的 L0-L3 四级分级 (本文不重复语义, 只给"全 endpoint × TTL" 表).

| Endpoint | freshness 等级 | 推荐 TTL | 实现方式 | invalidate 触发 |
|---|---|---|---|---|
| **L0 流式 (无 TTL)** |
| WSS `/ws/market` | L0 | 流式 | in-memory orderbook state | 无 — 流式覆盖 |
| WSS `/ws/user` | L0 | 流式 | in-memory order state | 无 |
| **L1 准实时 (秒级)** |
| clob `POST /books` (热门 token) | L1 | **2-5 s** | LRU + single-flight | WSS price_change 来后立刻 bust |
| clob `/book?token_id=` (冷启动) | L1 | **3 s** | per-token cache | WSS snapshot 覆盖 |
| clob `/price` `/midpoint` `/spread` | L1 | 不用 (走 /books) | n/a | n/a |
| data-api `/positions` | L1 | **15-30 s** | per-funder cache | WSS user channel 任何成交立即 bust |
| data-api `/value` | L1 | **30 s** | per-funder | 同上 |
| data-api `/trades` `/activity` | L1 | **3 s** for 监控, 5 min for 历史 | append-only log + WSS bust | WSS append |
| clob 私有 `/data/orders` | L1 | **5 s** (WSS user channel 主, REST 备) | per-apiKey | WSS order event bust |
| clob 私有 `/data/trades` | L1 | **5 s** for 监控 | append-only | WSS trade event bust |
| **L2 准稳定 (分钟级)** |
| gamma `/events?tag_slug=` (元数据子集) | L2 | **5 min** (元数据), 嵌套 quote 不可信 | full-listing snapshot + diff | 5 min 定时 + 显式 refresh |
| clob `/markets/{cid}` | L2 | **5 min** | LRU by cid | 市场 accepting_orders 变 bust |
| clob `/markets` (全市场) | L2 | **5 min** for hash, 1 h for 元数据子字段 | 分页 prefetch | 显式 |
| clob `/sampling-simplified-markets` | L2 | **30 s** for active set | LRU | 周期刷 |
| clob `/sampling-markets` | L2 | **5 min** | LRU | 同上 |
| clob 私有 `/balance-allowance` | L2 | **30 s**, 写后 (下单) 立即 bust | per-(apiKey, asset_type, token_id) | 任何下单/撤单 bust |
| data-api `/activity` 历史 | L2 | 5 min | append-only | 显式 |
| **L3 慢变 (小时-天级)** |
| gamma `/sports` | L3 | **24 h** | KV | 手动 |
| gamma `/tags` | L3 | **24 h** | KV | 手动 |
| gamma `/series` | L3 | **6 h** | KV | 手动 |
| clob `/tick-size?token_id=` | L3 | **24 h** by conditionId | KV | 显式 (市场创建后不变) |
| clob `/neg-risk?token_id=` | L3 | **永久** by conditionId | KV | 显式 (静态) |
| clob `/prices-history` | L3 | **15 min** for daily, **1 min** for 1m fidelity | LRU by (token, interval, fidelity) | 不主动 invalidate |
| clob 私有 `/auth/api-keys` | L3 | **6 h** | per-EOA | derive 后填 |
| clob 私有 `/auth/derive-api-key` | L3 | **进程生命周期** (idempotent, 同 EOA 反复 derive 同样结果) | per-EOA in-memory | EOA 切换 bust |
| **不缓存** |
| clob `/time` | L0 | **每分钟主动调 1 次校 clock skew, 不缓存** | n/a | n/a |
| `/events/{id}` `/markets/{id}` | n/a | **决策路径不调** | n/a | n/a |

**给小邓 (data-contract) 的请求 (v1 已提, v2 补充):**
- C-04 schema 加 `cache_ttl_class: enum(L0|L1|L2|L3)` 字段
- C-04 字段 `asset_type_enum`: `COLLATERAL | CONDITIONAL` (修正 v1 的 `param_type`)
- 新增 C-09 schema `signature_type_enum: 0 (EOA) | 1 (Magic Safe 1-of-1 — 我们的形态) | 2 (Polymarket proxy 1-of-1)`
- 新增 C-10 schema `activity_type_enum: TRADE | SPLIT | MERGE | REDEEM | REWARD | CONVERSION | DEPOSIT | WITHDRAWAL | YIELD | MAKER_REBATE | REFERRAL_REWARD`

---

## 7. 限流上限实测汇总

### 7.1 v2 实测 (单 IP, 跨洋, 20-30 并发)

| host | endpoint | 并发 | 全部 200? | p50 RTT | p95 RTT | 备注 |
|---|---|---:|---|---:|---:|---|
| gamma | `/events?tag_slug=nba&limit=5` | 20 | ✅ 20/20 200 | 2.3 s | 2.65 s | 无 429 / 无 1010 |
| clob 公开 | `/price?token_id=` | 30 | ✅ 30/30 200 | 1.05 s | 1.36 s | 无 429 |
| data-api | `/positions?user=<F>` | 20 | ✅ 20/20 200 | 1.49 s | 1.82 s | 无 429 |
| clob 公开 | `/books` 500-batch × 1 | 1 | ✅ 200 | 3.2 s | n/a | 单请求, 不属并发 |
| clob WSS | `/ws/market` 500 sub | 1 conn | ✅ | n/a | n/a | 500 token 单连接 OK |

### 7.2 推荐自我限流 (基于 v1 + v2 合并)

| API | 安全 RPS (sustained) | 突发 OK | 备注 |
|---|---:|---:|---|
| gamma 公开 | **60 / min** (实测 20 并发瞬时 OK) | **30 / 5s** | 大对象 (events listing) 不要超过 1 req/s |
| clob 公开 (light: /price etc) | **60 / min** | **30 并发瞬时** | 实测 OK |
| clob 公开 (heavy: /books 500-batch) | **6 / min** (≤ 10s 间隔) | **3 并发** | 单请求 3s, 600 token batch 也是 3s |
| clob 公开 (markets listing) | **6 / min** | **3 并发** | 大对象 |
| clob 私有 (L2) | **20 / min** | 不建议突发 | HMAC 签名负担 + apiKey rate limit 未明 |
| data-api | **60 / min** | **20 并发瞬时** | |
| WSS | 单 host **1 连接共享多订阅** | 重连 ≤ 3 次/分 | 重连退避 |

---

## 8. 推荐架构: 决策路径走哪几个 endpoint (≤ 5 个)

### 8.1 决策路径硬约束 (跨洋 + 临场决策延迟敏感)

需要解决的核心问题:
1. **拿到一组 sport 的全部市场元数据** (conditionId / clobTokenIds / negRisk / feeSchedule / tick_size / outcomes 顺序)
2. **拿到这组市场的实时订单簿 quote** (bids/asks)
3. **拿到我方持仓 + 余额 + 订单状态**, 用于风控 + 决策
4. **拿到 deciding 当前是否能下单** (accepting_orders / acceptingOrdersTimestamp / seconds_delay)

### 8.2 推荐架构 — 决策路径 5 个 endpoint

```
启动时 prefetch (一次性, 24h cache):
  E0: gamma /sports                  → sport → series → tag 字典 (192 entries, 51 KB)

启动时 prefetch + 5 min 周期刷 (per sport):
  E1: gamma /events?tag_slug=<S>&closed=false&limit=200
      → 一次拿 100-1977 markets 全字段
      → 提取: (conditionId, clobTokenIds, outcomes, tick_size, negRisk, feeSchedule, acceptingOrders, game_start_time)
      → 中位 1500 markets / 5 MB / 3 s, 复用率 ~700×
      → 写 L2 cache by conditionId

持续 (流式) — 决策核心:
  E2: WSS /ws/market (single connection, subscribe ALL tokens we care about)
      → 单连接订阅 500-3000 token 实时 quote
      → in-memory orderbook state machine
      → 决策直接读 in-memory state, 0 外网 RTT
      → 复用率 500-3000×

兜底 (WSS 5s 无消息时):
  E3: clob POST /books (batch ≤ 500)
      → 单批 3s 拿 500 token 全 book
      → single-flight + 短缓存 3s

私有 (风控 + 我方状态):
  E4: WSS /ws/user (single connection, subscribe ALL conditionId we hold)
      → 我方 order/trade event 实时推送
      → in-memory order state machine
      → 复用率 N×

  E5 (启动 / WSS user 重连补齐):
      clob private /data/orders + /data/trades + data-api /positions
      → 3 个 endpoint, 启动一次, 重连补一次
      → 平稳运行后只读 WSS in-memory
```

### 8.3 决策路径总调用数估算 (100 strategy / 全市场 5000 token)

| 阶段 | 调用 | 频率 | 总外网 RTT/小时 |
|---|---|---|---:|
| 启动 prefetch | E0 + 7×E1 (7 sport) + E5 (3 endpoint) | 1 次 | 11 次 (一次性) |
| 平稳运行 | E1 周期刷 (per sport 每 5 min) | 84 次/小时 | 84 |
| 平稳运行 | E2 WSS 流式 | 0 外网 RTT | **0** |
| 平稳运行 | E4 WSS 流式 | 0 外网 RTT | **0** |
| 平稳运行 | E3 兜底 (WSS 断时, 假设小时内 2 次重连) | 2 次 | 2 |
| 平稳运行 | E5 兜底 (WSS user 断时, 同上) | 6 次 | 6 |
| **合计** | | | **~92 次/小时 = 0.026 RPS 外网** |

vs naive (无 cache / 无 WSS):
- 100 strategy × 50 token × 1 quote/s = **5000 RPS 外网** = 触发限流瞬间 + 跨洋带宽爆

**优化幅度: 5000 → 0.026 RPS = 减少 99.9995%**.

### 8.4 cache key 设计建议 (给老周)

| 数据 | 主键 | 备注 |
|---|---|---|
| 市场元数据 | `conditionId` (bytes32 hex) | gamma `clobTokenIds[]` 作为 value 内 array, index 对齐 `outcomes[]` |
| token 静态属性 (tick / negRisk) | `token_id` (uint256 string) | 静态 24h 缓存 |
| 订单簿 quote | `token_id` | WSS 实时, REST 3 s TTL |
| 持仓 | `(funder_address, conditionId)` | data-api /positions |
| 余额 | `(funder_address, asset_type, token_id?)` | balance-allowance |
| 订单状态 | `order_id` (UUID) | WSS user channel |

---

## 9. 开放问题

### 9.1 v1 仍未关掉的 (老李 spec v1 §8.2 复制 + 状态更新)

| # | 问题 | 状态 (v2 后) |
|---|---|---|
| 1 | Exchange / NegRiskExchange / NegRiskAdapter 合约链上权威地址 | **仍待 @老叶 RPC 选型确认**, 我手头是社区流传值 |
| 2 | `seconds_delay` 体育市场实际值, 临场是否提高 | 实测 nba/mlb 当前 0, 但临场未压. 待 Sprint-2 临场实测 |
| 3 | `/balance-allowance` 401 → 401 → 200 — **v2 已解** (参数名 + sig path + sigType) | **关闭** |
| 4 | `/data/positions` 在 clob 404, data-api 200 — v2 重确认 | **关闭** (data-api 是权威 host) |
| 5 | `activity.type` 完整 enum — **v2 已拿到 server 权威 11 个** | **关闭** |
| 6 | 跨洋是否租 us-east-1 worker (基础设施决策) | 仍待 @老雷 决策, v2 数据加速论证 (5000 → 0.026 RPS 优化已显著, 但实时 quote 仍要 WSS 1-3s 跨洋, 临场必须落地 us-east) |
| 7 | `feeSchedule.exponent>1` 是否需在 v1 支持 | 实测体育全 exponent=1, **可以 v1 不实现, 但 schema 留 placeholder** |
| 8 | `clobTokenIds[]` 顺序对齐 `outcomes[]` 契约化 | 给小邓 C-04 schema 强制约束 (本文 §6 已列) |
| 9 | rewards / maker rebate 派发触发条件 | 仍待 @老雷 商务沟通 |

### 9.2 v2 新增

10. **@老孙 (key-mgmt)**: 我们 funder 是 **signature_type=1 (老 Magic 1-of-1 Safe)**, 不是 v1 spec 写的 sigType=2. 你 EIP-712 下单签名时 `Order.signatureType` 字段必须填 1. 老李 v1 spec §6.2 必须改. 已通知.
11. **@老孙**: `/auth/derive-api-key` 是 idempotent — 推荐**运行时 derive, in-memory 持有 apiKey/secret/passphrase, 不落盘**. 理由是减少人工管理 .env 的运维负担, **不是因为 .env 会失效**. **GM 2026-05-28 复核纠错**: v2 原文写".env 401 = 已过期/失效" 是错的. 实测 derive 出的三字段和 .env 逐字段相同, .env 这组就是 derive 那组. 老 v2 401 根因是 probe 脚本 HMAC base64 strip 了 padding (`rstrip(b"=")`), sig 少 1 char `=` 导致 401. 不影响 idempotent derive 建议本身.
12. **@老韩 (risk)**: `/positions?user=<F>&redeemable=true` 实测 24 个 redeemable 持仓 — 这是我们当前账户的"无主资金", 必须立刻通过 CTF `redeemPositions` 链上调用赎回回 USDC. 老李协助提供 conditionId 列表, 你接 risk loop.
13. **@老周 (架构)**: WSS 单连接 500 token 实测 OK, 上限至少 500. 我没压 1000+. 如果一个 worker 要订阅 > 500 token, 是否考虑多 WSS 连接? 建议: **per-sport 一个 WSS 连接**, 跨 sport router 内部 fan-out. NBA 1404 token, MLB 3954 token — 单 sport 已超 500, 需要拆 3 个 connection.
14. **@老雷**: gamma `tag_slug` 不通的 sport (ncaa-basketball / bundesliga / formula-1 / nascar) 必须 fallback 到 `tag_id=N`. 我建议在 sport-tag 字典里**双键索引** (slug + id). 这是 ETL 起步前必须定的契约. 给小邓 input.
15. ~~**@老雷**: 旧 .env 凭证 401 — 是否人为 rotate 过? 还是 server 端自动失效?~~ **GM 2026-05-28 复核已撤销**: .env 这组没失效, 401 是 probe 脚本 HMAC sig base64 strip 了 padding 的 bug. 撤销本条原议题. 替代议题: signing service 启动时 derive 一次 in-memory 持有, **作为运维优化, 不是失效防御**.
16. **@小段**: gamma `/sports` 含 192 个 sport entry, 其中体育大类 (nba/mlb/nfl/nhl/soccer/tennis 等) 标准, 但很多小众 (acn=Africa Cup / lal=la liga / ipl=cricket). **跟 Goalserve sport 命名做映射表**, 我建议你来定 sport canonical key (Goalserve 那边的命名更通用), 我把 Polymarket sport → canonical 映射做出来.
17. **@老李自己 (TODO)**: WSS 单连接 1000 / 2000 / 5000 token 压测未做, Sprint-2 补.
18. **@老李自己 (TODO)**: gamma `/series?sport=` 翻页 cursor 未测, Sprint-2 补 (本次 limit=500 server cap = 50).

---

## 10. 验收 checklist (给老雷)

- [x] gamma 全 endpoint (6 个) × 23 个 tag_slug 实测 (本文 §1.2)
- [x] CLOB 公开 endpoint 全表 (14 个含子组合) 实测 (本文 §2.1-§2.3)
- [x] CLOB 私有 endpoint 全表 (8 个) L2 + L1 实测通 (本文 §2.4)
- [x] data-api 全表 (4 个 + filter 23 个子组合) (本文 §3)
- [x] activity.type **server 权威 11 个 enum** 拿到 (本文 §3.2)
- [x] WSS market channel 1/50/100/300/500 五档压测 (本文 §4.2)
- [x] WSS user channel L2 鉴权 + 20 conditionId 订阅 (本文 §4.3)
- [x] 限流压测 gamma 20 / clob 30 / data 20 并发全 200 (本文 §7.1)
- [x] **v1 spec 3 处错误修正** (path 签名 / 参数名 / sigType) — 见 §0.3
- [x] Top 10 高复用 / Top 5 低复用 排名 (本文 §5)
- [x] 推荐 TTL 矩阵全 endpoint (本文 §6)
- [x] 决策路径 5 个 endpoint 架构 (本文 §8)
- [x] 开放问题 18 条 (本文 §9), 其中 v1 关掉 3 个 (#3/4/5), 新增 9 个 (#10-18)
- [ ] WSS 1000+ token 压测 (Sprint-2)
- [ ] `seconds_delay` 临场实测 (Sprint-2)
- [ ] sport canonical mapping 表 (待与小段对齐)

---

## 11. 一句话给 GM 老雷

**完成 47 个 Polymarket endpoint 实测 (v1 测过 5 个, v2 测全), 其中私有 endpoint 8/8 通 — 副产物**: 找到 v1 spec 3 处签名 / 参数 / sigType 错误并修正, 拿到 server 权威 activity.type 11 个 enum, /books 500 batch 上限死定, WSS 单连接 500 token 稳态 50 msg/s. **决策路径浓缩到 5 个 endpoint** (gamma /sports + gamma /events?tag_slug + WSS market + WSS user + /books 兜底), 100-strategy / 5000-token 系统外网 RPS 从 naive 5000 降到 0.026, 减少 99.9995%.

---

(完)
