// stcpp/polymarket/paper/paper_pm_client.cpp — PaperPolymarketClient 14 接口实现
//
// Owner: 老李 (#07) — Sprint-2 W5 Wave 24
// 落: docs/RESEARCH/laoli-polymarket-backend-requirements-v1.md §1.3 / §3.1 / §6 / §7
// 红线: R-7 / R-11 / R-20 / HMAC 4 bug — 见 .hpp 头注

#include "stcpp/polymarket/paper/paper_pm_client.hpp"

#include <algorithm>
#include <cstdio>

#include "stcpp/infra/wal/pit.hpp"

namespace stcpp::polymarket::paper {

namespace {

// PIT 4 ts AssertChain (R-20). signer 模块同款; 复用 pit::NowRealtimeNs() now() 取数源.
// 单测 grep enforce: src/stcpp/polymarket/ 下严禁 `now()` / `clock_gettime` 直调
// (PM payload data_source_ts 必须 UPSTREAM_PAYLOAD).
[[nodiscard]] bool AssertChainTs(const TimestampQuad& ts) noexcept {
    return ts.event_ts_ns       >  0
        && ts.data_source_ts_ns >= ts.event_ts_ns
        && ts.ingestion_ts_ns   >= ts.data_source_ts_ns
        && ts.as_of_ts_ns       >= ts.ingestion_ts_ns
        && ts.as_of_ts_ns       <= infra::wal::pit::NowRealtimeNs();
}

PMError MakeError(PMErrorKind k, int http, std::string body) noexcept {
    PMError e;
    e.kind            = k;
    e.http_status     = http;
    e.body            = std::move(body);
    e.observed_ts_ns  = infra::wal::pit::NowRealtimeNs();
    return e;
}

}  // namespace

std::string PaperPolymarketClient::MakeOrderId() noexcept {
    // paper-<10 digit zero-padded seq> — 一眼区分真 PM order_id (UUID), audit / log 不会混
    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "paper-%010llu",
                  static_cast<unsigned long long>(++next_order_seq_));
    return std::string{buf};
}

// ---------- F-01 GetOrderbook ----------

Result<OrderBookSnapshot> PaperPolymarketClient::GetOrderbook(std::string_view condition_id) noexcept {
    Result<OrderBookSnapshot> r;
    std::lock_guard<std::mutex> lk(mu_);
    auto it = books_.find(std::string{condition_id});
    if (it == books_.end()) {
        r.error = MakeError(PMErrorKind::NotFound, 404, "orderbook not in paper mirror");
        return r;
    }
    r.value = it->second;
    return r;
}

// ---------- F-02 SubmitOrder ----------

Result<OrderAck> PaperPolymarketClient::SubmitOrder(const SignedOrder& order) noexcept {
    Result<OrderAck> r;

    // R-20 PIT chain enforce (TimestampQuad UPSTREAM_PAYLOAD 必须由 caller 注入)
    if (!AssertChainTs(order.ts)) {
        r.error = MakeError(PMErrorKind::InvariantViolation, 0, "PIT chain violation");
        return r;
    }

    // HMAC bug #3 enforce: paper 也防 caller 误传错误 sigType (live 必须 sigType=1, Magic 1-of-1 Safe)
    if (order.signature_type != 1u) {
        OrderAck ack;
        ack.ts              = order.ts;
        ack.client_order_id = order.client_order_id;
        ack.status          = OrderStatus::Rejected;
        ack.reject_reason   = "signature_type must be 1 (Magic 1-of-1 Safe) — HMAC bug #3";
        ack.error           = MakeError(PMErrorKind::BadRequest, 400, ack.reject_reason);
        ack.audit_wal_kind  = infra::wal::WalKind::PaperAudit;  // R-11
        r.value             = ack;
        r.error             = ack.error;
        return r;
    }

    // 业务前置: tick_size / size > 0 / outcome 合理 (paper v0.1 简化)
    if (order.size_usdc_micro == 0 || order.limit_price_bps == 0 || order.limit_price_bps > 10000u) {
        OrderAck ack;
        ack.ts              = order.ts;
        ack.client_order_id = order.client_order_id;
        ack.status          = OrderStatus::Rejected;
        ack.reject_reason   = "size or price out of range (0 < price ≤ 10000 bps, size > 0)";
        ack.error           = MakeError(PMErrorKind::BadRequest, 400, ack.reject_reason);
        ack.audit_wal_kind  = infra::wal::WalKind::PaperAudit;
        r.value             = ack;
        r.error             = ack.error;
        return r;
    }

    std::lock_guard<std::mutex> lk(mu_);
    OrderAck ack;
    ack.ts                = order.ts;
    ack.order_id          = MakeOrderId();
    ack.client_order_id   = order.client_order_id;
    ack.status            = OrderStatus::Booked;       // paper v0.1: 立即 Booked (W5 VirtualMatcher 接入)
    ack.nonce             = next_order_seq_;           // mock nonce = order seq
    ack.audit_wal_kind    = infra::wal::WalKind::PaperAudit;  // R-11 硬绑

    PaperOrderEntry entry;
    entry.ack         = ack;
    entry.original    = order;
    orders_.emplace(ack.order_id, std::move(entry));

    r.value = ack;
    return r;
}

