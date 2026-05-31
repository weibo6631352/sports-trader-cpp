// tests/unit/test_live_order_gate.cpp — LiveOrderGate 强制风控门
//
// Owner: GM (老雷) 2026-05-31 — Phase 4 D-A。
// 核心断言: RM 拒单 → 下单 sink **零调用** (红线 §8: 任何下单必经 RM)。
//   注入 stub sink, 离线验证 (不触网)。RM setup 镜像 test_risk_gateway.cpp。
#include "stcpp/polymarket/live_order_gate.hpp"

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
using stcpp::risk::Decision;
using stcpp::risk::MarketState;
using stcpp::risk::Outcome;
using stcpp::risk::RiskConfig;
using stcpp::risk::RiskGateway;
using stcpp::risk::RmState;
using stcpp::risk::Side;

constexpr std::int64_t NS_PER_MS = 1'000'000LL;
constexpr const char* kTokenId = "1234567890";
constexpr const char* kConditionId = "0xa9db600590209698097db2fb8382989ea1cf6a9b91f0428b2e1d4f35d724c3ff";

class InMemoryEmitter : public AuditEmitter {
public:
    [[nodiscard]] bool emit(AuditRecord const& r) noexcept override {
        std::lock_guard<std::mutex> g(mu_);
        records_.push_back(r);
        return true;
    }

private:
    mutable std::mutex mu_;
    std::vector<AuditRecord> records_;
};

class LiveOrderGateTest : public ::testing::Test {
protected:
    void SetUp() override {
        emitter_ = std::make_shared<InMemoryEmitter>();
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
        rm_->set_market_state(kConditionId, MarketState::PREGAME);
        rm_->set_market_freshness_ms(kConditionId, 100);
        rm_->set_market_active(kConditionId, true);
    }

    stcpp::risk::OrderIntent make_ok_intent(std::string sig = "sig_gate") {
        auto const now = ::stcpp::infra::wal::pit::NowRealtimeNs();
        stcpp::risk::OrderIntent it;
        it.event_ts_ns = now - 500 * NS_PER_MS;
        it.data_source_ts_ns = now - 400 * NS_PER_MS;
        it.ingestion_ts_ns = now - 100 * NS_PER_MS;
        it.as_of_ts_ns = now - 10 * NS_PER_MS;
        it.condition_id = kConditionId;
        it.token_id = kTokenId;
        it.outcome = Outcome::Yes;
        it.side = Side::Buy;
        it.strategy_id = "strat_a";
        it.signal_id = std::move(sig);
        it.feature_snapshot_id = "fs_01H";
        it.price = 0.50;
        it.size_pUSD_micro = 1'000;
        it.book_depth_l1_usdc = 5'000;
        it.book_snapshot_ts_ns = now - 200 * NS_PER_MS;
        it.tick_size = 0.01;
        it.is_close = false;
        it.timestamp_ms = now / NS_PER_MS;
        return it;
    }

    // 计数 stub sink。
    OrderSinkFn make_counting_sink() {
        return [this](const LiveOrderRequest& req) -> LiveOrderResult {
            ++sink_calls_;
            last_req_ = req;
            LiveOrderResult r;
            r.success = true;
            r.http_status = 200;
            r.order_id = "0xstub";
            r.status = "matched";
            return r;
        };
    }

    std::shared_ptr<InMemoryEmitter> emitter_;
    std::unique_ptr<RiskGateway> rm_;
    int sink_calls_{0};
    LiveOrderRequest last_req_;
};

// 翻译: BUY $1 @ 0.5 → maker=1e6(USDC), taker=2e6(shares)。
TEST(LiveOrderGateTranslate, BuyAmounts) {
    stcpp::risk::OrderIntent it;
    it.token_id = "999";
    it.side = Side::Buy;
    it.price = 0.5;
    it.size_pUSD_micro = 1'000'000;  // $1
    const auto req = TranslateIntent(it, /*neg_risk=*/false);
    EXPECT_TRUE(req.is_buy);
    EXPECT_EQ(req.token_id, "999");
    EXPECT_EQ(req.maker_amount, 1'000'000u);  // USDC
    EXPECT_EQ(req.taker_amount, 2'000'000u);  // 2 shares
    EXPECT_EQ(req.order_type, "FOK");
}

// 翻译: SELL $1 @ 0.5 → maker=2e6(shares), taker=1e6(USDC); neg_risk 透传。
TEST(LiveOrderGateTranslate, SellAmountsAndNegRisk) {
    stcpp::risk::OrderIntent it;
    it.side = Side::Sell;
    it.price = 0.5;
    it.size_pUSD_micro = 1'000'000;
    const auto req = TranslateIntent(it, /*neg_risk=*/true);
    EXPECT_FALSE(req.is_buy);
    EXPECT_EQ(req.maker_amount, 2'000'000u);  // shares
    EXPECT_EQ(req.taker_amount, 1'000'000u);  // USDC
    EXPECT_TRUE(req.neg_risk);
}

// 门1 fail-closed: 默认未开闸 → sink 零调用, RM 都不进。
TEST_F(LiveOrderGateTest, DisarmedBlocksBeforeRm) {
    LiveOrderGate gate(*rm_, make_counting_sink());
    EXPECT_FALSE(gate.Armed());
    const auto gr = gate.Submit(make_ok_intent(), false);
    EXPECT_FALSE(gr.submitted);
    EXPECT_EQ(gr.gate_block, GateBlock::DISARMED);
    EXPECT_FALSE(gr.rm_approved);
    EXPECT_EQ(sink_calls_, 0);
}

