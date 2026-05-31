// src/stcpp/debug_api/endpoint_book_pair.cpp
//   GET /api/v1/book_pair/{condition_id}  (ADR-040 BinaryMarketBookView 显式端点)
//   GET /api/v1/book/token/{token_id}     (ADR-040 单边 book 按 token_id 直查)
// Owner: 小卢 (senior-ic-pool)  ADR-040 市场结构修正
// 关联:
//   docs/RESEARCH/laozhou-market-structure-contract-fix-v1.md §3.3 HTTP 路由
//   ADR-038 §3 schema 铁律: mode (R-11), 4 时间戳 epoch_ns int64 (R-20)
//
// book_pair: 看板主路显式 URL (book_pair/{condition_id})
//   → 与 /api/v1/book/{condition_id} 等价 (共用 provider.book_pair)
//   → 供前端/策略层显式标注 "我要双 token view" 的明确调用点
//
// book/token/{token_id}: 策略/调试单边旁路 (per-token_id 精确查询)
//   → provider.book(token_id) 直接返回 BookSnapshot

#include <string>

#include "src/stcpp/debug_api/endpoint_common.hpp"
#include "src/stcpp/debug_api/json_writer.hpp"
#include "src/stcpp/debug_api/server.hpp"

namespace stcpp::debug_api {

// 辅助: 序列化单边 BookSnapshot (与 endpoint_market.cpp 中的相同逻辑, 独立声明避免跨 TU 依赖)
static std::string bp_serialize_book_snapshot(const BookSnapshot& b) {
    std::string s;
    s.reserve(512);
    s += "{\"found\":";
    s += json::boolean(b.found);
    s += ",\"token_id\":";
    s += json::str(b.token_id);
    s += ",\"condition_id\":";
    s += json::str(b.condition_id);
    s += ",\"outcome\":";
    s += json::str(b.outcome);
    // DEPRECATED alias
    s += ",\"market_id\":";
    s += json::str(b.market_id);
    // wss_state / source 无论 found 均输出 (P1-3 对齐 endpoint_market.cpp; 修 found=false 时
    //   前端热力格因 wss_state=undefined 全红的行为不一致, 2026-05-31 接口核查发现)。
    s += ",\"wss_state\":";
    s += json::str(b.wss_state);
    s += ",\"source\":";
    s += json::str(b.source);
    if (b.found) {
        s += ",\"best_bid\":";
        s += json::num(b.best_bid);
        s += ",\"best_ask\":";
        s += json::num(b.best_ask);
        s += ",\"microprice\":";
        s += json::num(b.microprice);
        s += ",\"spread\":";
        s += json::num(b.spread);
        s += ",\"imbalance\":";
        s += json::num(b.imbalance);
        s += ",\"sequence_no\":";
        s += json::i64(b.sequence_no);
        s += ",\"gap_count\":";
        s += json::i64(b.gap_count);
        s += ",\"event_ts\":";
        s += json::i64(b.ts.event_ts_ns);
        s += ",\"data_source_ts\":";
        s += json::i64(b.ts.data_source_ts_ns);
        s += ",\"ingestion_ts\":";
        s += json::i64(b.ts.ingestion_ts_ns);
        s += ",\"book_as_of_ts\":";
        s += json::i64(b.ts.as_of_ts_ns);
        s += ",\"bids\":[";
        for (std::size_t i = 0; i < b.bids.size(); ++i) {
            if (i) {
                s += ',';
            }
            s += "{\"price\":";
            s += json::num(b.bids[i].price);
            s += ",\"size\":";
            s += json::num(b.bids[i].size);
            s += '}';
        }
        s += "],\"asks\":[";
        for (std::size_t i = 0; i < b.asks.size(); ++i) {
            if (i) {
                s += ',';
            }
            s += "{\"price\":";
            s += json::num(b.asks[i].price);
            s += ",\"size\":";
            s += json::num(b.asks[i].size);
            s += '}';
        }
        s += "]";
    }
    s += '}';
    return s;
}

// GET /api/v1/book_pair/{condition_id} — BinaryMarketBookView (显式端点)
static void register_book_pair_endpoint(httplib::Server& svr, const HttpServer& hs) {
    svr.Get(R"(/api/v1/book_pair/([^/]+))", [&hs](const httplib::Request& req, httplib::Response& res) {
        const StateProvider& sp = hs.provider();
        const std::string condition_id = req.matches[1];
        const BinaryMarketBookView bv = sp.book_pair(condition_id);
        const std::int64_t as_of = now_epoch_ns();

        std::string body;
        body.reserve(1536);
        body += "{\"mode\":";
        body += json::str(exec_mode_str(sp.mode()));
        body += ",\"as_of_ts\":";
        body += json::i64(as_of);
        body += ",\"found\":";
        body += json::boolean(bv.found);
        body += ",\"condition_id\":";
        body += json::str(bv.condition_id);
        if (bv.found) {
            body += ",\"cross_spread\":";
            body += json::num(bv.cross_spread);
            body += ",\"event_ts\":";
            body += json::i64(bv.ts.event_ts_ns);
            body += ",\"data_source_ts\":";
            body += json::i64(bv.ts.data_source_ts_ns);
            body += ",\"ingestion_ts\":";
            body += json::i64(bv.ts.ingestion_ts_ns);
            body += ",\"as_of_ts_ns\":";
            body += json::i64(bv.ts.as_of_ts_ns);
            body += ",\"token0\":";
            body += bp_serialize_book_snapshot(bv.token0);
            body += ",\"token1\":";
            body += bp_serialize_book_snapshot(bv.token1);
        }
        body += '}';

        res.set_content(body, "application/json; charset=utf-8");
        res.status = bv.found ? 200 : 404;
    });
}

// GET /api/v1/book/token/{token_id} — 单边 BookSnapshot (策略/调试旁路)
static void register_book_by_token(httplib::Server& svr, const HttpServer& hs) {
    svr.Get(R"(/api/v1/book/token/([^/]+))", [&hs](const httplib::Request& req, httplib::Response& res) {
        const StateProvider& sp = hs.provider();
        const std::string token_id = req.matches[1];
        const BookSnapshot b = sp.book(token_id);
        const std::int64_t as_of = now_epoch_ns();

        std::string body;
        body.reserve(768);
        body += "{\"mode\":";
        body += json::str(exec_mode_str(sp.mode()));
        body += ",\"as_of_ts\":";
        body += json::i64(as_of);
        body += ",\"book\":";
        body += bp_serialize_book_snapshot(b);
        body += '}';

        res.set_content(body, "application/json; charset=utf-8");
        res.status = b.found ? 200 : 404;
    });
}

void register_book_pair(httplib::Server& svr, const HttpServer& hs) {
    register_book_by_token(svr, hs);  // 先注册更具体的 /book/token/{id}
    register_book_pair_endpoint(svr, hs);
}

}  // namespace stcpp::debug_api
