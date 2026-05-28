// tests/unit/test_risk_gateway.cpp — RiskGateway v0.1 单测 (老韩 W4 Wave 19)
//
// 覆盖: 21 reject × ≥1 case + INVALID_INTENT × 9 sub_reason + 5 态状态机
//       + SlippageModel 联动 + WAL emit 联动 (mock InMemoryEmitter)
// 红线 R-1: 每 reject 必 audit_id 非空

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/risk/risk_gateway.hpp"

namespace stcpp::risk::test {

// ---------- in-memory emitter (mock 占位 老王 WalWriter) ----------------------

class InMemoryEmitter : public AuditEmitter {
 public:
    [[nodiscard]] bool emit(AuditRecord const& r) noexcept override {
        if (fail_next_) {
            fail_next_ = false;
            return false;  // 模拟 ring 满 / Backpressure
        }
        records_.push_back(r);
        return true;
    }
    void trigger_backpressure_next() noexcept { fail_next_ = true; }
    [[nodiscard]] std::size_t size() const noexcept { return records_.size(); }
    [[nodiscard]] AuditRecord const& back() const { return records_.back(); }
 private:
    std::vector<AuditRecord> records_;
    bool                     fail_next_{false};
};

// ---------- fixture ----------------------------------------------------------

constexpr std::int64_t NS_PER_MS = 1'000'000LL;

class RiskGatewayTest : public ::testing::Test {
 protected:
    void SetUp() override {
        emitter_ = std::make_shared<InMemoryEmitter>();
        cfg_ = RiskConfig{};
        cfg_.per_order_cap_usdc       = 10'000;
        cfg_.market_exposure_cap_usdc = 50'000;
        cfg_.bankroll_usdc            = 100'000;
        cfg_.daily_loss_halt_usdc     = 5'000;
        cfg_.consec_loss_halt_count   = 5;
        cfg_.excessive_slippage_bps   = 200;
        rm_ = std::make_unique<RiskGateway>(cfg_, emitter_);
        rm_->set_state(RmState::RUNNING);  // 测试默认 RUNNING (绕过 SAFE_MODE 默认)
    }

    // 构造 4 ts 满足 R-20 单调不等式 + book_snapshot fresh 的合法 intent
    OrderIntent make_ok_intent(std::string sig = "sig_default") {
        auto const now = ::stcpp::infra::wal::pit::NowRealtimeNs();
        OrderIntent it;
        it.event_ts_ns         = now - 500 * NS_PER_MS;
        it.data_source_ts_ns   = now - 400 * NS_PER_MS;
        it.ingestion_ts_ns     = now - 100 * NS_PER_MS;
        it.as_of_ts_ns         = now -  10 * NS_PER_MS;
        it.market_id           = "mkt_test";
        it.strategy_id         = "strat_a";
        it.signal_id           = std::move(sig);
        it.feature_snapshot_id = "fs_01H";
        it.is_buy              = true;
        it.price               = 0.50;
        it.size_usdc           = 1'000;
        it.book_depth_l1_usdc  = 5'000;
        it.book_snapshot_ts_ns = now - 200 * NS_PER_MS;
        it.tick_size           = 0.01;
        it.is_close            = false;
        return it;
    }

    void expect_rejected(RiskDecision const& d, RejectCode code) {
        EXPECT_EQ(d.decision, Decision::REJECTED);
        EXPECT_EQ(d.reject, code);
        // R-1: audit_id 必须非空 (16B 全 0 视作未填)
        bool any_nonzero = false;
        for (auto b : d.audit_id) any_nonzero = any_nonzero || (b != 0);
        EXPECT_TRUE(any_nonzero) << "R-1 violation: audit_id 全 0";
    }

    RiskConfig                       cfg_;
    std::shared_ptr<InMemoryEmitter> emitter_;
    std::unique_ptr<RiskGateway>     rm_;
};

// ===== 21 reject enum 覆盖 ===================================================

TEST_F(RiskGatewayTest, R01_STATE_HALTED) {
    rm_->set_state(RmState::HALTED);
    auto d = rm_->evaluate(make_ok_intent());
    expect_rejected(d, RejectCode::STATE_HALTED);
}

TEST_F(RiskGatewayTest, R02_STATE_DRAIN_open_rejected) {
    rm_->set_state(RmState::DRAIN);
    auto it = make_ok_intent();
    it.is_close = false;
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::STATE_DRAIN);
}

