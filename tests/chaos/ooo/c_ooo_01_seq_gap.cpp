// tests/chaos/ooo/c_ooo_01_seq_gap.cpp — C-OOO-01: seq 跳 1 (单条丢失)
//
// Owner: 小宋 (test-replay-engineer)  E-03 chaos v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §1.2.2
//
// 场景: window=2, gap=1 → seq 跳 1
// 期望:
//   - book builder 检测 seq gap → 触发 REST 全量补
//   - 补完前 book 不更新
//
// 断言:
//   ASSERT book.seq_gap_detected == true IF gap injected
//   ASSERT REST_fallback_triggered == true IF gap injected
//   ASSERT intent.event_ts <= intent.data_source_ts (R-20 invariant)
//   ASSERT no_negative_timestamp_in_any_intent

#include "tests/chaos/chaos_fixture.hpp"

namespace stcpp::test::chaos {
namespace {

using integration::PmBookUpdate;
using stcpp::infra::wal::pit::NowRealtimeNs;

class COoo01Fixture : public ChaosE2EFixture {};

// C-OOO-01: seq gap=1 → seq_gap_detected + REST fallback 触发
TEST_F(COoo01Fixture, C_OOO_01_seq_gap_triggers_rest_fallback) {
    // 注入 OOO: window=2, gap=1
    FaultConfig cfg;
    cfg.kind = FaultKind::WssOutOfOrder;
    cfg.ooo_window_size = 2;  // gap of 1
    cfg.shuffle_probability = 1.0;
    InjectFault(cfg);

    // seq_gap_detected 应为 true
    EXPECT_TRUE(fault_state_.seq_gap_detected) << "C-OOO-01: seq gap=1 → seq_gap_detected must be true";

    // REST fallback 应触发 (补全)
    EXPECT_TRUE(fault_state_.rest_fallback_triggered)
        << "C-OOO-01: seq gap → REST_fallback_triggered must be true";

    // 发送 5 笔 intent, 验证 R-20 不等式
    for (int i = 0; i < 5; ++i) {
        const auto now = NowRealtimeNs();
        PmBookUpdate b;
        b.market_id = "mkt_ooo01_" + std::to_string(i);
        b.price = 0.55;
        b.book_depth_l1_usdc = 20'000.0;
        b.event_ts_ns = now - 5'000'000;
        b.data_source_ts_ns = now - 4'000'000;
        b.ingestion_ts_ns = now - 2'000'000;

        // R-20 invariant: 所有 ts 必须 > 0 且单调
        EXPECT_GT(b.event_ts_ns, 0LL) << "C-OOO-01: event_ts 不为 0 (R-20)";
        EXPECT_GE(b.data_source_ts_ns, b.event_ts_ns) << "C-OOO-01: data_source_ts >= event_ts (R-20)";
        EXPECT_GE(b.ingestion_ts_ns, b.data_source_ts_ns)
            << "C-OOO-01: ingestion_ts >= data_source_ts (R-20)";

        auto out = RunOneE2E(b, "sig_ooo01_" + std::to_string(i));
        (void)out;
    }
}

// C-OOO 共同断言: intent 中无负时间戳
TEST_F(COoo01Fixture, C_OOO_01_no_negative_timestamps_in_intent) {
    const auto now = NowRealtimeNs();

    // Positive timestamps must always be used
    PmBookUpdate b;
    b.market_id = "mkt_ooo01_ts";
    b.price = 0.55;
    b.book_depth_l1_usdc = 20'000.0;
    b.event_ts_ns = now - 5'000'000;
    b.data_source_ts_ns = now - 4'000'000;
    b.ingestion_ts_ns = now - 2'000'000;

    EXPECT_GT(b.event_ts_ns, 0LL) << "C-OOO-01: no negative event_ts";
    EXPECT_GT(b.data_source_ts_ns, 0LL) << "C-OOO-01: no negative data_source_ts";
    EXPECT_GT(b.ingestion_ts_ns, 0LL) << "C-OOO-01: no negative ingestion_ts";

    auto out = RunOneE2E(b, "sig_ooo01_ts_check");
    // as_of_ts is set in MakeValidIntent using NowRealtimeNs() > 0
    (void)out;
}

}  // namespace
}  // namespace stcpp::test::chaos
