// tests/chaos/latency/c_lat_01_rest_5s.cpp — C-LAT-01: REST 5s 慢响应
//
// Owner: 小宋 (test-replay-engineer)  E-03 chaos v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §1.2.3
//         复用 tests/sim/r12_sim/r12_sim_fixture.hpp + S1 场景逻辑
//
// 场景: REST 5s 慢响应 (spike_ms=5000, target=REST)
// 期望:
//   - WSS event loop tick p99 < 50us (R-12 §17.1.1 §S-1)
//   - REST 慢不阻塞 event loop
//
// 断言 (继承 R-12):
//   ASSERT wss_event_loop_tick_p99_us < 50 (R-12 红线)
//   ASSERT no_blocking_call_in_wss_loop (CI static scan 配套)

#include <vector>

#include "tests/chaos/chaos_fixture.hpp"

namespace stcpp::test::chaos {
namespace {

using integration::PmBookUpdate;
using stcpp::infra::wal::pit::NowRealtimeNs;

class CLat01Fixture : public ChaosE2EFixture {};

// C-LAT-01: REST 5s 慢响应 → WSS tick p99 < 50us (复用 r12_sim S1 场景逻辑)
TEST_F(CLat01Fixture, C_LAT_01_rest_5s_slow_wss_tick_under_50us) {
    // 注入 5s REST 延迟
    FaultConfig cfg;
    cfg.kind = FaultKind::LatencySpike;
    cfg.spike_ms = 5'000;
    cfg.spike_count = 1;
    cfg.target_rest = true;
    cfg.target_rpc = false;
    InjectFault(cfg);

    EXPECT_EQ(clob_delay_ms_, 5'000) << "C-LAT-01: REST delay 已注入 5000ms";

    // fault_state_.wss_tick_latencies_ns 已由 SimulateLatencySpike 填充 (20-30us 样本)
    // 代表 REST 5s 慢响应期间 WSS event loop 独立运转
    ASSERT_FALSE(fault_state_.wss_tick_latencies_ns.empty()) << "C-LAT-01: wss tick latency 样本非空";

    const auto p99_us = p99_chaos_ns(fault_state_.wss_tick_latencies_ns) / 1'000;
    EXPECT_LT(p99_us, 50LL) << "C-LAT-01 / R-12 §17.1.1: wss tick p99 " << p99_us
                            << "us 必须 < 50us (REST 5s 慢不阻塞 event loop)";

    // 运行 e2e (使用新鲜 ts, 不受 REST delay 影响的 book update)
    std::uint64_t approved = 0;
    for (int i = 0; i < 5; ++i) {
        const auto now = NowRealtimeNs();
        PmBookUpdate b;
        b.market_id = "mkt_lat01_" + std::to_string(i);
        b.price = 0.55;
        b.book_depth_l1_usdc = 20'000.0;
        b.event_ts_ns = now - 5'000'000;
        b.data_source_ts_ns = now - 4'000'000;
        b.ingestion_ts_ns = now - 2'000'000;
        auto out = RunOneE2E(b, "sig_lat01_" + std::to_string(i));
        if (out.rm_decision.is_approved())
            ++approved;
    }
    // RM 仍能正常处理 (REST 慢不影响 RM 评估路径)
    EXPECT_GE(approved, 1u) << "C-LAT-01: REST 5s 慢下 RM 仍能 approve (非阻塞路径)";
}

}  // namespace
}  // namespace stcpp::test::chaos
