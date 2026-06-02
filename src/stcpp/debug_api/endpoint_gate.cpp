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

#include "src/stcpp/debug_api/endpoint_common.hpp"
#include "src/stcpp/debug_api/endpoint_payloads.hpp"
#include "src/stcpp/debug_api/server.hpp"

namespace stcpp::debug_api {

void register_gate(httplib::Server& svr, const HttpServer& hs) {
    // body 由 payload::gate 构建 (与 SSE /stream gate 通道单一数据源)
    svr.Get("/api/v1/gate/paper", [&hs](const httplib::Request& /*req*/, httplib::Response& res) {
        res.set_content(payload::gate(hs.provider(), now_epoch_ns()), "application/json; charset=utf-8");
        res.status = 200;
    });
}

}  // namespace stcpp::debug_api
