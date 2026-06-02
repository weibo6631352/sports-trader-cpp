#pragma once
// stcpp/data/tennis_scores_parser.hpp — Goalserve tennis livescore (tennis_scores/home) 解析器
//
// Owner: 老雷 (GM) | last_review: 2026-06-03
//
// 目的 (覆盖率杠杆, 2026-06-03):
//   inplay-tennis.gz 只覆盖 bet365 挂盘的 ~54 场 (atp/wta 为主), 缺 PM 大头的 ITF/Challenger 低级别。
//   tennis_scores/home 是【纯 livescore 全巡回】(431 场, 含 Challenger/ITF), 进行中 (status="Set N")
//   的实测对得上 PM 未匹配的 ITF 盘 (M. Malige / M. Trevisan 等)。接它作补充比分源 → 拉高 tennis 覆盖。
//   代价: 无 bet365 赔率 → 这些场无 inplay sharp fair (#18), 只有 score-prior fair; 但覆盖率↑+比分特征。
//
// XML 层级 (实测 2026-06-03):
//   <category name="Challenger Men - Singles: ..." id="...">
//     <match date=".." time=".." status="Set 2" id="2602466">
//       <player name="A. Surname" serve=".." s1="6.5" s2=".." totalscore="0" winner="False" .../>
//       <player name="B. Surname" ... totalscore="1" .../>
//   totalscore = 已赢盘数 (sets won); status="Set N"=进行中, "Finished"/"Not Started"=非 live。
//   纯函数, fail-soft; 只产 live (Set N) 场, 喂 ScoreSnapshotStore (与 inplay 合并)。

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "stcpp/debug_api/state_provider.hpp"  // EventScore / FourTs

namespace stcpp::data::tennis_scores {

namespace detail {
[[nodiscard]] inline std::string_view AttrIn(std::string_view block, std::string_view attr) noexcept {
    std::string needle;
    needle.reserve(attr.size() + 2);
    needle.append(attr);
    needle.append("=\"");
    // 单词边界: attr 前须空白/'<' (防 "id" 误命中 "dp1"/嵌套子串)
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
}  // namespace detail

// ParseTennisScoresLive — tennis_scores/home XML → 进行中比赛的 EventScore (键: match id)。
//   ingestion_ts_ns: 抓取本地刻 (R-20 ingestion); 比分源 ts 用 feed (livescore 无精确 ts → 用 ingestion)。
[[nodiscard]] inline std::vector<stcpp::debug_api::EventScore> ParseTennisScoresLive(
    std::string_view xml, std::int64_t ingestion_ts_ns) {
    using detail::AttrIn;
    using detail::ParseIntSafe;
    std::vector<stcpp::debug_api::EventScore> out;
    std::string current_league;
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
            // AttrIn 单词边界匹配 id=" (前须空白) → 取 <category ... id="X"> 的 id (不会误命中别的)。
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

        const auto match_hdr_end = span.find('>');
        if (match_hdr_end == std::string_view::npos)
            continue;
        const std::string_view hdr = span.substr(0, match_hdr_end);
        const std::string_view status = AttrIn(hdr, "status");
        // 仅进行中 (status="Set N"); 其余 (Finished/Not Started/Interrupted/...) 跳过。
        if (status.size() < 3 || status.substr(0, 3) != "Set")
            continue;
        const std::string_view mid = AttrIn(hdr, "id");
        if (mid.empty())
            continue;

        // 两 player: name + totalscore (已赢盘数) + s1..s5 (各盘已打局数 → 全场总局, totals/spreads 用)
        std::string p0, p1;
        int ts0 = 0, ts1 = 0;
        int gm0 = 0, gm1 = 0;  // 全场总局数 = s1+..+s5
        std::size_t pp = match_hdr_end;
        int pidx = 0;
        while (pidx < 2) {
            const auto pk = span.find("<player", pp);
            if (pk == std::string_view::npos)
                break;
            const auto pe = span.find("/>", pk);
            const std::size_t phe = (pe != std::string_view::npos) ? pe : span.find('>', pk);
            if (phe == std::string_view::npos)
                break;
            const std::string_view phdr = span.substr(pk, phe - pk);
            const std::string_view nm = AttrIn(phdr, "name");
            const int tsv = ParseIntSafe(AttrIn(phdr, "totalscore"));
            // s1..s5 各盘局数求和 (空属性 ParseIntSafe→0; tiebreak 盘记 7)
            int gsum = 0;
            for (const char* sk : {"s1", "s2", "s3", "s4", "s5"})
                gsum += ParseIntSafe(AttrIn(phdr, sk));
            if (pidx == 0) {
                p0 = std::string(nm);
                ts0 = tsv;
                gm0 = gsum;
            } else {
                p1 = std::string(nm);
                ts1 = tsv;
                gm1 = gsum;
            }
            ++pidx;
            pp = phe + 1;
        }
        if (p0.empty() || p1.empty())
            continue;

        stcpp::debug_api::EventScore es;
        es.found = true;
        es.event_id = std::string(mid);
        es.sport = "tennis";
        es.status = "inplay";
        es.period = std::string(status);  // "Set N"
        es.home = std::move(p0);
        es.away = std::move(p1);
        es.home_score = ts0;  // 已赢盘数
        es.away_score = ts1;
        es.games_home = gm0;  // 全场已打局数 (totals/spreads 用)
        es.games_away = gm1;
        es.league_id = current_league;
        es.source = "goalserve";
        // 比分源无 bet365 赔率 → inplay fair 留 -1 (默认); 这些场走 score-prior fair。
        // R-20: livescore feed 无精确 payload ts → 4ts 用 ingestion (允许带 IngestionFallback 标记)。
        es.ts.event_ts_ns = ingestion_ts_ns;
        es.ts.data_source_ts_ns = ingestion_ts_ns;
        es.ts.ingestion_ts_ns = ingestion_ts_ns;
        es.ts.as_of_ts_ns = ingestion_ts_ns;
        out.push_back(std::move(es));
    }
    return out;
}

}  // namespace stcpp::data::tennis_scores
