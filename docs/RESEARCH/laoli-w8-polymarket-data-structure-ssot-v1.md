# Polymarket 数据结构 SSOT v1

- owner: 老李 (polymarket-protocol-expert, #07)
- last_review: 2026-05-29
- sprint: W8 Wave 40 P0
- 触发: 老板 verbatim 2026-05-29 "数据结构很重要, 快点补齐吧, 摸清楚后起码大家看到后可以对市场结构和数据源结构有个清楚的认知"
- 依据 (一手 raw payload):
  - `docs/RESEARCH/data/polymarket-initialState.json` (1.6MB Redux store, 58 events)
  - `docs/RESEARCH/data/polymarket-sports-live-xhr.json` (9 XHR 实测)
  - `docs/RESEARCH/data/polymarket-sports-live-NEXT_DATA.json` (Next.js SSR)
  - `docs/RESEARCH/laoli-polymarket-api-spec-v1.md` (wire 契约)
  - `docs/RESEARCH/laoli-laoSun-handshake-v1.md` (SignedOrder + Position ABI lock)
  - `docs/RESEARCH/laoli-polymarket-endpoint-matrix-v3.md` (HMAC + endpoint 矩阵)
- 验收: 老周 (工程 ABI owner) + 老韩 (RM owner) + 老孙 (signer owner) + 老郭 (架构)

---

## §1 老板 verbatim 入约束

老板 2026-05-29 原话:

> "数据结构很重要, 快点补齐吧, 摸清楚后起码大家看到后可以对市场结构和数据源结构有个清楚的认知"

**直接触发背景:** GM 审计发现工程层 OrderIntent / Side enum 缺 token_id / outcome 字段, 与老李 spec v1 §88-91 (clobTokenIds / outcomes) 不对齐. 老李 spec v1 本身没错 — GM 代申辩. 根因是项目缺一个全员可读的数据结构 SSOT, 工程层拿 spec 时遗漏了 token 层.

**本文目标:**

1. 把 Polymarket 三层结构 (Event -> Market -> Token/Outcome) 用 ASCII 图+字段表一次说清
2. 工程层所有 struct (OrderIntent / SignedOrder / Position / MarketInfo / Orderbook) 的字段语义明确锚定到 Polymarket 原始字段
3. 当前工程 vs 真实结构的 gap 一览, 给修复方向

**ADR 约束 (本文立, 老郭 W8 W5 出 ADR-026):**
- 核心数据结构 (OrderIntent / SignedOrder / Position) 必须引用 Polymarket 一手 payload 字段
- 工程 ABI 改动必须三方 cross-check: 老李 (协议) + 老孙 (signer) + 老韩 (RM)
- 任何工程 struct 加字段前必须对照本文确认 Polymarket 原始字段存在且语义一致

---

## §2 市场结构 (从一手 raw payload 倒推)

### §2.1 三层结构总览

Polymarket 体育市场是严格三层嵌套结构:

```
Event/Game (赛事, gamma event 级)
   |  id, ticker, slug, title, startDate, endDate, active/closed, negRisk
   |  1 个体育比赛 = 1 个 Event
   |
   +-- Market_1 (盘口, condition_id 级)
   |      condition_id = 0xabc... (bytes32, 链上 CTF condition)
   |      question = "Will Home Win?"
   |      market_type = Moneyline / Total / Spread / Prop / 系列赛...
   |      |
   |      +-- Outcome[0]: "Home Win" / "Yes" / "Over X"
   |      |       token_id = "79394..." (uint256 string)
   |      |       price = 0.555  (当前市场价, 0~1)
   |      |       winner = false (结算后变 true)
   |      |       orderbook (bid/ask depth, 按 token_id 独立索引)
   |      |
   |      +-- Outcome[1]: "Away Win" / "No" / "Under X"
   |              token_id = "40471..." (uint256 string)
   |              price = 0.445
   |              winner = false
   |              orderbook (bid/ask depth, 按 token_id 独立索引)
   |
   +-- Market_2 (Total Over/Under, condition_id = 0xdef...)
   |      +-- Outcome[0]: "Over 220.5" → token_id = "6164..."
   |      +-- Outcome[1]: "Under 220.5" → token_id = "5451..."
   |
   +-- Market_3 (Spread, condition_id = 0xghi...)
   |      +-- Outcome[0]: "Team A -4.5" → token_id = "9475..."
   |      +-- Outcome[1]: "Team B +4.5" → token_id = "6188..."
   |
   +-- ... (Props / 分节 / 系列赛子项)
```

### §2.2 关键关系图 (详细版)

```
                      ┌────────────────────────────────────┐
                      │          Event / Game               │
                      │  id (gamma int)                     │
                      │  ticker / slug / title              │
                      │  startDate / endDate (ISO8601)      │
                      │  active=true, closed=false          │
                      │  negRisk (bool, outright 系列赛)    │
                      │  markets[] → 下一层                  │
                      └──────────────┬─────────────────────┘
                                     │ 1..N
              ┌──────────────────────┼─────────────────────┐
              ▼                      ▼                      ▼
   ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐
   │   Market         │  │   Market         │  │   Market         │
   │ (Moneyline)      │  │ (Total)          │  │ (Spread / Prop)  │
   │ condition_id=0x.│  │ condition_id=0x. │  │ condition_id=0x. │
   │ outcomes=["Yes" │  │ outcomes=["Over" │  │ outcomes=["H-4.5"│
   │          "No"]  │  │          "Under"]│  │          "A+4.5"]│
   │ clobTokenIds=   │  │ clobTokenIds=    │  │ clobTokenIds=    │
   │ ["794...","405"]│  │ ["616...","545..."]  │ ["947...","618..."]
   │ negRisk=false   │  │ negRisk=false    │  │ negRisk=false    │
   └────────┬────────┘  └────────┬─────────┘  └───────┬──────────┘
            │ index 0,1           │ index 0,1           │ index 0,1
     ┌──────┴──────┐       ┌──────┴──────┐        ┌────┴────────┐
     ▼             ▼       ▼             ▼        ▼             ▼
 Token[0]      Token[1] Token[0]     Token[1]  Token[0]    Token[1]
 outcome="Yes" out="No" out="Over"  out="Under" out="H-4.5" out="A+4.5"
 token_id=     token_id= token_id=   token_id=  token_id=   token_id=
 "794..."      "405..."  "616..."    "545..."   "947..."    "618..."
     |              |       |             |         |            |
     ▼              ▼       ▼             ▼         ▼            ▼
 Orderbook      Orderbook Orderbook    Orderbook Orderbook   Orderbook
 /book?         /book?    /book?       /book?    /book?      /book?
 token_id=      token_id= token_id=    token_id= token_id=   token_id=
 "794..."       "405..."  "616..."     "545..."  "947..."    "618..."
```

### §2.3 condition_id vs token_id — 最重要的概念区分

| 维度 | condition_id | token_id |
|---|---|---|
| 类型 | bytes32 (0x 前缀 hex, 66 char) | uint256 string (无 0x 前缀, 十进制, 最多 77 位) |
| 粒度 | 整个 market (盘口) 级 | 单个 outcome (结果) 级 |
| 对应关系 | 1 condition_id = 1 market = 2 token_id (二元市场) | 1 token_id = 1 orderbook |
| Polymarket 字段名 | gamma: `conditionId`; clob: `condition_id`; user channel 订阅: `markets[]` | gamma: `clobTokenIds[i]`; clob: `tokens[i].token_id`; market channel 订阅: `assets_ids[]`; /book 查询: `token_id=` |
| 示例 | `0xa9db600590209698097db2fb8382989ea1cf6a9b91f0428b2e1d4f35d724c3ff` | `1677202003548168512111076196662317438975560192301735320827449539424843146463` |
| 下单用哪个 | SignedOrder 需要两个都传 (EIP-712 Order.tokenId 用 token_id, 鉴权 user channel 用 condition_id) | SignedOrder.tokenId = uint256 形式 |
| Orderbook API | clob `/book?token_id=` 或 POST `/books` | 同左 |
| 历史 K 线 | clob `/prices-history?market=<token_id>` (注意: 参数名是 `market` 但值是 token_id, 命名陷阱) | 同左 |

**结论: token_id 是 CLOB 的一等公民. 挂单、查簿、订阅 orderbook, 全用 token_id. condition_id 用于 market 元数据、user channel 订阅、风控 per-market cap 层.**

### §2.4 outcomeIndex 对齐规则

gamma Market 字段 `outcomes`, `outcomePrices`, `clobTokenIds` 均为 **stringified JSON array**, 三者 index **严格对齐**:

```
outcomes[i]      ↔  outcomePrices[i]  ↔  clobTokenIds[i]
"Yes"               "0.555"              "794..." (Outcome Yes 的 token_id)
"No"                "0.445"              "405..." (Outcome No 的 token_id)
```

clob `/markets/{cid}` 的 `tokens[]` 同样对齐:
```
tokens[i].outcome  ↔  tokens[i].token_id  ↔  tokens[i].price  ↔  tokens[i].winner
"Yes"                  "794..."               0.555               false
"No"                   "405..."               0.445               false
```

**注意: `outcomes[0]` 不保证总是 "Yes" 或 "Home". 字面值取决于 market 类型. 必须用 index 对齐, 不能用字符串匹配.**

---

## §3 Polymarket 字段表 (完整)

### §3.1 Event/Game 顶层字段 (gamma /events)

| 字段 | 类型 | 含义 | 实测样本 |
|---|---|---|---|
| `id` | int | gamma 内部事件 ID | `27829` |
| `ticker` | string | slug 形式唯一标识 | `"2026-nhl-stanley-cup-champion"` |
| `slug` | string | URL slug | `"nhl-eastern-conf-finals-game-1"` |
| `title` | string | 人读标题 | `"Rangers vs Hurricanes Game 1"` |
| `startDate` | ISO8601 | 赛事开始时间 (UTC) | `"2026-05-28T23:00:00Z"` |
| `endDate` | ISO8601 | 事件 resolve 截止 | `"2026-06-01T00:00:00Z"` |
| `endDateIso` | ISO8601 | 市场关闭时间 (体育用这个, 不用 endDate) | `"2026-06-01T00:00:00Z"` |
| `active` | bool | 事件活跃 | `true` |
| `closed` | bool | 已结算 | `false` |
| `archived` | bool | 已归档 | `false` |
| `negRisk` | bool | 互斥结构 (outright / 系列赛, 多市场中只一个赢) | `false` (单场比赛), `true` (系列赛 champion) |
| `enableNegRisk` | bool | negRisk 开关 (gamma 视角) | 同上 |
| `markets` | `[]Market` | 该赛事下所有盘口 (嵌套) | 见 §3.2 |
| `liquidity` | float | 总流动性 USD | `12500.0` |
| `volume` | float | 总成交量 USD | `45000.0` |
| `volumeClob` | float | CLOB 渠道成交量 | `45000.0` |
| `volume24hr` | float | 24h 成交量 | `3200.0` |
| `liquidityClob` | float | CLOB 流动性 | `12500.0` |
| `tags` | `[]Tag` | 标签 (sport, league) | `[{id:"1",slug:"sports"}]` |
| `gameId` | string | Goalserve / 外部 game ID (SSR initialState 含) | `"1234567"` |
| `eventDate` | string | 赛事日期字符串 | `"2026-05-28"` |
| `seriesSlug` | string | 系列赛 slug | `"2026-nhl-playoffs"` |
| `automaticallyActive` | bool | 自动激活开关 | `true` |
| `negRiskAugmented` | bool | negRisk 增强模式 | `false` |
| `eventMetadata` | object | 扩展元数据 (球队名/联赛/赔率来源等) | `{homeTeam:"NYR",awayTeam:"CAR"}` |
| `resolvedTeams` | array | 结算后赢队列表 | `[]` (未结算时空) |

### §3.2 Market 字段 (condition_id 级, gamma 视角)

| 字段 | 类型 | 含义 | 实测样本 |
|---|---|---|---|
| `id` | int | gamma market ID (内部, 非链上) | `98765` |
| `conditionId` | bytes32 hex | 链上 CTF condition ID, **跨 gamma/clob 主键** | `"0xa9db6005902..."` |
| `questionID` | bytes32 hex | UMA oracle question ID | `"0x1b3c..."` |
| `slug` | string | market slug | `"rangers-win-game-1"` |
| `question` | string | 人读问题 | `"Will Rangers win Game 1?"` |
| `description` | string | 详细描述 | `"Rangers vs Hurricanes NHL..."` |
| `outcomes` | **stringified JSON** | outcome 名称数组 (需二次 parse) | `'["Yes","No"]'` 或 `'["Over 220.5","Under 220.5"]'` |
| `outcomePrices` | **stringified JSON** | 当前价格数组 (需二次 parse) | `'["0.555","0.445"]'` |
| `clobTokenIds` | **stringified JSON** | **token_id 数组 (需二次 parse), index 对齐 outcomes** | `'["794...","405..."]'` |
| `orderPriceMinTickSize` | float | tick size | `0.01` (高赔率冷门可能 `0.001`) |
| `orderMinSize` | float | 最小订单 USD size | `5.0` |
| `acceptingOrders` | bool | 当前是否接受挂单 | `true` (临场可能 false) |
| `acceptingOrdersTimestamp` | ISO8601 | 开始接受挂单的时间 | `"2026-05-01T00:00:00Z"` |
| `ready` | bool | 就绪可挂单 | `true` |
| `funded` | bool | 已铺底流动性 | `true` |
| `enableOrderBook` | bool | 走 CLOB (false = 老 fpmm, 不走 CLOB) | `true` |
| `negRisk` | bool | **负风险市场标志, 影响下单合约选择** | `false` |
| `negRiskMarketID` | bytes32 hex | negRisk 父市场 ID | `"0x0"` (普通市场为 null) |
| `negRiskRequestID` | bytes32 hex | negRisk 请求 ID | `"0x0"` |
| `makerBaseFee` | int | maker 基础费率 | `0` (体育 sports_fees_v2 下 maker=0) |
| `takerBaseFee` | int | taker 基础费率 | `1000` (bps, 但实际按 feeSchedule) |
| `feeType` | string | 费率类型 | `"sports_fees_v2"` |
| `feeSchedule` | object | 费率配置 | `{"exponent":1,"rate":0.03,"takerOnly":true,"rebateRate":0.25}` 含义: taker 收 3%, maker 0%, 已成交 maker 拿 25% 返佣 |
| `feesEnabled` | bool | 费率开关 | `true` |
| `spread` | float | 实时 best ask - best bid (gamma 缓存, 有秒级延迟) | `0.01` |
| `bestBid` / `bestAsk` | float | gamma 缓存最优价 (有延迟, 实时拿 clob /book) | `0.55` / `0.56` |
| `lastTradePrice` | float | 最近成交价 | `0.555` |
| `oneDayPriceChange` | float | 24h 价格变动 | `-0.02` |
| `volume` / `volumeClob` | float | 成交量 | 见 event 级 |
| `liquidityClob` | float | CLOB 流动性 | `4500.0` |
| `marketMakerAddress` | address | fpmm 模式才有值 | `"0x0"` |
| `groupItemTitle` | string | outright 子项标题 (e.g. 球队名) | `"New York Rangers"` |
| `groupItemThreshold` | string | handicap/total 阈值 | `"220.5"` |
| `rewardsMinSize` | float | 做市激励最小 size | `100.0` |
| `rewardsMaxSpread` | float | 做市激励最大 spread | `0.03` |
| `clobRewards` | array | 奖励配置 | `[{condition_id,tokens,rates}]` |
| `holdingRewardsEnabled` | bool | 持仓奖励 | `false` |
| `rfqEnabled` | bool | RFQ 开关 | `false` |
| `umaResolutionStatuses` | array | UMA oracle 状态机 | `[]` (未触发时空) |
| `endDate` / `endDateIso` | ISO8601 | 市场到期 (体育用 endDateIso) | `"2026-06-01T00:00:00Z"` |
| `startDate` / `startDateIso` | ISO8601 | 市场开始 | `"2026-05-28T00:00:00Z"` |
| `fpmm` | address | 非空 = 老 fpmm 模式, 禁用 CLOB 下单 | `""` (空) 或 `"0x123..."` |

### §3.3 Token/Outcome 字段 (token_id 级, clob /markets/{cid} 的 tokens[])

| 字段 | 类型 | 含义 | 实测样本 |
|---|---|---|---|
| `token_id` | uint256 string | **CLOB 一等公民标识, 用于所有 orderbook 操作** | `"1677202003548168512111076196662317438975560192301735320827449539424843146463"` |
| `outcome` | string | outcome 名称 | `"Yes"` / `"No"` / `"Over 220.5"` / `"Home Win"` |
| `price` | float | 当前市场价 (0~1) | `0.555` |
| `winner` | bool | 结算前 false, 结算后 true (仅赢方) | `false` |

### §3.4 Orderbook 字段 (GET /book?token_id= 或 POST /books)

GET /book 返回单条:

| 字段 | 类型 | 含义 | 实测样本 |
|---|---|---|---|
| `market` | bytes32 hex | 对应的 condition_id | `"0xa9db..."` |
| `asset_id` | uint256 string | 即 token_id (另一个名字) | `"1677..."` |
| `timestamp` | string | 服务端时间戳 (Unix 秒, string 形式) | `"1779941792"` |
| `hash` | string | orderbook 状态 hash (用于重连后状态校验) | `"abc123..."` |
| `bids` | `[{price,size}]` | 买方挂单 (降序, price 是 string, size 是 string) | `[{"price":"0.55","size":"100.0"}]` |
| `asks` | `[{price,size}]` | 卖方挂单 (升序) | `[{"price":"0.56","size":"80.0"}]` |

POST /books 额外字段 (比 GET /book 更全):

| 字段 | 类型 | 含义 |
|---|---|---|
| `min_order_size` | float | 该 token 最小订单 size |
| `tick_size` | float | tick size |
| `neg_risk` | bool | 是否 neg-risk market |
| `last_trade_price` | float | 最近成交价 |

**POST /books 是官方前端真实在用的端点** (实测 XHR 抓包, 见 `polymarket-sports-live-xhr.json` 第 5 条), 单 GET /book 是单 token 查询, 批量场景用 POST.

### §3.5 SignedOrder 字段 (EIP-712 下单, handshake v1 §3)

| 字段 | 类型 | 含义 | 工程 ABI lock |
|---|---|---|---|
| `condition_id` | bytes32 hex string | 市场标识 | 已锁 (handshake v1) |
| `token_id` | uint256 string | **outcome 标识 (CLOB 一等公民)** | 已锁 (handshake v1) |
| `side` | uint8 | `0=BUY, 1=SELL` (EIP-712 Order.side) | 已锁 |
| `limit_price_bps` | uint32 | 价格, 单位 bps (basis points), e.g. 5500 = 0.55 | 已锁 |
| `size_usdc_micro` | uint64 | USD size, 单位 micro (6 dec), e.g. 10_000_000 = 10 USDC | 已锁 |
| `expiration_unix_s` | uint64 | 过期时间 Unix 秒, 0 = GTC | 已锁 |
| `signature_type` | uint8 | `1` = Magic Safe 1-of-1 (我们的形态, 不是 0 不是 2) | 已锁, HMAC bug #2 |
| `signature` | string | EIP-712 ECDSA sig (65B r\|\|s\|\|v), base64 保留 padding | 已锁, HMAC bug #4 |
| `maker_address` | address hex | funder 地址 (资金账户) | 已锁 |
| `client_order_id` | string | 客户端自定义 ID (UUID 形式) | 已锁 |
| `ts` | TimestampQuad | R-20 四时间戳 | 已锁 |

**EIP-712 Order struct (链上, 对应 SignedOrder 的链上表示):**

| EIP-712 字段 | 类型 | 对应 SignedOrder 字段 |
|---|---|---|
| `salt` | uint256 | 运行时随机生成, 防重放 |
| `maker` | address | `maker_address` (funder) |
| `signer` | address | signer EOA (从 WALLET_PRIVATE_KEY 推出) |
| `taker` | address | `0x0` (任意 taker) |
| `tokenId` | uint256 | `token_id` (十进制 uint256) |
| `makerAmount` | uint256 | USDC (6 dec) 或 token amount |
| `takerAmount` | uint256 | 配对方 amount |
| `expiration` | uint256 | `expiration_unix_s` |
| `nonce` | uint256 | nonce |
| `feeRateBps` | uint256 | 体育市场 = 300 (3%) |
| `side` | uint8 | `0=BUY, 1=SELL` |
| `signatureType` | uint8 | `1` (我们的形态) |

**verifyingContract 选择 (negRisk 必须用不同合约):**
- 普通市场: `0x4bFb41d5B3570DeFd03C39a9A4D8dE6Bd8B8982E`
- negRisk 市场: `0xC5d563A36AE78145C45a50134d48A1215220f80a`

### §3.6 Position 字段 (data-api /positions, handshake v1 §3)

| 字段 | 类型 | 含义 | 工程 ABI lock |
|---|---|---|---|
| `condition_id` | bytes32 hex string | 市场标识 | 已锁 |
| `token_id` | uint256 string | **outcome 标识 (持仓到 token 级, 不是 market 级)** | 已锁 |
| `size_micro` | uint64 | 持仓量 (micro USD) | 已锁 |
| `avg_price_bps` | uint32 | 建仓均价 (bps) | 已锁 |
| `cur_price_bps` | uint32 | 当前市价 (bps) | 已锁 |
| `redeemable` | bool | 结算后可赎回 (true = 赢方, 自动赎回触发) | 已锁 |
| `mergeable` | bool | Yes+No 持仓相等可 burn 拿回 USDC (negRisk 场景) | 已锁 |
| `ts` | TimestampQuad | R-20 四时间戳 | 已锁 |

data-api /positions 原始字段对照:

| data-api 字段 | 对应工程字段 | 说明 |
|---|---|---|
| `asset` | `token_id` | uint256 string |
| `conditionId` | `condition_id` | bytes32 hex |
| `size` | `size_micro` / 1e6 | 原始是 float, 工程转 micro |
| `avgPrice` | `avg_price_bps` / 1e4 | 原始 0~1 float, 工程转 bps |
| `curPrice` | `cur_price_bps` / 1e4 | 同上 |
| `redeemable` | `redeemable` | 直接映射 |
| `mergeable` | `mergeable` | 直接映射 |
| `outcome` | 用 token_id 反查 | 可从 gamma market 的 clobTokenIds 索引反查 |
| `outcomeIndex` | 与 clobTokenIds[i] 对应 | outcomeIndex=999 是全市场 REDEEM, 特殊处理 |
| `proxyWallet` | funder 地址 (lowercase) | checksum 比较时 lowercase 后比 |

### §3.7 MarketInfo 字段 (工程层 F-05 GetMarketInfo, handshake v1 §3)

| 字段 | 类型 | 来源 | 含义 |
|---|---|---|---|
| `condition_id` | bytes32 hex string | clob /markets/{cid} | market 主键 |
| `outcomes` | `vector<string>` | gamma outcomes (parse stringified JSON) | outcome 名称列表 |
| `tick_size_bps` | uint32 | clob minimum_tick_size * 1e4 | tick 大小 |
| `neg_risk` | bool | clob neg_risk | negRisk 标志 |
| `fee_rate_bps` | uint32 | feeSchedule.rate * 1e4 | 体育 = 300 |
| `accepting_orders` | bool | clob accepting_orders | 当前是否接受挂单 |
| `game_start_time_unix_s` | int64 | clob game_start_time (ISO8601 → unix) | 比赛开球时间 (赛前冷冻判断) |
| `clob_token_ids` | `vector<string>` | gamma clobTokenIds (parse stringified JSON) | token_id 列表, index 对齐 outcomes |
| `liquidity_usdc` | float | gamma liquidityClob | 流动性 USD |
| `ts` | TimestampQuad | R-20 | 四时间戳 |

---

## §4 WSS Event 数据结构 (两个 endpoint)

### §4.1 公开 Market Channel (orderbook 推送)

| 项 | 值 |
|---|---|
| Endpoint | `wss://ws-subscriptions-clob.polymarket.com/ws/market` |
| 推送维度 | token_id (asset 级, outcome 级) |
| 订阅参数 | `assets_ids` (注意复数有 s) |

订阅 payload:
```json
{"type":"Market","assets_ids":["1677...","1060..."]}
```

**注意: 同一个 market (condition_id) 的两个 outcome 要分别列入 assets_ids, 各自独立推送.**

| event_type | 触发 | 关键字段 |
|---|---|---|
| `book` | 订阅时立即 snapshot + 偶尔重传 | `market`(condition_id), `asset_id`(token_id), `timestamp`, `hash`, `bids:[{price,size}]`, `asks:[{price,size}]`, `tick_size`, `last_trade_price` |
| `price_change` | 挂单/撤单/部分成交 | `market`(condition_id), `asset_id`(token_id), `price_changes:[{price,side,size}]`, `timestamp` |
| `last_trade_price` | 单笔完整成交 | `market`, `asset_id`, `price`, `size`, `side`, `timestamp` |
| `tick_size_change` | tick size 调整 (罕见) | `market`, `asset_id`, `tick_size` |

### §4.2 私有 User Channel (我的订单/成交推送)

| 项 | 值 |
|---|---|
| Endpoint | `wss://ws-subscriptions-clob.polymarket.com/ws/user` |
| 推送维度 | condition_id (market 级) |
| 订阅参数 | `markets` (注意: 值是 condition_id, 不是 token_id) |

订阅 payload (auth 写在 payload 内, 不在 HTTP header):
```json
{
  "type": "User",
  "auth": {
    "apiKey": "<POLY_API_KEY>",
    "secret": "<POLY_API_SECRET>",
    "passphrase": "<POLY_API_PASSPHRASE>"
  },
  "markets": ["0xa9db...", "0xdb39..."]
}
```

| event_type | 触发 | 关键字段 |
|---|---|---|
| `trade` | 我的订单被成交 | `order_id`, `market`(condition_id), `asset_id`(token_id), `price`, `size`, `side`, `fee`, `timestamp` |
| `order` | 订单状态变更 | `order_id`, `market`, `asset_id`, `status`(PLACED/MATCHED/CANCELED/EXPIRED), `timestamp` |

### §4.3 Sports WSS (体育 inplay 推送, 第 5 个 host)

| 项 | 值 |
|---|---|
| Endpoint | `wss://sports-api.polymarket.com/ws` |
| 推送维度 | condition_id (market 级) |
| 用途 | 体育 inplay 信息 (比分/状态/赔率概览) |

**注意: 这个 endpoint 是 endpoint-matrix v3 中 R-33 四维扫描发现的第 5 个 host, 早期版本漏扫.**

订阅 payload (已 endpoint-matrix v3 验证):
```json
{"type":"Market","assets_ids":["<condition_id>"]}
```

---

## §5 关键陷阱清单 (全员避坑)

### T-01 "market_id" 命名歧义 (最高频踩坑)

```
gamma 字段名        clob 字段名         实际含义
conditionId    =   condition_id    =  bytes32 hex, market 级
clobTokenIds[i] =  tokens[i].token_id = uint256 string, outcome 级

/prices-history?market=<token_id>  ← 参数名叫 market 但传的是 token_id !
/book?token_id=<token_id>          ← 正常命名
/books POST body: [{token_id:...}] ← 正常命名
user channel: markets:[condition_id] ← condition_id
market channel: assets_ids:[token_id] ← token_id
```

**原则: 不要用 "market_id" 命名变量. 用 condition_id 或 token_id, 两者语义截然不同.**

### T-02 单订一个 token 无法感知另一边变化

market channel 按 token_id 独立订阅. 如果只订 token_id="Yes", 完全感知不到 token_id="No" 的变化.
**做市必须同时订两个 token, 两个 orderbook 独立维护.**

### T-03 stringified JSON 三件套必须二次 parse

gamma Market 中以下三个字段是 **JSON string 内嵌 JSON array**, 不是 native array:
- `outcomes`: `'["Yes","No"]'` → parse 后得 `["Yes","No"]`
- `outcomePrices`: `'["0.555","0.445"]'` → parse 后得 `["0.555","0.445"]`
- `clobTokenIds`: `'["794...","405..."]'` → parse 后得 `["794...","405..."]`

**序列化层必须做两次 JSON parse. 直接当字符串存 = 数据层 bug.**

### T-04 negRisk 市场下单合约不同

`market.negRisk=true` 时 EIP-712 的 `verifyingContract` 必须换成 NegRisk 专用地址. 签错合约地址 = 下单被拒. **工程层必须把 negRisk 字段从 MarketInfo 传播到下单路径的合约选择逻辑.**

### T-05 token_id 是 uint256 string, 不是 condition_id

- condition_id: `"0xa9db6005..."` (0x 前缀, 66 char hex)
- token_id: `"1677202003548168..."` (无 0x 前缀, 十进制, 最多 77 位)

两者格式完全不同. 混用会导致 API 400 / 订单被拒. 代码中必须用类型系统区分 (ConditionId 类型 vs TokenId 类型).

### T-06 fpmm 非空的 market 禁止走 CLOB

`market.fpmm != ""` 或 `market.enableOrderBook=false` 时是老 Automated Market Maker 模式, CLOB 端点对这些 market 无效.
**过滤逻辑: 只处理 `enableOrderBook=true && fpmm==""` 的 market.**

### T-07 tick_size 不恒为 0.01

默认 0.01, 但高赔率冷门 market 可能 0.001. 每个 token 的 tick_size 从 `clob /tick-size?token_id=` 或 POST /books 返回的 `tick_size` 字段实时拿, 不可硬编码.

### T-08 acceptingOrders=false 不等于 market 关闭

临场冷冻窗口内 `acceptingOrders=false` 但 market 仍 active, 可以查价但不能下单. 体育市场常见在开球前 5-30 分钟冷冻, 策略层必须检测此字段.

### T-09 seconds_delay 是官方人为撮合延迟

`clob /markets/{cid}.seconds_delay` 非零时表示官方有意延迟撮合 (反 latency arb). 体育市场常见 1-3 秒. 策略 PnL 模型必须扣这部分成本.

### T-10 signature_type = 1, 不是 2

我们的 funder 是 Magic Safe 1-of-1 代理 (proxy wallet). `Order.signatureType` 必须填 `1`, 填 `2` 会导致 balance 查询返回 0 (假阳性零余额) + 下单鉴权失败. 这是 HMAC bug #2, 已在 v3 文档固化.

### T-11 HMAC base string 不含 querystring

L2 HMAC 签名的 base string = `ts + method + path_NO_QUERY + body`. querystring 只出现在 HTTP URL, 不进 base string. 含入 querystring 会导致 401. 这是 HMAC bug #1.

### T-12 outcomeIndex=999 特殊值

data-api /activity 中 `outcomeIndex=999` 表示"整个 market 赎回" (REDEEM 类型), 不是 index 999. 不要按 clobTokenIds[999] 解析.

### T-13 /prices-history 的 market 参数是 token_id

`GET /prices-history?market=<token_id>` 的参数名叫 `market` 但实际传的是 token_id (uint256 string). 传 condition_id 会报错或返回错数据.

### T-14 proxyWallet 地址 lowercase 比较

data-api 返回的 `proxyWallet` 字段是小写 hex 地址. 与 .env 的 funder 地址比较时必须先 lowercase 后比.

---

## §6 当前工程 vs Polymarket 真实结构 Gap

### §6.1 Gap 总览表

| 工程组件 | 工程当前字段/定义 | Polymarket 真实需求 | Gap 类型 | 负责人 |
|---|---|---|---|---|
| `OrderIntent` | `market_id` (condition_id 级) | 下单需要 condition_id + **token_id** 两个字段 | 缺 token_id 字段 | 老周 W8 W5 |
| `Side` enum | `{BuyYes, BuyNo}` | side = BUY/SELL, outcome 由 token_id 决定, 需 4 组合 (BuyYes/BuyNo/SellYes/SellNo) | Side 和 Outcome 概念耦合, 需解耦 | 老周 W8 W5 |
| `Outcome` 类型 | 不存在 | Polymarket outcome 是一等公民 (token_id 级), 每个 outcome 有独立 orderbook | 缺 Outcome/Token 类型 | 老周 W8 W5 |
| `OrderIntent.is_close` | 平仓不区分 outcome | 平 YES 仓 vs 平 NO 仓 = 操作不同的 token_id, 必须区分 | 平仓 token_id 缺失 | 老周 + 老韩 W8 W5 |
| `RiskManager` | per-market cap 按 condition_id | 实际风控应该支持 per-token cap (每个 outcome 独立 cap), 因为 Yes 仓和 No 仓是不同资产 | RM cap 粒度不够细 | 老韩 W8 W5 |
| `SignedOrder` | 已有 token_id (handshake v1 锁定) | 已正确 (ABI lock 保证) | 无 gap | N/A |
| `Position` | 已有 token_id (handshake v1 锁定) | 已正确 (ABI lock 保证) | 无 gap | N/A |

### §6.2 OrderIntent Gap 详解

当前 OrderIntent (推断结构, 待老周 W8 W5 审计输出):
```
OrderIntent {
    market_id,    // condition_id — 正确
    side,         // BuyYes/BuyNo — 问题: outcome 与 side 耦合
    size,         // 正确
    price,        // 正确
    is_close,     // 问题: 没有 outcome 标识
}
```

Polymarket 真实需求:
```
OrderIntent (修复后应有):
    condition_id,  // market 级 (现有, 正确)
    token_id,      // outcome 级 (新增, CLOB 下单必须)
    side,          // BUY 或 SELL (解耦 outcome)
    size,          //
    price,         //
    // is_close 改为: 用 side=SELL + token_id 自然表达平仓
```

**Side 解耦原理:**
- BUY token_id="794...(Yes)" = 开 Yes 仓 (原 BuyYes)
- BUY token_id="405...(No)" = 开 No 仓 (原 BuyNo)
- SELL token_id="794...(Yes)" = 平 Yes 仓 (原 is_close + BuyYes)
- SELL token_id="405...(No)" = 平 No 仓 (原 is_close + BuyNo)

用 token_id + side(BUY/SELL) 两个字段完全表达所有 4 种操作, 不需要 BuyYes/BuyNo enum.

### §6.3 RiskManager Gap 详解

当前 RM (推断) 按 per-market (condition_id) 做 cap:
```
R6.3 cap_per_market[condition_id] <= max_exposure
```

实际需要 per-token (token_id) cap:
```
cap_per_token[token_id] <= max_exposure_per_outcome
// Yes 仓和 No 仓是不同 CTF token, 分别 cap
// 否则 BuyYes 100 USDC + BuyNo 100 USDC 被当成 100 USDC 而不是 200 USDC
```

---

## §7 GM 错教训 (ADR 锁定, 老郭 W8 W5 出 ADR-026)

### §7.1 GM 错根因分析

**错 #N (W8 触发):** 工程层 OrderIntent 缺 token_id, 与 Polymarket 真实结构不符.

根因链:
1. 老李 spec v1 §88-91 **正确写明** `clobTokenIds` / `tokens` 字段
2. 工程层拿 spec 实现时**遗漏了 token 层** (只实现了 condition_id 级)
3. 没有 FOM (Field-of-Mention) 三方 cross-check 机制: 协议层 spec (老李) → 工程 ABI (老周) → Signer (老孙) 三层没有联合 review

### §7.2 ADR-026 锁定内容 (老郭 W8 W5 出 ADR)

| 规则 | 内容 |
|---|---|
| ADR-026 R1 | 核心数据结构 (OrderIntent / SignedOrder / Position / MarketInfo) 任何字段修改必须引用本文 (laoli-w8-polymarket-data-structure-ssot-v1.md) 对应章节 |
| ADR-026 R2 | 工程 ABI (OrderIntent 等) 修改必须三方 cross-check: 老李 (协议) + 老孙 (signer) + 老韩 (RM), 三方 ack 后 PR 才能合并 |
| ADR-026 R3 | 任何新 struct 加字段前必须先在本文 §3 找到对应 Polymarket 原始字段, 找不到 = 字段定义无依据 |
| ADR-026 R4 | condition_id 和 token_id 必须用类型系统区分 (C++ 强类型别名 / wrapper), 禁止 string 混用 |
| ADR-026 R5 | FOM 三方 cross-check: 工程 ABI 改动后, 老李出协议 ack, 老孙出 signer compat ack, 老韩出 RM cap ack, 三方缺一不合并 |

### §7.3 协议 owner 经验教训 (老李自检)

1. spec v1 写了 clobTokenIds 但没画关系图, 导致工程层理解偏差 → **本文 §2 关系图是补救**
2. v1/v2 都没有 gap 分析节 → **本文 §6 首次补齐**
3. ABI lock (handshake v1) 只锁定了 SignedOrder / Position, 没锁 OrderIntent → **ADR-026 扩展到 OrderIntent**

---

## §8 派单给 GM (下一步 backlog, W8 W5 截止)

| 派单目标 | 任务 | 截止 | 依据 |
|---|---|---|---|
| 老周 | OrderIntent ABI 修复: 加 token_id 字段 + Side 解耦 (BUY/SELL 独立于 outcome) + is_close 用 SELL side 表达 | W8 W5 | §6.2 |
| 老韩 | RM R6.3 cap 粒度升级: per-token (token_id 级) cap, 不只 per-market | W8 W5 | §6.3 |
| 老孙 | SignerV52 ABI 对齐: 确认 OrderIntent.token_id 传递链路 (OrderIntent → SignedOrder → EIP-712 Order.tokenId) | W8 W5 | §5 |
| 老郭 | 立 ADR-026: 数据结构 SSOT enforce + FOM 三方 cross-check 强约束 | W8 W5 | §7.2 |
| 老周 | condition_id / token_id 强类型: C++ 类型别名 ConditionId_t / TokenId_t, 禁 string 混用 | W8 W5 | §5 T-05 |

**优先级说明:** 老周 OrderIntent + 老韩 RM cap 是 P0 (影响下单正确性 + 风控有效性). 老郭 ADR-026 是 P1 (防止复发). 老孙 signer 对齐是 P1 (ABI 链路完整性).

---

## §9 不耻下问

| 问谁 | 问题 | 优先级 | 背景 |
|---|---|---|---|
| @小段 | Goalserve game_id 与 Polymarket event.gameId 对应关系 (本 wave 并行 worktree, 双源 market 结构对齐) | P0 | §2.1 Event 字段 gameId |
| @老周 | 工程 ABI gap audit (OrderIntent 现有字段完整列出, 对照本文 §6 确认修复范围) | P0 | §6 |
| @老韩 | OrderIntent owner W8 W5 修字段 + RM cap 粒度升级 (两个任务) | P0 | §6.3 |
| @老孙 | SignerV52 ABI 对齐 (OrderIntent.token_id → EIP-712 Order.tokenId 链路) | P1 | §5 + handshake v1 |
| @老郭 | ADR-026 立项 (SSOT enforce + FOM cross-check 强约束) | P1 | §7.2 |
| @老叶 | Exchange / NegRiskExchange verifyingContract 链上权威地址确认 (我这里是社区流传值) | P1 | §3.5 |
| @老雷 | GM final ack: OrderIntent 修复 + ADR-026 是否纳入 W8 sprint backlog | P0 | §8 |

---

## §10 文档索引 (本 SSOT 的上游一手文档)

| 文档 | 内容 | 关系 |
|---|---|---|
| `docs/RESEARCH/laoli-polymarket-api-spec-v1.md` | gamma/clob/data/WSS wire 契约 v1 | 本文 §3 字段表的一手来源 |
| `docs/RESEARCH/laoli-polymarket-endpoint-matrix-v3.md` | HMAC + 14 vector + 401 SOP | HMAC bug 陷阱 (§5 T-10/T-11 来源) |
| `docs/RESEARCH/laoli-laoSun-handshake-v1.md` | SignedOrder / Position ABI lock + 14 接口 | §3.5 SignedOrder + §3.6 Position 字段来源 |
| `docs/RESEARCH/laoli-polymarket-reverse-sports-live-v1.md` | XHR 抓包 + SSR 分析 + POST /books 发现 | §3.4 POST /books + §4.1 WSS market channel |
| `docs/RESEARCH/data/polymarket-sports-live-xhr.json` | 9 个 XHR 实测 payload | 真实 token_id 样本 + POST /books wire |
| `docs/RESEARCH/data/polymarket-initialState.json` | 1.6MB Redux store, 58 events | Event/Market/sections 结构一手来源 |
| `docs/RESEARCH/data/polymarket-sports-live-NEXT_DATA.json` | Next.js SSR dehydratedState | 补充 event 结构字段 |

---

**最后更新:** 2026-05-29 by 老李 (W8 Wave 40 P0)
