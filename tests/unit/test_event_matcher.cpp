// tests/unit/test_event_matcher.cpp — EventMatcher (A0 映射桥) 单测
//
// Owner: 老雷 (GM) — A0 配套 (M1 路线评审会决议)
// last_review: 2026-05-30
//
// 覆盖: 队名归一化 / overlap 相似度 / 双向分配 / kickoff 窗口 / fail-closed / 网球难例。

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "stcpp/app/event_matcher.hpp"
#include "stcpp/app/team_alias.hpp"

using stcpp::app::CanonicalTeam;
using stcpp::app::ClassifySportMatch;
using stcpp::app::EventMatcher;
using stcpp::app::EventMatchInput;
using stcpp::app::SportMatchCategory;
using stcpp::debug_api::EventScore;

namespace {

EventScore MakeEv(const std::string& id, const std::string& home, const std::string& away,
                  std::int64_t kickoff_sec) {
    EventScore es;
    es.found = true;
    es.event_id = id;
    es.home = home;
    es.away = away;
    es.kickoff_ts_sec = kickoff_sec;
    return es;
}

}  // namespace

// ============================================================================
// NormalizeTeamTokens
// ============================================================================
TEST(EventMatcherNorm, LowercaseAlnumTokens) {
    const auto t = EventMatcher::NormalizeTeamTokens("Los Angeles Lakers");
    ASSERT_EQ(t.size(), 3u);
    // 排序去重: angeles / lakers / los
    EXPECT_EQ(t[0], "angeles");
    EXPECT_EQ(t[1], "lakers");
    EXPECT_EQ(t[2], "los");
}

TEST(EventMatcherNorm, StripsPunctuationAndDedup) {
    // "F.C." 中的 '.' 是分隔符 → 拆成 f / c; "Porto" 出现两次去重为一.
    const auto t = EventMatcher::NormalizeTeamTokens("F.C. Porto (Porto)");
    ASSERT_EQ(t.size(), 3u);
    EXPECT_EQ(t[0], "c");
    EXPECT_EQ(t[1], "f");
    EXPECT_EQ(t[2], "porto");
}

TEST(EventMatcherNorm, EmptyAndSymbolsOnly) {
    EXPECT_TRUE(EventMatcher::NormalizeTeamTokens("").empty());
    EXPECT_TRUE(EventMatcher::NormalizeTeamTokens("--- !!!").empty());
}

// 变音符折叠 (2026-06-02 老板「名字可能不一样」): UTF-8 重音 → ASCII, 否则按字节切碎裂不匹配。
TEST(EventMatcherNorm, FoldsDiacriticsToAscii) {
    // Latin-1: ú → u (原 bug: ú=0xC3 0xBA 当分隔符 → "cer"+"ndolo" 碎裂)
    const auto t1 = EventMatcher::NormalizeTeamTokens("Cer\xC3\xBAndolo");  // "Cerúndolo"
    ASSERT_EQ(t1.size(), 1u);
    EXPECT_EQ(t1[0], "cerundolo");
    // Slavic: ć → c, đ → d
    const auto t2 = EventMatcher::NormalizeTeamTokens("\xC4\x90okovi\xC4\x87");  // "Đoković"
    ASSERT_EQ(t2.size(), 1u);
    EXPECT_EQ(t2[0], "dokovic");
    // ñ → n, á → a
    const auto t3 = EventMatcher::NormalizeTeamTokens("Nadal Espa\xC3\xB1");  // "Nadal Españ"
    EXPECT_EQ(t3[0], "espan");
    EXPECT_EQ(t3[1], "nadal");
}

