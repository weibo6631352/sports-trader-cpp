// derivative_fair_value.hpp — 派生盘口 (totals 大小分 / spreads 让分) 专属定价模型。
//
// Owner: 老雷 (GM) — 老板 2026-05-31「缺盘口模型就加盘口模型」
// last_review: 2026-05-31
//
// 背景: FairValueEstimator 是胜负盘 (moneyline) 语义 (score_diff→p_yes)。totals/spreads 语义不同,
//   套胜负盘模型会错误下单。此处给 totals/spreads 专属定价 — 终场分布建模 + line 比较:
//
//   TOTALS (大小分): 当前总分按节奏外推到终场总分分布 → P(终场总分 > line) = Over 概率。
//     高分运动 (篮球/橄榄球) 终场总分 ≈ 正态; 低分运动 (足球/冰球) 剩余进球 ≈ 泊松。
//   SPREADS (让分): 当前分差 + 剩余得分方差 → P(终场分差 > -line) = YES(favorite) cover 概率。
//     分差 ≈ 正态 (CLT); 低分运动用 Skellam 方差 = 两队剩余 λ 之和。
//
// 关键: 仅 in-play 有 edge (当前比分偏离 line 隐含节奏时); 赛前/太早 → 节奏外推不可靠 → invalid
//   (fail-closed, 不交易)。YES-canonical: game_row score_home = YES 边。totals YES=Over; spreads
//   YES=favorite (line 为 YES 边让分, 负=让分方)。dispersion 是可校准先验 (量化后续用数据调)。
#ifndef STCPP_PRICING_DERIVATIVE_FAIR_VALUE_HPP
#define STCPP_PRICING_DERIVATIVE_FAIR_VALUE_HPP

#include <cmath>
#include <string_view>

#include "stcpp/data/feature_store_contract.hpp"
#include "stcpp/data/goalserve_record.hpp"
#include "stcpp/pricing/fair_value_estimator.hpp"  // total_game_seconds

