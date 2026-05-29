// src/stcpp/debug_api/endpoint_pnl.cpp — GET /api/v1/pnl/{timeseries,attribution} (ADR-038 §4.1)
// Owner: 小卢 (senior-ic-pool)  ADR-038 MVP
// 关联:
//   ADR-038 §4.1 timeseries (cum_net_pnl/realized/unrealized/fee/gas/n_trades 分桶)
//             attribution (gross→fee→gas→slippage→spread→net 瀑布)
//   §3 schema 铁律: 顶层 mode (R-11), as_of_ts epoch_ns int64 (R-20)
//
// 只读: provider 读 snapshot。MVP stub 返回空序列 / 0 瀑布 (结构合法)。
// query: ?window=<sec>&bucket=<sec> (默认 window=3600, bucket=60); 仅影响读取参数,
//        不写任何状态。

#include <cstdint>
#include <string>
#include <vector>

#include "src/stcpp/debug_api/endpoint_common.hpp"
#include "src/stcpp/debug_api/json_writer.hpp"
#include "src/stcpp/debug_api/server.hpp"

namespace stcpp::debug_api {

static std::int64_t query_i64(const httplib::Request& req, const char* key, std::int64_t def) {
    if (!req.has_param(key)) {
        return def;
    }
    try {
        const std::int64_t v = std::stoll(req.get_param_value(key));
        return v > 0 ? v : def;
    } catch (...) {
        return def;
    }
}

static void register_timeseries(httplib::Server& svr, const HttpServer& hs) {
    svr.Get("/api/v1/pnl/timeseries", [&hs](const httplib::Request& req, httplib::Response& res) {
        const StateProvider& sp = hs.provider();
        const std::int64_t window = query_i64(req, "window", 3600);
        const std::int64_t bucket = query_i64(req, "bucket", 60);
        const std::vector<PnlBucket> buckets = sp.pnl_timeseries(window, bucket);
        const std::int64_t as_of = now_epoch_ns();

        std::string body;
        body.reserve(256 + buckets.size() * 192);
        body += "{\"mode\":";
        body += json::str(exec_mode_str(sp.mode()));
        body += ",\"window_sec\":";
        body += json::i64(window);
        body += ",\"bucket_sec\":";
        body += json::i64(bucket);
        body += ",\"as_of_ts\":";
        body += json::i64(as_of);
        body += ",\"buckets\":[";
        for (std::size_t i = 0; i < buckets.size(); ++i) {
            const PnlBucket& b = buckets[i];
            if (i) {
                body += ',';
            }
            body += "{\"bucket_start_ts\":";
            body += json::i64(b.bucket_start_ts_ns);
            body += ",\"cum_net_pnl\":";
            body += json::num(b.cum_net_pnl);
            body += ",\"realized\":";
            body += json::num(b.realized);
            body += ",\"unrealized\":";
            body += json::num(b.unrealized);
            body += ",\"fee\":";
            body += json::num(b.fee);
            body += ",\"gas\":";
            body += json::num(b.gas);
            body += ",\"n_trades\":";
            body += json::i64(b.n_trades);
            body += '}';
        }
        body += "]}";

        res.set_content(body, "application/json; charset=utf-8");
        res.status = 200;
    });
}

static void register_attribution(httplib::Server& svr, const HttpServer& hs) {
    svr.Get("/api/v1/pnl/attribution", [&hs](const httplib::Request& /*req*/, httplib::Response& res) {
        const StateProvider& sp = hs.provider();
        const PnlAttribution a = sp.pnl_attribution();
        const std::int64_t as_of = now_epoch_ns();

        std::string body;
        body.reserve(256);
        body += "{\"mode\":";
        body += json::str(exec_mode_str(sp.mode()));
        body += ",\"as_of_ts\":";
        body += json::i64(as_of);
        // 瀑布顺序固定: gross → fee → gas → slippage → spread → net
        body += ",\"waterfall\":{";
        body += "\"gross\":";
        body += json::num(a.gross);
        body += ",\"fee\":";
        body += json::num(a.fee);
        body += ",\"gas\":";
        body += json::num(a.gas);
        body += ",\"slippage\":";
        body += json::num(a.slippage);
        body += ",\"spread\":";
        body += json::num(a.spread);
        body += ",\"net\":";
        body += json::num(a.net);
        body += "}";
        // 分市场净 PnL (可空; attribution 面板右侧"分市场"列表)
        body += ",\"per_market\":[";
        for (std::size_t i = 0; i < a.per_market.size(); ++i) {
            if (i) {
                body += ',';
            }
            body += "{\"market_id\":";
            body += json::str(a.per_market[i].market_id);
            body += ",\"net_pnl\":";
            body += json::num(a.per_market[i].net_pnl);
            body += '}';
        }
        body += "]}";

        res.set_content(body, "application/json; charset=utf-8");
        res.status = 200;
    });
}

void register_pnl(httplib::Server& svr, const HttpServer& hs) {
    register_timeseries(svr, hs);
    register_attribution(svr, hs);
}

}  // namespace stcpp::debug_api
