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
// 序列化单一数据源 (2026-06-02 去冗余, 老板): body 调 payload::quote (与 SSE quote 通道复用)。
// 响应: 200 + QuoteParams 全字段 (found=true) / {found:false,...} (found=false, 非404 减噪)

#include <string>

#include "src/stcpp/debug_api/endpoint_payloads.hpp"
#include "src/stcpp/debug_api/server.hpp"

namespace stcpp::debug_api {

void register_quote(httplib::Server& svr, const HttpServer& hs) {
    svr.Get(R"(/api/v1/quote/([^/]+))", [&hs](const httplib::Request& req, httplib::Response& res) {
        const std::string condition_id = req.matches[1];
        res.set_content(payload::quote(hs.provider(), condition_id, now_epoch_ns()),
                        "application/json; charset=utf-8");
        res.status = 200;  // 数据未就绪返 found:false(非404), 减轮询噪音
    });
}

}  // namespace stcpp::debug_api
