// tests/unit/test_risk_gateway_buy_no.cpp — Phase B 买 NO 全链路 RM 专项单测
//
// Owner: 老韩 (risk-engineer, RM 主权) spec → GM 落地
// 落: docs/RESEARCH/laohan-phaseB-buy-no-rm-checklist-v1.md §8 (T-NO-1..12)
//
// 验证 RM 本体对买 NO intent (outcome=No, token_id=NO token, side=Buy) 零改支持:
//   - 9 gate token-agnostic (不误拒 NO / 不误判)
//   - per-condition cap 两边 (YES+NO) notional 天然合并 (condition_exposure 按 condition_id 聚合)
//   - NO intent audit emit outcome=No 正确打标 (R-1 可追溯)
//   - NO book freshness 独立判 (token 隔离, YES 陈旧不误杀 NO)
//   - NO liquidity gate 用喂进来的 (NO) book depth
//
// 红线: §8.1 纪律#1 引用粘原文; B-1..B-5 RM 主权放行门禁。

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "stcpp/domain/micro_pusd.hpp"
#include "stcpp/execution/virtual_matcher.hpp"
#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/risk/position_ledger.hpp"
#include "stcpp/risk/risk_gateway.hpp"

namespace stcpp::risk::buy_no_test {

namespace {

constexpr std::int64_t NS_PER_MS = 1'000'000LL;
// YES/NO 共享 condition_id (per-condition cap 合并的 key); YES/NO 各自 token_id
constexpr const char* kCid = "0xa9db600590209698097db2fb8382989ea1cf6a9b91f0428b2e1d4f35d724c3ff";
constexpr const char* kTidYes = "1234567890";
constexpr const char* kTidNo = "9876543210";  // 区别于 YES

// 捕获 audit 的 emitter (验 NO intent 的 outcome/token_id 打标)
class CapturingEmitter final : public AuditEmitter {
public:
    [[nodiscard]] bool emit(AuditRecord const& r) noexcept override {
        std::lock_guard<std::mutex> g(mu_);
        records_.push_back(r);
        return true;
    }
    [[nodiscard]] AuditRecord back() const {
        std::lock_guard<std::mutex> g(mu_);
        return records_.back();
    }
    [[nodiscard]] std::size_t size() const {
        std::lock_guard<std::mutex> g(mu_);
        return records_.size();
    }

private:
    mutable std::mutex mu_;
    std::vector<AuditRecord> records_;
};

execution::VirtualFill make_fill(std::int64_t size_micro, double price) {
    execution::VirtualFill f{};
    f.fill_shares_micro = size_micro;  // A1: micro int64
    f.fill_price = price;
    f.reject = execution::MatchReject::Ok;
    return f;
}

}  // namespace

class BuyNoTest : public ::testing::Test {
protected:
    void SetUp() override {
        emitter_ = std::make_shared<CapturingEmitter>();
        cfg_ = RiskConfig{};
        cfg_.per_order_cap_usdc = domain::MicroPUSD::from_pusd(2'000.0);
        cfg_.market_exposure_cap_usdc = domain::MicroPUSD::from_pusd(5'000.0);  // condition cap
        cfg_.per_outcome_cap_usdc = domain::MicroPUSD::from_pusd(3'000.0);
        cfg_.bankroll_usdc = domain::MicroPUSD::from_pusd(100'000.0);
        cfg_.daily_loss_halt_usdc = domain::MicroPUSD::from_pusd(5'000.0);
        cfg_.edge_ci_lower_floor = -1.0;  // 放宽信号门 (聚焦 token-agnostic 而非 edge)
        cfg_.excessive_slippage_bps = 200;
        rm_ = std::make_unique<RiskGateway>(cfg_, emitter_);
        rm_->set_state(RmState::RUNNING);
        inject_market();
    }

    void inject_market() {
        rm_->set_market_active(kCid, true);
        rm_->set_market_state(kCid, MarketState::PREGAME);
        rm_->set_market_freshness_ms(kCid, 100);
        rm_->set_token_book_freshness_ms(kTidYes, 100);
        rm_->set_token_book_freshness_ms(kTidNo, 100);
        rm_->set_recon_freshness_ms(100);
    }

    // 买 NO intent: outcome=No, token_id=NO token, side=Buy, price=ask_NO
    OrderIntent make_no_intent(std::string sig, std::int64_t size_micro = 1'000'000, double ask_no = 0.45) {
        auto const now = ::stcpp::infra::wal::pit::NowRealtimeNs();
        OrderIntent it;
        it.event_ts_ns = now - 500 * NS_PER_MS;
        it.data_source_ts_ns = now - 400 * NS_PER_MS;
        it.ingestion_ts_ns = now - 100 * NS_PER_MS;
        it.as_of_ts_ns = now - 10 * NS_PER_MS;
        it.condition_id = kCid;
        it.token_id = kTidNo;
        it.outcome = Outcome::No;
        it.side = Side::Buy;
        it.strategy_id = "strat_no";
        it.signal_id = std::move(sig);
        it.feature_snapshot_id = "fs_no";
        it.price = ask_no;
        it.size_pUSD_micro = size_micro;
        it.book_depth_l1_usdc = 5'000.0;  // NO book depth (够厚)
        it.book_snapshot_ts_ns = now - 200 * NS_PER_MS;
        it.tick_size = 0.01;
        it.is_close = false;
        it.timestamp_ms = now / NS_PER_MS;
        return it;
    }

    void expect_rejected(RiskDecision const& d, RejectCode code) {
        EXPECT_EQ(d.decision, Decision::REJECTED);
        EXPECT_EQ(d.reject, code);
    }

    RiskConfig cfg_;
    std::shared_ptr<CapturingEmitter> emitter_;
    std::unique_ptr<RiskGateway> rm_;
};

// T-NO-1: NO intent 全链路 APPROVED (9 gate token-agnostic, 不误拒 NO)
TEST_F(BuyNoTest, T_NO_1_NoIntent_Approved) {
    auto d = rm_->evaluate(make_no_intent("no_ok"));
    EXPECT_EQ(d.decision, Decision::APPROVED)
        << "买 NO intent 应过 RM 9 gate (token-agnostic); reject=" << static_cast<int>(d.reject);
}

// T-NO-2: NO token_id 非数字 → INVALID_TOKEN_ID_FORMAT
TEST_F(BuyNoTest, T_NO_2_NoTokenId_NonNumeric) {
    auto it = make_no_intent("no_badtok");
    it.token_id = "0xabc";  // 含非数字
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::INVALID_INTENT);
    EXPECT_EQ(d.sub_reason, InvalidIntentSubReason::INVALID_TOKEN_ID_FORMAT);
}

// T-NO-3: NO ask 越界 (price >= 1) → INVALID_INTENT
TEST_F(BuyNoTest, T_NO_3_NoAsk_OutOfRange) {
    auto it = make_no_intent("no_badprice");
    it.price = 1.0;  // 越界
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::INVALID_INTENT);
}

// T-NO-4: NO book freshness 独立判 — NO token 陈旧 → STALE_DATA
TEST_F(BuyNoTest, T_NO_4_NoBookStale) {
    rm_->set_market_state(kCid, MarketState::INPLAY_HOT_CRIT);
    rm_->set_token_book_freshness_ms(kTidNo, 50'000);  // 超 halt
    auto d = rm_->evaluate(make_no_intent("no_stale"));
    expect_rejected(d, RejectCode::STALE_DATA);
}

// T-NO-5: YES freshness 陈旧不误杀 NO (token 隔离)
TEST_F(BuyNoTest, T_NO_5_YesStale_DoesNotKillNo) {
    rm_->set_token_book_freshness_ms(kTidYes, 50'000);  // YES 陈旧
    rm_->set_token_book_freshness_ms(kTidNo, 100);      // NO 新鲜
    auto d = rm_->evaluate(make_no_intent("no_yesstale"));
    EXPECT_EQ(d.decision, Decision::APPROVED)
        << "NO intent 不应受 YES book 陈旧影响 (per-token freshness 隔离)";
}

// T-NO-6: NO intent audit emit outcome=No + token_id=NO token (R-1 可追溯)
TEST_F(BuyNoTest, T_NO_6_NoAudit_OutcomeNo) {
    auto d = rm_->evaluate(make_no_intent("no_audit"));
    (void)d;
    ASSERT_GT(emitter_->size(), 0u);
    auto rec = emitter_->back();
    EXPECT_EQ(rec.outcome, static_cast<std::uint8_t>(Outcome::No)) << "audit.outcome 应为 No";
    EXPECT_EQ(rec.token_id, kTidNo) << "audit.token_id 应为 NO token";
    EXPECT_EQ(rec.condition_id, kCid);
}

// T-NO-8: NO 大单 vs 薄 NO depth → EXCEED_BOOK_DEPTH (防误填 YES 厚 depth 放行 NO 大单)
TEST_F(BuyNoTest, T_NO_8_NoBigOrder_ThinDepth) {
    RiskConfig c = cfg_;
    c.per_order_cap_usdc = domain::MicroPUSD::from_pusd(2'000.0);
    auto le = std::make_shared<CapturingEmitter>();
    RiskGateway local(c, le);
    local.set_state(RmState::RUNNING);
    local.set_market_active(kCid, true);
    local.set_market_state(kCid, MarketState::PREGAME);
    local.set_market_freshness_ms(kCid, 100);
    local.set_token_book_freshness_ms(kTidNo, 100);
    local.set_recon_freshness_ms(100);
    auto it = make_no_intent("no_bigorder");
    it.size_pUSD_micro = domain::MicroPUSD::from_pusd(800.0).v;  // 800 pUSD (whole)
    it.book_depth_l1_usdc = 250.0;                               // NO 薄 depth → ρ=800/250>3
    auto d = local.evaluate(it);
    expect_rejected(d, RejectCode::EXCEED_BOOK_DEPTH);
}

// T-NO-9: per-condition cap 两边合并越限 (最关键) — YES+NO 同 condition 合并越 cap
TEST_F(BuyNoTest, T_NO_9_PerConditionCap_MergedExceed) {
    PositionLedger ledger;
    // 同 condition 下 YES 4000 + NO 4000 → 合并 8000 (> 5000 cap)
    ledger.apply_fill(kCid, kTidYes, Outcome::Yes, make_fill(domain::MicroPUSD::from_pusd(4'000.0).v, 0.50));
    ledger.apply_fill(kCid, kTidNo, Outcome::No, make_fill(domain::MicroPUSD::from_pusd(4'000.0).v, 0.45));
    // 合并 exposure 喂 RM (FeedRiskGateway 同源: get_per_condition_exposure 按 condition_id 聚合)
    const auto cond_exp = ledger.get_per_condition_exposure();
    auto it = cond_exp.find(kCid);
    ASSERT_NE(it, cond_exp.end());
    EXPECT_EQ(it->second, domain::MicroPUSD::from_pusd(8'000.0).v)
        << "condition_exposure 应 = YES 4000 + NO 4000 合并 (按 condition_id 聚合)";
    rm_->set_condition_exposure(kCid, it->second);

    // 新 NO 开仓 intent → 合并 8000 已越 5000 cap → EXCEED_CONDITION_EXPOSURE
    auto d = rm_->evaluate(make_no_intent("no_capexceed"));
    expect_rejected(d, RejectCode::EXCEED_CONDITION_EXPOSURE);
}

// T-NO-10: 合并未越 cap → 放行
TEST_F(BuyNoTest, T_NO_10_PerConditionCap_MergedUnder) {
    PositionLedger ledger;
    ledger.apply_fill(kCid, kTidYes, Outcome::Yes, make_fill(domain::MicroPUSD::from_pusd(1'000.0).v, 0.50));
    ledger.apply_fill(kCid, kTidNo, Outcome::No, make_fill(domain::MicroPUSD::from_pusd(1'000.0).v, 0.45));
    const auto cond_exp = ledger.get_per_condition_exposure();
    auto it = cond_exp.find(kCid);
    ASSERT_NE(it, cond_exp.end());
    rm_->set_condition_exposure(kCid, it->second);  // 合并 2000 < 5000 cap
    auto d = rm_->evaluate(make_no_intent("no_capok", domain::MicroPUSD::from_pusd(500.0).v));
    EXPECT_EQ(d.decision, Decision::APPROVED) << "合并 2000+500 < 5000 cap 应放行";
}

// T-NO-12: position_ledger NO apply_fill + condition/outcome 聚合正确
TEST_F(BuyNoTest, T_NO_12_Ledger_NoAggregation) {
    PositionLedger ledger;
    const std::int64_t no_micro = domain::MicroPUSD::from_pusd(1'500.0).v;
    ledger.apply_fill(kCid, kTidNo, Outcome::No, make_fill(no_micro, 0.45));
    const auto cond_exp = ledger.get_per_condition_exposure();
    const auto tok_exp = ledger.get_per_outcome_exposure();
    ASSERT_NE(cond_exp.find(kCid), cond_exp.end());
    ASSERT_NE(tok_exp.find(kTidNo), tok_exp.end());
    EXPECT_EQ(cond_exp.at(kCid), no_micro) << "condition_exposure 含 NO notional";
    EXPECT_EQ(tok_exp.at(kTidNo), no_micro) << "per-outcome exposure[NO token] == NO notional";
}

}  // namespace stcpp::risk::buy_no_test
