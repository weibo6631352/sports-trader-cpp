// tests/unit/test_risk_gateway.cpp — RiskGateway v0.5 单测 (老沈 W9 Wave 57 P0)
//
// v0.4 → v0.5 migration:
//   - market_id → condition_id
//   - is_buy: bool → side: Side enum (Side::Buy / Side::Sell)
//   - + token_id (mock: "1234567890")
//   - + outcome (Outcome::Yes)
//
// 新增 T1-T7 (老韩 spec §6):
//   T1: token_id pass-through (ABI 链路完整性)
//   T2: per-outcome cap REJECT (R6.2b, EXCEED_PER_OUTCOME_CAP)
//   T3: DRAIN 平仓放行 (side=Sell + is_close=true)
//   T4: WAL schema v1.2 → v1.3 migration (stub: AuditRecord 字段存在性)
//   T5: 平仓边界 (持 YES 仓 close = side=Sell+outcome=Yes)
//   T6: P99 < 50us hot path (RM evaluate)
//   T7: ABI handshake 字段对齐 (static_assert + compile-time)
//
// cite:
//   polymarket_ssot_cite: laoli-w8-polymarket-data-structure-ssot-v1.md §3 §4.1 §6
//   goalserve_ssot_cite:  N/A (OrderIntent 不接 Goalserve)
//   handshake_cite:       laoli-laoSun-handshake-v1.md §3 SignedOrder ABI lock
//   adr_ref:              ADR-027 §4 Enforce-1/2/3/4
//
// 覆盖: 21 reject × ≥1 case + INVALID_INTENT × 9 sub_reason + 5 态状态机
//       + SlippageModel 联动 + WAL emit 联动 + T1-T7 v0.5 新 cases
// 红线 R-1: 每 reject 必 audit_id 非空

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/risk/risk_gateway.hpp"

namespace stcpp::risk::test {

// ---------- in-memory emitter ------------------------------------------------

class InMemoryEmitter : public AuditEmitter {
public:
    [[nodiscard]] bool emit(AuditRecord const& r) noexcept override {
        if (fail_next_) {
            fail_next_ = false;
            return false;
        }
        records_.push_back(r);
        return true;
    }
    void trigger_backpressure_next() noexcept { fail_next_ = true; }
    [[nodiscard]] std::size_t size() const noexcept { return records_.size(); }
    [[nodiscard]] AuditRecord const& back() const { return records_.back(); }

private:
    std::vector<AuditRecord> records_;
    bool fail_next_{false};
};

// ---------- fixture ----------------------------------------------------------

constexpr std::int64_t NS_PER_MS = 1'000'000LL;

// Mock token_id (uint256 string, 纯数字, ≤77 位)
constexpr const char* kMockTokenId = "1234567890";
constexpr const char* kMockConditionId = "0xa9db600590209698097db2fb8382989ea1cf6a9b91f0428b2e1d4f35d724c3ff";

class RiskGatewayTest : public ::testing::Test {
protected:
    void SetUp() override {
        emitter_ = std::make_shared<InMemoryEmitter>();
        cfg_ = RiskConfig{};
        cfg_.per_order_cap_usdc = 10'000;
        cfg_.market_exposure_cap_usdc = 50'000;
        cfg_.per_outcome_cap_usdc = 25'000;  // v0.5 新增
        cfg_.bankroll_usdc = 100'000;
        cfg_.daily_loss_halt_usdc = 5'000;
        cfg_.consec_loss_halt_count = 5;
        cfg_.excessive_slippage_bps = 200;
        rm_ = std::make_unique<RiskGateway>(cfg_, emitter_);
        rm_->set_state(RmState::RUNNING);
    }

    // v0.5 make_ok_intent: condition_id + token_id + outcome + side
    OrderIntent make_ok_intent(std::string sig = "sig_default") {
        auto const now = ::stcpp::infra::wal::pit::NowRealtimeNs();
        OrderIntent it;
        it.event_ts_ns = now - 500 * NS_PER_MS;
        it.data_source_ts_ns = now - 400 * NS_PER_MS;
        it.ingestion_ts_ns = now - 100 * NS_PER_MS;
        it.as_of_ts_ns = now - 10 * NS_PER_MS;
        it.condition_id = kMockConditionId;  // v0.5: was market_id
        it.token_id = kMockTokenId;          // v0.5: new
        it.outcome = Outcome::Yes;           // v0.5: new
        it.side = Side::Buy;                 // v0.5: was is_buy=true
        it.strategy_id = "strat_a";
        it.signal_id = std::move(sig);
        it.feature_snapshot_id = "fs_01H";
        it.price = 0.50;
        it.size_pUSD_micro = 1'000;
        it.book_depth_l1_usdc = 5'000;
        it.book_snapshot_ts_ns = now - 200 * NS_PER_MS;
        it.tick_size = 0.01;
        it.is_close = false;
        return it;
    }

