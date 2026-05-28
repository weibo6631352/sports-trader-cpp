# Polymarket API 技术规范 v1

- Owner: 老李 (polymarket-protocol-expert)
- Last review: 2026-05-28
- Last measured: 2026-05-28 13:33 UTC [需 owner confirm — v1 spec 为 Sprint-1 S1-002, 沿用 v2 probe `laoli-polymarket-matrix-v2-20260528-133323.txt` 实测; 已被 endpoint-matrix-v3 superseded]
- 验收人: 老周
- Ticket: Sprint-1 / S1-002
- 实测环境: macOS 本地 (跨洋链路, 经 Cloudflare), curl 8.4 / Python 3.12 urllib + websocket-client
- 凭证: 从 `.env` 读取, 文件已在 `.gitignore` 内, 文档全程未落实键值

---

## 1. 实测概览

Polymarket 体育对接面公开是 **3 个 REST host + 2 个 WSS channel**:

| Host | 用途 | 鉴权 | 关键发现 |
|------|------|------|---------|
| `gamma-api.polymarket.com` | 赛事元数据 / 事件目录 / sports 列表 | 公开 (无鉴权) | 嵌套 markets 含 `clobTokenIds` / `negRisk` / `feeSchedule`, 是事件 -> 市场 -> token 三层翻译表的主入口 |
| `clob.polymarket.com` | 订单簿 / 报价 / 下单 / 撤单 / 我的成交 | 公开端点公开, 私有端点 L2 (HMAC) + 部分需 L1 (EIP-712) | `POLY_ADDRESS` 必须填 signer EOA, 不能填 funder, 这是头号踩坑 |
| `data-api.polymarket.com` | 用户持仓 / 资金 / 公开活动 | 用 `user=<funder>` 查询参数即可, **无鉴权** | 任何人能查任何 funder 的仓位 / 成交 (链上公开数据) |
| `ws-subscriptions-clob.polymarket.com/ws/market` | 公开订单簿增量 | 无 | 订阅后立即 snapshot, 之后增量 |
| `ws-subscriptions-clob.polymarket.com/ws/user` | 我的订单 / 成交事件 | L2 (apiKey/secret/passphrase 写在 subscribe payload) | 无事件就无推送, 不是心跳消息 |

实测延迟 (本地 -> Polymarket Cloudflare 边缘, TTFB 中位数):
- gamma: ~880ms
- clob 公开: ~900ms
- clob 私有 (L2): ~900-1500ms
- data-api: ~1000ms
- WSS 连接握手: ~1.1s, 之后无明显抖动

跨洋延迟无法在 API 侧优化, 必须靠老吴 (跨区域部署) 把 worker 推到 us-east 落地. 详见 §7.

---

## 2. gamma API

Base URL: `https://gamma-api.polymarket.com`

### 2.1 端点目录 (实测)

| Method | Path | 用途 | 返回 |
|--------|------|------|------|
| GET | `/sports` | 所有体育联盟 (ncaab, epl, lal, mlb, nfl, ...) | array, 每条含 `sport` / `tags` / `series` |
| GET | `/tags?limit=N` | 标签字典 (`tag_id=1` 是 "Sports") | array |
| GET | `/series?limit=N` | 系列赛元 (Stanley Cup, FIFA WC 等) | array |
| GET | `/events?tag_id=1&closed=false&limit=N&order=startDate&ascending=true` | 事件列表, 嵌套 markets | array |
| GET | `/events/{id}` | 单事件详情 | object |
| GET | `/markets/{id}` | gamma 视角的单市场 (跟 clob view 字段不同, 见 §3) | object |

### 2.2 Event schema (顶层关键字段)

```
id              int           gamma 内部 ID
ticker          string        slug 形式 e.g. "2026-nhl-stanley-cup-champion"
slug            string
title           string
startDate       ISO8601       事件开始时间 (UTC)
endDate         ISO8601       事件 / 系列结束 (resolve deadline)
active          bool          事件活跃
closed          bool          已结算
archived        bool
negRisk         bool          系列赛 / outright "其中一个赢" 互斥结构
enableNegRisk   bool          同上, gamma 视角的开关
markets         []Market      子市场 (见 2.3)
liquidity       float         liquidity USD
volume          float
volumeClob      float         CLOB 渠道量, 部分老市场是 fpmm
volume24hr      float
liquidityClob   float
tags            []Tag
eventMetadata   object
```

