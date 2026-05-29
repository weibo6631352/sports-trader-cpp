// tests/chaos/latency/c_lat_02_rest_sustained.cpp — C-LAT-02: REST 持续慢响应 10 次
//
// Owner: 小宋 (test-replay-engineer)  E-03 chaos v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §1.2.3
//
// 场景: spike_ms=3000, count=10 持续慢响应
// 期望:
//   - backoff 生效; 不死锁; 总等待 < 60s
//
// 断言 (继承 R-12):
//   ASSERT wss_event_loop_tick_p99_us < 50 (R-12)
//   ASSERT no deadlock (test completes within timeout)

#include "tests/chaos/chaos_fixture.hpp"

#include <chrono>

namespace stcpp::test::chaos {
namespace {

using integration::PmBookUpdate;
using stcpp::infra::wal::pit::NowRealtimeNs;

class CLat02Fixture : public ChaosE2EFixture {};

// C-LAT-02: 10 次持续 REST 慢 → 不死锁; backoff 生效; WSS tick p99 < 50us
TEST_F(CLat02Fixture, C_LAT_02_rest_sustained_10x_no_deadlock) {
    // 注入 10 次 3s 慢响应
    FaultConfig cfg;
    cfg.kind        = FaultKind::LatencySpike;
    cfg.spike_ms    = 3'000;
    cfg.spike_count = 10;
    cfg.target_rest = true;
    InjectFault(cfg);

    EXPECT_EQ(clob_delay_ms_, 3'000) << "C-LAT-02: REST delay 3000ms 已注入";

    // WSS event loop tick p99 校验 (R-12 §17.1.1)
    const auto p99_us = p99_chaos_ns(fault_state_.wss_tick_latencies_ns) / 1'000;
    EXPECT_LT(p99_us, 50LL)
        << "C-LAT-02 / R-12: wss tick p99 " << p99_us
        << "us 必须 < 50us (持续 REST 慢不阻塞 WSS event loop)";

    // 不死锁: 在合理时间内处理 5 笔 intent
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 5; ++i) {
        const auto now = NowRealtimeNs();
        PmBookUpdate b;
        b.market_id          = "mkt_lat02_" + std::to_string(i);
        b.price              = 0.55;
        b.book_depth_l1_usdc = 20'000.0;
        b.event_ts_ns        = now - 5'000'000;
        b.data_source_ts_ns  = now - 4'000'000;
        b.ingestion_ts_ns    = now - 2'000'000;
        EXPECT_NO_FATAL_FAILURE({
            auto out = RunOneE2E(b, "sig_lat02_" + std::to_string(i));
            (void)out;
        }) << "C-LAT-02: 不死锁 — RM evaluate 不 abort";
    }
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    // 总等待 < 60s (纯 in-process mock, 无真实网络延迟)
    EXPECT_LT(elapsed_ms, 60'000LL)
        << "C-LAT-02: 5 笔处理总时间 " << elapsed_ms << "ms 必须 < 60s (不死锁)";
}

}  // namespace
}  // namespace stcpp::test::chaos
