// tests/replay/r20/r_r20_07_paper_30d_attestation.cpp — R-R20-07 (paper): 30 日窗口 attestation
//
// Owner: 小宋 (test-replay-engineer)  E-04 replay framework v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §2.4.2
//
// 场景: paper runtime 30 日窗口 R-20 attestation 校验
// 期望: 0 违例 + 小余 (D 主管) 签字前提: attestation 输出
//
// OQ-04 处理: inferred_ts_count 计数, 不 FAIL; 月度 sweep 小冯负责

#include <gtest/gtest.h>

#include "stcpp/infra/wal/pit.hpp"
#include "tests/replay/paper/r20_ts_assertion.hpp"

namespace stcpp::test::replay {
namespace {

// R-R20-07: 30 日窗口 attestation → violation_count==0 (小余签字前提)
TEST(R20Replay30Day, R_R20_07_paper_30d_attestation_zero_violations) {
    TimestampMonotonicityAssertion r20;

    // 模拟 30 天 × 500 笔/天 = 15000 条 (OQ-02 stub)
    constexpr int kTotalEvents = 15000;
    const std::int64_t now_ns  = stcpp::infra::wal::pit::NowRealtimeNs();
    const std::int64_t day_ns  = 86'400'000'000'000LL;

    for (int i = 0; i < kTotalEvents; ++i) {
        const int    day        = i / 500;
        const int    within_day = i % 500;
        const std::int64_t base = static_cast<std::int64_t>(day) * day_ns
            + static_cast<std::int64_t>(within_day) * 172'800'000'000LL;  // ~172.8s step

        const std::int64_t event_ts       = now_ns - (30LL * day_ns) + base;
        const std::int64_t data_source_ts = event_ts + 200'000;
        const std::int64_t ingestion_ts   = data_source_ts + 800'000;
        const std::int64_t as_of_ts       = ingestion_ts + 2'000'000;

        const bool ok = r20.on_event(
            static_cast<std::uint64_t>(i + 1),
            event_ts, data_source_ts, ingestion_ts, as_of_ts);
        EXPECT_TRUE(ok) << "R-R20-07: event " << i << " R-20 violation at day=" << day;
    }

    // attestation: finalize
    const bool ok = r20.finalize();

    // 小余 (D 主管) 签字前提: violation_count == 0
    EXPECT_TRUE(ok)
        << "R-R20-07: 30 日 paper run 0 违例 (小余签字前提)";
    EXPECT_EQ(r20.violation_count(), 0u)
        << "R-R20-07: violation_count == 0 (attestation 通过)";
    EXPECT_EQ(r20.checked_count(), static_cast<std::uint64_t>(kTotalEvents))
        << "R-R20-07: " << kTotalEvents << " 条事件全部校验";

    // OQ-04: inferred_ts_count 记录 (不 FAIL, 月度 sweep 小冯用)
    // 本 stub 无 INFERRED_FROM_INGESTION, 所以 inferred_ts_count == 0
    EXPECT_EQ(r20.inferred_ts_count(), 0u)
        << "R-R20-07: 无推断 ts (stub 数据全部合法)";
}

}  // namespace
}  // namespace stcpp::test::replay
