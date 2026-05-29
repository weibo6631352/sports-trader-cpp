// tests/replay/r20/r_r20_violations.cpp — R-R20-02~05: 4 ts 违例注入检测
//
// Owner: 小宋 (test-replay-engineer)  E-04 replay framework v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §2.4.2
//
// 覆盖:
//   R-R20-02: 注入 event_ts=0 → 检出 violation; 定位 seq; FAIL
//   R-R20-03: 注入 data_source_ts < event_ts → 检出; 定位; FAIL
//   R-R20-04: 注入 ingestion_ts < data_source_ts → 检出; 定位; FAIL
//   R-R20-05: 注入 as_of_ts < ingestion_ts → 检出; 定位; FAIL

#include <gtest/gtest.h>

#include "stcpp/infra/wal/pit.hpp"
#include "tests/replay/paper/r20_ts_assertion.hpp"

namespace stcpp::test::replay {
namespace {

// R-R20-02: event_ts=0 → 违例检出 + 定位 seq
TEST(R20ReplayViolations, R_R20_02_event_ts_zero_detected) {
    TimestampMonotonicityAssertion r20;
    const std::int64_t now = stcpp::infra::wal::pit::NowRealtimeNs();

    // 5 条正常事件
    for (int i = 0; i < 5; ++i) {
        r20.on_event(static_cast<std::uint64_t>(i + 1),
            now - 5'000'000, now - 4'000'000, now - 2'000'000, now);
    }
    EXPECT_EQ(r20.violation_count(), 0u) << "R-R20-02: 5 条正常 → 0 违例";

    // 注入 event_ts=0 (R-20 红线: 禁用本地 now() 替代上游 ts)
    constexpr std::uint64_t kBadSeq = 6;
    const bool ok = r20.on_event(kBadSeq, 0, now, now + 1'000, now + 2'000);
    EXPECT_FALSE(ok) << "R-R20-02: event_ts=0 → on_event 返回 false";
    EXPECT_GE(r20.violation_count(), 1u) << "R-R20-02: 违例检出";

    // 定位到 seq=6
    bool found = false;
    for (const auto& v : r20.violations()) {
        if (v.seq == kBadSeq) { found = true; break; }
    }
    EXPECT_TRUE(found) << "R-R20-02: 违例定位到 seq=" << kBadSeq;

    // finalize → FAIL
    EXPECT_FALSE(r20.finalize()) << "R-R20-02: violation_count > 0 → finalize FAIL";

    // 违例信息可读
    const auto dump = r20.dump_violations();
    EXPECT_FALSE(dump.empty()) << "R-R20-02: dump_violations() 非空 (diff 可读)";
}

// R-R20-03: data_source_ts < event_ts → 违例检出
TEST(R20ReplayViolations, R_R20_03_ds_lt_event_ts_detected) {
    TimestampMonotonicityAssertion r20;
    const std::int64_t now = stcpp::infra::wal::pit::NowRealtimeNs();

    constexpr std::uint64_t kBadSeq = 1;
    // data_source_ts < event_ts (违例)
    const bool ok = r20.on_event(kBadSeq,
        now - 5'000'000,      // event_ts
        now - 6'000'000,      // data_source_ts < event_ts ← 违例
        now - 2'000'000,
        now);
    EXPECT_FALSE(ok)            << "R-R20-03: ds < event → on_event false";
    EXPECT_GE(r20.violation_count(), 1u) << "R-R20-03: 违例检出";

    bool found = false;
    for (const auto& v : r20.violations()) {
        if (v.seq == kBadSeq) { found = true; break; }
    }
    EXPECT_TRUE(found) << "R-R20-03: 违例定位到 seq=" << kBadSeq;
    EXPECT_FALSE(r20.finalize()) << "R-R20-03: finalize FAIL";
}

// R-R20-04: ingestion_ts < data_source_ts → 违例检出
TEST(R20ReplayViolations, R_R20_04_ingestion_lt_ds_detected) {
    TimestampMonotonicityAssertion r20;
    const std::int64_t now = stcpp::infra::wal::pit::NowRealtimeNs();

    constexpr std::uint64_t kBadSeq = 1;
    const bool ok = r20.on_event(kBadSeq,
        now - 5'000'000,
        now - 4'000'000,
        now - 5'000'000,  // ingestion_ts < data_source_ts ← 违例
        now);
    EXPECT_FALSE(ok) << "R-R20-04: ingestion < ds → on_event false";
    EXPECT_GE(r20.violation_count(), 1u) << "R-R20-04: 违例检出";

    bool found = false;
    for (const auto& v : r20.violations()) {
        if (v.seq == kBadSeq) { found = true; break; }
    }
    EXPECT_TRUE(found) << "R-R20-04: 违例定位到 seq=" << kBadSeq;
    EXPECT_FALSE(r20.finalize()) << "R-R20-04: finalize FAIL";
}

// R-R20-05: as_of_ts < ingestion_ts → 违例检出
TEST(R20ReplayViolations, R_R20_05_asof_lt_ingestion_detected) {
    TimestampMonotonicityAssertion r20;
    const std::int64_t now = stcpp::infra::wal::pit::NowRealtimeNs();

    constexpr std::uint64_t kBadSeq = 1;
    const bool ok = r20.on_event(kBadSeq,
        now - 5'000'000,
        now - 4'000'000,
        now - 2'000'000,
        now - 3'000'000);  // as_of_ts < ingestion_ts ← 违例
    EXPECT_FALSE(ok) << "R-R20-05: as_of < ingestion → on_event false";
    EXPECT_GE(r20.violation_count(), 1u) << "R-R20-05: 违例检出";

    bool found = false;
    for (const auto& v : r20.violations()) {
        if (v.seq == kBadSeq) { found = true; break; }
    }
    EXPECT_TRUE(found) << "R-R20-05: 违例定位到 seq=" << kBadSeq;
    EXPECT_FALSE(r20.finalize()) << "R-R20-05: finalize FAIL";
}

}  // namespace
}  // namespace stcpp::test::replay
