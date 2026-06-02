// src/stcpp/app/event_matcher.cpp — condition_id ↔ Goalserve event 映射桥实现 (A0)
//
// Owner: 老雷 (GM)  last_review: 2026-05-30
// fail-closed: 匹配不上绝不猜 (见头文件红线注释)。

#include "stcpp/app/event_matcher.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

#include "stcpp/app/team_alias.hpp"  // sport-aware: ClassifySportMatch / CanonicalTeam

namespace stcpp::app {

namespace {
// FoldDiacritics — UTF-8 变音符 → ASCII base (名字映射, 2026-06-02 老板「名字可能不一样」)。
//   修真 bug: NormalizeTeamTokens 按字节切, UTF-8 重音字符 (如 "Cerúndolo" 的 ú=0xC3 0xBA)
//   每字节 >127 → 被当分隔符 → token 碎裂 ("cer"+"ndolo") → 与 Goalserve "Cerundolo" 交集 0 不匹配。
//   折叠后两边归一到 ASCII (Cerúndolo→cerundolo, Ðoković→dokovic) → 国际选手/球队名可匹配。
//   覆盖 Latin-1 Supplement (0xC3 前缀: à-ÿ 西欧重音) + 常见 Slavic Latin Extended-A (网球高频)。
std::string FoldDiacritics(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        char base = 0;
        if (c == 0xC3 && i + 1 < s.size()) {  // U+00C0..U+00FF (À-ÿ)
            const unsigned char d = static_cast<unsigned char>(s[i + 1]);
            if ((d >= 0x80 && d <= 0x85) || (d >= 0xA0 && d <= 0xA5)) base = 'a';       // À-Å à-å
            else if (d == 0x87 || d == 0xA7) base = 'c';                                 // Ç ç
            else if ((d >= 0x88 && d <= 0x8B) || (d >= 0xA8 && d <= 0xAB)) base = 'e';   // È-Ë è-ë
            else if ((d >= 0x8C && d <= 0x8F) || (d >= 0xAC && d <= 0xAF)) base = 'i';   // Ì-Ï ì-ï
            else if (d == 0x91 || d == 0xB1) base = 'n';                                 // Ñ ñ
            else if ((d >= 0x92 && d <= 0x96) || (d >= 0xB2 && d <= 0xB6)) base = 'o';   // Ò-Ö ò-ö
            else if ((d >= 0x99 && d <= 0x9C) || (d >= 0xB9 && d <= 0xBC)) base = 'u';   // Ù-Ü ù-ü
            else if (d == 0x9D || d == 0xBD || d == 0xBF) base = 'y';                    // Ý ý ÿ
            if (base) { out.push_back(base); ++i; continue; }
        } else if (c == 0xC4 && i + 1 < s.size()) {  // Latin Extended-A 子集 (Slavic)
            const unsigned char d = static_cast<unsigned char>(s[i + 1]);
            if (d == 0x86 || d == 0x87 || d == 0x8C || d == 0x8D) base = 'c';            // Ć ć Č č
            else if (d == 0x90 || d == 0x91) base = 'd';                                 // Đ đ
            else if (d >= 0x80 && d <= 0x85) base = 'a';                                 // Ā ā Ă ă Ą ą
            else if (d >= 0x92 && d <= 0x9B) base = 'e';                                 // Ē-ě 区
            if (base) { out.push_back(base); ++i; continue; }
        } else if (c == 0xC5 && i + 1 < s.size()) {  // Latin Extended-A 子集 (Slavic)
            const unsigned char d = static_cast<unsigned char>(s[i + 1]);
            if (d == 0xA0 || d == 0xA1) base = 's';                                      // Š š
            else if (d == 0xBD || d == 0xBE) base = 'z';                                 // Ž ž
            else if (d >= 0x84 && d <= 0x88) base = 'n';                                 // Ń-ň 区
            else if (d >= 0x98 && d <= 0x9B) base = 'r';                                 // Ř ř 区
            if (base) { out.push_back(base); ++i; continue; }
        }
        out.push_back(s[i]);
    }
    return out;
}
}  // namespace

