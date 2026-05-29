// src/stcpp/debug_api/endpoint_gate.cpp — GET /api/v1/gate/paper (ADR-038 §4.4)
// Owner: 小卢 (senior-ic-pool)  ADR-038 MVP
// 关联:
//   ADR-038 §4.4 GM-PAPER-G 30 日滚动:
//     n_trades / positive_day_ratio / sharpe(+se) / p_value / hit_rate / mdd
//     / prelim_pass / confirm_pass
//   §3 schema 铁律: 顶层 mode (R-11), as_of_ts epoch_ns int64 (R-20)
//
// 只读: provider.paper_gate()。MVP stub 字段可空 (has_data=false 表门禁未积累数据),
//        真实接入 = 小蒋/小梁 paper 滚动统计。

#include <string>

#include "src/stcpp/debug_api/endpoint_common.hpp"
#include "src/stcpp/debug_api/json_writer.hpp"
#include "src/stcpp/debug_api/server.hpp"

namespace stcpp::debug_api {

void register_gate(httplib::Server& svr, const HttpServer& hs) {
    svr.Get("/api/v1/gate/paper", [&hs](const httplib::Request& /*req*/, httplib::Response& res) {
        const StateProvider& sp = hs.provider();
        const PaperGate g = sp.paper_gate();
        const std::int64_t as_of = now_epoch_ns();

        std::string body;
        body.reserve(384);
        body += "{\"mode\":";
        body += json::str(exec_mode_str(sp.mode()));
        body += ",\"as_of_ts\":";
        body += json::i64(as_of);
        body += ",\"window_days\":";
        body += json::i64(g.window_days);
        body += ",\"has_data\":";
        body += json::boolean(g.has_data);
        body += ",\"n_trades\":";
        body += json::i64(g.n_trades);
        body += ",\"positive_day_ratio\":";
        body += json::num(g.positive_day_ratio);
        body += ",\"sharpe\":";
        body += json::num(g.sharpe);
        body += ",\"sharpe_se\":";
        body += json::num(g.sharpe_se);
        body += ",\"p_value\":";
        body += json::num(g.p_value);
        body += ",\"hit_rate\":";
        body += json::num(g.hit_rate);
        body += ",\"max_drawdown\":";
        body += json::num(g.max_drawdown);
        body += ",\"prelim_pass\":";
        body += json::boolean(g.prelim_pass);
        body += ",\"confirm_pass\":";
        body += json::boolean(g.confirm_pass);
        body += '}';

        res.set_content(body, "application/json; charset=utf-8");
        res.status = 200;
    });
}

}  // namespace stcpp::debug_api
