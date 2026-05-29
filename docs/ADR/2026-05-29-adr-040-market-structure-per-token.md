# ADR-040: 市场结构契约修正 — per-token 订单簿 (correctness-bug 例外通道)

- **ID:** ADR-040
- **owner:** 老郭 (架构评审 + 顾问团协调 + freeze co-sign) + 老周 (系统工程主管 + StateProvider 契约架构主权)
- **last_review:** 2026-05-29
- **status:** **Accepted**
- **签字 (三签):** 老周 (架构主权) + 老郭 (G-FREEZE-W co-sign + 架构评审) + GM 老雷 (合并); 老李 (Polymarket 协议 ack)
- **类别:** correctness-driven 结构变更 (非 feature) — 触及 G-FREEZE-W 冻结契约, 走 correctness-bug 例外通道

---

## §0 决策摘要 (TL;DR)

1. **修正一个 P0 结构性正确性 bug:** debug_api 观测层把 Polymarket **per-token (per-outcome) 订单簿**错误压成 **per-condition 单一 book**。真实结构是 `Event → Market/condition → Outcome/token → 订单簿 (per token_id)`,一个 condition = 2 个互补镜像 token,每个 token 各有独立订单簿。
2. **G-FREEZE-W 增设 correctness-bug 例外条款:** 冻结的本意是「防 feature churn / 防破坏已上线前端」,从未预期、也无权阻止对结构性正确性 bug 的修正。本次走 **架构主权 (老周) + 老郭 co-sign 双签**例外通道,本 ADR 即为该 co-sign 的正式记录。
3. **alias 并存, 不裸删 / 不裸改名:** 错误字段 `market_id` 降级为 `deprecated alias` (值 = `condition_id`),与权威新字段并存,给前端一个 sprint 过渡窗,P2 移除。不破坏已上线前端 v3。
4. **接口选型:** `book_pair(condition_id) → BinaryMarketBookView` (双 token + cross_spread) 为看板主入口 (跨洋链路 1 RTT 拿双边);`book(token_id)` 改为单边旁路供策略/调试直查。
5. **token_id 可暴露:** 它是链上 ERC-1155 token id (CTF positionId),属协议事实而非 vendor 私有字段名,守 ADR-037 (数据/模型战略 vendor-agnostic) §3。
6. **已实现并合入 main:** 后端 `88dd1c4` + 前端 `cf636c3` / `a76008a`。本 ADR 为事后正式化 (decision 在派单与老周决议时已定,实现先行,ADR 补登记)。

---

## §1 背景与触发

### §1.1 触发链

- **老板质疑 (2026-05-29):** 「市场结构分级正确吗? 订单簿是属于盘口单边的吧?」
- **老李一手 API 审计** (`docs/RESEARCH/laoli-polymarket-market-structure-audit-v1.md`): 实测 gamma/clob/book 端点,**结论先行 — GM 质疑完全成立**。BookSnapshot 按 condition_id 聚合单一 book 是结构性错误。
- **小冯工程内证** (`docs/RESEARCH/xiaofeng-clob-orderbook-keying-check-v1.md`): 底层 `OrderBookAdapter` key 已是 `token_id` (YES/NO 各一独立 TokenState),**错只在 debug_api 暴露层**,非底层。
- **老周架构决议** (`docs/RESEARCH/laozhou-market-structure-contract-fix-v1.md`): 契约修正裁定 + 建议升 ADR-040,签字老郭 (freeze co-sign) / 老李 (protocol ack) / GM (合并)。

### §1.2 为何升正式 ADR

依 CLAUDE.md §6「架构争议进老郭评审,结论入 ADR」+ §8 红线「数据 schema 静默变更 (不通知下游) → 责任人承担事故」「跳过架构评审上重大变更 → 直接回滚」。本变更:① 触及已冻结契约 (G-FREEZE-W);② 跨 owner (小卢 / 小苏 / 小冯 / 小余 / 老李);③ 阻塞 MVP 下单链路 (P0)。三者任一都需正式 ADR + 双签。

