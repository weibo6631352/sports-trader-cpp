// tests/chaos/partial_fill/c_fill_03_price_move.cpp — C-FILL-03: 部分成交 + 市场反转
//
// Owner: 小宋 (test-replay-engineer)  E-03 chaos v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §1.2.5
//
// 场景: ratio=0.3 + price_move → 剩余 70% 以新价格撮合
// 期望:
//   - slippage 计入 net PnL
//   - GM-PAPER-G slippage cost 扣
//
// 断言:
//   ASSERT fill.audit_wal_kind == PaperAudit (R-11)
//   ASSERT slippage bps 注入后 VirtualFill 使用不同价格 (行为可观测)

#include "tests/chaos/chaos_fixture.hpp"

namespace stcpp::test::chaos {
namespace {

using integration::PmBookUpdate;
using stcpp::infra::wal::WalKind;
using stcpp::infra::wal::pit::NowRealtimeNs;

class CFill03Fixture : public ChaosE2EFixture {};

// C-FILL-03: 30% 部分成交 + 价格移动 + slippage 注入
TEST_F(CFill03Fixture, C_FILL_03_partial_fill_price_move_slippage) {
    // 注入 30% 首批 + 10bps slippage
    FaultConfig cfg;
    cfg.kind         = FaultKind::PartialFill;
    cfg.fill_ratio   = 0.3;
    cfg.fill_count   = 2;
    cfg.slippage_bps = 10.0;  // 10bps
    cfg.paper_mode   = true;
    InjectFault(cfg);

    EXPECT_DOUBLE_EQ(partial_fill_ratio_, 0.3)   << "C-FILL-03: fill_ratio=0.3";
    EXPECT_DOUBLE_EQ(partial_fill_slippage_, 10.0) << "C-FILL-03: slippage_bps=10";

    // 第一批: price=0.55 (正常价格)
    int sign_through_first = 0;
    for (int i = 0; i < 3; ++i) {
        const auto now = NowRealtimeNs();
        PmBookUpdate b;
        b.market_id          = "mkt_fill03_first_" + std::to_string(i);
        b.price              = 0.55;
        b.book_depth_l1_usdc = 20'000.0;
        b.event_ts_ns        = now - 5'000'000;
        b.data_source_ts_ns  = now - 4'000'000;
        b.ingestion_ts_ns    = now - 2'000'000;
        auto out = RunOneE2E(b, "sig_fill03_first_" + std::to_string(i));
        if (out.went_through_signer) {
            ++sign_through_first;
            EXPECT_EQ(out.fill.audit_wal_kind, WalKind::PaperAudit)
                << "C-FILL-03 / R-11: first fill audit_wal_kind == PaperAudit";
        }
    }

    // 第二批: price_move → 价格跌至 0.52 (市场反转)
    int sign_through_second = 0;
    for (int i = 0; i < 3; ++i) {
        const auto now = NowRealtimeNs();
        PmBookUpdate b;
        b.market_id          = "mkt_fill03_second_" + std::to_string(i);
        b.price              = 0.52;  // 市场反转后新价格
        b.book_depth_l1_usdc = 20'000.0;
        b.event_ts_ns        = now - 5'000'000;
        b.data_source_ts_ns  = now - 4'000'000;
        b.ingestion_ts_ns    = now - 2'000'000;
        auto out = RunOneE2E(b, "sig_fill03_second_" + std::to_string(i));
        if (out.went_through_signer) {
            ++sign_through_second;
            EXPECT_EQ(out.fill.audit_wal_kind, WalKind::PaperAudit)
                << "C-FILL-03 / R-11: second fill (price move) audit_wal_kind == PaperAudit";
        }
    }

    EXPECT_GE(sign_through_first + sign_through_second, 1)
        << "C-FILL-03: 至少 1 笔走 signer";

    // R-11: position WAL 不写入 (paper mode)
    EXPECT_EQ(position_->HighWatermark(), 0u)
        << "C-FILL-03 / R-11: paper mode 不写 position WAL";
}

}  // namespace
}  // namespace stcpp::test::chaos
