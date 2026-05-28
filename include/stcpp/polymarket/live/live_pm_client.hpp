// stcpp/polymarket/live/live_pm_client.hpp — Live mode PolymarketClient (M5+ deferred, stub only)
//
// Owner: 老李 (#07) — Sprint-2 W5 Wave 24
//
// 落:
//   docs/RESEARCH/laoli-polymarket-backend-requirements-v1.md §3.1 (14 接口 live 列)
//                                                        §5 (HMAC 4 bug enforce)
//                                                        附录 B (PM 5 host, live 全 5)
//
// 红线:
//   R-7  仅进 live binary (CMake STCPP_EXEC_MODE=live 才 link); v0.1 此 stub 不实现任何方法,
//        Sign 时 unimpl 返 Unknown error.
//   R-11 OrderAck.audit_wal_kind 必填 RiskAudit (live 走真账本; paper 用 PaperAudit, 不能混淆).
//   R-12 14 接口同步契约 + libcurl 多线程; live 实现走 worker pool (老陈 W5-02 网络层).
//   R-20 全部 4 ts UPSTREAM_PAYLOAD 优先 (gamma listing 走 UpstreamHeader 兜底).
//   HMAC R1-R4 (v3 §A): 实现 byte-equal v3 §D 14 wire vector (M5+ 老孙 review).
//                       sigType=1 / base64 保留 padding / path 不含 querystring / asset_type=COLLATERAL.
//
// 不耻下问:
//   - libcurl 多线程 + WSS reconnect → @老陈 (网络) W5-02
//   - L1 EIP-712 sig + SecureBuffer → @老孙 (signer v4) M5+
//   - sports-api 第 5 host WSS subscriber → @小冯 (D 数据基础设施) W5-02bis
//
// v0.1 仅 stub: 14 接口全部返 Unknown / "live mode not implemented (M5+ deferred)".
// 接入计划 (Sprint-3 / Sprint-4):
//   W6 — gamma /events listing + clob /books (公开 endpoint, 不签 HMAC)
//   W7 — HMAC L2 14 wire vector byte-equal + sigType=1
//   W8 — clob /ws/market + /ws/user (HMAC)
//   M5 — POST /clob/orders (真 EIP-712 + 真 nonce + 真 gas)

#pragma once

#include "stcpp/polymarket/pm_client.hpp"

namespace stcpp::polymarket::live {

class LivePolymarketClient final : public IPolymarketClient {
 public:
    LivePolymarketClient() noexcept = default;

    [[nodiscard]] execution::ExecutionMode Mode() const noexcept override {
        return execution::ExecutionMode::Live;
    }

    [[nodiscard]] Result<OrderBookSnapshot> GetOrderbook(std::string_view) noexcept override;
    [[nodiscard]] Result<OrderAck>          SubmitOrder(const SignedOrder&) noexcept override;
    [[nodiscard]] Result<OrderAck>          CancelOrder(std::string_view) noexcept override;
    [[nodiscard]] Result<std::uint32_t>     CancelAll() noexcept override;
    [[nodiscard]] Result<MarketInfo>        GetMarketInfo(std::string_view) noexcept override;
    [[nodiscard]] Result<std::vector<Position>> GetUserPositions(std::string_view) noexcept override;
    [[nodiscard]] Result<Balance>           GetBalance() noexcept override;
    [[nodiscard]] Result<OrderAck>          GetOrderStatus(std::string_view) noexcept override;
    [[nodiscard]] Result<std::vector<OrderAck>> GetMyOpenOrders() noexcept override;
    [[nodiscard]] Result<std::vector<Trade>> GetMyTrades(std::uint32_t) noexcept override;
    [[nodiscard]] Result<std::string>       DeriveApiKey() noexcept override;
    [[nodiscard]] Result<std::vector<std::string>> ListApiKeys() noexcept override;
    [[nodiscard]] Result<std::vector<PriceHistoryPoint>> GetPricesHistory(
        std::string_view, std::int64_t, std::int64_t) noexcept override;
    [[nodiscard]] Result<std::uint32_t>     SubscribeSportsWss(
        const std::vector<std::string>&, OrderBookCallback, void*) noexcept override;
};

}  // namespace stcpp::polymarket::live