---

## §2 真实市场结构 (一手 API 证明, 老李审计)

```
Event (赛事)                         gamma /events[i]   id, negRisk, negRiskMarketID
  └─ Market / Condition (盘口)        condition_id (bytes32 hex, "0x..", 66 字符)
        question / outcomes[] / clobTokenIds[] / tick_size / fee / accepting_orders
        恒含 2 个 token (Polymarket 二元市场全平台统一)
        ├─ Outcome / Token (单边)     token_id (= CLOB asset_id, uint256 string, ≤77 位)
        │     outcome ("Yes"/"No"/"Clippers"/"Over 220.5") / price / winner
        │     └─ 订单簿 (per token_id)   GET /book?token_id=  bids[] / asks[]
        └─ Outcome / Token (另一边)   独立订单簿, 与对侧完全分离
```

**核心事实:** 订单簿挂在 **token_id (outcome) 级别**,不在 condition_id 级别。

**互补镜像实测 (NHL Hurricanes):** `price_YES + price_NO = 1.00`;YES bestBid 0.55 + NO bestAsk 0.45 = 1.00;YES ask size = NO bid size 完全镜像。两个订单簿**互补但完全独立** (各有独立深度 / 序列号 / hash),**不可合并为单一 book**。

**ID 体系:**

| 维度 | condition_id | token_id (asset_id) |
|---|---|---|
| 类型 | bytes32 hex "0x.." 66 字符 | uint256 十进制 string ≤77 位 |
| 粒度 | 盘口 (Market) 级 | 单边 (Outcome) 级 |
| 关系 | 1 condition = 2 token | 1 token = 1 独立订单簿 |
| gamma 字段 | `conditionId` | `clobTokenIds[i]` |
| clob 字段 | `condition_id` | `tokens[i].token_id` |
| 下单 (EIP-712) | `condition_id` | `tokenId` (uint256) |
| 风控 cap | per-condition (市场级) | per-token (单边头寸) |

---

## §3 G-FREEZE-W 修订裁定 (架构主权 + 老郭 co-sign)

### §3.1 冻结原意

G-FREEZE-W 是老周在前端 v3 设计评审 (`docs/MEETINGS/2026-05-29-frontend-dashboard-v3-design-review.md` §3) 中立的 schema 冻结,原文「守 G-FREEZE-W **只增不改名**」。立法本意:① 防 feature 驱动的字段 churn (前端 v3 并行开发);② 防破坏已上线前端。冻结针对的是 **feature 驱动的 churn**,从未预期阻止**结构性正确性 bug** 的修正。

### §3.2 裁定: correctness-bug 例外条款 (本 ADR 正式新增到 G-FREEZE-W)

> **G-FREEZE-W correctness-bug 例外条款 (ADR-040 立):** 当冻结字段被审计证实为结构性正确性错误 (P0/P1) 时,修正不受「只增不改名」约束,但必须满足三条铁律:
> 1. **架构主权 (老周) 签 + 老郭 freeze co-sign** — 双签,缺一不可。
> 2. **不物理删除 / 不裸改名已上线字段** — 错误字段降级为 `deprecated alias`,与权威新字段并存,给前端一个 sprint 过渡窗。
> 3. **入正式 ADR + 通知全部下游** (最小惊喜, CLAUDE.md §7.3)。
>
> 此例外**不**对 feature churn 开口子 — feature 仍严守「只增不改名」。

### §3.3 老郭 co-sign 评审意见 (freeze 否决权持有人)

作为 G-FREEZE-W co-owner 与架构评审 / 否决权持有人,我 (老郭) **co-sign 批准**本次例外,理由:

