// stcpp/polymarket/pm_client.hpp — PolymarketClient v0.1 抽象接口
//
// Owner: 老李 (polymarket-protocol-expert, #07)
// Sprint-2 W5 Wave 24 (W5-A-01 / 老周 manager mandate §4.1 W5-01-A)
//
// 落:
//   docs/RESEARCH/laoli-polymarket-backend-requirements-v1.md §1 (paper/live 矩阵 12 概念)
//                                                        §3.1 (PMClient 14 接口 F-01..F-14)
//                                                        §3.2 (PMError 9 kind)
//                                                        §4   (4 ts R-20 字段位置)
//                                                        §5   (HMAC 4 bug enforce)
//                                                        §6.1 (OrderStatus 7 enum)
//                                                        §6.2 (状态机)
//                                                        附录 B (5 PM host)
//   docs/RESEARCH/laozhou-architecture-v0.6-e2e.md       §3.1 (OrderIntent 字段)
//                                                        §3.2 (4 ts 透传矩阵)
//                                                        §17  (vCPU0 4-5 conn 拓扑)
//   docs/RESEARCH/laoli-polymarket-endpoint-matrix-v3.md §A   (HMAC 4 bug + sigType=1)
//
// 红线:
//   R-7  paper / live CMake target 物理隔离, 此 header 仅放抽象基类 + 数据 struct;
//        实现走 `paper/paper_pm_client.cpp` (paper binary) 或 `live/live_pm_client.cpp` (live binary).
//   R-11 paper 实现 OrderAck.audit_wal_kind 硬填 PaperAudit, 严禁污染 RiskAudit / Position.
//   R-12 14 接口全部同步契约, caller 在 worker pool 调; signer 不可在 WSS event loop 直调.
//   R-20 OrderBookSnapshot / OrderAck / MarketInfo / Trade / Position 全部嵌入 TimestampQuad,
//        优先 UPSTREAM_PAYLOAD; 实在没源走 INFERRED_FROM_INGESTION 并显式标 enum.
//   HMAC R1-R4 (v3 §A): live binary live_pm_client.cpp 落实 (本 header 仅 reserve 字段);
//                       paper 不签真签名但 SignedOrder.signature 字段保留以便 live 接入 byte-equal.
//
// 不耻下问:
//   - SPSC ring 拓扑 + 4 ts 透传 → @老周 (architecture v0.6)
//   - paper_audit WAL 接入 → @小蒋 (paper engine), @老王 (WAL framework)
//   - HMAC 4 bug + sigType=1 + L1 EIP-712 → @老孙 (signer)
//   - RM 上游 RiskGateway evaluate → @老韩 (RiskManager v0.3)
//
// CMake 守门 (顶层 + src/stcpp/polymarket/CMakeLists.txt 双层):
//   if(STCPP_EXEC_MODE STREQUAL "paper") link stcpp_polymarket_paper
//   if(STCPP_EXEC_MODE STREQUAL "live")  link stcpp_polymarket_live (M5+ stub only now)

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "stcpp/execution/execution_mode.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"

