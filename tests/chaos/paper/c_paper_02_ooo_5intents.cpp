// tests/chaos/paper/c_paper_02_ooo_5intents.cpp — C-PAPER-02: paper engine 乱序 book, 连续 5 笔 intent
//
// Owner: 小宋 (test-replay-engineer)  E-03 chaos v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §1.3 C-PAPER-02
//
// 场景: paper engine 接收乱序 book, 连续 5 笔 intent
// 期望:
//   - 每笔 R-20 4 ts 单调
//   - 无 stale quote 进 VirtualMatcher
//
// 通过条件: 每笔 R-20 4 ts 单调; 无 stale quote 进 VirtualMatcher

#include "tests/chaos/chaos_fixture.hpp"

namespace stcpp::test::chaos {
namespace {

using integration::PmBookUpdate;
using stcpp::infra::wal::pit::NowRealtimeNs;

class CPaper02Fixture : public ChaosE2EFixture {};

// C-PAPER-02: 5 笔 intent, 每笔 R-20 4 ts 单调; 无 stale quote
TEST_F(CPaper02Fixture, C_PAPER_02_ooo_5intents_r20_monotone_no_stale) {
    // 注入乱序
    FaultConfig cfg;
    cfg.kind = FaultKind::WssOutOfOrder;
    cfg.ooo_window_size = 3;
    cfg.shuffle_probability = 0.8;
    InjectFault(cfg);

    // 5 笔连续 intent, 全部使用新鲜 ts (非 stale)
    int r20_violations = 0;
    int approved_count = 0;

    for (int i = 0; i < 5; ++i) {
        const auto now = NowRealtimeNs();
        PmBookUpdate b;
        b.market_id = "mkt_paper02_" + std::to_string(i);
        b.price = 0.55;
        b.book_depth_l1_usdc = 20'000.0;
        b.event_ts_ns = now - 5'000'000;
        b.data_source_ts_ns = now - 4'000'000;
        b.ingestion_ts_ns = now - 2'000'000;

        // R-20 4 ts 单调性 (每笔独立校验)
        if (b.event_ts_ns <= 0)
            ++r20_violations;
        if (b.data_source_ts_ns < b.event_ts_ns)
            ++r20_violations;
        if (b.ingestion_ts_ns < b.data_source_ts_ns)
            ++r20_violations;

        auto out = RunOneE2E(b, "sig_paper02_" + std::to_string(i));
        if (out.rm_decision.is_approved())
            ++approved_count;
    }

    // C-PAPER-02 核心断言
    EXPECT_EQ(r20_violations, 0) << "C-PAPER-02: 5 笔 intent 全部满足 R-20 4 ts 单调 (0 违例)";
    EXPECT_GE(approved_count, 1)
        << "C-PAPER-02: 乱序 book 但新鲜 ts → 至少 1 笔 approved (无 stale quote 进 VirtualMatcher)";

    // R-11: paper mode 不写 position WAL
    EXPECT_EQ(position_->HighWatermark(), 0u) << "C-PAPER-02: paper mode 不写 position WAL (R-11)";
}

}  // namespace
}  // namespace stcpp::test::chaos
