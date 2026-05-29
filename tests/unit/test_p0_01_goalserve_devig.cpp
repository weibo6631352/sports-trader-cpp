// tests/unit/test_p0_01_goalserve_devig.cpp — P0-01 Goalserve multiplicative de-vig 单测
//
// ADR-008 Wave 28 (小卢 IC pool E-035-02).
//
// 覆盖:
//   T1: multiplicative_devig 公式 paper case (3 家 mock odds)
//   T2: 8-9 家完整 case (与 Pinnacle no-vig 结果对比, 差异 < 2%)
//   T3: < 3 家 fallback (返 valid=false, 信号不触发)
//   T4: 5 触发条件不变 (与 W4 25 测试等价)
//   T5: feature_snapshot_id ABI 不动 (与 ML hook compatible)
//
// 红线:
//   R-20  SignalContext 4 ts + feature_snapshot_id 必带
//   ADR-008 multiplicative only, 不上 Shin
//   老周 ABI: SignalContext / SignalOutput 不动
//   小邓 ML: feature_snapshot_id 字段名不动

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "stcpp/ml/feature_snapshot.hpp"
#include "stcpp/strategy/live_section_classifier.hpp"
#include "stcpp/strategy/p0_01_goalserve_devig.hpp"
#include "stcpp/strategy/signal_iface.hpp"

namespace {

using stcpp::ml::FeatureName;
using stcpp::strategy::BookmakerOdds;
using stcpp::strategy::compute_multiplicative_devig;
using stcpp::strategy::DevigResult;
using stcpp::strategy::GameState;
using stcpp::strategy::GoalserveDevigSignal;
using stcpp::strategy::IGoalserveOddsSource;
using stcpp::strategy::MockGameStateSource;
using stcpp::strategy::MockGoalserveOddsSource;
using stcpp::strategy::MockPmSnapshotSource;
using stcpp::strategy::PmSnapshot;
using stcpp::strategy::Side;
using stcpp::strategy::SignalContext;
using stcpp::strategy::SignalId;
using stcpp::strategy::SignalOutput;
using stcpp::strategy::SIX_HOURS_NS;

constexpr std::int64_t NOW = 1'700'000'000'000'000'000LL;
constexpr std::int64_t SEC_NS = 1'000'000'000LL;
constexpr std::int64_t MIN_NS = 60LL * SEC_NS;
constexpr std::int64_t BANKROLL_USDC = 100'000;
constexpr char const* MID = "0xMARKET01";

// ---------------------------------------------------------------------------
// 标准 valid ctx — R-20 4 ts 单调非降, market_id + feature_snapshot_id 非空
// ---------------------------------------------------------------------------
SignalContext make_ctx() {
    SignalContext c;
    c.event_ts_ns = NOW - 100'000'000;
    c.data_source_ts_ns = NOW - 50'000'000;
    c.ingestion_ts_ns = NOW - 10'000'000;
    c.as_of_ts_ns = NOW;
    c.market_id = MID;
    c.feature_snapshot_id = "snap-devig-001";  // 小邓 ML hook ABI key
    return c;
}

// ---------------------------------------------------------------------------
// 标准 happy-path fixture
//
// 9 家 bookmaker mock (小段 ETL-12: 14/15/16/17/18/65/105/144 + 1 TBD):
//   odds 略微不对称: yes 偏向的 fair value ≈ 0.461
//   PM mid = 0.40 → edge = |0.40 - p_fair| > 0.05 → 触发
// ---------------------------------------------------------------------------
std::vector<BookmakerOdds> make_9_books(std::int64_t snap_ts = NOW - SEC_NS) {
    // 基础报价: decimal_yes=2.10, decimal_no=1.80 → 与 W4 Pinnacle 相동한 non-symmetric case
    // 各家轻微扰动 (±1 tick) 模拟真实多家报价
    return {
        // id  odds_yes  odds_no  snap_ts
        {14, 2.10, 1.80, snap_ts},   // 10Bet
        {15, 2.11, 1.79, snap_ts},   // WilliamHill
        {16, 2.09, 1.81, snap_ts},   // bet365
        {17, 2.10, 1.80, snap_ts},   // Marathon
        {18, 2.12, 1.78, snap_ts},   // Unibet
        {65, 2.08, 1.82, snap_ts},   // BetVictor
        {105, 2.10, 1.80, snap_ts},  // 1xBet
        {144, 2.11, 1.79, snap_ts},  // Betano
        {999, 2.10, 1.80, snap_ts},  // TBD (老彭 W6 EOW 补 id)
    };
}

struct HappyFixture {
    MockGoalserveOddsSource goalserve;
    MockPmSnapshotSource pm;
    MockGameStateSource games;