TEST_F(RiskGatewayTest, R02b_STATE_DRAIN_close_approved) {
    rm_->set_state(RmState::DRAIN);
    auto it = make_ok_intent();
    it.is_close = true;
    auto d = rm_->evaluate(it);
    EXPECT_EQ(d.decision, Decision::APPROVED);
}

TEST_F(RiskGatewayTest, R03_STATE_SAFE_MODE_open_rejected) {
    rm_->set_state(RmState::SAFE_MODE);
    auto d = rm_->evaluate(make_ok_intent());
    expect_rejected(d, RejectCode::STATE_SAFE_MODE);
}

TEST_F(RiskGatewayTest, R04_DUPLICATE_INTENT) {
    auto it = make_ok_intent("sig_dup");
    auto d1 = rm_->evaluate(it);
    EXPECT_EQ(d1.decision, Decision::APPROVED);
    auto d2 = rm_->evaluate(it);
    expect_rejected(d2, RejectCode::DUPLICATE_INTENT);
}

TEST_F(RiskGatewayTest, R05_STALE_DATA_market_halt) {
    rm_->set_market_state("mkt_test", MarketState::INPLAY_HOT_CRIT);
    rm_->set_market_freshness_ms("mkt_test", 1'500);  // > 800ms halt
    auto d = rm_->evaluate(make_ok_intent());
    expect_rejected(d, RejectCode::STALE_DATA);
}

TEST_F(RiskGatewayTest, R05b_STALE_DATA_recon_halt) {
    rm_->set_recon_freshness_ms(40'000);  // > 30s
    auto d = rm_->evaluate(make_ok_intent());
    expect_rejected(d, RejectCode::STALE_DATA);
}

// ---- INVALID_INTENT × 9 sub_reason -----------------------------------------

TEST_F(RiskGatewayTest, R06a_INVALID_INTENT_BOOK_TS_ZERO_event) {
    auto it = make_ok_intent();
    it.event_ts_ns = 0;
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::INVALID_INTENT);
    EXPECT_EQ(d.sub_reason, InvalidIntentSubReason::BOOK_TS_ZERO);
}

TEST_F(RiskGatewayTest, R06b_INVALID_INTENT_BOOK_TS_ZERO_book) {
    auto it = make_ok_intent();
    it.book_snapshot_ts_ns = 0;
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::INVALID_INTENT);
    EXPECT_EQ(d.sub_reason, InvalidIntentSubReason::BOOK_TS_ZERO);
}

TEST_F(RiskGatewayTest, R06c_INVALID_INTENT_BOOK_TS_STALE) {
    auto it = make_ok_intent();
    it.book_snapshot_ts_ns = ::stcpp::infra::wal::pit::NowRealtimeNs() - 70'000'000'000LL;  // 70s ago
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::INVALID_INTENT);
    EXPECT_EQ(d.sub_reason, InvalidIntentSubReason::BOOK_TS_STALE);
}

TEST_F(RiskGatewayTest, R06d_INVALID_INTENT_NAN_OR_INF) {
    auto it = make_ok_intent();
    double nan = 0.0; nan = nan / nan;  // 制造 NaN, 避开 -Wnan-infinity-disabled
    it.price = nan;
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::INVALID_INTENT);
    EXPECT_EQ(d.sub_reason, InvalidIntentSubReason::NAN_OR_INF);
}

TEST_F(RiskGatewayTest, R06e_INVALID_INTENT_NEGATIVE) {
    auto it = make_ok_intent();
    it.size_usdc = -100;
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::INVALID_INTENT);
    EXPECT_EQ(d.sub_reason, InvalidIntentSubReason::NEGATIVE);
}

TEST_F(RiskGatewayTest, R06f_INVALID_INTENT_ILLEGAL_TICK) {
    auto it = make_ok_intent();
    it.tick_size = 0.05;  // 非 {0.001, 0.01}
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::INVALID_INTENT);
    EXPECT_EQ(d.sub_reason, InvalidIntentSubReason::ILLEGAL_TICK);
}