1. **bug 性质成立 — 有一手实测 + 工程内证双重证据。** 老李一手 API 审计 + 小冯底层内证,二者交叉印证「错只在暴露层」。不是口径之争,是 verifiable 的结构性错误。证据强度满足「数字说话」(CLAUDE.md §3.3)。
2. **alias 并存方案守住了冻结第二本意。** 方案乙 (新增 `condition_id` 权威 + 保留 `market_id`=condition_id 的 deprecated alias) 对已上线前端 v3 **零破坏**,前端可在自己的 sprint 节奏内切换。我否决了方案甲 (裸 rename 删 `market_id`) —— 那会即时破坏 endpoint 输出契约。
3. **例外条款边界清晰, 不会被滥用为 churn 后门。** 触发条件硬绑「审计证实的 P0/P1 正确性 bug」+ 双签 + 入 ADR + 下游通知,四道闸。feature churn 想借此通道改名,缺「审计证实正确性 bug」前提即被拦。
4. **零反向依赖 (R-12) 与 vendor-agnostic (ADR-037 §3) 均未被破坏** (见 §6)。
5. **MVP 不修则阻塞下单链路。** MarketInfo 缺 tokens[] = 调用方无法从盘口元数据拿到 condition→token 映射 = 下单路径断点。correctness bug 直接挂在北极星目标的关键路径上,优先级正当。

> **co-sign 结论: 批准。** 例外通道开启仅限本次 per-token 修正及同性质后续 (HoldingView token_id 等);任何 feature 改名仍需重新走完整冻结流程。

### §3.4 兼容策略: alias 保留 (否决裸 rename)

- **方案甲 (裸 rename, 删 `market_id`):** 否决 — 破坏已上线前端 v3 (endpoint 当前输出 `market_id`),违反冻结第二本意。
- **方案乙 (alias 并存):** **采纳。** 新增 `condition_id` (权威) + `token_id` + `outcome`,保留 `market_id` 为 deprecated alias (值 = condition_id),endpoint JSON 同时输出。前端 v3 一个 sprint 内切到 `condition_id`,小苏报 ack → P2 移除 alias。

---

## §4 契约修正 (权威定义)

> 落地权威以代码 `src/stcpp/debug_api/state_provider.hpp` 为准 (commit 88dd1c4)。本节定字段与语义。

### §4.1 TokenInfo (新增)

```cpp
struct TokenInfo {
    std::string token_id;   // uint256 string (= CLOB asset_id; 协议事实, §5 裁定可暴露)
    std::string outcome;    // "Yes"/"No"/"Clippers"/"Over 220.5" (gamma outcomes[i])
    double price{0.0};      // gamma outcomePrices[i] / clob tokens[i].price
    bool winner{false};     // 结算后 true
};
```

### §4.2 MarketInfo (修正)

- **【权威·新增】** `condition_id` (bytes32 hex, 盘口主键)
- **【DEPRECATED alias】** `market_id` (= condition_id; 前端切后 P2 移除)
- **【P0 新增】** `std::vector<TokenInfo> tokens` — 双 token 列表, 下单链路入口 (原缺失 = 阻塞)
- **【P1 新增】** `neg_risk_market_id` (negRisk 父合约 ID, 可空)
- **【UI 新增, 落地态】** `slug` (gamma slug) + `polymarket_url` (= `https://polymarket.com/event/` + slug)
  - **注 (老郭):** `slug` / `polymarket_url` 不在老周决议 §2.2 schema 表内,是落地实现 (88dd1c4) 为前端超链接 (老板要求) 追加的 UI 字段,属「只增」纯增量,不触冻结改名条款,合规。本 ADR 如实登记落地态。

`condition_id` 恒对应 `tokens.size()==2`。`market(condition_id)` 入参不变,出参补 `tokens[]`。

### §4.3 BookSnapshot (per-token 化)

- **【权威·新增】** `token_id` (asset_id, 订单簿真实粒度) / `condition_id` (归属盘口, UI 分组) / `outcome` (这是哪一边)
- **【DEPRECATED alias】** `market_id` (= condition_id; P2 移除)
- `best_bid`/`best_ask`/`microprice`/`spread`/`imbalance` 均为**本 token 单边**值 (YES≠NO,不再合并双边)
- `sequence_no`/`gap_count` 为**本 token 的 per asset_id** 值 (禁单一 int64 混表双边, 审计 P1)