    HappyFixture() {
        goalserve.put(MID, make_9_books());

        PmSnapshot ps;
        ps.mid = 0.40;
        ps.top3_liquidity_usdc = 5'000.0;
        ps.expected_fill_rate = 0.70;
        ps.valid = true;
        pm.put(MID, ps);

        GameState g;
        g.live = true;
        g.ended = false;
        g.delayed = false;
        g.kickoff_ts_ns = NOW - 60 * MIN_NS;
        games.put(MID, g);
    }
};

// ===========================================================================
// T1: multiplicative de-vig 公式 paper case (3 家 mock odds)
// ===========================================================================

TEST(GoalserveDevig_T1, Formula_3Books_Symmetric) {
    // 3 家全对称 decimal_yes=decimal_no=1.92
    //   p_yes_raw = p_no_raw = 0.520833
    //   overround = 1.041666
    //   p_yes_fair = 0.5 (by symmetry)
    std::vector<BookmakerOdds> books = {
        {14, 1.92, 1.92, NOW},
        {15, 1.92, 1.92, NOW},
        {16, 1.92, 1.92, NOW},
    };
    DevigResult const r = compute_multiplicative_devig(books);
    ASSERT_TRUE(r.valid);
    EXPECT_EQ(r.books_used, 3u);
    EXPECT_NEAR(r.p_yes_fair_avg, 0.5, 1e-9);
    EXPECT_NEAR(r.overround_avg, 1.041666, 1e-4);
}

TEST(GoalserveDevig_T1, Formula_3Books_Asymmetric) {
    // 3 家各自 (2.10, 1.80) → p_yes_fair = 0.461538 per book
    // 均值 = 0.461538
    std::vector<BookmakerOdds> books = {
        {14, 2.10, 1.80, NOW},
        {15, 2.10, 1.80, NOW},
        {16, 2.10, 1.80, NOW},
    };
    DevigResult const r = compute_multiplicative_devig(books);
    ASSERT_TRUE(r.valid);
    EXPECT_EQ(r.books_used, 3u);
    // p_yes_raw = 1/2.10 = 0.476190, p_no_raw = 1/1.80 = 0.555556
    // overround = 1.031746, p_yes_fair = 0.476190/1.031746 = 0.461538
    EXPECT_NEAR(r.p_yes_fair_avg, 0.461538, 1e-5);
    EXPECT_NEAR(r.overround_avg, 1.031746, 1e-5);
}

TEST(GoalserveDevig_T1, Formula_SkipsInvalidOdds) {
    // 5 行, 其中 2 行 odds=0 应被跳过 → 3 有效行, valid=true
    std::vector<BookmakerOdds> books = {
        {14, 2.10, 1.80, NOW}, {15, 0.0, 1.80, NOW},  // odds_yes=0 → skip
        {16, 2.10, 0.0, NOW},                         // odds_no=0  → skip
        {17, 2.10, 1.80, NOW}, {18, 2.10, 1.80, NOW},
    };
    DevigResult const r = compute_multiplicative_devig(books);
    ASSERT_TRUE(r.valid);
    EXPECT_EQ(r.books_used, 3u);
    EXPECT_NEAR(r.p_yes_fair_avg, 0.461538, 1e-5);
}

TEST(GoalserveDevig_T1, Formula_NaNInf_Skipped) {
    double const inf = std::numeric_limits<double>::infinity();
    double const nan = std::numeric_limits<double>::quiet_NaN();
    std::vector<BookmakerOdds> books = {
        {14, inf, 1.80, NOW},  // skip
        {15, nan, 1.80, NOW},  // skip
        {16, 2.10, 1.80, NOW}, {17, 2.10, 1.80, NOW}, {18, 2.10, 1.80, NOW},
    };
    DevigResult const r = compute_multiplicative_devig(books);
    ASSERT_TRUE(r.valid);
    EXPECT_EQ(r.books_used, 3u);
}

// ===========================================================================
// T2: 8-9 家完整 case — 与单家 Pinnacle no-vig 结果差异 < 2%
//
// 参照物: W4 Pinnacle quote (2.10, 1.80) → p_yes_fair = 0.461538
// 9 家 mock 各自 ±1 tick 扰动 → 均值应在 0.461538 ± 2% 以内
// ===========================================================================

TEST(GoalserveDevig_T2, NineBooks_FairValueNearPinnacle) {
    auto const books = make_9_books();
    DevigResult const dv = compute_multiplicative_devig(books);
    ASSERT_TRUE(dv.valid);
    EXPECT_EQ(dv.books_used, 9u);

    // 参照: Pinnacle single-source no-vig (W4 公式)
    double const pinnacle_p_yes_raw = 1.0 / 2.10;
    double const pinnacle_p_no_raw = 1.0 / 1.80;
    double const pinnacle_overround = pinnacle_p_yes_raw + pinnacle_p_no_raw;
    double const pinnacle_p_yes_fair = pinnacle_p_yes_raw / pinnacle_overround;

    // |deviation| < 2% (200 bps)
    double const deviation = std::abs(dv.p_yes_fair_avg - pinnacle_p_yes_fair);
    EXPECT_LT(deviation, 0.02) << "deviation=" << deviation << " pinFair=" << pinnacle_p_yes_fair
                               << " devigFair=" << dv.p_yes_fair_avg;
}

TEST(GoalserveDevig_T2, NineBooks_OverroundSanity) {
    // overround_avg ∈ [1.01, 1.10] (健康范围, 典型 retail book 1.03-1.06)
    auto const books = make_9_books();
    DevigResult const dv = compute_multiplicative_devig(books);
    ASSERT_TRUE(dv.valid);
    EXPECT_GT(dv.overround_avg, 1.01);
    EXPECT_LT(dv.overround_avg, 1.10);
}

TEST(GoalserveDevig_T2, EightBooks_Valid) {
    // 8 家 (去掉 TBD) 仍有效
    auto books = make_9_books();
    books.pop_back();  // 移除第 9 家
    DevigResult const dv = compute_multiplicative_devig(books);
    ASSERT_TRUE(dv.valid);
    EXPECT_EQ(dv.books_used, 8u);
}

// ===========================================================================
// T3: < 3 家 fallback
// ===========================================================================

TEST(GoalserveDevig_T3, Zero_Books_Fallback) {
    std::vector<BookmakerOdds> empty;
    DevigResult const r = compute_multiplicative_devig(empty);
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.books_used, 0u);
    EXPECT_NEAR(r.p_yes_fair_avg, 0.0, 1e-15);
}

