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

// 全球足球俱乐部表 (一张表跨赛事共用: 同一俱乐部在 EPL/UCL/MLS... 名字相同)。
//   覆盖五大联赛(英超/西甲/意甲/德甲/法甲)+ 荷甲/葡超/苏超/土超 + 南美/中东/MLS 豪门。
//   同城多队靠多词消歧 (manchester city / manchester united; real madrid / atletico madrid;
//   ac milan / inter milan); 已审 bare-token 无跨队碰撞 (如 Inter Milan 用 "inter milan" 非裸
//   "inter" → 不撞 Inter Miami)。缩写/绰号尽量收全 (Spurs/Wolves/Barca/Juve/BVB/PSG...)。
const std::vector<TeamEntry>& SoccerTeams() {
    static const std::vector<TeamEntry> t = {
        // ---- 英超 EPL ----
        {"arsenal", {"arsenal", "gunners"}}, {"astonvilla", {"aston villa"}},
        {"bournemouth", {"bournemouth"}}, {"brentford", {"brentford"}}, {"brighton", {"brighton"}},
        {"chelsea", {"chelsea"}}, {"crystalpalace", {"crystal palace"}}, {"everton", {"everton"}},
        {"fulham", {"fulham"}}, {"ipswich", {"ipswich"}}, {"leicester", {"leicester"}},
        {"liverpool", {"liverpool"}}, {"mancity", {"manchester city", "man city"}},
        {"manutd", {"manchester united", "man united", "man utd", "manchester utd"}},
        {"newcastle", {"newcastle"}}, {"nottingham", {"nottingham forest", "forest"}},
        {"southampton", {"southampton"}}, {"tottenham", {"tottenham", "spurs"}},
        {"westham", {"west ham"}}, {"wolves", {"wolves", "wolverhampton"}},
        // ---- 西甲 La Liga ----
        {"realmadrid", {"real madrid"}}, {"barcelona", {"barcelona", "barca"}},
        {"atletico", {"atletico madrid", "atletico", "atleti"}},
        {"athleticbilbao", {"athletic bilbao", "athletic club", "ath bilbao"}},
        {"realsociedad", {"real sociedad"}}, {"betis", {"real betis", "betis"}},
        {"villarreal", {"villarreal"}}, {"valencia", {"valencia"}}, {"sevilla", {"sevilla"}},
        {"girona", {"girona"}}, {"osasuna", {"osasuna"}}, {"getafe", {"getafe"}},
        {"celta", {"celta vigo", "celta"}}, {"mallorca", {"mallorca"}}, {"laspalmas", {"las palmas"}},
        {"rayo", {"rayo vallecano", "rayo"}}, {"espanyol", {"espanyol"}}, {"leganes", {"leganes"}},
        {"valladolid", {"valladolid"}}, {"alaves", {"alaves"}},
        // ---- 意甲 Serie A ----
        {"inter", {"inter milan", "internazionale"}}, {"acmilan", {"ac milan"}},
        {"juventus", {"juventus", "juve"}}, {"napoli", {"napoli"}}, {"roma", {"as roma", "roma"}},
        {"lazio", {"lazio"}}, {"atalanta", {"atalanta"}}, {"fiorentina", {"fiorentina"}},
        {"bologna", {"bologna"}}, {"torino", {"torino"}}, {"udinese", {"udinese"}},
        {"genoa", {"genoa"}}, {"como", {"como"}}, {"cagliari", {"cagliari"}}, {"parma", {"parma"}},
        {"lecce", {"lecce"}}, {"verona", {"hellas verona", "verona"}}, {"empoli", {"empoli"}},
        {"venezia", {"venezia"}}, {"monza", {"monza"}},
        // ---- 德甲 Bundesliga ----
        {"bayern", {"bayern munich", "bayern", "bayern munchen"}},
        {"leverkusen", {"bayer leverkusen", "leverkusen"}},
        {"dortmund", {"borussia dortmund", "dortmund", "bvb"}}, {"leipzig", {"rb leipzig", "leipzig"}},
        {"stuttgart", {"stuttgart"}}, {"frankfurt", {"eintracht frankfurt", "frankfurt"}},
        {"hoffenheim", {"hoffenheim"}}, {"freiburg", {"freiburg"}}, {"wolfsburg", {"wolfsburg"}},
        {"mainz", {"mainz"}}, {"augsburg", {"augsburg"}},
        {"gladbach", {"borussia monchengladbach", "monchengladbach", "gladbach"}},
        {"werder", {"werder bremen", "werder"}}, {"unionberlin", {"union berlin"}},
        {"bochum", {"bochum"}}, {"heidenheim", {"heidenheim"}}, {"stpauli", {"st pauli"}},
        {"holsteinkiel", {"holstein kiel"}},
        // ---- 法甲 Ligue 1 ----
        {"psg", {"psg", "paris saint germain", "paris sg", "paris"}},
        {"marseille", {"marseille", "olympique marseille"}}, {"monaco", {"monaco"}},
        {"lille", {"lille"}}, {"lyon", {"lyon", "olympique lyonnais"}}, {"nice", {"nice"}},
        {"lens", {"lens"}}, {"rennes", {"rennes"}}, {"strasbourg", {"strasbourg"}},
        {"brest", {"brest"}}, {"toulouse", {"toulouse"}}, {"reims", {"reims"}}, {"nantes", {"nantes"}},
        {"montpellier", {"montpellier"}}, {"lehavre", {"le havre"}}, {"auxerre", {"auxerre"}},
        {"angers", {"angers"}}, {"saintetienne", {"saint etienne", "st etienne"}},
        // ---- 荷甲 Eredivisie ----
        {"ajax", {"ajax"}}, {"psv", {"psv", "psv eindhoven"}}, {"feyenoord", {"feyenoord"}},
        {"azalkmaar", {"az alkmaar", "alkmaar"}}, {"twente", {"twente"}}, {"utrecht", {"utrecht"}},
        {"heerenveen", {"heerenveen"}}, {"nec", {"nec nijmegen"}},
        // ---- 葡超 Primeira ----
        {"porto", {"fc porto", "porto"}}, {"benfica", {"benfica"}},
        {"sporting", {"sporting cp", "sporting lisbon"}}, {"braga", {"sporting braga", "braga"}},
        {"vitoriasc", {"vitoria sc", "vitoria guimaraes"}},
        // ---- 苏超 Scottish ----
        {"celtic", {"celtic"}}, {"rangers", {"rangers"}}, {"aberdeen", {"aberdeen"}},
        {"hearts", {"hearts", "heart of midlothian"}}, {"hibernian", {"hibernian", "hibs"}},
        // ---- 土超 Turkish ----
        {"galatasaray", {"galatasaray"}}, {"fenerbahce", {"fenerbahce"}}, {"besiktas", {"besiktas"}},
        {"trabzonspor", {"trabzonspor"}},
        // ---- 英冠 Championship (主流) ----
        {"leeds", {"leeds united", "leeds"}}, {"burnley", {"burnley"}},
        {"sheffieldutd", {"sheffield united", "sheffield utd"}},
        {"sheffieldwed", {"sheffield wednesday"}}, {"sunderland", {"sunderland"}},
        {"westbrom", {"west bromwich", "west brom"}}, {"norwich", {"norwich"}},
        {"watford", {"watford"}}, {"middlesbrough", {"middlesbrough"}}, {"coventry", {"coventry"}},
        // ---- 沙特 Saudi Pro League ----
        {"alnassr", {"al nassr"}}, {"alhilal", {"al hilal"}}, {"alittihad", {"al ittihad"}},
        {"alahli", {"al ahli"}}, {"alettifaq", {"al ettifaq"}},
        // ---- MLS ----
        {"intermiami", {"inter miami"}}, {"lagalaxy", {"la galaxy", "los angeles galaxy"}},
        {"lafc", {"lafc", "los angeles fc"}}, {"atlantautd", {"atlanta united", "atlanta utd"}},
        {"austinfc", {"austin fc"}}, {"charlottefc", {"charlotte fc"}},
        {"chicagofire", {"chicago fire"}}, {"fccincinnati", {"fc cincinnati"}},
        {"coloradorapids", {"colorado rapids", "rapids"}}, {"columbuscrew", {"columbus crew", "crew"}},
        {"dcunited", {"dc united"}}, {"fcdallas", {"fc dallas"}},
        {"houstondynamo", {"houston dynamo", "dynamo"}}, {"sportingkc", {"sporting kansas city", "sporting kc"}},
        {"minnesotautd", {"minnesota united", "minnesota utd"}}, {"cfmontreal", {"cf montreal"}},
        {"nashvillesc", {"nashville sc"}}, {"newengland", {"new england revolution", "revolution"}},
        {"nycfc", {"new york city fc", "nycfc"}}, {"nyrb", {"new york red bulls", "red bulls"}},
        {"orlandocity", {"orlando city"}}, {"philadelphiaunion", {"philadelphia union"}},
        {"portlandtimbers", {"portland timbers", "timbers"}}, {"realsaltlake", {"real salt lake"}},
        {"sandiegofc", {"san diego fc"}}, {"sanjose", {"san jose earthquakes", "earthquakes"}},
        {"seattlesounders", {"seattle sounders", "sounders"}}, {"stlouiscity", {"st louis city"}},
        {"torontofc", {"toronto fc"}}, {"vancouver", {"vancouver whitecaps", "whitecaps"}},
        // ---- 墨超 Liga MX (主流) ----
        {"clubamerica", {"club america"}}, {"chivas", {"guadalajara", "chivas"}},
        {"cruzazul", {"cruz azul"}}, {"pumas", {"pumas unam", "pumas"}},
        {"tigres", {"tigres uanl", "tigres"}}, {"monterrey", {"monterrey", "rayados"}},
        {"toluca", {"toluca"}}, {"leon", {"club leon"}}, {"pachuca", {"pachuca"}},
        {"atlas", {"atlas"}}, {"tijuana", {"tijuana", "xolos"}},
        // ---- 巴甲 Brasileirão (主流; 多词避撞 Santos Laguna 等) ----
        {"flamengo", {"flamengo"}}, {"palmeiras", {"palmeiras"}}, {"corinthians", {"corinthians"}},
        {"saopaulo", {"sao paulo"}}, {"fluminense", {"fluminense"}}, {"botafogo", {"botafogo"}},
        {"gremio", {"gremio"}}, {"internacional", {"internacional"}},
        {"atleticomineiro", {"atletico mineiro"}}, {"cruzeiro", {"cruzeiro"}},
        {"fortaleza", {"fortaleza"}}, {"vascodagama", {"vasco da gama"}},
        // ---- 阿甲 / 其它南美豪门 ----
        {"bocajuniors", {"boca juniors", "boca"}}, {"riverplate", {"river plate"}},
        // ---- 国家队 (ROI-1, 2026-06-13 覆盖研究: 唯一实证名字缺口) ----
        //   PM 用 FIFA 全称 (Korea Republic / IR Iran / China PR), GS 用通称 (South Korea / Iran / China)
        //   → 两侧别名同收敛到一个规范 ID。实证 near-miss: PM「Korea Republic」↔GS「South Korea」score=0.50。
        //   World Cup / 欧国联 / 美洲杯窗口生效; 纯加性不退化 (§8.1 carve-out)。变音符由归一化层折叠。
        {"southkorea", {"south korea", "korea republic", "korea rep"}},
        {"northkorea", {"north korea", "korea dpr", "dpr korea"}},
        {"iran", {"iran", "ir iran"}},
        {"ivorycoast", {"ivory coast", "cote d ivoire", "cote divoire"}},
        {"usmnt", {"united states", "usa"}},
        {"chinanat", {"china", "china pr"}},
        {"czechia", {"czech republic", "czechia"}},
        {"drcongo", {"dr congo", "congo dr"}},
        {"capeverde", {"cape verde", "cabo verde"}},
        {"turkiye", {"turkey", "turkiye"}},
        {"bosnia", {"bosnia", "bosnia and herzegovina", "bosnia herzegovina"}},
        {"uaenat", {"united arab emirates", "uae"}},
        {"saudinat", {"saudi arabia", "ksa"}},
        {"hollandnat", {"netherlands", "holland"}},
        {"trinidad", {"trinidad and tobago", "trinidad tobago"}},
    };
    return t;
}