namespace stcpp::polymarket {

// ---------- TimestampQuad (R-20, 与 stcpp::core::TimestampQuad 一致, 复制本地避免 core 头依赖) ----------

enum class DataSourceTsSource : std::uint8_t {
    UpstreamPayload          = 0,  // payload 自带 `timestamp` 字段 (PM WSS / clob /books 等)
    UpstreamHeader           = 1,  // HTTP Response Header `Date` 解出 (gamma listing 兜底)
    InferredFromDsTs         = 2,  // 同 batch 内 ds_ts 推断 (outcomes settle event)
    InferredFromIngestion    = 3,  // 实在没源, 走本地 ingestion_ts (月度 sweep > 5% 报警)
};

[[nodiscard]] constexpr std::string_view ToString(DataSourceTsSource s) noexcept {
    switch (s) {
        case DataSourceTsSource::UpstreamPayload:       return "UPSTREAM_PAYLOAD";
        case DataSourceTsSource::UpstreamHeader:        return "UPSTREAM_HEADER";
        case DataSourceTsSource::InferredFromDsTs:      return "INFERRED_FROM_DS_TS";
        case DataSourceTsSource::InferredFromIngestion: return "INFERRED_FROM_INGESTION";
    }
    return "unknown";
}

struct TimestampQuad {
    std::int64_t       event_ts_ns{0};         // R-20: upstream payload (game.last_update / market.timestamp)
    std::int64_t       data_source_ts_ns{0};   // R-20: PM server emit ts (payload.timestamp × 1e6)
    std::int64_t       ingestion_ts_ns{0};     // R-20: vCPU0 recv stamp (CLOCK_REALTIME)
    std::int64_t       as_of_ts_ns{0};         // R-20: caller 决策快照 (Signal / RM)
    DataSourceTsSource ds_ts_source{DataSourceTsSource::UpstreamPayload};
};

// ---------- OrderStatus (7 enum, v3 §B 实测 + 官方 SDK) ----------

enum class OrderStatus : std::uint8_t {
    Booked             = 0,  // 上 book 等撮合 (maker 状态, 非终态)
    PartiallyFilled    = 1,  // 部分成交, 余量在 book (非终态)
    Filled             = 2,  // 全成交 (终态)
    Canceled           = 3,  // 用户撤单 (终态)
    Expired            = 4,  // expiration 到期 (终态)
    Rejected           = 5,  // 不合法被拒 (终态, payload.reject_reason)
    Settled            = 6,  // 比赛结束 + CTF redeem 完成 (终态)
};

inline constexpr std::size_t kOrderStatusCount = 7;

[[nodiscard]] constexpr std::string_view ToString(OrderStatus s) noexcept {
    switch (s) {
        case OrderStatus::Booked:          return "BOOKED";
        case OrderStatus::PartiallyFilled: return "PARTIALLY_FILLED";
        case OrderStatus::Filled:          return "FILLED";
        case OrderStatus::Canceled:        return "CANCELED";
        case OrderStatus::Expired:         return "EXPIRED";
        case OrderStatus::Rejected:        return "REJECTED";
        case OrderStatus::Settled:         return "SETTLED";
    }
    return "unknown";
}

[[nodiscard]] constexpr bool IsTerminal(OrderStatus s) noexcept {
    switch (s) {
        case OrderStatus::Filled:
        case OrderStatus::Canceled:
        case OrderStatus::Expired:
        case OrderStatus::Rejected:
        case OrderStatus::Settled:
            return true;
        case OrderStatus::Booked:
        case OrderStatus::PartiallyFilled:
            return false;
    }
    return false;
}

// 状态机合法转移 (§6.2): Booked → {PartiallyFilled, Filled, Canceled, Expired, Rejected}
//                       PartiallyFilled → {Filled, Canceled, Expired}
//                       Filled → {Settled}  (+ ~4h settle 后)
[[nodiscard]] constexpr bool IsLegalTransition(OrderStatus from, OrderStatus to) noexcept {
    if (IsTerminal(from) && from != OrderStatus::Filled) {
        return false;  // 已终态 (除 Filled) 不可转移
    }
    switch (from) {
        case OrderStatus::Booked:
            return to == OrderStatus::PartiallyFilled || to == OrderStatus::Filled
                || to == OrderStatus::Canceled        || to == OrderStatus::Expired
                || to == OrderStatus::Rejected;
        case OrderStatus::PartiallyFilled:
            return to == OrderStatus::Filled || to == OrderStatus::Canceled
                || to == OrderStatus::Expired;
        case OrderStatus::Filled:
            return to == OrderStatus::Settled;
        case OrderStatus::Canceled:
        case OrderStatus::Expired:
        case OrderStatus::Rejected:
        case OrderStatus::Settled:
            return false;
    }
    return false;
}

// ---------- PMErrorKind (9 enum, §3.2) ----------

enum class PMErrorKind : std::uint8_t {
    Ok                   = 0,
    NotAuthenticated     = 1,  // HMAC 401, 走 v3 §B SOP, 5min 内禁说 "key 失效"
    RateLimited          = 2,  // 429, 退避 + 自我限流
    ServerError          = 3,  // 5xx, 老韩 STALE 判定
    BadRequest           = 4,  // 400, signing / sigType=2 bug 等
    NotFound             = 5,  // 404
    NetworkError         = 6,  // 断 / 超时, 老姜 STALE 5 档
    Stale                = 7,  // payload data_source_ts 落后超阈 (R-20)
    InvariantViolation   = 8,  // 语义违反 (outcome_index=999 误用 / next_cursor=LTE= 兜底失败)
    Unknown              = 9,
};

inline constexpr std::size_t kPMErrorKindCount = 10;  // Ok + 9 失败

[[nodiscard]] constexpr std::string_view ToString(PMErrorKind k) noexcept {
    switch (k) {
        case PMErrorKind::Ok:                 return "Ok";
        case PMErrorKind::NotAuthenticated:   return "NotAuthenticated";
        case PMErrorKind::RateLimited:        return "RateLimited";
        case PMErrorKind::ServerError:        return "ServerError";
        case PMErrorKind::BadRequest:         return "BadRequest";
        case PMErrorKind::NotFound:           return "NotFound";
        case PMErrorKind::NetworkError:       return "NetworkError";
        case PMErrorKind::Stale:              return "Stale";
        case PMErrorKind::InvariantViolation: return "InvariantViolation";
        case PMErrorKind::Unknown:            return "Unknown";
    }
    return "unknown";
}

struct PMError {
    PMErrorKind   kind{PMErrorKind::Ok};
    int           http_status{0};        // 0 = 非 HTTP / 无 status (e.g. NetworkError)
    std::string   body;                  // 老沈 enforce: 已 redact HMAC sig / apiKey / passphrase
    std::int64_t  observed_ts_ns{0};     // 本地观测到错误的 ts (CLOCK_REALTIME)
};

// ---------- OrderBookSnapshot (F-01 输出, L5 yes 双侧) ----------
//
// 与 PM WSS sports channel `market:{condition_id}` payload 对齐 (§2.2).
// L5 = book depth 5 档 (Polymarket 实测 L5 足够覆盖 99% 决策路径).

struct PriceLevel {
    std::uint32_t price_bps{0};      // 0..10000 (Polymarket 0.0..1.0 概率 × 10000)
    std::uint64_t size_usdc_micro{0};// USDC × 1e6
    std::int64_t  level_ts_ns{0};    // 内层 t (price level last update, R-20 data_source 级)
};

inline constexpr std::size_t kBookDepth = 5;

struct OrderBookSnapshot {
    TimestampQuad                              ts;                  // R-20 4 ts
    std::string                                condition_id;        // CTF condition (bytes32 hex)
    std::string                                token_id;            // ERC1155 (uint256 string)
    std::array<PriceLevel, kBookDepth>         yes_bids{};          // 降序 (best bid first)
    std::array<PriceLevel, kBookDepth>         yes_asks{};          // 升序 (best ask first)
    std::uint32_t                              tick_size_bps{10};   // 默认 0.001 = 10 bps (PM tick_size 字段)
    bool                                       neg_risk{false};     // PM negRisk 标识 (CTF v2)
};

// ---------- SignedOrder (F-02 输入) ----------
//
// HMAC 4 bug enforce (v3 §A): live 实现 signature 字段必须 base64 保留 padding;
// signatureType=1 (Magic 1-of-1 Safe), 不可 sigType=2.
// paper mode signature 留空 (mock 不签真签名, 但接口字段 reserve 给 live W5+ patch).

struct SignedOrder {
    std::string   condition_id;
    std::string   token_id;
    std::uint8_t  side{0};                 // 0=BUY (YES), 1=SELL (NO 或 YES 的对侧)
    std::uint32_t limit_price_bps{0};      // 0..10000
    std::uint64_t size_usdc_micro{0};      // USDC × 1e6
    std::int64_t  expiration_unix_s{0};    // 0 = GTC, otherwise epoch seconds
    std::uint32_t signature_type{1};       // HMAC bug #3: 必 sigType=1 (Magic 1-of-1 Safe)
    std::string   signature;               // L1 EIP-712 sig (live 填; paper 留空 reserve)
    std::string   maker_address;           // funder wallet
    std::string   client_order_id;         // 客户端 dedup key

