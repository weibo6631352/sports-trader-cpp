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

#include <string>

#include "src/stcpp/debug_api/endpoint_common.hpp"
#include "src/stcpp/debug_api/json_writer.hpp"
#include "src/stcpp/debug_api/server.hpp"

namespace stcpp::debug_api {

void register_events(httplib::Server& svr, const HttpServer& hs) {
    svr.Get("/api/v1/events", [&hs](const httplib::Request& /*req*/, httplib::Response& res) {
        const StateProvider& sp = hs.provider();
        const std::vector<EventInfo> evs = sp.events();
        const std::int64_t as_of = now_epoch_ns();

        std::string body;
        body.reserve(512 + evs.size() * 256);
        body += "{\"mode\":";
        body += json::str(exec_mode_str(sp.mode()));
        body += ",\"as_of_ts\":";
        body += json::i64(as_of);
        body += ",\"data_source\":";
        body += json::str(sp.data_source());
        body += ",\"events\":[";

        for (std::size_t i = 0; i < evs.size(); ++i) {
            if (i)
                body += ',';
            const EventInfo& ev = evs[i];
            body += "{\"event_id\":";
            body += json::str(ev.event_id);
            body += ",\"slug\":";
            body += json::str(ev.slug);
            body += ",\"title\":";
            body += json::str(ev.title);
            body += ",\"sport\":";
            body += json::str(ev.sport);
            body += ",\"neg_risk_market_id\":";
            body += json::str(ev.neg_risk_market_id);
            body += ",\"condition_ids\":[";
            for (std::size_t j = 0; j < ev.condition_ids.size(); ++j) {
                if (j)
                    body += ',';
                body += json::str(ev.condition_ids[j]);
            }
            body += "]}";
        }

        body += "]}";
        res.set_content(body, "application/json; charset=utf-8");
        res.status = 200;
    });
}

}  // namespace stcpp::debug_api