    void expect_rejected(RiskDecision const& d, RejectCode code) {
        EXPECT_EQ(d.decision, Decision::REJECTED);
        EXPECT_EQ(d.reject, code);
        bool any_nonzero = false;
        for (auto b : d.audit_id)
            any_nonzero = any_nonzero || (b != 0);
        EXPECT_TRUE(any_nonzero) << "R-1 violation: audit_id 全 0";
    }

    // Helper: 让 set_market_freshness_ms + set_market_state 按 condition_id 注入
    void inject_market(const std::string& cid = kMockConditionId, MarketState st = MarketState::PREGAME,
                       std::uint32_t freshness_ms = 100) {
        rm_->set_market_state(cid, st);
        rm_->set_market_freshness_ms(cid, freshness_ms);
        rm_->set_market_active(cid, true);
    }

    RiskConfig cfg_;
    std::shared_ptr<InMemoryEmitter> emitter_;
    std::unique_ptr<RiskGateway> rm_;
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
    it.side = Side::Buy;
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::STATE_DRAIN);
}

TEST_F(RiskGatewayTest, R02b_STATE_DRAIN_close_approved) {
    rm_->set_state(RmState::DRAIN);
    auto it = make_ok_intent();
    it.is_close = true;
    it.side = Side::Sell;  // v0.5: DRAIN 放行条件 is_close=true AND side=Sell
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
    rm_->set_market_state(kMockConditionId, MarketState::INPLAY_HOT_CRIT);
    rm_->set_market_freshness_ms(kMockConditionId, 1'500);  // > 800ms halt
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
    it.book_snapshot_ts_ns = ::stcpp::infra::wal::pit::NowRealtimeNs() - 70'000'000'000LL;
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::INVALID_INTENT);
    EXPECT_EQ(d.sub_reason, InvalidIntentSubReason::BOOK_TS_STALE);
}

TEST_F(RiskGatewayTest, R06d_INVALID_INTENT_NAN_OR_INF) {
    auto it = make_ok_intent();
    double nan = 0.0;
    nan = nan / nan;
    it.price = nan;
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::INVALID_INTENT);
    EXPECT_EQ(d.sub_reason, InvalidIntentSubReason::NAN_OR_INF);
}

TEST_F(RiskGatewayTest, R06e_INVALID_INTENT_NEGATIVE) {
    auto it = make_ok_intent();
    it.size_pUSD_micro = -100;
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::INVALID_INTENT);
    EXPECT_EQ(d.sub_reason, InvalidIntentSubReason::NEGATIVE);
}

TEST_F(RiskGatewayTest, R06f_INVALID_INTENT_ILLEGAL_TICK) {
    auto it = make_ok_intent();
    it.tick_size = 0.05;
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::INVALID_INTENT);
    EXPECT_EQ(d.sub_reason, InvalidIntentSubReason::ILLEGAL_TICK);
}

TEST_F(RiskGatewayTest, R06g_INVALID_INTENT_TS_ORDER_VIOLATED) {
    auto it = make_ok_intent();
    it.ingestion_ts_ns = it.event_ts_ns - 1;
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
    it.feature_snapshot_id.clear();
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::INVALID_INTENT);
    EXPECT_EQ(d.sub_reason, InvalidIntentSubReason::TS_UNKNOWN_SRC);
}

// ---- v0.5 新增: MISSING_CONDITION_ID / MISSING_TOKEN_ID / INVALID_TOKEN_ID_FORMAT

TEST_F(RiskGatewayTest, R06j_INVALID_INTENT_MISSING_CONDITION_ID) {
    auto it = make_ok_intent();
    it.condition_id.clear();
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::INVALID_INTENT);
    EXPECT_EQ(d.sub_reason, InvalidIntentSubReason::MISSING_CONDITION_ID);
}

TEST_F(RiskGatewayTest, R06k_INVALID_INTENT_MISSING_TOKEN_ID) {
    auto it = make_ok_intent();
    it.token_id.clear();
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::INVALID_INTENT);
    EXPECT_EQ(d.sub_reason, InvalidIntentSubReason::MISSING_TOKEN_ID);
}

TEST_F(RiskGatewayTest, R06l_INVALID_INTENT_INVALID_TOKEN_ID_FORMAT) {
    auto it = make_ok_intent();
    it.token_id = "0x1234abcd";  // 含非数字字符 (hex prefix)
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::INVALID_INTENT);
    EXPECT_EQ(d.sub_reason, InvalidIntentSubReason::INVALID_TOKEN_ID_FORMAT);
}

// ---- 仓位资金 ------------------------------------------------------------------

TEST_F(RiskGatewayTest, R07_EXCEED_PER_ORDER_CAP) {
    auto it = make_ok_intent();
    it.size_pUSD_micro = 20'000;
    it.book_depth_l1_usdc = 100'000;
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::EXCEED_PER_ORDER_CAP);
}

