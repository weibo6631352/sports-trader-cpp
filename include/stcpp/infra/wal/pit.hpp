// stcpp/infra/wal/pit.hpp — PIT assert chain (R-20 §7, p99 < 100 ns 内联无分支)
//
// 落:
//   laowang-wal-framework-v0.2.md §4 (AssertChain / DiagnoseViolation)
//   laowang-wal-framework-cpp-interface-v1.md §5
//   laozhou v0.5 §21 (R-20 4 ts 不等式)
//
// 红线 R-20: event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts ≤ now_utc_ns()
// 同步 Append 必调; 不暴露 skip; 内联无分支 (短路 && 编译期展开).
//
// 不耻下问: now() 来源 @老练 OQ-10 — REALTIME (与 as_of_ts 同源, 单调性靠 framework 拒倒流)

#pragma once

#include <cstdint>
#include <ctime>

#include "stcpp/infra/wal/wal_record_header.hpp"

namespace stcpp::infra::wal::pit {

enum class PitViolation : std::uint8_t {
    Ok = 0,
    EventTsZero = 1,          // → INVALID_INTENT.sub=BOOK_TS_ZERO
    DsBeforeEvent = 2,        // → INVALID_INTENT.sub=TS_ORDER_VIOLATED
    IngestionBeforeDs = 3,    // → 同上
    AsOfBeforeIngestion = 4,  // → 同上
    AsOfInFuture = 5,         // → INVALID_INTENT.sub=TS_FUTURE
};

// 内部辅助: now_realtime_ns. Append 同步路径 100ns budget — Linux REALTIME ~25ns.
[[nodiscard]] inline std::int64_t NowRealtimeNs() noexcept {
    timespec ts{};
    ::clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<std::int64_t>(ts.tv_sec) * 1'000'000'000LL + static_cast<std::int64_t>(ts.tv_nsec);
}

// 同步 Append 必调. 短路无分支 (链式 &&). p99 ≤ 100 ns.
//
// 5 个断言 (R-20):
//   1) event_ts > 0 (非零)
//   2) data_source_ts ≥ event_ts
//   3) ingestion_ts   ≥ data_source_ts
//   4) as_of_ts       ≥ ingestion_ts
//   5) as_of_ts       ≤ now() (穿越未来拒收)
[[nodiscard]] inline bool AssertChain(const WalRecordHeader& h) noexcept {
    const std::int64_t now = NowRealtimeNs();
    return (h.event_ts_ns > 0) && (h.data_source_ts_ns >= h.event_ts_ns) &&
           (h.ingestion_ts_ns >= h.data_source_ts_ns) && (h.as_of_ts_ns >= h.ingestion_ts_ns) &&
           (h.as_of_ts_ns <= now);
}

// 慢路径诊断 (Append 失败后, emit risk_audit RECON_DRIFT 时调). 不进热路径.
[[nodiscard]] inline PitViolation DiagnoseViolation(const WalRecordHeader& h) noexcept {
    if (h.event_ts_ns <= 0)
        return PitViolation::EventTsZero;
    if (h.data_source_ts_ns < h.event_ts_ns)
        return PitViolation::DsBeforeEvent;
    if (h.ingestion_ts_ns < h.data_source_ts_ns)
        return PitViolation::IngestionBeforeDs;
    if (h.as_of_ts_ns < h.ingestion_ts_ns)
        return PitViolation::AsOfBeforeIngestion;
    if (h.as_of_ts_ns > NowRealtimeNs())
        return PitViolation::AsOfInFuture;
    return PitViolation::Ok;
}

}  // namespace stcpp::infra::wal::pit
