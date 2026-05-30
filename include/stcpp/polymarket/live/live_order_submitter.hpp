// include/stcpp/polymarket/live/live_order_submitter.hpp — 生产实盘下单器 (LIVE only)
//
// Owner: GM (老雷) 2026-05-31 — 最后一公里 Phase 3。
//
// 串起已测模块 + libcurl: ComputeOrderV2Digest(eip712) → SignDigest(secp256k1)
//   → recover 自检(P1-5) → BuildOrderV2Body + ComputeL2Signature(clob_wire)
//   → POST https://clob.polymarket.com/order → 解析 OrderAck。
//
// 红线: 私钥仅在内存 (LiveCredentials), 绝不 log; api_secret/POLY_SIGNATURE 不 log。
//   R-12: 非 hot path (下单链路, 非 WSS event loop)。
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace stcpp::polymarket {

// 实盘凭证 (从 env 读)。析构不主动 zeroize (进程级机密, 与 .env 同等; 见 GM 决策)。
struct LiveCredentials {
    std::array<std::uint8_t, 32> private_key{};  // EOA secp256k1 私钥
    std::string funder_address;                  // 0x.. (maker, proxy/Safe)
    std::string api_key;                         // POLY_API_KEY
    std::string api_secret;                      // base64url (HMAC key)
    std::string api_passphrase;                  // POLY_PASSPHRASE

    // 从 env 加载: WALLET_PRIVATE_KEY / POLYMARKET_FUNDER_ADDRESS /
    //   POLYMARKET_API_KEY / POLYMARKET_API_SECRET / POLYMARKET_API_PASSPHRASE。
    // 缺任一 → false + err 写明缺哪个 (不含值)。
    [[nodiscard]] static bool FromEnv(LiveCredentials& out, std::string& err);
};

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

class LiveOrderSubmitter {
public:
    // creds 按值持有 (含私钥)。endpoint 默认生产 CLOB。
    explicit LiveOrderSubmitter(LiveCredentials creds,
                                std::string endpoint = "https://clob.polymarket.com");

    // 构造是否成功 (私钥能否推出 EOA)。false → 不可用。
    [[nodiscard]] bool Ready() const noexcept { return ready_; }
    // 推出的 signer EOA (0x.. 小写), 供 POLY_ADDRESS / 自检。
    [[nodiscard]] const std::string& SignerAddress() const noexcept { return eoa_lc_; }

    // 签名 + HMAC + POST。同步阻塞 (R-12 非 hot path)。
    [[nodiscard]] LiveOrderResult Submit(const LiveOrderRequest& req) noexcept;

private:
    LiveCredentials creds_;
    std::string endpoint_;
    std::array<std::uint8_t, 20> eoa_{};
    std::string eoa_lc_;
    bool ready_{false};
};

}  // namespace stcpp::polymarket
