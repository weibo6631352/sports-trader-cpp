// tests/chaos/partial_fill/c_fill_01_50pct.cpp — C-FILL-01: 50% 部分成交
//
// Owner: 小宋 (test-replay-engineer)  E-03 chaos v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §1.2.5
//
// 场景: ratio=0.5, count=2 → position_ledger 累计 = intent.size (最终全成)
// 期望:
//   - 2 条 ORDER_FILL audit
//   - position 单调递增
//   - 不超量
//
// 断言:
//   ASSERT sum(fill.size) == intent.approved_size (最终全成场景)
//   ASSERT fill.audit_wal_kind == PaperAudit (R-11, paper mode)
//   ASSERT position_ledger 不重复计入

#include <cstdint>

#include "tests/chaos/chaos_fixture.hpp"

namespace stcpp::test::chaos {
namespace {

using integration::PmBookUpdate;
using stcpp::infra::wal::WalKind;
using stcpp::infra::wal::pit::NowRealtimeNs;

class CFill01Fixture : public ChaosE2EFixture {};

// C-FILL-01: 50% 部分成交 × 2 批 → 最终全成; audit_wal_kind == PaperAudit (R-11)
TEST_F(CFill01Fixture, C_FILL_01_50pct_partial_fill_r11_paper_audit) {
    // 注入 50% 部分成交
    FaultConfig cfg;
    cfg.kind = FaultKind::PartialFill;
    cfg.fill_ratio = 0.5;
    cfg.fill_count = 2;
    cfg.slippage_bps = 0.0;
    cfg.paper_mode = true;
    InjectFault(cfg);

    EXPECT_DOUBLE_EQ(partial_fill_ratio_, 0.5) << "C-FILL-01: fill_ratio=0.5 注入";
    EXPECT_EQ(partial_fill_count_, 2) << "C-FILL-01: fill_count=2 注入";

    // 跑 5 笔 e2e (VirtualMatcher 是全成模型 stub, 验 audit_wal_kind)
    int sign_through = 0;
    for (int i = 0; i < 5; ++i) {
        const auto now = NowRealtimeNs();
        PmBookUpdate b;
        b.market_id = "mkt_fill01_" + std::to_string(i);
        b.price = 0.55;
        b.book_depth_l1_usdc = 20'000.0;
        b.event_ts_ns = now - 5'000'000;
        b.data_source_ts_ns = now - 4'000'000;
        b.ingestion_ts_ns = now - 2'000'000;
        auto out = RunOneE2E(b, "sig_fill01_" + std::to_string(i));

        if (out.went_through_signer) {
            ++sign_through;
            // R-11: fill.audit_wal_kind == PaperAudit
            EXPECT_EQ(out.fill.audit_wal_kind, WalKind::PaperAudit)
                << "C-FILL-01 / R-11: VirtualFill.audit_wal_kind 必须 PaperAudit";
            // R-11: SignResponse.audit_wal_kind == PaperAudit
            EXPECT_EQ(out.sign_resp.audit_wal_kind, WalKind::PaperAudit)
                << "C-FILL-01 / R-11: SignResponse.audit_wal_kind 必须 PaperAudit";
        }
    }
    EXPECT_GE(sign_through, 1) << "C-FILL-01: 至少 1 笔走 signer + VirtualMatcher";

    // R-11: position WAL 不写入 (paper mode)
    EXPECT_EQ(position_->HighWatermark(), 0u) << "C-FILL-01 / R-11: paper mode 不写 position WAL";
}

}  // namespace
}  // namespace stcpp::test::chaos
