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
#include "src/stcpp/debug_api/endpoint_common.hpp"
#include "src/stcpp/debug_api/endpoint_payloads.hpp"
#include "src/stcpp/debug_api/server.hpp"

namespace stcpp::debug_api {

void register_status(httplib::Server& svr, const HttpServer& hs) {
    // body 由 payload::status 构建 (与 SSE /stream status 通道单一数据源; REST 传 now → 含 as_of_ts)
    svr.Get("/status", [&hs](const httplib::Request& /*req*/, httplib::Response& res) {
        res.set_content(payload::status(hs, now_epoch_ns()), "application/json; charset=utf-8");
        res.status = 200;
    });
}

}  // namespace stcpp::debug_api