// ---------- F-03 CancelOrder ----------

Result<OrderAck> PaperPolymarketClient::CancelOrder(std::string_view order_id) noexcept {
    Result<OrderAck> r;
    std::lock_guard<std::mutex> lk(mu_);
    auto it = orders_.find(std::string{order_id});
    if (it == orders_.end()) {
        r.error = MakeError(PMErrorKind::NotFound, 404, "order_id not in paper book");
        return r;
    }
    OrderAck& ack = it->second.ack;
    if (IsTerminal(ack.status)) {
        r.error = MakeError(PMErrorKind::InvariantViolation, 0,
                            "cannot cancel terminal order (" + std::string{ToString(ack.status)} + ")");
        return r;
    }
    if (!IsLegalTransition(ack.status, OrderStatus::Canceled)) {
        r.error = MakeError(PMErrorKind::InvariantViolation, 0, "illegal transition to CANCELED");
        return r;
    }
    ack.status         = OrderStatus::Canceled;
    ack.audit_wal_kind = infra::wal::WalKind::PaperAudit;
    r.value            = ack;
    return r;
}

// ---------- F-04 CancelAll ----------

Result<std::uint32_t> PaperPolymarketClient::CancelAll() noexcept {
    Result<std::uint32_t> r;
    std::lock_guard<std::mutex> lk(mu_);
    std::uint32_t count = 0;
    for (auto& [_, entry] : orders_) {
        OrderAck& ack = entry.ack;
        if (!IsTerminal(ack.status) && IsLegalTransition(ack.status, OrderStatus::Canceled)) {
            ack.status = OrderStatus::Canceled;
            ++count;
        }
    }
    r.value = count;
    return r;
}

// ---------- F-05 GetMarketInfo ----------

Result<MarketInfo> PaperPolymarketClient::GetMarketInfo(std::string_view condition_id) noexcept {
    Result<MarketInfo> r;
    std::lock_guard<std::mutex> lk(mu_);
    auto it = markets_.find(std::string{condition_id});
    if (it == markets_.end()) {
        r.error = MakeError(PMErrorKind::NotFound, 404, "market info not cached");
        return r;
    }
    r.value = it->second;
    return r;
}

// ---------- F-06 GetUserPositions ----------

Result<std::vector<Position>> PaperPolymarketClient::GetUserPositions(
    std::string_view /*funder_addr*/) noexcept {
    Result<std::vector<Position>> r;
    std::lock_guard<std::mutex> lk(mu_);
    r.value = positions_;  // paper: 全返 (funder addr 在 paper mode 无意义, 1 个虚拟用户)
    return r;
}

// ---------- F-07 GetBalance ----------

Result<Balance> PaperPolymarketClient::GetBalance() noexcept {
    Result<Balance> r;
    std::lock_guard<std::mutex> lk(mu_);
    if (balance_set_) {
        r.value = balance_;
    } else {
        // paper mock infinite: 1e15 USDC (= $1B × 1e6)
        Balance b;
        b.balance_usdc_micro = 1'000'000'000'000'000ULL;  // 1e15
        b.ts.ds_ts_source    = DataSourceTsSource::InferredFromIngestion;
        // R-20 兜底: paper mock 没有 upstream payload, 显式标 INFERRED_FROM_INGESTION
        const std::int64_t now = infra::wal::pit::NowRealtimeNs();
        b.ts.event_ts_ns        = now;
        b.ts.data_source_ts_ns  = now;
        b.ts.ingestion_ts_ns    = now;
        b.ts.as_of_ts_ns        = now;
        r.value = b;
    }
    return r;
}

// ---------- F-08 GetOrderStatus ----------

Result<OrderAck> PaperPolymarketClient::GetOrderStatus(std::string_view order_id) noexcept {
    Result<OrderAck> r;
    std::lock_guard<std::mutex> lk(mu_);
    auto it = orders_.find(std::string{order_id});
    if (it == orders_.end()) {
        r.error = MakeError(PMErrorKind::NotFound, 404, "order_id not in paper book");
        return r;
    }
    r.value = it->second.ack;
    return r;
}

// ---------- F-09 GetMyOpenOrders ----------

