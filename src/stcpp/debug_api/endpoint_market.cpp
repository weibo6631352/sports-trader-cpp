// src/stcpp/debug_api/endpoint_market.cpp
//   GET /api/v1/market/{condition_id}  (ADR-038 §4.3 MarketInfo, ADR-040 tokens[]/condition_id)
//   GET /api/v1/book/{condition_id}    (ADR-040 → BinaryMarketBookView, 看板主路)
// Owner: 小卢 (senior-ic-pool)  ADR-038 MVP, ADR-040 市场结构修正
// 关联:
//   ADR-038 §4.3, ADR-040 (老周决议: per-token 订单簿, BinaryMarketBookView, condition_id 权威)
//   §3 schema 铁律: 顶层 mode (R-11), 4 时间戳 epoch_ns int64 (R-20),
//     vendor 降为 source 标签 (禁 pm_/clob_ 字段名前缀)
//
// ADR-040 变更:
//   market 端点: 输出 condition_id (权威) + market_id (deprecated alias) + tokens[] +
//                slug + polymarket_url + neg_risk_market_id
//   book 端点: 改输出 BinaryMarketBookView (token0 + token1 + cross_spread)
//              URL 保持 /api/v1/book/{condition_id} (body schema 变更, 走 §1 例外通道)
//
// 未找到 → HTTP 404 + {found:false}。真实接入: market=老李 MarketInfo / book=小冯 OrderBookFeatures。

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
        body.reserve(768);
        body += "{\"mode\":";
        body += json::str(exec_mode_str(sp.mode()));
        body += ",\"as_of_ts\":";
        body += json::i64(as_of);
        body += ",\"found\":";
        body += json::boolean(mi.found);
        // ADR-040: condition_id 权威字段
        body += ",\"condition_id\":";
        body += json::str(mi.condition_id);
        // DEPRECATED alias: market_id = condition_id (前端切换后 P2 移除)
        body += ",\"market_id\":";
        body += json::str(mi.market_id);
        if (mi.found) {
            body += ",\"tick_size\":";
            body += json::num(mi.tick_size);
            body += ",\"fee_rate\":";
            body += json::num(mi.fee_rate);
            body += ",\"neg_risk\":";
            body += json::boolean(mi.neg_risk);
            body += ",\"neg_risk_market_id\":";
            body += json::str(mi.neg_risk_market_id);
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
            // event 锚 (ADR-038 增量)
            body += ",\"event_id\":";
            body += json::str(mi.event_id);
            // ADR-040 Polymarket 超链接 (老板要求 P0)
            body += ",\"slug\":";
            body += json::str(mi.slug);
            body += ",\"polymarket_url\":";
            body += json::str(mi.polymarket_url);
            // 小冯 schema append (G-FREEZE-W 只增, 2026-05-29)
            body += ",\"sports_market_type\":";
            body += json::str(mi.sports_market_type);
            body += ",\"group_item_title\":";
            body += json::str(mi.group_item_title);
            // ADR-040: tokens[] — per-token 元数据列表
            body += ",\"tokens\":[";
            for (std::size_t i = 0; i < mi.tokens.size(); ++i) {
                if (i) {
                    body += ',';
                }
                const TokenInfo& tk = mi.tokens[i];
                body += "{\"token_id\":";
                body += json::str(tk.token_id);
                body += ",\"outcome\":";
                body += json::str(tk.outcome);
                body += ",\"price\":";
                body += json::num(tk.price);
                body += ",\"winner\":";
                body += json::boolean(tk.winner);
                body += '}';
            }
            body += ']';
        }
        body += '}';

        res.set_content(body, "application/json; charset=utf-8");
        res.status = mi.found ? 200 : 404;
    });
}

// 辅助: 序列化单边 BookSnapshot 为 JSON 对象 (不含外层花括号前缀)
static std::string serialize_book_snapshot(const BookSnapshot& b) {
    std::string s;
    s.reserve(512);
    s += "{\"found\":";
    s += json::boolean(b.found);
    // ADR-040 权威字段
    s += ",\"token_id\":";
    s += json::str(b.token_id);
    s += ",\"condition_id\":";
    s += json::str(b.condition_id);
    s += ",\"outcome\":";
    s += json::str(b.outcome);
    // DEPRECATED alias
    s += ",\"market_id\":";
    s += json::str(b.market_id);
    // wss_state / source: 连接状态元数据, 无论 found 均输出 (P1-3: 前端判断依据)
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
        // 4 时间戳 (R-20)
        s += ",\"event_ts\":";
        s += json::i64(b.ts.event_ts_ns);
        s += ",\"data_source_ts\":";
        s += json::i64(b.ts.data_source_ts_ns);
        s += ",\"ingestion_ts\":";
        s += json::i64(b.ts.ingestion_ts_ns);
        s += ",\"book_as_of_ts\":";
        s += json::i64(b.ts.as_of_ts_ns);
        // 深度阶梯
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
        // 小冯 schema append (G-FREEZE-W 只增, 2026-05-29)
        s += ",\"last_trade_price\":";
        s += json::num(b.last_trade_price);
    }
    s += '}';
    return s;
}

// GET /api/v1/book/{condition_id}
// ADR-040: 返回 BinaryMarketBookView (token0 + token1 + cross_spread); 看板主路
static void register_book(httplib::Server& svr, const HttpServer& hs) {
    svr.Get(R"(/api/v1/book/([^/]+))", [&hs](const httplib::Request& req, httplib::Response& res) {
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
            // 4 时间戳 (R-20): BinaryMarketBookView.ts (保守 staleness)
            body += ",\"event_ts\":";
            body += json::i64(bv.ts.event_ts_ns);
            body += ",\"data_source_ts\":";
            body += json::i64(bv.ts.data_source_ts_ns);
            body += ",\"ingestion_ts\":";
            body += json::i64(bv.ts.ingestion_ts_ns);
            body += ",\"as_of_ts_ns\":";
            body += json::i64(bv.ts.as_of_ts_ns);
            body += ",\"token0\":";
            body += serialize_book_snapshot(bv.token0);
            body += ",\"token1\":";
            body += serialize_book_snapshot(bv.token1);
        } else {
            // P1-3: found=false 时仍输出 token0/token1 的 wss_state (前端判断连接状态)
            // 前端可据此在"数据未就绪"时仍显示正确的 WSS 连接指示
            body += ",\"token0\":";
            body += serialize_book_snapshot(bv.token0);
            body += ",\"token1\":";
            body += serialize_book_snapshot(bv.token1);
        }
        body += '}';

        res.set_content(body, "application/json; charset=utf-8");
        res.status = bv.found ? 200 : 404;
    });
}

void register_market(httplib::Server& svr, const HttpServer& hs) {
    register_market_info(svr, hs);
    register_book(svr, hs);
}

}  // namespace stcpp::debug_api
