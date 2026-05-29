// src/stcpp/debug_api/endpoint_status.cpp — GET /status handler
// Owner: 小卢 (senior-ic-pool)  W9 W2
// 关联:
//   xiaolu-w9-rest-api-skeleton-implementation-spec-v1.md §4.3
//   laozhou-w8-debug-rest-api-spec-v1.md §6 /status schema
//   R-20: as_of_ts = system_clock::now() epoch ns (状态快照读取时刻)
//
// W9 W2 stub:
//   state                  = "RUNNING"   (无 RM SystemState atomic 接入)
//   mode                   = STCPP_EXEC_MODE_STR (编译期常量)
//   wss_connected.*        = false        (W9 W3 接 WSS monitor)
//   signals_active_count   = 0            (W9 W3 接 SignalEngine)
//   positions_count        = 0            (W9 W3 接 PositionLedger)
//   rm_rejects_last_60s    = 0            (W9 W3 接 老韩 RmDebugSnapshot)
//   uptime_sec             = 实际值 (来自 HttpServer start_time_)
//
// 字段 state 枚举值精确匹配老周 spec §6: "RUNNING" / "DRAIN" / "HALTED"
// W9 W3 接老韩 SystemState atomic 后 state 变为真实值

#include <chrono>
#include <cstdint>
#include <string>

#include "src/stcpp/debug_api/server.hpp"

namespace stcpp::debug_api {

static int64_t now_epoch_ns_status() noexcept {
    using namespace std::chrono;
    return static_cast<int64_t>(duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count());
}

static int64_t uptime_sec_status(std::chrono::steady_clock::time_point start) noexcept {
    using namespace std::chrono;
    return static_cast<int64_t>(duration_cast<seconds>(steady_clock::now() - start).count());
}

void register_status(httplib::Server& svr, const HttpServer& hs) {
    svr.Get("/status", [&hs](const httplib::Request& /*req*/, httplib::Response& res) {
        const int64_t ts = now_epoch_ns_status();
        const int64_t uptime = uptime_sec_status(hs.start_time());

        // W9 W2 stub 字段 (W9 W3 接真实 atomic snapshot)
        // state: "RUNNING" stub; mode: 编译期常量; wss: all false; counts: 0
        std::string body;
        body.reserve(512);
        body += R"({"state":"RUNNING","mode":")";
        body += STCPP_EXEC_MODE_STR;
        body += R"(","wss_connected":{)";
        body += R"("sports_api":false,)";
        body += R"("clob":false,)";
        body += R"("user_channel":false)";
        body += R"(},"signals_active_count":0)";
        body += R"(,"positions_count":0)";
        body += R"(,"rm_rejects_last_60s":0)";
        body += R"(,"uptime_sec":)";
        body += std::to_string(uptime);
        body += R"(,"as_of_ts":)";
        body += std::to_string(ts);
        // 前端 v3 DEMO 标记 (老钱红线): data_source 由 provider.data_source() 提供
        body += R"(,"data_source":")";
        body += hs.provider().data_source();
        body += '"';
        body += '}';

        res.set_content(body, "application/json; charset=utf-8");
        res.status = 200;
    });
}

}  // namespace stcpp::debug_api
