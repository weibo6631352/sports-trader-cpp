// stcpp/data/goalserve_adapter.hpp — Goalserve vendor-agnostic adapter schema v0.1
//
// Owner: 小段 (goalserve-specialist, #37)
// Date:  2026-05-29
// Task:  ADR-037 数据地基 — inplay full feed 深挖 + vendor-agnostic adapter schema
//
// 设计目标:
//   未来换数据源 (e.g. Sportradar / Pinnacle) 只换 adapter, 不动上层 strategy / signal.
//   中间表示 (IR) 分四类: scores / stats / events / odds.
//
// 信息类型覆盖 (来自 Goalserve inplay + livescore + pregame 三路径):
//
//   1. scores  — 比分 + 时钟 + 赛事状态 (GameScoreRecord)
//      来源: inplay.goalserve.com/inplay-<sport>.gz (每秒)
//             www.goalserve.com/getfeed/<KEY>/<sport>/home (每 5s)
//      关键字段: period, score, time_status, minute, seconds
//
//   2. stats   — 球队/球员统计 (GameStatsRecord)
//      来源: commentaries / live_stats / base livescore feed
//      关键字段: shots, corners, possession, fouls, yellowcards, redcards, ...
//
//   3. events  — 赛事事件 (goal/card/substitution/VAR/scoring play) (GameEventRecord)
//      来源: livescore events 节点 + commentaries play-by-play
//      关键字段: event_type, player, minute, team, description
//
//   4. odds    — 报价 (OddsQuoteRecord)
//      来源 A: inplay.goalserve.com (bet365 单源, value_eu, 每秒)
//      来源 B: www.goalserve.com/getfeed/getodds (9 家 bookmaker, 增量 ts)
//      关键字段: market_id, outcome, value_eu, handicap, bookmaker_id, suspend
//
// R-20 四时间戳契约 (全程强制):
//   event_ts <= data_source_ts <= ingestion_ts <= as_of_ts
//   data_source_ts 来自 Goalserve 自带: updated_ts (inplay ms) / @ts (pregame sec) / .NET ticks
//   禁本地 now() 替代 data_source_ts
//
// Vendor-agnostic 设计:
//   所有 IR struct 只含语义字段, 不含 Goalserve-specific id 格式约束.
//   AdaptFromGoalserveXxx() 函数负责 Goalserve → IR 映射.
//   未来: AdaptFromSportradarXxx() 等函数可平行实现, 上层代码不变.
//
// 不耻下问:
//   - 字段语义 (period / OT / stats 语义) → 体育专家
//   - WAL 落盘 schema → @小冯 / @老王
//   - 上层 signal 接口 → @老周 (ABI lock)

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "stcpp/data/goalserve_record.hpp"  // FourTs, DataSourceTsOrigin, TimeStatus

