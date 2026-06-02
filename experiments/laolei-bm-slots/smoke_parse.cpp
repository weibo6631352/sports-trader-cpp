// 烟雾验证: getodds 解析 → bm_slots 定向 → 跨庄家 de-vig fair 全链。跑完即弃, 不进 ctest。
// 编译: g++ -std=c++20 -I../../include smoke_parse.cpp -o smoke && ./smoke /tmp/getodds_basket.xml
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "stcpp/data/bm_slots_fill.hpp"
#include "stcpp/data/odds_feed_parser.hpp"
#include "stcpp/ml/model_feature_spec.hpp"  // detail::devig_one

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <xml-file>\n", argv[0]);
        return 2;
    }
    std::ifstream f(argv[1]);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string xml = ss.str();
    std::printf("xml bytes=%zu\n", xml.size());

    using namespace stcpp::data::goalserve;
    namespace fs = stcpp::data::feature_store;
    const auto recs = ParseGetOddsXml(xml, GoalserveSport::Basketball, 1780400000000000000LL);
    std::printf("parsed matches=%zu\n", recs.size());

    for (const auto& r : recs) {
        std::printf("--- match_id=%s %s vs %s  valid_books=%d\n", r.match_id.c_str(),
                    r.home_team.c_str(), r.away_team.c_str(), r.valid_book_count());
        // 定向: YES=home (yes_is_home=true), 非平局盘
        fs::FeatureStoreGameRow g;
        FillBmSlotsYesCanonical(r, /*yes_is_home=*/true, /*map_is_draw=*/false, g);
        // 复现特征侧跨庄家 de-vig 共识 (model_feature_spec extract_from_game_row 逻辑)
        double fair_sum = 0.0, over_sum = 0.0;
        int n = 0;
        for (const auto& sl : g.bm_slots) {
            if (!sl.is_present()) continue;
            const auto d = stcpp::ml::detail::devig_one(sl.odds_yes, sl.odds_no);
            if (!d.ok) continue;
            std::printf("      bm: yes_odds=%.3f no_odds=%.3f -> fair_yes=%.4f overround=%.4f\n",
                        sl.odds_yes, sl.odds_no, d.fair_yes, d.overround);
            fair_sum += d.fair_yes;
            over_sum += d.overround;
            ++n;
        }
        if (n > 0)
            std::printf("   >>> 跨庄家共识 fair_yes=%.4f  overround_avg=%.4f  valid_bm_count=%d\n",
                        fair_sum / n, over_sum / n, n);
    }
    return 0;
}
