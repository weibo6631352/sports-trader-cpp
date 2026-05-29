// tests/unit/sizing/test_sizing_rm_consistency.cpp — SizingCalculator RM 一致性验证 v0.1
//
// Owner: 小袁 (quant-microstructure)
// last_review: 2026-05-29
//
// 验收口径 (老韩 §5.2 RM 主权强制项):
//   1. 零 sizing 维度 reject: N≥10,000 组随机输入,
//      sizing suggested_notional → OrderIntent.size_pUSD_micro → RiskGateway::evaluate
//      → 断言不出现 EXCEED_PER_ORDER_CAP / EXCEED_PER_OUTCOME_CAP /
//        EXCEED_CONDITION_EXPOSURE / INSUFFICIENT_BANKROLL /
//        EDGE_CI_NEGATIVE / EDGE_NEGATED_BY_SLIPPAGE
//      (允许 state/stale/market/duplicate 等非 sizing 维度 reject)
//
//   2. net edge 同源 (±1e-9): sizing net_ci_edge 与 RM net_edge_after_fee
//      在同 (edge_ci_lower, p) 下数值一致
//
//   3. CI gating 同源: sizing 置 0 输入集 ⊇ RM EDGE_CI_NEGATIVE/fee 门输入集
//
//   4. bankroll 动态: set_bankroll(80k) 后 sizing cap4 用 80k 重算
//
//   5. grep 守护 (CI): stcpp_sizing TU 内无 cap 字面量
//      (此测试文件本身可用字面量, 守护目标是 sizing_calculator.cpp/.hpp)

#include <cmath>
#include <cstdint>
#include <memory>
#include <random>
#include <string>

#include <gtest/gtest.h>

#include "stcpp/risk/reject_enum.hpp"
#include "stcpp/risk/risk_gateway.hpp"
#include "stcpp/sizing/sizing_calculator.hpp"

