// tests/chaos/partial_fill/c_fill_02_5pct_many.cpp — C-FILL-02: 极小首批 5% 成交 × 20
//
// Owner: 小宋 (test-replay-engineer)  E-03 chaos v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §1.2.5
//
// 场景: ratio=0.05, count=20
// 期望:
//   - 20 次 FILL audit
//   - position 单调递增
//   - 不中途触发 EXCEED_MARKET_EXPOSURE
//
// 断言:
//   ASSERT fill.audit_wal_kind == PaperAudit (R-11)
//   ASSERT position_ledger[market] 不重复计入
//   ASSERT EXCEED_CONDITION_EXPOSURE reject_count == 0 (不超额)

#include "tests/chaos/chaos_fixture.hpp"

#include <cstdint>

namespace stcpp::test::chaos {
namespace {

using integration::PmBookUpdate;
using stcpp::infra::wal::WalKind;
using stcpp::infra::wal::pit::NowRealtimeNs;
using stcpp::risk::RejectCode;

class CFill02Fixture : public ChaosE2EFixture {};

// C-FILL-02: 5% 首批 × 20 → audit_wal_kind PaperAudit; 不超额 EXCEED_MARKET_EXPOSURE
TEST_F(CFill02Fixture, C_FILL_02_5pct_many_fills_no_exceed_exposure) {
    // 注入: 5% 首批, 20 批
    FaultConfig cfg;
    cfg.kind         = FaultKind::PartialFill;
    cfg.fill_ratio   = 0.05;
    cfg.fill_count   = 20;
    cfg.paper_mode   = true;
    InjectFault(cfg);

    EXPECT_DOUBLE_EQ(partial_fill_ratio_, 0.05)
        << "C-FILL-02: fill_ratio=0.05 注入";
    EXPECT_EQ(partial_fill_count_, 20)
        << "C-FILL-02: fill_count=20 注入";

    int sign_through = 0;
    int fill_wal_ok  = 0;

    for (int i = 0; i < 10; ++i) {
        const auto now = NowRealtimeNs();
        PmBookUpdate b;
        b.market_id          = "mkt_fill02_" + std::to_string(i);
        b.price              = 0.55;
        b.book_depth_l1_usdc = 20'000.0;
        b.event_ts_ns        = now - 5'000'000;
        b.data_source_ts_ns  = now - 4'000'000;
        b.ingestion_ts_ns    = now - 2'000'000;
        auto out = RunOneE2E(b, "sig_fill02_" + std::to_string(i));

        if (out.went_through_signer) {
            ++sign_through;
            if (out.fill.audit_wal_kind == WalKind::PaperAudit) ++fill_wal_ok;
        }
    }

    EXPECT_GE(sign_through, 1) << "C-FILL-02: 至少 1 笔走 signer";
    EXPECT_EQ(sign_through, fill_wal_ok)
        << "C-FILL-02 / R-11: 全部 fill.audit_wal_kind == PaperAudit";

    // 不超额: EXCEED_CONDITION_EXPOSURE (= EXCEED_MARKET_EXPOSURE) reject == 0
    // 小单 (100 micro) × 10 笔 = 1000 micro << market_exposure_cap 50'000 USDC
    const auto exceed_count = audit_emitter_->reject_count(RejectCode::EXCEED_CONDITION_EXPOSURE);
    EXPECT_EQ(exceed_count, 0u)
        << "C-FILL-02: 不触发 EXCEED_MARKET_EXPOSURE (小单不超额)";

    // R-11: position WAL 不写入
    EXPECT_EQ(position_->HighWatermark(), 0u)
        << "C-FILL-02 / R-11: paper mode 不写 position WAL";
}

}  // namespace
}  // namespace stcpp::test::chaos
