// stcpp/data/inplay_score_parser.hpp — Goalserve inplay-<sport>.gz score feed parser
//
// Owner: 小段 (goalserve-specialist, #37)
// Date:  2026-05-29
// Task:  小余接入方案 v1 §3 采集段 — 从 inplay.goalserve.com/inplay-<sport>.gz JSON
//        解析 GameScoreRecord (per-event 比分/状态/时钟), 4ts R-20 守法.
//
// 覆盖:
//   MVP sport: Soccer / Basketball / Tennis (inplay 已实证, 小余方案 §3.2)
//
// 时间戳 (R-20 强制):
//   data_source_ts_ns  = updated_ts (feed 顶层, Unix ms → ns)  PayloadScoresTs
//   event_ts_ns        = data_source_ts_ns (inplay 无独立事件时间戳)
//   ingestion_ts_ns    = caller 传入 (recv 完成时刻, CLOCK_MONOTONIC_RAW)
//   as_of_ts_ns        = 0, 由 ScoreSnapshotStore::get() 读取侧填入
//   禁止: 本函数内部调用 now() 替代 data_source_ts
//
// JSON 结构 (SSOT §3.2):
//   {
//     "bm": "bet365",
//     "updated_ts": <Unix ms>,
//     "events": {
//       "<inplay_match_id>": {
//         "info": {
//           "id", "mid", "bet365id", "league_id",
//           "period", "score", "minute", "seconds", "time_status"
//         },
//         "odds": { ... }   // 不解析, 归 OddsParser
//       }
//     }
//   }
//
// 设计约束 (R-12):
//   解析纯粹是 CPU-bound 计算, 无 IO, 可在采集线程调用.
//   返回 vector<GameScoreRecord>, 由 ScoreSnapshotStore::Publish() 消费.
//
// 依赖:
//   nlohmann/json (header-only, 已 FetchContent 在 debug_api CMake — W5 接入时
//                  本模块独立引入; 测试用 nlohmann_json FetchContent 或系统安装)
//   stcpp/data/goalserve_adapter.hpp (GameScoreRecord + FourTs helpers)
//   stcpp/data/goalserve_client.hpp  (GoalserveSport / TimeStatus)
//
// 不耻下问:
//   - sport-specific period/clock 语义 (倒计时 vs 累计) → 体育专家
//   - ingestion_ts MONOTONIC_RAW 采集时机 → 老周 (线程模型)

#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "stcpp/data/goalserve_adapter.hpp"
#include "stcpp/data/goalserve_client.hpp"

namespace stcpp::data::inplay {

// ============================================================================
// ParseResult — 一次 inplay-<sport>.gz 解析产物
// ============================================================================
struct ParseResult {
    // feed 整体 updated_ts (Unix ms, 来自 JSON 顶层)
    // 0 = payload 无此字段 (告警: R-20 data_source_ts fallback)
    std::int64_t updated_ts_ms = 0;

    // 解析成功的 GameScoreRecord 列表 (per-event)
    std::vector<adapter::GameScoreRecord> scores;

    // 解析失败的 event id 列表 (malformed / 缺字段)
    std::vector<std::string> parse_errors;

    // sport (传入 parser 时设置, 方便 caller 区分)
    goalserve::GoalserveSport sport = goalserve::GoalserveSport::Soccer;

    // inplay bet365 单源 de-vig 三边胜率 (home/away/draw), 与 scores 1:1 对齐 (index i ↔ scores[i])。
    //   双边/三边完整透传 (不丢信息); -1.0 = 该 event 无 odds (无 odds plan / market 缺)。
    //   orientation: 这是 Goalserve home/away 视角; caller (trading_loop) 按 yes_is_home 翻成
    //   Polymarket YES-canonical (home 视角 ≠ YES 视角, 绝不可直接当 YES 用 — 否则 away=YES 盘口镜像反)。
    std::vector<double> inplay_home_fairs;
    std::vector<double> inplay_away_fairs;
    std::vector<double> inplay_draw_fairs;  // binary 无平局市场 = 0

