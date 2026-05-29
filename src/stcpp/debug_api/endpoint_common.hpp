// src/stcpp/debug_api/endpoint_common.hpp — endpoint 公共小工具 (ADR-038 MVP)
// Owner: 小卢 (senior-ic-pool)  ADR-038 MVP
//
// now_epoch_ns(): as_of_ts = system_clock epoch ns (R-20: debug endpoint 读取快照时刻).
//   注意 — 仅用于 endpoint【快照读取】时刻 (as_of_ts 维度), 数据自带的上游 ts
//   (event/data_source/ingestion) 一律由 provider 透传, 禁本地 now() 替代上游 ts (R-20)。

#pragma once

#include <chrono>
#include <cstdint>

namespace stcpp::debug_api {

inline std::int64_t now_epoch_ns() noexcept {
    using namespace std::chrono;
    return static_cast<std::int64_t>(
        duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count());
}

}  // namespace stcpp::debug_api
