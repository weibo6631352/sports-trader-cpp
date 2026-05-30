// src/stcpp/app/event_matcher.cpp — condition_id ↔ Goalserve event 映射桥实现 (A0)
//
// Owner: 老雷 (GM)  last_review: 2026-05-30
// fail-closed: 匹配不上绝不猜 (见头文件红线注释)。

#include "stcpp/app/event_matcher.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace stcpp::app {

// ---------------------------------------------------------------------------
// NormalizeTeamTokens — lowercase + alnum token 集合 (去重排序)
// ---------------------------------------------------------------------------
std::vector<std::string> EventMatcher::NormalizeTeamTokens(const std::string& name) {
    std::vector<std::string> tokens;
    std::string cur;
    cur.reserve(name.size());
    for (char c : name) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc)) {
            cur.push_back(static_cast<char>(std::tolower(uc)));
        } else if (!cur.empty()) {
            tokens.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) {
        tokens.push_back(cur);
    }
    // 去重 + 排序 (集合语义)
    std::sort(tokens.begin(), tokens.end());
    tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());
    return tokens;
}

// ---------------------------------------------------------------------------
// TeamSimilarity — overlap 系数 = |A∩B| / min(|A|,|B|)
// ---------------------------------------------------------------------------
double EventMatcher::TeamSimilarity(const std::string& a, const std::string& b) {
    const std::vector<std::string> ta = NormalizeTeamTokens(a);
    const std::vector<std::string> tb = NormalizeTeamTokens(b);
    if (ta.empty() || tb.empty()) {
        return 0.0;
    }
    // ta/tb 已排序去重 → 线性求交集大小
    std::size_t inter = 0;
    std::size_t i = 0, j = 0;
    while (i < ta.size() && j < tb.size()) {
        if (ta[i] == tb[j]) {
            ++inter;
            ++i;
            ++j;
        } else if (ta[i] < tb[j]) {
            ++i;
        } else {
            ++j;
        }
    }
    const std::size_t denom = std::min(ta.size(), tb.size());
    return static_cast<double>(inter) / static_cast<double>(denom);
}

// ---------------------------------------------------------------------------
// Match — 从候选 EventScore 找最佳匹配 (fail-closed)
// ---------------------------------------------------------------------------
EventMatchResult EventMatcher::Match(const EventMatchInput& in,
                                     const std::vector<debug_api::EventScore>& candidates) const {
    EventMatchResult best;  // matched=false 默认 (fail-closed 起点)
    if (in.team0.empty() || in.team1.empty()) {
        return best;  // market 队名缺失 → 无法锚定
    }

    for (const auto& ev : candidates) {
        if (ev.home.empty() || ev.away.empty()) {
            continue;
        }

        // 双向分配: 直配 (t0→home, t1→away) vs 交叉配 (t0→away, t1→home), 取每队更优.
        const double direct0 = TeamSimilarity(in.team0, ev.home);
        const double direct1 = TeamSimilarity(in.team1, ev.away);
        const double cross0 = TeamSimilarity(in.team0, ev.away);
        const double cross1 = TeamSimilarity(in.team1, ev.home);

        const double direct_min = std::min(direct0, direct1);
        const double cross_min = std::min(cross0, cross1);
        // 选让"较弱一队"更强的分配 (双队都得过门, 故看 min)
        const double team_min = std::max(direct_min, cross_min);
        const double team_sum = (direct_min >= cross_min) ? (direct0 + direct1) : (cross0 + cross1);

        // 合格门 1: 双队各自 overlap ≥ 阈值 (用所选分配的 min)
        if (team_min < cfg_.team_sim_threshold) {
            continue;
        }

        // 合格门 1.5 (P2-1, 老郭审查): orientation fail-closed —— 直配/交叉两种分配都过门
        //   且分数接近时, yes_is_home 靠 >= 任意拍一边 = 比分方向可能接反 = 镜像 fair =
        //   系统性反向下单。此时 orientation 模糊, 宁可不匹配 (fail-closed), 不交易该盘。
        if (direct_min >= cfg_.team_sim_threshold && cross_min >= cfg_.team_sim_threshold &&
            std::abs(direct_min - cross_min) < cfg_.orientation_margin) {
            continue;  // orientation 模糊 → fail-closed (防反向下单)
        }

        // 合格门 2: kickoff 时间窗口 (两侧均已知才检查; 任一未知 → 不据时间否决)
        if (in.kickoff_ts_sec > 0 && ev.kickoff_ts_sec > 0) {
            const std::int64_t diff = std::llabs(in.kickoff_ts_sec - ev.kickoff_ts_sec);
            if (diff > cfg_.kickoff_window_sec) {
                continue;
            }
        }

        // 合格 → 择 team_sum 最高
        if (!best.matched || team_sum > best.team_score) {
            best.matched = true;
            best.inplay_match_id = ev.event_id;
            best.team_score = team_sum;
            // orientation: 直配胜出 → YES(team0)=home; 交叉胜出 → YES=away.
            best.yes_is_home = (direct_min >= cross_min);
        }
    }

    return best;
}

}  // namespace stcpp::app