### §4.4 BinaryMarketBookView (新增 — 看板主入口)

```cpp
struct BinaryMarketBookView {
    bool found{false};
    std::string condition_id;
    BookSnapshot token0;        // tokens[0] (index 对齐 gamma outcomes[0])
    BookSnapshot token1;        // tokens[1]
    double cross_spread{0.0};   // = token0.best_ask + token1.best_ask - 1.0 (等效 vig)
    FourTs ts{};                // 取两 token 较旧 as_of (保守 staleness)
};
```

> 用 `token0/token1` 而非 `yes_book/no_book`: Moneyline outcome 是球队名 (Clippers/Magic) 非 Yes/No,硬编会误导;index 对齐 gamma `outcomes[]`,`outcome` 字段自带语义。`cross_spread` **后端算好下发**,前端不自算 (避免双边 staleness 不一致算错)。

### §4.5 StateProvider 接口

```cpp
virtual MarketInfo market(const std::string& condition_id) const = 0;       // 入参不变, 出参补 tokens[]
virtual BinaryMarketBookView book_pair(const std::string& condition_id) const = 0;  // 【新增·看板主入口】
virtual BookSnapshot book(const std::string& token_id) const = 0;           // 【改签名】原 condition_id → token_id (单边旁路)
```

**不新增 `condition()` 全 token 列表方法:** `market(condition_id)` 的 `tokens[]` 已携带枚举入口,再加属冗余 API 面,否决。

### §4.6 HTTP 路由 (落地态)

```
GET /api/v1/market/{condition_id}        → MarketInfo (含 tokens[]/slug/polymarket_url)
GET /api/v1/book/{condition_id}          → BinaryMarketBookView (默认, 看板主路)
GET /api/v1/book_pair/{condition_id}     → BinaryMarketBookView (显式 pair 路由, 88dd1c4 新增)
GET /api/v1/book/token/{token_id}        → 单边 BookSnapshot (策略直查, 88dd1c4 新增)
```

---

## §5 守护确认

### §5.1 R-12 零反向依赖 — 仍守

per-token 化纯属 POD struct 字段增改 + 接口签名调整,`state_provider.hpp` 仍只 `#include <cstdint>/<string>/<vector>`,不 include 任何热路径模块 (risk/signer/exec/orderbook)。`book_pair`/`book` 仍为 const 只读。**不破坏。**

### §5.2 vendor-agnostic (ADR-037 §3) — token_id 可暴露 (裁定)

`token_id` (= CLOB `asset_id`) 是链上 **ERC-1155 token id** (CTF positionId, keccak 派生),属**协议 / 链上事实标识**,等同已暴露的 `condition_id` / `event_id`。ADR-037 §3 禁的是 **vendor 私有字段名前缀** (`pm_`/`clob_`/`goalserve_`)。`token_id` 是协议层中性公共标识,多家 vendor 指向同一链上对象。我方统一用中性名 `token_id`,`source` 标签仍标 `polymarket`。

- 反例对照: `pm_asset_id` → 违规;`token_id` → 合规。**采 `token_id`。**
- 黑名单 (私钥 / 签名字节 / API secret) 在这些 POD 里物理不存在。✓

---

## §6 影响面与分工 (落地态)

