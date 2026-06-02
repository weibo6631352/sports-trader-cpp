#pragma once
// stcpp/data/team_livescore_parser.hpp — Goalserve 队制 livescore 通用解析器
//   (cricket/livescore + esports/home; 同 localteam/(visitorteam|awayteam) 结构, 参数化)
//
// Owner: 老雷 (GM) | last_review: 2026-06-03
//
// 背景 (覆盖率杠杆, 2026-06-03 老板「GS 其他接口有没有」+「加吧加吧」):
//   覆盖率诊断 (EventMatcher::Diag) 决定性结论: 匹配器无问题 (orientation/kickoff 门拒全 0,
//   名字配上即匹配), 瓶颈是【候选池覆盖】—— GS inplay-*.gz (bet365 联动) 缺这些:
//     cricket: inplay-cricket.gz 404 → 候选 0, 而 PM 有 crint/crict20blast (12 场 live)。
//     esports: inplay-esports 仅 ~4, esports/home 更宽 (含更多 DOTA2/CS/LoL)。
//   cricket/livescore + esports/home 是【纯 livescore】(无 bet365 odds), 接作补充比分源 →
//   候选池↑ → EventMatcher 配上 PM 的 crint/esports 盘 → 覆盖率↑。代价同 tennis_scores:
//   无 sharp fair, 走 score-prior fair; cricket 定价模型另立 (本期只为覆盖 + 比分特征)。
//
// XML 结构 (实测 2026-06-03):
//   cricket/livescore:
//     <category name="..." id="...">
//       <match date=".." status="In Progress" type="T20" id="13072008299">
//         <localteam name="X" totalscore="308/5" winner=".." id=".." />
//         <visitorteam name="Y" totalscore="215" .. />
//     status: "In Progress"=live; "Not Started"/"Finished"/"Not covered Live"=非 live。
//     totalscore "308/5"=runs/wickets (取 runs); "215"=全 out。
//   esports/home:
//     <match status="Started" league_id="8319" league="..." type="DOTA 2" id="395873">
//       <localteam name="X" score="1" id=".." />
//       <awayteam name="Y" score="1" id=".." />
//     status: "Started"=live; "Finished"/"Not Started"/"Walkover"/"Postponed"=非 live。
//     league 在 match 属性 (无 <category> 包裹) → 取 match-level league_id。
//
//   差异参数化 (TeamLivescoreSpec): sport / live_status / score_attr / away_tag。
//   纯函数, fail-soft; 只产 live 场, 喂 ScoreSnapshotStore (与 inplay 合并, 去重见 InjectSupplementalScores)。

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "src/stcpp/debug_api/state_provider.hpp"  // EventScore / FourTs