// 重音折叠后跨源匹配: Polymarket 去重音名 vs Goalserve 带重音名 → 应匹配 (原碎裂 → 0)。
TEST(EventMatcherSim, DiacriticCrossSourceMatch) {
    // Polymarket "Cerundolo" (ASCII) vs Goalserve "Cerúndolo" (重音) → 折叠后 1.0
    EXPECT_DOUBLE_EQ(EventMatcher::TeamSimilarity("Cerundolo", "Cer\xC3\xBAndolo"), 1.0);
    // "Coric" vs "Ćorić" (Ć/ć → c) → 折叠后 1.0
    EXPECT_DOUBLE_EQ(EventMatcher::TeamSimilarity("Coric", "\xC4\x86ori\xC4\x87"), 1.0);
    // "Muller" vs "Müller" (ü → u) → 折叠后 1.0
    EXPECT_DOUBLE_EQ(EventMatcher::TeamSimilarity("Muller", "M\xC3\xBCller"), 1.0);
}

// ============================================================================
// sport-aware: 运动分类 + 团队规范化 (team_alias)
// ============================================================================
TEST(SportAware, ClassifySport) {
    EXPECT_EQ(ClassifySportMatch("nba"), SportMatchCategory::kTeam);
    EXPECT_EQ(ClassifySportMatch("mlb"), SportMatchCategory::kTeam);
    EXPECT_EQ(ClassifySportMatch("nfl"), SportMatchCategory::kTeam);
    EXPECT_EQ(ClassifySportMatch("nhl"), SportMatchCategory::kTeam);
    EXPECT_EQ(ClassifySportMatch("soccer"), SportMatchCategory::kTeam);
    EXPECT_EQ(ClassifySportMatch("atp"), SportMatchCategory::kIndividual);
    EXPECT_EQ(ClassifySportMatch("itf"), SportMatchCategory::kIndividual);
    EXPECT_EQ(ClassifySportMatch("ufc"), SportMatchCategory::kIndividual);
    EXPECT_EQ(ClassifySportMatch(""), SportMatchCategory::kUnknown);
}

TEST(SportAware, CanonicalTeam_CityVsNickname) {
    // "Los Angeles Lakers" / "LA Lakers" / "Lakers" → 同规范 ID
    EXPECT_EQ(CanonicalTeam("nba", "Los Angeles Lakers"), "lakers");
    EXPECT_EQ(CanonicalTeam("nba", "LA Lakers"), "lakers");
    EXPECT_EQ(CanonicalTeam("nba", "Lakers"), "lakers");
    // MLB 多词昵称消歧: Red Sox ≠ White Sox
    EXPECT_EQ(CanonicalTeam("mlb", "Boston Red Sox"), "redsox");
    EXPECT_EQ(CanonicalTeam("mlb", "Chicago White Sox"), "whitesox");
    // NFL 数字昵称 + 别名
    EXPECT_EQ(CanonicalTeam("nfl", "San Francisco 49ers"), "49ers");
    EXPECT_EQ(CanonicalTeam("nfl", "Niners"), "49ers");
    // 未建表运动 (橄榄球) → "" (回退通用); 已建表运动里未知队 → "" (回退通用)
    EXPECT_EQ(CanonicalTeam("rugby", "Leinster"), "");
    EXPECT_EQ(CanonicalTeam("nba", "Unknown Team XYZ"), "");
}

// 跨运动同昵称不串台 (panthers: NFL+NHL; 按 sport_code 分派各自表)。
TEST(SportAware, CrossSportNoCollision) {
    EXPECT_EQ(CanonicalTeam("nfl", "Carolina Panthers"), "panthers");
    EXPECT_EQ(CanonicalTeam("nhl", "Florida Panthers"), "panthers");  // 各自表内 panthers 唯一
}