    // A-step-2 分局盘 sharp (2026-06-04 老板「第一局/第二局这样的盘」, 小田设计; G-FREEZE-W 只增):
    //   当前段 de-vig fair (MVP=tennis 当前盘 Set Winner)。与 scores 1:1 对齐。seg_index = 当前段序号
    //   (tennis: 当前盘 1-5; 0 = 不适用/非分段运动)。-1.0 = 当前段无 bet365 赔率 (fail-closed 不交易)。
    std::vector<double> inplay_seg_home_fairs;
    std::vector<double> inplay_seg_away_fairs;
    std::vector<int> inplay_seg_index;  // 当前段序号 (0=不适用)
};

// soccer 1X2 (Full Time) inplay market_id (SSOT xiaoduan-w8 §4.3: "1"=1X2全场)。
inline constexpr std::string_view kSoccerMarketId1x2Fulltime = "1";

// ============================================================================
// InplayScoreParser — stateless, thread-safe (纯函数包装器)
//
// 线程安全: 无成员状态, 可多线程并发调用不同 sport.
// ============================================================================
class InplayScoreParser {
public:
    // ------------------------------------------------------------------------
    // Parse — 主入口
    //
    // 参数:
    //   json_body      : inplay-<sport>.gz 解压后的 JSON 文本 (raw string)
    //   sport          : 告知 parser 当前 sport (影响 period/clock 解读)
    //   ingestion_ts_ns: caller 传入 recv 完成时刻 (CLOCK_MONOTONIC_RAW ns)
    //                    R-20: ingestion_ts >= data_source_ts
    //
    // 返回: ParseResult (scores + updated_ts_ms + parse_errors)
    //
    // 异常: 不抛出. JSON 解析错误记入 parse_errors, partial 结果仍返回.
    //
    // 真实 HTTP 接线 (W5):
    //   GoalserveClient::Fetch(InplayOdds, sport) → body → Decompress(gzip)
    //   → InplayScoreParser::Parse(body, sport, recv_ns)
    //   接线点在采集线程 (vCPU3 worker pool, 非 WSS event loop, R-12 合规).
    // ------------------------------------------------------------------------
    //   result_market_ids (可选, 2026-06-04 老板「用 goalserve 字典」): 该 sport 字典解析出的
    //     【全场赛果盘 market_id 集合】(SelectResultMarketId 优先按 id 命中, 免 name 漂移)。
    //     nullptr/空 → 纯启发式 (IsResultMarketName 按 feed market name 选)。feed 线程从
    //     dictionaries/odds-markets/{sport} 拉取 + 解析后传入; 纯函数无 IO 红线保持。
    [[nodiscard]] static ParseResult Parse(
        const std::string& json_body, goalserve::GoalserveSport sport, std::int64_t ingestion_ts_ns,
        const std::unordered_set<std::string>* result_market_ids = nullptr) noexcept;

    // ------------------------------------------------------------------------
    // ParseScore — 解析 info.score 字段 "home:away" → (home_total, away_total)
    // 格式: "0:1" / "21:17" / "6.3:2.1" (网球 set 累计, 取整数部分)
    // 返回 false 表示解析失败.
    // ------------------------------------------------------------------------
    [[nodiscard]] static bool ParseScore(const std::string& score_str, std::int32_t& home_total,
                                         std::int32_t& away_total) noexcept;

    // ------------------------------------------------------------------------
    // ParseTennisScore — 网球 info.score 是【逐盘局数】"2:6,6:4"(逗号分隔各盘 h:a), 非盘数!
    //   sets_h/a = 已赢盘数 (完成盘: 该盘 ≥6 局且胜)。games_h/a = 全场总局数 (各盘求和)。
    //   修旧 bug: ParseScore("2:6,6:4") 只取首盘 "2:6" → 误当比分喂 moneyline。
    //   返回 false = 空/无效 (保持入参不变)。
    // ------------------------------------------------------------------------
    //   periods_h/a (可选, 各容 12): 逐盘局数 (实时比分 set_summary 用); periods_used: 实际盘数。
    [[nodiscard]] static bool ParseTennisScore(const std::string& score_str, std::int32_t& sets_h,
                                               std::int32_t& sets_a, std::int32_t& games_h,
                                               std::int32_t& games_a, std::int32_t* periods_h = nullptr,
                                               std::int32_t* periods_a = nullptr,
                                               std::uint8_t* periods_used = nullptr) noexcept;

    // ------------------------------------------------------------------------
    // ParseTimeStatus — info.time_status string → goalserve::TimeStatus
    // Goalserve inplay 传的是数字字符串 "0"/"1"/"3"/..."99"
    // 未知值 → TimeStatus::ToBeFixed + 记录 parse_errors
    // ------------------------------------------------------------------------
    [[nodiscard]] static goalserve::TimeStatus ParseTimeStatus(const std::string& ts_str) noexcept;

    // ------------------------------------------------------------------------
    // MapStatus — goalserve::TimeStatus → EventScore.status 字符串
    // 投影规则见小余方案 §3.3
    // ------------------------------------------------------------------------
    [[nodiscard]] static std::string MapStatus(goalserve::TimeStatus ts,
                                               const std::string& period_str) noexcept;

    // ------------------------------------------------------------------------
    // ts_chain_ok — R-20 守法自检
    // data_source_ts 来自 Goalserve payload, 禁止为 0 (IngestionFallback)
    // ------------------------------------------------------------------------
    [[nodiscard]] static bool TsChainOk(const goalserve::FourTs& ts) noexcept;
};

}  // namespace stcpp::data::inplay
