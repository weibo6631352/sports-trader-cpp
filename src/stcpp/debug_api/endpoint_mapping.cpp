// src/stcpp/debug_api/endpoint_mapping.cpp
//   GET /api/v1/mapping/status — condition ↔ Goalserve event 映射实况 (可观测)
// Owner: 老雷 (GM) 2026-06-01 — 闭合探针缺口A (映射只能 grep 日志看)
//   数据源: daemon RefreshEventMapping 周期 push 到 RealStateProvider (互斥快照)。
//   R-12: 只读快照; 非热路径 (前端轮询)。
#include <string>

#include "src/stcpp/debug_api/endpoint_common.hpp"
#include "src/stcpp/debug_api/endpoint_payloads.hpp"
#include "src/stcpp/debug_api/server.hpp"

namespace stcpp::debug_api {

void register_mapping(httplib::Server& svr, const HttpServer& hs) {
    // body 由 payload::mapping_status 构建 (与 SSE /stream mapping 通道单一数据源)
    svr.Get("/api/v1/mapping/status", [&hs](const httplib::Request& /*req*/, httplib::Response& res) {
        res.set_content(payload::mapping_status(hs.provider(), now_epoch_ns()), "application/json; charset=utf-8");
        res.status = 200;
    });
}

}  // namespace stcpp::debug_api