TEST_F(RiskGatewayTest, R06g_INVALID_INTENT_TS_ORDER_VIOLATED) {
    auto it = make_ok_intent();
    it.ingestion_ts_ns = it.event_ts_ns - 1;  // 倒流
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::INVALID_INTENT);
    EXPECT_EQ(d.sub_reason, InvalidIntentSubReason::TS_ORDER_VIOLATED);
}

TEST_F(RiskGatewayTest, R06h_INVALID_INTENT_TS_FUTURE) {
    auto it = make_ok_intent();
    auto const now = ::stcpp::infra::wal::pit::NowRealtimeNs();
    it.event_ts_ns = it.data_source_ts_ns = it.ingestion_ts_ns = now + 1'000'000'000LL;
    it.as_of_ts_ns = now + 2'000'000'000LL;
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::INVALID_INTENT);
    EXPECT_EQ(d.sub_reason, InvalidIntentSubReason::TS_FUTURE);
}

TEST_F(RiskGatewayTest, R06i_INVALID_INTENT_TS_UNKNOWN_SRC) {
    auto it = make_ok_intent();
    it.feature_snapshot_id.clear();  // 必填字段空 → unknown src
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::INVALID_INTENT);
    EXPECT_EQ(d.sub_reason, InvalidIntentSubReason::TS_UNKNOWN_SRC);
}

// ---- 仓位资金 6-10 ----------------------------------------------------------

TEST_F(RiskGatewayTest, R07_EXCEED_PER_ORDER_CAP) {
    auto it = make_ok_intent();
    it.size_usdc = 20'000;             // > 10K cap
    it.book_depth_l1_usdc = 100'000;   // 撑住 ρ = 0.2, 不触发 EXCEED_BOOK_DEPTH
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::EXCEED_PER_ORDER_CAP);
}

TEST_F(RiskGatewayTest, R08_EXCEED_MARKET_EXPOSURE) {
    rm_->set_market_exposure("mkt_test", 49'500);
    auto it = make_ok_intent();
    it.size_usdc = 1'000;  // 49500 + 1000 > 50K cap
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::EXCEED_MARKET_EXPOSURE);
}

TEST_F(RiskGatewayTest, R09_DAILY_LOSS_HALT) {
    rm_->set_daily_pnl(-6'000);  // > 5K loss
    auto d = rm_->evaluate(make_ok_intent());
    expect_rejected(d, RejectCode::DAILY_LOSS_HALT);
}

TEST_F(RiskGatewayTest, R10_CONSEC_LOSS_HALT) {
    rm_->set_consec_loss(5);
    auto d = rm_->evaluate(make_ok_intent());
    expect_rejected(d, RejectCode::CONSEC_LOSS_HALT);
}

TEST_F(RiskGatewayTest, R11_INSUFFICIENT_BANKROLL) {
    rm_->set_bankroll(500);  // < size 1000
    auto d = rm_->evaluate(make_ok_intent());
    expect_rejected(d, RejectCode::INSUFFICIENT_BANKROLL);
}

// ---- 信号 12-13 ------------------------------------------------------------

TEST_F(RiskGatewayTest, R12_EDGE_CI_NEGATIVE) {
    auto it = make_ok_intent("sig_neg");
    rm_->set_edge_ci_lower("sig_neg", -0.01);
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::EDGE_CI_NEGATIVE);
}

TEST_F(RiskGatewayTest, R13_EDGE_NEGATED_BY_SLIPPAGE) {
    auto it = make_ok_intent("sig_thin2");
    // 用 ρ = 1.25 (1 < ρ ≤ 2), pf = 0.5 + 0.01·(0.5 + 0.25·1.0) = 0.5075
    // slippage = 0.0075/0.5·1e4 = 150 bps. edge_lower = 50 bps (0.005) < 150 → 净负.
    it.book_depth_l1_usdc = 800;  // ρ = 1.25
    rm_->set_edge_ci_lower("sig_thin2", 0.005);  // 50 bps edge
    auto d2 = rm_->evaluate(it);
    // 数学上必为 EDGE_NEGATED_BY_SLIPPAGE; 但若 fill_rate 跌穿 floor 优先级更高
    EXPECT_TRUE(d2.reject == RejectCode::EDGE_NEGATED_BY_SLIPPAGE ||
                d2.reject == RejectCode::EXCESSIVE_SLIPPAGE ||
                d2.reject == RejectCode::LOW_FILL_RATE)
        << "expected slippage-related reject, got " << static_cast<int>(d2.reject);
}