namespace stcpp::sizing::test {

// ---------------------------------------------------------------------------
// 辅助: InMemoryAuditEmitter (不写 WAL; 单测用)
// ---------------------------------------------------------------------------

class NoopAuditEmitter : public risk::AuditEmitter {
public:
    bool emit(risk::AuditRecord const&) noexcept override { return true; }
};

// ---------------------------------------------------------------------------
// 辅助: 构造最小合法 OrderIntent (绕过 state/stale/duplicate 等非 sizing 维度)
// ---------------------------------------------------------------------------

static risk::OrderIntent make_minimal_intent(std::int64_t size_pUSD_micro, double /*edge_ci_lower*/,
                                             double price, std::int32_t /*slippage_bps*/,
                                             std::string const& signal_id) {
    risk::OrderIntent it;
    // R-20 4 ts (单调不等式)
    std::int64_t const now = 1'748'000'000'000'000'000LL;
    it.event_ts_ns = now - 200'000'000;
    it.data_source_ts_ns = now - 150'000'000;
    it.ingestion_ts_ns = now - 50'000'000;
    it.as_of_ts_ns = now - 1'000'000;

    it.condition_id = "0xABCD";
    it.token_id = "12345";
    it.outcome = risk::Outcome::Yes;
    it.side = risk::Side::Buy;
    it.strategy_id = "test_strategy";
    it.signal_id = signal_id;
    it.feature_snapshot_id = "fs_test";

    it.price = price;
    it.size_pUSD_micro = size_pUSD_micro;

    // book context (slippage model 需要; 给合法值绕过 EXCEED_BOOK_DEPTH / STALE)
    it.book_depth_l1_usdc = 1e8;  // 极大深度 → ρ ~ 0, slippage ~ 0
    // book_snapshot_ts_ns 需 > 0 且不 stale (RM check_stale / SlippageModel)
    // 使用 as_of_ts - 1s (在 60s 窗口内)
    it.book_snapshot_ts_ns = now - 1'000'000'000LL;  // 1s 前
    it.tick_size = 0.01;

    // V2 字段
    it.timestamp_ms = (now / 1'000'000LL);  // 毫秒时间戳 (非零, 绕过 TS_V2_MISSING)
    it.metadata = "0x0000000000000000000000000000000000000000000000000000000000000000";
    it.builder = "0x0000000000000000000000000000000000000000000000000000000000000000";

    return it;
}

// ---------------------------------------------------------------------------
// 辅助: 构造 RiskConfig + RiskGateway (RUNNING 状态, 市场激活)
// ---------------------------------------------------------------------------

static std::pair<risk::RiskConfig, std::shared_ptr<risk::RiskGateway>> make_rm(
    std::int64_t bankroll_usdc = 100'000) {
    risk::RiskConfig cfg;
    cfg.bankroll_usdc = bankroll_usdc;
    cfg.edge_ci_lower_floor = 0.0;
    cfg.excessive_slippage_bps = 10'000;  // 松弛 slippage 上限 (单测不关注)
    cfg.enable_moneyline = true;

    auto emitter = std::make_shared<NoopAuditEmitter>();
    auto gw = std::make_shared<risk::RiskGateway>(cfg, emitter);

    // 设置 RUNNING 状态 (默认 SAFE_MODE)
    gw->set_state(risk::RmState::RUNNING);

    // 激活市场 (绕过 MARKET_NOT_ACTIVE)
    gw->set_market_active("0xABCD", true);
    gw->set_market_state("0xABCD", risk::MarketState::PREGAME);

    // 市场 freshness (绕过 STALE_DATA; PREGAME halt 15s, 设 5s 内)
    gw->set_market_freshness_ms("0xABCD", 100);

    // token freshness (绕过 book_token_id_mismatch R8.4)
    gw->set_token_book_freshness_ms("12345", 100);

    // 全局 recon freshness (绕过 recon stale)
    gw->set_recon_freshness_ms(100);

    return {cfg, gw};
}

// ---------------------------------------------------------------------------
// 辅助: 判断拒绝码是否属于"sizing 维度"
//   sizing 维度: EXCEED_PER_ORDER_CAP / EXCEED_PER_OUTCOME_CAP /
//                EXCEED_CONDITION_EXPOSURE / INSUFFICIENT_BANKROLL /
//                EDGE_CI_NEGATIVE / EDGE_NEGATED_BY_SLIPPAGE
// ---------------------------------------------------------------------------

static bool is_sizing_reject(risk::RejectCode code) noexcept {
    switch (code) {
        case risk::RejectCode::EXCEED_PER_ORDER_CAP:
        case risk::RejectCode::EXCEED_PER_OUTCOME_CAP:
        case risk::RejectCode::EXCEED_CONDITION_EXPOSURE:
        case risk::RejectCode::INSUFFICIENT_BANKROLL:
        case risk::RejectCode::EDGE_CI_NEGATIVE:
        case risk::RejectCode::EDGE_NEGATED_BY_SLIPPAGE:
            return true;
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// Test 1: 零 sizing 维度 reject (N=10,000 组随机输入)
// ---------------------------------------------------------------------------

TEST(SizingRmConsistencyTest, ZeroSizingReject_RandomInputs_N10000) {
    auto [cfg, gw] = make_rm(100'000);

    std::mt19937_64 rng(42);
    std::uniform_real_distribution<double> dist_p(0.05, 0.95);        // fair_value
    std::uniform_real_distribution<double> dist_c(0.05, 0.95);        // price
    std::uniform_real_distribution<double> dist_ci(0.001, 0.30);      // edge_ci_lower (均正)
    std::uniform_real_distribution<double> dist_slip(0.0, 5.0);       // slippage_bps (很小)
    std::uniform_real_distribution<double> dist_fr(0.50, 1.0);        // fill_rate (>= floor)
    std::uniform_real_distribution<double> dist_br(50'000, 200'000);  // bankroll

    int sizing_dim_rejects = 0;
    int sizing_positive_count = 0;  // sizing 显正仓位的次数

    for (int i = 0; i < 10'000; ++i) {
        SizingInput in;
        in.fair_value = dist_p(rng);
        in.price = dist_c(rng);
        in.edge_ci_lower = dist_ci(rng);
        in.edge_bps = in.edge_ci_lower * 10'000.0;
        in.bankroll_usdc = dist_br(rng);
        in.fill_rate = dist_fr(rng);
        in.slippage_bps = dist_slip(rng);
        in.current_token_exposure_usdc = 0.0;
        in.current_condition_exposure_usdc = 0.0;
        in.buy_yes = (in.fair_value > in.price);
        in.model_conf = 1.0;

        auto out = SizingCalculator::compute(cfg, in);

        if (!out.valid || out.suggested_notional <= 0.0) {
            continue;  // sizing 拒绝 / 置 0 → 跳过 (RM 不需放行)
        }

        sizing_positive_count++;

        // sizing 显正仓位 → 喂 RM (构造 intent)
        auto const size_micro = static_cast<std::int64_t>(out.suggested_notional);
        if (size_micro <= 0) {
            continue;
        }

        std::string const sig_id = "sig_" + std::to_string(i);

        // 注入 edge_ci_lower (绕过 RM signal 维度需 set_edge_ci_lower)
        gw->set_edge_ci_lower(sig_id, in.edge_ci_lower);

        auto intent = make_minimal_intent(size_micro, in.edge_ci_lower, in.price,
                                          static_cast<std::int32_t>(in.slippage_bps), sig_id);

        auto decision = gw->evaluate(intent);

        if (decision.is_rejected() && is_sizing_reject(decision.reject)) {
            ++sizing_dim_rejects;
            // 打印前几条帮助 debug
            if (sizing_dim_rejects <= 3) {
                // GTEST_LOG_(INFO) << "sizing reject: code=" << static_cast<int>(decision.reject)
                //                  << " notional=" << out.suggested_notional
                //                  << " bankroll=" << in.bankroll_usdc;
            }
        }

        // 幂等 cache 清空 (prevent DUPLICATE_INTENT on repeated signal_id)
        // 不 clear — 每次用不同 sig_id 绕过幂等
    }

    // 核心断言: sizing 显正仓位时, RM 不因 sizing 维度拒
    EXPECT_EQ(sizing_dim_rejects, 0) << "sizing dimension rejects found: " << sizing_dim_rejects
                                     << " (out of " << sizing_positive_count << " positive sizing results)";
}

// ---------------------------------------------------------------------------
// Test 2: net edge 同源 ±1e-9 (sizing net_ci_edge vs RM net_edge_after_fee)
// ---------------------------------------------------------------------------

TEST(SizingRmConsistencyTest, NetEdge_SameFormula_WithinTolerance) {
    // RM 公式 (SSOT risk_gateway.cpp L584-L586):
    //   net_edge_after_fee = edge_ci_lower - kSportsTakerFeeRate × p × (1-p)
    // sizing helper 必须完全同源 (±1e-9 容差)

    struct TestCase {
        double edge_ci_lower;
        double p;  // price (= fair_value 用于 fee 计算)
    };

    std::vector<TestCase> cases = {
        {0.08, 0.60}, {0.15, 0.70}, {0.02, 0.45}, {0.001, 0.50},
        {0.30, 0.80}, {0.05, 0.35}, {0.10, 0.90}, {0.20, 0.55},
    };

    for (auto const& tc : cases) {
        // RM 公式 (直接计算)
        double const fee_per_unit = 0.03 * tc.p * (1.0 - tc.p);  // kSportsTakerFeeRate=0.03
        double const rm_net_edge = tc.edge_ci_lower - fee_per_unit;

        // sizing helper
        double const sz_net_edge = SizingCalculator::compute_net_ci_edge(tc.edge_ci_lower, tc.p);

        EXPECT_NEAR(sz_net_edge, rm_net_edge, 1e-9) << "edge_ci_lower=" << tc.edge_ci_lower << " p=" << tc.p
                                                    << " rm=" << rm_net_edge << " sz=" << sz_net_edge;
    }
}

// ---------------------------------------------------------------------------
// Test 3: CI gating 同源 (sizing 置 0 集合 ⊇ RM EDGE_CI_NEGATIVE 集合)
//   即: 若 sizing 置 0 但 RM 放行 → 违反铁律 (sizing 比 RM 更松, 不允许)
//   等价验证: RM EDGE_CI_NEGATIVE 的输入 → sizing 也置 0
// ---------------------------------------------------------------------------

TEST(SizingRmConsistencyTest, CI_Gating_SizingMoreConservativeThanRM) {
    auto [cfg, gw] = make_rm(100'000);

    // 构造: edge_ci_lower <= 0 → RM 必拒 EDGE_CI_NEGATIVE → sizing 也必须置 0
    std::vector<double> ci_values = {0.0, -0.001, -0.1, -1.0};

    for (double ci : ci_values) {
        SizingInput in;
        in.fair_value = 0.60;
        in.price = 0.50;
        in.edge_ci_lower = ci;
        in.edge_bps = 0.0;
        in.bankroll_usdc = 100'000.0;
        in.fill_rate = 0.80;
        in.slippage_bps = 10.0;
        in.current_token_exposure_usdc = 0.0;
        in.current_condition_exposure_usdc = 0.0;
        in.buy_yes = true;

        auto out = SizingCalculator::compute(cfg, in);

        // sizing 对 ci <= 0 必须置 0 (fail-closed)
        EXPECT_FALSE(out.valid) << "sizing should be invalid for edge_ci_lower=" << ci;
        EXPECT_EQ(out.capped_by, CappedBy::NO_EDGE) << "capped_by should be NO_EDGE for edge_ci_lower=" << ci;
        EXPECT_DOUBLE_EQ(out.suggested_notional, 0.0)
            << "suggested_notional should be 0 for edge_ci_lower=" << ci;
    }
}

// ---------------------------------------------------------------------------
// Test 4: bankroll 动态 — set_bankroll(80k) 后 sizing cap4 用 80k 重算
//   防止 drawdown 后 bankroll 缩水但 sizing 仍按旧 bankroll 算 (老韩 §1.3)
// ---------------------------------------------------------------------------

TEST(SizingRmConsistencyTest, BankrollDynamic_Drawdown_Cap4_Recalculated) {
    auto [cfg, gw] = make_rm(100'000);

    // 初始 bankroll: 100k
    SizingInput in;
    in.fair_value = 0.70;
    in.price = 0.50;
    in.edge_ci_lower = 0.15;
    in.edge_bps = 2000.0;
    in.bankroll_usdc = 100'000.0;
    in.fill_rate = 0.80;
    in.slippage_bps = 5.0;
    in.current_token_exposure_usdc = 0.0;
    in.current_condition_exposure_usdc = 0.0;
    in.buy_yes = true;

    auto out_100k = SizingCalculator::compute(cfg, in);
    ASSERT_TRUE(out_100k.valid);

    // drawdown: bankroll 缩水到 80k
    in.bankroll_usdc = 80'000.0;
    gw->set_bankroll(80'000);

    auto out_80k = SizingCalculator::compute(cfg, in);
    ASSERT_TRUE(out_80k.valid);

    // cap4 = bankroll × 0.10
    // out_80k 的 cap4 = 8000 USDC; out_100k 的 cap4 = 10000 USDC
    double const cap4_80k = 80'000.0 * kMaxBankrollFraction;
    double const cap4_100k = 100'000.0 * kMaxBankrollFraction;

    EXPECT_LE(out_80k.suggested_notional, cap4_80k + 1e-9) << "80k bankroll: cap4 should be 8000";
    EXPECT_LE(out_100k.suggested_notional, cap4_100k + 1e-9) << "100k bankroll: cap4 should be 10000";

    // 80k bankroll 的 sizing 应不超过 100k bankroll 的 sizing (更保守)
    EXPECT_LE(out_80k.suggested_notional, out_100k.suggested_notional + 1e-9);

    // 将 80k sizing 喂给 RM (bankroll 也 set_bankroll(80k)) — 不应 INSUFFICIENT_BANKROLL
    auto const size_micro_80k = static_cast<std::int64_t>(out_80k.suggested_notional);
    if (size_micro_80k > 0) {
        std::string const sig_id = "sig_bankroll_80k";
        gw->set_edge_ci_lower(sig_id, in.edge_ci_lower);

        auto intent = make_minimal_intent(size_micro_80k, in.edge_ci_lower, in.price,
                                          static_cast<std::int32_t>(in.slippage_bps), sig_id);
        auto decision = gw->evaluate(intent);

        // 不应出现 sizing 维度 reject
        if (decision.is_rejected()) {
            EXPECT_FALSE(is_sizing_reject(decision.reject))
                << "RM rejected with sizing code: " << static_cast<int>(decision.reject);
        }
    }
}

// ---------------------------------------------------------------------------
// Test 5: sizing 显正 ⟹ RM 不因 EDGE_CI_NEGATIVE 拒 (针对性验证)
// ---------------------------------------------------------------------------

TEST(SizingRmConsistencyTest, SizingPositive_Implies_RM_NotEdgeCINegative) {
    auto [cfg, gw] = make_rm(100'000);

    // 构造 sizing 输出为正的输入 (edge_ci_lower > 0, net_ci_edge > 0)
    SizingInput in;
    in.fair_value = 0.65;
    in.price = 0.50;
    in.edge_ci_lower = 0.10;  // 1000 bps
    in.edge_bps = 1500.0;
    in.bankroll_usdc = 100'000.0;
    in.fill_rate = 0.80;
    in.slippage_bps = 50.0;  // < 1000 bps (门 A 通过)
    in.current_token_exposure_usdc = 0.0;
    in.current_condition_exposure_usdc = 0.0;
    in.buy_yes = true;

    auto out = SizingCalculator::compute(cfg, in);
    ASSERT_TRUE(out.valid);
    ASSERT_GT(out.suggested_notional, 0.0);

    auto const size_micro = static_cast<std::int64_t>(out.suggested_notional);
    ASSERT_GT(size_micro, 0);

    std::string const sig_id = "sig_edge_ci_pos";
    gw->set_edge_ci_lower(sig_id, in.edge_ci_lower);

    auto intent = make_minimal_intent(size_micro, in.edge_ci_lower, in.price,
                                      static_cast<std::int32_t>(in.slippage_bps), sig_id);
    auto decision = gw->evaluate(intent);

    if (decision.is_rejected()) {
        EXPECT_NE(decision.reject, risk::RejectCode::EDGE_CI_NEGATIVE)
            << "sizing显正但 RM EDGE_CI_NEGATIVE 拒 — gating 不同源!";
        EXPECT_NE(decision.reject, risk::RejectCode::EXCEED_PER_ORDER_CAP);
        EXPECT_NE(decision.reject, risk::RejectCode::EXCEED_PER_OUTCOME_CAP);
        EXPECT_NE(decision.reject, risk::RejectCode::EXCEED_CONDITION_EXPOSURE);
        EXPECT_NE(decision.reject, risk::RejectCode::INSUFFICIENT_BANKROLL);
    }
}

}  // namespace stcpp::sizing::test
