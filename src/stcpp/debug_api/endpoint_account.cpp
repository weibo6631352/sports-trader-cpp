// src/stcpp/debug_api/endpoint_account.cpp — GET /api/v1/account (账户级现金 + 估值)
// Owner: 老雷 (GM)  2026-06-01 凯利评审 (docs/MEETINGS/2026-06-01-kelly-equity-review.md)
// 关联:
//   老板「虚拟盘要有现金、估值的显示, 不然凯利公式怎么对接」→ 账户级现金/净值暴露给前端资金面板。
//   双口径分离: equity_conservative (best_bid, 喂凯利/DD) + equity_mark (microprice, 展示)。
//   kelly_bankroll_basis: 口径说明字符串, 让操盘员一眼确认「风控纸面化」已修 (动态 vs 静态)。
//   §3 schema 铁律: 顶层 mode (R-11), as_of_ts epoch_ns int64 (R-20)。
//
// 只读: provider 经 daemon 回调读 paper_loop 发布的线程安全权益快照。未注入 → has_data=false (前端灰显)。

#include <cstdint>
#include <string>

#include "src/stcpp/debug_api/endpoint_common.hpp"
#include "src/stcpp/debug_api/json_writer.hpp"
#include "src/stcpp/debug_api/server.hpp"

namespace stcpp::debug_api {

void register_account(httplib::Server& svr, const HttpServer& hs) {
    svr.Get("/api/v1/account", [&hs](const httplib::Request& /*req*/, httplib::Response& res) {
        const StateProvider& sp = hs.provider();
        const AccountSnapshot a = sp.account_snapshot();

        std::string body;
        body.reserve(640);
        body += "{\"mode\":";
        body += json::str(a.mode.empty() ? exec_mode_str(sp.mode()) : a.mode);
        body += ",\"has_data\":";
        body += json::boolean(a.has_data);
        body += ",\"as_of_ts\":";
        body += json::i64(a.as_of_ts_ns);
        body += ",\"account\":{";
        body += "\"bankroll_initial\":";
        body += json::num(a.bankroll_initial);
        body += ",\"cash_available\":";
        body += json::num(a.cash_available);
        body += ",\"position_mtm\":";
        body += json::num(a.position_mtm);
        body += ",\"equity\":";  // 展示净值 (microprice 口径)
        body += json::num(a.equity_mark);
        body += ",\"equity_conservative\":";  // best_bid 口径 (= kelly_bankroll)
        body += json::num(a.equity_conservative);
        body += ",\"cum_realized_pnl\":";
        body += json::num(a.cum_realized_pnl);
        body += ",\"cum_unrealized_pnl\":";
        body += json::num(a.cum_unrealized_pnl);
        body += ",\"cum_fee_paid\":";
        body += json::num(a.cum_fee_paid);
        body += ",\"net_pnl\":";
        body += json::num(a.net_pnl);
        body += ",\"return_pct\":";
        body += json::num(a.return_pct);
        body += ",\"max_drawdown\":";
        body += json::num(a.max_drawdown);
        body += ",\"sharpe\":";
        body += json::num(a.sharpe);
        body += ",\"kelly_bankroll\":";
        body += json::num(a.kelly_bankroll);
        body += ",\"kelly_bankroll_basis\":";
        body += json::str(a.kelly_bankroll_basis);
        body += ",\"open_positions\":";
        body += json::i64(a.open_positions);
        body += "}}";

        res.set_content(body, "application/json; charset=utf-8");
        res.status = 200;
    });
}

}  // namespace stcpp::debug_api