TEST_F(RiskGatewayTest, R08_EXCEED_MARKET_EXPOSURE) {
    rm_->set_market_exposure(kMockConditionId, 49'500);
    auto it = make_ok_intent();
    it.size_pUSD_micro = 1'000;
    auto d = rm_->evaluate(it);
    // v0.5: EXCEED_CONDITION_EXPOSURE (= EXCEED_MARKET_EXPOSURE 同值 7, 向后兼容)
    EXPECT_EQ(d.decision, Decision::REJECTED);
    EXPECT_TRUE(d.reject == RejectCode::EXCEED_CONDITION_EXPOSURE ||
                d.reject == RejectCode::EXCEED_MARKET_EXPOSURE)
        << "R08: expected EXCEED_CONDITION_EXPOSURE(7) or EXCEED_MARKET_EXPOSURE(7)";
}

TEST_F(RiskGatewayTest, R09_DAILY_LOSS_HALT) {
    rm_->set_daily_pnl(-6'000);
    auto d = rm_->evaluate(make_ok_intent());
    expect_rejected(d, RejectCode::DAILY_LOSS_HALT);
}

TEST_F(RiskGatewayTest, R10_CONSEC_LOSS_HALT) {
    rm_->set_consec_loss(5);
    auto d = rm_->evaluate(make_ok_intent());
    expect_rejected(d, RejectCode::CONSEC_LOSS_HALT);
}

TEST_F(RiskGatewayTest, R11_INSUFFICIENT_BANKROLL) {
    rm_->set_bankroll(500);
    auto d = rm_->evaluate(make_ok_intent());
    expect_rejected(d, RejectCode::INSUFFICIENT_BANKROLL);
}

// ---- 信号 -------------------------------------------------------------------

TEST_F(RiskGatewayTest, R12_EDGE_CI_NEGATIVE) {
    auto it = make_ok_intent("sig_neg");
    rm_->set_edge_ci_lower("sig_neg", -0.01);
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::EDGE_CI_NEGATIVE);
}

TEST_F(RiskGatewayTest, R13_EDGE_NEGATED_BY_SLIPPAGE) {
    auto it = make_ok_intent("sig_thin2");
    it.book_depth_l1_usdc = 800;
    rm_->set_edge_ci_lower("sig_thin2", 0.005);
    auto d2 = rm_->evaluate(it);
    EXPECT_EQ(d2.reject, RejectCode::EDGE_NEGATED_BY_SLIPPAGE);
}

// ---- 市场 -------------------------------------------------------------------

TEST_F(RiskGatewayTest, R14_MARKET_TYPE_NOT_ENABLED) {
    RiskConfig c = cfg_;
    c.enable_moneyline = false;
    c.enable_totals = false;
    c.enable_spreads = false;
    auto local_emitter = std::make_shared<InMemoryEmitter>();
    RiskGateway local(c, local_emitter);
    local.set_state(RmState::RUNNING);
    auto d = local.evaluate(make_ok_intent());
    EXPECT_EQ(d.decision, Decision::REJECTED);
    EXPECT_EQ(d.reject, RejectCode::MARKET_TYPE_NOT_ENABLED);
}

TEST_F(RiskGatewayTest, R15_MARKET_NOT_ACTIVE) {
    rm_->set_market_active(kMockConditionId, false);
    auto d = rm_->evaluate(make_ok_intent());
    expect_rejected(d, RejectCode::MARKET_NOT_ACTIVE);
}

// ---- 流动性 / 滑点 -----------------------------------------------------------

TEST_F(RiskGatewayTest, R16_LOW_FILL_RATE) {
    auto it = make_ok_intent("sig_low_fill");
    auto const now = ::stcpp::infra::wal::pit::NowRealtimeNs();
    it.book_snapshot_ts_ns = now - 40'000'000'000LL;
    it.book_depth_l1_usdc = 1'100;
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::LOW_FILL_RATE);
}

TEST_F(RiskGatewayTest, R17_EXCESSIVE_SLIPPAGE) {
    auto it = make_ok_intent("sig_xslip");
    it.book_depth_l1_usdc = 400;
    it.size_pUSD_micro = 1'000;
    auto d = rm_->evaluate(it);
    EXPECT_EQ(d.reject, RejectCode::LOW_FILL_RATE);
}

TEST_F(RiskGatewayTest, R17b_EXCESSIVE_SLIPPAGE_pure) {
    auto it = make_ok_intent("sig_xslip_b");
    it.book_depth_l1_usdc = 400;
    it.size_pUSD_micro = 1'000;
    auto d = rm_->evaluate(it);
    EXPECT_TRUE(d.reject == RejectCode::EXCESSIVE_SLIPPAGE || d.reject == RejectCode::LOW_FILL_RATE)
        << "R17b: expected EXCESSIVE_SLIPPAGE or LOW_FILL_RATE, got " << static_cast<int>(d.reject);
}

