// tests/unit/test_fair_value_estimator.cpp — FairValueEstimator 单测
//
// Owner: 小肖 (numerical-algorithms)
// last_review: 2026-05-29
//
// 覆盖:
//   T01: 归一性 — p_yes + p_no == 1.0 (± 1e-12) 跨多场景
//   T02: 边界 clamp — 极端比分差下 probs ∈ (kProbEps, kProbMax)
//   T03: 单调性 — 领先 home → p_yes > 0.5; 落后 → p_yes < 0.5
//   T04: NaN 防护 — elapsed_sec=-1 / microprice=NaN → valid=true, probs 有限
//   T05: 赛前先验 — NotStarted + 0:0 → p_yes ≈ 0.5 (对称先验)
//   T06: 时钟效应 — 相同比分, 时钟越靠后领先方优势越大
//   T07: 订单簿混合 — microprice 有效时 book_blend > 0, 且对 prior 有拉扯
//   T08: 无订单簿退化 — book_row=nullptr → book_blend=0, 纯先验
//   T09: NaN microprice → 退化纯先验 (book_blend=0)
//   T10: 均匀分布 safe_sigmoid(0) → p_yes ≈ 0.5 (对称性)
//   T11: normalize2 Kahan — 极端输入 (p0=1e-15, p1=1) 仍归一
//   T12: clamp_prob — 边界值 0.0 / 1.0 / NaN / Inf 输出在 (kProbEps, kProbMax)
//   T13: safe_sigmoid 精度 — 已知值 sigmoid(0) = 0.5, sigmoid(ln3) ≈ 0.75
//   T14: FairValueEstimator facade — 通过 BaselineFairValueModel 正常运行
//   T15: IFairValueModel 多态 — 自定义 model 覆盖接口替换
//
// 红线:
//   R-20 不在 Estimator 层验证时间戳 (调用方负责)
//   所有单测 noexcept path (fair_value_estimator.hpp 契约)

#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "stcpp/pricing/fair_value_estimator.hpp"