namespace stcpp::data::adapter {

// ============================================================================
// § 0. 通用元数据
// ============================================================================

// 信息类型 (四分类)
enum class InfoKind : std::uint8_t {
    Scores = 0,  // 比分 + 时钟 + 赛事状态
    Stats = 1,   // 球队/球员统计
    Events = 2,  // 赛事事件 (goal/card/subst/VAR)
    Odds = 3,    // 赔率报价
};

// 数据源标识 (vendor)
enum class VendorId : std::uint8_t {
    Goalserve = 0,   // 当前唯一接入源
    Sportradar = 1,  // 未来可扩展
    Pinnacle = 2,    // 未来可扩展
    // 新 vendor 加枚举, 不动 IR struct
};

// 数据源端点路径 (用于 audit + 追溯)
enum class GoalserveEndpointPath : std::uint8_t {
    InplayGz = 0,       // inplay.goalserve.com/inplay-<sport>.gz (bet365 单源)
    PregameOdds = 1,    // www.goalserve.com/getfeed/.../getodds/soccer?cat=<sport>_10 (9 bm)
    LivescoreHome = 2,  // www.goalserve.com/getfeed/.../[sport]/home
    InplayResult = 3,   // inplay.goalserve.com/results/<yyyyMM>/<MID>.json
    Settlement = 4,     // oddsfeed.goalserve.com/api/v1/odds/pre-game/settlement?...
};

// MatchId: 跨 vendor 通用 match 主键 (不耦合 Goalserve 具体格式)
struct MatchId {
    std::string vendor_match_id;  // Goalserve: pregame 6-digit id 或 inplay 134xxxxxx
    std::string inplay_match_id;  // inplay.goalserve.com 专用 (可与 pregame id 不同)
    std::string league_id;        // 联赛 id (Goalserve gid / category id)
    VendorId vendor = VendorId::Goalserve;
};

// ============================================================================
// § 1. GameScoreRecord — 比分 + 时钟 + 赛事状态 (scores 类型 IR)
//
// 覆盖字段来源:
//   inplay.goalserve.com/inplay-<sport>.gz:
//     events.<id>.info.period, .score, .minute, .seconds, .state, .time_status
//   www.goalserve.com/<sport>/home:
//     match@status, match@minute, localteam@score, awayteam@score
//     scores@ts (.NET ticks), match@last_update
//   getodds pregame: match@status, @date, @time (no live clock)
//
// 体育差异:
//   period = 比赛阶段 (足球 1H/2H/ET/P; 篮球 Q1-Q4/OT; 棒球 inn1-9+; 网球 set1-5)
//   score  = 各阶段比分数组 (最多 12 槽, 与 goalserve_record::ScorePair 对齐)
// ============================================================================
struct GameScoreRecord {
    goalserve::FourTs ts{};  // R-20 四时间戳

    MatchId match_id{};
    std::string home_team;  // 主队名
    std::string away_team;  // 客队名
    goalserve::TimeStatus status = goalserve::TimeStatus::NotStarted;

    // 时钟 (inplay 才有, pregame 无)
    std::optional<std::string> period;         // 当前阶段 (e.g. "1st Half" / "Q2" / "Set 3")
    std::optional<std::int32_t> elapsed_min;   // 已用分钟 (足球/篮球)
    std::optional<std::int32_t> elapsed_sec;   // 已用秒 (额外精度)
    std::optional<std::int32_t> stoppage_min;  // 补时分钟 (足球)

    // 比分
    std::int32_t home_score_total = 0;  // 主队总分
    std::int32_t away_score_total = 0;  // 客队总分
    // 分节/分盘比分 (最多 12 槽, 0 = 未发生)
    std::array<std::int32_t, 12> home_periods{{}};
    std::array<std::int32_t, 12> away_periods{{}};
    std::uint8_t periods_used = 0;  // 实际节数

    // Goalserve-specific state (内部 5 位状态码), 可选
    std::optional<std::string> gs_state_code;  // e.g. "11007" (从 dictionaries/states 查)

    // 排定开赛 ts (Unix 秒; 仅当 payload start_ts 真实存在才填, 空串=0=未知)。EventMatcher 时间窗锚定专用。
    //   与 ts.event_ts_ns 刻意区分: event_ts 在 start_ts 空时回落 data_source_ts(now) 以满足 R-20 需有值,
    //   而本字段保持 0 → 令匹配器在 kickoff 未知时跳过时间窗 (否则 esports 等无 start_ts 的源会被
    //   "now vs PM 排定时间" 误判超窗拒绝 — 实测电竞 0 匹配根因, 2026-06-01)。
    std::int64_t scheduled_kickoff_ts_sec = 0;

    // R-20 合规检查
    [[nodiscard]] bool RespectsR20() const noexcept {
        return ts.IsMonotonic() && ts.ds_origin != goalserve::DataSourceTsOrigin::IngestionFallback;
    }
};

// ============================================================================
// § 2. GameStatsRecord — 球队/球员统计 (stats 类型 IR)
//
// 覆盖字段来源:
//   commentaries feed (每 30s): shots / corners / possession / passes / fouls / cards
//   livescore live_stats 节点 (5s): ICorner / IAttacks / IDangerousAttacks / ...
//   baseball: innings.hits / errors; cricket: wickets / overs / run_rate
// ============================================================================
struct TeamStats {
    // 射门 (足球/冰球)
    std::optional<std::int32_t> shots_total;
    std::optional<std::int32_t> shots_on_goal;
    std::optional<std::int32_t> shots_off_goal;
    std::optional<std::int32_t> shots_blocked;

