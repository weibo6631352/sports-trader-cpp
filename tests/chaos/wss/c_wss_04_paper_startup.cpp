// tests/chaos/wss/c_wss_04_paper_startup.cpp — C-WSS-04 (paper): paper runtime 启动阶段 WSS 断
//
// Owner: 小宋 (test-replay-engineer)  E-03 chaos v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §1.2.1 / §1.3 C-PAPER-01
//
// 场景: paper runtime 启动 2s 后 WSS 断
// 期望:
//   - paper engine 不产生任何 VirtualFill
//   - WAL 无 position 写入
//
// 断言:
//   ASSERT no VirtualFill produced during disconnect
//   ASSERT position WAL HighWatermark == 0 (R-11)
//   ASSERT paper_audit HWM 不回退

#include "tests/chaos/chaos_fixture.hpp"

namespace stcpp::test::chaos {
namespace {

using integration::PmBookUpdate;
using stcpp::infra::wal::pit::NowRealtimeNs;

class CWss04PaperFixture : public ChaosE2EFixture {};

// C-PAPER-01 / C-WSS-04: paper startup WSS 断 → VirtualMatcher 暂停, WAL 无 position 写入
TEST_F(CWss04PaperFixture, C_WSS_04_paper_startup_wss_disconnect) {
    // Simulate "startup + 2s" by injecting disconnect immediately (at=startup+2s equiv)
    FaultConfig cfg;
    cfg.kind                   = FaultKind::WssDisconnect;
    cfg.disconnect_duration_ms = 10'000;
    cfg.reconnect_ok           = true;
    InjectFault(cfg);

    EXPECT_TRUE(fault_state_.disconnected) << "C-PAPER-01: WSS 断连";

    // 断连中: 尝试推 book events, 期望 stale reject (VirtualMatcher 不被调用)
    int fill_count = 0;
    for (int i = 0; i < 5; ++i) {
        const auto now = NowRealtimeNs();
        PmBookUpdate b;
        b.market_id          = "mkt_paper01_" + std::to_string(i);
        b.price              = 0.55;
        b.book_depth_l1_usdc = 20'000.0;
        // startup WSS 断连: book_snapshot_ts > 60s ago → BOOK_TS_STALE (RM 60s 阈值)
        b.event_ts_ns        = now - 65'000'000'000LL;  // 65s > 60s stale
        b.data_source_ts_ns  = now - 64'999'000'000LL;
        b.ingestion_ts_ns    = now - 64'998'000'000LL;
        auto out = RunOneE2E(b, "sig_paper01_" + std::to_string(i));
        if (out.went_through_signer) ++fill_count;
    }

    // C-PAPER-01 核心断言: 断连期间 VirtualFill == 0
    EXPECT_EQ(fill_count, 0)
        << "C-PAPER-01: paper runtime WSS 断连期间不产生 VirtualFill";

    // R-11: position WAL 无写入
    EXPECT_EQ(position_->HighWatermark(), 0u)
        << "C-PAPER-01: WAL 无 position 写入 (R-11 paper 不污染 position WAL)";

    // paper_audit 不回退
    EXPECT_GE(paper_audit_->HighWatermark(), fault_state_.paper_audit_hwm_at_inject)
        << "C-PAPER-01: paper_audit WAL 不回退 (R-11)";

    // 重连后恢复
    SimulateWssReconnect();
    EXPECT_TRUE(wss_connected_) << "C-PAPER-01: 重连后 WSS 恢复";
}

}  // namespace
}  // namespace stcpp::test::chaos