namespace {

using namespace stcpp::pricing;
using stcpp::data::feature_store::FeatureStoreBookRow;
using stcpp::data::feature_store::FeatureStoreGameRow;
using stcpp::data::goalserve::TimeStatus;

// ---------------------------------------------------------------------------
// 测试夹具辅助函数
// ---------------------------------------------------------------------------

// 构造一个满足 R-20 4 ts 链且 time_status=InPlay 的最小 GameRow
// elapsed_sec: 当前节已用秒 (设为 -1 表示无)
// sport: Goalserve inplay slug (小写, e.g., "soccer" / "basket" / "amfootball")
FeatureStoreGameRow make_game_row(int score_home, int score_away, int elapsed_sec_val = -1,
                                  TimeStatus ts = TimeStatus::InPlay, std::string sport_val = "soccer") {
    FeatureStoreGameRow row{};
    // R-20 4 ts — 单调非降, 供调用方调用 (Estimator 不校验, 单测保证正确)
    constexpr std::int64_t BASE_TS = 1'700'000'000'000'000'000LL;
    row.event_ts_ns = BASE_TS;
    row.data_source_ts_ns = BASE_TS + 1;
    row.ingestion_ts_ns = BASE_TS + 2;
    row.as_of_ts_ns = BASE_TS + 3;
    row.sport = std::move(sport_val);
    row.market_type = "Moneyline";
    row.match_id = "test_match_001";
    row.home_team = "HomeFC";
    row.away_team = "AwayFC";
    row.score_home_total = score_home;
    row.score_away_total = score_away;
    row.time_status = ts;
    row.elapsed_sec = elapsed_sec_val;
    return row;
}

// 构造 YES side BookRow (microprice 有效)
FeatureStoreBookRow make_book_row(double microprice_val) {
    FeatureStoreBookRow br{};
    constexpr std::int64_t BASE_TS = 1'700'000'000'000'000'000LL;
    br.event_ts_ns = BASE_TS;
    br.data_source_ts_ns = BASE_TS + 1;
    br.ingestion_ts_ns = BASE_TS + 2;
    br.as_of_ts_ns = BASE_TS + 3;
    br.market_id = "0xMARKET_001";
    br.token_side = "YES";
    br.microprice = microprice_val;
    return br;
}

// 归一性检查精度 (double 浮点求和误差约 1e-15)
constexpr double kNormTol = 1e-12;

// ---------------------------------------------------------------------------
// T01: 归一性 — 多场景 sum(probs) == 1.0
// ---------------------------------------------------------------------------
TEST(FairValueEstimator, T01_Normalization) {
    BaselineFairValueModel model;

    struct Case {
        int home;
        int away;
        int elapsed_sec;
        TimeStatus ts;
    };
    std::array<Case, 6> cases{{
        {0, 0, 0, TimeStatus::NotStarted},
        {1, 0, 2700, TimeStatus::InPlay},  // soccer 30min, home leads
        {0, 2, 5400, TimeStatus::InPlay},  // soccer 60min, away leads
        {3, 3, 0, TimeStatus::InPlay},     // 平局
        {5, 0, -1, TimeStatus::InPlay},    // 大差, 无时钟
        {0, 0, 0, TimeStatus::Ended},      // 结束
    }};

    for (auto const& c : cases) {
        auto const game = make_game_row(c.home, c.away, c.elapsed_sec, c.ts);
        FairValueResult r = model.estimate(game, nullptr);
        double const sum = r.probs[0] + r.probs[1];
        EXPECT_NEAR(sum, 1.0, kNormTol) << "home=" << c.home << " away=" << c.away;
        EXPECT_TRUE(r.valid);
    }
}

// ---------------------------------------------------------------------------
// T02: 边界 clamp — 极端比分差不溢出
// ---------------------------------------------------------------------------
TEST(FairValueEstimator, T02_BoundaryClamping) {
    BaselineFairValueModel model;

    // 篮球大分差 (home +50), 最大 log-odds = 0.30 * 50 = 15 → sigmoid ≈ 1
    auto game_blowout = make_game_row(50, 0, 4800, TimeStatus::InPlay, "basket");
    FairValueResult r_hi = model.estimate(game_blowout, nullptr);
    EXPECT_TRUE(r_hi.valid);
    // clamp_prob maps extreme values to [kProbEps, kProbMax] — use GE/LE
    EXPECT_GE(r_hi.probs[0], kProbEps);
    EXPECT_LE(r_hi.probs[0], kProbMax);
    EXPECT_GE(r_hi.probs[1], kProbEps);
    EXPECT_LE(r_hi.probs[1], kProbMax);

    // 反向: away +50
    auto game_down = make_game_row(0, 50, 4800, TimeStatus::InPlay, "basket");
    FairValueResult r_lo = model.estimate(game_down, nullptr);
    EXPECT_TRUE(r_lo.valid);
    EXPECT_LT(r_lo.probs[0], 0.5);
    EXPECT_GT(r_lo.probs[1], 0.5);
    EXPECT_GE(r_lo.probs[0], kProbEps);
    EXPECT_LE(r_lo.probs[0], kProbMax);

    // 极端差值 (score_diff = 200, log_odds = 0.30 * 200 = 60)
    // safe_sigmoid(60) 应返回 kProbMax 而非 Inf
    auto game_extreme = make_game_row(200, 0, 0, TimeStatus::InPlay);
    FairValueResult r_ext = model.estimate(game_extreme, nullptr);
    EXPECT_TRUE(r_ext.valid);
    EXPECT_TRUE(std::isfinite(r_ext.probs[0]));
    EXPECT_TRUE(std::isfinite(r_ext.probs[1]));
}

// ---------------------------------------------------------------------------
// T03: 单调性 — 领先方 p_yes > 0.5
// ---------------------------------------------------------------------------
TEST(FairValueEstimator, T03_Monotonicity) {
    // alpha = 0.30 (默认), beta = 0 (赛前/无时钟) → 纯比分先验
    ScorePriorParams params{0.30, 0.0};
    BaselineFairValueModel model{params, 0.0};  // book_blend=0 → 纯先验

    // home leads → p_yes > 0.5
    auto game_lead = make_game_row(2, 0, -1, TimeStatus::InPlay);
    FairValueResult r_lead = model.estimate(game_lead, nullptr);
    EXPECT_TRUE(r_lead.valid);
    EXPECT_GT(r_lead.probs[0], 0.5) << "home leads → p_yes > 0.5";

    // away leads → p_yes < 0.5
    auto game_trail = make_game_row(0, 2, -1, TimeStatus::InPlay);
    FairValueResult r_trail = model.estimate(game_trail, nullptr);
    EXPECT_TRUE(r_trail.valid);
    EXPECT_LT(r_trail.probs[0], 0.5) << "away leads → p_yes < 0.5";

    // home leads more → higher p_yes than smaller lead
    auto game_big = make_game_row(3, 0, -1, TimeStatus::InPlay);
    auto game_small = make_game_row(1, 0, -1, TimeStatus::InPlay);
    FairValueResult r_big = model.estimate(game_big, nullptr);
    FairValueResult r_small = model.estimate(game_small, nullptr);
    EXPECT_GT(r_big.probs[0], r_small.probs[0]) << "+3 home lead → higher p_yes than +1";

    // 单调时钟效应: 相同领先分, 比赛越靠后优势越大
    ScorePriorParams params2{0.30, 0.50};
    BaselineFairValueModel model2{params2, 0.0};
    auto game_early = make_game_row(1, 0, 900, TimeStatus::InPlay);  // 15min
    auto game_late = make_game_row(1, 0, 4500, TimeStatus::InPlay);  // 75min
    FairValueResult r_early = model2.estimate(game_early, nullptr);
    FairValueResult r_late = model2.estimate(game_late, nullptr);
    EXPECT_GT(r_late.probs[0], r_early.probs[0]) << "same lead, later clock → higher p_yes (beta effect)";
}

// ---------------------------------------------------------------------------
// T04: NaN 防护 — elapsed_sec=-1, game row 无时钟 → valid=true
// ---------------------------------------------------------------------------
TEST(FairValueEstimator, T04_NanGuard_NoElapsed) {
    BaselineFairValueModel model;

    // elapsed_sec = -1 (feature_store 约定: -1 = 无)
    auto game = make_game_row(1, 0, -1, TimeStatus::InPlay);
    FairValueResult r = model.estimate(game, nullptr);
    EXPECT_TRUE(r.valid);
    EXPECT_TRUE(std::isfinite(r.probs[0]));
    EXPECT_TRUE(std::isfinite(r.probs[1]));
    EXPECT_NEAR(r.probs[0] + r.probs[1], 1.0, kNormTol);

    // score_home/away 都是 0, elapsed -1 → 退化均匀先验
    auto game_zero = make_game_row(0, 0, -1, TimeStatus::InPlay);
    FairValueResult r_zero = model.estimate(game_zero, nullptr);
    EXPECT_TRUE(r_zero.valid);
    // 0:0, no clock → score_diff=0, time_frac=0 → sigmoid(0) = 0.5
    EXPECT_NEAR(r_zero.probs[0], 0.5, 1e-6);
}

// ---------------------------------------------------------------------------
// T05: 赛前先验 — NotStarted + 0:0 → p_yes ≈ 0.5
// ---------------------------------------------------------------------------
TEST(FairValueEstimator, T05_PreMatchPrior) {
    // alpha > 0, beta > 0, 但 score_diff=0, time_frac=0 → sigmoid(0) = 0.5
    BaselineFairValueModel model;

    auto game = make_game_row(0, 0, 0, TimeStatus::NotStarted);
    FairValueResult r = model.estimate(game, nullptr);
    EXPECT_TRUE(r.valid);
    EXPECT_NEAR(r.probs[0], 0.5, 1e-6) << "pre-match 0:0 → symmetric prior";
    EXPECT_NEAR(r.probs[1], 0.5, 1e-6);
    EXPECT_NEAR(r.book_blend, 0.0, 1e-12) << "no book_row → book_blend=0";
}

// ---------------------------------------------------------------------------
// T06: 时钟效应单调性
// ---------------------------------------------------------------------------
TEST(FairValueEstimator, T06_ClockEffect_Monotonicity) {
    // beta > 0: 同等领先, 时钟越靠后概率越高
    ScorePriorParams params{0.30, 0.50};
    BaselineFairValueModel model{params, 0.0};  // book_blend=0

    // Soccer: total_game_seconds = 90 * 60 = 5400
    std::array<int, 5> times = {0, 1350, 2700, 4050, 5400};  // 0%, 25%, 50%, 75%, 100%
    double prev_p_yes = -1.0;
    for (int t : times) {
        auto game = make_game_row(1, 0, t, TimeStatus::InPlay);
        FairValueResult r = model.estimate(game, nullptr);
        EXPECT_TRUE(r.valid);
        if (prev_p_yes >= 0.0) {
            EXPECT_GE(r.probs[0], prev_p_yes)
                << "p_yes should be non-decreasing with time (1:0, beta=0.5) at t=" << t;
        }
        prev_p_yes = r.probs[0];
    }
}

// ---------------------------------------------------------------------------
// T07: 订单簿混合 — microprice 有效时 book_blend > 0
// ---------------------------------------------------------------------------
TEST(FairValueEstimator, T07_BookBlend_Applied) {
    // book_blend = 0.20 (default)
    BaselineFairValueModel model;

    auto game = make_game_row(0, 0, 0, TimeStatus::NotStarted);
    // microprice = 0.60 (market 认为 YES 概率高)
    auto book = make_book_row(0.60);

    FairValueResult r_with_book = model.estimate(game, &book);
    FairValueResult r_no_book = model.estimate(game, nullptr);

    EXPECT_TRUE(r_with_book.valid);
    EXPECT_NEAR(r_with_book.book_blend, 0.20, 1e-12) << "valid microprice → book_blend=0.20";

    // 混合后 p_yes 应该在 prior (0.5) 和 microprice (0.6) 之间
    // p_yes_adj = 0.2 * 0.6 + 0.8 * 0.5 = 0.12 + 0.40 = 0.52
    EXPECT_GT(r_with_book.probs[0], r_no_book.probs[0]) << "microprice > prior → p_yes should increase";
    EXPECT_NEAR(r_with_book.probs[0], 0.52, 1e-6);

    // 归一性仍然成立
    EXPECT_NEAR(r_with_book.probs[0] + r_with_book.probs[1], 1.0, kNormTol);
}

// ---------------------------------------------------------------------------
// T08: 无订单簿退化纯先验
// ---------------------------------------------------------------------------
TEST(FairValueEstimator, T08_NullBookRow_PurePrior) {
    BaselineFairValueModel model;

    auto game = make_game_row(1, 0, 2700, TimeStatus::InPlay);
    FairValueResult r = model.estimate(game, nullptr);
    EXPECT_TRUE(r.valid);
    EXPECT_NEAR(r.book_blend, 0.0, 1e-12) << "nullptr book_row → book_blend=0";
    // probs[0] 应等于纯先验 (prior_yes)
    EXPECT_NEAR(r.probs[0], r.prior_yes, 1e-6) << "no book → probs[0] == prior_yes";
}

// ---------------------------------------------------------------------------
// T09: NaN microprice → 退化纯先验
// ---------------------------------------------------------------------------
TEST(FairValueEstimator, T09_NaN_Microprice_Fallback) {
    BaselineFairValueModel model;

    auto game = make_game_row(0, 0, 0, TimeStatus::NotStarted);

    // NaN microprice
    auto book_nan = make_book_row(std::numeric_limits<double>::quiet_NaN());
    FairValueResult r_nan = model.estimate(game, &book_nan);
    EXPECT_TRUE(r_nan.valid);
    EXPECT_NEAR(r_nan.book_blend, 0.0, 1e-12) << "NaN microprice → book_blend=0";

    // Inf microprice
    auto book_inf = make_book_row(std::numeric_limits<double>::infinity());
    FairValueResult r_inf = model.estimate(game, &book_inf);
    EXPECT_TRUE(r_inf.valid);
    EXPECT_NEAR(r_inf.book_blend, 0.0, 1e-12) << "Inf microprice → book_blend=0";

    // microprice = 0 (边界, 无效)
    auto book_zero = make_book_row(0.0);
    FairValueResult r_zero = model.estimate(game, &book_zero);
    EXPECT_TRUE(r_zero.valid);
    EXPECT_NEAR(r_zero.book_blend, 0.0, 1e-12) << "microprice=0 → book_blend=0";

    // microprice = 1.0 (边界, 无效)
    auto book_one = make_book_row(1.0);
    FairValueResult r_one = model.estimate(game, &book_one);
    EXPECT_TRUE(r_one.valid);
    EXPECT_NEAR(r_one.book_blend, 0.0, 1e-12) << "microprice=1 → book_blend=0";

    // NO side → 不接受 (只接受 YES side microprice)
    auto book_no = make_book_row(0.45);
    book_no.token_side = "NO";
    FairValueResult r_no = model.estimate(game, &book_no);
    EXPECT_NEAR(r_no.book_blend, 0.0, 1e-12) << "NO side book_row → book_blend=0";
}

// ---------------------------------------------------------------------------
// T10: safe_sigmoid 精度 (known values)
// ---------------------------------------------------------------------------
TEST(FairValueEstimator, T10_SafeSigmoid) {
    // sigmoid(0) = 0.5 (exact)
    EXPECT_NEAR(safe_sigmoid(0.0), 0.5, 1e-15);

    // sigmoid(ln(3)) = 3 / (1 + 3) = 0.75
    double const ln3 = std::log(3.0);
    EXPECT_NEAR(safe_sigmoid(ln3), 0.75, 1e-12);

    // sigmoid(-ln(3)) = 1 / (1 + 3) = 0.25
    EXPECT_NEAR(safe_sigmoid(-ln3), 0.25, 1e-12);

    // 大值 → kProbMax (不 Inf)
    EXPECT_NEAR(safe_sigmoid(1000.0), kProbMax, 1e-12);
    EXPECT_NEAR(safe_sigmoid(-1000.0), kProbEps, 1e-12);

    // NaN 输入
    double const nan_val = std::numeric_limits<double>::quiet_NaN();
    double const r_nan = safe_sigmoid(nan_val);
    EXPECT_TRUE(std::isfinite(r_nan));
    EXPECT_GE(r_nan, kProbEps);
    EXPECT_LE(r_nan, kProbMax);

    // Inf 输入
    double const inf_val = std::numeric_limits<double>::infinity();
    EXPECT_EQ(safe_sigmoid(inf_val), kProbMax);
    EXPECT_EQ(safe_sigmoid(-inf_val), kProbEps);
}

// ---------------------------------------------------------------------------
// T11: normalize2 极端输入
// ---------------------------------------------------------------------------
TEST(FairValueEstimator, T11_Normalize2_Extreme) {
    // 正常输入
    auto n1 = normalize2(0.7, 0.3);
    EXPECT_NEAR(n1[0] + n1[1], 1.0, kNormTol);
    EXPECT_NEAR(n1[0], 0.7, 1e-12);

    // 非常小的一方 — clamp_prob 将 1e-15 映射到 kProbEps, 所以用 GE
    auto n2 = normalize2(0.9999, 1e-15);
    EXPECT_NEAR(n2[0] + n2[1], 1.0, kNormTol);
    EXPECT_GE(n2[0], kProbEps);
    EXPECT_GE(n2[1], kProbEps);

    // 两个 NaN → 均匀分布
    double const nan_v = std::numeric_limits<double>::quiet_NaN();
    auto n3 = normalize2(nan_v, nan_v);
    EXPECT_NEAR(n3[0], 0.5, 1e-12);
    EXPECT_NEAR(n3[1], 0.5, 1e-12);

    // 一个 NaN → 均匀分布
    auto n4 = normalize2(0.6, nan_v);
    EXPECT_NEAR(n4[0], 0.5, 1e-12);

    // Inf, Inf → 均匀分布
    double const inf_v = std::numeric_limits<double>::infinity();
    auto n5 = normalize2(inf_v, inf_v);
    EXPECT_NEAR(n5[0], 0.5, 1e-12);

    // 相等概率 → 0.5 各
    auto n6 = normalize2(1.0, 1.0);
    EXPECT_NEAR(n6[0], 0.5, 1e-12);
    EXPECT_NEAR(n6[1], 0.5, 1e-12);
}

// ---------------------------------------------------------------------------
// T12: clamp_prob 边界
// ---------------------------------------------------------------------------
TEST(FairValueEstimator, T12_ClampProb) {
    double const nan_v = std::numeric_limits<double>::quiet_NaN();
    double const inf_v = std::numeric_limits<double>::infinity();

    // NaN → 0.5
    EXPECT_NEAR(clamp_prob(nan_v), 0.5, 1e-12);

    // +Inf ≥ kProbMax → kProbMax; -Inf ≤ kProbEps → kProbEps
    EXPECT_EQ(clamp_prob(inf_v), kProbMax);
    EXPECT_EQ(clamp_prob(-inf_v), kProbEps);

    // 0.0 → kProbEps
    EXPECT_EQ(clamp_prob(0.0), kProbEps);

    // 1.0 → kProbMax
    EXPECT_EQ(clamp_prob(1.0), kProbMax);

    // 有效内部值不变
    EXPECT_NEAR(clamp_prob(0.5), 0.5, 1e-15);
    EXPECT_NEAR(clamp_prob(0.3), 0.3, 1e-15);
}

// ---------------------------------------------------------------------------
// T13: FairValueEstimator facade 正常运行
// ---------------------------------------------------------------------------
TEST(FairValueEstimator, T13_Facade_Works) {
    BaselineFairValueModel model;
    FairValueEstimator estimator{model};

    auto game = make_game_row(2, 1, 3000, TimeStatus::InPlay);
    auto book = make_book_row(0.65);

    // 直接调用 estimate (有 book_row)
    FairValueResult r = estimator.estimate(game, &book);
    EXPECT_TRUE(r.valid);
    EXPECT_NEAR(r.probs[0] + r.probs[1], 1.0, kNormTol);

    // 无 book_row 版本
    FairValueResult r2 = estimator.estimate(game);
    EXPECT_TRUE(r2.valid);
    EXPECT_NEAR(r2.book_blend, 0.0, 1e-12);
}

// ---------------------------------------------------------------------------
// T14: IFairValueModel 多态替换 (自定义 stub model)
// ---------------------------------------------------------------------------
namespace {

// 简单 stub: 始终返回 p_yes = 0.70 (测试多态替换接口)
class FixedFairValueModel final : public IFairValueModel {
public:
    explicit FixedFairValueModel(double fixed_p_yes) noexcept : fixed_{fixed_p_yes} {}