    // 控球 (足球)
    std::optional<std::int32_t> possession_pct;  // 0-100

    // 角球/犯规/牌 (足球)
    std::optional<std::int32_t> corners;
    std::optional<std::int32_t> fouls;
    std::optional<std::int32_t> yellow_cards;
    std::optional<std::int32_t> red_cards;
    std::optional<std::int32_t> offsides;

    // 传球 (足球)
    std::optional<std::int32_t> passes_total;
    std::optional<std::int32_t> passes_accurate;

    // 进攻强度 (足球 live_stats)
    std::optional<std::int32_t> attacks;
    std::optional<std::int32_t> dangerous_attacks;

    // 篮球 (assists / rebounds / turnovers / steals / blocks)
    std::optional<std::int32_t> assists;
    std::optional<std::int32_t> rebounds;
    std::optional<std::int32_t> turnovers;
    std::optional<std::int32_t> steals;

    // 棒球 (hits / errors)
    std::optional<std::int32_t> hits;
    std::optional<std::int32_t> errors;

    // 板球 (wickets / overs)
    std::optional<std::int32_t> wickets;
    std::optional<std::string> overs;  // e.g. "2.1" = 2 over 1 ball
};

struct GameStatsRecord {
    goalserve::FourTs ts{};

    MatchId match_id{};
    TeamStats home_stats{};
    TeamStats away_stats{};

    [[nodiscard]] bool RespectsR20() const noexcept {
        return ts.IsMonotonic() && ts.ds_origin != goalserve::DataSourceTsOrigin::IngestionFallback;
    }
};

// ============================================================================
// § 3. GameEventRecord — 赛事事件 (events 类型 IR)
//
// 覆盖字段来源:
//   livescore events 节点: goal / yellowcard / yellowred / redcard / subst
//   commentaries play-by-play (type): Shot On Target / Goal / Corner / Foul /
//     Substitution / Yellow Card / Red Card / VAR / Penalty / Offside
//   棒球 scoring play / 板球 ball-by-ball
// ============================================================================
enum class GameEventType : std::uint8_t {
    Goal = 0,
    YellowCard = 1,
    YellowRed = 2,  // 两黄变红
    RedCard = 3,
    Substitution = 4,
    ShotOnTarget = 5,
    ShotOffTarget = 6,
    Corner = 7,
    Offside = 8,
    Penalty = 9,
    VAR = 10,
    ScoringPlay = 11,  // 棒球 / 板球 / 橄榄球 scoring event
    Other = 255,
};

struct GameEventRecord {
    goalserve::FourTs ts{};

    MatchId match_id{};
    GameEventType event_type = GameEventType::Other;

    // 时间定位
    std::optional<std::int32_t> minute;     // 比赛分钟
    std::optional<std::int32_t> extra_min;  // 补时 (足球 "90+3" 中的 3)

    // 参与者
    std::string team_side;  // "home" / "away"
    std::optional<std::string> player_name;
    std::optional<std::string> player_id;
    std::optional<std::string> assist_name;  // 助攻 / 换人入场
    std::optional<std::string> assist_id;

    // 事件详情
    std::optional<std::string> description;  // VAR reason / event comment
    std::optional<bool> own_goal;            // 乌龙球
    std::optional<bool> penalty_goal;        // 点球进球

    // Goalserve play-by-play 额外字段 (可选)
    std::optional<double> pitch_x;  // 球场坐标
    std::optional<double> pitch_y;