    // R-20 4 ts (caller 注入, RM Allowed 后透传)
    TimestampQuad ts;
};

// ---------- OrderAck (F-02 ~ F-04 输出) ----------

struct OrderAck {
    TimestampQuad        ts;                          // R-20 4 ts (booked_at = data_source_ts)
    std::string          order_id;                    // PM server 返 UUID (paper mock 走 "paper-XXXX")
    std::string          client_order_id;             // echo back
    OrderStatus          status{OrderStatus::Booked};
    PMError              error;                       // 失败时填; status=Rejected 必带 reject_reason
    std::string          reject_reason;               // status=Rejected / error.kind!=Ok 时填
    std::uint64_t        nonce{0};                    // server-issued nonce (paper 走 VirtualNonceProvider)

    // R-11: paper 实现硬绑 PaperAudit (绝不进 RiskAudit / Position)
    infra::wal::WalKind  audit_wal_kind{infra::wal::WalKind::PaperAudit};
};

// ---------- MarketInfo (F-05 输出) ----------

struct MarketOutcome {
    std::string   name;             // "Yes" / "No" 或队伍名
    std::string   token_id;         // ERC1155 outcome token
    std::uint32_t last_price_bps{0};// 最近成交
    double        volume_24h_usdc{0.0};
};

struct MarketInfo {
    TimestampQuad                ts;                    // R-20 4 ts
    std::string                  condition_id;
    std::vector<MarketOutcome>   outcomes;              // 通常 2 个 (Yes/No)
    std::uint32_t                tick_size_bps{10};     // 0.001 = 10 bps
    bool                         neg_risk{false};       // CTF v2 标识
    std::uint16_t                fee_rate_bps{0};       // taker 3% = 300; maker 0
    bool                         accepting_orders{true};
    std::int64_t                 game_start_time_unix_s{0};
    std::vector<std::string>     clob_token_ids;        // listing.markets[i].clob_token_ids
    double                       liquidity_usdc{0.0};   // listing.markets[i].liquidity
};

// ---------- Position (F-06 输出) ----------

struct Position {
    std::string   condition_id;
    std::string   token_id;          // (= asset, data-api 字段别名)
    std::int64_t  size_micro{0};     // signed; 正 long, 负 short
    std::uint32_t avg_price_bps{0};
    std::uint32_t cur_price_bps{0};
    bool          redeemable{false}; // PM 已 settle, 等用户 redeem
    bool          mergeable{false};  // CTF mergePositions 可合并
    TimestampQuad ts;
};

// ---------- Balance (F-07 输出) ----------

struct AllowanceEntry {
    std::string   spender;             // 0xC5d563A36AE78145C45a50134d48A1215220f80a 等 PM exchange
    std::uint64_t allowance_micro{0};  // USDC × 1e6
};

struct Balance {
    TimestampQuad                  ts;
    std::uint64_t                  balance_usdc_micro{0};
    std::vector<AllowanceEntry>    allowances;  // multi-spender (CTF + exchange v2)
};

// ---------- Trade (F-10 输出) ----------

struct Trade {
    TimestampQuad ts;
    std::string   trade_id;
    std::string   order_id;
    std::string   condition_id;
    std::string   token_id;
    std::uint8_t  side{0};
    std::uint32_t price_bps{0};
    std::uint64_t size_usdc_micro{0};
    std::uint64_t fee_usdc_micro{0};
    std::int64_t  match_time_ns{0};   // R-20: event_ts (撮合时刻, payload `match_time`)
};

// ---------- PriceHistoryPoint (F-13 输出) ----------

struct PriceHistoryPoint {
    std::int64_t  bucket_ts_unix_s{0};
    std::uint32_t price_bps{0};
};

// ---------- WSS callback type (F-14) ----------
//
// 4 ts UPSTREAM_PAYLOAD 优先 (R-20). callback 必须在 worker pool 调用 (R-12 不阻 event loop).

using OrderBookCallback = void (*)(const OrderBookSnapshot& snap, void* user_data);

// ---------- IPolymarketClient (14 接口, F-01..F-14) ----------
//
// 全部同步契约 (R-12 caller 在 worker pool 调; signer 不可在 WSS event loop 直调).
// 错误返回走 PMError; 成功路径返回 0/data, 失败路径返回 PMErrorKind != Ok.
// Result<T> = T + PMError, optional<T> 携带成功 payload.

template <typename T>
struct Result {
    std::optional<T> value;
    PMError          error;