// 板球表: 主要国际队 + IPL 全队 (缩写 RCB/CSK/MI 极常见, 别名表价值高)。
const std::vector<TeamEntry>& CricketTeams() {
    static const std::vector<TeamEntry> t = {
        // ---- 国际队 ----
        {"india", {"india"}}, {"australia", {"australia"}}, {"england", {"england"}},
        {"pakistan", {"pakistan"}}, {"southafrica", {"south africa"}}, {"newzealand", {"new zealand"}},
        {"srilanka", {"sri lanka"}}, {"bangladesh", {"bangladesh"}}, {"westindies", {"west indies"}},
        {"afghanistan", {"afghanistan"}}, {"ireland", {"ireland"}}, {"zimbabwe", {"zimbabwe"}},
        {"netherlands", {"netherlands"}}, {"scotland", {"scotland"}}, {"nepal", {"nepal"}},
        {"usacricket", {"united states", "usa"}}, {"uae", {"uae", "united arab emirates"}},
        // ---- Big Bash (澳, 同城多词消歧) ----
        {"sydneysixers", {"sydney sixers"}}, {"sydneythunder", {"sydney thunder"}},
        {"melbournestars", {"melbourne stars"}}, {"melbournerenegades", {"melbourne renegades"}},
        {"perthscorchers", {"perth scorchers", "scorchers"}},
        {"adelaidestrikers", {"adelaide strikers", "strikers"}},
        {"brisbaneheat", {"brisbane heat"}}, {"hobarthurricanes", {"hobart hurricanes"}},
        // ---- IPL ----
        {"mumbaiindians", {"mumbai indians", "mi"}}, {"chennai", {"chennai super kings", "csk"}},
        {"rcb", {"royal challengers bengaluru", "royal challengers bangalore", "rcb"}},
        {"kkr", {"kolkata knight riders", "kkr"}}, {"delhicapitals", {"delhi capitals", "dc"}},
        {"punjabkings", {"punjab kings", "pbks"}}, {"rajasthan", {"rajasthan royals", "rr"}},
        {"srh", {"sunrisers hyderabad", "srh"}}, {"gujarattitans", {"gujarat titans", "gt"}},
        {"lucknow", {"lucknow super giants", "lsg"}},
    };
    return t;
}

