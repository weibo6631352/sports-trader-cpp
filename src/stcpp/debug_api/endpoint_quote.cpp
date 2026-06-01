// src/stcpp/debug_api/endpoint_quote.cpp
//   GET /api/v1/quote/{condition_id}  (ADR-038 增量; 前端 v3 盯盘 demo 数据)
// Owner: 小卢 (senior-ic-pool)  2026-05-29
// 关联:
//   ADR-038 §4 / G-FREEZE-W (只增不改名)
//   R-20: as_of_ts epoch_ns int64
//   R-12: 观测侧零反向依赖; provider.quote_params() 只读
//   量化字段集: 小梁量化部决议 — de-vig fair prob / fair vs mid / edge_bps net /
//               Kelly / signal/α / model confidence
//
// 响应:
//   200 + QuoteParams 全字段 (found=true)
//   404 + {found:false, market_id, mode, as_of_ts} (found=false)

#include <string>

#include "src/stcpp/debug_api/endpoint_common.hpp"
#include "src/stcpp/debug_api/json_writer.hpp"
#include "src/stcpp/debug_api/server.hpp"

namespace stcpp::debug_api {

void register_quote(httplib::Server& svr, const HttpServer& hs) {
    svr.Get(R"(/api/v1/quote/([^/]+))", [&hs](const httplib::Request& req, httplib::Response& res) {
        const StateProvider& sp = hs.provider();
        const std::string condition_id = req.matches[1];
        const QuoteParams q = sp.quote_params(condition_id);
        const std::int64_t as_of = now_epoch_ns();

        std::string body;
        body.reserve(384);
        body += "{\"mode\":";
        body += json::str(exec_mode_str(sp.mode()));
        body += ",\"as_of_ts\":";
        body += json::i64(as_of);
        body += ",\"found\":";
        body += json::boolean(q.found);
        body += ",\"market_id\":";
        body += json::str(q.market_id);

        if (q.found) {
            body += ",\"fair_value\":";
            body += json::num(q.fair_value);
            body += ",\"market_mid\":";
            body += json::num(q.market_mid);
            body += ",\"edge_bps\":";
            body += json::num(q.edge_bps);
            body += ",\"kelly_fraction\":";
            body += json::num(q.kelly_fraction);
            body += ",\"suggested_notional\":";
            body += json::num(q.suggested_notional);
            body += ",\"signal_strength\":";
            body += json::num(q.signal_strength);
            // DEPRECATED: model_conf — 保留向后兼容 alias (G-FREEZE-W; 值 = model_confidence)
            body += ",\"model_conf\":";
            body += json::num(q.model_conf);
            body += ",\"quote_as_of_ts\":";
            body += json::i64(q.as_of_ts_ns);
            // ---- ML provenance (小邓 spec v1 §3.3; G-FREEZE-W append-only) ----
            body += ",\"model_id\":";
            body += json::str(q.model_id);
            body += ",\"model_kind\":";
            body += json::str(q.model_kind);
            body += ",\"spec_version\":";
            body += json::str(q.spec_version);
            body += ",\"model_confidence\":";
            body += json::num(q.model_confidence);
            body += ",\"model_calibrated\":";
            body += json::boolean(q.model_calibrated);
            body += ",\"fair_ci_lower\":";
            body += json::num(q.fair_ci_lower);
            body += ",\"fair_ci_upper\":";
            body += json::num(q.fair_ci_upper);
            body += ",\"predict_ok\":";
            body += json::boolean(q.predict_ok);
            body += ",\"model_as_of_ts\":";
            body += json::i64(q.model_as_of_ts_ns);
            body += ",\"advisory\":";
            body += json::boolean(q.advisory);
            // ---- 调试可观测: fair 来源分解 (G-FREEZE-W append-only) ----
            body += ",\"sharp_fair\":";
            body += json::num(q.sharp_fair);
            body += ",\"devig_ok\":";
            body += json::boolean(q.devig_ok);
            body += ",\"g_time_x_lead\":";
            body += json::num(q.g_time_x_lead);
            body += ",\"joint_as_of_ts\":";
            body += json::i64(q.joint_as_of_ts_ns);
        }

        body += '}';

        res.set_content(body, "application/json; charset=utf-8");
        res.status = 200;  // #1 数据未就绪返 found:false(非404), 减轮询噪音
    });
}

}  // namespace stcpp::debug_api