### 2.3 Market schema (gamma 视角, 嵌入 event.markets[])

我手抄一份**字段语义表**, 这是后续序列化工程师 (老韩 / 小石) 的契约源:

```
id                       int               gamma market id, 不是链上标识
conditionId              0x... bytes32     **链上 CTF condition id** —— 跟 clob 串场的主键
questionID               0x... bytes32     UMA question id (oracle)
slug                     string
question                 string            人读问题文本
description              string
endDate / endDateIso     ISO8601           市场到期
startDate / startDateIso ISO8601
outcomes                 JSON string       e.g. `'["Yes","No"]'` —— 注意是 stringified JSON
outcomePrices            JSON string       e.g. `'["0.555","0.445"]'` —— 同上, stringified
clobTokenIds             JSON string       e.g. `'["7939...","4047..."]'` —— **uint256 字符串**, 直接喂 clob /book?token_id=
                                           **顺序与 outcomes 数组对齐: index 0 是 Yes/Home, index 1 是 No/Away**
orderPriceMinTickSize    float             tick size (e.g. 0.01, 高赔率冷门可能 0.001)
orderMinSize             float             最小订单 USD size (常见 5)
acceptingOrders          bool              下单允许窗 (赛前 / 临场可能关)
acceptingOrdersTimestamp ISO8601
ready                    bool              已就绪可挂单
funded                   bool              已铺底
enableOrderBook          bool              是否走 CLOB (否则 fpmm)
negRisk                  bool              **负风险市场标志, 影响下单合约**
negRiskMarketID          0x... bytes32     负风险市场 marketId
negRiskRequestID         0x... bytes32
makerBaseFee             int               bps? 实测 1000 但同时 feeType=sports_fees_v2 才是真规则, 见下
takerBaseFee             int               同上
feeType                  string            **"sports_fees_v2"** —— 体育市场专属费率
feeSchedule              object            {"exponent":1,"rate":0.03,"takerOnly":true,"rebateRate":0.25}
                                           解读: taker 收 3%, maker 0%, 已成交 maker 拿 25% 返佣
feesEnabled              bool
spread                   float             实时 best ask - best bid
bestBid / bestAsk        float             gamma 缓存的最优价 (有秒级延迟, 实时拿 clob /book)
lastTradePrice           float
oneDayPriceChange        float
volume / volumeClob      float
liquidityClob            float
marketMakerAddress       0x... address     如果是 fpmm 模式才有
groupItemTitle           string            outright 子项目标题 (e.g. 球队名)
groupItemThreshold       string            handicap / total 的阈值
rewardsMinSize / rewardsMaxSpread          做市激励参数 (官方 rewards program)
clobRewards              []                奖励配置
holdingRewardsEnabled    bool
rfqEnabled               bool              是否启用 RFQ
umaResolutionStatuses    array             oracle 状态机
```

### 2.4 限流 / 缓存

- 20 并发同时打 `/events` 无 429, 无 Cloudflare 拦截
- 服务端有秒级缓存: `bestBid` / `bestAsk` / `volume24hr` 这些聚合数据有 1-5s 漂移, 想精确**必须打 clob**
- gamma response 体积大: `/events?tag_id=1&limit=3` 已经 419KB, 不能高频拉. 建议 5-10s 轮一次拉 `events?closed=false`, 增量 diff

### 2.5 gamma 已知坑

1. `outcomes` / `outcomePrices` / `clobTokenIds` 三个字段都是 **stringified JSON**, 不是 array, 要二次 parse
2. `outcomes[0]` 和 `clobTokenIds[0]` 严格对齐, 别用 `outcomes` 字面去查 token, 用 index
3. 同一个事件有 negRisk 和非 negRisk 混在 markets[] 中 (我没见到, 但 doc 提过), 用前先看 `market.negRisk` 字段单独判
4. `endDateIso` 和 `endDate` 偶尔不一致 (`endDate` 可能是 question close, `endDateIso` 是市场关闭), 体育统一用 `endDateIso`
5. `acceptingOrders=false` 不等于市场关闭, 可能只是临场冻结, 仍可查价

