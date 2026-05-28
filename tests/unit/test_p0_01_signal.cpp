// tests/unit/test_p0_01_signal.cpp — P0-01 PinnacleNoVig signal 单测
// 落: 小程 P0-01 spec v0.1
//   - no-vig 公式 paper case (overround 1.04, p_yes_fair vs raw 对比)
//   - 5 触发条件边界 (各 negative + 1 positive)
//   - R-20 4 ts SignalContext 校验

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "stcpp/strategy/live_section_classifier.hpp"
#include "stcpp/strategy/p0_01_pinnacle_no_vig.hpp"
#include "stcpp/strategy/signal_iface.hpp"

namespace {

using stcpp::strategy::GameState;
using stcpp::strategy::MockGameStateSource;
using stcpp::strategy::MockPinnacleSource;
using stcpp::strategy::MockPmSnapshotSource;
using stcpp::strategy::NoVigResult;
using stcpp::strategy::PinnacleNoVigSignal;
using stcpp::strategy::PinnacleQuote;
using stcpp::strategy::PmSnapshot;
using stcpp::strategy::Side;
using stcpp::strategy::SignalContext;
using stcpp::strategy::SignalId;
using stcpp::strategy::SignalOutput;
using stcpp::strategy::SIX_HOURS_NS;
using stcpp::strategy::compute_no_vig;

constexpr std::int64_t NOW            = 1'700'000'000'000'000'000LL;
constexpr std::int64_t SEC_NS         = 1'000'000'000LL;
constexpr std::int64_t MIN_NS         = 60LL * SEC_NS;
constexpr std::int64_t BANKROLL_USDC  = 100'000;
constexpr char const*  MID            = "0xMARKET01";

// 标准 valid ctx (4 ts 单调非降, market_id + feature_snapshot_id 非空)
SignalContext make_ctx() {
    SignalContext c;
    c.event_ts_ns        = NOW - 100'000'000;
    c.data_source_ts_ns  = NOW - 50'000'000;
    c.ingestion_ts_ns    = NOW - 10'000'000;
    c.as_of_ts_ns        = NOW;
    c.market_id          = MID;
    c.feature_snapshot_id = "snap-001";
    return c;
}

// 标准 Pinnacle quote (overround 典型 1.04)
//   decimal_yes = 1.92 → p_yes_raw = 0.520833
//   decimal_no  = 1.92 → p_no_raw  = 0.520833
//   overround = 1.041666; p_yes_fair = 0.5 (对称定盘)
//
// 但为造 edge, 用非对称: decimal_yes=2.10, decimal_no=1.80
//   p_yes_raw = 0.476190, p_no_raw = 0.555556, overround = 1.031746
//   p_yes_fair = 0.476190 / 1.031746 = 0.461538

// === Part 1: no-vig 公式 paper case ===

TEST(P0_01_NoVig, Paper_Overround_104_Symmetric) {
    // decimal_yes=1.92, decimal_no=1.92 → overround 1.041666 — 教科书 1.04 vig
    NoVigResult const r = compute_no_vig(1.92, 1.92);
    ASSERT_TRUE(r.valid);
    EXPECT_NEAR(r.p_yes_raw, 0.520833333, 1e-6);
    EXPECT_NEAR(r.p_no_raw,  0.520833333, 1e-6);
    EXPECT_NEAR(r.overround, 1.041666666, 1e-6);
    EXPECT_NEAR(r.p_yes_fair, 0.5, 1e-9);
    // raw vs fair 对比: raw > fair (因 vig 抬两边)
    EXPECT_GT(r.p_yes_raw, r.p_yes_fair);
}

TEST(P0_01_NoVig, Asymmetric_YesFavored) {
    // decimal_yes=2.10, decimal_no=1.80
    NoVigResult const r = compute_no_vig(2.10, 1.80);
    ASSERT_TRUE(r.valid);
    EXPECT_NEAR(r.p_yes_raw, 1.0 / 2.10, 1e-9);
    EXPECT_NEAR(r.p_no_raw,  1.0 / 1.80, 1e-9);
    EXPECT_NEAR(r.overround, 1.0/2.10 + 1.0/1.80, 1e-9);
    EXPECT_NEAR(r.p_yes_fair, (1.0/2.10) / (1.0/2.10 + 1.0/1.80), 1e-9);
    // 0.461538 ≈
    EXPECT_NEAR(r.p_yes_fair, 0.4615384615, 1e-6);
}

TEST(P0_01_NoVig, RejectInvalidOdds) {
    EXPECT_FALSE(compute_no_vig(0.5, 1.92).valid);  // < 1.0
    EXPECT_FALSE(compute_no_vig(1.92, 1.0).valid);  // == 1.0 边界
    EXPECT_FALSE(compute_no_vig(std::nan(""), 1.92).valid);
    double const inf = std::numeric_limits<double>::infinity();
    EXPECT_FALSE(compute_no_vig(inf, 1.92).valid);
    EXPECT_FALSE(compute_no_vig(-1.5, 1.92).valid);
}

// === Part 2: 5 触发条件 (positive + 5 个 negative) ===

// helper: 标准 happy-path setup → signal 必出
struct HappyFixture {
    MockPinnacleSource  pinnacle;
    MockPmSnapshotSource pm;
    MockGameStateSource  games;