// 足球: 同城多队消歧 + 缩写/绰号 + 跨联赛同表。
TEST(SportAware, SoccerCanonical) {
    // 曼联 缩写全收
    EXPECT_EQ(CanonicalTeam("soccer", "Manchester United"), "manutd");
    EXPECT_EQ(CanonicalTeam("epl", "Man Utd"), "manutd");
    EXPECT_EQ(CanonicalTeam("soccer", "Manchester Utd"), "manutd");
    // 同城不串: 曼城 ≠ 曼联
    EXPECT_EQ(CanonicalTeam("soccer", "Manchester City"), "mancity");
    EXPECT_NE(CanonicalTeam("soccer", "Manchester City"), CanonicalTeam("soccer", "Manchester United"));
    // 马德里双雄消歧
    EXPECT_EQ(CanonicalTeam("laliga", "Real Madrid"), "realmadrid");
    EXPECT_EQ(CanonicalTeam("laliga", "Atletico Madrid"), "atletico");
    // 绰号 / 缩写
    EXPECT_EQ(CanonicalTeam("soccer", "Spurs"), "tottenham");
    EXPECT_EQ(CanonicalTeam("soccer", "Barca"), "barcelona");
    EXPECT_EQ(CanonicalTeam("soccer", "Juve"), "juventus");
    EXPECT_EQ(CanonicalTeam("ucl", "PSG"), "psg");
    // Inter Milan vs Inter Miami 不串 (裸 "inter" 不解析)
    EXPECT_EQ(CanonicalTeam("seriea", "Inter Milan"), "inter");
    EXPECT_EQ(CanonicalTeam("mls", "Inter Miami"), "intermiami");
    // 扩充联赛: MLS / 巴甲 / 沙特 / 墨超 / 荷甲 / 英冠
    EXPECT_EQ(CanonicalTeam("mls", "Seattle Sounders"), "seattlesounders");
    EXPECT_EQ(CanonicalTeam("mls", "LAFC"), "lafc");
    EXPECT_EQ(CanonicalTeam("soccer", "Flamengo"), "flamengo");
    EXPECT_EQ(CanonicalTeam("soccer", "Al Hilal"), "alhilal");
    EXPECT_EQ(CanonicalTeam("soccer", "Chivas"), "chivas");
    EXPECT_EQ(CanonicalTeam("soccer", "Ajax"), "ajax");
    EXPECT_EQ(CanonicalTeam("soccer", "Leeds United"), "leeds");
    // Internacional(巴甲) vs Inter Milan 不串 (拼写不同 internacional≠internazionale)
    EXPECT_EQ(CanonicalTeam("soccer", "Internacional"), "internacional");
    EXPECT_NE(CanonicalTeam("soccer", "Internacional"), "inter");
}

// 板球: 国际队 + IPL 缩写。
TEST(SportAware, CricketCanonical) {
    EXPECT_EQ(CanonicalTeam("cricket", "India"), "india");
    EXPECT_EQ(CanonicalTeam("cricket", "West Indies"), "westindies");
    EXPECT_EQ(CanonicalTeam("ipl", "Royal Challengers Bengaluru"), "rcb");
    EXPECT_EQ(CanonicalTeam("ipl", "RCB"), "rcb");
    EXPECT_EQ(CanonicalTeam("t20", "Chennai Super Kings"), "chennai");
    EXPECT_EQ(CanonicalTeam("ipl", "CSK"), "chennai");
    // Big Bash 同城消歧 (Sydney Sixers ≠ Thunder)
    EXPECT_EQ(CanonicalTeam("t20", "Sydney Sixers"), "sydneysixers");
    EXPECT_EQ(CanonicalTeam("t20", "Sydney Thunder"), "sydneythunder");
    EXPECT_NE(CanonicalTeam("t20", "Sydney Sixers"), CanonicalTeam("t20", "Sydney Thunder"));
}

// 足球同城 derby 经规范 ID 正确定向 (通用 overlap 会因共享城市名 0.5 误配)。
TEST(EventMatcherSim, SoccerDerbyOrientation) {
    EventMatcher m;
    EventScore ev;
    ev.home = "Manchester City";
    ev.away = "Manchester United";
    EventMatchInput in;
    in.sport = "epl";
    in.team0 = "Man Utd";       // Polymarket 缩写
    in.team1 = "Man City";
    const auto r = m.Match(in, {ev});
    EXPECT_TRUE(r.matched);
    EXPECT_FALSE(r.yes_is_home);  // team0(Man Utd)→away → 交叉配
}

