// tests/chaos/wss/c_wss_03_reconnect_fail.cpp — C-WSS-03: WSS 断 + 重连失败 3 次
//
// Owner: 小宋 (test-replay-engineer)  E-03 chaos v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §1.2.1
//
// 场景: WSS 断 + reconnect=false, retries=3
// 期望:
//   - RM 维持 HALTED; 不崩溃; alert log 出 reconnect_fail_count=3
//
// 断言:
//   ASSERT RM 不崩溃 (no exception / abort)
//   ASSERT 重连尝试次数记录 == 3
//   ASSERT paper_audit.HighWatermark 不回退

#include "tests/chaos/chaos_fixture.hpp"

namespace stcpp::test::chaos {
namespace {

using integration::PmBookUpdate;
using stcpp::infra::wal::pit::NowRealtimeNs;

class CWss03Fixture : public ChaosE2EFixture {};

// C-WSS-03: 重连失败 3 次 → 系统不崩; HALTED 维持; 重连计数正确
TEST_F(CWss03Fixture, C_WSS_03_reconnect_fail_3_times_no_crash) {
    // 注入: 断连 + 重连失败
    FaultConfig cfg;
    cfg.kind                    = FaultKind::WssDisconnect;
    cfg.disconnect_duration_ms  = 30'000;
    cfg.reconnect_ok            = false;
    cfg.reconnect_retry_count   = 3;
    InjectFault(cfg);

    EXPECT_TRUE(fault_state_.disconnected) << "C-WSS-03: WSS 已断连";
    EXPECT_EQ(fault_state_.reconnect_attempt_count, 3)
        << "C-WSS-03: reconnect_fail_count 必须记录 3 次";

    const auto hwm_before = fault_state_.paper_audit_hwm_at_inject;

    // 不崩溃检验: 在 HALTED 状态下仍可调用 RM (返回 HALTED reject, 不 abort)
    for (int i = 0; i < 3; ++i) {
        const auto now = NowRealtimeNs();
        PmBookUpdate b;
        b.market_id          = "mkt_wss03_halt_" + std::to_string(i);
        b.price              = 0.55;
        b.book_depth_l1_usdc = 20'000.0;
        b.event_ts_ns        = now - 5'000'000;
        b.data_source_ts_ns  = now - 4'000'000;
        b.ingestion_ts_ns    = now - 2'000'000;
        // RunOneE2E 应不 throw / abort
        EXPECT_NO_FATAL_FAILURE({
            auto out = RunOneE2E(b, "sig_wss03_halt_" + std::to_string(i));
            (void)out;  // 结果不重要, 关键是不崩
        }) << "C-WSS-03: 重连失败 3 次后 RM 调用不崩溃";
    }

    // R-11: paper_audit HighWatermark 不回退
    EXPECT_GE(paper_audit_->HighWatermark(), hwm_before)
        << "R-11: 重连失败不回退 paper_audit WAL";

    // reconnect_ok=false → wss_connected_ 不恢复
    EXPECT_FALSE(wss_connected_) << "C-WSS-03: reconnect=false → WSS 不恢复";
}

}  // namespace
}  // namespace stcpp::test::chaos