TEST(GoalserveDevig_T3, One_Book_Fallback) {
    std::vector<BookmakerOdds> books = {{14, 2.10, 1.80, NOW}};
    DevigResult const r = compute_multiplicative_devig(books);
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.books_used, 1u);
}

TEST(GoalserveDevig_T3, Two_Books_Fallback) {
    std::vector<BookmakerOdds> books = {
        {14, 2.10, 1.80, NOW},
        {15, 2.11, 1.79, NOW},
    };
    DevigResult const r = compute_multiplicative_devig(books);
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.books_used, 2u);
}

TEST(GoalserveDevig_T3, TwoValid_OneInvalid_Fallback) {
    // 3 行但其中 1 行 odds=0 → 只有 2 有效 → fallback
    std::vector<BookmakerOdds> books = {
        {14, 2.10, 1.80, NOW},
        {15, 2.10, 1.80, NOW},
        {16, 0.0, 1.80, NOW},  // skip
    };
    DevigResult const r = compute_multiplicative_devig(books);
    EXPECT_FALSE(r.valid);
    EXPECT_EQ(r.books_used, 2u);
}

TEST(GoalserveDevig_T3, SignalNullopt_WhenFallback) {
    // GoalserveDevigSignal.tick → nullopt 当 books_used < 3
    MockGoalserveOddsSource gs;
    // 只放 2 家
    gs.put(MID, {{14, 2.10, 1.80, NOW}, {15, 2.11, 1.79, NOW}});

    MockPmSnapshotSource pm;
    PmSnapshot ps;
    ps.mid = 0.40;
    ps.top3_liquidity_usdc = 5'000.0;
    ps.expected_fill_rate = 0.70;
    ps.valid = true;
    pm.put(MID, ps);

    MockGameStateSource games;
    GameState g;
    g.live = true;
    g.ended = false;
    g.delayed = false;
    g.kickoff_ts_ns = NOW - 60 * MIN_NS;
    games.put(MID, g);

    GoalserveDevigSignal sig(gs, pm, games, BANKROLL_USDC);
    EXPECT_FALSE(sig.tick(make_ctx()).has_value());
}

