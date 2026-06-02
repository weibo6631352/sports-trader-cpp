// tests/unit/test_team_livescore_parser.cpp — 队制 livescore 通用解析器单测
//   (cricket/livescore + esports/home; 覆盖率杠杆 2026-06-03)
//
// Owner: 老雷 (GM) | Date: 2026-06-03
//
// 覆盖:
//   C1: cricket "In Progress" → live; "Finished"/"Not Started"/"Not covered Live" → 跳过
//   C2: cricket localteam/visitorteam → home/away; totalscore "308/5"→308, "215"→215
//   C3: cricket category id → league_id
//   E1: esports "Started" → live; "Finished"/"Not Started"/"Walkover" → 跳过
//   E2: esports localteam/awayteam → home/away; score "1"→1
//   E3: esports match-level league_id 优先 (无 <category> 包裹)
//   X1: AttrIn 单词边界 — league_id 的 "id" 子串不误命中 match id
//   X2: 空 XML / 残缺 match 不崩

#include <gtest/gtest.h>

#include "stcpp/data/team_livescore_parser.hpp"

using stcpp::data::team_livescore::ParseTeamLivescoreLive;
using stcpp::data::team_livescore::TeamLivescoreSpec;

namespace {
constexpr TeamLivescoreSpec kCricket{"cricket", "In Progress", "totalscore", "visitorteam"};
constexpr TeamLivescoreSpec kEsports{"esports", "Started", "score", "awayteam"};
constexpr std::int64_t kTs = 1780000000000000000LL;
}  // namespace

// ---- Cricket ----
TEST(TeamLivescoreParser, CricketLiveStatusFilter) {
    const std::string xml = R"(<scores>
  <category name="Tri-Series" id="7940">
    <match date="03.06.2026" time="10:00" status="In Progress" type="T20" id="13072008299">
      <localteam name="Scotland Women" totalscore="145/6" winner="False" id="4220" />
      <visitorteam name="Bangladesh Women" totalscore="98/3" winner="False" id="4218" />
    </match>
    <match date="01.06.2026" time="04:30" status="Finished" type="ODI" id="13072008300">
      <localteam name="Amo Region" totalscore="308/5" winner="True" id="1" />
      <visitorteam name="Band-e-Amir Region" totalscore="215" winner="False" id="2" />
    </match>
    <match date="04.06.2026" time="09:00" status="Not Started" type="T20" id="13072008301">
      <localteam name="India Women" totalscore="" id="3" />
      <visitorteam name="England Women" totalscore="" id="4" />
    </match>
  </category>
</scores>)";
    const auto out = ParseTeamLivescoreLive(xml, kTs, kCricket);
    ASSERT_EQ(out.size(), 1u);  // 仅 In Progress
    EXPECT_EQ(out[0].event_id, "13072008299");
    EXPECT_EQ(out[0].sport, "cricket");
    EXPECT_EQ(out[0].status, "inplay");
    EXPECT_EQ(out[0].home, "Scotland Women");
    EXPECT_EQ(out[0].away, "Bangladesh Women");
    EXPECT_EQ(out[0].home_score, 145);  // "145/6" → runs 145
    EXPECT_EQ(out[0].away_score, 98);   // "98/3"  → runs 98
    EXPECT_EQ(out[0].league_id, "7940");
    EXPECT_EQ(out[0].ts.ingestion_ts_ns, kTs);
}

TEST(TeamLivescoreParser, CricketAllOutScore) {
    const std::string xml = R"(<scores><category name="x" id="9">
      <match status="In Progress" id="500">
        <localteam name="A" totalscore="215" id="1" />
        <visitorteam name="B" totalscore="60/2" id="2" />
      </match></category></scores>)";
    const auto out = ParseTeamLivescoreLive(xml, kTs, kCricket);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].home_score, 215);  // 全 out 无 '/'
    EXPECT_EQ(out[0].away_score, 60);
}

// ---- Esports ----
TEST(TeamLivescoreParser, EsportsLiveStatusFilter) {
    const std::string xml = R"(<scores>
  <match status="Started" id="395873" league_id="8319" league="EWC Playoffs" type="DOTA 2" time="14:00">
    <localteam name="Pipsqueak+4" id="27130" score="1" />
    <awayteam name="Natus Vincere" id="17422" score="1" />
    <scoreboard />
  </match>
  <match status="Finished" id="395800" league_id="8319" type="DOTA 2">
    <localteam name="Team A" id="1" score="2" />
    <awayteam name="Team B" id="2" score="0" />
  </match>
  <match status="Not Started" id="395999" league_id="8400" type="CS GO">
    <localteam name="Team C" id="3" score="0" />
    <awayteam name="Team D" id="4" score="0" />
  </match>
</scores>)";
    const auto out = ParseTeamLivescoreLive(xml, kTs, kEsports);
    ASSERT_EQ(out.size(), 1u);  // 仅 Started
    EXPECT_EQ(out[0].event_id, "395873");
    EXPECT_EQ(out[0].sport, "esports");
    EXPECT_EQ(out[0].home, "Pipsqueak+4");
    EXPECT_EQ(out[0].away, "Natus Vincere");
    EXPECT_EQ(out[0].home_score, 1);
    EXPECT_EQ(out[0].away_score, 1);
    EXPECT_EQ(out[0].league_id, "8319");  // match-level league_id 优先
}

// ---- 边界 ----
TEST(TeamLivescoreParser, AttrWordBoundaryIdNotConfusedWithLeagueId) {
    // league_id 含 "id" 子串; match id 须取 id="..." 非 league_id="..."。
    const std::string xml = R"(<scores>
  <match league_id="9999" id="42" status="Started">
    <localteam name="X" score="3" /><awayteam name="Y" score="2" />
  </match></scores>)";
    const auto out = ParseTeamLivescoreLive(xml, kTs, kEsports);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].event_id, "42");       // 非 "9999"
    EXPECT_EQ(out[0].league_id, "9999");
}

TEST(TeamLivescoreParser, EmptyAndMalformed) {
    EXPECT_TRUE(ParseTeamLivescoreLive("", kTs, kCricket).empty());
    EXPECT_TRUE(ParseTeamLivescoreLive("<scores></scores>", kTs, kEsports).empty());
    // 缺 visitorteam → 跳过 (fail-soft 不崩)
    const std::string half = R"(<match status="In Progress" id="1"><localteam name="A" totalscore="5"/></match>)";
    EXPECT_TRUE(ParseTeamLivescoreLive(half, kTs, kCricket).empty());
}

TEST(TeamLivescoreParser, MissingMatchIdSkipped) {
    const std::string xml = R"(<match status="Started"><localteam name="A" score="1"/><awayteam name="B" score="0"/></match>)";
    EXPECT_TRUE(ParseTeamLivescoreLive(xml, kTs, kEsports).empty());  // 无 id → 跳过
}
