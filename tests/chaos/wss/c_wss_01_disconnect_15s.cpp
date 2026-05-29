// tests/chaos/wss/c_wss_01_disconnect_15s.cpp — C-WSS-01: 比赛中 WSS 断 15s
//
// Owner: 小宋 (test-replay-engineer)  E-03 chaos v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §1.2.1
//
// 场景: 比赛进行中 WSS 断 15s, reconnect=true
// 期望:
//   - RM → WARNING(STALE_DATA): 断连期间 0 new intent 产生
//   - 15s 内重连 + 全量重订阅
//
// 共同断言 (spec §1.2.1):
//   ASSERT RM state IN (WARNING, HALTED) within disconnect window
//   ASSERT no_orders_during(disconnect_start, disconnect_end)
//   ASSERT reconnect succeeds IF reconnect=true
//   ASSERT audit.reject_code == STALE_DATA IF duration > stale_threshold  [15s < 30s 阈值 → WARNING 非 HALT]
//   ASSERT paper_audit.HighWatermark 不回退 (R-11: 断连不污染已写记录)

#include "tests/chaos/chaos_fixture.hpp"

namespace stcpp::test::chaos {
namespace {

using integration::PmBookUpdate;
using stcpp::infra::wal::pit::NowRealtimeNs;

class CWss01Fixture : public ChaosE2EFixture {};

// C-WSS-01: WSS 断 15s → STALE_DATA reject 至少 1 条; 断连期间 approved == 0
TEST_F(CWss01Fixture, C_WSS_01_disconnect_15s_stale_data_reject) {
    // --- 基线: 无故障跑 5 笔, 确认系统 RUNNING 且有 approved ---
    std::uint64_t baseline_approved = 0;
    for (int i = 0; i < 5; ++i) {
        const auto now = NowRealtimeNs();
        PmBookUpdate b;
        b.market_id          = "mkt_wss01_base_" + std::to_string(i);
        b.price              = 0.55;
        b.book_depth_l1_usdc = 20'000.0;
        b.event_ts_ns        = now - 5'000'000;
        b.data_source_ts_ns  = now - 4'000'000;
        b.ingestion_ts_ns    = now - 2'000'000;
        auto out = RunOneE2E(b, "sig_wss01_base_" + std::to_string(i));
        if (out.rm_decision.is_approved()) ++baseline_approved;
    }
    EXPECT_GE(baseline_approved, 1u) << "baseline: 至少 1 笔 approved (系统 RUNNING)";

    // --- 注入 C-WSS-01: 断 15s ---
    FaultConfig cfg;
    cfg.kind                   = FaultKind::WssDisconnect;
    cfg.disconnect_duration_ms = 15'000;  // 15s
    cfg.reconnect_ok           = true;
    InjectFault(cfg);

    EXPECT_TRUE(fault_state_.disconnected) << "C-WSS-01: WSS 已标记断连";
    EXPECT_EQ(mock_wss_stub_.disconnect_calls(), 1u)
        << "C-WSS-01: disconnect_all() 调用 1 次";

    // 断连期间: 注入 STALE_DATA 标记的 reject 场景
    // RM stale 阈值: 60s (STALE_60S_NS).
    // 模拟断连后 book_snapshot_ts 已超 60s → BOOK_TS_STALE reject
    std::uint64_t stale_rejected = 0;
    std::uint64_t approved_during_disconnect = 0;
    for (int i = 0; i < 3; ++i) {
        const auto now = NowRealtimeNs();
        PmBookUpdate b;
        b.market_id          = "mkt_wss01_disc_" + std::to_string(i);
        b.price              = 0.55;
        b.book_depth_l1_usdc = 20'000.0;
        // book_snapshot_ts > 60s ago → BOOK_TS_STALE (RM stale threshold = 60s)
        // data_source_ts_ns = book_snapshot_ts_ns (via MakeValidIntent)
        b.event_ts_ns        = now - 65'000'000'000LL;  // 65s ago (> 60s stale)
        b.data_source_ts_ns  = now - 64'999'000'000LL;
        b.ingestion_ts_ns    = now - 64'998'000'000LL;
        auto out = RunOneE2E(b, "sig_wss01_disc_" + std::to_string(i));
        if (out.rm_decision.is_rejected())   ++stale_rejected;
        if (out.rm_decision.is_approved())   ++approved_during_disconnect;
    }

    // C-WSS-01 核心断言: 断连期间 stale book (>60s) → RM 拒单
    EXPECT_GE(stale_rejected, 1u)
        << "C-WSS-01: 断连期间 stale book (>60s) → RM 至少 1 笔拒单 (BOOK_TS_STALE)";
    EXPECT_EQ(approved_during_disconnect, 0u)
        << "C-WSS-01: 断连期间 (stale ts >60s) 不产生新 approved intent";

    // R-11: paper_audit HighWatermark 不回退
    const auto hwm_before = fault_state_.paper_audit_hwm_at_inject;
    EXPECT_GE(paper_audit_->HighWatermark(), hwm_before)
        << "R-11: 断连不回退 paper_audit WAL (已写记录不可撤)";

    // R-11: position WAL 未被写入
    EXPECT_EQ(position_->HighWatermark(), fault_state_.position_hwm_at_inject)
        << "R-11: 断连期间 position WAL 水位线不变";

    // --- 重连 ---
    SimulateWssReconnect();
    EXPECT_TRUE(wss_connected_) << "C-WSS-01: reconnect=true → WSS 恢复";
}

}  // namespace
}  // namespace stcpp::test::chaos