// Disarm = kill: 开闸成功一笔后 Disarm → 立即停发。
TEST_F(LiveOrderGateTest, DisarmKillsSubmit) {
    LiveOrderGate gate(*rm_, make_counting_sink());
    gate.Arm();
    EXPECT_TRUE(gate.Submit(make_ok_intent("s1"), false).submitted);
    gate.Disarm();
    const auto gr = gate.Submit(make_ok_intent("s2"), false);
    EXPECT_FALSE(gr.submitted);
    EXPECT_EQ(gr.gate_block, GateBlock::DISARMED);
    EXPECT_EQ(sink_calls_, 1);
}

// 红线核心: armed 后 RM HALTED → 下单 sink 零调用, 不触达 CLOB。
TEST_F(LiveOrderGateTest, HaltedBlocksSubmit) {
    rm_->set_state(RmState::HALTED);
    LiveOrderGate gate(*rm_, make_counting_sink());
    gate.Arm();
    const auto gr = gate.Submit(make_ok_intent(), /*neg_risk=*/false);
    EXPECT_FALSE(gr.submitted);
    EXPECT_FALSE(gr.rm_approved);
    EXPECT_EQ(sink_calls_, 0) << "红线违反: RM 拒单但 sink 被调用 (下单触达 CLOB)";
    EXPECT_EQ(gr.reject_code, stcpp::risk::RejectCode::STATE_HALTED);
}

// SAFE_MODE 开仓也必须拒 + sink 零调用。
TEST_F(LiveOrderGateTest, SafeModeBlocksSubmit) {
    rm_->set_state(RmState::SAFE_MODE);
    LiveOrderGate gate(*rm_, make_counting_sink());
    gate.Arm();
    const auto gr = gate.Submit(make_ok_intent(), false);
    EXPECT_FALSE(gr.submitted);
    EXPECT_EQ(sink_calls_, 0);
}

// armed + RM 放行 → sink 恰好调用一次, 且请求已正确翻译。
TEST_F(LiveOrderGateTest, ApprovedSubmitsOnce) {
    LiveOrderGate gate(*rm_, make_counting_sink());
    gate.Arm();
    const auto gr = gate.Submit(make_ok_intent(), false);
    ASSERT_TRUE(gr.rm_approved) << "make_ok_intent 应被 RM 放行";
    EXPECT_TRUE(gr.submitted);
    EXPECT_EQ(sink_calls_, 1);
    EXPECT_EQ(gate.OrdersToday(), 1);
    EXPECT_EQ(last_req_.token_id, kTokenId);
    EXPECT_TRUE(last_req_.is_buy);
    EXPECT_TRUE(gr.order.success);
}

// 重复 intent (同 signal_id) → 第二次被 RM duplicate 拒 → sink 不再调用。
TEST_F(LiveOrderGateTest, DuplicateIntentBlocksSecondSubmit) {
    LiveOrderGate gate(*rm_, make_counting_sink());
    gate.Arm();
    const auto it = make_ok_intent("dup_sig");
    const auto g1 = gate.Submit(it, false);
    EXPECT_TRUE(g1.submitted);
    const auto g2 = gate.Submit(it, false);
    EXPECT_FALSE(g2.submitted) << "重复 intent 应被 RM 拒";
    EXPECT_EQ(sink_calls_, 1) << "重复单不得再次触达 CLOB";
}

// 门2 OrderRateCap: 日单数超限 → 拦截, 不进 RM。
TEST_F(LiveOrderGateTest, RateCapBlocks) {
    LiveGateConfig cfg;
    cfg.max_orders_per_day = 2;
    LiveOrderGate gate(*rm_, make_counting_sink(), cfg);
    gate.Arm();
    EXPECT_TRUE(gate.Submit(make_ok_intent("r1"), false).submitted);
    EXPECT_TRUE(gate.Submit(make_ok_intent("r2"), false).submitted);
    const auto gr = gate.Submit(make_ok_intent("r3"), false);
    EXPECT_FALSE(gr.submitted);
    EXPECT_EQ(gr.gate_block, GateBlock::RATE_CAP);
    EXPECT_EQ(sink_calls_, 2) << "超限单不得触达 CLOB";
    gate.ResetDailyCount();
    EXPECT_TRUE(gate.Submit(make_ok_intent("r4"), false).submitted) << "日切后恢复";
}

// 灰度 RiskConfig 工厂: 老韩灰度值 (绝不复用 paper 100k)。
TEST(LiveOrderGateGray, GrayLaunchConfigValues) {
    const auto c = MakeGrayLaunchRiskConfig();
    EXPECT_EQ(c.per_order_cap_usdc.v, 1'000'000);       // $1
    EXPECT_EQ(c.market_exposure_cap_usdc.v, 2'000'000); // $2
    EXPECT_EQ(c.per_outcome_cap_usdc.v, 2'000'000);     // $2
    EXPECT_EQ(c.bankroll_usdc.v, 25'000'000);           // $25
    EXPECT_EQ(c.daily_loss_halt_usdc.v, 5'000'000);     // $5
    EXPECT_EQ(c.consec_loss_halt_count, 3);
}

}  // namespace stcpp::polymarket::test