---

## 3. CLOB REST

Base URL: `https://clob.polymarket.com`

### 3.1 公开端点 (无鉴权)

| Method | Path | 用途 | 返回示例 |
|--------|------|------|---------|
| GET | `/time` | 服务端 Unix 秒 | `1779941792` (纯数字) |
| GET | `/tick-size?token_id=` | 该 token 的最小 tick | `{"minimum_tick_size":0.01}` |
| GET | `/neg-risk?token_id=` | 是否 neg-risk | `{"neg_risk":true}` |
| GET | `/book?token_id=` | 单边订单簿 (按 token, 不是 conditionId) | `{market, asset_id, timestamp, hash, bids:[{price,size}...], asks:[...]}` |
| POST | `/books` | 批量订单簿, body `[{token_id},...]` | array, 每项含 `min_order_size` `tick_size` `neg_risk` `last_trade_price` (比 /book 字段更全) |
| GET | `/price?token_id=&side=BUY\|SELL` | 单边最优价 | `{"price":"0.55"}` |
| GET | `/midpoint?token_id=` | 中价 | `{"mid":"0.555"}` |
| GET | `/spread?token_id=` | bid-ask spread | `{"spread":"0.01"}` |
| GET | `/prices-history?market=<token_id>&interval=1d&fidelity=60` | K 线 | `{"history":[{"t":<unix>,"p":<float>}...]}` 注意 `market=` 这里要传的是 **token_id**, 不是 conditionId, 命名坑 |
| GET | `/markets?next_cursor=MA==` | 全市场分页, cursor base64 形式 | `{data:[Market], next_cursor, limit, count}` `limit=1000` 是上限 |
| GET | `/markets/{condition_id}` | 单市场 (clob 视角) | Market object |
| GET | `/sampling-markets` | 当前有 rewards 程序的活跃市场 | 同 /markets 结构 |

### 3.2 CLOB Market schema (跟 gamma 不同)

```
condition_id              0x... bytes32     **跟 gamma.conditionId 是同一字段**
question_id               0x... bytes32     UMA
question                  string
market_slug               string
end_date_iso              ISO8601
game_start_time           ISO8601           体育独有, 比赛开球时间 (用于赛前冷冻)
seconds_delay             int               官方延迟撮合秒数 (反 latency arb)
enable_order_book         bool
active / closed / archived bool
accepting_orders          bool
accepting_order_timestamp ISO8601
minimum_order_size        float
minimum_tick_size         float
neg_risk                  bool
neg_risk_market_id        0x... bytes32
neg_risk_request_id       0x... bytes32
maker_base_fee / taker_base_fee  int        clob 视角的费基
is_50_50_outcome          bool              Yes/No 二元 vs outright 子项
rewards                   {rates:[],min_size,max_spread}
tokens                    [{token_id, outcome, price, winner}]   **每条 token 含 winner 字段, 用于结算后判赢**
tags                      []
fpmm                      0x... address     非空 = 老 fpmm 市场, 别走 CLOB
```

### 3.3 私有端点 (鉴权)

**鉴权头 (必须 5 个):**
```
POLY_ADDRESS    = signer EOA 地址 (从 WALLET_PRIVATE_KEY 推出), **不是 funder**
POLY_API_KEY    = UUID 形式 (36 字符)
POLY_PASSPHRASE = 64 字符 hex
POLY_SIGNATURE  = base64url(HMAC-SHA256(base64url_decode(secret), ts + method + path + body))
POLY_TIMESTAMP  = Unix 秒字符串
```

**签名细节 (实测验证):**
- `secret` 是 base64url 编码的 32 字节, decode 后用作 HMAC key
- `path` 包含 querystring (例如 `/balance-allowance?param_type=COLLATERAL&signature_type=2`)
- `body` 是请求体原文, GET 请求空字符串
- 签名结果 base64url 编码, **不去 padding 也可** (Polymarket 兼容)
- 时间窗约 ±10s, 用 `/time` 校准本地时钟

**实测可用私有端点:**

