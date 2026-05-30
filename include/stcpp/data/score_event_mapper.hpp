// stcpp/data/score_event_mapper.hpp — Goalserve inplay ↔ Polymarket event 映射层
//
// Owner: 小段 (goalserve-specialist, #37)
// Date:  2026-05-30
// Task:  GM 派单 — 接 Goalserve inplay ↔ Polymarket event↔condition 映射
//        依据 docs/RESEARCH/laoli-xiaoduan-cross-source-mapping-v1.md §1 / §2
//
// 问题背景 (小宫 B3):
//   ScoreSnapshotStore 以 Goalserve inplay_match_id (134xxxxxxx 格式) 为 key 存比分.
//   RealStateProvider.score(event_id) 传入 Polymarket event_id (gamma Event id 字符串).
//   两者 ID 体系完全不同 → score() 永远 miss (found=false).
//
// 解决思路:
//   维护一张 Polymarket event_id → Goalserve inplay_match_id 的双向映射表.
//   填表时优先精确匹配 (gameId 路径), 否则走 fuzzy match (队名 + 开赛时间).
//   RealStateProvider.score() 先过此映射层再查 ScoreSnapshotStore.
//
// 映射策略 (laoli-xiaoduan §1.2 两步 lookup):
//
//   路径 A — gameId 精确匹配:
//     Polymarket gamma event 有 gameId 字段 → 等于 Goalserve pregame_match_id (6位整数)
//     pregame_match_id → inplay_match_id 需查 inplay-mapping endpoint (§1.2 Step 1)
//     MVP: 若 inplay-mapping 表已缓存此 pregame id, 则精确命中
//
//   路径 B — 队名 + 开赛时间 fuzzy match:
//     当 gameId 为空或 pregame→inplay 映射表无此 id 时触发
//     匹配规则:
//       1. 队名一致化 (ToLower + TrimSpaces + StripPunctuation)
//       2. 双向包含检测 (防"Man Utd" vs "Manchester United" 缩写差异):
//          norm_pm ⊆ norm_gs OR norm_gs ⊆ norm_pm (≥ kMinFuzzyNameLen 字符)
//       3. 时间邻近: |pm_start_ts - gs_start_ts| ≤ kTimeFuzzyWindowSec (900s)
//     sport 范围 (MVP): Soccer / Basketball / Tennis
//
// 匹配统计 (供 /metrics 的 score_matched 使用):
//   matched_count / total_attempted + 失败原因分布
//
// R-20 合规: 本类只读/写映射表, 不产生时间戳
// R-12 合规: Refresh 在采集线程调用; Resolve 在 debug_api 读线程调用 (mutex < 1us)
//
// 线程安全: 全部公共方法持 mu_ mutex, 临界区极短 (map lookup / swap)
//
// 不耻下问:
//   @老周: 映射表内存模型 review (当前 mutex + unordered_map, 可升 RCU-lite)
//   @体育专家: 队名缩写词典 (MVP 先覆盖双向包含)

#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "stcpp/data/goalserve_client.hpp"  // GoalserveSport

namespace stcpp::data {

// ============================================================================
// MissReason — 匹配失败原因枚举
// ============================================================================
enum class MissReason : std::uint8_t {
    NoInplayMappingForGameId = 0,  // 有 gameId 但 pregame→inplay 表无此条目
    NoInplayCandidates = 1,        // 无 gameId, fuzzy 候选集为空 (该 sport 无 inplay 数据)
    FuzzyNameMismatch = 2,         // fuzzy: 队名不匹配
    FuzzyTimeMismatch = 3,         // fuzzy: 时间差超出窗口
    NoInplayScoreData = 4,         // gameId 映射到 inplay_id 但 store 无比分数据
    NoMatchableFields = 5,         // event 既无 gameId 也无队名/时间 → 无法匹配
};

[[nodiscard]] inline const char* MissReasonName(MissReason r) noexcept {
    switch (r) {
        case MissReason::NoInplayMappingForGameId:
            return "NoInplayMappingForGameId";
        case MissReason::NoInplayCandidates:
            return "NoInplayCandidates";
        case MissReason::FuzzyNameMismatch:
            return "FuzzyNameMismatch";
        case MissReason::FuzzyTimeMismatch:
            return "FuzzyTimeMismatch";
        case MissReason::NoInplayScoreData:
            return "NoInplayScoreData";
        case MissReason::NoMatchableFields:
            return "NoMatchableFields";
    }
    return "Unknown";
}

// ============================================================================
// MatchStats — 一个 Refresh 周期的匹配统计
// ============================================================================
struct MatchStats {
    std::int64_t matched_count = 0;    // 成功映射的 event 数量
    std::int64_t total_attempted = 0;  // 尝试匹配的 event 数量