    [[nodiscard]] FairValueResult estimate(FeatureStoreGameRow const& /*game_row*/,
                                           FeatureStoreBookRow const* /*book_row*/) const noexcept override {
        FairValueResult r{};
        auto normed = normalize2(fixed_, 1.0 - fixed_);
        r.probs[0] = normed[0];
        r.probs[1] = normed[1];
        r.prior_yes = fixed_;
        r.book_blend = 0.0;
        r.valid = true;
        return r;
    }

private:
    double fixed_{0.5};
};

}  // anonymous namespace

TEST(FairValueEstimator, T14_PolymorphicModel) {
    FixedFairValueModel fixed_model{0.70};
    FairValueEstimator estimator{fixed_model};

    auto game = make_game_row(0, 0, 0, TimeStatus::NotStarted);
    FairValueResult r = estimator.estimate(game, nullptr);
    EXPECT_TRUE(r.valid);
    EXPECT_NEAR(r.probs[0], 0.70, 1e-6) << "fixed model should return 0.70";
    EXPECT_NEAR(r.probs[1], 0.30, 1e-6);
    EXPECT_NEAR(r.probs[0] + r.probs[1], 1.0, kNormTol);
}

// ---------------------------------------------------------------------------
// T15: total_game_seconds 运动映射
// ---------------------------------------------------------------------------
TEST(FairValueEstimator, T15_TotalGameSeconds) {
    // sport 字段来自 SportInplaySlug() — 小写 slug
    EXPECT_EQ(total_game_seconds("soccer"), 90 * 60);
    EXPECT_EQ(total_game_seconds("basket"), 48 * 60);  // inplay slug = "basket"
    EXPECT_EQ(total_game_seconds("amfootball"), 60 * 60);
    EXPECT_EQ(total_game_seconds("hockey"), 60 * 60);
    EXPECT_EQ(total_game_seconds("baseball"), 0);       // 无时钟
    EXPECT_EQ(total_game_seconds("tennis"), 0);         // 无时钟
    EXPECT_EQ(total_game_seconds("unknown_sport"), 0);  // 未知退化

    // 棒球/网球 → time_frac=0 (纯比分先验)
    BaselineFairValueModel model;
    auto baseball_game = make_game_row(3, 1, 3600, TimeStatus::InPlay, "baseball");
    FairValueResult r = model.estimate(baseball_game, nullptr);
    EXPECT_TRUE(r.valid);
    // time_frac=0, score_diff=2 → prior_yes = sigmoid(0.30 * 2) ≈ sigmoid(0.60) ≈ 0.6457
    double const expected = safe_sigmoid(0.30 * 2.0);
    EXPECT_NEAR(r.prior_yes, expected, 1e-12);
}

}  // anonymous namespace