    [[nodiscard]] bool ok() const noexcept { return error.kind == PMErrorKind::Ok && value.has_value(); }
};

class IPolymarketClient {
 public:
    virtual ~IPolymarketClient() = default;

    // 标识本实例 mode (paper / live), R-7 防御 caller 误注入
    [[nodiscard]] virtual execution::ExecutionMode Mode() const noexcept = 0;

    // F-01: 取 condition_id 的最新 orderbook snapshot (paper: 从内存 mirror; live: WSS market + REST /books 兜底)
    [[nodiscard]] virtual Result<OrderBookSnapshot> GetOrderbook(std::string_view condition_id) noexcept = 0;

    // F-02: 下单 (paper: VirtualMatcher mock; live: POST /clob/orders + HMAC L2)
    [[nodiscard]] virtual Result<OrderAck> SubmitOrder(const SignedOrder& order) noexcept = 0;

    // F-03: 撤单 (paper: VirtualMatcher cancel; live: DELETE /clob/orders/{id} + HMAC)
    [[nodiscard]] virtual Result<OrderAck> CancelOrder(std::string_view order_id) noexcept = 0;

    // F-04: 撤全部 (paper: 撤所有挂单; live: POST /cancel-all + HMAC)
    [[nodiscard]] virtual Result<std::uint32_t> CancelAll() noexcept = 0;