    [[nodiscard]] bool RespectsR20() const noexcept {
        return ts.IsMonotonic() && ts.ds_origin != goalserve::DataSourceTsOrigin::IngestionFallback;
    }
};

// ============================================================================
// § 4. OddsQuoteRecord — 赔率报价 (odds 类型 IR)
//
// 覆盖字段来源:
//   路径 A: inplay.goalserve.com/inplay-<sport>.gz
//     events.<id>.odds.<market_id>.participants.<pid>.{value_eu, handicap, suspend}
//     bookmaker 固定 = "bet365" (单源)
//   路径 B: getodds/soccer?cat=<sport>_10 (pregame, 9 bm, 增量)
//     scores.category[].matches.match[].odds.bookmaker[k].@{id, name, ts, value}
//     market type: <type value="Match Winner" id="1"> / <handicap> / <total>
//   路径 C: racing/uk (仅 UK 赛马, 18 bookmakers, 嵌入 base feed)
//
// market_id 语义: 由 dictionaries/odds-markets/<sport> 查表得名
//   soccer 常见: 1=1X2, 2=Asian Handicap, 3=Over/Under, 27=1X2 1st Half
//
// R-20: data_source_ts 来自:
//   inplay: updated_ts (Unix ms, feed 整体刷新时间)
//   pregame getodds: scores.@ts (Unix sec) 或 bookmaker.@ts (per-bm, 更精确)
//   bookmaker.@ts 各家可差 5-30 分钟 — 使用 per-bm ts 做精准 data_source_ts
// ============================================================================
enum class OddsSource : std::uint8_t {
    InplayGz = 0,     // inplay.goalserve.com (bet365 单源, 1s 刷新)
    PregameOdds = 1,  // getodds pregame (9 bm, ~30s 增量)
    RacingUk = 2,     // racing/uk (赛马特殊路径, 18 bm)
};

// 单家 bookmaker 单 outcome 报价
struct OddsQuoteRecord {
    goalserve::FourTs ts{};  // R-20: data_source_ts 来自 bm.@ts 或 updated_ts

    MatchId match_id{};

    OddsSource source = OddsSource::InplayGz;
    std::int32_t bookmaker_id = 0;  // 0 = bet365 (inplay 单源无 numeric id)
    std::string bookmaker_name;     // e.g. "bet365" / "WilliamHill" / "1xBet"

    // 盘口定位
    std::string market_id;           // e.g. "1" / "27" / "421" (inplay) 或 "Match Winner" (pregame)
    std::string market_name;         // 人类可读 (从 dictionaries 查表填充)
    std::string outcome;             // "Home" / "Draw" / "Away" / "Over" / "Under" / "Player1"
    std::optional<double> handicap;  // AH / Total 让分线 (可为负)

    // 报价值
    double value_eu = 0.0;   // 欧赔 (decimal odds), > 1.0 才有效
    bool suspended = false;  // true = 庄家暂停此 outcome

    // R-20
    [[nodiscard]] bool RespectsR20() const noexcept {
        return ts.IsMonotonic() && ts.ds_origin != goalserve::DataSourceTsOrigin::IngestionFallback;
    }

    [[nodiscard]] bool IsValid() const noexcept { return value_eu > 1.0 && !suspended && RespectsR20(); }
};

// ============================================================================
// § 5. AdaptBatch — 一次 inplay-<sport>.gz 解析后的四类 IR 批量
//
// 每次 fetch 产生一个 AdaptBatch, 包含同一 ts 下的全部 records.
// 上层按 InfoKind 分发到对应 SPSC queue.
// ============================================================================
struct AdaptBatch {
    std::int64_t data_source_ts_ns = 0;  // fetch 整体 data_source_ts (R-20)
    std::int64_t ingestion_ts_ns = 0;    // recv 完成时刻 (R-20)
    VendorId vendor = VendorId::Goalserve;
    GoalserveEndpointPath endpoint_path = GoalserveEndpointPath::InplayGz;

