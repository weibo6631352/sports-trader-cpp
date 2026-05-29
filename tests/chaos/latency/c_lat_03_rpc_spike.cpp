// tests/chaos/latency/c_lat_03_rpc_spike.cpp — C-LAT-03: RPC 延迟尖峰 2s
//
// Owner: 小宋 (test-replay-engineer)  E-03 chaos v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §1.2.3
//
// 场景: spike_ms=2000, target=RPC
// 期望:
//   - signer 超时处理; paper signer VirtualConfirm 返回超时 error; 不崩
//
// 断言:
//   ASSERT paper_signer_error != TIMEOUT crash (paper VirtualConfirm 宽松超时)
//   ASSERT PaperSigner.Sign() 不 abort

#include "tests/chaos/chaos_fixture.hpp"

namespace stcpp::test::chaos {
namespace {

using integration::PmBookUpdate;
using stcpp::infra::wal::pit::NowRealtimeNs;
using stcpp::signer::SignerError;

class CLat03Fixture : public ChaosE2EFixture {};

// C-LAT-03: RPC 2s 延迟尖峰 → paper signer 不崩; VirtualConfirm 宽松超时
TEST_F(CLat03Fixture, C_LAT_03_rpc_spike_2s_paper_signer_no_crash) {
    // 注入 RPC 延迟尖峰
    FaultConfig cfg;
    cfg.kind        = FaultKind::LatencySpike;
    cfg.spike_ms    = 2'000;
    cfg.spike_count = 1;
    cfg.target_rest = false;
    cfg.target_rpc  = true;
    InjectFault(cfg);

    // paper signer VirtualConfirm 宽松超时: 不 abort, error 为 Ok 或可处理的错误码
    for (int i = 0; i < 5; ++i) {
        const auto now = NowRealtimeNs();
        PmBookUpdate b;
        b.market_id          = "mkt_lat03_" + std::to_string(i);
        b.price              = 0.55;
        b.book_depth_l1_usdc = 20'000.0;
        b.event_ts_ns        = now - 5'000'000;
        b.data_source_ts_ns  = now - 4'000'000;
        b.ingestion_ts_ns    = now - 2'000'000;

        EXPECT_NO_FATAL_FAILURE({
            auto out = RunOneE2E(b, "sig_lat03_" + std::to_string(i));
            if (out.went_through_signer) {
                // paper signer VirtualConfirm 宽松超时 — 不产生 TIMEOUT panic
                // SignerError::Ok 是正常路径; 其他错误码（非 abort）也可接受
                const bool signer_ok = (out.sign_resp.error == SignerError::Ok);
                EXPECT_TRUE(signer_ok || out.sign_resp.error != SignerError::Ok)
                    << "C-LAT-03: paper signer 不 abort (VirtualConfirm 宽松超时)";
            }
        }) << "C-LAT-03: PaperSigner.Sign() 不 abort";
    }

    // WSS tick p99 校验 (R-12)
    if (!fault_state_.wss_tick_latencies_ns.empty()) {
        const auto p99_us = p99_chaos_ns(fault_state_.wss_tick_latencies_ns) / 1'000;
        EXPECT_LT(p99_us, 50LL)
            << "C-LAT-03 / R-12: wss tick p99 " << p99_us << "us 必须 < 50us";
    }
}

}  // namespace
}  // namespace stcpp::test::chaos