// 团队精确匹配: Polymarket 缩写 vs Goalserve 全名 → 经规范 ID 命中 (通用 overlap 会漏)。
TEST(EventMatcherSim, TeamCanonicalMatch) {
    EventMatcher m;
    EventScore ev;
    ev.home = "Los Angeles Lakers";
    ev.away = "Boston Celtics";
    EventMatchInput in;
    in.sport = "nba";
    in.team0 = "LA Lakers";    // Polymarket 缩写
    in.team1 = "Celtics";
    const auto r = m.Match(in, {ev});
    EXPECT_TRUE(r.matched);
    EXPECT_TRUE(r.yes_is_home);  // team0(Lakers)→home
}

// 个人项目: 语序不同 (名 姓 vs 姓 名首字母) 经姓锚匹配。
TEST(EventMatcherSim, IndividualSurnameMatch) {
    EventMatcher m;
    EventScore ev;
    ev.home = "Khomutsianskaya D.";  // Goalserve: 姓 名首字母
    ev.away = "Yang Y.";
    EventMatchInput in;
    in.sport = "itf";
    in.team0 = "Daria Khomutsianskaya";  // Polymarket: 名 姓
    in.team1 = "Yidi Yang";
    const auto r = m.Match(in, {ev});
    EXPECT_TRUE(r.matched);
}

// ============================================================================
// TeamSimilarity (overlap coefficient)
// ============================================================================
TEST(EventMatcherSim, ExactMatch) {
    EXPECT_DOUBLE_EQ(EventMatcher::TeamSimilarity("Galorys", "Galorys"), 1.0);
}

TEST(EventMatcherSim, CaseAndPunctInsensitive) {
    EXPECT_DOUBLE_EQ(EventMatcher::TeamSimilarity("LA Lakers", "la lakers"), 1.0);
}

TEST(EventMatcherSim, AbbrevContainment_LALakers) {
    // "LA Lakers"{la,lakers} vs "Los Angeles Lakers"{los,angeles,lakers}
    // overlap = |{lakers}| / min(2,3) = 1/2 = 0.5
    EXPECT_DOUBLE_EQ(EventMatcher::TeamSimilarity("LA Lakers", "Los Angeles Lakers"), 0.5);
}

TEST(EventMatcherSim, NoCommonToken_Zero) {
    EXPECT_DOUBLE_EQ(EventMatcher::TeamSimilarity("Galorys", "largadosypelados"), 0.0);
}

TEST(EventMatcherSim, EmptyZero) {
    EXPECT_DOUBLE_EQ(EventMatcher::TeamSimilarity("", "Lakers"), 0.0);
}

// ============================================================================
// Match — 基础正向
// ============================================================================
TEST(EventMatcher, ExactBothTeams_Match) {
    EventMatcher m;
    EventMatchInput in{"Galorys", "largadosypelados", 1000000, "cs2"};
    std::vector<EventScore> cands{MakeEv("134000001", "Galorys", "largadosypelados", 1000000)};
    const auto r = m.Match(in, cands);
    EXPECT_TRUE(r.matched);
    EXPECT_EQ(r.inplay_match_id, "134000001");
}

TEST(EventMatcher, CrossAssignment_HomeAwaySwapped) {
    // market t0=A t1=B; event home=B away=A (顺序相反) → 交叉配应匹配
    EventMatcher m;
    EventMatchInput in{"Lakers", "Celtics", 2000000, "nba"};
    std::vector<EventScore> cands{MakeEv("134000002", "Celtics", "Lakers", 2000000)};
    const auto r = m.Match(in, cands);
    EXPECT_TRUE(r.matched);
    EXPECT_EQ(r.inplay_match_id, "134000002");
}

TEST(EventMatcher, Orientation_DirectAssignment_YesIsHome) {
    // market t0(YES)=Lakers, event home=Lakers → YES=home → yes_is_home=true
    EventMatcher m;
    EventMatchInput in{"Lakers", "Celtics", 2000000, "nba"};
    std::vector<EventScore> cands{MakeEv("d", "Lakers", "Celtics", 2000000)};
    const auto r = m.Match(in, cands);
    ASSERT_TRUE(r.matched);
    EXPECT_TRUE(r.yes_is_home);
}

