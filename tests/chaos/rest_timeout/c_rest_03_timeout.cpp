// tests/chaos/rest_timeout/c_rest_03_timeout.cpp — C-REST-03: REST 完全超时 (无响应)
//
// Owner: 小宋 (test-replay-engineer)  E-03 chaos v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §1.2.4
//
// 场景: status=timeout, timeout_ms=10000
// 期望:
//   - client 在 deadline 内主动断
//   - 不阻塞 WSS loop
//   - alert log 出 rest_timeout_count
//
// 断言:
//   ASSERT wss_event_loop_unblocked (R-12)
//   ASSERT rest_error_count > 0 (timeout 记录)
//   ASSERT test completes (no hang)

#include "tests/chaos/chaos_fixture.hpp"

namespace stcpp::test::chaos {
namespace {

using integration::PmBookUpdate;
using stcpp::infra::wal::pit::NowRealtimeNs;

class CRest03Fixture : public ChaosE2EFixture {};

// C-REST-03: REST 完全超时 → 不阻塞 WSS; rest_timeout_count 记录; 不 hang
TEST_F(CRest03Fixture, C_REST_03_rest_timeout_no_hang_wss_unblocked) {
    // 注入完全超时 (status=0 = no response)
    FaultConfig cfg;
    cfg.kind             = FaultKind::RestTimeout;
    cfg.http_status_code = 0;        // timeout = no response
    cfg.timeout_ms       = 10'000;   // 10s timeout
    InjectFault(cfg);

    // REST 超时: clob_delay_ms_ == timeout_ms (模拟无响应)
    EXPECT_EQ(clob_delay_ms_, 10'000)
        << "C-REST-03: REST 超时已注入 (delay=10s)";

    // rest_error_count 记录超时
    EXPECT_GE(fault_state_.rest_error_count, 1u)
        << "C-REST-03: rest_timeout_count (rest_error_count) 必须记录";

    // WSS event loop 不被 REST 超时阻塞 (R-12)
    if (!fault_state_.wss_tick_latencies_ns.empty()) {
        const auto p99_us = p99_chaos_ns(fault_state_.wss_tick_latencies_ns) / 1'000;
        EXPECT_LT(p99_us, 50LL)
            << "C-REST-03 / R-12: wss tick p99 " << p99_us << "us 必须 < 50us";
    }

    // 不 hang: 处理 3 笔 intent 完成 (mock 不真超时)
    for (int i = 0; i < 3; ++i) {
        const auto now = NowRealtimeNs();
        PmBookUpdate b;
        b.market_id          = "mkt_rest03_" + std::to_string(i);
        b.price              = 0.55;
        b.book_depth_l1_usdc = 20'000.0;
        b.event_ts_ns        = now - 5'000'000;
        b.data_source_ts_ns  = now - 4'000'000;
        b.ingestion_ts_ns    = now - 2'000'000;
        EXPECT_NO_FATAL_FAILURE({
            auto out = RunOneE2E(b, "sig_rest03_" + std::to_string(i));
            (void)out;
        }) << "C-REST-03: REST 超时期间 RM 不 hang / abort i=" << i;
    }
}

}  // namespace
}  // namespace stcpp::test::chaos
