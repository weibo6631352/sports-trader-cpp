# xiaofeng-clob-orderbook-keying-check-v1.md

owner: 小冯 (#34, api-watch / PolymarketCLOBSubscriber)
last_review: 2026-05-29
task: 配合老李市场结构审计 — 核对 CLOB orderbook 真实 key 粒度 + debug_api 映射一致性

---

## 1. 底层 orderbook 的 key 粒度：结论先行

**底层 per-token_id（asset_id），不是 per-condition_id。**

代码证据：

- `orderbook_adapter.hpp` 第 222 行：内部状态容器为
  `std::unordered_map<std::string, TokenState> token_states_`，key 是 `token_id`（string）。

- `orderbook_adapter.hpp` 第 139 行：热路径入口签名
  `OnEvent(const WssEvent& ev, std::string_view token_id, ...)` ——
  调用方传入的就是 `token_id`（即 CLOB 协议里的 `asset_id`），而不是 condition_id。

- `orderbook_adapter.hpp` 第 156–166 行：`ResetToken(std::string_view token_id)` /
  `GetSnapshotState(std::string_view token_id)` —— 所有对外操作也以 token_id 为 key。

- `orderbook_adapter.hpp` 文件头注释（第 17 行）明确写：
  "消费 WssEvent (kBook / kPriceChange) → 维护每 token_id 单档订单簿状态机"。

结论：**一个 condition（盘口）= 两个 token（YES/NO 各一个），底层就维护两个独立的 OrderBookAdapter::TokenState 实例，key 分别是 YES token_id 和 NO token_id。**

---

## 2. CLOB WSS 消息里的 key 粒度

**WSS `book` / `price_change` 消息的核心标识字段是 `asset_id`（= token_id），不是 condition_id。**

代码证据：

- `polymarket_clob_subscriber.cpp` 第 432–434 行（ParseBook）：
  ```cpp
  std::string_view asset_id = ExtractStringField(body, "asset_id");
  std::string_view market   = ExtractStringField(body, "market");
  ```
  `asset_id` 用于 sequence_no gap 检测（token 粒度）和 `snapshot_received_` 跟踪（token 粒度）；
  `market` 字段（= condition_id）当前读取后被 `(void)market` 忽略，不参与路由。

- `polymarket_clob_subscriber.cpp` 第 479–483 行（ParsePriceChange）：同样提取 `asset_id`，
  用 `asset_key(asset_id)` 做 sequence gap 检测，sequence map key = token_id。

- `polymarket_clob_subscriber.hpp` 第 211 行：
  `std::unordered_map<std::string, std::uint64_t> token_seq_map_` —— key 为 token_id；
  第 212 行：`std::unordered_set<std::string> snapshot_received_` —— 元素为 token_id。

- 订阅 frame 构建（第 606–618 行）：
  ```cpp
  out.append(R"({"type":"Market","assets_ids":[)");
  ```
  订阅的 payload 字段名是 `assets_ids`，值传入的是 token_id 列表（调用方传参）；
  user channel 才用 condition_id（`markets` 字段，第 635 行）。

- `polymarket_clob_subscriber.hpp` 注释 §2.1：
  "market channel: subscribe assets_ids (token_id 粒度, 双 token 同订)"。

**订阅粒度结论：market channel 订阅是 per-token_id（asset_id），每个盘口订两个 token（YES + NO）。**

---

## 3. debug_api BookSnapshot 的暴露粒度

**debug_api 按 condition_id（盘口级）暴露一个 BookSnapshot，只含单一 best_bid / best_ask / microprice / imbalance，没有 token 区分。**

代码证据：

- `state_provider.hpp` 第 231–247 行：
  ```cpp
  struct BookSnapshot {
      std::string market_id;   // = 请求的 condition_id 内部映射
      double best_bid{0.0};
      double best_ask{0.0};
      double microprice{0.0};
      ...
  };
  ```
  无 `token_id` 字段，无 outcome/side 标识。

- `state_provider.hpp` 第 268 行 StateProvider 接口：
  `virtual BookSnapshot book(const std::string& condition_id) const = 0;`
  输入是 condition_id，输出是单一 BookSnapshot。

- `endpoint_market.cpp` 第 71–74 行：
  ```cpp
  const std::string condition_id = req.matches[1];
  const BookSnapshot b = sp.book(condition_id);
  ```
  HTTP 路由 `/api/v1/book/{condition_id}` —— 以 condition_id 查询，返回单一 book。

- `demo_state_provider.hpp` 第 264–285 行（DemoStateProvider::book）：
  对同一个 condition_id 返回固定 `best_bid=0.644 / best_ask=0.656 / microprice=0.648`，
  没有 YES/NO 的区分，bids/asks 深度列表也是单边混合的。

---

## 4. 底层与 debug_api 的映射关系分析

### 4.1 当前实际状态

底层 `OrderBookAdapter` 是 per-token 粒度，但 debug_api 的 `BookSnapshot` 是 per-condition 粒度。
**目前两者之间没有实际接入代码**——`StubStateProvider::book()` 返回 `found=false`，
`DemoStateProvider::book()` 返回硬编码演示值，均未消费真实的 `OrderBookAdapter` 输出。

真实接入路径（state_provider.hpp 第 11–13 行注释）尚待实现：
"真实接入: book = 小冯 OrderBookFeatures"。

### 4.2 接入时必然存在的信息丢失问题

当真实接入发生时，若按当前 `BookSnapshot` 接口（per-condition，单一 bid/ask），
则必须把两个 token_id（YES + NO）的 per-token orderbook 聚合压缩成一个 book：

- **YES token 的 best_bid/ask ≠ NO token 的 best_bid/ask。**
  Polymarket 二元市场中，YES 价格 + NO 价格 ≈ 1（存在 spread 和 fee），
  两个 token 各有独立的 L2 orderbook，不可简单合并。

- 当前 `BookSnapshot` 没有 `token_id` 字段、没有 `outcome` 字段（YES/NO side）、
  没有 per-token 的 imbalance/spread，无法携带双边信息。

- **丢失的信息**：
  1. 哪个 outcome（YES/NO）的报价是 best_bid/best_ask——不知道这是 YES 侧还是 NO 侧。
  2. 两个 token 各自的 imbalance/spread——二者独立，对定价信号影响不同。
  3. per-token 的 sequence_no / gap_count——现在 sequence_no 是单个 int64，
     无法区分 YES 还是 NO token 的 gap 状态。

**老板质疑是正确的：debug_api 用 per-condition 单一 book 表达 per-token 双 book 确实丢了信息。**

---

## 5. 正确的暴露方式建议

建议分两个层次修正，优先级从高到低：

### 5.1 最小修正（推荐，不破坏现有接口）

在 `BookSnapshot` 中增加 `token_id` 和 `outcome` 字段，允许 caller 指定查哪个 token：

```
/api/v1/book/{condition_id}?token=yes   → YES token 的 book
/api/v1/book/{condition_id}?token=no    → NO token 的 book
/api/v1/book/{condition_id}             → 默认返回 YES token（或主 token）
```

`BookSnapshot` 增加：
```cpp
std::string token_id;    // 实际 asset_id（per-token 唯一标识）
std::string outcome;     // "YES" / "NO"
```

`StateProvider::book()` 签名变为：
```cpp
virtual BookSnapshot book(const std::string& condition_id,
                          std::string_view token_side = "yes") const = 0;
```

### 5.2 更完整方案（架构层面，供老李/老周评审）

新增 `BinaryMarketBookView`：

```cpp
struct BinaryMarketBookView {
    bool found{false};
    std::string condition_id;
    BookSnapshot yes_book;   // YES token 的完整 book（含 token_id）
    BookSnapshot no_book;    // NO token 的完整 book（含 token_id）
    FourTs ts{};
};
```

路由 `/api/v1/book/{condition_id}` 返回上述结构，前端看板可同时展示 YES/NO 双侧深度。

### 5.3 BookSnapshot 中 sequence_no 字段澄清

当前 `BookSnapshot.sequence_no` 是 int64 单值，接入时需要明确"这是哪个 token 的 sequence_no"。
建议 yes_sequence_no + no_sequence_no 分开，或在 per-token 方案中各自携带。

---

## 6. 总结

| 维度 | 事实 |
|------|------|
| 底层 orderbook key 粒度 | per-token_id（asset_id），YES 和 NO 各一个独立的 TokenState |
| CLOB WSS book/price_change 的消息 key | asset_id（= token_id），不是 condition_id |
| 订阅粒度 | market channel 订 token_id（双 token 同订）；user channel 订 condition_id |
| debug_api BookSnapshot 粒度 | per-condition_id（盘口级），单一 best_bid/ask，无 token/outcome 区分 |
| 当前两者是否已接入 | 否，BookSnapshot 目前是 Stub/Demo，未消费真实 OrderBookAdapter |
| 接入时是否会丢信息 | 是，YES/NO 各自的 book 被压成一个，outcome/token 标识完全丢失 |
| 修正方向 | BookSnapshot 增加 token_id + outcome 字段，或拆出 BinaryMarketBookView（双 token view）|

**老板质疑成立：底层是 per-token（asset_id 粒度），debug_api 暴露成 per-condition 单一 book，
信息粒度不匹配，接入后会丢失 outcome 标识和双侧独立的 microstructure 信号。
正确修正是接口改为 per-token 查询，或在 per-condition 接口内拆出 YES/NO 双 book 字段。**

具体接口设计变更需老李（spec owner）和老周（架构）评审决策，由小卢（debug_api owner）实施。
