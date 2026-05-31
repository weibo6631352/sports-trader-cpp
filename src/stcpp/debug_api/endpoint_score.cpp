// src/stcpp/debug_api/endpoint_score.cpp
//   GET /api/v1/score/{event_id}  (ADR-038 增量; 前端 v3 盯盘 demo 数据)
// Owner: 小卢 (senior-ic-pool)  2026-05-29
// 关联:
//   ADR-038 §4 / G-FREEZE-W (只增不改名)
//   R-20: 4 时间戳 epoch_ns int64; as_of_ts = endpoint 读取快照时刻
//   R-12: 观测侧零反向依赖; provider.score() 只读
//
// 响应:
//   200 + EventScore 全字段 (found=true)
//   404 + {found:false, event_id, mode, as_of_ts} (found=false)

#include <string>

#include "src/stcpp/debug_api/endpoint_common.hpp"
#include "src/stcpp/debug_api/json_writer.hpp"
#include "src/stcpp/debug_api/server.hpp"

namespace stcpp::debug_api {

void register_score(httplib::Server& svr, const HttpServer& hs) {
    svr.Get(R"(/api/v1/score/([^/]+))", [&hs](const httplib::Request& req, httplib::Response& res) {
        const StateProvider& sp = hs.provider();
        const std::string event_id = req.matches[1];
        const EventScore s = sp.score(event_id);
        const std::int64_t as_of = now_epoch_ns();

        std::string body;
        body.reserve(512);
        body += "{\"mode\":";
        body += json::str(exec_mode_str(sp.mode()));
        body += ",\"as_of_ts\":";
        body += json::i64(as_of);
        body += ",\"found\":";
        body += json::boolean(s.found);
        body += ",\"event_id\":";
        body += json::str(s.event_id);

        if (s.found) {
            body += ",\"sport\":";
            body += json::str(s.sport);
            body += ",\"status\":";
            body += json::str(s.status);
            body += ",\"period\":";
            body += json::str(s.period);
            body += ",\"clock_sec\":";
            body += json::i64(s.clock_sec);
            body += ",\"home\":";
            body += json::str(s.home);
            body += ",\"away\":";
            body += json::str(s.away);
            body += ",\"home_score\":";
            body += std::to_string(s.home_score);
            body += ",\"away_score\":";
            body += std::to_string(s.away_score);
            body += ",\"source\":";
            body += json::str(s.source);
            // 4 时间戳 (R-20): 上游 ts 由 provider 透传, as_of 为本层读取时刻
            body += ",\"event_ts\":";
            body += json::i64(s.ts.event_ts_ns);
            body += ",\"data_source_ts\":";
            body += json::i64(s.ts.data_source_ts_ns);
            body += ",\"ingestion_ts\":";
            body += json::i64(s.ts.ingestion_ts_ns);
            body += ",\"score_as_of_ts\":";
            body += json::i64(s.ts.as_of_ts_ns);
        }

        body += '}';

        res.set_content(body, "application/json; charset=utf-8");
        res.status = 200;  // #1 数据未就绪返 found:false(非404), 减轮询噪音
    });
}

}  // namespace stcpp::debug_api
