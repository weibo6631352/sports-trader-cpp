// live_order_cli.cpp — 实盘下单器验证 CLI (LIVE only)
//
// Owner: GM (老雷) 2026-05-31 — Phase 3 真网验证用。
// 用法: stcpp_live_order_cli <token_id> <BUY|SELL> <maker_micro> <taker_micro> [neg_risk:0|1] [FOK|GTC]
//   凭证从 env 读 (WALLET_PRIVATE_KEY / POLYMARKET_FUNDER_ADDRESS / POLYMARKET_API_*)。
// 红线: 私钥/HMAC 不 log; 只打印响应 (公开)。
#include "stcpp/polymarket/live/live_order_submitter.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr,
                     "用法: %s <token_id> <BUY|SELL> <maker_micro> <taker_micro> [neg_risk:0|1] [FOK|GTC]\n",
                     argv[0]);
        return 2;
    }
    stcpp::polymarket::LiveCredentials creds;
    std::string err;
    if (!stcpp::polymarket::LiveCredentials::FromEnv(creds, err)) {
        std::fprintf(stderr, "❌ 凭证: %s (source .env)\n", err.c_str());
        return 1;
    }
    stcpp::polymarket::LiveOrderSubmitter submitter(std::move(creds));
    if (!submitter.Ready()) {
        std::fprintf(stderr, "❌ submitter 未就绪 (私钥无效)\n");
        return 1;
    }

    stcpp::polymarket::LiveOrderRequest req;
    req.token_id = argv[1];
    req.is_buy = (std::strcmp(argv[2], "BUY") == 0);
    req.maker_amount = std::strtoull(argv[3], nullptr, 10);
    req.taker_amount = std::strtoull(argv[4], nullptr, 10);
    req.neg_risk = (argc > 5) && std::strcmp(argv[5], "1") == 0;
    if (argc > 6) req.order_type = argv[6];

    std::printf("signer EOA: %s\n", submitter.SignerAddress().c_str());
    std::printf("提交 %s %s maker=%llu taker=%llu neg_risk=%d type=%s ...\n", argv[2], req.token_id.c_str(),
                static_cast<unsigned long long>(req.maker_amount),
                static_cast<unsigned long long>(req.taker_amount), req.neg_risk ? 1 : 0,
                req.order_type.c_str());

    const auto r = submitter.Submit(req);
    std::printf("\nHTTP %d  success=%d\n", r.http_status, r.success ? 1 : 0);
    std::printf("orderID: %s\nstatus : %s\nerror  : %s\ntx     : %s\nraw    : %s\n", r.order_id.c_str(),
                r.status.c_str(), r.error.c_str(), r.transaction_hash.c_str(), r.raw_response.c_str());
    return r.success ? 0 : 3;
}