| Method | Path | 用途 | 返回 |
|--------|------|------|------|
| GET | `/auth/api-keys` | 列我名下所有 apiKey | `{"apiKeys":["..."]}` |
| GET | `/auth/derive-api-key` | **L1 鉴权**, 用 EIP-712 签名换 apiKey/secret/passphrase | `{apiKey, secret, passphrase}` |
| POST | `/auth/api-key` | 新建 apiKey (也是 L1) | 同上 |
| DELETE | `/auth/api-key` | 注销 apiKey (L2) | |
| GET | `/trades` | 我的成交历史 | `{data:[Trade], next_cursor, limit, count}` 含 `taker_order_id`, `market`, `asset_id`, `price`, `size`, `side`, `transaction_hash`, `fee_rate_bps`, `bucket_index`... |
| GET | `/data/orders` | 我的活跃订单 (空时返回 data:[]) | `{data:[], next_cursor, limit, count}` |
| GET | `/balance-allowance?param_type=COLLATERAL\|CONDITIONAL&signature_type=2[&token_id=]` | 我的 USDC / CTF 余额和授权 | 测试时 401, 怀疑 signature_type 取值有更细分, 见 §8 开放问题 |
| POST | `/order` | 下单, body 是 EIP-712 签好的 order struct | 见 §6 |
| DELETE | `/order` | 撤单 | |
| POST | `/orders` | 批量下单 | |
| DELETE | `/orders` | 批量撤单 | |
| POST | `/cancel-all` | 全撤 | |
| GET | `/data/order/{order_id}` | 单订单状态 | |

**signature_type 字段:**
- `0` = EOA 直签 (普通钱包)
- `1` = Polymarket proxy wallet (Gnosis Safe 形式, 老 Magic 用户)
- `2` = Polymarket proxy wallet (1-of-1 Safe, 当前主流, 我们就是这种)

我们 funder `0x78dE...8686BE` 是 signature_type=2 的 proxy, signer EOA `0xB9c8...38d7` 通过 proxy 操作.

### 3.4 CLOB 已知坑

1. **`POLY_ADDRESS` 是 signer 不是 funder** —— 我用 funder 一直 401, 改 signer 立刻 200. 这点 Polymarket 官方文档藏在 py-clob-client 源码里, 文档站没写明
2. base64url vs base64: secret 是 urlsafe (含 `-` `_`), 签名输出也用 urlsafe. 用普通 base64 lib 会偶发失败 (取决于字节)
3. `prices-history?market=` 这里 `market` 实际是 `token_id`, gamma 用 `clobTokenIds`, clob 用 `asset_id`, 同一个东西三个名字, 序列化层要做别名
4. `/orders` GET 是 405 (不允许), 列订单是 `/data/orders`. doc 站老旧导致混淆
5. `/data/positions` 404 —— 持仓在 data-api host, 不在 clob host
6. Cloudflare WAF 会拦默认 UA (python urllib 直接 403 1010), client 必须设 User-Agent 类似 `curl/8.4.0` 或自定义产品名
7. `seconds_delay` 字段非零时表示官方人为撮合延迟, 反延迟套利, 体育市场常见 1-3 秒, **策略侧必须把这个延迟纳入 PnL 模型**
8. `tokens[i].winner` 在结算前是 false, 结算后变 true, 配合 `/data/positions.redeemable=true` 做赎回

---

## 4. data REST

Base URL: `https://data-api.polymarket.com`

**鉴权: 无.** 用 `?user=<funder_address>` 查询参数, 任何人能查任何 funder. 这是链上公开数据的镜像服务.

| Method | Path | 用途 | 关键字段 |
|--------|------|------|---------|
| GET | `/positions?user=<funder>` | 当前持仓 | `proxyWallet, asset, conditionId, size, avgPrice, initialValue, currentValue, cashPnl, percentPnl, realizedPnl, curPrice, redeemable, mergeable, title, slug, icon, outcome, outcomeIndex, endDate, negativeRisk` |
| GET | `/value?user=<funder>` | 账户净值 | `[{user, value}]` |
| GET | `/trades?user=<funder>&limit=N` | 历史成交 | `proxyWallet, side, asset, conditionId, size, price, timestamp, title, slug, outcome, outcomeIndex, name, pseudonym, transactionHash...` |
| GET | `/activity?user=<funder>&limit=N` | 链上动作流 (含 REDEEM / MERGE / TRADE / CONVERT) | `timestamp, type, size, usdcSize, conditionId, transactionHash, title...` |

