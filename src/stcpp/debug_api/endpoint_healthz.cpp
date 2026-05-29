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

#include <chrono>
#include <cstdint>
#include <string>

#include "src/stcpp/debug_api/server.hpp"

namespace stcpp::debug_api {

// as_of_ts: system_clock epoch nanoseconds (R-20 语义: debug endpoint 读取快照的本地时刻)
static int64_t now_epoch_ns() noexcept {
    using namespace std::chrono;
    return static_cast<int64_t>(duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count());
}

// uptime_sec: 从 start_time (steady_clock) 到 now
static int64_t uptime_sec(std::chrono::steady_clock::time_point start) noexcept {
    using namespace std::chrono;
    return static_cast<int64_t>(duration_cast<seconds>(steady_clock::now() - start).count());
}

void register_healthz(httplib::Server& svr, const HttpServer& hs) {
    svr.Get("/healthz", [&hs](const httplib::Request& /*req*/, httplib::Response& res) {
        // W9 W2 stub: 所有 thread heartbeat = "alive"
        // W10+ 接 watchdog atomic bool per-thread
        const int64_t ts = now_epoch_ns();
        const int64_t uptime = uptime_sec(hs.start_time());

        // 手拼 JSON (MVP; non-hot-path, 性能不敏感)
        std::string body;
        body.reserve(256);
        body += R"({"ok":true,"threads":{)";
        body += R"("ingest_reactor":"alive",)";
        body += R"("signal_engine":"alive",)";
        body += R"("risk_manager":"alive",)";
        body += R"("paper_signer":"alive",)";
        body += R"("api_server":"alive")";
        body += R"(},"uptime_sec":)";
        body += std::to_string(uptime);
        body += R"(,"as_of_ts":)";
        body += std::to_string(ts);
        body += '}';

        res.set_content(body, "application/json; charset=utf-8");
        res.status = 200;
    });
}

}  // namespace stcpp::debug_api
