# WSS Subscriber 工程实施 Spec v1

- **Owner**: 老李 (polymarket-protocol-expert, #07)
- **Date**: 2026-05-29
- **Last review**: 2026-05-29
- **Sprint**: W9 Wave 63 P1 (小米 audit §4 缺失 doc 补交)
- **受众**: 小冯 (#34, 主实施) / 老周 (#02, 架构 review) / 小余 (D-单元主管, W9 W3 ack)
- **ADR-027 cite**:
  ```
  cite:
    - polymarket_ssot: docs/RESEARCH/laoli-w8-polymarket-data-structure-ssot-v1.md (§4)
    - goalserve_ssot:  docs/RESEARCH/xiaoduan-goalserve-data-structure-ssot-v1.md   (§7.1)
  ```
- **WebFetch 来源**: docs.polymarket.com (尝试抓取; 官方 REST/WSS 协议以本项目实测 + py-clob-client SDK 源码为 SSOT, 见 §0 说明)
- **关联文档**:
  - `docs/RESEARCH/laoli-w8-polymarket-data-structure-ssot-v1.md` §4 (WSS event 数据结构)
  - `docs/RESEARCH/laoli-polymarket-endpoint-matrix-v3.md` §C (4-5 conn 拓扑)
  - `include/stcpp/polymarket/wss/pm_wss_subscriber.hpp` (现 PMWssSubscriber, 覆盖 sports-api host)
  - `docs/ADR/2026-06-W4-adr-027-core-data-structure-ssot-enforce.md` (Enforce-1 SSOT cite)

---

## §0 协议来源说明 (R-33 四维扫描)

**WebFetch 状态 @ 2026-05-29**: docs.polymarket.com 为 Polymarket 官方文档站。本文协议规格来自以下四维:

1. **官方 portal**: https://docs.polymarket.com — 已索引; 截至 2026-05-29 CLOB WSS 路径在 /developers/clob 节; sports-api WSS 无单独公开 reference page, 以实测为准
2. **一手 SDK 源码**: `py-clob-client` (Python, PyPI `py-clob-client>=0.20.0`); `clob-client` (TypeScript, github.com/Polymarket/clob-client). 两者 WSS subscribe payload 格式吻合本文 §2/§3
3. **实测 RTT 行为**: 见 `laoli-polymarket-endpoint-matrix-v3.md` §4 + `laoli-polymarket-data-structure-ssot-v1.md` §4; 500 token 单 conn 16s 收 1014 msg ≈ 63 msg/s 稳态
4. **SSOT cross-reference**: `laoli-w8-polymarket-data-structure-ssot-v1.md` §4 (market channel / user channel / sports channel 三路) 为权威字段表, 本文不重写字段表, 仅补实施 spec

**协议地址汇总 (R-33 锁定)**:

| channel | URL | 鉴权 |
|---|---|---|
| market channel (orderbook) | `wss://ws-subscriptions-clob.polymarket.com/ws/market` | 无 |
| user channel (我的订单/成交) | `wss://ws-subscriptions-clob.polymarket.com/ws/user` | payload 内 apiKey/secret/passphrase |
| sports inplay channel | `wss://sports-api.polymarket.com/ws` | 无 (R-33 第 5 host) |

---

## §1 架构定位

**现有 PMWssSubscriber** (`include/stcpp/polymarket/wss/pm_wss_subscriber.hpp`) 已实现:
- 连接 `wss://sports-api.polymarket.com/ws` (R-33 第 5 host)
- 8 sub topic 解析 + SPSC sink + reconnect + heartbeat + 4-ts R-20
- W5 Wave 24 交付, W9 单测已通过

**本 spec 新增**: `PolymarketCLOBSubscriber` — 专门覆盖 CLOB 两路 WSS:
- market channel: `wss://ws-subscriptions-clob.polymarket.com/ws/market`
- user channel: `wss://ws-subscriptions-clob.polymarket.com/ws/user`

**为何新建而不复用 PMWssSubscriber**:

| 维度 | PMWssSubscriber | PolymarketCLOBSubscriber (新) |
|---|---|---|
| endpoint | sports-api.polymarket.com/ws | ws-subscriptions-clob.polymarket.com/ws/{market,user} |
| 订阅单位 | condition_id / event_id | market channel: **token_id** (assets_ids); user channel: condition_id (markets) |
| 鉴权 | 无 | user channel payload 内 apiKey/secret/passphrase |
| event schema | sports inplay JSON | CLOB orderbook diff / trade event JSON |
| 连接数 | 1 (sports) | 2 (market hot) + 1-2 (market cold) + 1 (user) = 3-4 per §C.2 拓扑 |

两者职责正交, 不合并. 老周 v0.4 §17 拓扑 T0a/T0b/T0c 对应 PolymarketCLOBSubscriber 的 3 个 conn 实例.

---

## §2 market channel — subscribe payload

**endpoint**: `wss://ws-subscriptions-clob.polymarket.com/ws/market`

### §2.1 订阅 payload

```json
{
  "type": "Market",
  "assets_ids": [
    "79394...",
    "40471..."
  ]
}
```

**字段约束**:

| 字段 | 类型 | 语义 | 坑 |
|---|---|---|---|
| `type` | string | 固定 `"Market"` (大写 M) | 小写 `"market"` 不识别 |
| `assets_ids` | `[]string` | token_id 数组 (uint256 decimal string) | **不是** condition_id; 复数有 s |

**双 token 同订规则**: 同一 condition (market) 的两个 outcome (YES/NO) 必须**同时列入 assets_ids**. 单订一个 token 完全感知不到另一边 orderbook 变动, 做市双边价必须同时订. 见 SSOT §5 T-02.

**capacity sizing** (老周 v0.3 §C.3 实测):
- hot conn (T0a): 300-500 token, 150-300 msg/s
- cold conn (T0b): 2000-3500 token, 50 msg/s
- 分片规则见 `laoli-polymarket-endpoint-matrix-v3.md` §C.5 (hot/cold 定义)

**hot/cold 动态迁移**: token 从 cold conn 迁到 hot conn = `UNSUBSCRIBE cold` + `SUBSCRIBE hot`, 两步接力. 迁移 hysteresis window = 5min (防抖).

### §2.2 market channel event types

| event_type | 触发时机 | 关键字段 |
|---|---|---|
| `book` | 订阅时立即发 snapshot; 偶尔全量重传 | `market`(condition_id), `asset_id`(token_id), `timestamp`(ms), `hash`, `bids:[{price,size}]`, `asks:[{price,size}]`, `tick_size`, `last_trade_price` |
| `price_change` | 挂单/撤单/部分成交导致 depth 变化 | `market`, `asset_id`, `price_changes:[{price,side,size}]`, `timestamp` |
| `last_trade_price` | 单笔完整成交 | `market`, `asset_id`, `price`, `size`, `side`, `timestamp` |
| `tick_size_change` | tick size 被动态调整 (罕见) | `market`, `asset_id`, `tick_size`, `timestamp` |

**`book` snapshot 标志**: `is_snapshot` 逻辑 = 判 bids/asks 非空且无 `price_changes` 字段. C++ 层用 `WssEvent.payload.book.is_snapshot` bit 区分 (见现有 wss_event.hpp).

**`price`/`size` 类型**: wire 上均为 **string**, 内容是 decimal (e.g. `"0.55"`, `"100.0"`). C++ 解析必须 `std::from_chars` 而非 `atof`/`stof` (精度陷阱).

**`timestamp` 语义**: Unix **毫秒** (int64 string 形式, e.g. `"1748390400000"`). 对应 R-20 `data_source_ts` = 此值 × 1e6 ns. 不可用本地 `now()` 替代.

---

## §3 user channel — subscribe payload

**endpoint**: `wss://ws-subscriptions-clob.polymarket.com/ws/user`

### §3.1 订阅 payload (鉴权在 payload 内, 不在 HTTP header)

```json
{
  "type": "User",
  "auth": {
    "apiKey": "<POLY_API_KEY>",
    "secret": "<POLY_API_SECRET>",
    "passphrase": "<POLY_API_PASSPHRASE>"
  },
  "markets": [
    "0xa9db6005902...",
    "0xdb39..."
  ]
}
```

**字段约束**:

| 字段 | 类型 | 语义 | 坑 |
|---|---|---|---|
| `type` | string | 固定 `"User"` (大写 U) | |
| `auth.apiKey` | string | POLY_API_KEY (从 .env 读) | **不是** HMAC 鉴权, payload 明文传; WSS 走 TLS, 不降级 |
| `auth.secret` | string | POLY_API_SECRET | 同上 |
| `auth.passphrase` | string | POLY_API_PASSPHRASE | 同上 |
| `markets` | `[]string` | **condition_id 数组** (bytes32 hex, 0x 前缀) | 不是 token_id! |

**静默问题**: user channel 无事件时不推送任何消息, **不等于** 连接健康. 必须靠应用层 heartbeat 维持感知. 见 §5.

### §3.2 user channel event types

| event_type | 触发 | 关键字段 |
|---|---|---|
| `trade` | 我的订单被成交 (maker 或 taker fill) | `order_id`, `market`(condition_id), `asset_id`(token_id), `price`, `size`, `side`(`BUY`/`SELL`), `fee`, `timestamp` |
| `order` | 订单状态变更 | `order_id`, `market`, `asset_id`, `status`(`PLACED`/`MATCHED`/`CANCELED`/`EXPIRED`), `timestamp` |

**`fee` 字段**: 字符串 decimal USDC. 体育市场 taker fee = 3% (feeSchedule.rate=0.03), maker fee = 0. 风控 PnL ledger 必须记录. 见 SSOT §3.2 `feeSchedule`.

---

## §4 sequence_no 单调性

market channel 和 user channel 的 `book` / `price_change` 事件中均含可选字段 `sequence_no` (uint64, 不保证每帧都有). 语义:

- 同一 `asset_id` 的 `sequence_no` 必须严格单调递增
- 收到 `sequence_no` 跳号 (gap > 1) → 该 token orderbook 状态可能不一致 → 触发 **重新订阅** (unsubscribe + subscribe) 拿新 snapshot
- 重连后 sequence_no 重置从 0 开始, 不跨连接比较

**C++ 实施**: 每个 token_id 维护 `last_seq[token_id]`; 收帧时 `if (seq > 0 && seq != last_seq + 1) → gap → trigger_resubscribe()`.

---

## §5 reconnect / heartbeat 策略

### §5.1 reconnect — exponential backoff

| 参数 | 值 | 来源 |
|---|---|---|
| `reconnect_initial` | 1000ms | endpoint-matrix v3 §C.6 |
| `reconnect_cap` | 30000ms | 同上 |
| `reconnect_multiplier` | 2.0× | 同上 |
| 序列 | 1s, 2s, 4s, 8s, 16s, 30s, 30s... | 软 cap |

**重连后必做**:
1. 重发 subscribe payload (market channel 重发 assets_ids; user channel 重发含 auth + markets)
2. 重置 sequence_no 状态
3. 等收到第一个 `book` snapshot 后才开始接受 `price_change` diff

### §5.2 heartbeat

| 参数 | 值 |
|---|---|
| application-level PING 间隔 | 10s (`heartbeat_interval`) |
| PONG / any frame 无响应超时 | 30s (`heartbeat_timeout`) |
| 超时动作 | `Close()` + 触发 reconnect |
| PING payload | `"PING"` (text frame) |

**user channel 静默不等于健康**: 无订单事件时 user channel 不发任何帧. 10s PING 是唯一存活探测手段. 30s 无任何帧 (含 PONG) → 强重连.

---

## §6 4-ts R-20 契约 (强制)

```
event_ts  ≤  data_source_ts  ≤  ingestion_ts  ≤  as_of_ts
```

| ts 字段 | 来源 | 单位 |
|---|---|---|
| `event_ts` | 无直接字段; 用 `data_source_ts` 代填 (两者同源) | ns |
| `data_source_ts` | WSS frame `timestamp` 字段 (ms) × 1e6 | ns |
| `ingestion_ts` | transport callback 入口本地 `CLOCK_MONOTONIC_RAW` | ns |
| `as_of_ts` | SPSC TryPush 前本地 `CLOCK_MONOTONIC_RAW` | ns |

**禁止**: 用本地 `now()` 填 `data_source_ts`. 若 frame 缺 `timestamp` 字段 → parse error, drop frame, 不 fallback 到 now().

**未来时间戳校验**: `data_source_ts > ingestion_ts + 5s` → reject frame + `frames_parse_error_total++`. 现有 PMWssSubscriber T5 单测已覆盖此逻辑, PolymarketCLOBSubscriber 复用同样校验.

---

## §7 单 socket 多 token 订阅

market channel 支持单 WebSocket 连接内追加订阅 (无需重连):

```json
{"type":"Market","assets_ids":["<new_token_id_1>","<new_token_id_2>"]}
```

- 追加订阅后立即收到这批新 token 的 `book` snapshot
- 取消订阅: 无官方 unsubscribe frame — 唯一方法是**关闭并重连**, 重连时只订需要的 token
- **实际 hot/cold 迁移**: cold → hot 用 "hot conn 追加订阅新 token + cold conn 断开重连去掉已迁 token" 两步

**token 数上限** (实测推断): 单 conn 无硬限制文档, 实测 500 token 稳态 63 msg/s 正常. 工程约束: hot conn ≤ 500 token, cold conn ≤ 3500 token (老周 §C.3).

---

## §8 C++ class skeleton — PolymarketCLOBSubscriber (spec, 非实现)

以下是给小冯 W9 W3 实施的接口 spec. 工程师写 cpp 实现, 本 doc 只定义 wire 契约和接口形态.

```cpp
// include/stcpp/polymarket/clob_wss/polymarket_clob_subscriber.hpp
//
// Owner: 小冯 (#34) 实施, 老李 (#07) spec, 老周 (#02) 架构 review
// 与 PMWssSubscriber 区分: 本类覆盖 ws-subscriptions-clob.polymarket.com 两路
// PMWssSubscriber 覆盖 sports-api.polymarket.com/ws (R-33 第 5 host)
//
// ADR-027 cite: laoli-w8-polymarket-data-structure-ssot-v1.md §4

namespace stcpp::polymarket::clob_wss {

// -- PolymarketCLOBSubscriberConfig ------------------------------------------
//
// 老周 v0.4 §17 拓扑对应关系:
//   market_hot_url  → T0a poly_market_hot_reactor  (1 conn, 300-500 token)
//   market_cold_url → T0b poly_market_cold_reactor (1-2 conn, 2500-3500 token)
//   user_url        → T0c poly_user_reactor         (1 conn, N condition_id)
//
struct PolymarketCLOBSubscriberConfig {
    // market channel
    std::string market_url = "wss://ws-subscriptions-clob.polymarket.com/ws/market";
    // user channel
    std::string user_url   = "wss://ws-subscriptions-clob.polymarket.com/ws/user";

    // reconnect (复用 PMWssSubscriberConfig 参数规格)
    std::chrono::milliseconds reconnect_initial    {1000};
    std::chrono::milliseconds reconnect_cap        {30000};
    double                    reconnect_multiplier {2.0};

    // heartbeat
    std::chrono::milliseconds heartbeat_interval   {10000};
    std::chrono::milliseconds heartbeat_timeout    {30000};

    // user channel 凭证 (从 .env 读取, 不落日志)
    std::string api_key;
    std::string api_secret;
    std::string api_passphrase;

    // 初始订阅 (启动后立即发)
    std::vector<std::string> initial_market_token_ids;     // token_id, market channel
    std::vector<std::string> initial_user_condition_ids;   // condition_id, user channel
};

// -- PolymarketCLOBSubscriber ------------------------------------------------
//
// 职责:
//   1. 管理 market channel + user channel 两个独立 IWssTransport 实例
//   2. market channel: subscribe assets_ids (token_id 粒度), 解析 book/price_change/
//      last_trade_price/tick_size_change, push WssEvent 到 SPSC sink
//   3. user channel: subscribe markets (condition_id 粒度) + auth payload,
//      解析 trade/order 事件, push 到独立 user SPSC sink (或同 sink 不同 SubTopic)
//   4. sequence_no gap 检测 → trigger resubscribe (§4)
//   5. reconnect exp backoff (§5.1), heartbeat (§5.2)
//   6. R-20 4-ts 填充: data_source_ts = frame.timestamp × 1e6 (禁 now() 替代)
//   7. 暴露 last_msg_ts_ns() 给 RM STALE 检测 (老韩 v0.3 §16)
//
// 线程模型 (R-12 严格):
//   transport callback 线程 (= vCPU0) 严禁 block > 100us
//   SPSC TryPush 非阻塞; 满则 drop + metric
//
class PolymarketCLOBSubscriber {
public:
    PolymarketCLOBSubscriber(
        std::unique_ptr<IWssTransport>  market_transport,
        std::unique_ptr<IWssTransport>  user_transport,
        std::shared_ptr<ISpscEventSink> market_sink,
        std::shared_ptr<ISpscEventSink> user_sink,
        PolymarketCLOBSubscriberConfig  cfg);

    PolymarketCLOBSubscriber(const PolymarketCLOBSubscriber&) = delete;
    PolymarketCLOBSubscriber& operator=(const PolymarketCLOBSubscriber&) = delete;

    bool Start();    // 两个 transport 并发 AsyncConnect
    void Stop() noexcept;

    // 动态订阅 (hot token 追加, 无需重连)
    bool SubscribeMarketTokens(std::span<const std::string> token_ids);
    // user channel 动态追加 condition_id
    bool SubscribeUserMarkets(std::span<const std::string> condition_ids);

    // hot/cold 迁移: cold → hot
    // 1. hot conn 追加订阅 token_ids
    // 2. cold conn 重连去掉这批 token_ids (下次重连时 cold subscribe payload 不含这批)
    void MigrateTokensHotToCold(std::span<const std::string> token_ids) noexcept;

    [[nodiscard]] std::int64_t last_market_msg_ts_ns() const noexcept;
    [[nodiscard]] std::int64_t last_user_msg_ts_ns()   const noexcept;
    [[nodiscard]] const SubscriberMetrics& market_metrics() const noexcept;
    [[nodiscard]] const SubscriberMetrics& user_metrics()   const noexcept;

private:
    // market channel handlers
    void OnMarketFrame(std::string_view payload, std::int64_t recv_ts_ns);
    bool ParseBook(std::string_view body,        std::int64_t recv_ts_ns, WssEvent& ev);
    bool ParsePriceChange(std::string_view body, std::int64_t recv_ts_ns, WssEvent& ev);
    bool ParseLastTrade(std::string_view body,   std::int64_t recv_ts_ns, WssEvent& ev);
    bool ParseTickChange(std::string_view body,  std::int64_t recv_ts_ns, WssEvent& ev);

    // user channel handlers
    void OnUserFrame(std::string_view payload,   std::int64_t recv_ts_ns);
    bool ParseTrade(std::string_view body,       std::int64_t recv_ts_ns, WssEvent& ev);
    bool ParseOrder(std::string_view body,       std::int64_t recv_ts_ns, WssEvent& ev);

    // subscribe frame builders
    std::string MakeMarketSubscribeFrame(std::span<const std::string> token_ids) const;
    std::string MakeUserSubscribeFrame(std::span<const std::string> condition_ids) const;

    // sequence_no gap detection
    bool CheckSequenceGap(std::string_view token_id, std::uint64_t seq);
    void TriggerResubscribe(std::string_view token_id);

    std::unique_ptr<IWssTransport>  market_transport_;
    std::unique_ptr<IWssTransport>  user_transport_;
    std::shared_ptr<ISpscEventSink> market_sink_;
    std::shared_ptr<ISpscEventSink> user_sink_;
    PolymarketCLOBSubscriberConfig  cfg_;
    SubscriberMetrics               market_metrics_;
    SubscriberMetrics               user_metrics_;

    // sequence_no per-token state (unordered_map, vCPU0 single-thread access, no lock needed)
    std::unordered_map<std::string, std::uint64_t> token_seq_map_;
    // hot token set (for migration logic)
    std::unordered_set<std::string> hot_token_ids_;
    // cold token set
    std::unordered_set<std::string> cold_token_ids_;
};

}  // namespace stcpp::polymarket::clob_wss
```

---

## §9 已知坑清单 (协议 owner 警示)

| # | 坑 | 正解 | 来源 |
|---|---|---|---|
| P-01 | `assets_ids` 只订一个 token 以为能监控整个 market | 两个 token 必须同时订, YES 和 NO orderbook 完全独立 | SSOT §5 T-02 |
| P-02 | `price`/`size` wire 上是 string, 用 `atof` 解析 | `std::from_chars`, 不用 locale-dependent 函数 | SSOT §3.4 |
| P-03 | `timestamp` 单位是 ms 不是 s | × 1e6 才是 ns; 用 s 会导致 4-ts 单调性检查失败 | SSOT §4.1 |
| P-04 | user channel 静默 = 健康 | 无事件时不推任何帧; 靠 10s PING 探活 | SSOT §4.3 |
| P-05 | `book` snapshot 后直接接受 `price_change` diff | 必须等到 `book` snapshot 收到并应用, 才接受后续 diff; 重连时尤其注意 | 协议语义 |
| P-06 | `sequence_no` 跳号不处理 | 跳号 → resubscribe, 不猜 missing diff | §4 |
| P-07 | user channel 订阅用 token_id | user channel `markets` 字段传 condition_id, 不是 token_id | SSOT §4.2 |
| P-08 | 重连后不重发 subscribe payload | 重连后 server 不记忆上次订阅; 必须重发 | §5.1 |
| P-09 | 凭证 (apiKey/secret/passphrase) 出现在日志 | user channel auth payload 严禁 log; 启动时 mask | 红线: 私钥明文落日志 |
| P-10 | 追加订阅后未等 `book` snapshot 直接用 `price_change` | 追加新 token 后同样要等 snapshot; 用 token 维度的 "snapshot received" flag | §2.1 |

---

## §10 派单 / 接力

| 任务 | 受理人 | 截止 | 依据 |
|---|---|---|---|
| PolymarketCLOBSubscriber C++ 实施 (§8 skeleton + CLOB wire 两路) | 小冯 (#34) | W9 W3 | 本 spec §8 |
| wss_event.hpp 补 CLOB event types (ClobBook / ClobPriceChange / ClobTrade / ClobOrder) | 小冯 (#34) | W9 W3 | §2.2 §3.2 |
| PolymarketCLOBSubscriber 架构 review | 老周 (#02) | W9 W3 ack | §8 接口形态 |
| hot/cold token 分类 worker 接口 (vCPU3 → T0a/T0b 迁移 SPSC 通知) | 小余 (D 主管) | W9 W4 | §7 迁移逻辑 |
| 补 test_clob_subscriber.cpp (单测覆盖 P-01~P-10) | 小冯 (#34) | W9 W3 | §9 坑 |

---

**最后更新**: 2026-05-29 by 老李 (W9 Wave 63 P1, 小米 audit §4 补交)
