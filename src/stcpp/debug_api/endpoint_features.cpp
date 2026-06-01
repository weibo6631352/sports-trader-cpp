// src/stcpp/debug_api/endpoint_features.cpp
//   GET /api/v1/features/health — 110 ML 特征逐列健康 (填充率/非零/range/活死)
// Owner: 老雷 (GM) 2026-06-01 — 可观测 ("特征没问题训练才有意义" 落地)
//   数据源: fv_hub 全市场快照逐列聚合 (RealStateProvider::feature_health)。
//   R-12: 只读快照聚合; 非热路径 (前端轮询)。
#include <string>

#include "src/stcpp/debug_api/endpoint_common.hpp"
#include "src/stcpp/debug_api/json_writer.hpp"
#include "src/stcpp/debug_api/server.hpp"

namespace stcpp::debug_api {

void register_features(httplib::Server& svr, const HttpServer& hs) {
    svr.Get("/api/v1/features/health", [&hs](const httplib::Request& /*req*/, httplib::Response& res) {
        const StateProvider& sp = hs.provider();
        const FeatureHealthReport rep = sp.feature_health();
        const std::int64_t as_of = now_epoch_ns();

        std::string body;
        body.reserve(8192);
        body += "{\"mode\":";
        body += json::str(exec_mode_str(sp.mode()));
        body += ",\"as_of_ts\":";
        body += json::i64(as_of);
        body += ",\"n_records\":";
        body += json::i64(rep.n_records);
        body += ",\"dead\":";
        body += json::i64(rep.dead);
        body += ",\"const\":";
        body += json::i64(rep.constant);
        body += ",\"healthy\":";
        body += json::i64(rep.healthy);
        body += ",\"total\":";
        body += json::i64(static_cast<std::int64_t>(rep.rows.size()));
        body += ",\"rows\":[";
        bool first = true;
        for (const auto& r : rep.rows) {
            if (!first) body += ',';
            first = false;
            body += "{\"i\":";
            body += json::i64(r.index);
            body += ",\"name\":";
            body += json::str(r.name);
            body += ",\"populated\":";
            body += json::i64(r.populated);
            body += ",\"nonzero\":";
            body += json::i64(r.nonzero);
            body += ",\"min\":";
            body += json::num(r.min);
            body += ",\"max\":";
            body += json::num(r.max);
            body += ",\"mean\":";
            body += json::num(r.mean);
            body += ",\"status\":";
            body += json::str(r.status);
            body += '}';
        }
        body += "]}";

        res.set_content(body, "application/json; charset=utf-8");
        res.status = 200;
    });
}

}  // namespace stcpp::debug_api