未通的: `/holdings` `/pnl` `/user` `/leaderboard` 都 404. 看来 data-api 只有上面 4 个稳定面.

**关键观察:**
- `positions[].redeemable=true` 表示该仓位已结算可赎回, 这是我们必须监控的 — 老钱说要在 risk-manager 把 redeemable 自动赎回
- `positions[].mergeable=true` 表示 Yes+No 持仓相等可以 burn 拿回 USDC (negRisk merge)
- `activity[].type` 我看到的取值: `TRADE`, `REDEEM`. 其他可能值: `MERGE`, `SPLIT`, `CONVERT`, `REWARD`, 见 §8

### 4.1 data-api 已知坑

1. 链路有秒级延迟, 不是 chain-tip realtime. 严格对账要走 Polygon RPC (老叶的活)
2. 同一笔成交在 `/trades` 和 clob `/trades` 字段不同: data-api 给的是用户视角带标题, clob 给的是订单级 (含 order_id / fee_rate_bps), 用途不重叠
3. `outcomeIndex=999` 我在 REDEEM 活动里看到 —— 应该是 "整个市场赎回" 而非单 outcome, 不要按 index 解析
4. `proxyWallet` 字段是小写地址 — checksum 比较时要 lowercase 后比

---

## 5. WSS

Base URL: `wss://ws-subscriptions-clob.polymarket.com`

### 5.1 公开 market channel: `/ws/market`

**订阅 payload:**
```json
{"type":"Market","assets_ids":["<token_id1>","<token_id2>",...]}
```

注意复数 `assets_ids` (拼写有 s), 单次可订阅多 token, 上限我没碰到.

**消息类型:**

| event_type | 触发 | 字段 |
|-----------|------|------|
| `book` | 订阅时立即推一次 snapshot, 之后偶尔重传 | `market, asset_id, timestamp, hash, bids:[{price,size}], asks:[...], tick_size, last_trade_price, event_type` |
| `price_change` | 价位变动 (挂单 / 撤单 / 部分成交) | `market, price_changes:[{price, side, size}], timestamp, event_type` |
| `last_trade_price` | 单笔成交 (我没在 12s 内捕到, 文档列了) | `market, asset_id, price, size, side, timestamp` |
| `tick_size_change` | 罕见, tick size 调整时 | |

**消息频率 (实测):**
- 单 token 静止市场: ~0.1 msg/s (12s 内 2 条)
- 临场热门市场预估 5-50 msg/s (本次未测, 因当时无球赛进行)
- 老吴的跨洋方案要按 50 msg/s 设上限做带宽预算

**心跳:** 客户端发 `PING` 文本, 服务端不回应也不断 (实测). WebSocket 协议层的 ping/pong 是另一回事. 保活策略: **应用层每 10s 发 `PING`**, 收到 `PONG` 不强求, 30s 无任何消息再判死.

**重连:** 连接断开后, 重新订阅会拿到新 snapshot. 没有 sequence number, 用 `timestamp` + `hash` 校验状态机一致.

### 5.2 私有 user channel: `/ws/user`

**订阅 payload (auth 在 payload 里, 不在头里):**
```json
{
  "type":"User",
  "auth":{
    "apiKey":"<POLYMARKET_API_KEY>",
    "secret":"<POLYMARKET_API_SECRET>",
    "passphrase":"<POLYMARKET_API_PASSPHRASE>"
  },
  "markets":["<condition_id1>","<condition_id2>"]
}
```

注意是 `markets` (复数), 内容是 **conditionId**, 不是 token_id. 跟 market channel 反过来.

**消息类型:**
- `trade`: 我的订单被成交
- `order`: 订单状态变更 (PLACED / MATCHED / CANCELED / EXPIRED)

实测无新订单时无消息, 不发心跳. 静默连接 = 健康.

### 5.3 WSS 已知坑