// ===========================================================================
// T4: 5 触发条件不变 (与 W4 25 测试逻辑等价)
// ===========================================================================

TEST(GoalserveDevig_T4, HappyPath_BuyYes) {
    HappyFixture f;
    GoalserveDevigSignal sig(f.goalserve, f.pm, f.games, BANKROLL_USDC);
    auto const out = sig.tick(make_ctx());
    ASSERT_TRUE(out.has_value());
    // PM_mid=0.40 < p_yes_fair~0.461 → BUY (v0.5: Side::Buy, outcome 由 Orchestrator 层设)
    EXPECT_EQ(out->side, Side::Buy);
    EXPECT_EQ(out->signal_id, SignalId::P0_01_PinnacleNoVig);  // ABI lock
    EXPECT_GT(out->edge_bps, 0);
    EXPECT_GT(out->suggested_size_usdc, 0);
    EXPECT_LE(out->suggested_size_usdc, 5'000);
    EXPECT_GE(out->confidence, 0.0);
    EXPECT_LE(out->confidence, 1.0);
}

TEST(GoalserveDevig_T4, HappyPath_BuyNo) {
    // PM_mid=0.55 > p_yes_fair~0.461 → BUY_NO
    HappyFixture f;
    PmSnapshot ps;
    ps.mid = 0.55;
    ps.top3_liquidity_usdc = 5'000.0;
    ps.expected_fill_rate = 0.70;
    ps.valid = true;
    f.pm.put(MID, ps);

    GoalserveDevigSignal sig(f.goalserve, f.pm, f.games, BANKROLL_USDC);
    auto const out = sig.tick(make_ctx());
    ASSERT_TRUE(out.has_value());
    // v0.5: side=Buy (outcome 由 Orchestrator 层按 token_id 决定)
    EXPECT_EQ(out->side, Side::Buy);
}

// Cond 1: edge ≤ 0.05 → nullopt
TEST(GoalserveDevig_T4, Cond1_Fail_EdgeBelowThreshold) {
    HappyFixture f;
    // p_yes_fair_avg ≈ 0.461, pm.mid=0.45 → |0.45 - 0.461| ≈ 0.011 < 0.05
    PmSnapshot ps;
    ps.mid = 0.45;
    ps.top3_liquidity_usdc = 5'000.0;
    ps.expected_fill_rate = 0.70;
    ps.valid = true;
    f.pm.put(MID, ps);

    GoalserveDevigSignal sig(f.goalserve, f.pm, f.games, BANKROLL_USDC);
    EXPECT_FALSE(sig.tick(make_ctx()).has_value());
}

// Cond 2: liquidity < $2K → nullopt
TEST(GoalserveDevig_T4, Cond2_Fail_LowLiquidity) {
    HappyFixture f;
    PmSnapshot ps;
    ps.mid = 0.40;
    ps.top3_liquidity_usdc = 1'500.0;
    ps.expected_fill_rate = 0.70;
    ps.valid = true;
    f.pm.put(MID, ps);

    GoalserveDevigSignal sig(f.goalserve, f.pm, f.games, BANKROLL_USDC);
    EXPECT_FALSE(sig.tick(make_ctx()).has_value());
}

// Cond 3: kickoff > 6h AND not live → nullopt
TEST(GoalserveDevig_T4, Cond3_Fail_FarKickoffNotLive) {
    HappyFixture f;
    GameState g;
    g.live = false;
    g.ended = false;
    g.delayed = false;
    g.kickoff_ts_ns = NOW + 12 * 60 * MIN_NS;  // 12h 后
    f.games.put(MID, g);

    GoalserveDevigSignal sig(f.goalserve, f.pm, f.games, BANKROLL_USDC);
    EXPECT_FALSE(sig.tick(make_ctx()).has_value());
}

// Cond 3 positive override: live=true 即使 kickoff > 6h
TEST(GoalserveDevig_T4, Cond3_Pass_LiveOverride) {
    HappyFixture f;
    GameState g;
    g.live = true;
    g.ended = false;
    g.delayed = false;
    g.kickoff_ts_ns = NOW + 24 * 60 * MIN_NS;
    f.games.put(MID, g);

    GoalserveDevigSignal sig(f.goalserve, f.pm, f.games, BANKROLL_USDC);
    EXPECT_TRUE(sig.tick(make_ctx()).has_value());
}

// Cond 4: fill_rate < 0.50 → nullopt
TEST(GoalserveDevig_T4, Cond4_Fail_LowFillRate) {
    HappyFixture f;
    PmSnapshot ps;
    ps.mid = 0.40;
    ps.top3_liquidity_usdc = 5'000.0;
    ps.expected_fill_rate = 0.30;
    ps.valid = true;
    f.pm.put(MID, ps);

    GoalserveDevigSignal sig(f.goalserve, f.pm, f.games, BANKROLL_USDC);
    EXPECT_FALSE(sig.tick(make_ctx()).has_value());
}

// Cond 5: LiveSection == Delayed → nullopt
TEST(GoalserveDevig_T4, Cond5_Fail_Delayed) {
    HappyFixture f;
    GameState g;
    g.delayed = true;
    g.live = false;
    g.ended = false;
    g.kickoff_ts_ns = NOW + 2 * 60 * MIN_NS;
    f.games.put(MID, g);

    GoalserveDevigSignal sig(f.goalserve, f.pm, f.games, BANKROLL_USDC);
    EXPECT_FALSE(sig.tick(make_ctx()).has_value());
}

// Cond 5: LiveSection == Closed → nullopt
TEST(GoalserveDevig_T4, Cond5_Fail_Closed) {
    HappyFixture f;
    GameState g;
    g.ended = true;
    g.live = false;
    g.delayed = false;
    g.kickoff_ts_ns = NOW - 4 * 60 * MIN_NS;
    f.games.put(MID, g);

    GoalserveDevigSignal sig(f.goalserve, f.pm, f.games, BANKROLL_USDC);
    EXPECT_FALSE(sig.tick(make_ctx()).has_value());
}

// R-20: zero ts → nullopt
TEST(GoalserveDevig_T4, R20_ZeroTs) {
    HappyFixture f;
    GoalserveDevigSignal sig(f.goalserve, f.pm, f.games, BANKROLL_USDC);

    for (int i = 0; i < 4; ++i) {
        auto ctx = make_ctx();
        if (i == 0)
            ctx.event_ts_ns = 0;
        if (i == 1)
            ctx.data_source_ts_ns = 0;
        if (i == 2)
            ctx.ingestion_ts_ns = 0;
        if (i == 3)
            ctx.as_of_ts_ns = 0;
        EXPECT_FALSE(sig.tick(ctx).has_value()) << "i=" << i;
    }
}

// R-20: ts order violation → nullopt
TEST(GoalserveDevig_T4, R20_TsOrderViolation) {
    HappyFixture f;
    GoalserveDevigSignal sig(f.goalserve, f.pm, f.games, BANKROLL_USDC);

    // event > data_source
    auto ctx = make_ctx();
    ctx.event_ts_ns = NOW;
    ctx.data_source_ts_ns = NOW - SEC_NS;
    EXPECT_FALSE(sig.tick(ctx).has_value());

    // ingestion > as_of
    ctx = make_ctx();
    ctx.ingestion_ts_ns = NOW + SEC_NS;
    EXPECT_FALSE(sig.tick(ctx).has_value());
}

// R-20: empty market_id / feature_snapshot_id → nullopt
TEST(GoalserveDevig_T4, R20_EmptyKeys) {
    HappyFixture f;
    GoalserveDevigSignal sig(f.goalserve, f.pm, f.games, BANKROLL_USDC);

    auto ctx = make_ctx();
    ctx.market_id = "";
    EXPECT_FALSE(sig.tick(ctx).has_value());

    ctx = make_ctx();
    ctx.feature_snapshot_id = "";
    EXPECT_FALSE(sig.tick(ctx).has_value());
}

// size clip $5K
TEST(GoalserveDevig_T4, SizeClipAt5K) {
    HappyFixture f;
    // 极大 edge: PM_mid=0.05 → edge ~ 0.41
    PmSnapshot ps;
    ps.mid = 0.05;
    ps.top3_liquidity_usdc = 5'000.0;
    ps.expected_fill_rate = 0.95;
    ps.valid = true;
    f.pm.put(MID, ps);

    GoalserveDevigSignal sig(f.goalserve, f.pm, f.games, 1'000'000);
    auto const out = sig.tick(make_ctx());
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->suggested_size_usdc, 5'000);
    EXPECT_NEAR(out->confidence, 1.0, 1e-9);
}

// ===========================================================================
// T5: feature_snapshot_id ABI — enum 值锁定, ML column index 不变
// ===========================================================================

TEST(GoalserveDevig_T5, FeatureName_Enum_Values_ABI_Lock) {
    // 列 index = enum 值 — 锁死 (小邓 ML pipeline column index)
    // ADR-008 字段名改了, 但 index 4/5 不变
    EXPECT_EQ(static_cast<int>(FeatureName::Goalserve_devig_p_yes_fair), 4);
    EXPECT_EQ(static_cast<int>(FeatureName::Goalserve_overround_avg), 5);
}

TEST(GoalserveDevig_T5, FeatureName_ToStr_NewNames) {
    // 字段名 cascade: to_string 必须返回新名 (小邓 Parquet schema 列名)
    EXPECT_EQ(stcpp::ml::to_string(FeatureName::Goalserve_devig_p_yes_fair), "Goalserve_devig_p_yes_fair");
    EXPECT_EQ(stcpp::ml::to_string(FeatureName::Goalserve_overround_avg), "Goalserve_overround_avg");
}

TEST(GoalserveDevig_T5, FeatureSnapshot_SetGet_Devig_Fields) {
    // FeatureSnapshot set/get 用新 enum 正常工作
    stcpp::ml::FeatureSnapshot snap;
    snap.set(FeatureName::Goalserve_devig_p_yes_fair, 0.4615f);
    snap.set(FeatureName::Goalserve_overround_avg, 1.0317f);

    EXPECT_NEAR(snap.get(FeatureName::Goalserve_devig_p_yes_fair), 0.4615f, 1e-4f);
    EXPECT_NEAR(snap.get(FeatureName::Goalserve_overround_avg), 1.0317f, 1e-4f);
}

TEST(GoalserveDevig_T5, SignalContext_FieldsUnchanged_ABI) {
    // SignalContext 字段验证 (老周 ABI lock)
    SignalContext ctx = make_ctx();
    EXPECT_EQ(ctx.feature_snapshot_id, "snap-devig-001");
    EXPECT_FALSE(ctx.market_id.empty());
    EXPECT_GT(ctx.event_ts_ns, 0);
    EXPECT_GE(ctx.data_source_ts_ns, ctx.event_ts_ns);
    EXPECT_GE(ctx.ingestion_ts_ns, ctx.data_source_ts_ns);
    EXPECT_GE(ctx.as_of_ts_ns, ctx.ingestion_ts_ns);
}

TEST(GoalserveDevig_T5, SignalOutput_SignalId_ABI_Lock) {
    // signal_id ABI: GoalserveDevigSignal 仍报 P0_01_PinnacleNoVig (老周 lock)
    HappyFixture f;
    GoalserveDevigSignal sig(f.goalserve, f.pm, f.games, BANKROLL_USDC);
    auto const out = sig.tick(make_ctx());
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->signal_id, SignalId::P0_01_PinnacleNoVig);
    // ISignalEngine::id() 也一致
    EXPECT_EQ(sig.id(), SignalId::P0_01_PinnacleNoVig);
}

}  // namespace