// ---------------------------------------------------------------------------
// NormalizeTeamTokens — fold 变音符 → lowercase + alnum token 集合 (去重排序)
// ---------------------------------------------------------------------------
std::vector<std::string> EventMatcher::NormalizeTeamTokens(const std::string& name) {
    const std::string folded = FoldDiacritics(name);  // 先折重音 → ASCII (国际选手名映射)
    std::vector<std::string> tokens;
    std::string cur;
    cur.reserve(folded.size());
    for (char c : folded) {
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

namespace {
// 个人项目 (网球/MMA) 相似度: 共享一个 ≥3 字符 token (= 姓) → 1.0 (语序无关:
//   "Daria Khomutsianskaya" vs "Khomutsianskaya D." 共享 khomutsianskaya)。无共享姓 → 回退通用。
double IndividualSim(const std::string& a, const std::string& b) {
    const auto ta = EventMatcher::NormalizeTeamTokens(a);
    const auto tb = EventMatcher::NormalizeTeamTokens(b);
    std::size_t i = 0, j = 0;
    while (i < ta.size() && j < tb.size()) {
        if (ta[i] == tb[j]) {
            if (ta[i].size() >= 3) return 1.0;  // 共享真姓 (非首字母) → 强匹配
            ++i;
            ++j;
        } else if (ta[i] < tb[j]) {
            ++i;
        } else {
            ++j;
        }
    }
    return EventMatcher::TeamSimilarity(a, b);  // 无共享姓 → 通用回退
}

// sport-aware 相似度: 团队→规范队 ID 精确比 (解析不出回退通用); 个人→姓锚; 未知→通用。
//   加法语义: 精确解析失败一律回退通用, 故只增不减 (绝不退化)。
double PairSim(const std::string& sport, const std::string& a, const std::string& b) {
    switch (ClassifySportMatch(sport)) {
        case SportMatchCategory::kTeam: {
            const std::string ca = CanonicalTeam(sport, a);
            const std::string cb = CanonicalTeam(sport, b);
            if (!ca.empty() && !cb.empty())
                return (ca == cb) ? 1.0 : 0.0;       // 两边都解析到规范队 → 精确判定
            return EventMatcher::TeamSimilarity(a, b);  // 任一未解析 → 通用回退
        }
        case SportMatchCategory::kIndividual:
            return IndividualSim(a, b);
        default:
            return EventMatcher::TeamSimilarity(a, b);
    }
}
}  // namespace

// ---------------------------------------------------------------------------
// Match — 从候选 EventScore 找最佳匹配 (fail-closed)
// ---------------------------------------------------------------------------
EventMatchResult EventMatcher::Match(const EventMatchInput& in,
                                     const std::vector<debug_api::EventScore>& candidates) const {
    EventMatchResult best;  // matched=false 默认 (fail-closed 起点)
    if (in.team0.empty() || in.team1.empty()) {
        return best;  // market 队名缺失 → 无法锚定
    }

    // 覆盖率诊断 (本次 Match 局部): 名字是否配上 + 名字候选被哪道门拒。
    bool any_namematch = false;
    int rej_orient_here = 0;
    int rej_kick_here = 0;

    for (const auto& ev : candidates) {
        if (ev.home.empty() || ev.away.empty()) {
            continue;
        }

        // 双向分配: 直配 (t0→home, t1→away) vs 交叉配 (t0→away, t1→home), 取每队更优.
        //   PairSim 按运动分派 (团队→规范队 ID / 个人→姓锚 / 其余→通用)。
        const double direct0 = PairSim(in.sport, in.team0, ev.home);
        const double direct1 = PairSim(in.sport, in.team1, ev.away);
        const double cross0 = PairSim(in.sport, in.team0, ev.away);
        const double cross1 = PairSim(in.sport, in.team1, ev.home);

        const double direct_min = std::min(direct0, direct1);
        const double cross_min = std::min(cross0, cross1);
        // 选让"较弱一队"更强的分配 (双队都得过门, 故看 min)
        const double team_min = std::max(direct_min, cross_min);
        const double team_sum = (direct_min >= cross_min) ? (direct0 + direct1) : (cross0 + cross1);

        // 合格门 1: 双队各自 overlap ≥ 阈值 (用所选分配的 min)
        if (team_min < cfg_.team_sim_threshold) {
            continue;
        }
        any_namematch = true;  // 名字配上了 (过 threshold); 下面若被拒 = 可恢复缺口

        // 合格门 1.5 (P2-1, 老郭审查): orientation fail-closed —— 直配/交叉两种分配都过门
        //   且分数接近时, yes_is_home 靠 >= 任意拍一边 = 比分方向可能接反 = 镜像 fair =
        //   系统性反向下单。此时 orientation 模糊, 宁可不匹配 (fail-closed), 不交易该盘。
        if (direct_min >= cfg_.team_sim_threshold && cross_min >= cfg_.team_sim_threshold &&
            std::abs(direct_min - cross_min) < cfg_.orientation_margin) {
            ++rej_orient_here;
            continue;  // orientation 模糊 → fail-closed (防反向下单)
        }

        // 合格门 2: kickoff 时间窗口 (两侧均已知才检查; 任一未知 → 不据时间否决)
        if (in.kickoff_ts_sec > 0 && ev.kickoff_ts_sec > 0) {
            const std::int64_t diff = std::llabs(in.kickoff_ts_sec - ev.kickoff_ts_sec);
            if (diff > cfg_.kickoff_window_sec) {
                ++rej_kick_here;
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

    // 诊断累计 (relaxed; 只在「名字配上却没匹配」时记凶手门, 隔离可恢复缺口)。
    diag_calls_.fetch_add(1, std::memory_order_relaxed);
    if (best.matched) {
        diag_matched_.fetch_add(1, std::memory_order_relaxed);
    }
    if (any_namematch) {
        diag_namematch_found_.fetch_add(1, std::memory_order_relaxed);
        if (!best.matched) {
            diag_namematch_but_unmatched_.fetch_add(1, std::memory_order_relaxed);
            diag_rej_orientation_.fetch_add(rej_orient_here, std::memory_order_relaxed);
            diag_rej_kickoff_.fetch_add(rej_kick_here, std::memory_order_relaxed);
        }
    }

    return best;
}

}  // namespace stcpp::app