1. market channel 用 `assets_ids` (token_id), user channel 用 `markets` (conditionId), 字段名混乱, client 封装层要做适配
2. 无 sequence number, 重连后只能用 hash 比对状态机
3. 用 `PING` 字面字符串保活, 不是 RFC websocket ping frame (虽然 Polymarket 也接受 frame ping)
4. 连接握手 ~1.1s (跨洋), reconnect 频率要节制 — 老蒋的 latency budget 里要扣这部分
5. user channel auth 失败时**不会立即断**, 静默无消息, 容易误判健康. **必须开 user channel 后下个测试单验证**

---

## 6. EIP-712 签名要点 (与老孙对接)

老孙做 key 管理 / 签名服务, 这里写给他的契约.

### 6.1 L1 认证: `ClobAuth` 类型

用途: 派生 API key (`/auth/derive-api-key`), 创建 API key (`/auth/api-key` POST)

```
EIP712Domain:
  name      = "ClobAuthDomain"
  version   = "1"
  chainId   = 137  (Polygon mainnet)

Type ClobAuth:
  address   address      # signer EOA
  timestamp string       # Unix 秒, 注意类型是 string
  nonce     uint256
  message   string       # 固定字面: "This message attests that I control the given wallet"
```

签出 65 字节 ECDSA sig (r||s||v), hex 编码加 `0x` 前缀.

请求头:
```
POLY_ADDRESS   = <signer EOA>
POLY_SIGNATURE = 0x<hex sig>
POLY_TIMESTAMP = <同 typed data 里的 ts>
POLY_NONCE     = <同 typed data 里的 nonce, 默认 0>
```

### 6.2 下单签名: `Order` EIP-712

用途: POST `/order` body 里的 `signature` 字段

```
EIP712Domain:
  name      = "Polymarket CTF Exchange"
  version   = "1"
  chainId   = 137
  verifyingContract = 0x4bFb41d5B3570DeFd03C39a9A4D8dE6Bd8B8982E  # Exchange (非 negRisk)
                    或 0xC5d563A36AE78145C45a50134d48A1215220f80a  # Exchange (negRisk)
                       (具体地址以老叶给的链上配置为准, 我这里是社区实测值)

Type Order:
  salt          uint256   # 客户端生成随机, 防重放
  maker         address   # 资金账户 (funder)
  signer        address   # 签名地址 (EOA)
  taker         address   # 0x0 表示任意 taker
  tokenId       uint256   # CTF token id
  makerAmount   uint256   # 6-dec USDC 或 token amount
  takerAmount   uint256   # 配对
  expiration    uint256   # 过期 Unix 秒, 0 = GTC
  nonce         uint256
  feeRateBps    uint256
  side          uint8     # 0 = BUY, 1 = SELL
  signatureType uint8     # 0/1/2 同 §3.3
```

**关键点:**
- `maker` = funder, `signer` = EOA, 两个地址不一样, server 用 signatureType 验证 signer 有权代 maker
- negRisk 市场用不同 verifyingContract, 选错合约签名会被拒
- `salt` 必须每次随机, 服务端会拒重复 salt
- `feeRateBps` 必须匹配市场 `feeSchedule.rate * 10000` (体育是 300), 不匹配 server 拒
- `makerAmount` / `takerAmount` 都是 6 decimals (USDC 标准), 不是 18

### 6.3 撤单签名

DELETE `/order` 不需要再签 EIP-712, 用 L2 HMAC 鉴权 + body 里给 `order_id` 即可. 服务端从 order_id 查回 maker, 验证调用方 apiKey 属于该 maker.

### 6.4 给老孙的具体需求

签名服务需要提供 4 个 RPC:
1. `signClobAuth(ts, nonce) -> sig_hex` (L1)
2. `signOrder(orderStruct, verifyingContract) -> sig_hex` (L2 下单)
3. `signL2Headers(method, path, body) -> {sig, ts}` (L2 HMAC, 用 apiSecret)
4. `getSignerAddress() -> address` (供 POLY_ADDRESS / Order.signer 用)

私钥 `WALLET_PRIVATE_KEY` 永远不出签名服务, 其他模块只看到地址和签名结果.

---

## 7. 实测延迟 / 限流数据

