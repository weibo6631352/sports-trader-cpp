# Polymarket 市场结构审计 v1 — debug_api 模型正确性

- **owner:** 老李 (polymarket-protocol-expert, #07)
- **last_review:** 2026-05-29
- **触发:** GM 紧急派单 — 老板质疑 "市场结构分级正确吗? 订单簿是属于盘口单边的吧?"
- **一手数据来源:**
  - `gamma-api.polymarket.com/events?closed=false&limit=20&tag_slug=sports` 实测 @ 2026-05-29
  - `gamma-api.polymarket.com/events/27829` 实测 @ 2026-05-29 (NHL Stanley Cup outright)
  - `clob.polymarket.com/markets/0xf7b5491e70b477d451afe7d9c1fde4bf1a927e69ff289d294b96df164f6c10f0` 实测 @ 2026-05-29
  - `clob.polymarket.com/book?token_id=<YES>` 实测 @ 2026-05-29
  - `clob.polymarket.com/book?token_id=<NO>` 实测 @ 2026-05-29
  - `POST clob.polymarket.com/books` 实测 @ 2026-05-29
  - `clob.polymarket.com/markets?next_cursor=MA==` 实测 @ 2026-05-29
  - `docs/RESEARCH/laoli-w8-polymarket-data-structure-ssot-v1.md` (SSOT v1, 老李, W8)
  - `docs/RESEARCH/xiaofeng-clob-orderbook-keying-check-v1.md` (小冯工程内证, 2026-05-29)
- **审计范围:** `src/stcpp/debug_api/state_provider.hpp` — MarketInfo / BookSnapshot / StateProvider
- **结论先行:** GM 质疑**完全成立**。BookSnapshot 按 condition_id 聚合单一 book 是结构性错误；应改为 per token_id / per outcome 双 book。MarketInfo 的 market_id/outcome 语义需区分 condition_id vs token_id。

---

## §1 真实 Polymarket 市场结构层级（一手 API 证明）

### §1.1 四层结构

实测返回的 JSON 层级如下：

```
Event（赛事）        → gamma /events[i]
  |  id: 27829
  |  title: "2026 NHL Stanley Cup Champion"
  |  negRisk: true（outright / 系列赛互斥组）
  |  negRiskMarketID: "0x7faa974ff857682d64433d5c4dfba46ff51415a68cbd5bd1994248df2d561200"
  |
  +-- Market / Condition（盘口）→ markets[j]，condition_id 级别
  |     conditionId: "0xf7b5491e70b477d451afe7d9c1fde4bf1a927e69ff289d294b96df164f6c10f0"
  |     question: "Will the Carolina Hurricanes win the 2026 NHL Stanley Cup?"
  |     outcomes: '["Yes","No"]'              ← stringified JSON，需二次 parse
  |     outcomePrices: '["0.555","0.445"]'    ← stringified JSON
  |     clobTokenIds: '["79397...","40473..."]'  ← stringified JSON，index 对齐 outcomes
  |     negRisk: true
  |     acceptingOrders: true
  |     orderPriceMinTickSize: 0.01
  |     feeSchedule: {exponent:1, rate:0.03, takerOnly:true, rebateRate:0.25}
  |
  +-- Outcome / Token（单边，orderbook 的真实粒度）→ clobTokenIds[k]
  |     token_id[0] = "79397003434468715775480922117285203652110865791390656395657957066470661722480"
  |     outcome[0]  = "Yes"
  |     price[0]    = 0.555
  |     |
  |     +-- Orderbook（per token_id，CLOB 一等公民）
  |           GET /book?token_id=79397...
  |           {
  |             "market": "0xf7b5491e...",   ← condition_id
  |             "asset_id": "79397...",      ← 即 token_id
  |             "bids": [{"price":"0.55","size":"44334.82"}, ...],
  |             "asks": [{"price":"0.56","size":"9554.48"},  ...]
  |           }
  |
  +-- Outcome / Token（另一边）
        token_id[1] = "40473977441010332007887229299980126707900816205713676157646215720073415416624"
        outcome[1]  = "No"
        price[1]    = 0.445
        |
        +-- Orderbook（独立，与 YES book 完全分离）
              GET /book?token_id=40473...
              {
                "market": "0xf7b5491e...",   ← 同一 condition_id
                "asset_id": "40473...",      ← 不同 token_id
                "bids": [{"price":"0.44","size":"9554.48"}, ...],
                "asks": [{"price":"0.45","size":"44334.82"}, ...]
              }
```

**关键结论：一个 condition（盘口）有 2 个 token_id，每个 token_id 有独立的 orderbook。订单簿挂在 token_id（outcome）级别，不在 condition_id 级别。**

### §1.2 真实 API 原始返回样本（精简）

**gamma /events?closed=false&limit=20&tag_slug=sports（单条 market 精简）：**

```json
{
  "id": 27829,
  "title": "2026 NHL Stanley Cup Champion",
  "negRisk": true,
  "negRiskMarketID": "0x7faa974ff857682d64433d5c4dfba46ff51415a68cbd5bd1994248df2d561200",
  "markets": [
    {
      "conditionId": "0xf7b5491e70b477d451afe7d9c1fde4bf1a927e69ff289d294b96df164f6c10f0",
      "question": "Will the Carolina Hurricanes win the 2026 NHL Stanley Cup?",
      "groupItemTitle": "Carolina Hurricanes",
      "outcomes": "[\"Yes\", \"No\"]",
      "outcomePrices": "[\"0.555\", \"0.445\"]",
      "clobTokenIds": "[\"79397003434468715775480922117285203652110865791390656395657957066470661722480\", \"40473977441010332007887229299980126707900816205713676157646215720073415416624\"]",
      "negRisk": true,
      "acceptingOrders": true,
      "orderPriceMinTickSize": 0.01,
      "feeSchedule": {"exponent": 1, "rate": 0.03, "takerOnly": true, "rebateRate": 0.25},
      "bestBid": 0.55,
      "bestAsk": 0.56
    }
  ]
}
```

**clob /markets/{condition_id}（tokens[] 结构）：**

```json
{
  "condition_id": "0xf7b5491e70b477d451afe7d9c1fde4bf1a927e69ff289d294b96df164f6c10f0",
  "question": "Will the Carolina Hurricanes win the 2026 NHL Stanley Cup?",
  "neg_risk": true,
  "accepting_orders": true,
  "minimum_tick_size": 0.01,
  "minimum_order_size": 5,
  "seconds_delay": 0,
  "tokens": [
    {
      "token_id": "79397003434468715775480922117285203652110865791390656395657957066470661722480",
      "outcome": "Yes",
      "price": 0.555,
      "winner": false
    },
    {
      "token_id": "40473977441010332007887229299980126707900816205713676157646215720073415416624",
      "outcome": "No",
      "price": 0.445,
      "winner": false
    }
  ]
}
```

**clob GET /book?token_id=YES（YES token 独立 orderbook）：**

```json
{
  "market": "0xf7b5491e...",
  "asset_id": "79397003434468715775480922117285203652110865791390656395657957066470661722480",
  "timestamp": "1780045519126",
  "hash": "92360c958b867708e90e38426583af72917a4f54",
  "bids": [
    {"price": "0.55", "size": "44334.82"},
    {"price": "0.54", "size": "35017.7"},
    {"price": "0.53", "size": "16000"}
  ],
  "asks": [
    {"price": "0.56", "size": "9554.48"},
    {"price": "0.57", "size": "77596.92"},
    {"price": "0.58", "size": "3454.54"}
  ]
}
```

**clob GET /book?token_id=NO（NO token 独立 orderbook，与 YES book 完全不同）：**

```json
{
  "market": "0xf7b5491e...",
  "asset_id": "40473977441010332007887229299980126707900816205713676157646215720073415416624",
  "timestamp": "1780045519126",
  "hash": "7357039e17a43b437d536556fd789d88f692581a",
  "bids": [
    {"price": "0.44", "size": "9554.48"},
    {"price": "0.43", "size": "77596.92"},
    {"price": "0.42", "size": "3454.54"}
  ],
  "asks": [
    {"price": "0.45", "size": "44334.82"},
    {"price": "0.46", "size": "35017.7"},
    {"price": "0.47", "size": "16000"}
  ]
}
```

**POST /books 批量（返回每个 token 的额外字段）：**

```json
[
  {
    "market": "0xf7b5491e...",
    "asset_id": "79397...",
    "tick_size": 0.01,
    "neg_risk": true,
    "min_order_size": 5,
    "last_trade_price": 0.550,
    "bids": [...],
    "asks": [...]
  },
  {
    "market": "0xf7b5491e...",
    "asset_id": "40473...",
    "tick_size": 0.01,
    "neg_risk": true,
    "min_order_size": 5,
    "last_trade_price": 0.550,
    "bids": [...],
    "asks": [...]
  }
]
```

---

## §2 YES/NO 互补性实测验证

实测数据（来自 NHL Stanley Cup Hurricanes 市场）：

| 指标 | YES token | NO token | 互补性检验 |
|---|---|---|---|
| bestBid | 0.55 | 0.44 | YES bestBid + NO bestAsk = 0.55 + 0.45 = **1.00** |
| bestAsk | 0.56 | 0.45 | YES bestAsk + NO bestBid = 0.56 + 0.44 = **1.00** |
| depth bids[0] | 44334.82 shares @ 0.55 | 9554.48 shares @ 0.44 | — |
| depth asks[0] | 9554.48 shares @ 0.56 | 44334.82 shares @ 0.45 | YES ask size = NO bid size 完全镜像 |

**结论：YES token 的 asks 就是 NO token 的 bids 的镜像（price_YES + price_NO = 1.00），两个 orderbook 是互补但完全独立的数据结构。每个 token 各有自己的深度、序列号、hash，不可合并为一个单一 book。**

---

## §3 正确的市场结构层级与 ID 体系

### §3.1 层级关系

```
Event（赛事）
  id: int（gamma 内部 ID）
  negRisk: bool
  negRiskMarketID: bytes32 hex（outright/系列赛的父 negRisk 合约 ID）
  1..N
  |
  +-- Market / Condition（盘口）
  |     conditionId: bytes32 hex（"0x..." 66 字符，链上 CTF condition，跨 gamma/clob 主键）
  |     question: 人读盘口描述
  |     negRisk: bool（盘口级，影响下单合约选择）
  |     negRiskMarketID: bytes32 hex（同 event 级，指向 NegRisk 父合约）
  |     outcomes: string[]（stringified JSON）
  |     clobTokenIds: string[]（stringified JSON，index 对齐 outcomes）
  |     tick_size / min_order_size / accepting_orders / fee_schedule
  |     2（二元市场）
  |     |
  |     +-- Outcome / Token（单边，CLOB 一等公民）
  |           token_id: uint256 string（无 0x 前缀，十进制，最多 77 位）
  |           outcome: "Yes"/"No"/"Home Win"/"Over X.5" 等
  |           price: float [0,1]
  |           winner: bool（结算后 true）
  |           |
  |           +-- Orderbook（per token_id，CLOB /book?token_id= 查询粒度）
  |                 bids: [{price,size}]（降序）
  |                 asks: [{price,size}]（升序）
  |                 asset_id = token_id（同义字段）
  |                 market = condition_id（归属盘口）
```

### §3.2 ID 对照表

| 维度 | condition_id | token_id（asset_id）|
|---|---|---|
| 类型 | bytes32 hex，"0x..." 前缀，66 字符 | uint256 string，无前缀，十进制，≤77 位 |
| 粒度 | 盘口（Market）级 | 单边（Outcome）级 |
| 1:N 关系 | 1 condition = 2 token（二元市场）| 1 token = 1 独立 orderbook |
| gamma 字段名 | `conditionId` | `clobTokenIds[i]` |
| clob 字段名 | `condition_id` | `tokens[i].token_id` |
| WSS market channel 订阅 | 不用 | `assets_ids[]`（订阅用 token_id）|
| WSS user channel 订阅 | `markets[]`（用 condition_id）| 不用 |
| /book 查询 | 不用 | `GET /book?token_id=` |
| 下单（SignedOrder）| `condition_id` | `tokenId`（EIP-712 Order 用 uint256）|
| 风控 cap | per-condition cap（市场级别）| per-token cap（单边头寸）|

### §3.3 negRisk group 结构

negRisk event（如冠军赛、系列赛）下每个 Market 各自是一个独立 condition，共享同一个 `negRiskMarketID`（NegRisk 父合约 ID）：

```
Event 27829: "2026 NHL Stanley Cup Champion"
  negRiskMarketID: "0x7faa974ff857682d64433d5c4dfba46ff51415a68cbd5bd1994248df2d561200"
  |
  +-- Market: conditionId=0xf7b5491e... (Hurricanes Win?)   negRisk=true
  +-- Market: conditionId=0x44887f53... (Stars Win?)        negRisk=true
  +-- Market: conditionId=0x4c317ee8... (Blue Jackets Win?) negRisk=true
  ... (共 32 个 market，每个各有 2 个 token)
```

negRisk 影响：下单时 verifyingContract 必须用 NegRisk Exchange V2 地址（`0xe2222d279d744050d28e00520010520000310F59`），普通市场用 CTF Exchange V2（`0xE111180000d2663C0091e4f400237545B87B996B`）。

### §3.4 体育盘口家族在 Polymarket 的组织方式

CLOB 历史数据实测（/markets next_cursor 翻页）确认单场比赛盘口家族组织模式：

```
Event（单场比赛，如 NBA: Clippers vs Magic 2023-03-18）
  |
  +-- Market[0]: "NBA: LA Clippers vs. Orlando Magic"（Moneyline）
  |     condition_id: 0x8945183c...
  |     outcomes: ["Clippers", "Magic"]     ← 直接用球队名，不是 Yes/No
  |     clobTokenIds: ["91665...", "59154..."]
  |     neg_risk: false
  |
  +-- Market[1]: Totals（Over/Under X.5）（若开盘）
  |     condition_id: 0x...
  |     outcomes: ["Over 220.5", "Under 220.5"]
  |
  +-- Market[2]: Spread（让分）（若开盘）
  |     condition_id: 0x...
  |     outcomes: ["Clippers -4.5", "Magic +4.5"]
  |
  +-- Market[3]: 分节盘口（若开盘）
  |     ...
```

**关键确认：**
1. 每个盘口（Moneyline/Totals/Spread）是独立 condition_id，一个 event 下有 1..N 个 condition
2. 每个 condition 有 2 个 token（二元市场，Polymarket 全平台统一）
3. Moneyline 的 outcome 名称不一定是"Yes/No"，可以直接是球队名（如"Clippers"/"Magic"）
4. 每个 token 有独立 orderbook，共 2N 个 orderbook per event（N = 盘口数）

---

## §4 debug_api 现有模型审计

### §4.1 BookSnapshot — 核心错误（GM 质疑正确）

**当前实现（state_provider.hpp 第 231-247 行）：**

```cpp
struct BookSnapshot {
    bool found{false};
    std::string market_id;   // = 请求的 condition_id 内部映射
    double best_bid{0.0};    // 单一 best_bid
    double best_ask{0.0};    // 单一 best_ask
    double microprice{0.0};  // 单一 microprice
    double spread{0.0};
    double imbalance{0.0};   // 单一 imbalance
    std::int64_t sequence_no{0};
    ...
    std::vector<BookLevel> bids{};  // 单一深度列表
    std::vector<BookLevel> asks{};  // 单一深度列表
};

// 接口：按 condition_id 查，返回单一 book
virtual BookSnapshot book(const std::string& condition_id) const = 0;
```

**错误分析：**

| 错误点 | 说明 |
|---|---|
| 粒度错误（根本错误）| Polymarket orderbook 是 per token_id，不是 per condition_id。一个 condition 下有两个独立 orderbook（YES book + NO book），按 condition 聚合成单一 book 丢失了 outcome 标识 |
| best_bid/best_ask 语义不明 | 这个 best_bid 是 YES token 的还是 NO token 的？两者的 best_bid/best_ask 不同（YES bestBid=0.55，NO bestBid=0.44），无法合并 |
| imbalance 无意义 | 两个独立 book 各有自己的 imbalance（YES 侧 vs NO 侧），合并到一个数字丧失信号 |
| sequence_no / gap_count 混用 | sequence_no 是 per token 粒度（WSS asset_id 级），单一 int64 无法区分 YES/NO 两个序列 |
| market_id 命名歧义 | 字段注释说"= 请求的 condition_id 内部映射"，但字段名 market_id 不区分 condition 还是 token 语义 |
| bids/asks 深度列表混合 | YES token 的 asks 和 NO token 的 asks 含义不同（YES asks = 卖 YES；NO asks = 卖 NO，即从对手方看等价于买 YES）不能合并 |

**小冯工程内证（xiaofeng-clob-orderbook-keying-check-v1.md）已确认：**
- 底层 `OrderBookAdapter` key 是 `token_id`，YES 和 NO 各一个独立 TokenState
- CLOB WSS `book`/`price_change` 消息的核心标识是 `asset_id`（= token_id），`market`（condition_id）字段当前被 `(void)market` 忽略
- 两者之间目前没有接入代码（Stub/Demo），但接入时必然遇到信息丢失

### §4.2 MarketInfo — 语义不完整

**当前实现（state_provider.hpp 第 160-175 行）：**

```cpp
struct MarketInfo {
    bool found{false};
    std::string market_id;  // = 请求的 condition_id 内部映射
    std::string outcome;    // 内部 outcome 标签（非 vendor token 字符串）
    double tick_size{0.0};
    double fee_rate{0.0};
    bool neg_risk{false};
    bool accepting_orders{false};
    bool active{false};
    bool closed{false};
    bool resolved{false};
    std::string source{"polymarket"};
    std::int64_t as_of_ts_ns{0};
    std::string event_id;
};
```

**问题分析：**

| 字段 | 问题 |
|---|---|
| `market_id`（绑 condition_id）| 字段名 market_id 语义不明确；实际是 condition_id 级查询，正确但命名有歧义 |
| `outcome`（单个字符串）| 一个 condition 有 2 个 outcome，MarketInfo 只记录一个 outcome 字符串，不携带 clobTokenIds 映射。调用方无法从这里知道"YES 对应哪个 token_id" |
| 缺 `token_id` 字段列表 | 正确的 MarketInfo 应包含 `tokens: [{token_id, outcome, price, winner}]` 的完整列表，这是调用方查 book 的入口 |
| `tick_size` 是 per token 的 | tick_size 在 /book 和 POST /books 返回的是 per token 的字段（实测每个 token 的 tick_size 相同，但协议上 per token 独立）|
| 缺 `negRiskMarketID` | negRisk 市场下单需要 NegRisk Exchange 合约，仅有 neg_risk bool 不够，还需要 negRiskMarketID 用于 event 级聚合风控 |

**StateProvider 接口设计问题：**

```cpp
// 当前接口（state_provider.hpp 第 267-268 行）
virtual MarketInfo market(const std::string& condition_id) const = 0;
virtual BookSnapshot book(const std::string& condition_id) const = 0;
```

`book()` 以 condition_id 为 key 但返回"代表整个盘口"的单一 book，这与 CLOB 协议不符。应以 token_id 为 key，或在 condition_id 接口下返回双 token book view。

### §4.3 HoldingView — 轻微问题

```cpp
struct HoldingView {
    std::string market_id;  // 语义不明确（condition 还是 token？）
    std::string outcome;    // 文字 outcome，但缺 token_id
    ...
};
```

持仓实际是 per token 的（data-api /positions 返回 `asset`=token_id）。`market_id` 应该是 condition_id，`outcome` 应配套 `token_id` 字段，否则无法路由到 orderbook 查询。

---

## §5 错误严重程度分级

| 错误 | 影响范围 | 严重程度 | MVP 必修 |
|---|---|---|---|
| BookSnapshot 按 condition 聚合单一 book | 观测面板无法正确展示双边盘口；接入真实 book 时 YES/NO 数据混合，best_bid/ask 语义不明 | **P0** | 是 |
| book() 接口签名用 condition_id | 底层已是 per-token，接口用 condition_id 强制聚合，架构层面错误 | **P0** | 是 |
| MarketInfo 缺 tokens[] 列表（token_id + outcome 对应关系）| 调用方无法从 MarketInfo 知道哪个 outcome 对应哪个 token_id，无法构建下单链路 | **P0** | 是 |
| market_id 命名歧义（condition vs token 不区分）| 与 SSOT v1 ADR-026 R4 冲突（禁止 string 混用）| P1 | 是（顺便修）|
| sequence_no 单值无法区分 YES/NO gap | 断线重连时无法独立判断双 token 序列完整性 | P1 | 是 |
| HoldingView 缺 token_id | 持仓无法路由 orderbook | P1 | 是 |
| MarketInfo 缺 negRiskMarketID | negRisk 合约选择不完整 | P2 | 后续 |

---

## §6 修正建议（正确 Schema 分级）

### §6.1 核心原则

**Orderbook 必须挂在 token_id 级别，不在 condition_id 级别。**

正确的数据模型：

```
StateProvider 查询路径：
  condition_id → MarketInfo（含 tokens[]: [{token_id, outcome, price, winner}]）
  token_id     → BookSnapshot（单边，每个 outcome 各自独立）
```

### §6.2 BookSnapshot 修正方向

**方案 A（最小修正，推荐 MVP）：**

```cpp
struct BookSnapshot {
    bool found{false};
    // 关键：按 token_id 查询，不是 condition_id
    std::string token_id;      // 新增：asset_id（CLOB 一等公民）
    std::string condition_id;  // 新增：归属盘口（用于 UI 分组）
    std::string outcome;       // 新增：outcome 名称（"Yes"/"No"/"Clippers"等）
    double best_bid{0.0};
    double best_ask{0.0};
    double microprice{0.0};
    double spread{0.0};
    double imbalance{0.0};
    std::int64_t sequence_no{0};   // 这个 token 的序列号
    std::int64_t gap_count{0};
    std::string wss_state{"unknown"};
    FourTs ts{};
    std::string source{"polymarket"};
    std::vector<BookLevel> bids{};
    std::vector<BookLevel> asks{};
};

// 接口：按 token_id 查，不按 condition_id 查
virtual BookSnapshot book(const std::string& token_id) const = 0;

// 或：保留 condition_id 入口，但返回双 token view
virtual BinaryMarketBookView book_pair(const std::string& condition_id) const = 0;
```

**方案 B（更完整，观测面板友好）：**

```cpp
struct BinaryMarketBookView {
    bool found{false};
    std::string condition_id;
    BookSnapshot token0_book;  // outcomes[0] 的 book（含 token_id + outcome）
    BookSnapshot token1_book;  // outcomes[1] 的 book（含 token_id + outcome）
    // 跨 token 汇总
    double cross_spread{0.0};  // ask_token0 + ask_token1 - 1.0（等效 vig）
    FourTs ts{};
};

virtual BinaryMarketBookView book_pair(const std::string& condition_id) const = 0;
```

**HTTP 路由建议（不破坏现有接口）：**

```
GET /api/v1/book/{condition_id}?outcome=yes  → YES token book
GET /api/v1/book/{condition_id}?outcome=no   → NO token book
GET /api/v1/book/{condition_id}              → BinaryMarketBookView（双 token）
GET /api/v1/book/token/{token_id}            → 直接按 token_id 查（最纯粹）
```

### §6.3 MarketInfo 修正方向

```cpp
struct TokenInfo {
    std::string token_id;   // uint256 string（CLOB 一等公民）
    std::string outcome;    // "Yes"/"No"/"Clippers" 等
    double price{0.0};      // 当前市场价（gamma outcomePrices[i]）
    bool winner{false};     // 结算后
};

struct MarketInfo {
    bool found{false};
    std::string condition_id;           // 重命名：明确语义（原 market_id）
    std::vector<TokenInfo> tokens;      // 新增：双 token 完整列表（原缺失）
    double tick_size{0.0};
    double fee_rate{0.0};
    bool neg_risk{false};
    std::string neg_risk_market_id;     // 新增：negRisk 父合约 ID（per-market）
    bool accepting_orders{false};
    bool active{false};
    bool closed{false};
    bool resolved{false};
    std::string source{"polymarket"};
    std::int64_t as_of_ts_ns{0};
    std::string event_id;               // 已有，保留
};

// 接口：用 condition_id 查 market 元数据（这个不变）
virtual MarketInfo market(const std::string& condition_id) const = 0;
```

### §6.4 看板分级呈现建议

```
看板 UI 结构（修正后）：

[Event 卡片] "2026 NHL Stanley Cup Champion"
  |
  +-- [Market 卡片] "Will Hurricanes Win?" (condition_id=0xf7b5491e...)
  |     negRisk=true | accepting=true | tick=0.01 | fee=3%
  |     |
  |     +-- [YES Token] outcome=Yes | token_id=79397... | price=0.555
  |     |     bids: 0.55×44334 / 0.54×35017 / ...
  |     |     asks: 0.56×9554  / 0.57×77596 / ...
  |     |     best_bid=0.55 | best_ask=0.56 | spread=0.01 | imbalance=X
  |     |
  |     +-- [NO Token] outcome=No | token_id=40473... | price=0.445
  |           bids: 0.44×9554  / 0.43×77596 / ...
  |           asks: 0.45×44334 / 0.46×35017 / ...
  |           best_bid=0.44 | best_ask=0.45 | spread=0.01 | imbalance=X
  |
  |     cross_spread (ask_yes + ask_no - 1) = 0.56 + 0.45 - 1.0 = 0.01
```

---

## §7 MVP 必修 vs 后续

### §7.1 MVP 必修（P0，影响接入正确性）

| 任务 | 负责人 | 工程影响 |
|---|---|---|
| BookSnapshot 改为 per token_id（方案 A 最小修正）| 小卢（debug_api owner）+ 老周（架构 ack）| 新增 token_id / condition_id / outcome 字段；book() 接口改为 token_id 参数或 pair |
| MarketInfo 增加 tokens[] 列表（TokenInfo 含 token_id + outcome）| 小卢 + 老周 | 调用方才能知道 condition→token 映射 |
| StateProvider book() 接口签名修正 | 老周（架构主权）+ 老李 spec ack | 接口改动影响 endpoint_market.cpp 路由 |
| market_id 字段重命名为 condition_id | 小卢（改 state_provider.hpp）| 符合 ADR-026 R4 强类型要求 |
| sequence_no 拆分（per token）| 小冯（orderbook_adapter owner）→ 小卢对接 | YES/NO 各自 sequence_no |

### §7.2 后续（P1/P2）

| 任务 | 优先级 | 说明 |
|---|---|---|
| BinaryMarketBookView 完整方案（方案 B）| P1 | 前端看板双边展示，cross_spread 计算 |
| MarketInfo 增加 negRiskMarketID | P1 | negRisk 合约选择完整性 |
| HoldingView 增加 token_id | P1 | 持仓到 orderbook 路由 |
| /api/v1/book/token/{token_id} 直接路由 | P2 | 策略层直接用 token_id 查 |

---

## §8 总结

**GM 质疑完全正确。**

Polymarket 的 orderbook 是 **per token_id（per outcome 单边）** 级别的，不在盘口（condition_id）级别。一个盘口有 2 个 token_id，每个 token_id 有完全独立的 bid/ask 深度、序列号、hash 状态。YES/NO 两个 book 互补（price_YES + price_NO = 1.00）但数据结构独立，不能合并为单一 book。

当前 `debug_api` 的 `BookSnapshot` 用 condition_id 聚合单一 book，在接入真实 `OrderBookAdapter` 时将造成：
1. YES/NO 数据混合，best_bid/best_ask 语义不明
2. per-token imbalance / spread 信号丢失
3. sequence_no 无法区分两个 token 的 gap 状态
4. 调用方（前端/策略层）无法知道报价来自哪个 outcome

`MarketInfo` 缺少 tokens[] 列表，调用方无法从市场元数据中得知 condition_id 到 token_id 的映射关系，是下单路径的阻塞缺口。

**修正方向确定：BookSnapshot 必须改为 per token_id 查询；MarketInfo 必须补充 tokens[] 列表。架构最终形态由老周（系统工程主管）+ 老李（协议 owner）联合确认后，小卢（debug_api owner）实施。**

---

## §9 参考文档

| 文档 | 关联 |
|---|---|
| `docs/RESEARCH/laoli-w8-polymarket-data-structure-ssot-v1.md` | Polymarket 三层结构 SSOT，§2/§3 condition_id vs token_id 体系 |
| `docs/RESEARCH/laoli-w9-w5-polymarket-market-research-update-v1.md` | CLOB V2 协议升级，V2 合约地址，新费率体系 |
| `docs/RESEARCH/xiaofeng-clob-orderbook-keying-check-v1.md` | 工程层 OrderBookAdapter per-token 内证 |
| `docs/RESEARCH/laozhou-w8-debug-rest-api-spec-v1.md` | debug_api REST spec（接口修改需同步更新）|
| `src/stcpp/debug_api/state_provider.hpp` | 审计对象 |

---

**最后更新：** 2026-05-29 by 老李 (#07, polymarket-protocol-expert, GM 紧急审计派单)
