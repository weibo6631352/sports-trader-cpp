// tests/replay/r11/r_r11_01_50_paper_e2e_replay.cpp — R-R11-01: 50 笔 paper e2e 历史回放
//
// Owner: 小宋 (test-replay-engineer)  E-04 replay framework v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §2.3.2
//
// 场景: 50 条 event (含 approved + rejected) 历史回放
// 期望:
//   - paper_audit HWM > 0
//   - risk_audit / position / shadow HWM == 初始值
//
// 通过条件 (spec §2.3.2 R-R11-01):
//   paper_audit HWM > 0; risk_audit/position/shadow HWM == 初始值

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

class RR1101Fixture : public integration::PaperE2EFixture {
protected:
    void SetUp() override {
        integration::PaperE2EFixture::SetUp();
        // spec §5.4: paper mode 防串
        ASSERT_EQ(stcpp::execution::ExecutionContext::Mode(), stcpp::execution::ExecutionMode::Paper)
            << "replay: execution::ExecutionContext::Mode() 必须 Paper";
        ASSERT_EQ(stcpp::observability::AuditWalKindForBuild(), WalKind::PaperAudit)
            << "replay: AuditWalKindForBuild 必须 PaperAudit";
    }
};

// R-R11-01: 50 笔 paper e2e → paper_audit HWM > 0; 其他 3 WAL HWM 不变
TEST_F(RR1101Fixture, R_R11_01_50_paper_e2e_replay_r11_isolation) {
    // 初始化 PaperLedgerIsolationAssertion
    PaperLedgerIsolationAssertion r11(paper_audit_.get(), risk_audit_.get(), position_.get(),
                                      shadow_audit_.get());

    r11.set_baselines(paper_audit_->HighWatermark(), risk_audit_->HighWatermark(), position_->HighWatermark(),
                      shadow_audit_->HighWatermark());

    // 回放 50 笔 paper e2e
    std::uint64_t seq = 0;
    std::uint64_t approved = 0;
    std::uint64_t rejected = 0;

    for (int i = 0; i < 50; ++i) {
        const auto now = NowRealtimeNs();
        PmBookUpdate b;
        b.market_id = "mkt_replay_r11_01_" + std::to_string(i);
        b.price = 0.50 + 0.001 * (i % 20);
        b.book_depth_l1_usdc = 20'000.0;
        b.event_ts_ns = now - 5'000'000;
        b.data_source_ts_ns = now - 4'000'000;
        b.ingestion_ts_ns = now - 2'000'000;
        auto out = RunOneE2E(b, "sig_replay_r11_01_" + std::to_string(i));
        ++seq;

        if (out.rm_decision.is_approved()) {
            ++approved;
            r11.on_decision_wal_kind(WalKind::PaperAudit, seq);
            if (out.went_through_signer) {
                r11.on_fill_wal_kind(out.fill.audit_wal_kind, seq);
            }
        } else if (out.rm_decision.is_rejected()) {
            ++rejected;
        }
    }

    EXPECT_EQ(approved + rejected, 50u) << "R-R11-01: 50 笔全部处理";
    EXPECT_GE(approved, 1u) << "R-R11-01: 至少 1 笔 approved";

    // finalize: live WAL 水位线未变; paper_audit 有新增
    const bool r11_ok = r11.finalize();

    // 打印违例 (spec §1.1: diff 必须可读)
    if (!r11_ok) {
        for (const auto& v : r11.violations()) {
            ADD_FAILURE() << "R-R11-01 violation: " << v.message;
        }
    }
    EXPECT_TRUE(r11_ok) << "R-R11-01: R-11 校验通过 (paper 账本零污染)";

    // 明细断言
    EXPECT_GT(paper_audit_->HighWatermark(), 0u) << "R-R11-01: paper_audit HWM > 0";
    EXPECT_EQ(risk_audit_->HighWatermark(), 0u) << "R-R11-01 / R-11: risk_audit HWM 未变";
    EXPECT_EQ(position_->HighWatermark(), 0u) << "R-R11-01 / R-11: position HWM 未变";
    EXPECT_EQ(shadow_audit_->HighWatermark(), 0u) << "R-R11-01 / R-11: shadow_audit HWM 未变";

    // 老韩 RM 主权签字前提: live_wal_delta == 0
    EXPECT_EQ(r11.finalized_live_wal_delta(), 0u) << "R-R11-01: live WAL delta == 0 (老韩签字前提)";
}

}  // namespace
}  // namespace stcpp::test::replay
