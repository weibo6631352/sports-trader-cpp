// src/stcpp/debug_api/endpoint_mapping.cpp
//   GET /api/v1/mapping/status — condition ↔ Goalserve event 映射实况 (可观测)
// Owner: 老雷 (GM) 2026-06-01 — 闭合探针缺口A (映射只能 grep 日志看)
//   数据源: daemon RefreshEventMapping 周期 push 到 RealStateProvider (互斥快照)。
//   R-12: 只读快照; 非热路径 (前端轮询)。
#include <string>

#include "src/stcpp/debug_api/endpoint_common.hpp"
#include "src/stcpp/debug_api/json_writer.hpp"
#include "src/stcpp/debug_api/server.hpp"

namespace stcpp::debug_api {

void register_mapping(httplib::Server& svr, const HttpServer& hs) {
    svr.Get("/api/v1/mapping/status", [&hs](const httplib::Request& /*req*/, httplib::Response& res) {
        const StateProvider& sp = hs.provider();
        const MappingStatusReport rep = sp.mapping_status();
        const std::int64_t as_of = now_epoch_ns();

        std::string body;
        body.reserve(4096);
        body += "{\"mode\":";
        body += json::str(exec_mode_str(sp.mode()));
        body += ",\"as_of_ts\":";
        body += json::i64(as_of);
        body += ",\"total_markets\":";
        body += json::i64(rep.total_markets);
        body += ",\"matched\":";
        body += json::i64(rep.matched);
        body += ",\"live_games\":";
        body += json::i64(rep.live_games);
        body += ",\"markets\":[";
        bool first = true;
        for (const auto& r : rep.markets) {
            if (!first) body += ',';
            first = false;
            body += "{\"condition_id\":";
            body += json::str(r.condition_id);
            body += ",\"team0\":";
            body += json::str(r.team0);
            body += ",\"team1\":";
            body += json::str(r.team1);
            body += ",\"is_draw\":";
            body += json::boolean(r.is_draw);
            body += ",\"matched\":";
            body += json::boolean(r.matched);
            body += ",\"inplay_match_id\":";
            body += json::str(r.inplay_match_id);
            body += ",\"match_confidence\":";
            body += json::num(r.match_confidence);
            body += '}';
        }
        body += "],\"games\":[";
        first = true;
        for (const auto& g : rep.games) {
            if (!first) body += ',';
            first = false;
            body += "{\"event_id\":";
            body += json::str(g.event_id);
            body += ",\"home\":";
            body += json::str(g.home);
            body += ",\"away\":";
            body += json::str(g.away);
            body += ",\"sport\":";
            body += json::str(g.sport);
            body += ",\"status\":";
            body += json::str(g.status);
            body += ",\"home_score\":";
            body += json::i64(g.home_score);
            body += ",\"away_score\":";
            body += json::i64(g.away_score);
            body += '}';
        }
        body += "]}";

        res.set_content(body, "application/json; charset=utf-8");
        res.status = 200;
    });
}

}  // namespace stcpp::debug_api
