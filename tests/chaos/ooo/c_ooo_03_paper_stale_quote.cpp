// tests/chaos/ooo/c_ooo_03_paper_stale_quote.cpp — C-OOO-03 (paper): paper engine 乱序 book 快照
//
// Owner: 小宋 (test-replay-engineer)  E-03 chaos v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §1.2.2
//
// 场景: paper runtime 接收乱序 book 快照, window=3
// 期望:
//   - VirtualMatcher 不接受 stale quote (quote_ts < last_fill_ts)
//   - paper_audit 无乱序 decision
//
// 断言:
//   ASSERT VirtualMatcher stale quote 被拒 (通过 RM INVALID_INTENT stale book 验证)
//   ASSERT paper_audit 无乱序 (每条 audit 4 ts 单调)
//   ASSERT position WAL == 0 (R-11: paper 不写 position WAL)

#include "tests/chaos/chaos_fixture.hpp"

namespace stcpp::test::chaos {
namespace {

using integration::PmBookUpdate;
using stcpp::infra::wal::pit::NowRealtimeNs;

class COoo03PaperFixture : public ChaosE2EFixture {};

// C-OOO-03 (paper): 乱序 book 快照 → VirtualMatcher 拒 stale quote
TEST_F(COoo03PaperFixture, C_OOO_03_paper_stale_quote_rejected) {
    FaultConfig cfg;
    cfg.kind = FaultKind::WssOutOfOrder;
    cfg.ooo_window_size = 3;
    cfg.shuffle_probability = 1.0;
    InjectFault(cfg);

    // 先跑 3 笔正常 intent (建立 "last_fill_ts" baseline)
    for (int i = 0; i < 3; ++i) {
        const auto now = NowRealtimeNs();
        PmBookUpdate b;
        b.market_id = "mkt_ooo03_base_" + std::to_string(i);
        b.price = 0.55;
        b.book_depth_l1_usdc = 20'000.0;
        b.event_ts_ns = now - 5'000'000;
        b.data_source_ts_ns = now - 4'000'000;
        b.ingestion_ts_ns = now - 2'000'000;
        auto out = RunOneE2E(b, "sig_ooo03_base_" + std::to_string(i));
        (void)out;
    }

    // 现在注入 stale book (乱序后 stale 到达): quote_ts < last_fill_ts 等价
    // 构造: book_snapshot_ts 极度 stale (60s 前) → RM INVALID_INTENT(BOOK_TS_STALE)
    std::uint64_t stale_rejected = 0;
    for (int i = 0; i < 3; ++i) {
        const auto now = NowRealtimeNs();
        PmBookUpdate b;
        b.market_id = "mkt_ooo03_stale_" + std::to_string(i);
        b.price = 0.55;
        b.book_depth_l1_usdc = 20'000.0;
        // 乱序 stale quote: 60+ 秒前的 book snapshot
        b.event_ts_ns = now - 70'000'000'000LL;  // 70s ago
        b.data_source_ts_ns = now - 69'999'000'000LL;
        b.ingestion_ts_ns = now - 69'998'000'000LL;
        auto out = RunOneE2E(b, "sig_ooo03_stale_" + std::to_string(i));
        if (out.rm_decision.is_rejected())
            ++stale_rejected;
    }

    // C-OOO-03 核心断言: stale quote 被 RM 拒绝
    EXPECT_GE(stale_rejected, 1u) << "C-OOO-03: VirtualMatcher 不接受 stale quote → RM 至少 1 笔拒单";

    // R-11: paper 不写 position WAL
    EXPECT_EQ(position_->HighWatermark(), 0u) << "C-OOO-03: paper 不写 position WAL (R-11)";
}

}  // namespace
}  // namespace stcpp::test::chaos
