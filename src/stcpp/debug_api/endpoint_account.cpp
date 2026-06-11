// src/stcpp/debug_api/endpoint_account.cpp — GET /api/v1/account (账户级现金 + 估值)
// Owner: 老雷 (GM)  2026-06-01 凯利评审 (docs/MEETINGS/2026-06-01-kelly-equity-review.md)
// 关联:
//   老板「虚拟盘要有现金、估值的显示, 不然凯利公式怎么对接」→ 账户级现金/净值暴露给前端资金面板。
//   双口径分离: equity_conservative (best_bid, 喂凯利/DD) + equity_mark (microprice, 展示)。
//   kelly_bankroll_basis: 口径说明字符串, 让操盘员一眼确认「风控纸面化」已修 (动态 vs 静态)。
//   §3 schema 铁律: 顶层 mode (R-11), as_of_ts epoch_ns int64 (R-20)。
//
// 只读: provider 经 daemon 回调读 trading_loop 发布的线程安全权益快照。未注入 → has_data=false (前端灰显)。

#include <cstdint>
#include <string>

#include "src/stcpp/debug_api/endpoint_payloads.hpp"
#include "src/stcpp/debug_api/server.hpp"

namespace stcpp::debug_api {

void register_account(httplib::Server& svr, const HttpServer& hs) {
    // body 由 payload::account 构建 (与 SSE /stream account 通道单一数据源)
    svr.Get("/api/v1/account", [&hs](const httplib::Request& /*req*/, httplib::Response& res) {
        res.set_content(payload::account(hs.provider()), "application/json; charset=utf-8");
        res.status = 200;
    });
}

// GET /api/v1/fills[?market=X] — 成交流水 (2026-06-04 老板「看懂买卖价」; market 过滤=盯盘按盘看)
void register_fills(httplib::Server& svr, const HttpServer& hs) {
    svr.Get("/api/v1/fills", [&hs](const httplib::Request& req, httplib::Response& res) {
        const std::string market = req.has_param("market") ? req.get_param_value("market") : "";
        res.set_content(payload::fills(hs.provider(), market), "application/json; charset=utf-8");
        res.status = 200;
    });
}

}  // namespace stcpp::debug_api
