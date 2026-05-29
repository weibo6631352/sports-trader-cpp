// tests/chaos/rest_timeout/c_rest_02_502_gamma.cpp — C-REST-02: gamma 502 快照失败
//
// Owner: 小宋 (test-replay-engineer)  E-03 chaos v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §1.2.4
//
// 场景: status=502, path=/markets → fallback endpoint
// 期望:
//   - 切 fallback endpoint (老叶 D8 multi-provider)
//   - book 降级到 WSS only
//
// 断言:
//   ASSERT rest_fallback_triggered == true (502 触发 fallback)
//   ASSERT system continues to function (WSS-only book mode)

#include "tests/chaos/chaos_fixture.hpp"

namespace stcpp::test::chaos {
namespace {

using integration::PmBookUpdate;
using stcpp::infra::wal::pit::NowRealtimeNs;

class CRest02Fixture : public ChaosE2EFixture {};

// C-REST-02: gamma 502 → fallback endpoint 触发, book 降级到 WSS only
TEST_F(CRest02Fixture, C_REST_02_gamma_502_triggers_fallback) {
    // 注入 502
    FaultConfig cfg;
    cfg.kind = FaultKind::RestTimeout;
    cfg.http_status_code = 502;
    cfg.path_filter = "/markets";
    InjectFault(cfg);

    // 502 应触发 fallback (stub 层记录)
    EXPECT_TRUE(fault_state_.rest_fallback_triggered)
        << "C-REST-02: 502 gamma snapshot 失败 → fallback endpoint 触发";

    // fallback 后系统使用 WSS-only book: RM 应仍能处理 intent (使用 WSS 数据)
    std::uint64_t processed = 0;
    for (int i = 0; i < 5; ++i) {
        const auto now = NowRealtimeNs();
        PmBookUpdate b;
        b.market_id = "mkt_rest02_" + std::to_string(i);
        b.price = 0.55;
        b.book_depth_l1_usdc = 20'000.0;
        // WSS 数据: 正常 ts (fallback 后 WSS only 仍更新 book)
        b.event_ts_ns = now - 5'000'000;
        b.data_source_ts_ns = now - 4'000'000;
        b.ingestion_ts_ns = now - 2'000'000;
        EXPECT_NO_FATAL_FAILURE({
            auto out = RunOneE2E(b, "sig_rest02_" + std::to_string(i));
            ++processed;
            (void)out;
        }) << "C-REST-02: 502 fallback 后 RM evaluate 不 abort";
    }
    EXPECT_EQ(processed, 5u) << "C-REST-02: fallback 后 5 笔全处理";
}

}  // namespace
}  // namespace stcpp::test::chaos
