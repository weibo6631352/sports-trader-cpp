// include/stcpp/polymarket/live_order_types.hpp — 下单请求/响应 POD 类型 (mode-agnostic)
//
// Owner: GM (老雷) 2026-05-31。
// 从 live_order_submitter.hpp 抽出, 供 LiveOrderGate (mode-agnostic, 可单测) 与 submitter 共用。
#pragma once

#include <cstdint>
#include <string>

namespace stcpp::polymarket {

struct LiveOrderRequest {
    std::string token_id;             // decimal ERC1155 outcome token
    bool is_buy{true};                // true=BUY false=SELL
    std::uint64_t maker_amount{0};    // micro (BUY:USDC / SELL:shares)
    std::uint64_t taker_amount{0};    // micro (BUY:shares / SELL:USDC)
    bool neg_risk{false};             // 决定 verifyingContract (V2 normal vs negRisk)
    std::uint32_t signature_type{1};  // 1=POLY_PROXY
    std::string order_type{"FOK"};    // FOK/GTC
};

struct LiveOrderResult {
    bool success{false};
    int http_status{0};
    std::string order_id;
    std::string status;            // "matched"/"live"/"unmatched"
    std::string error;            // errorMsg / error
    std::string transaction_hash;
    std::string raw_response;     // 已可安全 log (不含私钥/HMAC)
};

}  // namespace stcpp::polymarket