Result<std::vector<OrderAck>> PaperPolymarketClient::GetMyOpenOrders() noexcept {
    Result<std::vector<OrderAck>> r;
    std::vector<OrderAck> out;
    std::lock_guard<std::mutex> lk(mu_);
    out.reserve(orders_.size());
    for (const auto& [_, entry] : orders_) {
        if (!IsTerminal(entry.ack.status)) {
            out.push_back(entry.ack);
        }
    }
    r.value = std::move(out);
    return r;
}

// ---------- F-10 GetMyTrades ----------

Result<std::vector<Trade>> PaperPolymarketClient::GetMyTrades(std::uint32_t limit) noexcept {
    Result<std::vector<Trade>> r;
    std::lock_guard<std::mutex> lk(mu_);
    if (limit == 0 || static_cast<std::size_t>(limit) >= trades_.size()) {
        r.value = trades_;
    } else {
        // 返末 N 条 (PM /data/trades 默认按 ts desc; 这里简化 head-N)
        std::vector<Trade> out(trades_.begin(), trades_.begin() + static_cast<std::ptrdiff_t>(limit));
        r.value = std::move(out);
    }
    return r;
}

// ---------- F-11 / F-12 (paper 不用) ----------

Result<std::string> PaperPolymarketClient::DeriveApiKey() noexcept {
    Result<std::string> r;
    r.error = MakeError(PMErrorKind::BadRequest, 0,
                        "paper mode does not derive api keys (live only, F-11)");
    return r;
}

Result<std::vector<std::string>> PaperPolymarketClient::ListApiKeys() noexcept {
    Result<std::vector<std::string>> r;
    r.error = MakeError(PMErrorKind::BadRequest, 0,
                        "paper mode does not list api keys (live only, F-12)");
    return r;
}

// ---------- F-13 GetPricesHistory ----------

Result<std::vector<PriceHistoryPoint>> PaperPolymarketClient::GetPricesHistory(
    std::string_view /*token_id*/,
    std::int64_t     /*start_unix_s*/,
    std::int64_t     /*end_unix_s*/) noexcept {
    // paper v0.1: 返空 (W5 联调期接 PM 公开 GET /clob/prices-history; both real)
    Result<std::vector<PriceHistoryPoint>> r;
    r.value = std::vector<PriceHistoryPoint>{};
    return r;
}

// ---------- F-14 SubscribeSportsWss ----------

Result<std::uint32_t> PaperPolymarketClient::SubscribeSportsWss(
    const std::vector<std::string>& condition_ids,
    OrderBookCallback                cb,
    void*                            user_data) noexcept {
    Result<std::uint32_t> r;
    if (cb == nullptr) {
        r.error = MakeError(PMErrorKind::BadRequest, 0, "callback is null");
        return r;
    }
    // paper v0.1: 注册占位 — 立即对已注入的 book 回调一次 (单测可观测 callback wiring)
    // W5 联调期接小冯 WSS subscriber, 真订阅 wss://sports-api.polymarket.com/ws.
    std::uint32_t fired = 0;
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (const auto& cid : condition_ids) {
            auto it = books_.find(cid);
            if (it != books_.end()) {
                cb(it->second, user_data);
                ++fired;
            }
        }
    }
    r.value = fired;
    return r;
}

// ---------- 单测辅助 ----------

void PaperPolymarketClient::TestInjectOrderbook(const OrderBookSnapshot& snap) noexcept {
    std::lock_guard<std::mutex> lk(mu_);
    books_[snap.condition_id] = snap;
}

void PaperPolymarketClient::TestInjectPosition(const Position& p) noexcept {
    std::lock_guard<std::mutex> lk(mu_);
    positions_.push_back(p);
}

void PaperPolymarketClient::TestInjectMarketInfo(const MarketInfo& m) noexcept {
    std::lock_guard<std::mutex> lk(mu_);
    markets_[m.condition_id] = m;
}

bool PaperPolymarketClient::TestTransitionOrder(std::string_view order_id, OrderStatus to) noexcept {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = orders_.find(std::string{order_id});
    if (it == orders_.end()) {
        return false;
    }
    if (!IsLegalTransition(it->second.ack.status, to)) {
        return false;
    }
    it->second.ack.status = to;
    return true;
}

void PaperPolymarketClient::TestInjectTrade(const Trade& t) noexcept {
    std::lock_guard<std::mutex> lk(mu_);
    trades_.push_back(t);
}

void PaperPolymarketClient::TestSetBalance(const Balance& b) noexcept {
    std::lock_guard<std::mutex> lk(mu_);
    balance_     = b;
    balance_set_ = true;
}

}  // namespace stcpp::polymarket::paper
