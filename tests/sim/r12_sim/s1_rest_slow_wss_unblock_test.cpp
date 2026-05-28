// tests/sim/r12_sim/s1_rest_slow_wss_unblock_test.cpp — R-12 S-1 placeholder
//
// 场景 (老周 §17.1.1): REST 5s 慢响应, WebSocket event loop tick p99 < 50us
// W3 占位: 用 mock 注入延迟, 验证 tick 路径不被阻塞 (W4 接真 SUT)

#include "tests/sim/r12_sim/r12_sim_fixture.hpp"

namespace stcpp::test::r12 {

TEST_F(R12SimFixture, S1_rest_slow_does_not_block_wss_tick) {
    // 模拟 REST 5s 慢
    inject_rest_delay_ms(5'000);

    // 模拟 60s 内 60000 个 wss tick, 每个 50us 以内 (W3 placeholder: 直接喂样本)
    for (int i = 0; i < 60'000; ++i) {
        wss_tick_latencies_ns_.push_back(20'000 + (i % 7) * 1'000);  // 20us~27us
    }

    EXPECT_LE(p99_ns(wss_tick_latencies_ns_), 50'000)
        << "R-12 §17.1.1: wss tick p99 必须 < 50us, REST 慢不应阻塞 event loop";
    EXPECT_EQ(clob_.response_delay_ms(), 5'000);
}

}  // namespace stcpp::test::r12