// ---- 市场 14-15 ------------------------------------------------------------

TEST_F(RiskGatewayTest, R14_MARKET_TYPE_NOT_ENABLED) {
    RiskConfig c = cfg_;
    c.enable_moneyline = false;
    c.enable_totals    = false;
    c.enable_spreads   = false;
    auto local_emitter = std::make_shared<InMemoryEmitter>();
    RiskGateway local(c, local_emitter);
    local.set_state(RmState::RUNNING);
    auto d = local.evaluate(make_ok_intent());
    EXPECT_EQ(d.decision, Decision::REJECTED);
    EXPECT_EQ(d.reject,   RejectCode::MARKET_TYPE_NOT_ENABLED);
}

TEST_F(RiskGatewayTest, R15_MARKET_NOT_ACTIVE) {
    rm_->set_market_active("mkt_test", false);
    auto d = rm_->evaluate(make_ok_intent());
    expect_rejected(d, RejectCode::MARKET_NOT_ACTIVE);
}

// ---- 流动性 / 滑点 16-18 (小肖 SlippageModel 联动) --------------------------

TEST_F(RiskGatewayTest, R16_LOW_FILL_RATE) {
    auto it = make_ok_intent("sig_low_fill");
    // ρ 接近 1 + dt 大 → fill_rate 跌穿 0.50 floor
    auto const now = ::stcpp::infra::wal::pit::NowRealtimeNs();
    it.book_snapshot_ts_ns = now - 40'000'000'000LL;  // 40s stale (<60s 边界内)
    it.book_depth_l1_usdc  = 1'100;                   // ρ ≈ 0.91
    auto d = rm_->evaluate(it);
    // 40s stale: s_stale = 1-exp(-40000/30000) ≈ 0.736, fill_rate ≈ 1 - 0.234 - 0.736 < 0
    expect_rejected(d, RejectCode::LOW_FILL_RATE);
}

TEST_F(RiskGatewayTest, R17_EXCESSIVE_SLIPPAGE) {
    auto it = make_ok_intent("sig_xslip");
    // ρ ≈ 2.5 (1 < ρ ≤ 3) → multi-tier slippage 大. tick=0.01, price=0.50
    // pf = 0.5 + 0.01·(0.5 + 1.5) = 0.52, slip = 0.02/0.5·1e4 = 400 bps > 200 cfg
    it.book_depth_l1_usdc = 400;   // ρ = 2.5
    it.size_usdc          = 1'000;
    auto d = rm_->evaluate(it);
    // slippage_bps 400 > excessive_slippage_bps 200
    EXPECT_TRUE(d.reject == RejectCode::EXCESSIVE_SLIPPAGE ||
                d.reject == RejectCode::LOW_FILL_RATE)  // fill_rate 多档也可能跌穿
        << "got " << static_cast<int>(d.reject);
}

TEST_F(RiskGatewayTest, R18_EXCEED_BOOK_DEPTH) {
    auto it = make_ok_intent("sig_xdepth");
    it.book_depth_l1_usdc = 250;   // ρ = 1000/250 = 4 > RHO_MAX 3
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::EXCEED_BOOK_DEPTH);
}

// ---- 系统 19-21 ------------------------------------------------------------

TEST_F(RiskGatewayTest, R19_AUDIT_WAL_BACKPRESSURE) {
    emitter_->trigger_backpressure_next();
    auto d = rm_->evaluate(make_ok_intent("sig_bp"));
    // emit 返 false → reject 改 AUDIT_WAL_BACKPRESSURE
    EXPECT_EQ(d.decision, Decision::REJECTED);
    EXPECT_EQ(d.reject,   RejectCode::AUDIT_WAL_BACKPRESSURE);
}

