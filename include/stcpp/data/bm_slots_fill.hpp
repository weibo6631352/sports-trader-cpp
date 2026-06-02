#pragma once
// stcpp/data/bm_slots_fill.hpp — MatchResultOdds → FeatureStoreGameRow.bm_slots 定向填充
//
// Owner: 老雷 (GM) | last_review: 2026-06-02
//
// 目的 (bm_slots 管线):
//   getodds 解析器产出中性 (home/draw/away) 跨庄家赔率 MatchResultOdds; 本助手按 PM 市场的
//   YES 取向 (yes_is_home) + 是否平局盘 (map_is_draw) 定向折成二元 YES/NO decimal 赔率,
//   填 FeatureStoreGameRow.bm_slots[] → g_bm_devig_p_yes(#5)/overround(#6)/valid_bm_count(#7)。
//
// 3-way → 2-way de-vig 语义 (老雷 2026-06-02; advisory 特征, 标准比例折法, 量化可后续精修):
//   特征侧 devig_one(odds_yes, odds_no) 做比例 de-vig: fair_yes = (1/oy)/(1/oy + 1/on)。
//   要让它对 3-way (Home/Draw/Away) 给出正确的二元 PM fair, "NO 腿" 必须是 YES 以外所有结果的
//   合并隐含赔率: on = 1 / (Σ 非YES腿的 1/odds)。这样 devig_one 等价于全 3-way 比例 de-vig 后取 P(YES)。
//     - 胜负盘 (map_is_draw=false): YES = yes_is_home?home:away 胜; NO = 其余(对手胜 [+ 平局])。
//     - 平局盘 (map_is_draw=true) : YES = 平局; NO = home 胜 + away 胜 合并。
//   2-way 运动 (basket/tennis…无 draw 腿, b.draw<=1): NO 直接取对手腿。

#include <cmath>
#include <cstddef>
#include <limits>

#include "stcpp/data/data_contract.hpp"          // kNumBookmakers
#include "stcpp/data/feature_store_contract.hpp"  // FeatureStoreGameRow / BookmakerOddsOptional
#include "stcpp/data/odds_feed_parser.hpp"        // MatchResultOdds

namespace stcpp::data::goalserve {

namespace bm_fill_detail {
// 合并多腿为单一"合并腿"decimal 赔率: combined = 1 / Σ(1/leg)。任一腿 <=1 视作无效跳过。
[[nodiscard]] inline double CombineOdds(double a, double b) noexcept {
    double inv = 0.0;
    if (a > 1.0) inv += 1.0 / a;
    if (b > 1.0) inv += 1.0 / b;
    return (inv > 0.0) ? (1.0 / inv) : 0.0;
}
}  // namespace bm_fill_detail

// MatchResultOdds → game_row.bm_slots (YES-canonical 二元 decimal)。
//   yes_is_home: PM YES 腿是否对应 home; map_is_draw: 该盘 YES 是否为"平局"结果。
//   只填 present 且折出的 yes/no 均 > 1.0 的家; 其余保持默认 (NaN/valid=false)。
inline void FillBmSlotsYesCanonical(const MatchResultOdds& mo, bool yes_is_home, bool map_is_draw,
                                    stcpp::data::feature_store::FeatureStoreGameRow& g) noexcept {
    using bm_fill_detail::CombineOdds;
    for (std::size_t i = 0; i < kNumBookmakers; ++i) {
        const BookResultOdds& b = mo.books[i];
        if (!b.present)  // present 保证 home>1 && away>1
            continue;
        const bool three_way = (b.draw > 1.0);

        double yes_odds = 0.0, no_odds = 0.0;
        if (map_is_draw) {
            // 平局盘: YES = 平局腿; NO = home 胜 + away 胜 合并。需有平局腿。
            if (!three_way)
                continue;  // 2-way 运动无平局盘, 跳过
            yes_odds = b.draw;
            no_odds = CombineOdds(b.home, b.away);
        } else {
            const double win = yes_is_home ? b.home : b.away;
            const double lose = yes_is_home ? b.away : b.home;
            yes_odds = win;
            no_odds = three_way ? CombineOdds(lose, b.draw) : lose;  // 3-way: NO=对手+平局合并
        }

        if (yes_odds > 1.0 && no_odds > 1.0) {
            g.bm_slots[i].odds_yes = yes_odds;
            g.bm_slots[i].odds_no = no_odds;
            g.bm_slots[i].valid = true;
        }
    }
}

}  // namespace stcpp::data::goalserve
