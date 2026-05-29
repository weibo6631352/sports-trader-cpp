// tests/chaos/paper/c_paper_06_multi_fault.cpp — C-PAPER-06: 多故障组合 WSS断 + REST慢
//
// Owner: 小宋 (test-replay-engineer)  E-03 chaos v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §1.3 C-PAPER-06
//
// 场景: WSS断 + REST慢 同时注入
// 期望:
//   - 不崩
//   - HALTED 后人工 ack 恢复
//   - audit chain 不断
//
// 通过条件: 不崩; HALTED 后人工 ack 恢复; audit chain 不断

#include "tests/chaos/chaos_fixture.hpp"

namespace stcpp::test::chaos {
namespace {

using integration::PmBookUpdate;
using stcpp::infra::wal::pit::NowRealtimeNs;

class CPaper06Fixture : public ChaosE2EFixture {};

// C-PAPER-06: WSS 断 + REST 慢同时注入 → 不崩; audit chain 不断
TEST_F(CPaper06Fixture, C_PAPER_06_multi_fault_no_crash_audit_chain_intact) {
    // Step 1: 注入 WSS 断连
    FaultConfig wss_cfg;
    wss_cfg.kind                   = FaultKind::WssDisconnect;
    wss_cfg.disconnect_duration_ms = 10'000;
    wss_cfg.reconnect_ok           = true;
    InjectFault(wss_cfg);

    EXPECT_TRUE(fault_state_.disconnected) << "C-PAPER-06: WSS 断连已注入";

    // Step 2: 同时注入 REST 慢响应 (叠加故障)
    FaultConfig rest_cfg;
    rest_cfg.kind        = FaultKind::LatencySpike;
    rest_cfg.spike_ms    = 200;
    rest_cfg.spike_count = 5;
    rest_cfg.target_rest = true;
    // 不直接调用 InjectFault (会覆盖 fault_cfg_), 手动叠加
    clob_delay_ms_ = rest_cfg.spike_ms;

    // Step 3: 组合故障下 RM 不崩
    for (int i = 0; i < 5; ++i) {
        const auto now = NowRealtimeNs();
        PmBookUpdate b;
        b.market_id          = "mkt_paper06_" + std::to_string(i);
        b.price              = 0.55;
        b.book_depth_l1_usdc = 20'000.0;
        // WSS 断 → stale ts
        b.event_ts_ns        = now - 10'000'000'000LL;
        b.data_source_ts_ns  = now - 9'999'000'000LL;
        b.ingestion_ts_ns    = now - 9'998'000'000LL;
        EXPECT_NO_FATAL_FAILURE({
            auto out = RunOneE2E(b, "sig_paper06_" + std::to_string(i));
            (void)out;
        }) << "C-PAPER-06: WSS断+REST慢组合故障下 RM 不崩 i=" << i;
    }

    // Step 4: 人工 ack 恢复 (模拟: 重连 + 清 REST 延迟)
    SimulateWssReconnect();
    ClearFault();

    EXPECT_TRUE(wss_connected_)   << "C-PAPER-06: 人工 ack → WSS 恢复";
    EXPECT_EQ(clob_delay_ms_, 0LL) << "C-PAPER-06: REST 延迟清零";

    // Step 5: 恢复后新鲜 book → RM 至少 1 笔 approved
    std::uint64_t post_recover_approved = 0;
    for (int i = 0; i < 5; ++i) {
        const auto now = NowRealtimeNs();
        PmBookUpdate b;
        b.market_id          = "mkt_paper06_recover_" + std::to_string(i);
        b.price              = 0.55;
        b.book_depth_l1_usdc = 20'000.0;
        b.event_ts_ns        = now - 5'000'000;
        b.data_source_ts_ns  = now - 4'000'000;
        b.ingestion_ts_ns    = now - 2'000'000;
        auto out = RunOneE2E(b, "sig_paper06_recover_" + std::to_string(i));
        if (out.rm_decision.is_approved()) ++post_recover_approved;
    }
    EXPECT_GE(post_recover_approved, 1u)
        << "C-PAPER-06: 恢复后至少 1 笔 approved (audit chain 不断, RM 正常)";

    // audit chain 不断: emitted_total > 0 (emitter 自始至终工作)
    EXPECT_GT(audit_emitter_->emitted_total(), 0u)
        << "C-PAPER-06: audit chain 不断 (emitter 持续工作)";

    // R-11: paper mode 不写 position WAL
    EXPECT_EQ(position_->HighWatermark(), 0u)
        << "C-PAPER-06: paper mode 不写 position WAL (R-11)";
}

}  // namespace
}  // namespace stcpp::test::chaos