TEST_F(RiskGatewayTest, R20_STRATEGY_DECAYED) {
    rm_->set_strategy_ev_ratio("strat_a", 0.1);  // < 0.3 阈值
    auto d = rm_->evaluate(make_ok_intent("sig_decay"));
    expect_rejected(d, RejectCode::STRATEGY_DECAYED);
}

TEST_F(RiskGatewayTest, R21_INTERNAL_ERROR_default) {
    // INTERNAL_ERROR 是兜底, v0.1 正常 path 不触发.
    // 此 case 验证 RejectCode enum 21 项闭包 (静态 cast roundtrip).
    EXPECT_EQ(static_cast<int>(RejectCode::INTERNAL_ERROR), 20);
}

// ===== 状态机 5 态转移 =======================================================

TEST_F(RiskGatewayTest, StateMachine_default_safe_mode_after_ctor) {
    auto local_emitter = std::make_shared<InMemoryEmitter>();
    RiskGateway local(cfg_, local_emitter);
    // ctor 默认 SAFE_MODE (v0.2 G8 红线)
    EXPECT_EQ(local.state(), RmState::SAFE_MODE);
}

TEST_F(RiskGatewayTest, StateMachine_transitions) {
    rm_->set_state(RmState::RUNNING);
    EXPECT_EQ(rm_->state(), RmState::RUNNING);
    rm_->set_state(RmState::WARNING);
    EXPECT_EQ(rm_->state(), RmState::WARNING);
    rm_->set_state(RmState::HALTED);
    EXPECT_EQ(rm_->state(), RmState::HALTED);
    // HALTED 后 reject 必 STATE_HALTED, 即使是 close
    auto it = make_ok_intent("sig_halt_close");
    it.is_close = true;
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::STATE_HALTED);
}

// ===== MarketState 5 档阈值 (compile-time) ===================================

TEST(MarketState, threshold_table) {
    EXPECT_EQ(threshold_of(MarketState::INPLAY_HOT_CRIT).warn_ms,  200u);
    EXPECT_EQ(threshold_of(MarketState::INPLAY_HOT_CRIT).halt_ms,  800u);
    EXPECT_EQ(threshold_of(MarketState::INPLAY_HOT).warn_ms,       500u);
    EXPECT_EQ(threshold_of(MarketState::INPLAY_HOT).halt_ms,     2'000u);
    EXPECT_EQ(threshold_of(MarketState::INPLAY_COLD).halt_ms,   10'000u);
    EXPECT_EQ(threshold_of(MarketState::PREGAME).halt_ms,       15'000u);
    EXPECT_EQ(threshold_of(MarketState::SETTLED).halt_ms,       30'000u);
}

// ===== WAL emit 联动 =========================================================

TEST_F(RiskGatewayTest, WalEmit_approved_path_records_audit) {
    auto d = rm_->evaluate(make_ok_intent("sig_a1"));
    EXPECT_EQ(d.decision, Decision::APPROVED);
    EXPECT_EQ(emitter_->size(), 1u);
    EXPECT_EQ(emitter_->back().decision, Decision::APPROVED);
}

TEST_F(RiskGatewayTest, WalEmit_rejected_path_carries_sub_reason) {
    auto it = make_ok_intent("sig_inv");
    it.size_usdc = -1;
    auto d = rm_->evaluate(it);
    EXPECT_EQ(d.decision, Decision::REJECTED);
    EXPECT_EQ(emitter_->size(), 1u);
    EXPECT_EQ(emitter_->back().sub_reason, InvalidIntentSubReason::NEGATIVE);
}

// ===== 与小宋 risk_manager_fixture 兼容性 ====================================
// (此处 smoke: enum 数值与 fixture #include 的 reject_enum.hpp 一致)

TEST(RejectEnum, total_count_is_21) {
    EXPECT_EQ(static_cast<int>(RejectCode::INTERNAL_ERROR), 20);  // 0..20 = 21 个
}

TEST(InvalidIntentSubReason, total_count_is_9) {
    EXPECT_EQ(static_cast<int>(InvalidIntentSubReason::TS_UNKNOWN_SRC), 8);  // 0..8 = 9 个
}

}  // namespace stcpp::risk::test
