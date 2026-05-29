// tests/chaos/partial_fill/c_fill_04_paper_wal.cpp — C-FILL-04 (paper): VirtualMatcher 部分成交写 paper WAL
//
// Owner: 小宋 (test-replay-engineer)  E-03 chaos v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §1.2.5 / §1.3 C-PAPER-05
//
// 场景: ratio=0.6, paper_mode=true
// 期望:
//   - VirtualFill.audit_wal_kind == PaperAudit
//   - position WAL = paper path
//   - 不写 live position
//
// 断言 (C-FILL-04 是 chaos gate 核心 3 条之一):
//   ASSERT fill.audit_wal_kind == PaperAudit (R-11, paper mode)
//   ASSERT no position write to live_position_wal (R-11)
//   ASSERT paper_audit.HighWatermark > base (审计记录正常)

#include <cstdint>

#include "tests/chaos/chaos_fixture.hpp"

namespace stcpp::test::chaos {
namespace {

using integration::PmBookUpdate;
using stcpp::infra::wal::WalKind;
using stcpp::infra::wal::pit::NowRealtimeNs;

class CFill04PaperFixture : public ChaosE2EFixture {};

// C-PAPER-05 / C-FILL-04: VirtualMatcher 60% 部分成交 × 20 笔 → paper WAL 路径正确
TEST_F(CFill04PaperFixture, C_FILL_04_paper_wal_partial_fill_r11) {
    // 注入: 60% 首批, paper_mode=true
    FaultConfig cfg;
    cfg.kind = FaultKind::PartialFill;
    cfg.fill_ratio = 0.6;
    cfg.fill_count = 2;
    cfg.paper_mode = true;
    InjectFault(cfg);

    const auto paper_audit_base = paper_audit_->HighWatermark();

    // 20 笔 e2e — 核心 C-FILL-04 场景
    int sign_through = 0;
    int fill_wal_ok = 0;
    int sign_wal_ok = 0;

    for (int i = 0; i < 20; ++i) {
        const auto now = NowRealtimeNs();
        PmBookUpdate b;
        b.market_id = "mkt_fill04_paper_" + std::to_string(i);
        b.price = 0.55;
        b.book_depth_l1_usdc = 20'000.0;
        b.event_ts_ns = now - 5'000'000;
        b.data_source_ts_ns = now - 4'000'000;
        b.ingestion_ts_ns = now - 2'000'000;
        auto out = RunOneE2E(b, "sig_fill04_paper_" + std::to_string(i));

        if (out.went_through_signer) {
            ++sign_through;
            if (out.fill.audit_wal_kind == WalKind::PaperAudit)
                ++fill_wal_ok;
            if (out.sign_resp.audit_wal_kind == WalKind::PaperAudit)
                ++sign_wal_ok;
        }
    }

    EXPECT_GE(sign_through, 1) << "C-FILL-04: 至少 1 笔走 signer";

    // C-FILL-04 核心断言: 全部 fill + sign audit_wal_kind == PaperAudit
    EXPECT_EQ(sign_through, fill_wal_ok) << "C-FILL-04 / R-11: 全部 VirtualFill.audit_wal_kind == PaperAudit";
    EXPECT_EQ(sign_through, sign_wal_ok)
        << "C-FILL-04 / R-11: 全部 SignResponse.audit_wal_kind == PaperAudit";

    // R-11: paper_audit 有新增记录
    EXPECT_GT(paper_audit_->HighWatermark(), paper_audit_base)
        << "C-FILL-04 / R-11: paper_audit WAL 有新增记录";

    // R-11: live WAL 不被写入
    EXPECT_EQ(risk_audit_->HighWatermark(), 0u) << "C-FILL-04 / R-11: live risk_audit WAL 未被写入";
    EXPECT_EQ(position_->HighWatermark(), 0u) << "C-FILL-04 / R-11: live position WAL 未被写入";
    EXPECT_EQ(shadow_audit_->HighWatermark(), 0u) << "C-FILL-04 / R-11: shadow_audit WAL 未被写入";
}

}  // namespace
}  // namespace stcpp::test::chaos
