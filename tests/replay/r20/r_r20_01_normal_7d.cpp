// tests/replay/r20/r_r20_01_normal_7d.cpp — R-R20-01: 正常历史流 7 天回放
//
// Owner: 小宋 (test-replay-engineer)  E-04 replay framework v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §2.4.2
//
// 场景: 正常历史流 (7 天 paper run 模拟)
// 期望: violation_count == 0; inferred_ts_count 记录供月度 sweep

#include <gtest/gtest.h>

#include "stcpp/infra/wal/pit.hpp"
#include "tests/replay/paper/r20_ts_assertion.hpp"

namespace stcpp::test::replay {
namespace {

// R-R20-01: 正常历史流 7 天 → 0 违例
TEST(R20ReplayNormal, R_R20_01_normal_7d_zero_violations) {
    TimestampMonotonicityAssertion r20;

    // 模拟 7 天 × 1000 笔/天 = 7000 条事件 (OQ-02 stub: 待小余 EventRecorder 就绪)
    constexpr int kTotalEvents = 7000;

    // 7 天时间范围: 当前时刻起, 每条事件间隔 86400s/1000 ≈ 86.4s
    const std::int64_t now_ns = stcpp::infra::wal::pit::NowRealtimeNs();
    const std::int64_t day_ns = 86'400'000'000'000LL;  // 1 day in ns

    for (int i = 0; i < kTotalEvents; ++i) {
        const int    day         = i / 1000;   // 0-6
        const int    within_day  = i % 1000;
        const std::int64_t base_offset_ns = static_cast<std::int64_t>(day) * day_ns
            + static_cast<std::int64_t>(within_day) * 86'400'000'000LL;  // ~86s step

        // 合法 4 ts (相对 7d 前)
        const std::int64_t event_ts       = now_ns - (7LL * day_ns) + base_offset_ns;
        const std::int64_t data_source_ts = event_ts + 100'000;      // +100us
        const std::int64_t ingestion_ts   = data_source_ts + 500'000; // +500us
        const std::int64_t as_of_ts       = ingestion_ts + 1'000'000; // +1ms

        const bool ok = r20.on_event(
            static_cast<std::uint64_t>(i + 1),
            event_ts, data_source_ts, ingestion_ts, as_of_ts);
        EXPECT_TRUE(ok) << "R-R20-01: event " << i << " should be OK";
    }

    const bool finalize_ok = r20.finalize();
    EXPECT_TRUE(finalize_ok)
        << "R-R20-01: violation_count=" << r20.violation_count()
        << " 必须 == 0 (7 天正常历史流)";
    EXPECT_EQ(r20.violation_count(), 0u)
        << "R-R20-01: 0 违例 (正常历史流)";
    EXPECT_EQ(r20.checked_count(), static_cast<std::uint64_t>(kTotalEvents))
        << "R-R20-01: 全部 " << kTotalEvents << " 条事件被校验";
}

}  // namespace
}  // namespace stcpp::test::replay