namespace stcpp::data::team_livescore {

// 队制 livescore feed 的差异参数 (cricket vs esports)。
struct TeamLivescoreSpec {
    std::string_view sport;        // EventScore.sport ("cricket" / "esports")
    std::string_view live_status;  // 进行中状态值 ("In Progress" / "Started")
    std::string_view score_attr;   // 比分属性名 ("totalscore" / "score")
    std::string_view away_tag;     // 客队标签 ("visitorteam" / "awayteam")
};

namespace detail {
// AttrIn — 取 block 内 attr="..." 的值; 单词边界 (attr 前须空白/'<', 防子串误命中)。
[[nodiscard]] inline std::string_view AttrIn(std::string_view block, std::string_view attr) noexcept {
    std::string needle;
    needle.reserve(attr.size() + 2);
    needle.append(attr);
    needle.append("=\"");
    std::size_t from = 0;
    while (true) {
        const auto a = block.find(needle, from);
        if (a == std::string_view::npos)
            return {};
        const bool boundary = (a == 0) || block[a - 1] == ' ' || block[a - 1] == '\t' || block[a - 1] == '<';
        if (boundary) {
            const auto vs = a + needle.size();
            const auto ve = block.find('"', vs);
            if (ve == std::string_view::npos)
                return {};
            return block.substr(vs, ve - vs);
        }
        from = a + 1;
    }
}
// ParseIntSafe — 取前导整数 ("308/5"→308, "1"→1, "215"→215); 无数字→0。
[[nodiscard]] inline int ParseIntSafe(std::string_view sv) noexcept {
    int v = 0;
    bool any = false;
    for (char c : sv) {
        if (c >= '0' && c <= '9') {
            v = v * 10 + (c - '0');
            any = true;
        } else if (any) {
            break;
        }
    }
    return any ? v : 0;
}
// TeamIn — 取 span 内 <tag name=".." {score_attr}=".." /> 的 name + score。
inline void TeamIn(std::string_view span, std::string_view tag, std::string_view score_attr,
                   std::string& name_out, int& score_out) {
    std::string open;
    open.reserve(tag.size() + 1);
    open.push_back('<');
    open.append(tag);
    const auto k = span.find(open);
    if (k == std::string_view::npos)
        return;
    // 取该标签头 (到 '>' 或 "/>")
    const auto e = span.find('>', k);
    if (e == std::string_view::npos)
        return;
    const std::string_view hdr = span.substr(k, e - k + 1);
    name_out = std::string(AttrIn(hdr, "name"));
    score_out = ParseIntSafe(AttrIn(hdr, score_attr));
}
}  // namespace detail

// ParseTeamLivescoreLive — 队制 livescore XML → 进行中比赛的 EventScore (键: match id)。
//   ingestion_ts_ns: 抓取本地刻 (R-20 ingestion); livescore 无精确 payload ts → 4ts 用 ingestion。
[[nodiscard]] inline std::vector<stcpp::debug_api::EventScore> ParseTeamLivescoreLive(
    std::string_view xml, std::int64_t ingestion_ts_ns, const TeamLivescoreSpec& spec) {
    using detail::AttrIn;
    using detail::TeamIn;
    std::vector<stcpp::debug_api::EventScore> out;
    std::string current_league;  // <category id> (cricket); esports 走 match-level league_id
    std::size_t pos = 0;
    while (pos < xml.size()) {
        const auto cat = xml.find("<category", pos);
        const auto mat = xml.find("<match", pos);
        if (mat == std::string_view::npos)
            break;
        if (cat != std::string_view::npos && cat < mat) {
            const auto gt = xml.find('>', cat);
            if (gt == std::string_view::npos)
                break;
            current_league = std::string(AttrIn(xml.substr(cat, gt - cat + 1), "id"));
            pos = gt + 1;
            continue;
        }
        const auto me = xml.find("</match>", mat);
        const auto next_mat = xml.find("<match", mat + 6);
        const std::size_t span_end =
            (me != std::string_view::npos) ? me + 8
            : (next_mat == std::string_view::npos) ? xml.size()
                                                   : next_mat;
        const std::string_view span = xml.substr(mat, span_end - mat);
        pos = span_end;

        const auto hdr_end = span.find('>');
        if (hdr_end == std::string_view::npos)
            continue;
        const std::string_view hdr = span.substr(0, hdr_end);
        const std::string_view status = AttrIn(hdr, "status");
        if (status != spec.live_status)
            continue;  // 仅进行中
        const std::string_view mid = AttrIn(hdr, "id");
        if (mid.empty())
            continue;
        // league: match-level league_id 优先 (esports), 否则当前 category id (cricket)。
        const std::string_view mlid = AttrIn(hdr, "league_id");
        const std::string league = !mlid.empty() ? std::string(mlid) : current_league;

        std::string home, away;
        int hs = 0, as = 0;
        TeamIn(span, "localteam", spec.score_attr, home, hs);
        TeamIn(span, spec.away_tag, spec.score_attr, away, as);
        if (home.empty() || away.empty())
            continue;

        stcpp::debug_api::EventScore es;
        es.found = true;
        es.event_id = std::string(mid);
        es.sport = std::string(spec.sport);
        es.status = "inplay";
        es.home = std::move(home);
        es.away = std::move(away);
        es.home_score = hs;
        es.away_score = as;
        es.league_id = league;
        es.source = "goalserve";
        // 无 bet365 赔率 → inplay fair 留默认 (-1); 走 score-prior。
        es.ts.event_ts_ns = ingestion_ts_ns;
        es.ts.data_source_ts_ns = ingestion_ts_ns;
        es.ts.ingestion_ts_ns = ingestion_ts_ns;
        es.ts.as_of_ts_ns = ingestion_ts_ns;
        out.push_back(std::move(es));
    }
    return out;
}

}  // namespace stcpp::data::team_livescore