namespace stcpp::pricing {

// 派生盘口定价结果 (与 FairValueResult 同风格: valid=false → fail-closed 不交易)。
struct DerivativeFairResult {
    bool valid{false};
    double p_yes{0.5};  // YES 边公允概率 (totals: Over; spreads: favorite cover)
};

// 运动得分剖面 (决定 totals/spreads 分布形态; dispersion 为可校准先验)。
struct ScoringProfile {
    bool valid{false};        // false = 无时钟/不支持运动 (tennis/volleyball 等 set 制) → 不定价
    bool poisson{false};      // true = 低分泊松 (足球/冰球); false = 高分正态 (篮球/橄榄球)
    double total_dispersion{1.0};   // 终场总分 σ = dispersion × sqrt(剩余期望总分) [正态路径]
    double margin_dispersion{1.0};  // 终场分差 σ = dispersion × sqrt(剩余期望总分)
};

// 各运动得分剖面 (slug 见 feature_store_contract → SportInplaySlug)。先验值, 量化后续用历史校准。
[[nodiscard]] inline ScoringProfile ScoringProfileFor(std::string_view sport) noexcept {
    // 篮球: 高分 (终场 ~220), 正态。终场总分 σ≈16 → /sqrt(~110剩余)≈1.5; 分差 σ≈13 → ≈1.2。
    if (sport == "basket")
        return ScoringProfile{true, false, 1.5, 1.2};
    // 美式橄榄球: 中高分 (终场 ~45), 正态。总分 σ≈14, 分差 σ≈13。
    if (sport == "amfootball")
        return ScoringProfile{true, false, 2.0, 1.9};
    // 足球: 低分 (终场 ~2.7 球), 泊松。分差用 Skellam (方差=两 λ 和) → margin_dispersion=1.0。
    if (sport == "soccer")
        return ScoringProfile{true, true, 1.0, 1.0};
    // 冰球: 低分 (终场 ~6), 泊松。
    if (sport == "hockey")
        return ScoringProfile{true, true, 1.0, 1.0};
    // 棒球: 中低分 (终场 ~9 分), 泊松近似。
    if (sport == "baseball")
        return ScoringProfile{true, true, 1.0, 1.0};
    // 其余 (tennis/volleyball/esports 等 set/局 制, 无连续时钟) → 不支持 → invalid。
    return ScoringProfile{false, false, 1.0, 1.0};
}

namespace detail {

// 正态 CDF Φ(x) = 0.5·erfc(−x/√2) (无 Boost; 同 backtest/stats.hpp 口径)。
[[nodiscard]] inline double NormalCdf(double x) noexcept {
    return 0.5 * std::erfc(-x / std::sqrt(2.0));
}

// 泊松生存函数 P(X > k), X~Poisson(λ)。k 可半整数 (line − current)。
[[nodiscard]] inline double PoissonSf(double k, double lambda) noexcept {
    if (lambda <= 0.0)
        return (k < 0.0) ? 1.0 : 0.0;  // 无剩余得分 → 总分=current
    if (k < 0.0)
        return 1.0;  // X≥0 恒 > 负阈值
    const int n = static_cast<int>(std::floor(k));
    // 累加 CDF = Σ_{i=0}^{n} e^{−λ} λ^i / i! (n 小 — 泊松仅用于低分运动)
    double term = std::exp(-lambda);
    double cdf = term;
    for (int i = 1; i <= n; ++i) {
        term *= lambda / static_cast<double>(i);
        cdf += term;
    }
    return std::clamp(1.0 - cdf, 0.0, 1.0);
}

// 时钟分数 [0,1] (= elapsed/total; NotStarted→0, terminal→1, 无时钟→0)。同 BaselineFairValueModel 口径。
[[nodiscard]] inline double TimeFraction(const data::feature_store::FeatureStoreGameRow& g) noexcept {
    using stcpp::data::goalserve::TimeStatus;
    if (g.time_status == TimeStatus::NotStarted)
        return 0.0;
    if (stcpp::data::goalserve::IsTerminal(g.time_status))
        return 1.0;
    if (g.elapsed_sec < 0)
        return 0.0;
    const int total_sec = total_game_seconds(g.sport);
    if (total_sec <= 0)
        return 0.0;
    const double f = static_cast<double>(g.elapsed_sec) / static_cast<double>(total_sec);
    if (!std::isfinite(f) || f < 0.0)
        return 0.0;
    return (f > 1.0) ? 1.0 : f;
}

// 太早保护: 节奏外推需累积足够样本 (< 此分数 → 噪声主导 → 不定价)。
inline constexpr double kMinTimeFrac = 0.10;
inline constexpr double kProbClamp = 0.001;  // p_yes clamp [clamp, 1−clamp]

}  // namespace detail

// ---------------------------------------------------------------------------
// TotalsFairYes — 大小分 Over 概率。yes_is_over=true → YES=Over (Polymarket outcomes[0]=Over)。
// ---------------------------------------------------------------------------
[[nodiscard]] inline DerivativeFairResult TotalsFairYes(
    const data::feature_store::FeatureStoreGameRow& g, double line, bool yes_is_over = true) noexcept {
    DerivativeFairResult out;
    const ScoringProfile prof = ScoringProfileFor(g.sport);
    if (!prof.valid || !std::isfinite(line))
        return out;  // 不支持运动 / 无 line → fail-closed
    const double f = detail::TimeFraction(g);
    const double current = static_cast<double>(g.score_home_total) + static_cast<double>(g.score_away_total);
    const bool terminal = stcpp::data::goalserve::IsTerminal(g.time_status);
    if (terminal) {
        // 已定: 终场总分 = current, P(Over)=1 当 current>line 否则 0 (=line 算 push→0.5)。
        out.valid = true;
        const double p_over = (current > line) ? 1.0 : (current < line ? 0.0 : 0.5);
        out.p_yes = yes_is_over ? p_over : 1.0 - p_over;
        return out;
    }
    if (f < detail::kMinTimeFrac)
        return out;  // 太早 → 节奏外推不可靠 → fail-closed
    const double mu_final = current / f;             // 节奏外推终场总分
    const double expected_remaining = mu_final - current;  // = mu_final·(1−f)
    double p_over;
    if (prof.poisson) {
        // 终场 = current + Poisson(剩余期望); P(总分 > line) = P(Poisson > line − current)。
        p_over = detail::PoissonSf(line - current, std::max(expected_remaining, 0.0));
    } else {
        const double sigma = prof.total_dispersion * std::sqrt(std::max(expected_remaining, 1.0));
        p_over = 1.0 - detail::NormalCdf((line - mu_final) / sigma);  // P(终场 > line)
    }
    out.valid = true;
    p_over = std::clamp(p_over, detail::kProbClamp, 1.0 - detail::kProbClamp);
    out.p_yes = yes_is_over ? p_over : 1.0 - p_over;
    return out;
}

// ---------------------------------------------------------------------------
// SpreadsFairYes — 让分 cover 概率。line = YES 边让分 (favorite 负值)。
//   YES cover ⟺ 终场分差 (YES − opp) > −line。yes_is_favorite 仅语义注释 (公式对两符号通用)。
// ---------------------------------------------------------------------------
[[nodiscard]] inline DerivativeFairResult SpreadsFairYes(
    const data::feature_store::FeatureStoreGameRow& g, double line) noexcept {
    DerivativeFairResult out;
    const ScoringProfile prof = ScoringProfileFor(g.sport);
    if (!prof.valid || !std::isfinite(line))
        return out;
    const double f = detail::TimeFraction(g);
    const double margin =
        static_cast<double>(g.score_home_total) - static_cast<double>(g.score_away_total);  // YES − opp
    const double current_total =
        static_cast<double>(g.score_home_total) + static_cast<double>(g.score_away_total);
    const double threshold = -line;  // YES 需分差 > threshold 才 cover
    const bool terminal = stcpp::data::goalserve::IsTerminal(g.time_status);
    if (terminal) {
        out.valid = true;
        out.p_yes = (margin > threshold) ? (1.0 - detail::kProbClamp)
                                         : (margin < threshold ? detail::kProbClamp : 0.5);
        return out;
    }
    if (f < detail::kMinTimeFrac)
        return out;
    const double mu_total_final = current_total / f;
    const double expected_remaining = std::max(mu_total_final - current_total, 1.0);
    // 终场分差 ≈ 正态 (mu=current margin 鞅假设; σ ∝ sqrt(剩余总分) — 低分运动 Skellam 方差=两 λ 和)。
    const double sigma = prof.margin_dispersion * std::sqrt(expected_remaining);
    double p_cover = 1.0 - detail::NormalCdf((threshold - margin) / sigma);  // P(分差 > threshold)
    out.valid = true;
    out.p_yes = std::clamp(p_cover, detail::kProbClamp, 1.0 - detail::kProbClamp);
    return out;
}

// 派生盘口统一入口 (market_type_id: 1=spread, 2=totals; 其余 → invalid)。
[[nodiscard]] inline DerivativeFairResult DerivativeFairYes(
    const data::feature_store::FeatureStoreGameRow& g, std::int32_t market_type_id, double line) noexcept {
    if (market_type_id == 2)
        return TotalsFairYes(g, line, /*yes_is_over=*/true);
    if (market_type_id == 1)
        return SpreadsFairYes(g, line);
    return DerivativeFairResult{};  // moneyline/outright/prop/series — 不在此定价
}

}  // namespace stcpp::pricing

#endif  // STCPP_PRICING_DERIVATIVE_FAIR_VALUE_HPP