    // 失败明细
    std::int64_t miss_no_inplay_mapping = 0;     // NoInplayMappingForGameId
    std::int64_t miss_no_inplay_candidates = 0;  // NoInplayCandidates
    std::int64_t miss_fuzzy_name = 0;            // FuzzyNameMismatch
    std::int64_t miss_fuzzy_time = 0;            // FuzzyTimeMismatch
    std::int64_t miss_no_score_data = 0;         // NoInplayScoreData
    std::int64_t miss_no_matchable_fields = 0;   // NoMatchableFields

    // 命中路径细分
    std::int64_t matched_via_game_id = 0;  // 精确 gameId 路径命中
    std::int64_t matched_via_fuzzy = 0;    // fuzzy 路径命中

    void IncrMiss(MissReason r) noexcept {
        switch (r) {
            case MissReason::NoInplayMappingForGameId:
                ++miss_no_inplay_mapping;
                break;
            case MissReason::NoInplayCandidates:
                ++miss_no_inplay_candidates;
                break;
            case MissReason::FuzzyNameMismatch:
                ++miss_fuzzy_name;
                break;
            case MissReason::FuzzyTimeMismatch:
                ++miss_fuzzy_time;
                break;
            case MissReason::NoInplayScoreData:
                ++miss_no_score_data;
                break;
            case MissReason::NoMatchableFields:
                ++miss_no_matchable_fields;
                break;
        }
    }

    // 总 miss 数
    [[nodiscard]] std::int64_t total_miss() const noexcept { return total_attempted - matched_count; }

    // 匹配率 [0.0, 1.0], 无 attempt 时返回 0.0
    [[nodiscard]] double match_rate() const noexcept {
        if (total_attempted <= 0)
            return 0.0;
        return static_cast<double>(matched_count) / static_cast<double>(total_attempted);
    }
};

// ============================================================================
// InplayCandidate — Goalserve inplay 赛事候选 (供 fuzzy match)
//
// 来源: ScoreSnapshotStore 当前快照的 GameScoreRecord
// ============================================================================
struct InplayCandidate {
    std::string inplay_match_id;    // ScoreSnapshotStore key (9位整数字符串)
    std::string home_team;          // 主队名 (来自 info.name "Home vs Away")
    std::string away_team;          // 客队名
    std::int64_t start_ts_sec = 0;  // 比赛开始 Unix sec (0 = unknown)
    goalserve::GoalserveSport sport = goalserve::GoalserveSport::Soccer;
};

// ============================================================================
// ScoreEventMapper — Goalserve inplay ↔ Polymarket event ID 映射层
//
// 核心接口:
//   写侧 (采集线程, ~1s 周期):
//     SetInplayCandidates(sport, candidates)     — 更新 fuzzy 候选集
//     SetPolymarketEvents(events)                — 更新 PM event 目录
//     SetPregameToInplayMapping(mapping)         — 更新 pregame→inplay 表
//     Refresh()                                  — 驱动映射更新, 返回统计
//
//   读侧 (debug_api 线程):
//     Resolve(pm_event_id)                       — 查 inplay_match_id (O(1))
//     LastStats()                                — 最近统计快照
// ============================================================================
class ScoreEventMapper {
public:
    // 时间 fuzzy 窗口 (秒): 15min
    // PM gamma event.startTime 与 Goalserve start_ts 差异通常 < 5min
    // 15min 容忍 gamma 数据滞后 + 不同时区表示
    static constexpr std::int64_t kTimeFuzzyWindowSec = 900;

    // 队名 fuzzy 最短边长度 (字符数)
    // < 5 字符缩写误匹配风险过高 (e.g. "LA" vs "LA Lakers" 会错误包含)
    static constexpr std::size_t kMinFuzzyNameLen = 5;

    ScoreEventMapper() = default;

    ScoreEventMapper(const ScoreEventMapper&) = delete;
    ScoreEventMapper& operator=(const ScoreEventMapper&) = delete;
    ScoreEventMapper(ScoreEventMapper&&) = delete;
    ScoreEventMapper& operator=(ScoreEventMapper&&) = delete;

    ~ScoreEventMapper() = default;

    // -------------------------------------------------------------------------
    // PmEventRecord — Polymarket event 轻量描述
    // 不依赖 state_provider.hpp::EventInfo (data 层零 debug_api 反向依赖)
    // -------------------------------------------------------------------------
    struct PmEventRecord {
        std::string event_id;           // gamma Event id (不透明字符串)
        std::string title;              // gamma title (e.g. "Will Lakers win vs Celtics?")
        std::string game_id;            // Polymarket gameId (== Goalserve pregame_match_id)
        std::string home_team;          // 从 title/slug 解析, 可空
        std::string away_team;          // 从 title/slug 解析, 可空
        std::int64_t start_ts_sec = 0;  // 开赛 Unix sec (0 = unknown)
        std::string sport_slug;         // "soccer" / "basketball" / "tennis" / "nba" / "nfl" ...
    };

