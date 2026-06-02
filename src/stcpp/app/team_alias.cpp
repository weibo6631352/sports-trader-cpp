// src/stcpp/app/team_alias.cpp — 按运动分派的队名规范化实现 (见头文件)
//
// 团队表覆盖: NBA(30) / MLB(30) / NFL(32) / NHL(32) 全队 (队名稳定可准确枚举)。
//   足球/板球等全球联盟队数上千, 暂返 "" 回退通用匹配 (诚实: 需按真实 Goalserve↔Polymarket
//   名字样本逐联盟建表, 列为后续数据组专项)。规范 ID = 联盟内唯一昵称 (跨运动同名如
//   panthers(NFL/NHL)/kings(NBA/NHL) 不冲突 —— 按 sport_code 分派到各自联盟表)。

#include "stcpp/app/team_alias.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <vector>

#include "stcpp/app/event_matcher.hpp"  // 复用 NormalizeTeamTokens (折叠变音符 + 分词)

namespace stcpp::app {

namespace {

// 一队 = 规范 ID + 若干别名组 (每组空格分隔的 token; 任一组全部 token 命中即解析到该队)。
struct TeamEntry {
    std::string id;
    std::vector<std::string> alias_groups;  // 如 {"red sox"} / {"76ers", "sixers"}
};

// 小写化 sport 码 (大小写不敏感分类)。
std::string Lower(std::string_view s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) o.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return o;
}

bool Contains(std::string_view hay, std::string_view needle) {
    return hay.find(needle) != std::string_view::npos;
}

// ---- 四大联盟队表 (规范 ID = 昵称; alias_groups 处理多词昵称如 "red sox") ----
const std::vector<TeamEntry>& NbaTeams() {
    static const std::vector<TeamEntry> t = {
        {"hawks", {"hawks"}}, {"celtics", {"celtics"}}, {"nets", {"nets"}}, {"hornets", {"hornets"}},
        {"bulls", {"bulls"}}, {"cavaliers", {"cavaliers", "cavs"}}, {"mavericks", {"mavericks", "mavs"}},
        {"nuggets", {"nuggets"}}, {"pistons", {"pistons"}}, {"warriors", {"warriors"}},
        {"rockets", {"rockets"}}, {"pacers", {"pacers"}}, {"clippers", {"clippers"}}, {"lakers", {"lakers"}},
        {"grizzlies", {"grizzlies"}}, {"heat", {"heat"}}, {"bucks", {"bucks"}},
        {"timberwolves", {"timberwolves", "wolves"}}, {"pelicans", {"pelicans"}}, {"knicks", {"knicks"}},
        {"thunder", {"thunder"}}, {"magic", {"magic"}}, {"76ers", {"76ers", "sixers"}}, {"suns", {"suns"}},
        {"blazers", {"blazers", "trail blazers"}}, {"kings", {"kings"}}, {"spurs", {"spurs"}},
        {"raptors", {"raptors"}}, {"jazz", {"jazz"}}, {"wizards", {"wizards"}},
    };
    return t;
}

const std::vector<TeamEntry>& MlbTeams() {
    static const std::vector<TeamEntry> t = {
        {"diamondbacks", {"diamondbacks", "dbacks"}}, {"braves", {"braves"}}, {"orioles", {"orioles"}},
        {"redsox", {"red sox"}}, {"whitesox", {"white sox"}}, {"cubs", {"cubs"}}, {"reds", {"reds"}},
        {"guardians", {"guardians", "indians"}}, {"rockies", {"rockies"}}, {"tigers", {"tigers"}},
        {"astros", {"astros"}}, {"royals", {"royals"}}, {"angels", {"angels"}}, {"dodgers", {"dodgers"}},
        {"marlins", {"marlins"}}, {"brewers", {"brewers"}}, {"twins", {"twins"}}, {"mets", {"mets"}},
        {"yankees", {"yankees"}}, {"athletics", {"athletics"}}, {"phillies", {"phillies"}},
        {"pirates", {"pirates"}}, {"padres", {"padres"}}, {"giants", {"giants"}}, {"mariners", {"mariners"}},
        {"cardinals", {"cardinals"}}, {"rays", {"rays"}}, {"rangers", {"rangers"}},
        {"bluejays", {"blue jays"}}, {"nationals", {"nationals", "nats"}},
    };
    return t;
}

const std::vector<TeamEntry>& NflTeams() {
    static const std::vector<TeamEntry> t = {
        {"cardinals", {"cardinals"}}, {"falcons", {"falcons"}}, {"ravens", {"ravens"}}, {"bills", {"bills"}},
        {"panthers", {"panthers"}}, {"bears", {"bears"}}, {"bengals", {"bengals"}}, {"browns", {"browns"}},
        {"cowboys", {"cowboys"}}, {"broncos", {"broncos"}}, {"lions", {"lions"}}, {"packers", {"packers"}},
        {"texans", {"texans"}}, {"colts", {"colts"}}, {"jaguars", {"jaguars", "jags"}}, {"chiefs", {"chiefs"}},
        {"raiders", {"raiders"}}, {"chargers", {"chargers"}}, {"rams", {"rams"}}, {"dolphins", {"dolphins"}},
        {"vikings", {"vikings"}}, {"patriots", {"patriots", "pats"}}, {"saints", {"saints"}},
        {"giants", {"giants"}}, {"jets", {"jets"}}, {"eagles", {"eagles"}}, {"steelers", {"steelers"}},
        {"49ers", {"49ers", "niners"}}, {"seahawks", {"seahawks"}}, {"buccaneers", {"buccaneers", "bucs"}},
        {"titans", {"titans"}}, {"commanders", {"commanders"}},
    };
    return t;
}

const std::vector<TeamEntry>& NhlTeams() {
    static const std::vector<TeamEntry> t = {
        {"ducks", {"ducks"}}, {"bruins", {"bruins"}}, {"sabres", {"sabres"}}, {"flames", {"flames"}},
        {"hurricanes", {"hurricanes", "canes"}}, {"blackhawks", {"blackhawks"}},
        {"avalanche", {"avalanche", "avs"}}, {"bluejackets", {"blue jackets"}}, {"stars", {"stars"}},
        {"redwings", {"red wings"}}, {"oilers", {"oilers"}}, {"panthers", {"panthers"}}, {"kings", {"kings"}},
        {"wild", {"wild"}}, {"canadiens", {"canadiens", "habs"}}, {"predators", {"predators", "preds"}},
        {"devils", {"devils"}}, {"islanders", {"islanders"}}, {"rangers", {"rangers"}},
        {"senators", {"senators", "sens"}}, {"flyers", {"flyers"}}, {"penguins", {"penguins", "pens"}},
        {"sharks", {"sharks"}}, {"kraken", {"kraken"}}, {"blues", {"blues"}}, {"lightning", {"lightning"}},
        {"mapleleafs", {"maple leafs", "leafs"}}, {"goldenknights", {"golden knights", "knights"}},
        {"canucks", {"canucks"}}, {"capitals", {"capitals", "caps"}}, {"jets", {"jets"}},
        {"utah", {"utah", "mammoth"}},
    };
    return t;
}

// sport_code → 联盟表 (nullptr = 该团队运动暂无表, 回退通用)。
const std::vector<TeamEntry>* LeagueFor(std::string_view sl) {
    if (Contains(sl, "nba") || Contains(sl, "wnba")) return &NbaTeams();
    if (Contains(sl, "mlb")) return &MlbTeams();
    if (Contains(sl, "nfl")) return &NflTeams();
    if (Contains(sl, "nhl")) return &NhlTeams();
    return nullptr;  // 足球/板球/大学等: 待真实样本建表, 暂回退通用
}

// group ("red sox") 的所有 token 是否都在 sorted+deduped 的 name_tokens 里。
bool GroupSubset(const std::string& group, const std::vector<std::string>& name_tokens) {
    // group 可能多词; 用 NormalizeTeamTokens 同口径切 (折叠+小写+alnum)。
    const std::vector<std::string> gt = EventMatcher::NormalizeTeamTokens(group);
    if (gt.empty()) return false;
    for (const auto& tk : gt) {
        if (!std::binary_search(name_tokens.begin(), name_tokens.end(), tk)) return false;
    }
    return true;
}

}  // namespace

