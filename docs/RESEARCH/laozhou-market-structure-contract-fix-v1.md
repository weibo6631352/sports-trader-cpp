# 市场结构契约修正 — 架构决议 v1 (debug_api StateProvider per-token 化)

- **owner:** 老周 (chief-architect, 系统工程主管 + StateProvider 契约 owner, #01)
- **last_review:** 2026-05-29
- **触发:** GM 派单 — 老板质疑市场结构分级。审计已证实 debug_api 观测层把 Polymarket per-token 订单簿错误压成 condition 级单一 book (P0)。
- **性质:** correctness-driven 结构变更 (非 feature)，触及我之前 (老周/老郭口径) 立的 G-FREEZE-W "只增不改名" 冻结，需架构主权裁定 + 老郭 co-sign。
- **建议升级:** **本决议建议升正式 ADR (ADR-040)，签字: 老郭 (freeze co-sign + 架构评审) / 老李 (协议 ack) / GM (合并)。** 理由见 §1.3 — 触及冻结契约 + 跨 owner (小卢/小苏/小冯/小余/老李) + 阻塞下单链路 (MVP P0)。
- **关联输入:**
  - `docs/RESEARCH/laoli-polymarket-market-structure-audit-v1.md` (老李 一手 API 审计，结论先行)
  - `docs/RESEARCH/xiaofeng-clob-orderbook-keying-check-v1.md` (小冯 工程内证: 底层已 per-token，错只在暴露层)
  - `docs/ADR/2026-05-29-observability-debug-api.md` (ADR-038 §3 schema 铁律)
  - `docs/MEETINGS/2026-05-29-frontend-dashboard-v3-design-review.md` §3 (G-FREEZE-W 原始裁定)
  - `src/stcpp/debug_api/state_provider.hpp` (修正对象)

---

## §0 结论先行 (TL;DR)

1. **接口选型: pair view 主路 + 单 token 旁路。** `book_pair(condition_id)` 返回 `BinaryMarketBookView` (YES/NO 双 book + cross_spread) 作为看板主入口；`book(token_id)` 保留为策略/调试单边纯查询旁路。理由见 §3.1。
2. **G-FREEZE-W 修订: correctness-bug 例外通道开启。** 冻结的本意是"防 churn / 防破坏已上线前端"，不是"禁止修正正确性 bug"。本次走 **架构主权 (老周) + 老郭 co-sign** 例外通道。**`market_id` 不物理删除/不改名 → 保留为 `deprecated alias`，新增 `condition_id` / `token_id` / `outcome` 为权威字段**，前端 v3 不破坏。见 §2。
3. **`token_id` (asset_id) 是协议事实，非 vendor 字段名，可暴露。** 它是链上 ERC-1155 ERC token id (CTF positionId)，不是 `pm_`/`clob_` 这类厂商私有字段命名。不违反 ADR-038 §3 vendor-agnostic。见 §4。
4. **看板分级呈现确认正确**: Event 卡 → Market(condition) 卡 → 下挂 YES/NO 两个 outcome 单边 book，卡片头算 `cross_spread = ask_YES + ask_NO − 1` (等效 vig)。见 §5。
5. **MVP 必改 4 项** (P0 阻塞下单链路): MarketInfo 加 `tokens[]`、BookSnapshot per-token 化、`book_pair` 接口、`condition_id`/`token_id` 权威字段。**后续 P1/P2 6 项**。分工见 §6。

---

## §1 G-FREEZE-W 修订裁定 (架构主权 + 老郭 co-sign)

### §1.1 冻结的原意 (回到立法本意)

G-FREEZE-W 是我在前端 v3 设计评审 (2026-05-29) 中立的 schema 冻结，原文 "守 G-FREEZE-W **只增不改名**"。立法本意有二:

- **防 churn**: 前端 v3 在并行开发，schema 字段名频繁改会让前端反复返工。
- **防破坏已上线前端**: `MarketInfo.event_id` 等增量字段以"只增不改名"方式落地，老消费方不受影响。

冻结针对的是 **feature 驱动的字段 churn**，**从未预期、也无权阻止对一个结构性正确性 bug 的修正**。本次 per-token 化属后者。

### §1.2 裁定: correctness-driven 变更走例外通道

依 CLAUDE.md §8 红线 ("回测与实盘用不同数据处理逻辑 → 不允许上线"、"数据 schema 静默变更 → 责任人承担事故") 与 §6 决策机制 ("架构争议进老郭评审，结论入 ADR")，我作为 StateProvider 契约 owner + 架构主权，裁定:

> **G-FREEZE-W 增设 correctness-bug 例外条款**: 当冻结字段被审计证实为结构性正确性错误 (P0/P1) 时，修正不受"只增不改名"约束，但必须满足三条:
> 1. **架构主权 (老周) 签 + 老郭 freeze co-sign** (双签，缺一不可)；
> 2. **不物理删除/不裸改名已上线字段** — 错误字段降级为 `deprecated alias` 与权威新字段并存，给前端一个 sprint 过渡窗 (见 §2 兼容策略)；
> 3. **入正式 ADR** + 通知全部下游 (最小惊喜，CLAUDE.md §7.3)。

此例外**不**对 feature churn 开口子 — feature 仍严守"只增不改名"。

### §1.3 兼容策略裁定: alias 保留，不裸 rename

两个候选:
- **方案甲 (裸 rename)**: 直接 `market_id` → `condition_id`，删 `market_id`。**否决** — 破坏已上线前端 v3 (endpoint_market.cpp 当前 book 端点已输出 `market_id` 字段)，违反冻结本意第二条。
- **方案乙 (alias 并存)**: 新增 `condition_id` (权威) + `token_id` + `outcome`，**保留 `market_id` 为 deprecated alias，值 = condition_id**，下个 sprint 前端切完后 P2 移除。**采纳。**

> **裁定**: 采方案乙。`market_id` 在本轮保留为 `deprecated alias`(注释标 `// DEPRECATED: 用 condition_id; 前端切换后 P2 移除`)，endpoint JSON 同时输出 `market_id` 与 `condition_id` (endpoint_market.cpp 现状已对 book 端点这么做，对齐到 market 端点)。前端 v3 一个 sprint 内切到 `condition_id`，切完小苏报 ack → P2 移除 alias。

---

## §2 修正后的 Schema (精确到字段)

> 以下为契约权威定义。实施由小卢按此落 `state_provider.hpp`。本节不写实现逻辑，只定字段与语义。

### §2.1 TokenInfo (新增 struct)

```
struct TokenInfo {
    std::string token_id;   // uint256 string (= CLOB asset_id; 协议事实, §4 裁定可暴露)
    std::string outcome;    // "Yes"/"No"/"Clippers"/"Over 220.5" 等 (gamma outcomes[i])
    double price{0.0};      // gamma outcomePrices[i] / clob tokens[i].price
    bool winner{false};     // 结算后 true
};
```

### §2.2 MarketInfo (修正)

```
struct MarketInfo {
    bool found{false};
    std::string condition_id;            // 【权威·新增】bytes32 hex "0x..", 盘口主键
    std::string market_id;               // 【DEPRECATED alias】= condition_id; 前端切后 P2 移除
    std::vector<TokenInfo> tokens;       // 【P0 新增】双 token 列表 — 下单链路入口 (原缺失=阻塞)
    double tick_size{0.0};
    double fee_rate{0.0};
    bool neg_risk{false};
    std::string neg_risk_market_id;      // 【P1 新增】negRisk 父合约 ID (合约选择完整性)
    bool accepting_orders{false};
    bool active{false};
    bool closed{false};
    bool resolved{false};
    std::string source{"polymarket"};
    std::int64_t as_of_ts_ns{0};
    std::string event_id;                // 已有 (ADR-038 增量), 保留
};
```

- **condition_id vs token_id 语义厘清**: `condition_id` = 盘口 (Market) 级主键 (bytes32 hex)；`token_id` = 单边 (Outcome) 级主键 (uint256 十进制 string)。一个 `condition_id` 恒对应 `tokens.size()==2` (Polymarket 二元市场全平台统一)。`market(condition_id)` 接口入参不变 (仍按盘口查元数据)，但出参补 `tokens[]` 让调用方拿到 condition→token 映射。

### §2.3 BookSnapshot (per-token 化)

```
struct BookSnapshot {
    bool found{false};
    std::string token_id;        // 【权威·新增】asset_id — orderbook 真实粒度
    std::string condition_id;    // 【新增】归属盘口 (UI 分组用)
    std::string outcome;         // 【新增】"Yes"/"No"/球队名 — 这是哪一边的报价
    std::string market_id;       // 【DEPRECATED alias】= condition_id; P2 移除
    double best_bid{0.0};        // 本 token 的 best_bid (YES≠NO)
    double best_ask{0.0};
    double microprice{0.0};
    double spread{0.0};
    double imbalance{0.0};       // 本 token 单边 imbalance (不再合并双边)
    std::int64_t sequence_no{0}; // 本 token 的 WSS 序列号 (per asset_id; 不再单值混用)
    std::int64_t gap_count{0};   // 本 token 的 gap
    std::string wss_state{"unknown"};
    FourTs ts{};
    std::string source{"polymarket"};
    std::vector<BookLevel> bids{};
    std::vector<BookLevel> asks{};
};
```

### §2.4 BinaryMarketBookView (新增 — 看板主入口)

```
struct BinaryMarketBookView {
    bool found{false};
    std::string condition_id;
    BookSnapshot token0;        // tokens[0] (index 对齐 gamma outcomes[0])
    BookSnapshot token1;        // tokens[1]
    double cross_spread{0.0};   // = token0.best_ask + token1.best_ask - 1.0 (等效 vig)
    FourTs ts{};                // 取两 token 较旧 as_of (保守 staleness)
};
```

> **不用 yes_book/no_book 命名，用 token0/token1**: 因 Moneyline outcome 是球队名 (Clippers/Magic) 而非 Yes/No，硬编 yes/no 会误导。index 对齐 gamma `outcomes[]` 顺序，`outcome` 字段自带语义。

### §2.5 StateProvider 接口 (修正)

```
// 元数据: 入参不变 (盘口级查询), 出参补 tokens[]
virtual MarketInfo market(const std::string& condition_id) const = 0;

// 【新增·看板主入口】按盘口返回双 token book view + cross_spread
virtual BinaryMarketBookView book_pair(const std::string& condition_id) const = 0;

// 【改签名】按 token_id 查单边 book (策略/调试旁路)。
// 原 book(condition_id) 语义错误, 改为 book(token_id)。
virtual BookSnapshot book(const std::string& token_id) const = 0;
```

- **是否需要 `condition()`/`market()` 返回全 token 列表方法**: **不需要新增独立方法。** `market(condition_id)` 出参的 `tokens[]` 已携带盘口下全部 token 列表 (token_id+outcome+price+winner)，调用方拿 MarketInfo 即可枚举两个 token_id 再去 `book(token_id)` 或直接 `book_pair`。再加 `condition()` 是冗余 API 面，否决。

---

## §3 接口选型拍板: pair view vs 单 token

### §3.1 裁定: pair view 主路 + 单 token 旁路 (二者都要，主次分明)

| 维度 | book_pair(condition_id) | book(token_id) |
|---|---|---|
| 看板同屏看一盘口两边 | ✅ 一次拿齐 YES/NO，算 cross_spread/vig | ❌ 要发两次请求，前端自己拼 |
| 策略层定向查单边 | ❌ 拿多余的对侧 | ✅ 最纯粹 |
| RTT (跨洋链路敏感) | ✅ 1 RTT 拿双边 | 双边要 2 RTT |
| 与底层 OrderBookAdapter 对齐 | book_pair 内部 = 2 次 per-token 查再组装 | 直接 1:1 映射 token_states_ |

**结论**: 看板是 debug_api 第一消费方，"同屏看一盘口两边 + 算 vig"是核心场景，跨洋链路下 1 RTT 拿双边显著优于 2 RTT → **`book_pair` 为主入口**。但策略/调试单边纯查询不应被迫拿对侧，且 `book_pair` 内部本就由两次 per-token 查询组装 → **保留 `book(token_id)` 单边旁路**。两者共用同一 BookSnapshot per-token 结构，无重复实现。

### §3.2 sequence_no/gap 按 token 区分

per-token 化后，每个 BookSnapshot 各自携带 `sequence_no`/`gap_count` (对应小冯 `token_seq_map_` 的 per-token_id 值)。`book_pair` 内 token0/token1 各有独立序列，断线重连可独立判定哪一侧 gap。**禁止**再用单一 int64 混表双边 (审计 P1)。

### §3.3 HTTP 路由 (endpoint 层，不破坏现有 URL)

```
GET /api/v1/market/{condition_id}            → MarketInfo (含 tokens[])
GET /api/v1/book/{condition_id}              → BinaryMarketBookView (默认, 看板主路)
GET /api/v1/book/{condition_id}?outcome=0|1  → 单边 BookSnapshot (按 index 选 token)
GET /api/v1/book/token/{token_id}            → 单边 BookSnapshot (策略直查, P2)
```

> 现有 `/api/v1/book/{condition_id}` URL 保留，但 body 从单一 book 改为 BinaryMarketBookView (含 condition_id + 两 token + cross_spread + market_id alias)。这是 body schema 变更，走 §1 例外通道 + 前端 ack。

---

## §4 零反向依赖 (R-12) + vendor-agnostic (ADR-038 §3) 守护确认

### §4.1 R-12 零反向依赖: 仍守

per-token 化纯属 POD struct 字段增改 + 接口签名调整，`state_provider.hpp` 仍只 `#include <cstdint>/<string>/<vector>`，不 include 任何热路径模块 (risk/signer/exec/orderbook)。`book_pair`/`book` 仍是 const 只读，由小冯 OrderBookAdapter 后续提供 double-buffer front snapshot 注入。**零反向依赖不破坏。**

### §4.2 vendor-agnostic: token_id 可暴露 (裁定)

> **裁定: `token_id` (= CLOB `asset_id`) 是协议事实，非 vendor 字段名，可暴露。**

理由:
- `token_id` 是链上 **ERC-1155 token id** (Polymarket CTF `positionId`，keccak 派生)，是**协议/链上事实标识**，等同于 `condition_id` (bytes32 CTF condition)、`event_id` 这类已暴露的协议主键。
- ADR-038 §3 禁的是 **vendor 私有字段名前缀** (`pm_`/`clob_`/`goalserve_`) — 即厂商把同一语义起私有名。`token_id` 不是私有命名，它是协议层公共标识，多家 vendor (gamma 叫 `clobTokenIds`、clob 叫 `token_id`/`asset_id`) 指向同一链上对象。我方统一用协议中性名 `token_id`，`source` 标签仍标 `polymarket`。
- 反例对照: 若我方字段叫 `pm_asset_id` → 违规；叫 `token_id` → 合规。**采 `token_id`。**

黑名单 (私钥/签名字节/API secret) 在这些 POD 里物理不存在，本次不引入。✅

---

## §5 看板分级呈现裁定

> **确认呈现正确。** 老李 §6.4 的结构即采纳态:

```
[Event 卡]  "2026 NHL Stanley Cup Champion"  (event_id 锚, 拉比分)
  └─ [Market/condition 卡]  question  (condition_id, negRisk, tick, fee, accepting)
        ├─ [Outcome token0]  outcome=Yes  token_id=79397…  price=0.555
        │     best_bid / best_ask / spread / imbalance / depth(bids,asks) / seq,gap
        └─ [Outcome token1]  outcome=No   token_id=40473…  price=0.445
        │     best_bid / best_ask / spread / imbalance / depth / seq,gap
        └─ cross_spread = token0.ask + token1.ask − 1.0   (等效 vig; 越大佣金越厚)
```

裁定要点:
1. 每张 **condition 卡片**下挂 **两个 outcome 单边订单簿**，各自独立 best_bid/ask/depth/imbalance — 正确，对齐 per-token 真实结构。
2. `cross_spread = ask_token0 + ask_token1 − 1` 是 vig 的正确度量 (买齐两边的过付)。实测 NHL 样本 = 0.56+0.45−1 = 0.01，合理。**确认正确，由后端 `book_pair` 算好下发** (前端不自算，避免双边 staleness 不一致时算错)。
3. DEMO 标记 (老钱红线) / advisory 角标 (小邓) 等前端纪律不变，沿用前端 v3 评审 §4。

---

## §6 影响面 + 实施分工

### §6.1 受影响文件 (精确清单)

| 文件 | 改动 | owner |
|---|---|---|
| `src/stcpp/debug_api/state_provider.hpp` | TokenInfo 新增；MarketInfo 加 tokens[]/condition_id/neg_risk_market_id；BookSnapshot per-token 字段；BinaryMarketBookView 新增；StateProvider 加 book_pair / 改 book 签名；StubStateProvider 同步空实现 | 小卢 (契约实施) |
| `src/stcpp/debug_api/demo_state_provider.hpp` | book→per-token demo (YES/NO 两组互补值)；新增 book_pair demo (cross_spread)；market 填 tokens[] demo | 小卢 |
| `src/stcpp/debug_api/endpoint_market.cpp` | market 端点输出 tokens[] 数组；book 端点改输出 BinaryMarketBookView (双 token + cross_spread + condition_id + market_id alias)；加 ?outcome= 分支 | 小卢 |
| 前端 v3 盯盘 (小苏负责的前端代码库) | 消费新 book_pair body：每 condition 卡下挂两 outcome book + cross_spread；字段从 market_id 切到 condition_id；token_id/outcome 展示 | 小苏 |
| `docs/RESEARCH/laozhou-w8-debug-rest-api-spec-v1.md` | REST spec 同步 (book body schema 变更, market tokens[]) | 小卢 + 小米 doc curator |

> 注: 无独立 `endpoint_book.cpp`，book 路由在 `endpoint_market.cpp` 内 (`register_book`)。grep 确认 `endpoint_quote.cpp`/`endpoint_score.cpp` 也持 condition_id 但语义正确 (quote/score 本就盘口级/赛事级)，**不在本次改动面**。

### §6.2 跨 owner 标注

- **老李 (协议 ack)**: §2 字段语义、token_id/condition_id 体系、tokens[] index 对齐 outcomes — 老李 protocol ack 后小卢方可落字段。已有审计文档为依据，ack 走 §1 ADR 签字。
- **小冯 (底层供数)**: BookSnapshot per-token 化后，真实接入时 sequence_no/gap/imbalance 由小冯 OrderBookAdapter per-token_id snapshot 供给 (底层已 per-token，小冯无需改底层，只需提供 double-buffer 读接口对接)。本轮 demo 先行不阻塞小冯。
- **小余 (event↔market 映射)**: tokens[] 与 event_id 映射的真实 provider 后续接入，本轮 demo 先行。
- **老郭 (freeze co-sign)**: §1 G-FREEZE-W 例外通道必须老郭 co-sign 方生效。
- **GM**: 统一合并 (本决议不 commit/push)。

### §6.3 MVP 必改 (P0/P1) vs 后续 (P1/P2)

**MVP 必改 (P0 — 阻塞下单链路/看板正确性):**
1. MarketInfo 加 `tokens[]` (TokenInfo) — 下单链路入口，P0 阻塞。
2. BookSnapshot per-token 化 (token_id/condition_id/outcome + per-token seq/gap)。
3. `book_pair(condition_id)` 接口 + BinaryMarketBookView + cross_spread。
4. `condition_id` 权威字段 + `market_id` 降 deprecated alias (MarketInfo/BookSnapshot 同步)。
5. demo + endpoint + Stub 同步上述。
6. 前端 v3 切 condition_id + 双 outcome 卡呈现。

**后续 (P1/P2):**
- `neg_risk_market_id` (P1, negRisk 合约选择完整性)。
- HoldingView 加 token_id (P1, 持仓→orderbook 路由；本决议未改 HoldingView，留对应 owner 排期)。
- `book(token_id)` 单边旁路 + `/api/v1/book/token/{token_id}` 路由 (P2, 策略直查)。
- `market_id` deprecated alias 移除 (P2, 前端切换 ack 后)。
- 真实 provider 接入 (小冯 book / 小余 映射, 各单元排期)。

---

## §7 决议摘要 (回报 GM)

1. **接口选型**: `book_pair(condition_id)` → BinaryMarketBookView 为看板主入口 (1 RTT 拿双边 + cross_spread)；`book(token_id)` 单边为策略/调试旁路。二者共用 per-token BookSnapshot。
2. **G-FREEZE-W 修订**: 增 correctness-bug 例外条款 — 走架构主权(老周)+老郭 co-sign 双签，错误字段降 deprecated alias 不裸删/不裸改名，入 ADR。本次采 alias 并存 (market_id 保留=condition_id, 前端切后 P2 移除)，不破坏已上线前端 v3。
3. **token_id 可暴露**: 链上 ERC-1155 token id 属协议事实，非 vendor 私有字段名，守 ADR-038 §3。
4. **看板呈现**: condition 卡下挂两 outcome 单边 book + cross_spread=ask0+ask1−1，确认正确，cross_spread 后端算好下发。
5. **分工**: 小卢 (state_provider.hpp / demo / endpoint_market.cpp 后端) + 小苏 (前端 v3) + 老李 (protocol ack) + 老郭 (freeze co-sign) + GM (合并)。MVP 必改 6 项，后续 P1/P2 5 项。
6. **建议升 ADR-040** (老郭/老李/GM 签)。

---

**最后更新:** 2026-05-29 by 老周 (#01, chief-architect, GM 市场结构契约修正派单)
