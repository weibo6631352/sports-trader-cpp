// tests/sim/r12_sim/s3_wss_disconnect_batch_test.cpp — R-12 S-3 placeholder
//
// 场景: WebSocket 断线 + 5 markets 同时刷新, REST 兜底只发 1 个 batched 调用

#include "tests/sim/r12_sim/r12_sim_fixture.hpp"

namespace stcpp::test::r12 {

TEST_F(R12SimFixture, S3_wss_disconnect_falls_back_to_one_batched_rest) {
    wss_.disconnect_all();

    const std::string batch_path = "/clob/books?markets=m1,m2,m3,m4,m5";
    clob_.record_request(batch_path);  // 期望 1 次批量

    EXPECT_EQ(wss_.disconnect_calls(), 1u);
    EXPECT_EQ(clob_.recv_count(batch_path), 1u) << "R-12 §17.1.1 S-3: 5 markets 兜底必须 batched 为 1 REST";
}

}  // namespace stcpp::test::r12