    HappyFixture() {
        // Pinnacle: p_yes_fair = 0.461538 (asymmetric 2.10 / 1.80)
        pinnacle.put(MID, PinnacleQuote{2.10, 1.80, NOW - 1 * SEC_NS});

        // PM mid = 0.40 → edge = |0.40 - 0.461538| = 0.061538 > 0.05 ✓
        PmSnapshot ps;
        ps.mid                  = 0.40;
        ps.top3_liquidity_usdc  = 5'000.0;  // ≥ 2K ✓
        ps.expected_fill_rate   = 0.70;     // ≥ 0.50 ✓
        ps.valid                = true;
        pm.put(MID, ps);

        // GameState: Live (live=true, ended=false, delayed=false)
        GameState g;
        g.live = true;
        g.ended = false;
        g.delayed = false;
        g.kickoff_ts_ns = NOW - 60 * MIN_NS;  // 已开 1h
        games.put(MID, g);
    }
};

TEST(P0_01_Signal, HappyPath_BuyYes_5CondAllPass) {
    HappyFixture f;
    PinnacleNoVigSignal sig(f.pinnacle, f.pm, f.games, BANKROLL_USDC);
    auto const out = sig.tick(make_ctx());
    ASSERT_TRUE(out.has_value());
    // PM_mid=0.40 < p_yes_fair=0.461538 → BUY (v0.5: Side::Buy, outcome 由 Orchestrator 层设)
    EXPECT_EQ(out->side, Side::Buy);
    EXPECT_EQ(out->signal_id, SignalId::P0_01_PinnacleNoVig);
    // edge = 0.061538 → 615 bps (round to nearest)
    EXPECT_GE(out->edge_bps, 610);
    EXPECT_LE(out->edge_bps, 620);
    // confidence = edge / 0.10 ≈ 0.6154, clamp(0, 1)
    EXPECT_GT(out->confidence, 0.60);
    EXPECT_LT(out->confidence, 0.62);
    // size > 0
    EXPECT_GT(out->suggested_size_usdc, 0);
    // size 上限 $5K
    EXPECT_LE(out->suggested_size_usdc, 5'000);
}

TEST(P0_01_Signal, HappyPath_BuyNo_PmAbove) {
    // PM_mid=0.55 > p_yes_fair=0.461538 → BUY_NO; edge=0.0885 > 0.05
    HappyFixture f;
    PmSnapshot ps;
    ps.mid                 = 0.55;
    ps.top3_liquidity_usdc = 5'000.0;
    ps.expected_fill_rate  = 0.70;
    ps.valid               = true;
    f.pm.put(MID, ps);

    PinnacleNoVigSignal sig(f.pinnacle, f.pm, f.games, BANKROLL_USDC);
    auto const out = sig.tick(make_ctx());
    ASSERT_TRUE(out.has_value());
    // v0.5: side=Buy (outcome 由 Orchestrator 层按 token_id 决定, BuyNo = side=Buy + outcome=No token)
    EXPECT_EQ(out->side, Side::Buy);
}

// Cond 1 negative: edge ≤ 0.05
TEST(P0_01_Signal, Cond1_Fail_EdgeBelow5Cent) {
    HappyFixture f;
    PmSnapshot ps;
    ps.mid = 0.45;  // |0.45 - 0.461538| = 0.0115 < 0.05
    ps.top3_liquidity_usdc = 5'000.0;
    ps.expected_fill_rate  = 0.70;
    ps.valid               = true;
    f.pm.put(MID, ps);

    PinnacleNoVigSignal sig(f.pinnacle, f.pm, f.games, BANKROLL_USDC);
    EXPECT_FALSE(sig.tick(make_ctx()).has_value());
}

// Cond 2 negative: liquidity < $2K
TEST(P0_01_Signal, Cond2_Fail_LowLiquidity) {
    HappyFixture f;
    PmSnapshot ps;
    ps.mid                 = 0.40;
    ps.top3_liquidity_usdc = 1'500.0;  // < 2K
    ps.expected_fill_rate  = 0.70;
    ps.valid               = true;
    f.pm.put(MID, ps);

    PinnacleNoVigSignal sig(f.pinnacle, f.pm, f.games, BANKROLL_USDC);
    EXPECT_FALSE(sig.tick(make_ctx()).has_value());
}

// Cond 3 negative: kickoff > 6h && not live
TEST(P0_01_Signal, Cond3_Fail_KickoffFarAndNotLive) {
    HappyFixture f;
    GameState g;
    g.live = false;
    g.ended = false;
    g.delayed = false;
    g.kickoff_ts_ns = NOW + 12 * 60 * MIN_NS;  // 12h 后, > 6h
    f.games.put(MID, g);

    PinnacleNoVigSignal sig(f.pinnacle, f.pm, f.games, BANKROLL_USDC);
    EXPECT_FALSE(sig.tick(make_ctx()).has_value());
}

// Cond 3 positive: kickoff > 6h BUT live=true → 仍过 (OR)
// 但 LiveSection (Live) 由 cond 5 验, 这里只验 cond 3 OR
TEST(P0_01_Signal, Cond3_Pass_LiveOverride) {
    HappyFixture f;
    GameState g;
    g.live = true;  // live 覆盖 kickoff 远
    g.ended = false;
    g.delayed = false;
    g.kickoff_ts_ns = NOW + 24 * 60 * MIN_NS;
    f.games.put(MID, g);

    PinnacleNoVigSignal sig(f.pinnacle, f.pm, f.games, BANKROLL_USDC);
    EXPECT_TRUE(sig.tick(make_ctx()).has_value());
}

// Cond 4 negative: fill_rate < 0.50
TEST(P0_01_Signal, Cond4_Fail_LowFillRate) {
    HappyFixture f;
    PmSnapshot ps;
    ps.mid                 = 0.40;
    ps.top3_liquidity_usdc = 5'000.0;
    ps.expected_fill_rate  = 0.30;
    ps.valid               = true;
    f.pm.put(MID, ps);

    PinnacleNoVigSignal sig(f.pinnacle, f.pm, f.games, BANKROLL_USDC);
    EXPECT_FALSE(sig.tick(make_ctx()).has_value());
}

// Cond 5 negative: LiveSection == Delayed
TEST(P0_01_Signal, Cond5_Fail_DelayedSection) {
    HappyFixture f;
    GameState g;
    g.delayed = true;
    g.live    = false;
    g.ended   = false;
    g.kickoff_ts_ns = NOW + 2 * 60 * MIN_NS;
    f.games.put(MID, g);

    PinnacleNoVigSignal sig(f.pinnacle, f.pm, f.games, BANKROLL_USDC);
    EXPECT_FALSE(sig.tick(make_ctx()).has_value());
}

// Cond 5 negative: LiveSection == Closed
TEST(P0_01_Signal, Cond5_Fail_ClosedSection) {
    HappyFixture f;
    GameState g;
    g.ended = true;
    g.live  = false;
    g.delayed = false;
    g.kickoff_ts_ns = NOW - 4 * 60 * MIN_NS;
    f.games.put(MID, g);

    PinnacleNoVigSignal sig(f.pinnacle, f.pm, f.games, BANKROLL_USDC);
    EXPECT_FALSE(sig.tick(make_ctx()).has_value());
}

// === Part 3: R-20 4 ts SignalContext 校验 ===

TEST(P0_01_R20, RejectZeroTs) {
    HappyFixture f;
    PinnacleNoVigSignal sig(f.pinnacle, f.pm, f.games, BANKROLL_USDC);
    auto ctx = make_ctx();
    ctx.event_ts_ns = 0;
    EXPECT_FALSE(sig.tick(ctx).has_value());

    ctx = make_ctx();
    ctx.data_source_ts_ns = 0;
    EXPECT_FALSE(sig.tick(ctx).has_value());

    ctx = make_ctx();
    ctx.ingestion_ts_ns = 0;
    EXPECT_FALSE(sig.tick(ctx).has_value());

    ctx = make_ctx();
    ctx.as_of_ts_ns = 0;
    EXPECT_FALSE(sig.tick(ctx).has_value());
}

TEST(P0_01_R20, RejectTsOrderViolated) {
    HappyFixture f;
    PinnacleNoVigSignal sig(f.pinnacle, f.pm, f.games, BANKROLL_USDC);
    // event > data_source (反序)
    auto ctx = make_ctx();
    ctx.event_ts_ns = NOW;
    ctx.data_source_ts_ns = NOW - SEC_NS;
    EXPECT_FALSE(sig.tick(ctx).has_value());

    // ingestion > as_of
    ctx = make_ctx();
    ctx.ingestion_ts_ns = NOW + SEC_NS;
    EXPECT_FALSE(sig.tick(ctx).has_value());
}

TEST(P0_01_R20, RejectEmptyMarketIdOrFeatureSnapshotId) {
    HappyFixture f;
    PinnacleNoVigSignal sig(f.pinnacle, f.pm, f.games, BANKROLL_USDC);

    auto ctx = make_ctx();
    ctx.market_id = "";
    EXPECT_FALSE(sig.tick(ctx).has_value());

    ctx = make_ctx();
    ctx.feature_snapshot_id = "";
    EXPECT_FALSE(sig.tick(ctx).has_value());
}

TEST(P0_01_R20, RejectMissingDataSource) {
    HappyFixture f;
    PinnacleNoVigSignal sig(f.pinnacle, f.pm, f.games, BANKROLL_USDC);

    // pinnacle 缺 market_id
    auto ctx = make_ctx();
    ctx.market_id = "0xMISSING";
    EXPECT_FALSE(sig.tick(ctx).has_value());
}

// === Part 4: size clip $5K ===

TEST(P0_01_Size, ClipAt5K) {
    HappyFixture f;
    // edge 极大: PM_mid=0.05 < p_yes_fair=0.461538 → edge ~ 0.41
    PmSnapshot ps;
    ps.mid                 = 0.05;
    ps.top3_liquidity_usdc = 5'000.0;
    ps.expected_fill_rate  = 0.95;
    ps.valid               = true;
    f.pm.put(MID, ps);

    // bankroll 1M → kelly·0.25·fill·1M 必远超 5K
    PinnacleNoVigSignal sig(f.pinnacle, f.pm, f.games, 1'000'000);
    auto const out = sig.tick(make_ctx());
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->suggested_size_usdc, 5'000);
    // confidence clamp 1.0
    EXPECT_NEAR(out->confidence, 1.0, 1e-9);
}

// === Part 5: SignalContext / Output 字段完整性 (R-20 audit anchor) ===

TEST(P0_01_Output, AllFieldsPopulated) {
    HappyFixture f;
    PinnacleNoVigSignal sig(f.pinnacle, f.pm, f.games, BANKROLL_USDC);
    auto const out = sig.tick(make_ctx());
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->signal_id, SignalId::P0_01_PinnacleNoVig);
    EXPECT_GT(out->edge_bps, 0);
    EXPECT_GT(out->suggested_size_usdc, 0);
    EXPECT_GE(out->confidence, 0.0);
    EXPECT_LE(out->confidence, 1.0);
    EXPECT_EQ(stcpp::strategy::to_string(out->signal_id), "P0_01_PinnacleNoVig");
}

}  // namespace
