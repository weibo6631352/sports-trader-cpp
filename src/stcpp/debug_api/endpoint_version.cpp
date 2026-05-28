// src/stcpp/debug_api/endpoint_version.cpp — GET /version handler
// Owner: 小卢 (senior-ic-pool)  W9 W2
// 关联:
//   xiaolu-w9-rest-api-skeleton-implementation-spec-v1.md §4.2
//   laozhou-w8-debug-rest-api-spec-v1.md §6 /version schema
//   R-20: build_time 是编译期常量 (不是数据时间戳; 不违反 R-20)
//         as_of_ts = system_clock::now() epoch ns
//
// 响应体:
// {
//   "version": "0.1.0",
//   "git_hash": "<STCPP_GIT_HASH_STR>",
//   "build_mode": "<paper|live|backtest>",
//   "build_time": "May 29 2026 00:00:00",
//   "cpp_standard": "C++20",
//   "as_of_ts": <epoch_ns>
// }
//
// 编译期常量来源:
//   STCPP_VERSION_STR      — configure_file 从 project() VERSION 注入
//   STCPP_GIT_HASH_STR     — configure_file 从 git rev-parse --short HEAD 注入
//   STCPP_EXEC_MODE_STR    — compile definition 从 STCPP_EXEC_MODE 注入
//   build_time             — __DATE__ + " " + __TIME__ (编译期宏, 不违反 R-20)

#include "src/stcpp/debug_api/server.hpp"

// version_generated.hpp: 由 CMake configure_file 生成在 ${CMAKE_CURRENT_BINARY_DIR}
// PRIVATE include path 已在 CMakeLists.txt 中配置
#include "version_generated.hpp"

#include <chrono>
#include <cstdint>
#include <string>

namespace stcpp::debug_api {

static int64_t now_epoch_ns_ver() noexcept
{
    using namespace std::chrono;
    return static_cast<int64_t>(
        duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count()
    );
}

// build_time: __DATE__ + " " + __TIME__
// 例: "May 29 2026 12:34:56"
// 这是编译器注入的常量, 不是运行时 now(), 不违反 R-20
static constexpr const char* k_build_time = __DATE__ " " __TIME__;

void register_version(httplib::Server& svr)
{
    svr.Get("/version", [](const httplib::Request& /*req*/, httplib::Response& res) {
        const int64_t ts = now_epoch_ns_ver();

        // 手拼 JSON (MVP)
        std::string body;
        body.reserve(256);
        body += R"({"version":")";
        body += STCPP_VERSION_STR;
        body += R"(","git_hash":")";
        body += STCPP_GIT_HASH_STR;
        body += R"(","build_mode":")";
        body += STCPP_EXEC_MODE_STR;          // compile definition
        body += R"(","build_time":")";
        body += k_build_time;
        body += R"(","cpp_standard":"C++20","as_of_ts":)";
        body += std::to_string(ts);
        body += '}';

        res.set_content(body, "application/json; charset=utf-8");
        res.status = 200;
    });
}

} // namespace stcpp::debug_api