    // F-05: market 静态元数据 (paper / live both real, 24h cache)
    [[nodiscard]] virtual Result<MarketInfo> GetMarketInfo(std::string_view condition_id) noexcept = 0;

    // F-06: 用户持仓 (paper: PaperLedger; live: GET /data/positions + L1 cache 15s)
    [[nodiscard]] virtual Result<std::vector<Position>> GetUserPositions(std::string_view funder_addr) noexcept = 0;

    // F-07: USDC + allowance (paper: infinite; live: GET /balance-allowance + HMAC, sigType=1)
    [[nodiscard]] virtual Result<Balance> GetBalance() noexcept = 0;

    // F-08: 查 order_id 当前状态 (paper: VirtualMatcher 状态机; live: GET /data/order/{id} + HMAC)
    [[nodiscard]] virtual Result<OrderAck> GetOrderStatus(std::string_view order_id) noexcept = 0;

    // F-09: 我的挂单 (paper: PaperLedger 挂单表; live: GET /data/orders?next_cursor=)
    [[nodiscard]] virtual Result<std::vector<OrderAck>> GetMyOpenOrders() noexcept = 0;

    // F-10: 我的成交 (paper: VirtualMatcher 成交流; live: GET /data/trades?limit=N + HMAC)
    [[nodiscard]] virtual Result<std::vector<Trade>> GetMyTrades(std::uint32_t limit) noexcept = 0;

    // F-11: 派生 API key (paper: 不用; live only, GET /auth/derive-api-key + L1 EIP-712)
    [[nodiscard]] virtual Result<std::string> DeriveApiKey() noexcept = 0;

    // F-12: 列已有 API key (paper: 不用; live only, GET /auth/api-keys + HMAC)
    [[nodiscard]] virtual Result<std::vector<std::string>> ListApiKeys() noexcept = 0;

    // F-13: K 线 (paper / live both real, 公开 endpoint)
    [[nodiscard]] virtual Result<std::vector<PriceHistoryPoint>> GetPricesHistory(
        std::string_view token_id,
        std::int64_t     start_unix_s,
        std::int64_t     end_unix_s) noexcept = 0;

    // F-14: 订阅 sports WSS (both real; paper read-only, paper 不影响 book)
    //   传 callback + user_data 走 C-style 防 std::function 堆分配 (R-12 热路径).
    [[nodiscard]] virtual Result<std::uint32_t> SubscribeSportsWss(
        const std::vector<std::string>& condition_ids,
        OrderBookCallback                cb,
        void*                            user_data) noexcept = 0;
};

}  // namespace stcpp::polymarket