### 7.1 单次延迟 (本地 -> Polymarket, TTFB)

| 端点 | 1 | 2 | 3 | 4 | 5 | 中位 |
|------|---|---|---|---|---|------|
| `gamma /events?limit=5` | 0.68 | 0.90 | 0.90 | 0.88 | 0.88 | **0.88s** |
| `clob /book` | 1.17 | 0.88 | 0.88 | 0.85 | 0.91 | **0.88s** |
| `clob /price` | 0.95 | - | - | - | - | **0.95s** |
| `clob /midpoint` | 0.94 | - | - | - | - | **0.94s** |
| `clob /trades (L2)` | 0.95 | - | - | - | - | **0.95s** |
| `data-api /positions` | 1.08 | - | - | - | - | **1.08s** |
| `data-api /trades` | 0.98 | - | - | - | - | **0.98s** |
| `clob /sampling-markets` (2.4MB) | 5.96 TTFB / 7.63 总 | - | - | - | - | **6s** 大对象专项 |
| WSS `/ws/market` open + snapshot | 2.05s (含握手 ~1.1s) | - | - | - | - | **2s 首包** |

### 7.2 burst 测试 (20 并发 `/price`)

20 个并发 curl, 全部 200, 完成时间 0.88s ~ 1.41s, **无 429 / 无 rate limit**.

这只是 20 并发的样本, 老吴老蒋如果做 1000 token 同时查需要再扫一遍, 我建议先按 **public 端点 50 rps, private 20 rps** 自我限流, 这是社区流传的稳态值, Polymarket 官方未公开硬上限.

### 7.3 实测 curl 命令清单 (可复现, 不含凭证)

```bash
# 公开
curl -sS -A "curl/8.4.0" "https://gamma-api.polymarket.com/sports"
curl -sS -A "curl/8.4.0" "https://gamma-api.polymarket.com/events?tag_id=1&closed=false&limit=3&order=startDate&ascending=true"
curl -sS -A "curl/8.4.0" "https://gamma-api.polymarket.com/events/27829"

curl -sS -A "curl/8.4.0" "https://clob.polymarket.com/time"
curl -sS -A "curl/8.4.0" "https://clob.polymarket.com/tick-size?token_id=<TOKEN>"
curl -sS -A "curl/8.4.0" "https://clob.polymarket.com/neg-risk?token_id=<TOKEN>"
curl -sS -A "curl/8.4.0" "https://clob.polymarket.com/book?token_id=<TOKEN>"
curl -sS -A "curl/8.4.0" "https://clob.polymarket.com/price?token_id=<TOKEN>&side=BUY"
curl -sS -A "curl/8.4.0" "https://clob.polymarket.com/midpoint?token_id=<TOKEN>"
curl -sS -A "curl/8.4.0" "https://clob.polymarket.com/spread?token_id=<TOKEN>"
curl -sS -A "curl/8.4.0" "https://clob.polymarket.com/prices-history?market=<TOKEN>&interval=1d&fidelity=60"
curl -sS -A "curl/8.4.0" "https://clob.polymarket.com/markets?next_cursor=MA=="
curl -sS -A "curl/8.4.0" "https://clob.polymarket.com/markets/<CONDITION_ID>"
curl -sS -A "curl/8.4.0" -X POST -H "Content-Type: application/json" \
  "https://clob.polymarket.com/books" -d '[{"token_id":"<T1>"},{"token_id":"<T2>"}]'

curl -sS -A "curl/8.4.0" "https://data-api.polymarket.com/positions?user=<FUNDER>"
curl -sS -A "curl/8.4.0" "https://data-api.polymarket.com/value?user=<FUNDER>"
curl -sS -A "curl/8.4.0" "https://data-api.polymarket.com/trades?user=<FUNDER>&limit=3"
curl -sS -A "curl/8.4.0" "https://data-api.polymarket.com/activity?user=<FUNDER>&limit=2"

# 私有 (L2 HMAC, headers 由签名服务生成, 这里只列形态)
curl -sS -A "curl/8.4.0" "https://clob.polymarket.com/trades" \
  -H "POLY_ADDRESS: <SIGNER_EOA>" \
  -H "POLY_API_KEY: <KEY>" \
  -H "POLY_PASSPHRASE: <PASS>" \
  -H "POLY_TIMESTAMP: <TS>" \
  -H "POLY_SIGNATURE: <SIG>"
```

