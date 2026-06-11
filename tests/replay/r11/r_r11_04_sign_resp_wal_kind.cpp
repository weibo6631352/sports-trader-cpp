// tests/replay/r11/r_r11_04_sign_resp_wal_kind.cpp — R-R11-04: SignResponse.audit_wal_kind 全程校验
//
// Owner: 小宋 (test-replay-engineer)  E-04 replay framework v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §2.3.2
//
// 场景: 含 signer 输出的 event
// 期望:
//   - 每条 SignResponse.audit_wal_kind == PaperAudit (T2 升级为 replay)
//
// 说明: 这是 integration/r11_paper_pollution_test.cpp T2 的 replay 版本,
//       用 PaperLedgerIsolationAssertion 包装, 使其可在历史数据回放中持续验证.

#include <gtest/gtest.h>

#include "stcpp/execution/execution_mode.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/infra/wal/wal_writer.hpp"
#include "stcpp/observability/audit_record.hpp"

#include "tests/integration/test_fixture.hpp"
#include "tests/replay/paper/paper_r11_assertion.hpp"

namespace stcpp::test::replay {
namespace {

using integration::PmBookUpdate;
using stcpp::infra::wal::WalKind;
using stcpp::infra::wal::pit::NowRealtimeNs;

class RR1104Fixture : public integration::PaperE2EFixture {
protected:
    void SetUp() override {
        integration::PaperE2EFixture::SetUp();
        ASSERT_EQ(stcpp::execution::ExecutionContext::Mode(), stcpp::execution::ExecutionMode::Paper);
        ASSERT_EQ(stcpp::observability::AuditWalKindForBuild(), WalKind::PaperAudit);
    }
};

// R-R11-04: SignResponse.audit_wal_kind 全程校验 (signer 输出 replay)
TEST_F(RR1104Fixture, R_R11_04_sign_response_wal_kind_replay_all_paper_audit) {
    PaperLedgerIsolationAssertion r11(paper_audit_.get(), risk_audit_.get(), position_.get(),
                                      shadow_audit_.get());
    r11.set_baselines(paper_audit_->HighWatermark(), risk_audit_->HighWatermark(), position_->HighWatermark(),
                      shadow_audit_->HighWatermark());

    constexpr int kN = 30;
    int sign_through = 0;
    int sign_wal_ok = 0;
    int fill_wal_ok = 0;

    for (int i = 0; i < kN; ++i) {
        const auto now = NowRealtimeNs();
        PmBookUpdate b;
        b.market_id = "mkt_replay_sign_" + std::to_string(i);
        b.price = 0.55;
        b.book_depth_l1_usdc = 20'000.0;
        b.event_ts_ns = now - 5'000'000;
        b.data_source_ts_ns = now - 4'000'000;
        b.ingestion_ts_ns = now - 2'000'000;
        auto out = RunOneE2E(b, "sig_replay_sign_" + std::to_string(i));

        if (out.went_through_signer) {
            ++sign_through;

            // SignResponse.audit_wal_kind replay 校验 (T2 升级)
            const bool sign_ok =
                r11.on_decision_wal_kind(out.sign_resp.audit_wal_kind, static_cast<std::uint64_t>(i + 1));
            if (sign_ok)
                ++sign_wal_ok;

            // VirtualFill.audit_wal_kind replay 校验
            const bool fill_ok =
                r11.on_fill_wal_kind(out.fill.audit_wal_kind, static_cast<std::uint64_t>(i + 1));
            if (fill_ok)
                ++fill_wal_ok;
        }
    }

    EXPECT_GE(sign_through, 1) << "R-R11-04: 至少 1 笔走 signer";
    EXPECT_EQ(sign_through, sign_wal_ok) << "R-R11-04 / R-11: 全部 SignResponse.audit_wal_kind == PaperAudit";
    EXPECT_EQ(sign_through, fill_wal_ok) << "R-R11-04 / R-11: 全部 VirtualFill.audit_wal_kind == PaperAudit";

    // finalize
    const bool r11_ok = r11.finalize();
    if (!r11_ok) {
        for (const auto& v : r11.violations()) {
            ADD_FAILURE() << "R-R11-04 violation: " << v.message;
        }
    }
    EXPECT_TRUE(r11_ok) << "R-R11-04: signer WAL kind replay 校验通过 (R-11)";
}

}  // namespace
}  // namespace stcpp::test::replay