TEST(EventMatcher, Orientation_CrossAssignment_YesIsAway) {
    // market t0(YES)=Lakers, event home=Celtics away=Lakers → YES=away → yes_is_home=false
    // (消费侧据此把 away_score 填进 score_home_total, 防 prior 方向反 → 老周张冠李戴)
    EventMatcher m;
    EventMatchInput in{"Lakers", "Celtics", 2000000, "nba"};
    std::vector<EventScore> cands{MakeEv("c", "Celtics", "Lakers", 2000000)};
    const auto r = m.Match(in, cands);
    ASSERT_TRUE(r.matched);
    EXPECT_FALSE(r.yes_is_home);
}

TEST(EventMatcher, Orientation_Ambiguous_FailClosed) {
    // 队名近似 (City / City FC): 直配与交叉两种分配都过门且分差≈0 → orientation 模糊
    //   靠 >= 任意拍一边 = 比分方向可能接反 = 反向下单。P2-1 (老郭): fail-closed 不匹配。
    EventMatcher m;
    EventMatchInput in{"City", "City FC", 2000000, "soccer"};
    std::vector<EventScore> cands{MakeEv("amb", "City", "City FC", 2000000)};
    const auto r = m.Match(in, cands);
    EXPECT_FALSE(r.matched) << "P2-1: orientation 模糊 (直配/交叉都过门且分差<margin) → fail-closed 不匹配";
}

TEST(EventMatcher, AbbrevTeamsWithinThreshold_Match) {
    // 双队 overlap=0.5 (== 默认阈值 0.5) → 过门
    EventMatcher m;
    EventMatchInput in{"LA Lakers", "GS Warriors", 3000000, "nba"};
    std::vector<EventScore> cands{
        MakeEv("134000003", "Los Angeles Lakers", "Golden State Warriors", 3000000)};
    const auto r = m.Match(in, cands);
    EXPECT_TRUE(r.matched);
    EXPECT_EQ(r.inplay_match_id, "134000003");
}

// ============================================================================
// Match — fail-closed
// ============================================================================
TEST(EventMatcher, NoTeamMatch_FailClosed) {
    EventMatcher m;
    EventMatchInput in{"Galorys", "largadosypelados", 1000000, "cs2"};
    std::vector<EventScore> cands{MakeEv("134000004", "Team X", "Team Y", 1000000)};
    const auto r = m.Match(in, cands);
    EXPECT_FALSE(r.matched);
    EXPECT_TRUE(r.inplay_match_id.empty());
}

TEST(EventMatcher, OneTeamMatchOnly_FailClosed) {
    // 只有一队匹配 (min < 阈值) → 不匹配 (防张冠李戴)
    EventMatcher m;
    EventMatchInput in{"Lakers", "Celtics", 1000000, "nba"};
    std::vector<EventScore> cands{MakeEv("134000005", "Lakers", "Bulls", 1000000)};
    const auto r = m.Match(in, cands);
    EXPECT_FALSE(r.matched);
}

TEST(EventMatcher, KickoffTooFar_FailClosed) {
    // 队名全配但 kickoff 差 > 15min → 时间窗口否决 (同队不同场次防误配)
    EventMatcher m;
    EventMatchInput in{"Lakers", "Celtics", 1000000, "nba"};
    std::vector<EventScore> cands{MakeEv("134000006", "Lakers", "Celtics", 1000000 + 1000)};  // +1000s > 900
    const auto r = m.Match(in, cands);
    EXPECT_FALSE(r.matched);
}

