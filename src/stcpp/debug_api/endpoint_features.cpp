// src/stcpp/debug_api/endpoint_features.cpp
//   GET /api/v1/features/health — 110 ML 特征逐列健康 (填充率/非零/range/活死)
// Owner: 老雷 (GM) 2026-06-01 — 可观测 ("特征没问题训练才有意义" 落地)
//   数据源: fv_hub 全市场快照逐列聚合 (RealStateProvider::feature_health)。
//   R-12: 只读快照聚合; 非热路径 (前端轮询)。
#include <string>

#include "src/stcpp/debug_api/endpoint_common.hpp"
#include "src/stcpp/debug_api/endpoint_payloads.hpp"
#include "src/stcpp/debug_api/server.hpp"

namespace stcpp::debug_api {

void register_features(httplib::Server& svr, const HttpServer& hs) {
    // body 由 payload::features_health 构建 (与 SSE /stream features 通道单一数据源)
    svr.Get("/api/v1/features/health", [&hs](const httplib::Request& /*req*/, httplib::Response& res) {
        res.set_content(payload::features_health(hs.provider(), now_epoch_ns()), "application/json; charset=utf-8");
        res.status = 200;
    });
}

}  // namespace stcpp::debug_api
