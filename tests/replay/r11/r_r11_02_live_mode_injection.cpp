// tests/replay/r11/r_r11_02_live_mode_injection.cpp — R-R11-02: paper mode 切换检测
//
// Owner: 小宋 (test-replay-engineer)  E-04 replay framework v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §2.3.2
//
// 场景: 含 1 条 mode=live 误注入的 event
// 期望:
//   - replay assertion 检出 R-11 violation
//   - FAIL 且定位到事件序号
//
// 通过条件: 检出 violation; 定位到事件序号

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

class RR1102Fixture : public integration::PaperE2EFixture {
protected:
    void SetUp() override {
        integration::PaperE2EFixture::SetUp();
        ASSERT_EQ(stcpp::execution::kCompiledMode, stcpp::execution::ExecutionMode::Paper);
        ASSERT_EQ(stcpp::observability::AuditWalKindForBuild(), WalKind::PaperAudit);
    }
};

// R-R11-02: live mode 误注入 → R-11 violation 检出并定位序号
TEST_F(RR1102Fixture, R_R11_02_live_mode_injection_detected) {
    PaperLedgerIsolationAssertion r11(paper_audit_.get(), risk_audit_.get(), position_.get(),
                                      shadow_audit_.get());
    r11.set_baselines(paper_audit_->HighWatermark(), risk_audit_->HighWatermark(), position_->HighWatermark(),
                      shadow_audit_->HighWatermark());

    // 回放 5 笔 paper mode intent (正常)
    for (int i = 0; i < 5; ++i) {
        r11.on_decision_wal_kind(WalKind::PaperAudit, static_cast<std::uint64_t>(i + 1));
    }
    EXPECT_EQ(r11.violation_count(), 0u) << "R-R11-02: 5 笔 paper mode 无违例";

    // 第 6 条: 误注入 live mode (audit_wal_kind == RiskAudit)
    const std::uint64_t live_seq = 6;
    const bool live_ok = r11.on_decision_wal_kind(WalKind::RiskAudit, live_seq);
    EXPECT_FALSE(live_ok) << "R-R11-02: live mode 注入 → on_decision_wal_kind 返回 false";
    EXPECT_GE(r11.violation_count(), 1u) << "R-R11-02: live mode 注入 → R-11 violation 检出";

    // 定位到事件序号
    bool found_seq = false;
    for (const auto& v : r11.violations()) {
        if (v.seq == live_seq) {
            found_seq = true;
            break;
        }
    }
    EXPECT_TRUE(found_seq) << "R-R11-02: R-11 violation 定位到 seq=" << live_seq;
}

// R-R11-02 补充: live fill 注入也被检出
TEST_F(RR1102Fixture, R_R11_02_live_fill_injection_detected) {
    PaperLedgerIsolationAssertion r11(paper_audit_.get(), risk_audit_.get(), position_.get(),
                                      shadow_audit_.get());
    r11.set_baselines(0, 0, 0, 0);

    // 正常 paper fill
    r11.on_fill_wal_kind(WalKind::PaperAudit, 1);
    EXPECT_EQ(r11.violation_count(), 0u);

    // live fill 误注入 (Position WAL kind)
    const bool live_fill_ok = r11.on_fill_wal_kind(WalKind::Position, 2);
    EXPECT_FALSE(live_fill_ok) << "R-R11-02: live fill 注入 → on_fill_wal_kind 返回 false";
    EXPECT_GE(r11.violation_count(), 1u) << "R-R11-02: live fill 注入 → R-11 violation 检出";
}

}  // namespace
}  // namespace stcpp::test::replay