TEST(EventMatcher, KickoffWithinWindow_Match) {
    EventMatcher m;
    EventMatchInput in{"Lakers", "Celtics", 1000000, "nba"};
    std::vector<EventScore> cands{MakeEv("134000007", "Lakers", "Celtics", 1000000 + 600)};  // +600s < 900
    const auto r = m.Match(in, cands);
    EXPECT_TRUE(r.matched);
}

TEST(EventMatcher, UnknownKickoff_TeamOnly_Match) {
    // kickoff 任一未知 (0) → 不据时间否决, 纯队名匹配
    EventMatcher m;
    EventMatchInput in{"Lakers", "Celtics", 0, "nba"};
    std::vector<EventScore> cands{MakeEv("134000008", "Lakers", "Celtics", 0)};
    const auto r = m.Match(in, cands);
    EXPECT_TRUE(r.matched);
}

TEST(EventMatcher, EmptyMarketTeams_FailClosed) {
    EventMatcher m;
    EventMatchInput in{"", "Celtics", 1000000, "nba"};
    std::vector<EventScore> cands{MakeEv("134000009", "Lakers", "Celtics", 1000000)};
    EXPECT_FALSE(m.Match(in, cands).matched);
}

TEST(EventMatcher, EmptyCandidates_FailClosed) {
    EventMatcher m;
    EventMatchInput in{"Lakers", "Celtics", 1000000, "nba"};
    EXPECT_FALSE(m.Match(in, {}).matched);
}

// ============================================================================
// Match — 多候选择优 + 同队多场 (时间窗口区分)
// ============================================================================
TEST(EventMatcher, MultipleCandidates_PicksBestTeamScore) {
    // 注: 用未建别名表的运动 (cs2 → 通用 overlap) 测「按 score 排序选最佳」。
    //   NBA 等已建表运动下 "LA Lakers"==​"Los Angeles Lakers" (规范同队, 都 1.0) → 打平,
    //   分辨力按设计消失 (缩写=全名正是改进目的), 故此处用通用路径验证 score 排序。
    EventMatcher m;
    EventMatchInput in{"Los Angeles Lakers", "Boston Celtics", 1000000, "cs2"};
    std::vector<EventScore> cands{
        MakeEv("weak", "LA Lakers", "Celtics", 1000000),                   // 部分缩写, score 较低
        MakeEv("strong", "Los Angeles Lakers", "Boston Celtics", 1000000)  // 全配, score 满
    };
    const auto r = m.Match(in, cands);
    EXPECT_TRUE(r.matched);
    EXPECT_EQ(r.inplay_match_id, "strong");
}

TEST(EventMatcher, SameTeamsDoubleHeader_TimeWindowDisambiguates) {
    // 同两队同日两场 (G1/G2): 仅 kickoff 接近的那场匹配
    EventMatcher m;
    EventMatchInput in{"Lakers", "Celtics", 5000000, "nba"};
    std::vector<EventScore> cands{
        MakeEv("game1", "Lakers", "Celtics", 5000000 + 100),    // 近 → 匹配
        MakeEv("game2", "Lakers", "Celtics", 5000000 + 100000)  // 远 → 否决
    };
    const auto r = m.Match(in, cands);
    EXPECT_TRUE(r.matched);
    EXPECT_EQ(r.inplay_match_id, "game1");
}

// ============================================================================
// 网球难例 (小段: 运动员名格式差异, 匹配率 ~70%)
// ============================================================================
TEST(EventMatcher, Tennis_PlayerNameVariation_KnownLimitation) {
    // Goalserve "Djokovic N." vs Polymarket "Novak Djokovic": {djokovic,n} vs {novak,djokovic}
    // overlap = |{djokovic}| / min(2,2) = 0.5 == 阈值 → 过 (姓匹配); 名/缩写不阻塞
    EXPECT_GE(EventMatcher::TeamSimilarity("Djokovic N.", "Novak Djokovic"), 0.5);
    // 但完全不同译名/拼写 → 不匹配 (fail-closed, 该盘不交易)
    EXPECT_LT(EventMatcher::TeamSimilarity("Alcaraz C.", "Rafael Nadal"), 0.5);
}
