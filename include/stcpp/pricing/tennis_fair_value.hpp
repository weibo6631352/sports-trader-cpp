#pragma once
// include/stcpp/pricing/tennis_fair_value.hpp — 网球 totals/spreads 专属定价 (games/sets 制)
//
// Owner: 老雷 (GM) | last_review: 2026-06-03
//
// 背景 (老板「全盘口量化都接入,缺的都加」, 2026-06-03):
//   derivative_fair_value 的 totals/spreads 是【连续时钟】运动 (足球/篮球/冰球 等) 的终场分布模型,
//   不支持网球 (set/game 制, 无连续时钟)。但网球是 PM live 覆盖最大的运动 (84 场)。此处给网球
//   专属 games 制 totals/spreads 模型。
//
// 数据 (Goalserve tennis_scores: s1..s5 = 各盘已打局数; totalscore = 已赢盘数):
//   game_row.score_home_total/away_total = 已赢【盘】数 (YES-canonical)
//   game_row.score_home_games/away_games = 全场已打【局】数 (s1+..+s5, YES-canonical)
//
// 模型 (v1, 保守先验 — 量化后续用真实数据校准):
//   TOTALS (总局 O/U): E[终场总局] = 已打局 + E[剩余局]; 剩余局 = 当前盘剩余 + 剩余整盘×μ_set。
//     盘数不确定 (bo3 打 2 还是 3 盘 ≈ ±μ_set 局) 是方差主项。P(总局 > line) = Over。
//   SPREADS (让局): 终场局差 ≈ 当前局差 (保守不外推方向, 避免过拟合势头) + 对称剩余方差。
//     P(终场局差 > −line) = YES cover (line = YES 边让局, 负 = 让分方; 同 SpreadsFairYes 约定)。
//
// 关键 fail-closed: 终态 (已有人 to_win 盘) / 太早 (赛前·0 局) → invalid 不定价 (防垃圾)。
//   best-of: 默认 bo3 (大满贯男单 bo5 无可靠标识 → 用 bo3, 偏低估剩余 = 保守)。
//   μ_set/σ 是【可校准先验】, paper 期观测后用真实终场分布调 (注释标 TODO-CALIB)。

#include <algorithm>
#include <cmath>

#include "stcpp/data/feature_store_contract.hpp"
#include "stcpp/pricing/derivative_fair_value.hpp"  // DerivativeFairResult / NormalCdf / kProbClamp

namespace stcpp::pricing {

// ---- 网球先验常数 (TODO-CALIB: paper 期收集终场分布后校准) ----
inline constexpr double kTennisMuSet = 9.7;            // 平均每盘局数 (6-4/6-3/7-5/7-6 混合)
inline constexpr double kTennisCompletedSetGames = 9.2;  // 已完成盘均局 (估当前盘已打用)
inline constexpr double kTennisSigSet = 2.0;           // 每盘总局数 sd
inline constexpr double kTennisSigMarginSet = 4.0;     // 每盘局差 sd (6-0→6 / 7-6→1)
inline constexpr int kTennisSetsToWin = 2;             // bo3 (默认)

// TennisTotalsFairYes — 网球总局大小盘。yes_is_over=true → YES=Over。
[[nodiscard]] inline DerivativeFairResult TennisTotalsFairYes(
    const data::feature_store::FeatureStoreGameRow& g, double line, bool yes_is_over = true) noexcept {
    DerivativeFairResult out;
    if (!std::isfinite(line))
        return out;
    const int sets_y = g.score_home_total;
    const int sets_o = g.score_away_total;
    const int games_total = g.score_home_games + g.score_away_games;
    const int sets_done = sets_y + sets_o;
    if (std::max(sets_y, sets_o) >= kTennisSetsToWin)
        return out;  // 终态 → 不定价
    if (games_total <= 0 && sets_done == 0)
        return out;  // 太早 (赛前 / 刚开)

    // 当前盘已打局 (估) = 总局 − 已完成盘×完成盘均局; clamp 合理范围 [0,13]。
    double cur_set_games = static_cast<double>(games_total) - sets_done * kTennisCompletedSetGames;
    cur_set_games = std::clamp(cur_set_games, 0.0, 13.0);
    const double games_to_finish_cur = std::max(2.0, kTennisMuSet - cur_set_games);
    // 当前盘之后还要打几整盘 (bo3): 0-0 → 期望 ~1.5 整盘; 已打 ≥1 盘 → ~0.5。
    const double add_sets = (sets_done == 0) ? 1.5 : 0.5;
    const double e_total = static_cast<double>(games_total) + games_to_finish_cur + add_sets * kTennisMuSet;
    // 单位守卫 (2026-06-03 修 fair=0.999 垃圾): 本模型按【整场总局数】定价 (e_total~12-40 局)。网球 totals
    //   另有【总盘数】(line~2.5)/【分盘局数】(line~9.5) 等子盘口 — 拿它们的 line 比整场局数 e_total → NormalCdf
    //   饱和成 0/1 → fair=0.999 垃圾单。line 与 e_total 量级严重不符 → 非整场总局盘, 不定价 (市场 de-vig 兜底)。
    if (line < 0.5 * e_total || line > 2.0 * e_total)
        return out;
    // 方差: 盘数不确定 (是否多打一盘, Bernoulli var≈0.25 × μ²) + 剩余各盘局数方差。
    const double n_rem_sets = add_sets + 1.0;
    const double var = 0.25 * kTennisMuSet * kTennisMuSet + n_rem_sets * kTennisSigSet * kTennisSigSet;
    const double sig = std::sqrt(var);
    const double p_over = 1.0 - detail::NormalCdf((line - e_total) / sig);
    double p_yes = yes_is_over ? p_over : (1.0 - p_over);
    p_yes = std::clamp(p_yes, detail::kProbClamp, 1.0 - detail::kProbClamp);
    out.valid = true;
    out.p_yes = p_yes;
    return out;
}

// TennisSpreadsFairYes — 网球让局盘。line = YES 边让局 (负 = YES 让分方; 同 SpreadsFairYes 约定)。
//   YES cover = 终场局差 (YES − 对手) > −line。
[[nodiscard]] inline DerivativeFairResult TennisSpreadsFairYes(
    const data::feature_store::FeatureStoreGameRow& g, double line) noexcept {
    DerivativeFairResult out;
    if (!std::isfinite(line))
        return out;
    const int sets_y = g.score_home_total;
    const int sets_o = g.score_away_total;
    const int games_y = g.score_home_games;
    const int games_o = g.score_away_games;
    const int sets_done = sets_y + sets_o;
    if (std::max(sets_y, sets_o) >= kTennisSetsToWin)
        return out;  // 终态
    if (games_y + games_o <= 0 && sets_done == 0)
        return out;  // 太早

    // 终场局差 ≈ 当前局差 (v1 保守: 不外推剩余方向, 当前 margin 是主信号; 避免势头过拟合)。
    const double e_final_margin = static_cast<double>(games_y - games_o);
    // 剩余局差方差: 剩余整盘数 × 每盘局差方差。
    const double add_sets = (sets_done == 0) ? 1.5 : 0.5;
    const double n_rem_sets = add_sets + 1.0;
    const double sig = std::sqrt(n_rem_sets) * kTennisSigMarginSet;
    const double p_cover = 1.0 - detail::NormalCdf((-line - e_final_margin) / sig);
    out.valid = true;
    out.p_yes = std::clamp(p_cover, detail::kProbClamp, 1.0 - detail::kProbClamp);
    return out;
}

}  // namespace stcpp::pricing
