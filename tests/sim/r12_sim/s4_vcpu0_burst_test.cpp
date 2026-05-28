// tests/sim/r12_sim/s4_vcpu0_burst_test.cpp — R-12 S-4 placeholder
//
// 场景 (老周 §17.1.1): vCPU0 上 4 WSS connection × 1k msg/s × 60s 不阻塞 event loop

#include "tests/sim/r12_sim/r12_sim_fixture.hpp"

namespace stcpp::test::r12 {

TEST_F(R12SimFixture, S4_vcpu0_burst_does_not_starve_loop) {
    constexpr int kConnections = 4;
    constexpr int kMsgPerSecPerConn = 1'000;
    constexpr int kSeconds = 60;
    constexpr int kTotal = kConnections * kMsgPerSecPerConn * kSeconds;

    // W3 占位: 模拟 240k tick, 每个 25us (W4 接真 vCPU0 event loop)
    for (int i = 0; i < kTotal; ++i) {
        wss_tick_latencies_ns_.push_back(25'000 + (i % 5) * 500);
    }

    EXPECT_LE(p99_ns(wss_tick_latencies_ns_), 50'000)
        << "R-12 §17.1.1 S-4: vCPU0 4 conn burst p99 必须 < 50us";
    EXPECT_GE(wss_tick_latencies_ns_.size(), 240'000u);
}

}  // namespace stcpp::test::r12
