// 烟雾验证: getodds XML 解析器对真实 body 工作正常。跑完即弃, 不进 ctest。
// 编译: g++ -std=c++20 -I../../include smoke_parse.cpp -o smoke && ./smoke /tmp/getodds_basket.xml
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "stcpp/data/odds_feed_parser.hpp"

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
    const auto recs = ParseGetOddsXml(xml, GoalserveSport::Basketball, 1780400000000000000LL);
    std::printf("parsed matches=%zu\n", recs.size());

    for (const auto& r : recs) {
        std::printf("--- match_id=%s %s vs %s  valid_books=%d  odds_ts_ns=%lld\n",
                    r.match_id.c_str(), r.home_team.c_str(), r.away_team.c_str(),
                    r.valid_book_count(), static_cast<long long>(r.odds_ts_ns));
        for (std::size_t i = 0; i < kNumBookmakers; ++i) {
            const auto& b = r.books[i];
            if (b.present)
                std::printf("      [%zu] %-12s home=%.3f draw=%.3f away=%.3f\n",
                            i, std::string(kBookmakerNames[i]).c_str(), b.home, b.draw, b.away);
        }
    }
    return 0;
}
