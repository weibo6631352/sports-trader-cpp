// market_taxonomy.hpp — Polymarket 真实市场/盘口分类 → categorical 整数码 (SSOT)。
//
// Owner: 老雷 (GM)
// last_review: 2026-05-31
//
// 依据真实 gamma /events 结构 (2026-05-31 实测, 非臆测):
//   - event.sport = 对象 {id, sport, ...}: sport.sport = 联赛码 (nba/bkcba/atp/wta/lol/...),
//     sport.id = 稳定整数 (nba=34, bkcba=104=CBA, wnba=6, atp=45, wta=46, mlb=8, nhl=35, ...)。
//     → cat_league 直接用 sport.id 原值 (天然区分 NBA vs CBA, ATP vs WTA; LightGBM categorical level)。
//   - market.sportsMarketType = 盘口类型 (moneyline/spreads/totals/child_moneyline/
//     tennis_match_totals/kill_over_under_game/map_handicap/...), 经 NormalizeSportsMarketType
//     归一 → moneyline/spread/totals/outright/prop/series/(raw 长尾)。
//   - event.tags = ["sports","nba","games","basketball"] → 大类 (sports/crypto/politics/esports)。
//
// ⚠ 这些是 categorical (非 ordinal): 整数码仅作 level 标识, 训练侧声明 categorical_feature。
//   unknown = -1 (独立 level, 非 NaN)。映射在此 app 层算一次, 热路径只搬运 int (零字符串解析)。
#ifndef STCPP_DATA_MARKET_TAXONOMY_HPP
#define STCPP_DATA_MARKET_TAXONOMY_HPP

#include <cstdint>
#include <string>
#include <string_view>

namespace stcpp::data::taxonomy {

// ---- 资产大类 (event.tags / electionType) ----
enum class AssetClass : std::int32_t { kUnknown = -1, kSports = 0, kCrypto = 1, kPolitics = 2, kEsports = 3 };

// ---- 盘口类型 (NormalizeSportsMarketType 归一值 → 码) ----
enum class MarketTypeCat : std::int32_t {
    kUnknown = -1, kMoneyline = 0, kSpread = 1, kTotals = 2, kOutright = 3, kProp = 4, kSeries = 5
};

// ---- 运动大类 (联赛码 string → 粗粒度家族; cat_league 才是细粒度 sport.id) ----
enum class SportFamily : std::int32_t {
    kUnknown = -1, kSoccer = 0, kBasketball = 1, kTennis = 2, kBaseball = 3,
    kHockey = 4, kAmFootball = 5, kEsports = 6, kMma = 7, kCricket = 8
};

// MarketTypeCode — NormalizeSportsMarketType 的归一字符串 (小写) → 盘口类型码。
//   归一已把 child_moneyline→moneyline, kill_over_under_game/tennis_*_totals→totals, *handicap→? 处理一部分;
//   此处兜底再扫一遍 (含归一未覆盖的 raw 长尾)。未知 → -1。
[[nodiscard]] inline std::int32_t MarketTypeCode(std::string_view normalized) noexcept {
    if (normalized == "moneyline") return 0;
    if (normalized == "spread") return 1;
    if (normalized == "totals") return 2;
    if (normalized == "outright") return 3;
    if (normalized == "prop") return 4;
    if (normalized == "series") return 5;
    // raw 长尾兜底 (归一未匹配时透传的原值, e.g. map_handicap / first_blood_game / lol_penta_kill)
    if (normalized.find("moneyline") != std::string_view::npos) return 0;
    if (normalized.find("handicap") != std::string_view::npos ||
        normalized.find("spread") != std::string_view::npos) return 1;
    if (normalized.find("total") != std::string_view::npos ||
        normalized.find("over_under") != std::string_view::npos) return 2;
    if (normalized.find("outright") != std::string_view::npos ||
        normalized.find("futures") != std::string_view::npos) return 3;
    if (!normalized.empty()) return 4;  // 其余具名 (esports specials: first_blood/penta_kill/...) 归 prop
    return -1;
}

// SportFamilyCode — Polymarket 联赛码 (sport.sport, e.g. "nba"/"bkcba"/"atp") → 运动家族码。
//   实测码: nba/wnba/bkcba=篮球, mlb/kbo=棒球, nhl=冰球, atp/wta/itf=网球, lol/dota2/es2/ccc=电竞,
//           fif/bra/chi/per1/j1100/j2100/swe/bra2=足球, ufc=mma, crint/cricipl/crict20blast=板球。
[[nodiscard]] inline std::int32_t SportFamilyCode(std::string_view code) noexcept {
    if (code.empty()) return -1;
    // 篮球: nba / wnba / bkcba(CBA) / 其他 bk* 前缀
    if (code == "nba" || code == "wnba" || code == "bkcba" || code.substr(0, 2) == "bk") return 1;
    // 棒球: mlb / kbo
    if (code == "mlb" || code == "kbo") return 3;
    // 冰球
    if (code == "nhl") return 4;
    // 美式足球
    if (code == "nfl" || code == "ncaaf") return 5;
    // 网球: atp / wta / itf
    if (code == "atp" || code == "wta" || code == "itf") return 2;
    // 电竞: lol / dota2 / cs2 / es2 / ccc (前缀 es / cs / 已知码)
    if (code == "lol" || code == "dota2" || code == "cs2" || code == "es2" || code == "ccc" ||
        code.substr(0, 2) == "es" || code.substr(0, 2) == "cs")
        return 6;
    // mma / ufc
    if (code == "ufc" || code == "mma") return 7;
    // 板球: cricipl / crict20blast / crint (前缀 cric)
    if (code.substr(0, 4) == "cric") return 8;
    // 足球: fif(FIFA) / bra / bra2 / chi(中超) / per1 / j1100 / j2100 / swe / crint? → 默认归足球家族
    //   (Polymarket 足球联赛码极多, 以联赛级 sport.id 区分; 家族粗粒度兜底足球)
    return 0;
}

}  // namespace stcpp::data::taxonomy

#endif  // STCPP_DATA_MARKET_TAXONOMY_HPP
