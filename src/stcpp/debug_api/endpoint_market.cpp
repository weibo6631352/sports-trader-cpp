// src/stcpp/debug_api/endpoint_market.cpp
//   GET /api/v1/market/{condition_id}  (ADR-038 §4.3 MarketInfo)
//   GET /api/v1/book/{condition_id}    (ADR-038 §4.3 OrderBookSnapshot 摘要)
// Owner: 小卢 (senior-ic-pool)  ADR-038 MVP
// 关联:
//   ADR-038 §4.3 market: active/closed/resolved 三态分开; tick_size/fee_rate/neg_risk/accepting_orders
//             book: microprice/spread/imbalance (后端算好) + sequence_no + gap_count + WSS state
//   §3 schema 铁律: 顶层 mode (R-11), 4 时间戳 epoch_ns int64 (R-20),
//     vendor 降为 source 标签 (禁 pm_/clob_ 字段名前缀)
//
// 只读: provider.market()/book()。MVP stub found=false (无市场目录/无 orderbook feed)。
//   真实接入: market = 老李 MarketInfo snapshot; book = 小冯 OrderBookFeatures。
//   未找到 → HTTP 404 + {found:false} (调用方可区分 not found vs error)。

#include <string>

#include "src/stcpp/debug_api/endpoint_common.hpp"
#include "src/stcpp/debug_api/json_writer.hpp"
#include "src/stcpp/debug_api/server.hpp"

namespace stcpp::debug_api {

static void register_market_info(httplib::Server& svr, const HttpServer& hs) {
    svr.Get(R"(/api/v1/market/([^/]+))", [&hs](const httplib::Request& req, httplib::Response& res) {
        const StateProvider& sp = hs.provider();
        const std::string condition_id = req.matches[1];
        const MarketInfo mi = sp.market(condition_id);
        const std::int64_t as_of = now_epoch_ns();

        std::string body;
        body.reserve(384);
        body += "{\"mode\":";
        body += json::str(exec_mode_str(sp.mode()));
        body += ",\"as_of_ts\":";
        body += json::i64(as_of);
        body += ",\"found\":";
        body += json::boolean(mi.found);
        body += ",\"market_id\":";
        body += json::str(mi.market_id);
        if (mi.found) {
            body += ",\"outcome\":";
            body += json::str(mi.outcome);
            body += ",\"tick_size\":";
            body += json::num(mi.tick_size);
            body += ",\"fee_rate\":";
            body += json::num(mi.fee_rate);
            body += ",\"neg_risk\":";
            body += json::boolean(mi.neg_risk);
            body += ",\"accepting_orders\":";
            body += json::boolean(mi.accepting_orders);
            body += ",\"active\":";
            body += json::boolean(mi.active);
            body += ",\"closed\":";
            body += json::boolean(mi.closed);
            body += ",\"resolved\":";
            body += json::boolean(mi.resolved);
            body += ",\"source\":";
            body += json::str(mi.source);
        }
        body += '}';

        res.set_content(body, "application/json; charset=utf-8");
        res.status = mi.found ? 200 : 404;
    });
}

static void register_book(httplib::Server& svr, const HttpServer& hs) {
    svr.Get(R"(/api/v1/book/([^/]+))", [&hs](const httplib::Request& req, httplib::Response& res) {
        const StateProvider& sp = hs.provider();
        const std::string condition_id = req.matches[1];
        const BookSnapshot b = sp.book(condition_id);
        const std::int64_t as_of = now_epoch_ns();

        std::string body;
        body.reserve(512);
        body += "{\"mode\":";
        body += json::str(exec_mode_str(sp.mode()));
        body += ",\"as_of_ts\":";
        body += json::i64(as_of);
        body += ",\"found\":";
        body += json::boolean(b.found);
        body += ",\"market_id\":";
        body += json::str(b.market_id);
        if (b.found) {
            body += ",\"best_bid\":";
            body += json::num(b.best_bid);
            body += ",\"best_ask\":";
            body += json::num(b.best_ask);
            body += ",\"microprice\":";
            body += json::num(b.microprice);
            body += ",\"spread\":";
            body += json::num(b.spread);
            body += ",\"imbalance\":";
            body += json::num(b.imbalance);
            body += ",\"sequence_no\":";
            body += json::i64(b.sequence_no);
            body += ",\"gap_count\":";
            body += json::i64(b.gap_count);
            body += ",\"wss_state\":";
            body += json::str(b.wss_state);
            body += ",\"source\":";
            body += json::str(b.source);
            // 4 时间戳 (R-20): 上游 ts 由 provider 透传, 非本地 now()
            body += ",\"event_ts\":";
            body += json::i64(b.ts.event_ts_ns);
            body += ",\"data_source_ts\":";
            body += json::i64(b.ts.data_source_ts_ns);
            body += ",\"ingestion_ts\":";
            body += json::i64(b.ts.ingestion_ts_ns);
            body += ",\"book_as_of_ts\":";
            body += json::i64(b.ts.as_of_ts_ns);
        }
        body += '}';

        res.set_content(body, "application/json; charset=utf-8");
        res.status = b.found ? 200 : 404;
    });
}

void register_market(httplib::Server& svr, const HttpServer& hs) {
    register_market_info(svr, hs);
    register_book(svr, hs);
}

}  // namespace stcpp::debug_api