SportMatchCategory ClassifySportMatch(std::string_view sport_code) {
    const std::string sl = Lower(sport_code);
    if (sl.empty()) return SportMatchCategory::kUnknown;
    // 个人项目: 网球各级别 / MMA / 拳击 / 高尔夫 / 飞镖 / 斯诺克。
    if (Contains(sl, "tennis") || Contains(sl, "atp") || Contains(sl, "wta") || Contains(sl, "itf") ||
        Contains(sl, "mma") || Contains(sl, "ufc") || Contains(sl, "box") || Contains(sl, "golf") ||
        Contains(sl, "dart") || Contains(sl, "snooker")) {
        return SportMatchCategory::kIndividual;
    }
    // 团队项目: 四大联盟 + 足球 (含各联赛码) + 板球 + 大学。
    if (Contains(sl, "nba") || Contains(sl, "wnba") || Contains(sl, "mlb") || Contains(sl, "nfl") ||
        Contains(sl, "nhl") || Contains(sl, "soccer") || Contains(sl, "football") || Contains(sl, "epl") ||
        Contains(sl, "laliga") || Contains(sl, "seriea") || Contains(sl, "bundesliga") ||
        Contains(sl, "ligue") || Contains(sl, "cricket") || Contains(sl, "ncaa") || Contains(sl, "cfb") ||
        Contains(sl, "cbb")) {
        return SportMatchCategory::kTeam;
    }
    return SportMatchCategory::kUnknown;
}

std::string CanonicalTeam(std::string_view sport_code, const std::string& name) {
    const std::vector<TeamEntry>* league = LeagueFor(Lower(sport_code));
    if (league == nullptr) return "";
    const std::vector<std::string> tokens = EventMatcher::NormalizeTeamTokens(name);  // 已 sorted+dedup
    if (tokens.empty()) return "";

    std::string hit;
    for (const auto& team : *league) {
        bool matched = false;
        for (const auto& g : team.alias_groups) {
            if (GroupSubset(g, tokens)) { matched = true; break; }
        }
        if (!matched) continue;
        if (!hit.empty() && hit != team.id) return "";  // 歧义 (多队命中) → fail-closed 回退
        hit = team.id;
    }
    return hit;
}

}  // namespace stcpp::app