    std::vector<GameScoreRecord> scores;
    std::vector<GameStatsRecord> stats;
    std::vector<GameEventRecord> events;
    std::vector<OddsQuoteRecord> odds;
};

// ============================================================================
// § 6. Adaptation helpers — Goalserve raw → IR 映射规则 (声明, 实现在 .cpp)
//
// 规则:
//   AdaptInplayOddsQuote: inplay JSON events.<id>.odds → OddsQuoteRecord[]
//     data_source_ts = updated_ts (feed 整体, ms → ns)
//     bookmaker_id = 0 (bet365 inplay 无 numeric id)
//     handicap: AH / Total 参数 participants.<pid>.handicap 解析
//
//   AdaptPregameOddsQuote: getodds bookmaker[k] → OddsQuoteRecord[]
//     data_source_ts = bookmaker.@ts (sec → ns), 优先 per-bm ts
//     bookmaker_id = bookmaker.@id (14/15/16/17/18/65/105/144)
//
//   AdaptGameScore: inplay events.<id>.info → GameScoreRecord
//     period: info.period (string), minute: info.minute (string → int32)
//     time_status: info.time_status → goalserve::TimeStatus
//
//   AdaptNetDotTicks: Goalserve updated (.NET DateTime ticks) → epoch_ns
//     unix_sec = (ticks - 621355968000000000) / 10000000
//
// 注意:
//   - 增量 key 映射 (去元音): pregame getodds 增量响应中 key 名压缩
//     (e.g. ookmakrs = bookmaker, valu = value, matchs = match)
//     adapter 层识别并标准化, 上层看不到 raw key 差异.
//   - 3-way soccer 1x2: 每个 outcome (Home/Draw/Away) 产生独立 OddsQuoteRecord.
//     fair value 计算在 strategy 层对三条记录联合 de-vig (ADR-008 §4).
// ============================================================================

// Goalserve .NET ticks (stored as string int64) → epoch_ns
// 公式: unix_sec = (ticks - 621355968000000000) / 10_000_000
[[nodiscard]] inline std::int64_t NetTicksToEpochNs(std::int64_t ticks) noexcept {
    constexpr std::int64_t kTicksEpochOffset = 621'355'968'000'000'000LL;
    constexpr std::int64_t kTicksPerSec = 10'000'000LL;
    constexpr std::int64_t kNsPerSec = 1'000'000'000LL;
    if (ticks <= kTicksEpochOffset)
        return 0;
    return ((ticks - kTicksEpochOffset) / kTicksPerSec) * kNsPerSec;
}

// inplay updated_ts (Unix ms) → epoch_ns
[[nodiscard]] inline std::int64_t InplayTsMsToNs(std::int64_t ts_ms) noexcept {
    return ts_ms * 1'000'000LL;
}

// pregame @ts (Unix sec) → epoch_ns
[[nodiscard]] inline std::int64_t PregameTsSecToNs(std::int64_t ts_sec) noexcept {
    return ts_sec * 1'000'000'000LL;
}

// GameEventType 从 Goalserve livescore event type string 映射
[[nodiscard]] constexpr GameEventType MapGoalserveEventType(std::string_view gs_type) noexcept {
    if (gs_type == "goal")
        return GameEventType::Goal;
    if (gs_type == "yellowcard")
        return GameEventType::YellowCard;
    if (gs_type == "yellowred")
        return GameEventType::YellowRed;
    if (gs_type == "redcard")
        return GameEventType::RedCard;
    if (gs_type == "subst")
        return GameEventType::Substitution;
    if (gs_type == "Shot On Target")
        return GameEventType::ShotOnTarget;
    if (gs_type == "Shot Off Target")
        return GameEventType::ShotOffTarget;
    if (gs_type == "Corner")
        return GameEventType::Corner;
    if (gs_type == "Offside")
        return GameEventType::Offside;
    if (gs_type == "Penalty")
        return GameEventType::Penalty;
    // VAR — 多个变体
    if (gs_type.find("VAR") != std::string_view::npos)
        return GameEventType::VAR;
    return GameEventType::Other;
}

// compile-time 闭合校验
static_assert(static_cast<std::uint8_t>(InfoKind::Odds) == 3U);
static_assert(static_cast<std::uint8_t>(VendorId::Goalserve) == 0U);
static_assert(static_cast<std::uint8_t>(GameEventType::Other) == 255U);

}  // namespace stcpp::data::adapter
