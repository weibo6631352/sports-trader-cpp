// src/stcpp/debug_api/endpoint_positions.cpp — GET /api/v1/positions (ADR-038 §4.1)
// Owner: 小卢 (senior-ic-pool)  ADR-038 MVP
// 关联:
//   ADR-038 §4.1 (per market/outcome 持仓 + pnl_realized/unrealized + mark_price)
//   §3 schema 铁律: 顶层 mode (R-11), as_of_ts epoch_ns int64 (R-20)
//   小白 §1: 黑名单字段 (私钥/签名字节/secret) 物理不在 HoldingView 中
//
// 只读: 从 provider.positions() 读 double-buffer front snapshot, 不碰交易/控制面。
// MVP 默认 StubStateProvider 返回空数组 (结构合法)。真实接入 = 小石 持仓账本 (position ledger) snapshot。

#include <string>
#include <vector>

#include "src/stcpp/debug_api/endpoint_common.hpp"
#include "src/stcpp/debug_api/json_writer.hpp"
#include "src/stcpp/debug_api/server.hpp"

namespace stcpp::debug_api {

void register_positions(httplib::Server& svr, const HttpServer& hs) {
    svr.Get("/api/v1/positions", [&hs](const httplib::Request& /*req*/, httplib::Response& res) {
        const StateProvider& sp = hs.provider();
        const std::vector<HoldingView> rows = sp.positions();
        const std::int64_t as_of = now_epoch_ns();

        std::string body;
        body.reserve(256 + rows.size() * 256);
        body += "{\"mode\":";
        body += json::str(exec_mode_str(sp.mode()));
        body += ",\"as_of_ts\":";
        body += json::i64(as_of);
        body += ",\"positions\":[";
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const HoldingView& r = rows[i];
            if (i) {
                body += ',';
            }
            body += "{\"market_id\":";
            body += json::str(r.market_id);
            body += ",\"outcome\":";
            body += json::str(r.outcome);
            body += ",\"net_qty\":";
            body += json::num(r.net_qty);
            body += ",\"avg_entry_price\":";
            body += json::num(r.avg_entry_price);
            body += ",\"mark_price\":";
            body += json::num(r.mark_price);
            body += ",\"pnl_realized\":";
            body += json::num(r.pnl_realized);
            body += ",\"pnl_unrealized\":";
            body += json::num(r.pnl_unrealized);
            body += ",\"as_of_ts\":";
            body += json::i64(r.as_of_ts_ns);
            body += '}';
        }
        body += "]}";

        res.set_content(body, "application/json; charset=utf-8");
        res.status = 200;
    });
}

}  // namespace stcpp::debug_api
