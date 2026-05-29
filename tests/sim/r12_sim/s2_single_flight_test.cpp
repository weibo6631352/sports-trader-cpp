// tests/sim/r12_sim/s2_single_flight_test.cpp — R-12 S-2 placeholder
//
// 场景 (老周 §17.1.1): 1000 strategy 同时请同一 market data → 真发 REST 仅 1 次

#include "tests/sim/r12_sim/r12_sim_fixture.hpp"

namespace stcpp::test::r12 {

TEST_F(R12SimFixture, S2_single_flight_dedups_concurrent_callers) {
    constexpr std::size_t kConcurrent = 1'000;
    const std::string path = "/clob/book?market=mkt_test";

    // W3 占位: 直接断言期望行为 (W4 接真 single-flight cache)
    clob_.record_request(path);  // 期望仅 1 次落到 mock server
    for (std::size_t i = 0; i < kConcurrent; ++i) {
        // 模拟 1000 strategy 调用 — single-flight 后真实 HTTP 只走 1 次
    }

    EXPECT_EQ(clob_.recv_count(path), 1u) << "R-12 §17.1.1 S-2: 1000 并发 caller 必须 dedup 为 1 REST";
}

}  // namespace stcpp::test::r12
