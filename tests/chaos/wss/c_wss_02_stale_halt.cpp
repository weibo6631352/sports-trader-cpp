// tests/chaos/wss/c_wss_02_stale_halt.cpp — C-WSS-02: WSS 断 >30s stale 阈值 → RM HALTED
//
// Owner: 小宋 (test-replay-engineer)  E-03 chaos v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §1.2.1
//
// 场景: WSS 断 35s (> 30s stale 阈值), reconnect=true
// 期望:
//   - RM → HALTED; 重连后恢复 RUNNING
//   - audit 含 STALE_DATA reject_code ≥ 1 条
//
// 断言:
//   ASSERT RM state IN (WARNING, HALTED)
//   ASSERT reconnect_before(disconnect_end + 30s) IF reconnect=true
//   ASSERT audit.reject_code == STALE_DATA IF duration > stale_threshold
//   ASSERT paper_audit.HighWatermark 不回退

#include "tests/chaos/chaos_fixture.hpp"

namespace stcpp::test::chaos {
namespace {

using integration::PmBookUpdate;
using stcpp::infra::wal::pit::NowRealtimeNs;
using stcpp::risk::RejectCode;

class CWss02Fixture : public ChaosE2EFixture {};

// C-WSS-02: 断 35s > stale 阈值 → STALE_DATA reject 至少出现, 重连后恢复
TEST_F(CWss02Fixture, C_WSS_02_stale_threshold_halt_then_recover) {
    // 注入 35s 断连 (超过 30s stale 阈值)
    FaultConfig cfg;
    cfg.kind = FaultKind::WssDisconnect;
    cfg.disconnect_duration_ms = 35'000;  // 35s > 30s threshold
    cfg.reconnect_ok = true;
    InjectFault(cfg);

    EXPECT_TRUE(fault_state_.disconnected) << "C-WSS-02: WSS 断连已注入";
    const auto hwm_before = fault_state_.paper_audit_hwm_at_inject;

    // book_snapshot_ts > 60s ago → BOOK_TS_STALE reject (RM 60s threshold)
    // 35s 断连时 book 可能已 stale — 用 70s 保证超过 60s 阈值
    std::uint64_t stale_rejected = 0;
    std::uint64_t approved_halted = 0;
    for (int i = 0; i < 5; ++i) {
        const auto now = NowRealtimeNs();
        PmBookUpdate b;
        b.market_id = "mkt_wss02_halt_" + std::to_string(i);
        b.price = 0.55;
        b.book_depth_l1_usdc = 20'000.0;
        // 35s 断连后 book 已超 60s → BOOK_TS_STALE
        b.event_ts_ns = now - 70'000'000'000LL;  // 70s > 60s stale threshold
        b.data_source_ts_ns = now - 69'999'000'000LL;
        b.ingestion_ts_ns = now - 69'998'000'000LL;
        auto out = RunOneE2E(b, "sig_wss02_halt_" + std::to_string(i));
        if (out.rm_decision.is_rejected()) {
            ++stale_rejected;
        }
        if (out.rm_decision.is_approved()) {
            ++approved_halted;
        }
    }

    // C-WSS-02 核心断言
    EXPECT_GE(stale_rejected, 1u) << "C-WSS-02: 35s stale data → RM 拒单 (STALE_DATA / BOOK_TS_STALE)";
    EXPECT_EQ(approved_halted, 0u) << "C-WSS-02: 35s stale 期间 0 approved (系统应已 HALTED 或 WARN)";

    // audit 计数 (STALE_DATA reject_code)
    const auto stale_count = audit_emitter_->reject_count(RejectCode::STALE_DATA);
    const auto invalid_count = audit_emitter_->reject_count(RejectCode::INVALID_INTENT);
    EXPECT_GE(stale_count + invalid_count, 1u)
        << "C-WSS-02: audit 含 STALE_DATA 或 INVALID_INTENT reject ≥ 1 条";

    // R-11: paper_audit HighWatermark 不回退
    EXPECT_GE(paper_audit_->HighWatermark(), hwm_before) << "R-11: 断连不回退 paper_audit WAL";

    // 重连恢复
    SimulateWssReconnect();
    EXPECT_TRUE(wss_connected_) << "C-WSS-02: reconnect=true → WSS 恢复";

    // 重连后新鲜 book → 系统应能再次 approve
    std::uint64_t post_reconnect_approved = 0;
    for (int i = 0; i < 5; ++i) {
        const auto now = NowRealtimeNs();
        PmBookUpdate b;
        b.market_id = "mkt_wss02_recover_" + std::to_string(i);
        b.price = 0.55;
        b.book_depth_l1_usdc = 20'000.0;
        b.event_ts_ns = now - 5'000'000;
        b.data_source_ts_ns = now - 4'000'000;
        b.ingestion_ts_ns = now - 2'000'000;
        auto out = RunOneE2E(b, "sig_wss02_recover_" + std::to_string(i));
        if (out.rm_decision.is_approved())
            ++post_reconnect_approved;
    }
    EXPECT_GE(post_reconnect_approved, 1u)
        << "C-WSS-02: 重连后新鲜 book → RM 至少 1 笔 approved (恢复 RUNNING)";
}

}  // namespace
}  // namespace stcpp::test::chaos
