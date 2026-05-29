# gamma /events + CLOB WS 映射规范 v1

- **owner:** 老李 (polymarket-protocol-expert, #07)
- **last_review:** 2026-05-29
- **sprint:** W10 Wave (GM 直派, 供小冯实现 Event 实体 + sports_market_type schema + 真实 WSS)
- **受众:** 小冯 (#34, 主实施) / 老周 (#02, 架构 review) / 序列化工程师
- **一手数据来源:**
  - `gamma-api.polymarket.com/events?tag_id=1&closed=false` 实测 @ 2026-05-29
  - `clob.polymarket.com/book?token_id=<YES/NO>` 实测 @ 2026-05-29
  - `docs/RESEARCH/data/polymarket-initialState.json` (1.6MB Redux store, 58 events, 201 markets)
  - `docs/RESEARCH/laoli-w8-polymarket-data-structure-ssot-v1.md` (字段语义 SSOT)
  - `docs/RESEARCH/laoli-polymarket-market-structure-audit-v1.md` (一手 API 原始样本)
  - `docs/RESEARCH/laoli-w9-wss-subscriber-impl-spec-v1.md` (WSS 工程实施 Spec)
  - `docs/RESEARCH/xiaofeng-clob-orderbook-keying-check-v1.md` (底层 keying 证明)
  - `docs/ADR/2026-05-29-adr-040-market-structure-per-token.md` (结构契约修正)
- **相关文档:**
  - `docs/RESEARCH/laoli-polymarket-endpoint-matrix-v3.md` (连接拓扑 + HMAC SOP)
  - `docs/RESEARCH/laoli-polymarket-reverse-sports-live-v1.md` (sports/live SSR + sportsMarketType 字段发现)

---

## §0 文档目的

本文是小冯实现 Event/Market/Token 三层实体 + WS 消息处理的**唯一协议权威**。

覆盖：
1. gamma `/events` 三层结构 → 我们 schema 的字段映射表
2. `sportsMarketType` 枚举完整取值 + 语义
3. `groupItemTitle` 和 `line` 字段的用途
4. CLOB WS 四种消息处理规范 (book / price_change / last_trade_price / tick_size_change)
5. 4 时间戳契约 (R-20) 在 WS 消息中的映射
6. 订阅策略 (从 /events 取 token → 订阅)
7. negRisk 分组表达

不在本文范围：wire 实现 / JSON parse 代码（派给小冯/序列化工程师）。

---

## §1 三层结构总览

```
Event (赛事)                      gamma /events[i]
   id / slug / title / ticker
   negRisk / negRiskMarketID
   tags[] → sport/league 标签
   markets[] ─────────────────────────────────────────┐
                                                       │
   Market / Condition (盘口)      markets[j]           │
      conditionId (bytes32 hex)                        │
      questionID (bytes32 hex)                         │
      sportsMarketType ("moneyline" | "tennis_*" | ...)│
      outcomes (stringified JSON array)                │
      clobTokenIds (stringified JSON array)            │
      orderPriceMinTickSize / feeSchedule              │
      groupItemTitle / line                            │
      negRisk / negRiskMarketID                        │
      │                                               ◄┘
      ├─ Token/Outcome [0]         clobTokenIds[0]
      │     token_id (uint256 string)
      │     outcome  ("Yes" / "No" / "Over X" / ...)
      │     price    (float, 0~1)
      │     └─ Orderbook (per token_id) ← CLOB 一等公民
      │           bids:[{price,size}]
      │           asks:[{price,size}]
      │
      └─ Token/Outcome [1]         clobTokenIds[1]
            token_id (uint256 string)
            outcome  (互补边)
            price    (= 1 - price[0] 近似, 实际有 spread)
            └─ Orderbook (per token_id, 完全独立)
```

**关键约束 (ADR-040 锁定):**
- 订单簿粒度 = **token_id (outcome) 级**, 不是 condition_id 级
- 1 condition = 恒 2 个 token (Polymarket 二元市场全平台统一)
- YES token price + NO token price ≈ 1.00 (互补镜像, 但各有独立深度/序列号/hash)

---

## §2 Event / Market / Token 三层字段映射表

### §2.1 Event 层 (gamma /events 顶层字段)

| gamma 字段 | 类型 | 建议 EventInfo 字段 | 语义说明 |
|---|---|---|---|
| `id` | int | `gamma_event_id` | gamma 内部 ID, **不是链上标识**, 仅用于引用 gamma /events/{id} |
| `slug` | string | `event_slug` | URL slug, e.g. `"nhl-eastern-conf-finals-game-1"` |
| `ticker` | string | `event_ticker` | slug 变体, 有时等同 slug, 有时含系列赛前缀 |
| `title` | string | `event_title` | 人读标题, e.g. `"Rangers vs Hurricanes Game 1"` |
| `startDate` | ISO8601 string | `event_start_ts_utc` | 赛事开始时间 (UTC), 转 int64 unix_s 存储 |
| `endDate` | ISO8601 string | `event_end_ts_utc` | 名义结束/resolve deadline, 体育**优先用 endDateIso** |
| `endDateIso` | ISO8601 string | `market_close_ts_utc` | **体育市场实际关闭时间** (与 endDate 可能不同) |
| `active` | bool | `is_active` | 事件是否活跃 |
| `closed` | bool | `is_closed` | 是否已结算 |
| `archived` | bool | `is_archived` | 是否归档 |
| `negRisk` | bool | `is_neg_risk` | true = outright/系列赛互斥结构 (多 market 中只一个赢) |
| `enableNegRisk` | bool | `neg_risk_enabled` | gamma 视角 negRisk 开关 (语义同 negRisk) |
| `negRiskMarketID` | bytes32 hex | `neg_risk_market_id` | negRisk 父市场 ID (非 negRisk 时为空/""); 见 §6 |
| `markets` | `[]Market` | `markets` | 子盘口列表, 嵌套展开 |
| `tags` | `[]Tag` | `sport_tags` | 含 sport slug/id, 用于过滤体育事件 |
| `eventMetadata` | object | `metadata_raw` | 扩展元数据 (homeTeam/awayTeam 等), 存 JSON 字符串 |
| `gameId` | string | `external_game_id` | Goalserve / 外部 game ID (SSR initialState 含), 双源对齐用 |
| `seriesSlug` | string | `series_slug` | 系列赛 slug (非系列赛为空) |
| `liquidity` | float | (只读统计) | 总流动性 USD, 非决策字段 |
| `volume24hr` | float | (只读统计) | 24h 成交量 |

**注意:** `gameId` 在 gamma REST 响应中可能不直接出现; 在 SSR initialState 的 `games{}` 层有对应。双源对齐 (Goalserve ↔ Polymarket) 时通过 `event_slug` 模糊匹配或 `eventMetadata` 中的 team 名。

---

### §2.2 Market 层 (gamma event.markets[j], condition_id 级)

| gamma 字段 | 类型 | 建议 MarketInfo 字段 | 语义说明 |
|---|---|---|---|
| `conditionId` | bytes32 hex (0x 前缀, 66 字符) | `condition_id` | **跨 gamma/clob 主键**, 链上 CTF condition |
| `questionID` | bytes32 hex | `question_id` | UMA oracle question ID |
| `question` | string | `question_text` | 人读问题 |
| `sportsMarketType` | string | `sports_market_type` | 盘口类型枚举 (见 §3) |
| `outcomes` | **stringified JSON** | `outcome_labels: []string` | **必须二次 parse**, e.g. `'["Yes","No"]'` → `["Yes","No"]` |
| `outcomePrices` | **stringified JSON** | (只读, 有延迟) | gamma 缓存价格, 实时价从 WS/clob 拿 |
| `clobTokenIds` | **stringified JSON** | `token_ids: []string` | **必须二次 parse**, index 与 outcomes 严格对齐 |
| `orderPriceMinTickSize` | float | `tick_size` | 最小报价步长, 典型值 0.01; 高赔率冷门可能 0.001 |
| `orderMinSize` | float | `min_order_size_usdc` | 最小订单 USD size, 典型值 5.0 |
| `acceptingOrders` | bool | `is_accepting_orders` | 当前是否接受挂单 (临场可能 false) |
| `ready` | bool | `is_ready` | 就绪可挂单 |
| `enableOrderBook` | bool | `is_clob_enabled` | false = 老 fpmm 模式, **禁止 CLOB 下单** |
| `negRisk` | bool | `is_neg_risk` | **影响下单合约地址选择** (普通 vs negRisk verifyingContract) |
| `negRiskMarketID` | bytes32 hex | `neg_risk_market_id` | negRisk 父市场 ID (见 §6) |
| `feeSchedule` | object | (拆开存) | `{rate, rebateRate, takerOnly, exponent}` |
| `feeSchedule.rate` | float | `fee_rate` | taker 费率, 体育当前 0.03 (3%) |
| `feeSchedule.rebateRate` | float | `maker_rebate_rate` | maker 返佣率, 体育当前 0.25 (25%) |
| `feeSchedule.takerOnly` | bool | `is_taker_only_fee` | true = maker 0 费, taker 收费 |
| `groupItemTitle` | string | `group_item_title` | 见 §4, 用于 outright/系列赛子项标识 |
| `line` | float | `handicap_line` | Totals/Spread 的数值线, e.g. 220.5 / -1.5; 见 §4 |
| `bestBid` / `bestAsk` | float | (只读, 有延迟) | gamma 缓存, **实时报价用 CLOB WS** |
| `lastTradePrice` | float | `last_trade_price_cached` | gamma 缓存最近成交价 |
| `spread` | float | (只读统计) | gamma 缓存 spread |
| `makerBaseFee` | int | (参考) | bps 形式, 体育下通常为 0 (见 feeSchedule) |
| `takerBaseFee` | int | (参考) | bps 形式, 实际按 feeSchedule.rate |

**stringified JSON 坑:** `outcomes` / `outcomePrices` / `clobTokenIds` 三个字段在 gamma REST 返回时是 **字符串**, 内容是 JSON 数组, 需要二次 parse。clob `/markets/{cid}` 的 `tokens[]` 是原生数组无此问题。

**outcomeIndex 对齐规则:**
```
outcomes[i]  ↔  outcomePrices[i]  ↔  clobTokenIds[i]
"Yes"            "0.555"              "794..." (YES token_id)
"No"             "0.445"              "405..." (NO token_id)
```
`outcomes[0]` 不保证总是 "Yes" 或主队。必须用 index 对齐，不能用字符串匹配。

---

### §2.3 Token/Outcome 层 (token_id 级, CLOB 一等公民)

| 来源字段 | 类型 | 建议 TokenInfo 字段 | 语义说明 |
|---|---|---|---|
| `clobTokenIds[i]` (gamma) 或 `tokens[i].token_id` (clob) | uint256 string (无 0x 前缀, 十进制, ≤77 位) | `token_id` | **CLOB 所有操作的主键**: 订阅 WS / 查 orderbook / 挂单 |
| `outcomes[i]` (gamma) 或 `tokens[i].outcome` (clob) | string | `outcome_label` | outcome 名称, e.g. "Yes" / "No" / "Over 220.5" |
| `outcomePrices[i]` (gamma, 缓存) 或 `tokens[i].price` (clob) | float | `price_cached` | 当前市价 (0~1), 有延迟, 实时价从 WS |
| `tokens[i].winner` (clob 专有) | bool | `is_winner` | 结算前 false; 结算后赢方 token = true |
| (CLOB book 响应) `asset_id` | uint256 string | `token_id` (同一字段) | `asset_id` 是 token_id 的别名 (WS 和 REST /book 均用 asset_id) |

**condition_id vs token_id 对照:**

| 维度 | condition_id | token_id (= asset_id) |
|---|---|---|
| 类型 | bytes32 hex, "0x" 前缀, 66 字符 | uint256 十进制 string, 无 "0x", ≤77 位 |
| 粒度 | 盘口 (Market) 级 | 单边 (Outcome) 级 |
| 1:N | 1 condition_id → 2 token_id | 1 token_id → 1 独立 orderbook |
| 用于下单 | EIP-712 Order 需要两者 (condition_id 在 user channel 订阅; token_id 在 Order.tokenId) | `tokenId` 字段 |
| CLOB WS 订阅 | user channel `markets[]` 用 condition_id | market channel `assets_ids[]` 用 token_id |
| REST /book 查询 | 不接受 condition_id | `GET /book?token_id=<token_id>` |

---

## §3 sportsMarketType 枚举完整定义

### §3.1 实测枚举值 (来自 initialState.json 58 个 event, 201 个 market)

| sportsMarketType 值 | 出现次数 | 分类 | 语义 |
|---|---|---|---|
| `"moneyline"` | 58 | **通用** | 胜负盘 (Who wins), 适用所有运动 |
| `"tennis_completed_match"` | 55 | 网球专属 | 完赛确认 (Did the match complete?) |
| `"tennis_match_totals"` | 33 | 网球专属 | 全场总盘 (Match Total Games O/U X) |
| `"tennis_first_set_totals"` | 33 | 网球专属 | 第一盘总盘 (First Set O/U X) |
| `"tennis_set_totals"` | 14 | 网球专属 | 特定盘总盘 (Set N O/U X), 配合 `line` 字段 |
| `"tennis_first_set_winner"` | 11 | 网球专属 | 第一盘赢家 (Who wins Set 1) |
| `"tennis_set_handicap"` | 7 | 网球专属 | 盘让分 (Set Handicap), `line` = -1.5 / +1.5 |

**注意: 此快照为 2026-05-28 ATP/WTA/ITF/KBO 赛程。NBA/NFL/NHL/MLB 赛季期间会出现更多枚举值。**

### §3.2 补充枚举值 (预期但此快照未出现, 来自 marketsSections 结构反推)

SSR `marketsSections` 数据揭示前端支持的盘口分类键：

```
moneyline          — 胜负盘 (已实测)
spreads            — 让分 (Spread/Handicap)
totals             — 总盘 (Over/Under)
firstHalfSpreads   — 上半场让分
firstHalfTotals    — 上半场总盘
firstInningsTotals — 首局总盘 (棒球)
secondInningsTotals — 第二局总盘 (棒球)
firstSetTotals     — 第一盘总盘 (网球, 已实测)
setTotals          — 盘总盘 (网球, 已实测)
matchTotals        — 全场总盘 (网球, 已实测)
setHandicap        — 盘让分 (网球, 已实测)
childMoneyline     — 子赛事 moneyline (系列赛子场)
nrfi               — No Run First Inning (棒球 prop)
btts               — Both Teams to Score (足球 prop)
```

对应的 `sportsMarketType` 映射推测 (未全量实测, 标注 [推测]):

| 预期 sportsMarketType | 对应盘口 | 有 line 字段 | 备注 |
|---|---|---|---|
| `"moneyline"` | 胜负盘 | 否 | 已实测 |
| `"spread"` | 让分 | 是 | [推测], line = -3.5 等 |
| `"totals"` | 总盘 O/U | 是 | [推测], line = 220.5 等 |
| `"first_half_moneyline"` | 上半场胜负 | 否 | [推测] |
| `"first_half_spread"` | 上半场让分 | 是 | [推测] |
| `"first_half_totals"` | 上半场总盘 | 是 | [推测] |
| `"nrfi"` | 首局不得分 | 否 | [推测], 棒球 prop |
| `"btts"` | 双方均入球 | 否 | [推测], 足球 prop |
| `"series_winner"` | 系列赛冠军 | 否 | [推测], negRisk=true |
| `"tennis_completed_match"` | 完赛确认 | 否 | 已实测 |
| `"tennis_match_totals"` | 网球全场总盘 | 是 | 已实测 |
| `"tennis_first_set_totals"` | 网球第一盘总盘 | 是 | 已实测 |
| `"tennis_set_totals"` | 网球分盘总盘 | 是 | 已实测 |
| `"tennis_first_set_winner"` | 网球第一盘赢家 | 否 | 已实测 |
| `"tennis_set_handicap"` | 网球盘让分 | 是 | 已实测 |

### §3.3 negRisk Outright 的归类

**negRisk=true 的 market (系列赛冠军/outright) 无专属 sportsMarketType 字段值。**

实测 NHL Stanley Cup Champion event (negRisk=true):
- 每个子 market (每支球队) 的 `sportsMarketType` = `"moneyline"`
- 区分 outright 组的唯一方式是 `negRisk=true` + `negRiskMarketID` 非空
- `groupItemTitle` = 球队名 (如 `"Carolina Hurricanes"`)

因此: **outright/系列赛不是靠 sportsMarketType 识别, 而是靠 `negRisk=true` + `negRiskMarketID` 识别**。实现侧建议单独标注 `is_outright = (negRisk && negRiskMarketID != "")` 逻辑字段。

### §3.4 schema 建议

C++ 枚举定义建议 (精确已实测 + 预留扩展):

```cpp
enum class SportsMarketType : uint8_t {
    kUnknown               = 0,
    // 通用 (跨运动)
    kMoneyline             = 1,
    kSpread                = 2,   // [推测]
    kTotals                = 3,   // [推测]
    kFirstHalfMoneyline    = 4,   // [推测]
    kFirstHalfSpread       = 5,   // [推测]
    kFirstHalfTotals       = 6,   // [推测]
    kFirstInningsTotals    = 7,   // [推测], 棒球
    kSecondInningsTotals   = 8,   // [推测], 棒球
    kNrfi                  = 9,   // [推测], 棒球 prop
    kBtts                  = 10,  // [推测], 足球 prop
    // 网球专属 (已实测)
    kTennisCompletedMatch  = 20,
    kTennisMatchTotals     = 21,
    kTennisFirstSetTotals  = 22,
    kTennisSetTotals       = 23,
    kTennisFirstSetWinner  = 24,
    kTennisSetHandicap     = 25,
};
```

**parse 规则:** gamma 返回字符串 → 映射到枚举; 未知值 → `kUnknown` (不报错, 打 WARN 日志, 继续处理)。新盘口类型上线时扩展枚举不影响已有逻辑。

---

## §4 groupItemTitle 和 line 字段用途

### §4.1 groupItemTitle

| 场景 | groupItemTitle 含义 | 实测样本 |
|---|---|---|
| Outright / 系列赛 (negRisk=true) | 该子市场代表的球队名 | `"Carolina Hurricanes"` / `"New York Rangers"` |
| 网球分盘总盘 (tennis_set_totals) | 完整盘口描述 | `"Centurion: Yusuke Takahashi vs Edward Winter Total Sets: O/U 2.5"` |
| 网球完赛 (tennis_completed_match) | 固定值 | `"Completed Match"` |
| 普通 moneyline | 通常为空 | `""` |

**用法:** 在 outright event 中, `groupItemTitle` 是唯一标识各子市场对应球队的字段。渲染看板时用于显示球队名标签。数据库层应作为 `MarketInfo.group_item_label` 存储。

### §4.2 line (handicap/total 数值线)

**仅 Spread 类和 Totals 类市场有意义的 `line` 字段 (float):**

| sportsMarketType | line 含义 | 实测样本 |
|---|---|---|
| `tennis_set_totals` | 总盘线 (总盘数 O/U) | `2.5` |
| `tennis_first_set_totals` | 第一盘总盘线 (games O/U) | `8.5` / `9.5` / `10.5` |
| `tennis_match_totals` | 全场总盘线 (games O/U) | `21.5` / `22.5` / `23.5` |
| `tennis_set_handicap` | 盘让分线 | `-1.5` (负值 = 强队让盘) |
| `spread` (推测) | 分差让分线 | `-3.5` 等 |
| `totals` (推测) | 总分线 | `220.5` 等 |

**注意:** 同一 event 可能有多个 Totals market (不同 line 值), 如 `O/U 21.5` / `O/U 22.5` / `O/U 23.5` 三档。`line` 字段是区分同类型多档盘口的关键，应存入 `MarketInfo.handicap_line`。

moneyline / tennis_completed_match / tennis_first_set_winner 无 `line` 字段 (或值为 0)。

---

## §5 CLOB WS 消息处理规范

### §5.1 endpoint 与订阅 payload

**Endpoint:** `wss://ws-subscriptions-clob.polymarket.com/ws/market`

**订阅 payload:**
```json
{
  "type": "Market",
  "assets_ids": ["<token_id_0>", "<token_id_1>", "..."]
}
```

**坑清单:**
- `type` 值必须大写 `"Market"`, 小写 `"market"` 服务端不识别
- 字段名是 `assets_ids` (有复数 s), 不是 `asset_ids`
- 值传 **token_id** (uint256 decimal string), 不是 condition_id
- 同一 condition 的两个 token **必须同时订** (双 token 同订规则), 单订一边无法感知另一边变动

---

### §5.2 消息类型: book (全量快照)

**触发时机:** 订阅时服务端立即推送; 连接不稳定时可能重传。

**消息结构:**
```json
{
  "event_type": "book",
  "market":     "<condition_id>",
  "asset_id":   "<token_id>",
  "timestamp":  "1748390400000",
  "hash":       "92360c958b867708e90e38426583af72917a4f54",
  "tick_size":  "0.01",
  "last_trade_price": "0.55",
  "bids": [
    {"price": "0.55", "size": "44334.82"},
    {"price": "0.54", "size": "35017.7"}
  ],
  "asks": [
    {"price": "0.56", "size": "9554.48"},
    {"price": "0.57", "size": "77596.92"}
  ]
}
```

**字段语义:**

| 字段 | 类型 | 语义 | 处理规则 |
|---|---|---|---|
| `event_type` | string | 固定 `"book"` | dispatch 分支 |
| `market` | string | condition_id (0x hex) | 反查 condition → market 元数据 |
| `asset_id` | string | token_id (uint256 decimal) | **orderbook 路由主键** |
| `timestamp` | string | Unix **毫秒** (int64 形式) | 转 int64; × 1e6 得 ns; 映射 R-20 `data_source_ts` |
| `hash` | string | orderbook 状态 SHA1 hash | 完整性校验 (非 seq); 重连后对比判断是否需全量 diff |
| `tick_size` | string | 当前 tick size | 更新本地 token 的 tick_size 缓存 |
| `last_trade_price` | string | 最近成交价 | 更新本地 last_trade_price 缓存 |
| `bids` | `[]{"price":str, "size":str}` | 买方挂单 (降序) | 全量替换, price/size 均为 string → double |
| `asks` | `[]{"price":str, "size":str}` | 卖方挂单 (升序) | 全量替换 |

**处理规则:**
1. `book` = **全量快照**, 完整替换该 token_id 的本地 orderbook 状态
2. 收到 `book` 后才能开始接受该 token 的 `price_change` 增量 (设 `snapshot_received[token_id] = true`)
3. `bids` 按 price 降序, `asks` 按 price 升序 (服务端已排序, 本地直接存)
4. `price` / `size` 均为字符串 decimal → 用 `std::from_chars` 转 double (不用 atof/stof, 精度陷阱)

---

### §5.3 消息类型: price_change (增量)

**触发时机:** 任何挂单 / 撤单 / 部分成交导致 depth 变化时。

**消息结构:**
```json
{
  "event_type": "price_change",
  "market":     "<condition_id>",
  "asset_id":   "<token_id>",
  "timestamp":  "1748390401234",
  "price_changes": [
    {"price": "0.55", "side": "BUY",  "size": "50000.0"},
    {"price": "0.56", "side": "SELL", "size": "0"}
  ]
}
```

**字段语义:**

| 字段 | 类型 | 语义 |
|---|---|---|
| `event_type` | string | `"price_change"` |
| `market` | string | condition_id |
| `asset_id` | string | token_id (路由主键) |
| `timestamp` | string | Unix 毫秒 (同 book) |
| `price_changes` | array | 增量变化列表 |
| `price_changes[i].price` | string | 受影响的价格档位 |
| `price_changes[i].side` | string | `"BUY"` (bid 侧) 或 `"SELL"` (ask 侧) |
| `price_changes[i].size` | string | **该档位新的总 size** (0 = 该档位已消失) |

**增量 apply 规则 (apply to local L2 book):**
1. 前提: `snapshot_received[token_id] == true` (否则丢弃并触发重订)
2. 对每个 `price_changes[i]`:
   - `side == "BUY"`: 更新 bids[price] = size; 若 size == 0 则删除该价位
   - `side == "SELL"`: 更新 asks[price] = size; 若 size == 0 则删除该价位
3. 更新 `last_ts[token_id] = timestamp`
4. 检查 sequence_no (见 §5.6)

**size=0 语义:** 该价格档位的所有挂单已被撤/成交, 从 L2 book 中移除该档位。

---

### §5.4 消息类型: last_trade_price (成交事件)

**触发时机:** 单笔完整成交 (taker 完全吃掉 maker 挂单)。

**消息结构:**
```json
{
  "event_type": "last_trade_price",
  "market":     "<condition_id>",
  "asset_id":   "<token_id>",
  "price":      "0.555",
  "size":        "100.0",
  "side":        "BUY",
  "timestamp":  "1748390402000"
}
```

**处理规则:**
- 更新本地 `last_trade_price[token_id]`
- 不触发 L2 book 状态变更 (book 状态靠 price_change 维护)
- 可用于成交价信号 / 流动性监测

---

### §5.5 消息类型: tick_size_change (罕见)

**触发时机:** Polymarket 动态调整 tick size (非常罕见, 市场流动性大幅变化时)。

**消息结构:**
```json
{
  "event_type":  "tick_size_change",
  "market":      "<condition_id>",
  "asset_id":    "<token_id>",
  "tick_size":   "0.001",
  "timestamp":   "1748390403000"
}
```

**处理规则:**
- 更新本地 `tick_size[token_id]`
- 同时更新对应 `condition_id` 的 `MarketInfo.tick_size` 缓存
- tick_size 变化可能影响挂单策略; 需通知上层策略层

---

### §5.6 R-20 四时间戳映射 (WS 消息)

R-20 要求所有数据源带四维时间戳。CLOB WS 消息映射规则：

| R-20 字段 | 映射来源 | 说明 |
|---|---|---|
| `event_ts` | N/A (赛事本身时间, 非订单簿消息时间) | 对 orderbook 消息无意义, 设为 0 或不填 |
| `data_source_ts` | WS 消息 `timestamp` 字段 × 1e6 → ns | **Polymarket 服务端时间**, 必须用此值, 禁用本地 now() |
| `ingestion_ts` | `steady_clock::now()` 在 WS 回调中读取 | 帧到达应用层的时刻, recv 时立即记录 |
| `as_of_ts` | `steady_clock::now()` 在数据被消费时读取 | 策略/风控读取此数据的时刻 |

**约束 (R-20 红线):** `event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts`

`data_source_ts` 必须转为纳秒 (ns): `data_source_ts_ns = stoull(timestamp_str) * 1'000'000LL`

---

### §5.7 hash 字段用途

`hash` 字段是 Polymarket 服务端对当前 orderbook 状态的 SHA1 摘要。

**用途:**
1. **重连状态校验:** 重连后收到新 `book` snapshot, 对比 hash 是否与重连前一致。一致 = orderbook 无变化; 不一致 = 有漏帧, 需全量重建
2. **完整性校验 (非实时):** 可定期用 REST `/book?token_id=` 拉一次对比 hash, 检测本地状态是否发散
3. **不作为序列号使用:** hash 不单调, 不用于 gap 检测

---

### §5.8 gap / sequence_no 维护

**sequence_no 字段:** market channel 的 `book` / `price_change` 消息中含可选 `sequence_no` (uint64)。该字段不保证每帧都出现。

**gap 检测规则:**
```
per token_id 维护: last_seq[token_id]
收到帧时:
  if seq_no > 0 && seq_no != last_seq[token_id] + 1:
    // gap 检测到 → 该 token orderbook 状态不一致
    snapshot_received[token_id] = false
    触发: UNSUBSCRIBE(token_id) + SUBSCRIBE(token_id) 重新订阅
  else:
    last_seq[token_id] = seq_no
```

**重连 vs 重订阅 (gap):**
- **重连** (TCP 断): 整条连接重建, 所有 token 状态清空, 重发 subscribe payload, 等全量 book snapshot
- **重订阅** (seq gap): 不断连接, 仅对该 token UNSUBSCRIBE + SUBSCRIBE, 拿新 snapshot

**接收 `price_change` 的前提:** `snapshot_received[token_id] == true`; 否则丢弃该帧 (不 apply 到 L2 book)。

---

## §6 订阅策略 (从 /events 取 token → 订阅 WS)

### §6.1 完整流程

```
Step 1: GET gamma-api.polymarket.com/events
        ?tag_id=1          (tag_id=1 = "Sports" 标签)
        &closed=false
        &active=true
        &limit=100
        &order=startDate
        &ascending=true

Step 2: 过滤活跃体育 event
        filter: event.active == true
             && event.closed == false
             && event.archived == false
        可选: 按 startDate 限制范围 (如未来 48h)

Step 3: 对每个 event.markets[j]:
        filter: market.acceptingOrders == true
             && market.enableOrderBook == true   (clob_enabled, 非老 fpmm)
        parse: token_ids = JSON.parse(market.clobTokenIds)  // 二次 parse!

Step 4: 收集所有 token_id
        all_token_ids = [token for each qualifying market's clobTokenIds]
        (每个 market 贡献 2 个 token_id)

Step 5: 分片订阅 (容量限制)
        hot_conn  ≤ 500 tokens  (高流动性 event)
        cold_conn ≤ 3500 tokens (其余)
        见 endpoint-matrix v3 §C 拓扑

Step 6: 发送 subscribe payload
        {"type":"Market","assets_ids":[... token_ids ...]}
```

### §6.2 速率与 ToS

- gamma `/events` 不需鉴权, 但属公开 API, 遵守速率限制
- 建议轮询间隔: 30s~60s (拉新事件 / 检测盘口状态变化)
- WS market channel 公开免鉴权, 纯只读 orderbook, 不影响市场
- 不执行批量 REST book 爬取 (拿 orderbook 只通过 WS)

### §6.3 market 级过滤建议

```
拒绝订阅:
  market.enableOrderBook == false   → 老 fpmm 模式, CLOB 无效
  market.acceptingOrders == false   → 临场冻结, 无挂单活动
  market.active == false            → 市场暂停

可选过滤:
  market.sportsMarketType == kUnknown → 未识别盘口类型, 观望
  market.feeSchedule 不存在          → 无费率信息, 谨慎
```

---

## §7 negRisk 分组表达

### §7.1 negRisk 结构含义

**negRisk = true** 表示同一 event 下的多个 market 是**互斥关系** (只有一个 market 结算为 Yes)。
典型场景: 联盟冠军 outright (32 支球队, 只有 1 支夺冠)。

**为何称 negRisk:** 在互斥市场中持有多个头寸时, 其中一个赢 → 其余自动输 → 各个 position 的 P&L 是负相关的 → "negative risk"。

### §7.2 字段表达

```
Event (negRisk=true, e.g. "2026 NHL Stanley Cup Champion")
  negRiskMarketID: "0x7faa974ff857682d64433d5c4dfba46ff51415a68cbd5bd1994248df2d561200"
  markets: [
    { conditionId: "0xf7b5...",  negRisk: true,  negRiskMarketID: "0x7faa...",
      groupItemTitle: "Carolina Hurricanes",
      sportsMarketType: "moneyline",
      clobTokenIds: ["79397...", "40473..."]
    },
    { conditionId: "0xabc1...",  negRisk: true,  negRiskMarketID: "0x7faa...",
      groupItemTitle: "New York Rangers",
      sportsMarketType: "moneyline",
      clobTokenIds: ["88821...", "11209..."]
    },
    ... (每支球队一个 market)
  ]
```

**分组规则:** `negRiskMarketID` 相同的所有 market 属于同一个互斥组。

### §7.3 negRiskMarketID 的工程表达

建议在 `MarketInfo` 中增加字段:

| 字段 | 类型 | 含义 |
|---|---|---|
| `is_neg_risk` | bool | 直接来自 market.negRisk |
| `neg_risk_group_id` | bytes32 hex string | 来自 market.negRiskMarketID; 空 = 非互斥 |
| `is_outright` | bool | 派生: `is_neg_risk && !neg_risk_group_id.empty()` |

**group 级聚合逻辑:**
```
by neg_risk_group_id 分组 → 得到同一 outright event 下所有球队的 market
每支球队对应:
  - 1 个 condition_id
  - 1 个 groupItemTitle (球队名)
  - 2 个 token_id (Yes/No)
  - 1 个 YES token orderbook
  - 1 个 NO token orderbook

注意: YES token 代表"该球队夺冠"; NO token 代表"该球队不夺冠"
```

### §7.4 negRisk 下单合约选择 (P0)

**negRisk market 必须用不同的 EIP-712 verifyingContract:**

| 市场类型 | verifyingContract |
|---|---|
| 普通市场 (negRisk=false) | `0x4bFb41d5B3570DeFd03C39a9A4D8dE6Bd8B8982E` |
| negRisk 市场 (negRisk=true) | `0xC5d563A36AE78145C45a50134d48A1215220f80a` |

下单时必须检查 `market.is_neg_risk` 并选择正确合约地址。选错 = 签名无效, 订单被拒。

---

## §8 已知坑汇总

| # | 坑描述 | 正确处理 |
|---|---|---|
| P-01 | `type` 字段大小写: WS subscribe payload 必须 `"Market"` 大写 | 硬编码字面量, 不用变量转换 |
| P-02 | `assets_ids` 复数 s: 字段名是 `assets_ids` 不是 `asset_ids` | 硬编码字段名 |
| P-03 | `outcomes` / `clobTokenIds` 是 stringified JSON: 需二次 parse | 先 JSON parse 整个 market, 再对这两个字段做第二次 JSON parse |
| P-04 | price/size 是字符串: WS 消息中 price/size 值是 string, 不是 number | `std::from_chars` 转 double (不用 atof/stof) |
| P-05 | timestamp 是 Unix 毫秒: `"1748390400000"` 单位是毫秒 string; ×1e6 得 ns | 不要当秒处理 (会差 1000 倍) |
| P-06 | price_change size=0 语义: 该档位被移除, 不是 size 为零的挂单 | size="0" → 从 L2 book 删除该价位 |
| P-07 | 单边订阅不感知对侧: 只订 YES token 看不到 NO token 的 book 变动 | 每个 condition 的两个 token 必须同时列入 assets_ids |
| P-08 | 无 snapshot 直接 apply price_change: 初始状态未知, diff 无意义 | 等 book snapshot 到达后 (snapshot_received=true) 才 apply 增量 |
| P-09 | negRisk market 用错合约地址: 普通合约地址签 negRisk 订单 → 无效 | 下单前检查 is_neg_risk, 选对 verifyingContract |
| P-10 | outcomeIndex 不稳定: outcomes[0] 不保证是 "Yes" | 用 index 对齐, 不用字符串匹配 |
| P-11 | hash 当 seq_no 用: hash 不单调, 不能做 gap 检测 | gap 检测用 sequence_no 字段 |
| P-12 | sportsMarketType 未知值 panic: 未来新盘口类型会上线 | parse 失败 → kUnknown + WARN 日志, 不 crash |
| P-13 | enableOrderBook=false 的 market 仍尝试订阅: 老 fpmm market 没有 CLOB orderbook | filter: enableOrderBook==true 才订阅 |
| P-14 | gamma 缓存价格当实时价用: bestBid/bestAsk 有秒级延迟 | 实时价只从 CLOB WS book/price_change 取 |

---

## §9 快速参考 (给小冯实现)

**端点:**
- 元数据: `GET https://gamma-api.polymarket.com/events?tag_id=1&closed=false&active=true&limit=100`
- WS: `wss://ws-subscriptions-clob.polymarket.com/ws/market`
- 单 book 拉取 (可选, 状态对比): `GET https://clob.polymarket.com/book?token_id=<token_id>`

**订阅 payload:**
```json
{"type":"Market","assets_ids":["<token_id_A>","<token_id_B>",...]}
```

**消息分发:**
```
event_type == "book"              → 全量替换 L2 book[asset_id]
event_type == "price_change"      → apply 增量到 L2 book[asset_id]
event_type == "last_trade_price"  → 更新 last_trade_price[asset_id]
event_type == "tick_size_change"  → 更新 tick_size[asset_id]
```

**R-20 时间戳:**
```
data_source_ts  = stoull(msg.timestamp) * 1'000'000LL  // ms → ns
ingestion_ts    = steady_clock::now() (recv 时立即读)
as_of_ts        = steady_clock::now() (被消费时读)
event_ts        = 0  (orderbook 消息无赛事时间)
```

**negRisk 识别:**
```
is_outright = (market.negRisk == true && market.negRiskMarketID != "")
group_id    = market.negRiskMarketID  (同一 group 的所有 market 共享)
```