| 文件 | 改动 | owner | 状态 |
|---|---|---|---|
| `src/stcpp/debug_api/state_provider.hpp` | TokenInfo/BinaryMarketBookView 新增; MarketInfo+tokens[]/condition_id/neg_risk_market_id/slug/url; BookSnapshot per-token; book_pair/book 签名; StubStateProvider 同步 | 小卢 | ✅ 88dd1c4 |
| `src/stcpp/debug_api/demo_state_provider.hpp` | book(token_id) 互补单边; book_pair cross_spread; market 填 tokens[] | 小卢 | ✅ 88dd1c4 |
| `src/stcpp/debug_api/endpoint_market.cpp` | market 输出 tokens[]/slug/url; book 改 BinaryMarketBookView | 小卢 | ✅ 88dd1c4 |
| `src/stcpp/debug_api/endpoint_book_pair.cpp` (新增) | `/api/v1/book_pair/{condition_id}` + `/api/v1/book/token/{token_id}` | 小卢 | ✅ 88dd1c4 |
| 前端 v3 盯盘 | 双 outcome 卡 + cross_spread + market_id→condition_id 切换 + Polymarket 超链接 + 同赛事多盘口并列 | 小苏 | ✅ cf636c3 / a76008a |

**协议 ack:** 老李 — §2/§4 字段语义、token_id/condition_id 体系、tokens[] index 对齐 outcomes,以审计文档为依据,protocol ack 走本 ADR 签字 (老李 protocol ack)。

### §6.1 MVP 必改 (已完成) vs 后续

**MVP 必改 (P0, 全部 ✅):** MarketInfo+tokens[]、BookSnapshot per-token、book_pair+BinaryMarketBookView+cross_spread、condition_id 权威 + market_id alias、demo/endpoint/Stub 同步、前端双 outcome 卡。

**后续 (P1/P2):**
- `neg_risk_market_id` 真实填充 (P1, negRisk 合约选择完整性) — 字段已加,真实 provider 接入待排期。
- HoldingView 加 token_id (P1, 持仓→订单簿路由) — 本 ADR 未改,同例外条款覆盖,留对应 owner 排期。
- `market_id` deprecated alias 移除 (P2, 前端切换 ack 后)。
- 真实 provider 接入 (小冯 book per-token snapshot / 小余 event↔market 映射, 各单元排期)。

---

## §7 决议要点 (回报 GM)

1. **per-token 订单簿修正 = correctness bug, 走 G-FREEZE-W 例外通道** (架构主权老周 + 老郭 co-sign 双签),本 ADR 即 co-sign 正式记录。
2. **G-FREEZE-W 增 correctness-bug 例外条款** (§3.2),四道闸限定,不开 feature churn 后门。
3. **alias 并存** (market_id=condition_id deprecated, P2 移除),已上线前端零破坏。
4. **token_id 可暴露** (协议事实, 守 ADR-037 §3);**R-12 零反向依赖不破坏**。
5. **已实现并合入 main:** 后端 88dd1c4,前端 cf636c3 / a76008a。落地比老周决议多了 `slug`/`polymarket_url` 两个 UI 增量字段 (纯「只增」,合规,本 ADR 如实登记)。

---

## §8 关联文档

| 文档 | 关联 |
|---|---|
| `docs/RESEARCH/laozhou-market-structure-contract-fix-v1.md` | 老周架构决议 (本 ADR 主依据) |
| `docs/RESEARCH/laoli-polymarket-market-structure-audit-v1.md` | 老李一手 API 审计 (结论先行) |
| `docs/RESEARCH/xiaofeng-clob-orderbook-keying-check-v1.md` | 小冯工程内证 (底层已 per-token) |
| `docs/MEETINGS/2026-05-29-frontend-dashboard-v3-design-review.md` §3 | G-FREEZE-W 原始裁定 |
| `docs/ADR/2026-05-29-observability-debug-api.md` (ADR-038) | §3 schema 铁律 (4 时间戳 + vendor-agnostic) |
| `docs/ADR/2026-05-29-data-model-strategy-vendor-agnostic.md` (ADR-037) | §3 vendor-agnostic (token_id 可暴露依据) |
| `src/stcpp/debug_api/state_provider.hpp` | 契约权威落地 (88dd1c4) |

---

**最后更新:** 2026-05-29 by 老郭 (#F, 架构评审 + freeze co-sign + GM 派单正式化 ADR-040)
