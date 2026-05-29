// stcpp/polymarket/live/live_pm_client.cpp — LivePolymarketClient v0.1 stub (M5+ deferred)
//
// Owner: 老李 (#07) — Sprint-2 W5 Wave 24
// 红线: R-7 仅 live binary; 14 接口全 stub, 返 PMErrorKind::Unknown.
// 接入计划见 .hpp 头注 (W6 / W7 / W8 / M5).

#include "stcpp/polymarket/live/live_pm_client.hpp"

#include "stcpp/infra/wal/pit.hpp"

namespace stcpp::polymarket::live {

namespace {

constexpr const char* kUnimplemented = "live mode not implemented (M5+ deferred, see W5-A-01 派单)";

PMError MakeUnimplemented() noexcept {
    PMError e;
    e.kind = PMErrorKind::Unknown;
    e.http_status = 0;
    e.body = kUnimplemented;
    e.observed_ts_ns = infra::wal::pit::NowRealtimeNs();
    return e;
}

}  // namespace

Result<OrderBookSnapshot> LivePolymarketClient::GetOrderbook(std::string_view) noexcept {
    Result<OrderBookSnapshot> r;
    r.error = MakeUnimplemented();
    return r;
}

Result<OrderAck> LivePolymarketClient::SubmitOrder(const SignedOrder&) noexcept {
    Result<OrderAck> r;
    r.error = MakeUnimplemented();
    return r;
}

Result<OrderAck> LivePolymarketClient::CancelOrder(std::string_view) noexcept {
    Result<OrderAck> r;
    r.error = MakeUnimplemented();
    return r;
}

Result<std::uint32_t> LivePolymarketClient::CancelAll() noexcept {
    Result<std::uint32_t> r;
    r.error = MakeUnimplemented();
    return r;
}

Result<MarketInfo> LivePolymarketClient::GetMarketInfo(std::string_view) noexcept {
    Result<MarketInfo> r;
    r.error = MakeUnimplemented();
    return r;
}

Result<std::vector<Position>> LivePolymarketClient::GetUserPositions(std::string_view) noexcept {
    Result<std::vector<Position>> r;
    r.error = MakeUnimplemented();
    return r;
}

Result<Balance> LivePolymarketClient::GetBalance() noexcept {
    Result<Balance> r;
    r.error = MakeUnimplemented();
    return r;
}

Result<OrderAck> LivePolymarketClient::GetOrderStatus(std::string_view) noexcept {
    Result<OrderAck> r;
    r.error = MakeUnimplemented();
    return r;
}

Result<std::vector<OrderAck>> LivePolymarketClient::GetMyOpenOrders() noexcept {
    Result<std::vector<OrderAck>> r;
    r.error = MakeUnimplemented();
    return r;
}

Result<std::vector<Trade>> LivePolymarketClient::GetMyTrades(std::uint32_t) noexcept {
    Result<std::vector<Trade>> r;
    r.error = MakeUnimplemented();
    return r;
}

Result<std::string> LivePolymarketClient::DeriveApiKey() noexcept {
    Result<std::string> r;
    r.error = MakeUnimplemented();
    return r;
}

Result<std::vector<std::string>> LivePolymarketClient::ListApiKeys() noexcept {
    Result<std::vector<std::string>> r;
    r.error = MakeUnimplemented();
    return r;
}

Result<std::vector<PriceHistoryPoint>> LivePolymarketClient::GetPricesHistory(std::string_view, std::int64_t,
                                                                              std::int64_t) noexcept {
    Result<std::vector<PriceHistoryPoint>> r;
    r.error = MakeUnimplemented();
    return r;
}

Result<std::uint32_t> LivePolymarketClient::SubscribeSportsWss(const std::vector<std::string>&,
                                                               OrderBookCallback, void*) noexcept {
    Result<std::uint32_t> r;
    r.error = MakeUnimplemented();
    return r;
}

}  // namespace stcpp::polymarket::live
