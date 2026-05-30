// stcpp/data/score_event_mapper.cpp — ScoreEventMapper 实现
//
// Owner: 小段 (goalserve-specialist, #37)
// Date:  2026-05-30
// 依据: docs/RESEARCH/laoli-xiaoduan-cross-source-mapping-v1.md §1 / §2
//
// 映射逻辑:
//   1. 精确 gameId 路径 (ResolveViaGameIdLocked):
//      pm_event.game_id (== Goalserve pregame_match_id)
//      → pregame_to_inplay_ 表 (来自 /inplay-mapping?json=1)
//      → inplay_match_id (ScoreSnapshotStore key)
//
//   2. Fuzzy 路径 (ResolveViaFuzzyLocked):
//      同 sport 候选集 → 对每个 InplayCandidate:
//        a. NormalizeName(home_team) fuzzy match NormalizeName(pm.home_team)
//           AND NormalizeName(away_team) fuzzy match NormalizeName(pm.away_team)
//        b. |pm.start_ts_sec - cand.start_ts_sec| ≤ kTimeFuzzyWindowSec
//           (若任一 start_ts = 0 则跳过时间检测, 仅靠队名)
//      返回第一个通过双重检验的 candidate.inplay_match_id
//
// 无锁覆盖注意: Refresh 统一持锁后调用 Locked 内部函数, 避免重入
// R-12: Resolve 持锁时间 < 1us (unordered_map::find)

#include "stcpp/data/score_event_mapper.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string_view>

