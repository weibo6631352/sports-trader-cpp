// tests/unit/test_live_executor.cpp — LiveExecutor (决策→gate→中性 ExecReport)
//
// Owner: GM (老雷) 2026-05-31 — Phase 4 接线。
// 验证: 中性 ExecReport 取自回执实际成交量 (老韩硬要求, 非请求量) + disarmed 不成交。
#include "stcpp/polymarket/live_executor.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/risk/risk_gateway.hpp"

namespace stcpp::polymarket::test {

using stcpp::risk::AuditEmitter;
using stcpp::risk::AuditRecord;
using stcpp::risk::MarketState;
using stcpp::risk::Outcome;
using stcpp::risk::RiskConfig;
using stcpp::risk::RiskGateway;
using stcpp::risk::RmState;
using stcpp::risk::Side;

constexpr std::int64_t NS_PER_MS = 1'000'000LL;
constexpr const char* kCid = "0xa9db600590209698097db2fb8382989ea1cf6a9b91f0428b2e1d4f35d724c3ff";

class ExecEmitter : public AuditEmitter {
public:
    [[nodiscard]] bool emit(AuditRecord const&) noexcept override { return true; }
};

class LiveExecutorTest : public ::testing::Test {
protected:
    void SetUp() override {
        emitter_ = std::make_shared<ExecEmitter>();
        RiskConfig cfg;
        cfg.per_order_cap_usdc = stcpp::domain::MicroPUSD::from_micro(10'000);
        cfg.market_exposure_cap_usdc = stcpp::domain::MicroPUSD::from_micro(50'000);
        cfg.per_outcome_cap_usdc = stcpp::domain::MicroPUSD::from_micro(25'000);
        cfg.bankroll_usdc = stcpp::domain::MicroPUSD::from_pusd(100'000.0);
        cfg.daily_loss_halt_usdc = stcpp::domain::MicroPUSD::from_pusd(5'000.0);
        cfg.consec_loss_halt_count = 5;
        cfg.excessive_slippage_bps = 200;
        rm_ = std::make_unique<RiskGateway>(cfg, emitter_);
        rm_->set_state(RmState::RUNNING);
        rm_->set_market_state(kCid, MarketState::PREGAME);
        rm_->set_market_freshness_ms(kCid, 100);
        rm_->set_market_active(kCid, true);
    }

    stcpp::risk::OrderIntent intent(Side side, std::string sig) {
        auto const now = ::stcpp::infra::wal::pit::NowRealtimeNs();
        stcpp::risk::OrderIntent it;
        it.event_ts_ns = now - 500 * NS_PER_MS;
        it.data_source_ts_ns = now - 400 * NS_PER_MS;
        it.ingestion_ts_ns = now - 100 * NS_PER_MS;
        it.as_of_ts_ns = now - 10 * NS_PER_MS;
        it.condition_id = kCid;
        it.token_id = "1234567890";
        it.outcome = Outcome::Yes;
        it.side = side;
        it.strategy_id = "s";
        it.signal_id = std::move(sig);
        it.feature_snapshot_id = "fs";
        it.price = 0.074;
        it.size_pUSD_micro = 1'000;
        it.book_depth_l1_usdc = 5'000;
        it.book_snapshot_ts_ns = now - 200 * NS_PER_MS;
        it.tick_size = 0.001;
        it.timestamp_ms = now / NS_PER_MS;
        return it;
    }

    // stub sink: 回执 matched, making=1.02 USDC / taking=13.78 shares (BUY 真网那笔)。
    OrderSinkFn matched_sink() {
        return [](const LiveOrderRequest&) -> LiveOrderResult {
            LiveOrderResult r;
            r.success = true;
            r.http_status = 200;
            r.status = "matched";
            r.order_id = "0x6f11be4d";
            r.transaction_hash = "0x41bd6c50";
            r.making_amount = 1.02;    // BUY: USDC 实付
            r.taking_amount = 13.78;   // BUY: shares 实得
            return r;
        };
    }

    std::shared_ptr<ExecEmitter> emitter_;
    std::unique_ptr<RiskGateway> rm_;
};

// disarmed → 不成交, 拦在 gate。
TEST_F(LiveExecutorTest, DisarmedNoFill) {
    LiveOrderGate gate(*rm_, matched_sink());
    LiveExecutor ex(gate);
    const auto rep = ex.Execute(intent(Side::Buy, "e1"), false);
    EXPECT_FALSE(rep.filled);
    EXPECT_FALSE(rep.submitted);
    EXPECT_EQ(rep.gate_block, GateBlock::DISARMED);
    EXPECT_DOUBLE_EQ(rep.filled_usdc, 0.0);
}

// armed + matched → 中性 ExecReport 取**回执实际成交量** (非请求量)。
TEST_F(LiveExecutorTest, BuyFillFromReceipt) {
    LiveOrderGate gate(*rm_, matched_sink());
    gate.Arm();
    LiveExecutor ex(gate);
    const auto rep = ex.Execute(intent(Side::Buy, "e2"), false);
    ASSERT_TRUE(rep.filled);
    EXPECT_TRUE(rep.submitted);
    EXPECT_TRUE(rep.rm_approved);
    EXPECT_DOUBLE_EQ(rep.filled_usdc, 1.02);     // making (USDC 实付)
    EXPECT_DOUBLE_EQ(rep.filled_shares, 13.78);  // taking (shares 实得)
    EXPECT_NEAR(rep.fill_price, 1.02 / 13.78, 1e-9);
    EXPECT_EQ(rep.order_id, "0x6f11be4d");
    EXPECT_EQ(rep.tx_hash, "0x41bd6c50");
}

// SELL: making=shares / taking=USDC 反向映射。
TEST_F(LiveExecutorTest, SellFillMapping) {
    auto sink = [](const LiveOrderRequest&) -> LiveOrderResult {
        LiveOrderResult r;
        r.success = true;
        r.status = "matched";
        r.making_amount = 13.78;  // SELL: shares 实卖
        r.taking_amount = 1.02;   // SELL: USDC 实得
        return r;
    };
    LiveOrderGate gate(*rm_, sink);
    gate.Arm();
    LiveExecutor ex(gate);
    const auto rep = ex.Execute(intent(Side::Sell, "e3"), false);
    ASSERT_TRUE(rep.filled);
    EXPECT_DOUBLE_EQ(rep.filled_usdc, 1.02);     // taking (USDC 实得)
    EXPECT_DOUBLE_EQ(rep.filled_shares, 13.78);  // making (shares 实卖)
}

// unmatched (FOK 未成交) → 不写账本。
TEST_F(LiveExecutorTest, UnmatchedNoFill) {
    auto sink = [](const LiveOrderRequest&) -> LiveOrderResult {
        LiveOrderResult r;
        r.success = false;
        r.status = "unmatched";
        return r;
    };
    LiveOrderGate gate(*rm_, sink);
    gate.Arm();
    LiveExecutor ex(gate);
    const auto rep = ex.Execute(intent(Side::Buy, "e4"), false);
    EXPECT_FALSE(rep.filled);
    EXPECT_DOUBLE_EQ(rep.filled_usdc, 0.0);
}

}  // namespace stcpp::polymarket::test