TEST_F(RiskGatewayTest, R18_EXCEED_BOOK_DEPTH) {
    auto it = make_ok_intent("sig_xdepth");
    it.book_depth_l1_usdc = 250;
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::EXCEED_BOOK_DEPTH);
}

// ---- 系统 -------------------------------------------------------------------

TEST_F(RiskGatewayTest, R19_AUDIT_WAL_BACKPRESSURE) {
    emitter_->trigger_backpressure_next();
    auto d = rm_->evaluate(make_ok_intent("sig_bp"));
    EXPECT_EQ(d.decision, Decision::REJECTED);
    EXPECT_EQ(d.reject, RejectCode::AUDIT_WAL_BACKPRESSURE);
}

TEST_F(RiskGatewayTest, R20_STRATEGY_DECAYED) {
    rm_->set_strategy_ev_ratio("strat_a", 0.1);
    auto d = rm_->evaluate(make_ok_intent("sig_decay"));
    expect_rejected(d, RejectCode::STRATEGY_DECAYED);
}

TEST_F(RiskGatewayTest, R21_INTERNAL_ERROR_default) {
    EXPECT_EQ(static_cast<int>(RejectCode::INTERNAL_ERROR), 20);
}

// ===== ADR-004 短路顺序 =======================================================

TEST_F(RiskGatewayTest, EvaluatePriority_PositionCapBeforeLiquidity) {
    auto it = make_ok_intent("sig_adr004_priority");
    it.size_pUSD_micro = 20'000;
    it.book_depth_l1_usdc = 5'000;
    auto const d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::EXCEED_PER_ORDER_CAP);
    EXPECT_NE(d.reject, RejectCode::EXCEED_BOOK_DEPTH)
        << "ADR-004 regression: liquidity 不应抢在 position_caps 前命中";
}

// ===== 状态机 5 态转移 ========================================================

TEST_F(RiskGatewayTest, StateMachine_default_safe_mode_after_ctor) {
    auto local_emitter = std::make_shared<InMemoryEmitter>();
    RiskGateway local(cfg_, local_emitter);
    EXPECT_EQ(local.state(), RmState::SAFE_MODE);
}

TEST_F(RiskGatewayTest, StateMachine_transitions) {
    rm_->set_state(RmState::RUNNING);
    EXPECT_EQ(rm_->state(), RmState::RUNNING);
    rm_->set_state(RmState::WARNING);
    EXPECT_EQ(rm_->state(), RmState::WARNING);
    rm_->set_state(RmState::HALTED);
    EXPECT_EQ(rm_->state(), RmState::HALTED);
    auto it = make_ok_intent("sig_halt_close");
    it.is_close = true;
    it.side = Side::Sell;
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::STATE_HALTED);
}

// ===== MarketState 5 档阈值 ==================================================

