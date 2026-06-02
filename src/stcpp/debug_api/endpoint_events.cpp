// src/stcpp/debug_api/endpoint_events.cpp
//   GET /api/v1/events  — 活跃体育 event 列表 (小冯 schema append, G-FREEZE-W 只增)
//
// Owner: 小冯 (#34)  2026-05-29
// 关联:
//   state_provider.hpp EventInfo (新增)
//   docs/RESEARCH/laoli-events-ws-mapping-spec-v1.md (Events→Market→Token 映射)
//
// 返回格式:
//   {"mode":"live","as_of_ts":...,
//    "events":[
//      {"event_id":"...","slug":"...","title":"...","sport":"...",
//       "neg_risk_market_id":"...","condition_ids":["...","...",...]},...]}
//
// live 模式: RealStateProvider.events() 从 gamma /events 启动时发现的结果
// stub/demo 模式: events=[] (空数组, 前端 fallback)

#include "src/stcpp/debug_api/endpoint_common.hpp"
#include "src/stcpp/debug_api/endpoint_payloads.hpp"
#include "src/stcpp/debug_api/server.hpp"

namespace stcpp::debug_api {

void register_events(httplib::Server& svr, const HttpServer& hs) {
    // body 由 payload::events 构建 (与 SSE /stream events 通道单一数据源)
    svr.Get("/api/v1/events", [&hs](const httplib::Request& /*req*/, httplib::Response& res) {
        res.set_content(payload::events(hs.provider(), now_epoch_ns()), "application/json; charset=utf-8");
        res.status = 200;
    });
}

}  // namespace stcpp::debug_api