---

## 8. 风险与开放问题

### 8.1 风险

1. **跨洋延迟 ~880ms** 是物理硬限, 任何"决策即下单"路径都要 >1s, 这迫使 latency-sensitive 策略必须在 us-east 落地. 给老吴 / 老蒋的输入: 至少 worker 必须靠近 polymarket 边缘
2. **官方 `seconds_delay` 反延迟套利**: 体育市场 server 故意延迟撮合 1-3s, 必须把这个延迟塞进策略 PnL 估计, 否则会反复亏 toxic fill
3. **3% taker fee + 25% rebate (sports_fees_v2)**: 体育市场 taker 单边 3%, 这是巨额成本. 策略不挂 maker 几乎不可能盈利. 老彭做行业分析时要重点比同行
4. **Cloudflare 拦截默认 UA**: client 必须设 User-Agent, 否则 403 1010. 老吴写 client 时记得初始化 header
5. **API key 与 funder 绑死**: derive-api-key 是 EOA -> apiKey 一对一, 同一 EOA 反复 derive 拿到同一组. 我们当前 .env 凭证是有效的, 但只对一个 funder. 多 funder 体系要么多 EOA 要么多 apiKey 体系
6. **negRisk 合约和普通合约 verifyingContract 不同**: 签错就拒. 序列化层要强制把 `negRisk` 字段 propagate 到下单路径

### 8.2 开放问题 (待 @老雷 + @老叶 确认)

1. **@老叶**: Exchange / NegRiskExchange / NegRiskAdapter 三个合约的链上权威地址我这里只有社区流传值, 你能从你那 RPC 选型确认下吗 (`getImplementation()` 或 etherscan 验证)
2. **@老雷**: `seconds_delay` 体育市场实际值是多少 (我看到 0 但临场可能调高), 这影响策略可执行性, 我没法在 dev 环境压测临场, 求线上观测授权
3. **@老雷**: `/balance-allowance` 我用 `signature_type=2` 还是 401, 怀疑还要额外 `wallet_type` 或 `proxy_address` 参数. 是否可让老孙临时帮我下个 1 USDC 真单, 用 py-clob-client 抓 wire 比对? (按你定的纪律不下真单, 等你批)
4. **@老雷**: `/data/positions` 在 clob host 404 但 data-api host 200, doc 站老旧, 是否还有 `data-clob` 第四个 host 我没扫到?
5. **@老叶**: `activity.type` 全枚举值, 我只见到 `TRADE` `REDEEM`, 还有 `MERGE` `SPLIT` `CONVERT` `REWARD` 吗? CTF 子图能给完整 enum 吗
6. **@老雷**: 跨洋是否考虑租 vultr.us-east-1 节点专跑 polymarket worker, 把决策延迟从 1s 砍到 50ms? 这是基础设施决策, 我只摆数据
7. **@老雷**: 体育市场 `feeSchedule.exponent=1` 是线性, 但官方文档提过有 exponent>1 的二次费率市场, 实例没见到, 是否需要在 v1 就支持
8. **@老叶**: `clobTokenIds[]` 顺序对齐 `outcomes[]`, 这是我从多市场抽样观察的, 官方契约能在你那 ADR 里固化吗 (否则未来翻一次哭一次)
9. **@老雷**: rewards / makeR rebate 的具体派发触发条件 (是按订单时段, 还是按 maker 时间在簿)? 这影响做市策略, 但不在 S1 范围, 先记录

---

## 附: 实测脚本片段保留位置

实测过程中产生的 JSON 样本在 `/tmp/laoli_*.json`, 测试结束会清理. 如果老周复盘需要, 可按 §7.3 命令再跑一遍, 输出确定性高 (除 timestamp / volume 等时变字段).

签名脚本 (Python, 不含凭证) 也在 venv `/tmp/laoli_venv` 里, 不入仓. 老孙如需正式 C++ 实现参考, 我可以再整一份 wire-only 协议跟踪文档.

---

(完)
