// src/stcpp/debug_api/endpoint_book_pair.cpp
//   GET /api/v1/book_pair/{condition_id}  (ADR-040 BinaryMarketBookView 显式端点)
//   GET /api/v1/book/token/{token_id}     (ADR-040 单边 book 按 token_id 直查)
// Owner: 小卢 (senior-ic-pool)  ADR-040 市场结构修正
// 关联:
//   docs/RESEARCH/laozhou-market-structure-contract-fix-v1.md §3.3 HTTP 路由
//   ADR-038 §3 schema 铁律: mode (R-11), 4 时间戳 epoch_ns int64 (R-20)
//
// 序列化单一数据源: 调用 payload::book_pair / payload::book_snapshot (与 SSE book 通道复用,
//   不重复)。2026-06-02 去冗余 (老板): 原 bp_serialize_book_snapshot 静态函数已上移到
//   endpoint_payloads.hpp::book_snapshot。

#include <string>

#include "src/stcpp/debug_api/endpoint_payloads.hpp"
#include "src/stcpp/debug_api/server.hpp"

namespace stcpp::debug_api {

// GET /api/v1/book_pair/{condition_id} — BinaryMarketBookView (显式端点)
static void register_book_pair_endpoint(httplib::Server& svr, const HttpServer& hs) {
    svr.Get(R"(/api/v1/book_pair/([^/]+))", [&hs](const httplib::Request& req, httplib::Response& res) {
        const std::string condition_id = req.matches[1];
        res.set_content(payload::book_pair(hs.provider(), condition_id, now_epoch_ns()),
                        "application/json; charset=utf-8");
        res.status = 200;  // #1 数据未就绪返 found:false(非404), 减轮询噪音
    });
}

// GET /api/v1/book/token/{token_id} — 单边 BookSnapshot (策略/调试旁路)
static void register_book_by_token(httplib::Server& svr, const HttpServer& hs) {
    svr.Get(R"(/api/v1/book/token/([^/]+))", [&hs](const httplib::Request& req, httplib::Response& res) {
        const StateProvider& sp = hs.provider();
        const std::string token_id = req.matches[1];
        std::string body = "{\"mode\":";
        body += json::str(exec_mode_str(sp.mode()));
        body += ",\"as_of_ts\":";
        body += json::i64(now_epoch_ns());
        body += ",\"book\":";
        body += payload::book_snapshot(sp.book(token_id));
        body += '}';
        res.set_content(body, "application/json; charset=utf-8");
        res.status = 200;  // #1 数据未就绪返 found:false(非404), 减轮询噪音
    });
}

void register_book_pair(httplib::Server& svr, const HttpServer& hs) {
    register_book_by_token(svr, hs);  // 先注册更具体的 /book/token/{id}
    register_book_pair_endpoint(svr, hs);
}

}  // namespace stcpp::debug_api