    // -------------------------------------------------------------------------
    // SetInplayCandidates — 更新指定 sport 的 Goalserve inplay 候选集
    // 采集线程每次 ScoreSnapshotStore Publish 后调用
    // -------------------------------------------------------------------------
    void SetInplayCandidates(goalserve::GoalserveSport sport,
                             std::vector<InplayCandidate> candidates) noexcept;

    // -------------------------------------------------------------------------
    // SetPolymarketEvents — 更新 Polymarket event 目录
    // gamma /events 刷新后由采集线程调用 (启动时 + 定期)
    // -------------------------------------------------------------------------
    void SetPolymarketEvents(std::vector<PmEventRecord> events) noexcept;

    // -------------------------------------------------------------------------
    // SetPregameToInplayMapping — 更新 Goalserve pregame→inplay ID 映射表
    // 来源: /inplay-mapping?json=1 endpoint (§1.2 Step 1)
    // pregame_match_id (6位) → inplay_match_id (9位)
    // -------------------------------------------------------------------------
    void SetPregameToInplayMapping(std::unordered_map<std::string, std::string> pregame_to_inplay) noexcept;

    // -------------------------------------------------------------------------
    // Refresh — 驱动一次完整映射更新, 返回 MatchStats
    //
    // 对每个 PmEventRecord 尝试:
    //   1. 精确路径 (gameId): game_id → pregame_to_inplay_ → inplay_match_id
    //   2. fuzzy 路径: 同 sport 候选集中队名 + 时间匹配
    // 更新 event_id_to_inplay_ 正向表; 原有成功映射在新周期重验证 (防 stale 映射)
    // -------------------------------------------------------------------------
    [[nodiscard]] MatchStats Refresh() noexcept;

    // -------------------------------------------------------------------------
    // Resolve — 查 pm_event_id → inplay_match_id
    // 返回空字符串 = 未找到 (caller 降级为 found=false)
    // R-12: O(1) hash lookup, mutex 持锁 < 5ns
    // -------------------------------------------------------------------------
    [[nodiscard]] std::string Resolve(const std::string& pm_event_id) const noexcept;

    // -------------------------------------------------------------------------
    // LastStats — 最近一次 Refresh 统计 (线程安全)
    // -------------------------------------------------------------------------
    [[nodiscard]] MatchStats LastStats() const noexcept;

    // ---- 静态工具 (供测试 + 内部使用) ----

    // 队名一致化: ToLower + TrimSpaces + StripPunctuation
    [[nodiscard]] static std::string NormalizeName(const std::string& raw) noexcept;

    // 队名 fuzzy 匹配: norm_a ⊆ norm_b OR norm_b ⊆ norm_a (且最短边 ≥ kMinFuzzyNameLen)
    [[nodiscard]] static bool NameFuzzyMatch(const std::string& norm_a, const std::string& norm_b) noexcept;

    // sport_slug → GoalserveSport (e.g. "soccer"→Soccer, "nba"/"basketball"→Basketball)
    // 返回 std::nullopt = 未识别
    [[nodiscard]] static std::optional<goalserve::GoalserveSport> ParseSportSlug(
        const std::string& slug) noexcept;

private:
    mutable std::mutex mu_;

    // Polymarket event 目录
    std::vector<PmEventRecord> pm_events_;

    // Goalserve inplay 候选集: sport (uint8) → candidates
    std::unordered_map<std::uint8_t, std::vector<InplayCandidate>> inplay_candidates_;

    // Goalserve pregame→inplay ID 映射 (来自 inplay-mapping endpoint)
    std::unordered_map<std::string, std::string> pregame_to_inplay_;

    // 正向映射结果: pm_event_id → inplay_match_id
    std::unordered_map<std::string, std::string> event_id_to_inplay_;

    // 最近 Refresh 统计
    MatchStats last_stats_;

    // ---- 内部辅助 (由 Refresh 在持锁环境中调用, 无锁版本) ----

    // 精确路径
    [[nodiscard]] std::string ResolveViaGameIdLocked(const PmEventRecord& ev) const noexcept;

    // fuzzy 路径
    [[nodiscard]] std::string ResolveViaFuzzyLocked(const PmEventRecord& ev,
                                                    MissReason& reason_out) const noexcept;
};

}  // namespace stcpp::data