namespace stcpp::data {

// ============================================================================
// NormalizeName — 队名一致化
//
// 操作:
//   1. 全部转小写
//   2. 去除首尾空格 + 内部连续空格压缩为单空格
//   3. 去除标点 (保留字母/数字/空格)
//   4. 结果再 trim
//
// 示例:
//   "Manchester United F.C." → "manchester united fc"
//   "LA  Lakers"             → "la lakers"
//   "Djoković, N."           → "djokovié n" (非 ASCII 保留, 不做 unicode 折叠)
//   Note: 非 ASCII 字符 (e.g. é ç) 保留, 仅处理 ASCII 范围标点
//         完整 unicode fold 留给后续迭代 (需引入 ICU, MVP 不引入)
// ============================================================================
std::string ScoreEventMapper::NormalizeName(const std::string& raw) noexcept {
    std::string out;
    out.reserve(raw.size());
    bool last_space = true;  // 初始 true = 跳前导空格

    for (char ch : raw) {
        const auto uc = static_cast<unsigned char>(ch);
        if (std::isalnum(uc)) {
            out.push_back(static_cast<char>(std::tolower(uc)));
            last_space = false;
        } else if (uc == ' ' || uc == '\t' || uc == '\n' || uc == '\r') {
            if (!last_space) {
                out.push_back(' ');
                last_space = true;
            }
        } else if (uc > 127U) {
            // 非 ASCII 字符原样保留 (多语言队名)
            out.push_back(ch);
            last_space = false;
        }
        // ASCII 标点/特殊字符 → 丢弃 (e.g. '.', ',', '-', '(', ')')
    }

    // 去尾部空格
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

// ============================================================================
// NameFuzzyMatch — 队名 fuzzy 匹配
//
// 规则: norm_a ⊆ norm_b OR norm_b ⊆ norm_a (且最短边 ≥ kMinFuzzyNameLen)
//   设计目的: 处理队名缩写 ("Man Utd" ⊆ "manchester united")
//             及全名变体 ("lal" ⊆ "la lakers")
//
// 注意: std::string::find 为 O(n*m), 队名通常 < 40 字符, 完全可接受
// ============================================================================
bool ScoreEventMapper::NameFuzzyMatch(const std::string& norm_a, const std::string& norm_b) noexcept {
    const auto la = norm_a.size();
    const auto lb = norm_b.size();
    const auto min_len = std::min(la, lb);

    if (min_len < kMinFuzzyNameLen) {
        return false;  // 太短 → 误匹配风险高
    }

    // 精确相等
    if (norm_a == norm_b)
        return true;

    // norm_a ⊆ norm_b (a 是 b 的子串)
    if (la <= lb && norm_b.find(norm_a) != std::string::npos)
        return true;

    // norm_b ⊆ norm_a (b 是 a 的子串)
    if (lb < la && norm_a.find(norm_b) != std::string::npos)
        return true;

    return false;
}

// ============================================================================
// ParseSportSlug — sport 字符串 → GoalserveSport
//
// 覆盖 MVP sport + 常见 Polymarket tag 别名:
//   "soccer" / "football" (欧式) → Soccer
//   "basketball" / "nba" / "ncaab" → Basketball
//   "tennis" → Tennis
// ============================================================================
std::optional<goalserve::GoalserveSport> ScoreEventMapper::ParseSportSlug(const std::string& slug) noexcept {
    // 转小写后匹配
    std::string low;
    low.reserve(slug.size());
    for (char ch : slug) {
        low.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }

    if (low == "soccer" || low == "football" || low == "fifa" || low == "mls" || low == "champions league" ||
        low == "premier league") {
        return goalserve::GoalserveSport::Soccer;
    }
    if (low == "basketball" || low == "nba" || low == "ncaab" || low == "euroleague") {
        return goalserve::GoalserveSport::Basketball;
    }
    if (low == "tennis" || low == "atp" || low == "wta" || low == "grand slam") {
        return goalserve::GoalserveSport::Tennis;
    }
    if (low == "hockey" || low == "nhl" || low == "ice hockey") {
        return goalserve::GoalserveSport::Hockey;
    }
    if (low == "baseball" || low == "mlb") {
        return goalserve::GoalserveSport::Baseball;
    }
    if (low == "american football" || low == "nfl" || low == "ncaaf") {
        return goalserve::GoalserveSport::AmericanFootball;
    }
    if (low == "volleyball") {
        return goalserve::GoalserveSport::Volleyball;
    }
    if (low == "esports" || low == "esport" || low == "cs2" || low == "lol" || low == "dota2") {
        return goalserve::GoalserveSport::Esports;
    }
    return std::nullopt;
}

// ============================================================================
// SetInplayCandidates
// ============================================================================
void ScoreEventMapper::SetInplayCandidates(goalserve::GoalserveSport sport,
                                           std::vector<InplayCandidate> candidates) noexcept {
    const auto key = static_cast<std::uint8_t>(sport);
    std::lock_guard<std::mutex> lk(mu_);
    inplay_candidates_[key] = std::move(candidates);
}

// ============================================================================
// SetPolymarketEvents
// ============================================================================
void ScoreEventMapper::SetPolymarketEvents(std::vector<PmEventRecord> events) noexcept {
    std::lock_guard<std::mutex> lk(mu_);
    pm_events_ = std::move(events);
}

// ============================================================================
// SetPregameToInplayMapping
// ============================================================================
void ScoreEventMapper::SetPregameToInplayMapping(
    std::unordered_map<std::string, std::string> pregame_to_inplay) noexcept {
    std::lock_guard<std::mutex> lk(mu_);
    pregame_to_inplay_ = std::move(pregame_to_inplay);
}

// ============================================================================
// ResolveViaGameIdLocked — 精确 gameId 路径 (持锁环境调用)
//
// 步骤:
//   pm_event.game_id (pregame_match_id 字符串)
//   → pregame_to_inplay_ 表
//   → inplay_match_id
//
// 若 game_id 为空 → 直接返回 ""
// 若 pregame→inplay 表无此条目 → 返回 ""
// ============================================================================
std::string ScoreEventMapper::ResolveViaGameIdLocked(const PmEventRecord& ev) const noexcept {
    if (ev.game_id.empty())
        return "";

    const auto it = pregame_to_inplay_.find(ev.game_id);
    if (it == pregame_to_inplay_.end())
        return "";

    return it->second;
}

// ============================================================================
// ResolveViaFuzzyLocked — fuzzy 路径 (持锁环境调用)
//
// 算法:
//   1. 解析 pm_event.sport_slug → GoalserveSport (不识别则遍历所有 sport)
//   2. 对候选集每个 InplayCandidate:
//      a. home_team fuzzy match + away_team fuzzy match (双边同时满足)
//         OR 双边交叉匹配 (home↔away 都ok, 防主客场标记不一致)
//      b. 时间检测 (若 pm 和 gs 均有 start_ts)
//   3. 返回第一个通过的 candidate.inplay_match_id
//   4. 无候选 → reason=NoInplayCandidates
//      有候选但全部名字不匹配 → reason=FuzzyNameMismatch
//      名字匹配但时间全不对 → reason=FuzzyTimeMismatch
// ============================================================================
std::string ScoreEventMapper::ResolveViaFuzzyLocked(const PmEventRecord& ev,
                                                    MissReason& reason_out) const noexcept {
    // 如果 home/away 均为空 → 无法 fuzzy
    if (ev.home_team.empty() && ev.away_team.empty()) {
        reason_out = MissReason::NoMatchableFields;
        return "";
    }

    const std::string norm_pm_home = NormalizeName(ev.home_team);
    const std::string norm_pm_away = NormalizeName(ev.away_team);

    // 确定候选池 sport key
    auto sport_opt = ParseSportSlug(ev.sport_slug);

    // 收集候选集 (指定 sport 或全部 sport)
    const std::vector<InplayCandidate>* pool_ptr = nullptr;
    std::vector<InplayCandidate> merged;  // 跨 sport 合并时使用

    if (sport_opt.has_value()) {
        const auto key = static_cast<std::uint8_t>(*sport_opt);
        const auto it = inplay_candidates_.find(key);
        if (it == inplay_candidates_.end() || it->second.empty()) {
            reason_out = MissReason::NoInplayCandidates;
            return "";
        }
        pool_ptr = &it->second;
    } else {
        // sport 未识别 → 遍历所有 sport 候选
        for (const auto& [k, v] : inplay_candidates_) {
            merged.insert(merged.end(), v.begin(), v.end());
        }
        if (merged.empty()) {
            reason_out = MissReason::NoInplayCandidates;
            return "";
        }
        pool_ptr = &merged;
    }

    const auto& pool = *pool_ptr;

    // 逐候选检验
    bool any_name_match = false;
    for (const auto& cand : pool) {
        const std::string norm_gs_home = NormalizeName(cand.home_team);
        const std::string norm_gs_away = NormalizeName(cand.away_team);

        // 双边名字检验: pm_home↔gs_home AND pm_away↔gs_away
        // OR 允许翻转 (防主客场标记差异): pm_home↔gs_away AND pm_away↔gs_home
        bool home_match = false;
        bool away_match = false;
        bool name_ok = false;

        if (!norm_pm_home.empty() && !norm_gs_home.empty()) {
            home_match = NameFuzzyMatch(norm_pm_home, norm_gs_home);
        }
        if (!norm_pm_away.empty() && !norm_gs_away.empty()) {
            away_match = NameFuzzyMatch(norm_pm_away, norm_gs_away);
        }

        // 正向匹配 (pm_home ↔ gs_home AND pm_away ↔ gs_away)
        if (home_match && away_match) {
            name_ok = true;
        }
        // 交叉匹配 (pm_home ↔ gs_away AND pm_away ↔ gs_home)
        // 防 Polymarket 与 Goalserve 主客场定义不同
        if (!name_ok && !norm_pm_home.empty() && !norm_gs_away.empty() && !norm_pm_away.empty() &&
            !norm_gs_home.empty()) {
            const bool cross_home = NameFuzzyMatch(norm_pm_home, norm_gs_away);
            const bool cross_away = NameFuzzyMatch(norm_pm_away, norm_gs_home);
            if (cross_home && cross_away) {
                name_ok = true;
            }
        }

        if (!name_ok) {
            continue;
        }
        any_name_match = true;

        // 时间检验 (若双方 start_ts 均已知)
        if (ev.start_ts_sec > 0 && cand.start_ts_sec > 0) {
            const std::int64_t diff = ev.start_ts_sec - cand.start_ts_sec;
            const std::int64_t abs_diff = diff < 0 ? -diff : diff;
            if (abs_diff > kTimeFuzzyWindowSec) {
                // 名字匹配但时间差超窗口 → 继续找下一个候选
                continue;
            }
        }
        // 时间检验通过 (或任一方无 start_ts → 跳过时间检验)
        return cand.inplay_match_id;
    }

    // 无候选通过
    reason_out = any_name_match ? MissReason::FuzzyTimeMismatch : MissReason::FuzzyNameMismatch;
    return "";
}

// ============================================================================
// Refresh — 驱动完整映射更新
// ============================================================================
MatchStats ScoreEventMapper::Refresh() noexcept {
    std::lock_guard<std::mutex> lk(mu_);

    MatchStats stats;
    stats.total_attempted = static_cast<std::int64_t>(pm_events_.size());

    // 重建正向映射表 (不保留上轮结果, 防 stale 映射)
    std::unordered_map<std::string, std::string> new_map;
    new_map.reserve(pm_events_.size());

    for (const auto& ev : pm_events_) {
        // --- 路径 A: gameId 精确 ---
        if (!ev.game_id.empty()) {
            const std::string inplay_id = ResolveViaGameIdLocked(ev);
            if (!inplay_id.empty()) {
                new_map[ev.event_id] = inplay_id;
                ++stats.matched_count;
                ++stats.matched_via_game_id;
                continue;
            }
            // gameId 存在但 pregame→inplay 表无条目
            // 仍尝试 fuzzy (inplay-mapping 可能未刷新)
            // 注意: 若 gameId 非空但映射表无此 id, 不计为 miss (先走 fuzzy fallback)
        }

        // --- 路径 B: fuzzy ---
        MissReason reason = MissReason::NoMatchableFields;
        const std::string fuzzy_id = ResolveViaFuzzyLocked(ev, reason);
        if (!fuzzy_id.empty()) {
            new_map[ev.event_id] = fuzzy_id;
            ++stats.matched_count;
            ++stats.matched_via_fuzzy;
            continue;
        }

        // gameId 路径 + fuzzy 均 miss
        // 若 gameId 非空但两路均 miss → 记 NoInplayMappingForGameId
        if (!ev.game_id.empty() && reason == MissReason::NoMatchableFields) {
            reason = MissReason::NoInplayMappingForGameId;
        }
        stats.IncrMiss(reason);
    }

    event_id_to_inplay_ = std::move(new_map);
    last_stats_ = stats;
    return stats;
}

// ============================================================================
// Resolve
// ============================================================================
std::string ScoreEventMapper::Resolve(const std::string& pm_event_id) const noexcept {
    std::lock_guard<std::mutex> lk(mu_);
    const auto it = event_id_to_inplay_.find(pm_event_id);
    if (it == event_id_to_inplay_.end())
        return "";
    return it->second;
}

// ============================================================================
// LastStats
// ============================================================================
MatchStats ScoreEventMapper::LastStats() const noexcept {
    std::lock_guard<std::mutex> lk(mu_);
    return last_stats_;
}

}  // namespace stcpp::data
