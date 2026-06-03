#pragma once
// include/stcpp/pricing/esports_fair_value.hpp — 电竞 maps totals/spreads 专属定价 (best-of-N maps 制)
//
// Owner: 老雷 (GM) | last_review: 2026-06-03
//
// 背景 (老板「全盘口量化都接入,缺的都加」, 2026-06-03):
//   电竞是 best-of-N maps 制 (BO3/BO5), totals=总图数 O/U, spreads=图让分。离散且数小 →
//   精确枚举系列赛结局分布 (无需正态近似)。比连续时钟/网球模型更干净。
//
// 数据: game_row.score_home_total/away_total = 已赢【图】数 (esports/home parser score 属性, YES-canonical)。
//   注: inplay-esports 的图数在 stats.Res 非 info.score → 那批为 0:0 (本模型对其 fail-closed);
//   esports/home 补充源带真图数 → 本模型对其生效。
//
// 模型: per-map P(YES 赢一图) = p (从已赢图数 Laplace 平滑 + clamp [0.35,0.65] 防过度自信)。
//   从当前 (a,b) 图递归枚举到系列结束 (有人达 maps_to_win) → 叶子的 (总图数, 图差) 分布。
//   TOTALS: P(总图数 > line) = Over。 SPREADS: P(终场图差 > −line) = YES cover。
//   best-of: 默认 BO3 (maps_to_win=2; 多数赛事)。BO5 暂用 BO3 近似 (round 字段未 plumb)。
//   fail-closed: 终态 (已有人达 to_win) / 太早 (0:0 且无信息) / 无 line → invalid。

#include <algorithm>
#include <cmath>
#include <vector>

#include "stcpp/data/feature_store_contract.hpp"
#include "stcpp/pricing/derivative_fair_value.hpp"  // DerivativeFairResult / kProbClamp

namespace stcpp::pricing {

inline constexpr int kEsportsMapsToWin = 2;  // BO3 默认

namespace edetail {
// 系列赛叶子结局 (枚举用): 总图数 + 图差 (yes − opp) + 概率。
struct MapsLeaf {
    int total_maps;
    int margin;
    double prob;
};
// 从 (a,b) 图递归枚举到结束 (有人达 to_win); p = YES 每图胜率。累积叶子分布。
inline void EnumMaps(int a, int b, int to_win, double p, double acc, std::vector<MapsLeaf>& out) {
    if (a >= to_win || b >= to_win) {
        out.push_back({a + b, a - b, acc});
        return;
    }
    EnumMaps(a + 1, b, to_win, p, acc * p, out);
    EnumMaps(a, b + 1, to_win, p, acc * (1.0 - p), out);
}
}  // namespace edetail

// EsportsTotalsFairYes — 总图数大小盘。yes_is_over=true → YES=Over。
[[nodiscard]] inline DerivativeFairResult EsportsTotalsFairYes(
    const data::feature_store::FeatureStoreGameRow& g, double line, bool yes_is_over = true) noexcept {
    DerivativeFairResult out;
    if (!std::isfinite(line))
        return out;
    // 单位守卫 (2026-06-03 修 fair=0.999 垃圾): 本模型按【总图数】定价 (BO_n 总图 ≤ 2k−1)。电竞 totals
    //   另有【总击杀】(line~27.5)/【总回合】等子盘口 — 拿击杀 line 比总图 (2-3) → 无 leaf 超过 → p_over=0
    //   → under 边 fair=0.999 垃圾单。line 远超最大可能总图 (BO7 也 ≤7) → 非总图盘, 不定价 (市场 de-vig 兜底)。
    //   阈值 9 取在「最大总图(BO7=7)」与「击杀/回合总数(≥15)」之间的安全间隔, 不误伤边界图数 line。
    if (line > 9.0)
        return out;
    const int maps_y = g.score_home_total;
    const int maps_o = g.score_away_total;
    const int maps_done = maps_y + maps_o;
    if (std::max(maps_y, maps_o) >= kEsportsMapsToWin)
        return out;  // 终态
    if (maps_done == 0)
        return out;  // 太早 / 无图数 (含 inplay-esports 的 0:0)
    // per-map p: Laplace 平滑 (赢图多 → p 略高), clamp 防过度自信。
    const double p = std::clamp((maps_y + 1.0) / (maps_done + 2.0), 0.35, 0.65);
    std::vector<edetail::MapsLeaf> leaves;
    edetail::EnumMaps(maps_y, maps_o, kEsportsMapsToWin, p, 1.0, leaves);
    double p_over = 0.0;
    for (const auto& lf : leaves)
        if (static_cast<double>(lf.total_maps) > line)
            p_over += lf.prob;
    double p_yes = yes_is_over ? p_over : (1.0 - p_over);
    p_yes = std::clamp(p_yes, detail::kProbClamp, 1.0 - detail::kProbClamp);
    out.valid = true;
    out.p_yes = p_yes;
    return out;
}

// EsportsSpreadsFairYes — 图让分盘。line = YES 边让图 (负 = YES 让分方)。YES cover = 终场图差 > −line。
[[nodiscard]] inline DerivativeFairResult EsportsSpreadsFairYes(
    const data::feature_store::FeatureStoreGameRow& g, double line) noexcept {
    DerivativeFairResult out;
    if (!std::isfinite(line))
        return out;
    const int maps_y = g.score_home_total;
    const int maps_o = g.score_away_total;
    const int maps_done = maps_y + maps_o;
    if (std::max(maps_y, maps_o) >= kEsportsMapsToWin)
        return out;  // 终态
    if (maps_done == 0)
        return out;  // 太早
    const double p = std::clamp((maps_y + 1.0) / (maps_done + 2.0), 0.35, 0.65);
    std::vector<edetail::MapsLeaf> leaves;
    edetail::EnumMaps(maps_y, maps_o, kEsportsMapsToWin, p, 1.0, leaves);
    double p_cover = 0.0;
    for (const auto& lf : leaves)
        if (static_cast<double>(lf.margin) > -line)  // 终场图差 > −line
            p_cover += lf.prob;
    out.valid = true;
    out.p_yes = std::clamp(p_cover, detail::kProbClamp, 1.0 - detail::kProbClamp);
    return out;
}

}  // namespace stcpp::pricing
