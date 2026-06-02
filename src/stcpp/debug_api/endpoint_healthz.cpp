// src/stcpp/debug_api/endpoint_healthz.cpp — GET /healthz handler
// Owner: 小卢 (senior-ic-pool)  W9 W2
// 关联:
//   xiaolu-w9-rest-api-skeleton-implementation-spec-v1.md §4.1
//   laozhou-w8-debug-rest-api-spec-v1.md §6 /healthz schema
//   R-20: as_of_ts = system_clock::now() epoch ns (debug endpoint, 无上游 ts)
//
// 响应体:
// {
//   "ok": true,
//   "threads": {
//     "ingest_reactor": "alive",   <- W9 W2 stub; W10+ 接各模块 watchdog atomic
//     "signal_engine": "alive",
//     "risk_manager": "alive",
//     "paper_signer": "alive",
//     "api_server": "alive"
//   },
//   "uptime_sec": <N>,
//   "as_of_ts": <epoch_ns>
// }
//
// JSON 格式: 手拼 string (MVP OK; Sprint-4 升 glaze — 老周 spec §2.2 + 小卢 spec §1 OQ-4)
// Content-Type: application/json; charset=utf-8
// HTTP status: 200 OK

#include "src/stcpp/debug_api/endpoint_common.hpp"
#include "src/stcpp/debug_api/endpoint_payloads.hpp"
#include "src/stcpp/debug_api/server.hpp"

namespace stcpp::debug_api {

void register_healthz(httplib::Server& svr, const HttpServer& hs)
{
    // body 由 payload::healthz 构建 (与 SSE /stream healthz 通道单一数据源)
    svr.Get("/healthz", [&hs](const httplib::Request& /*req*/, httplib::Response& res) {
        res.set_content(payload::healthz(hs, now_epoch_ns()), "application/json; charset=utf-8");
        res.status = 200;
    });
}

} // namespace stcpp::debug_api