// 足球类 sport 码 (一张全球俱乐部表跨赛事共用)。
bool IsSoccerCode(std::string_view sl) {
    return Contains(sl, "soccer") || Contains(sl, "epl") || Contains(sl, "premier") ||
           Contains(sl, "laliga") || Contains(sl, "liga") || Contains(sl, "serie") ||
           Contains(sl, "bundesliga") || Contains(sl, "ligue") || Contains(sl, "ucl") ||
           Contains(sl, "uefa") || Contains(sl, "champions") || Contains(sl, "europa") ||
           Contains(sl, "mls") || Contains(sl, "eredivisie") || Contains(sl, "primeira") ||
           Contains(sl, "fifa") || Contains(sl, "worldcup");
}

// sport_code → 联盟表 (nullptr = 该团队运动暂无表, 回退通用)。NFL 先于足球判 (避免 "football" 误路由)。
const std::vector<TeamEntry>* LeagueFor(std::string_view sl) {
    if (Contains(sl, "nba") || Contains(sl, "wnba")) return &NbaTeams();
    if (Contains(sl, "mlb")) return &MlbTeams();
    if (Contains(sl, "nfl")) return &NflTeams();
    if (Contains(sl, "nhl")) return &NhlTeams();
    if (IsSoccerCode(sl)) return &SoccerTeams();
    if (Contains(sl, "cricket") || Contains(sl, "ipl") || Contains(sl, "t20") || Contains(sl, "odi"))
        return &CricketTeams();
    return nullptr;  // 大学(队数过多)等: 待真实样本建表, 暂回退通用
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
    // 团队项目: 四大联盟 + 足球 (各联赛码, IsSoccerCode 统一口径) + 板球 + 大学。
    if (Contains(sl, "nba") || Contains(sl, "wnba") || Contains(sl, "mlb") || Contains(sl, "nfl") ||
        Contains(sl, "nhl") || Contains(sl, "football") || IsSoccerCode(sl) || Contains(sl, "cricket") ||
        Contains(sl, "ipl") || Contains(sl, "t20") || Contains(sl, "odi") || Contains(sl, "ncaa") ||
        Contains(sl, "cfb") || Contains(sl, "cbb")) {
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
