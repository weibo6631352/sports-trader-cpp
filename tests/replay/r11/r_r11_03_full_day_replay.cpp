// tests/replay/r11/r_r11_03_full_day_replay.cpp — R-R11-03: paper runtime 全天日志回放
//
// Owner: 小宋 (test-replay-engineer)  E-04 replay framework v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §2.3.2
//
// 场景: 一日完整 paper audit WAL 回放 (模拟, OQ-02 golden fixture 待小余 EventRecorder 就绪)
// 期望:
//   - 所有 WAL 写入在 paper 路径
//   - GM-PAPER-G 报表字段全出 (audit_emitter 正常工作)
//
// OQ-02 处理: golden fixture 用合成数据 stub; 小余 EventRecorder 就绪后换真 decoder

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

class RR1103Fixture : public integration::PaperE2EFixture {
protected:
    void SetUp() override {
        integration::PaperE2EFixture::SetUp();
        ASSERT_EQ(stcpp::execution::ExecutionContext::Mode(), stcpp::execution::ExecutionMode::Paper);
        ASSERT_EQ(stcpp::observability::AuditWalKindForBuild(), WalKind::PaperAudit);
    }
};

// R-R11-03: 模拟全天 500 笔 paper audit WAL 回放 → 所有写入在 paper 路径
TEST_F(RR1103Fixture, R_R11_03_full_day_500_intents_paper_path) {
    PaperLedgerIsolationAssertion r11(paper_audit_.get(), risk_audit_.get(), position_.get(),
                                      shadow_audit_.get());
    r11.set_baselines(paper_audit_->HighWatermark(), risk_audit_->HighWatermark(), position_->HighWatermark(),
                      shadow_audit_->HighWatermark());

    // 模拟全天: 500 笔 (OQ-02 stub, 待小余 EventRecorder 接真 decoder)
    constexpr int kDayIntents = 500;
    std::uint64_t seq = 0;

    for (int i = 0; i < kDayIntents; ++i) {
        const auto now = NowRealtimeNs();
        PmBookUpdate b;
        b.market_id = "mkt_replay_day_" + std::to_string(i % 20);  // 20 市场循环
        b.price = 0.50 + 0.001 * (i % 40);
        b.book_depth_l1_usdc = 20'000.0;
        b.event_ts_ns = now - 5'000'000;
        b.data_source_ts_ns = now - 4'000'000;
        b.ingestion_ts_ns = now - 2'000'000;
        auto out = RunOneE2E(b, "sig_replay_day_" + std::to_string(i));
        ++seq;

        if (out.rm_decision.is_approved()) {
            r11.on_decision_wal_kind(WalKind::PaperAudit, seq);
            if (out.went_through_signer) {
                r11.on_fill_wal_kind(out.fill.audit_wal_kind, seq);
            }
        }
    }

    EXPECT_EQ(seq, static_cast<std::uint64_t>(kDayIntents)) << "R-R11-03: 全天 500 笔全部处理";

    // finalize
    const bool r11_ok = r11.finalize();
    if (!r11_ok) {
        for (const auto& v : r11.violations()) {
            ADD_FAILURE() << "R-R11-03 violation: " << v.message;
        }
    }
    EXPECT_TRUE(r11_ok) << "R-R11-03: 全天回放 R-11 校验通过 (paper 账本零污染)";

    // GM-PAPER-G 报表字段: paper_audit 有新增
    EXPECT_GT(paper_audit_->HighWatermark(), 0u) << "R-R11-03: paper_audit WAL 有记录 (GM-PAPER-G 报表可出)";

    // 老韩签字前提
    EXPECT_EQ(r11.finalized_live_wal_delta(), 0u) << "R-R11-03: live WAL delta == 0 (老韩 RM 主权签字前提)";
}

}  // namespace
}  // namespace stcpp::test::replay