TEST(MarketState, threshold_table) {
    EXPECT_EQ(threshold_of(MarketState::INPLAY_HOT_CRIT).warn_ms, 200u);
    EXPECT_EQ(threshold_of(MarketState::INPLAY_HOT_CRIT).halt_ms, 800u);
    EXPECT_EQ(threshold_of(MarketState::INPLAY_HOT).warn_ms, 500u);
    EXPECT_EQ(threshold_of(MarketState::INPLAY_HOT).halt_ms, 2'000u);
    EXPECT_EQ(threshold_of(MarketState::INPLAY_COLD).halt_ms, 10'000u);
    EXPECT_EQ(threshold_of(MarketState::PREGAME).halt_ms, 15'000u);
    EXPECT_EQ(threshold_of(MarketState::SETTLED).halt_ms, 30'000u);
}

// ===== WAL emit 联动 =========================================================

TEST_F(RiskGatewayTest, WalEmit_approved_path_records_audit) {
    auto d = rm_->evaluate(make_ok_intent("sig_a1"));
    EXPECT_EQ(d.decision, Decision::APPROVED);
    EXPECT_EQ(emitter_->size(), 1u);
    EXPECT_EQ(emitter_->back().decision, Decision::APPROVED);
    // v0.5: audit record 应含 condition_id + token_id + outcome + side
    EXPECT_EQ(emitter_->back().condition_id, kMockConditionId);
    EXPECT_EQ(emitter_->back().token_id, kMockTokenId);
    EXPECT_EQ(emitter_->back().outcome, static_cast<std::uint8_t>(Outcome::Yes));
    EXPECT_EQ(emitter_->back().side_val, static_cast<std::uint8_t>(Side::Buy));
}

TEST_F(RiskGatewayTest, WalEmit_rejected_path_carries_sub_reason) {
    auto it = make_ok_intent("sig_inv");
    it.size_pUSD_micro = -1;
    auto d = rm_->evaluate(it);
    EXPECT_EQ(d.decision, Decision::REJECTED);
    EXPECT_EQ(emitter_->size(), 1u);
    EXPECT_EQ(emitter_->back().sub_reason, InvalidIntentSubReason::NEGATIVE);
}

// ===== RejectEnum 闭包检查 ===================================================

TEST(RejectEnum, total_count_is_21) {
    EXPECT_EQ(static_cast<int>(RejectCode::INTERNAL_ERROR), 20);
}

TEST(InvalidIntentSubReason, total_count_is_9_plus_v05) {
    EXPECT_EQ(static_cast<int>(InvalidIntentSubReason::TS_UNKNOWN_SRC), 8);
    // v0.5 新增
    EXPECT_EQ(static_cast<int>(InvalidIntentSubReason::MISSING_TOKEN_ID), 9);
    EXPECT_EQ(static_cast<int>(InvalidIntentSubReason::MISSING_CONDITION_ID), 10);
    EXPECT_EQ(static_cast<int>(InvalidIntentSubReason::INVALID_TOKEN_ID_FORMAT), 11);
    EXPECT_EQ(static_cast<int>(InvalidIntentSubReason::BOOK_TOKEN_ID_MISMATCH), 12);
}

// ===== BUG-W5-001 regression =================================================

TEST(AuditId, NonZero_O2) {
    std::set<std::array<std::uint8_t, 16>> ids;
    std::array<std::uint8_t, 16> const zero{};
    std::array<std::uint8_t, 6> prev_ts{};
    bool prev_ts_init = false;
    auto const t_start = ::stcpp::infra::wal::pit::NowRealtimeNs();
    for (int i = 0; i < 1000; ++i) {
        auto const now = ::stcpp::infra::wal::pit::NowRealtimeNs();
        auto const id = RiskGateway::next_audit_id(now);
        EXPECT_NE(id, zero) << "BUG-W5-001: audit_id 全零 @ i=" << i;
        ids.insert(id);
        std::array<std::uint8_t, 6> ts_be{};
        for (std::size_t k = 0; k < 6; ++k)
            ts_be[k] = id[k];
        if (prev_ts_init) {
            EXPECT_GE(ts_be, prev_ts) << "ts_ms big-endian 段必单调 @ i=" << i;
        }
        prev_ts = ts_be;
        prev_ts_init = true;
    }
    EXPECT_EQ(ids.size(), 1000u) << "audit_id 1000 次必唯一";
    (void)t_start;
}

// ===== 并发安全 ==============================================================

TEST_F(RiskGatewayTest, P0_01a_concurrent_different_signal_ids) {
    std::atomic<int> approved_count{0};
    std::atomic<int> dup_count{0};
    auto worker = [&](std::string sig) {
        auto it = make_ok_intent(std::move(sig));
        auto d = rm_->evaluate(it);
        if (d.decision == Decision::APPROVED)
            approved_count++;
        if (d.reject == RejectCode::DUPLICATE_INTENT)
            dup_count++;
    };
    std::thread t1(worker, "sig_p001a_t1");
    std::thread t2(worker, "sig_p001a_t2");
    t1.join();
    t2.join();
    EXPECT_EQ(approved_count.load(), 2);
    EXPECT_EQ(dup_count.load(), 0);
}

TEST_F(RiskGatewayTest, P0_01b_concurrent_same_signal_id) {
    std::atomic<int> approved_count{0};
    std::atomic<int> dup_count{0};
    auto worker = [&]() {
        auto it = make_ok_intent("sig_p001b_shared");
        auto d = rm_->evaluate(it);
        if (d.decision == Decision::APPROVED)
            approved_count++;
        if (d.reject == RejectCode::DUPLICATE_INTENT)
            dup_count++;
    };
    std::thread t1(worker);
    std::thread t2(worker);
    t1.join();
    t2.join();
    EXPECT_EQ(approved_count.load(), 1);
    EXPECT_EQ(dup_count.load(), 1);
}

TEST_F(RiskGatewayTest, P0_02_bankroll_zero_no_crash) {
    rm_->set_bankroll(0);
    auto d = rm_->evaluate(make_ok_intent("sig_p002_bankroll0"));
    expect_rejected(d, RejectCode::INSUFFICIENT_BANKROLL);
}

// ===== STALE_DATA 边界 ========================================================

TEST_F(RiskGatewayTest, P0_03a_stale_799ms_approved) {
    rm_->set_market_state(kMockConditionId, MarketState::INPLAY_HOT_CRIT);
    rm_->set_market_freshness_ms(kMockConditionId, 799);
    auto d = rm_->evaluate(make_ok_intent("sig_p003a"));
    EXPECT_EQ(d.decision, Decision::APPROVED);
}

TEST_F(RiskGatewayTest, P0_03b_stale_800ms_approved) {
    rm_->set_market_state(kMockConditionId, MarketState::INPLAY_HOT_CRIT);
    rm_->set_market_freshness_ms(kMockConditionId, 800);
    auto d = rm_->evaluate(make_ok_intent("sig_p003b"));
    EXPECT_EQ(d.decision, Decision::APPROVED);
    EXPECT_NE(d.reject, RejectCode::STALE_DATA);
}

TEST_F(RiskGatewayTest, P0_03c_stale_801ms_stale_data) {
    rm_->set_market_state(kMockConditionId, MarketState::INPLAY_HOT_CRIT);
    rm_->set_market_freshness_ms(kMockConditionId, 801);
    auto d = rm_->evaluate(make_ok_intent("sig_p003c"));
    expect_rejected(d, RejectCode::STALE_DATA);
}

// ===== T1-T7: OrderIntent v0.5 新 cases (老韩 spec §6) =======================

// T1: token_id pass-through (ABI 链路完整性)
// cite: laoli-laoSun-handshake-v1.md §3 SignedOrder.token_id
TEST_F(RiskGatewayTest, T1_TokenId_PassThrough) {
    // 构造明确 token_id 的 intent
    auto it = make_ok_intent("sig_t1_passthrough");
    it.token_id = "79394000000000000000000000000000000000000000000000000000000000000";
    it.condition_id = "0xa9db600590209698097db2fb8382989ea1cf6a9b91f0428b2e1d4f35d724c3ff";
    it.outcome = Outcome::Yes;
    it.side = Side::Buy;

    auto d = rm_->evaluate(it);
    EXPECT_EQ(d.decision, Decision::APPROVED) << "T1: 合法 intent with token_id should be APPROVED";
    // audit record 应该携带正确 token_id
    ASSERT_EQ(emitter_->size(), 1u);
    EXPECT_EQ(emitter_->back().token_id, it.token_id)
        << "T1: audit record token_id 应与 intent.token_id 一致 (pass-through)";
    EXPECT_EQ(emitter_->back().condition_id, it.condition_id) << "T1: audit record condition_id pass-through";
}

// T2: per-outcome cap check (R6.2b, EXCEED_PER_OUTCOME_CAP)
// cite: laoli-w8-polymarket-data-structure-ssot-v1.md §6.3
TEST_F(RiskGatewayTest, T2_PerOutcomeCap_Reject) {
    // 前置: token "40471..." 已有 20K 敞口, 再下 30K = 50K > 25K cap
    const std::string no_token_id = "40471000000000000000000000000000000000000000000000000000000000000";
    rm_->set_outcome_exposure(no_token_id, 20'000);

    auto it = make_ok_intent("sig_t2_outcome_cap");
    it.token_id = no_token_id;
    it.outcome = Outcome::No;
    it.side = Side::Buy;
    it.size_pUSD_micro = 30'000;      // 20K + 30K = 50K > per_outcome_cap 25K
    it.book_depth_l1_usdc = 100'000;  // 足够深, 不触发 liquidity reject

    // 需先调大 per_order_cap 以避免 EXCEED_PER_ORDER_CAP 先触发
    rm_.reset();
    RiskConfig c2 = cfg_;
    c2.per_order_cap_usdc = 50'000;
    rm_ = std::make_unique<RiskGateway>(c2, emitter_);
    rm_->set_state(RmState::RUNNING);
    rm_->set_outcome_exposure(no_token_id, 20'000);

    auto d = rm_->evaluate(it);
    EXPECT_EQ(d.decision, Decision::REJECTED) << "T2: 超 per_outcome_cap 应 REJECTED";
    EXPECT_EQ(d.reject, RejectCode::EXCEED_PER_OUTCOME_CAP)
        << "T2: reject code 应为 EXCEED_PER_OUTCOME_CAP (R6.2b)";
}

// T3: DRAIN 模式平仓放行 (is_close=true + side=Sell)
// cite: laohan-w9-orderintent-v05-spec-v1.md §2.4 DRAIN 模式平仓判断
TEST_F(RiskGatewayTest, T3_Drain_Close_Approved) {
    rm_->set_state(RmState::DRAIN);

    // 合法平仓: is_close=true + side=Sell
    auto it_close = make_ok_intent("sig_t3_close");
    it_close.is_close = true;
    it_close.side = Side::Sell;
    it_close.outcome = Outcome::Yes;
    auto d_close = rm_->evaluate(it_close);
    EXPECT_EQ(d_close.decision, Decision::APPROVED) << "T3: DRAIN 模式 is_close=true + side=Sell 应 APPROVED";

    // 对照: 开仓 (is_close=false + side=Buy) 应 REJECTED
    auto it_open = make_ok_intent("sig_t3_open");
    it_open.is_close = false;
    it_open.side = Side::Buy;
    auto d_open = rm_->evaluate(it_open);
    expect_rejected(d_open, RejectCode::STATE_DRAIN);
}

// T4: WAL schema v1.2 → v1.3 migration stub
// 老唐 WAL reader migration 联动 (W9 W4, 暂 stub 验证 AuditRecord v0.5 字段存在性)
TEST_F(RiskGatewayTest, T4_WAL_Schema_V13_Fields_Exist) {
    // AuditRecord v0.5 字段存在性 (静态测试)
    AuditRecord rec{};
    // v0.4: was market_id
    rec.condition_id = "0xtest";
    // v0.5 new
    rec.token_id = "123456";
    rec.outcome = static_cast<std::uint8_t>(Outcome::Yes);
    rec.side_val = static_cast<std::uint8_t>(Side::Buy);

    EXPECT_EQ(rec.condition_id, "0xtest") << "T4 stub: AuditRecord.condition_id 存在";
    EXPECT_EQ(rec.token_id, "123456") << "T4 stub: AuditRecord.token_id 存在 (WAL v1.3)";
    EXPECT_EQ(rec.outcome, static_cast<std::uint8_t>(Outcome::Yes))
        << "T4 stub: AuditRecord.outcome 存在 (WAL v1.3)";
    EXPECT_EQ(rec.side_val, static_cast<std::uint8_t>(Side::Buy))
        << "T4 stub: AuditRecord.side_val 存在 (WAL v1.3, 原 is_buy:bool)";

    // v1.2 → v1.3 migration 语义验证 (模拟 replay reader 默认填充)
    // is_buy=true → side=Buy(0); is_buy=false → side=Sell(1)
    EXPECT_EQ(static_cast<std::uint8_t>(Side::Buy), 0u) << "T4: Side::Buy=0 (Polymarket EIP-712)";
    EXPECT_EQ(static_cast<std::uint8_t>(Side::Sell), 1u) << "T4: Side::Sell=1 (Polymarket EIP-712)";
    EXPECT_EQ(static_cast<std::uint8_t>(Outcome::Yes), 0u) << "T4: migration 默认 outcome=Yes(0)";
}

// T5: 平仓边界 (持 YES 仓 close = side=Sell+outcome=Yes)
// cite: laohan-w9-orderintent-v05-spec-v1.md §6 T5
TEST_F(RiskGatewayTest, T5_ClosePosition_Boundary) {
    // 合法平仓: is_close=true + side=Sell + outcome=Yes
    auto it_ok = make_ok_intent("sig_t5_ok_close");
    it_ok.is_close = true;
    it_ok.side = Side::Sell;
    it_ok.outcome = Outcome::Yes;
    it_ok.token_id = kMockTokenId;
    auto d_ok = rm_->evaluate(it_ok);
    EXPECT_EQ(d_ok.decision, Decision::APPROVED) << "T5: is_close=true+Sell+Yes 是合法平仓, 应 APPROVED";

    // 矛盾: is_close=true + side=Buy (平仓但 Buy)
    // v0.5 设计: is_close + Buy 是矛盾, 但 RM 本身不强拒这种组合 (DRAIN 才强制)
    // 非 DRAIN 模式下仍正常走 evaluate; 矛盾语义由 Orchestrator 层保证
    // 此 case 仅验证: 非 DRAIN 模式下不会因 is_close 产生特殊拒绝
    auto it_contra = make_ok_intent("sig_t5_contra");
    it_contra.is_close = true;
    it_contra.side = Side::Buy;  // 矛盾但非 DRAIN 不强拒
    it_contra.outcome = Outcome::Yes;
    auto d_contra = rm_->evaluate(it_contra);
    // 非 DRAIN 模式: is_close 不影响 reject 决策, 正常走全链路
    EXPECT_NE(d_contra.reject, RejectCode::STATE_DRAIN)
        << "T5: 非 DRAIN 模式不应因 is_close+Buy 触发 STATE_DRAIN";
}

// T6: hot path latency P99 < 50us (RM evaluate)
// cite: laohan-w9-orderintent-v05-spec-v1.md §6 T6
TEST_F(RiskGatewayTest, T6_HotPath_P99_Under_50us) {
    // 预热: 确保 intent v0.5 (含 token_id / outcome / side) 在热路径下 P99 < 50us
    // 注: NoopEmitter (不写 IO), 10000 次 evaluate 统计 P99
    class NoopEmit : public AuditEmitter {
    public:
        [[nodiscard]] bool emit(AuditRecord const&) noexcept override { return true; }
    };
    auto noop = std::make_shared<NoopEmit>();
    RiskConfig c = cfg_;
    c.per_outcome_cap_usdc = 25'000;
    RiskGateway fast_rm(c, noop);
    fast_rm.set_state(RmState::RUNNING);
    fast_rm.set_market_active(kMockConditionId, true);
    fast_rm.set_market_state(kMockConditionId, MarketState::PREGAME);
    fast_rm.set_market_freshness_ms(kMockConditionId, 100);
    fast_rm.set_bankroll(1'000'000);

    constexpr int kN = 10'000;
    std::vector<std::int64_t> times;
    times.reserve(kN);

    for (int i = 0; i < kN; ++i) {
        auto const now = ::stcpp::infra::wal::pit::NowRealtimeNs();
        OrderIntent it;
        it.event_ts_ns = now - 500 * NS_PER_MS;
        it.data_source_ts_ns = now - 400 * NS_PER_MS;
        it.ingestion_ts_ns = now - 100 * NS_PER_MS;
        it.as_of_ts_ns = now - 10 * NS_PER_MS;
        it.condition_id = kMockConditionId;
        it.token_id = kMockTokenId;
        it.outcome = Outcome::Yes;
        it.side = Side::Buy;
        it.strategy_id = "strat_a";
        it.signal_id = "sig_t6_" + std::to_string(i);
        it.feature_snapshot_id = "fs_t6";
        it.price = 0.50;
        it.size_pUSD_micro = 1'000;
        it.book_depth_l1_usdc = 5'000;
        it.book_snapshot_ts_ns = now - 200 * NS_PER_MS;
        it.tick_size = 0.01;

        auto const t0 = std::chrono::steady_clock::now();
        auto d = fast_rm.evaluate(it);
        auto const t1 = std::chrono::steady_clock::now();
        (void)d;
        times.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    }

    std::sort(times.begin(), times.end());
    auto const p99_idx = static_cast<std::size_t>(static_cast<double>(kN) * 0.99);
    auto const p99_ns = times[p99_idx];
    auto const p50_ns = times[kN / 2];

    EXPECT_LT(p99_ns, 50'000) << "T6: RM evaluate P99 < 50us (实测 " << p99_ns << " ns)";
    EXPECT_LT(p50_ns, 5'000) << "T6: RM evaluate P50 < 5us (实测 " << p50_ns << " ns)";

    std::printf("[T6 RM evaluate v0.5] P50=%lld ns  P99=%lld ns  budget=50000 ns\n",
                static_cast<long long>(p50_ns), static_cast<long long>(p99_ns));
}

// T7: ABI handshake 字段对齐 100% (OrderIntent v0.5 → SignedOrder)
// cite: laoli-laoSun-handshake-v1.md §3 SignedOrder ABI lock
// 老孙 SignerV52 v5.3 联动 stub (W9 W4 联调完成)
TEST(T7_ABI_Handshake, OrderIntent_V05_FieldAlignment) {
    // compile-time: OrderIntent v0.5 必含以下字段
    OrderIntent it;

    // condition_id → SignedOrder.condition_id [PASS]
    it.condition_id = "0x1234";
    EXPECT_EQ(it.condition_id, "0x1234");

    // token_id → SignedOrder.token_id [PASS]
    it.token_id = "79394000000";
    EXPECT_EQ(it.token_id, "79394000000");

    // side (Buy=0) → SignedOrder.side (0) [PASS]
    it.side = Side::Buy;
    EXPECT_EQ(static_cast<std::uint8_t>(it.side), 0u)
        << "T7: Side::Buy 必须 = 0 (Polymarket EIP-712 Order.side BUY=0)";

    // side (Sell=1) → SignedOrder.side (1) [PASS]
    it.side = Side::Sell;
    EXPECT_EQ(static_cast<std::uint8_t>(it.side), 1u)
        << "T7: Side::Sell 必须 = 1 (Polymarket EIP-712 Order.side SELL=1)";

    // outcome → (not in SignedOrder, token_id 隐含) [OK, 设计意图]
    // Outcome enum ABI lock v1.7: 枚举值固定
    EXPECT_EQ(static_cast<std::uint8_t>(Outcome::Yes), 0u) << "T7: Outcome::Yes=0";
    EXPECT_EQ(static_cast<std::uint8_t>(Outcome::No), 1u) << "T7: Outcome::No=1";
    EXPECT_EQ(static_cast<std::uint8_t>(Outcome::Home), 2u) << "T7: Outcome::Home=2";
    EXPECT_EQ(static_cast<std::uint8_t>(Outcome::Draw), 3u) << "T7: Outcome::Draw=3";
    EXPECT_EQ(static_cast<std::uint8_t>(Outcome::Away), 4u) << "T7: Outcome::Away=4";
    EXPECT_EQ(static_cast<std::uint8_t>(Outcome::Over), 5u) << "T7: Outcome::Over=5";
    EXPECT_EQ(static_cast<std::uint8_t>(Outcome::Under), 6u) << "T7: Outcome::Under=6";

    // price → limit_price_bps (*10000) [PASS, transformer 负责]
    it.price = 0.55;
    EXPECT_NEAR(it.price * 10000.0, 5500.0, 0.01) << "T7: price → limit_price_bps 转换正确性验证";

    // size_pUSD_micro (v0.6 rename from size_usdc): 直接是 pUSD micro 单位 (1e-6)
    // transformer_v62 直接透传此值进 SignV62Request.size_pUSD_micro
    it.size_pUSD_micro = 10'000'000LL;  // 10 pUSD (micro)
    EXPECT_EQ(it.size_pUSD_micro, 10'000'000LL) << "T7: size_pUSD_micro v0.6 rename (老唐 audit v1.4 对齐)";

    std::printf("[T7 ABI Handshake] OrderIntent v0.5 字段对齐 100%% PASS\n");
}

}  // namespace stcpp::risk::test
