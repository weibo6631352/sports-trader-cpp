// stcpp/data/inplay_score_parser.cpp — Goalserve inplay-<sport>.gz score parser 实现
//
// Owner: 小段 (goalserve-specialist, #37)
// Date:  2026-05-29
// Task:  小余接入方案 v1 §3 采集段
//
// 解析策略:
//   Goalserve inplay JSON 结构固定 (SSOT §3.2), 用轻量手写字段提取,
//   无需引入 nlohmann/json 或 simdjson (零外部依赖, 环境无 HTTP 时测试仍可跑).
//
//   提取算法: JSON key 扫描 — 找 "\"key\":" 后提取值 (string/number).
//   局限: 仅适用于 Goalserve inplay 这类结构扁平的 JSON.
//   W5 真接时如需全量解析 odds 字段, 可升级到 simdjson (老李 W10 backlog).
//
// R-20 时间戳守法:
//   data_source_ts_ns = updated_ts (ms) × 1e6  — PayloadScoresTs (优先)
//   event_ts_ns       = data_source_ts_ns       — inplay 无独立事件时钟
//   ingestion_ts_ns   = caller 传入
//   as_of_ts_ns       = 0 (由 ScoreSnapshotStore::Get() 读取侧填入)
//   禁止: 内部调用 now() 替代 data_source_ts
//
// sport 差异 (MVP: Soccer / Basketball / Tennis, SSOT §3.5):
//   Soccer:     period = "1st Half"/"2nd Half"/"Half Time"/"Extra Time"
//               minute/seconds = 经过时间 (累计)
//   Basketball: period = "1st Quarter"/"2nd Quarter"/"3rd Quarter"/"4th Quarter"/"OT"
//               minute/seconds = 节内剩余时间 (倒计时, 体育专家待确认)
//   Tennis:     period = "Set 1"/"Set 2"/"Set 3" 等
//               minute/seconds 无意义 (网球无时钟)
//
// 真实 HTTP 接线 (W5 TODO):
//   调用方 (vCPU3 worker): GoalserveClient::Fetch(UrlSpec{.endpoint=InplayOdds,
//                           .sport=sport, .host=Inplay}) → gzip decompress
//                           → InplayScoreParser::Parse(body, sport, recv_ns)
//   接线点: src/stcpp/data/inplay_feed_thread.cpp (W5 小冯/小段)

#include "stcpp/data/inplay_score_parser.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace stcpp::data::inplay {

namespace {

// ============================================================================
// §0 极简 JSON 字段提取工具 (针对 Goalserve inplay 固定结构)
//
// Goalserve inplay JSON:
//   {
//     "bm": "bet365",
//     "updated_ts": 1716988800123,
//     "events": {
//       "134181543": {
//         "info": { "id":"..", "period":"1st Half", "score":"0:1",
//                   "minute":"28", "seconds":"0", "time_status":"1",
//                   "league_id":"6374", "state":"11007" },
//         "odds": { ... }
//       }
//     }
//   }
// ============================================================================

// 从 JSON 文本中找 "key": 后的字符串值 (去引号)
// 返回 true = 找到. result 填入找到的字符串值.
// 只查第一次出现 (从 search_start 位置开始).
[[nodiscard]] bool ExtractStringValue(std::string_view json, std::string_view key, std::string& result,
                                      std::size_t search_start = 0) noexcept {
    // 构造 "key": 模式
    std::string pattern;
    pattern.reserve(key.size() + 4);
    pattern += '"';
    pattern += key;
    pattern += '"';
    pattern += ':';

    auto pos = json.find(pattern, search_start);
    if (pos == std::string_view::npos)
        return false;
    pos += pattern.size();

    // 跳过空白
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])))
        ++pos;

    if (pos >= json.size())
        return false;

    if (json[pos] == '"') {
        // 字符串值
        ++pos;
        std::size_t end = pos;
        while (end < json.size() && json[end] != '"') {
            if (json[end] == '\\')
                ++end;  // 跳过转义
            ++end;
        }
        result.assign(json.data() + pos, end - pos);
        return true;
    }
    // 非字符串 (数字 / null / bool) — 不应出现在需要 string 的字段
    return false;
}

// 从 JSON 文本中找 "key": 后的数字值 (整数)
[[nodiscard]] bool ExtractInt64Value(std::string_view json, std::string_view key, std::int64_t& result,
                                     std::size_t search_start = 0) noexcept {
    std::string pattern;
    pattern.reserve(key.size() + 4);
    pattern += '"';
    pattern += key;
    pattern += '"';
    pattern += ':';

    auto pos = json.find(pattern, search_start);
    if (pos == std::string_view::npos)
        return false;
    pos += pattern.size();

    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])))
        ++pos;

    if (pos >= json.size())
        return false;

    const char* begin = json.data() + pos;
    const char* end = json.data() + json.size();
    auto [ptr, ec] = std::from_chars(begin, end, result);
    return ec == std::errc{};
}

// 从 JSON 文本中找 "key": 后的 int32 值
[[nodiscard]] bool ExtractInt32Value(std::string_view json, std::string_view key, std::int32_t& result,
                                     std::size_t search_start = 0) noexcept {
    std::int64_t v = 0;
    if (!ExtractInt64Value(json, key, v, search_start))
        return false;
    result = static_cast<std::int32_t>(v);
    return true;
}

// ============================================================================
// §1 解析单个 event 的 info 块
//
// 从 event_block (从 event id 起始的 JSON 片段) 提取 info 字段.
// event_block 示例:
//   "134181543": { "info": { "id":"...", ... }, "odds": {...} }
// ============================================================================
[[nodiscard]] bool ParseEventInfo(std::string_view event_block, goalserve::GoalserveSport sport,
                                  std::int64_t data_source_ts_ns, std::int64_t ingestion_ts_ns,
                                  adapter::GameScoreRecord& rec, std::string& error_out) noexcept {
    // 找 "info": { ... } 块
    const auto info_key_pos = event_block.find("\"info\":");
    if (info_key_pos == std::string_view::npos) {
        error_out = "missing info key";
        return false;
    }
    // 找 info 块的 { 开始
    const auto brace_pos = event_block.find('{', info_key_pos);
    if (brace_pos == std::string_view::npos) {
        error_out = "missing info brace";
        return false;
    }

    // 找 info 块的 } 结束 (简单计数器, 处理嵌套)
    int depth = 0;
    std::size_t info_end = brace_pos;
    for (std::size_t i = brace_pos; i < event_block.size(); ++i) {
        if (event_block[i] == '{')
            ++depth;
        else if (event_block[i] == '}') {
            --depth;
            if (depth == 0) {
                info_end = i;
                break;
            }
        }
    }
    const auto info_block = event_block.substr(brace_pos, info_end - brace_pos + 1);
    const std::size_t base = 0;

    // --- 必须字段: id (inplay match id) ---
    std::string id_str;
    if (!ExtractStringValue(info_block, "id", id_str, base)) {
        error_out = "missing info.id";
        return false;
    }
    rec.match_id.inplay_match_id = id_str;
    rec.match_id.vendor_match_id = id_str;  // inplay_match_id 作为主键

    // --- league_id ---
    std::string league_id_str;
    if (ExtractStringValue(info_block, "league_id", league_id_str, base)) {
        rec.match_id.league_id = league_id_str;
    }

    // --- time_status ---
    std::string ts_str;
    if (!ExtractStringValue(info_block, "time_status", ts_str, base)) {
        // time_status 也可能是数字
        std::int32_t ts_int = 0;
        if (ExtractInt32Value(info_block, "time_status", ts_int, base)) {
            ts_str = std::to_string(ts_int);
        } else {
            ts_str = "0";  // 默认 NotStarted
        }
    }
    rec.status = InplayScoreParser::ParseTimeStatus(ts_str);

    // --- score: "0:1" → home_score_total, away_score_total ---
    std::string score_str;
    if (ExtractStringValue(info_block, "score", score_str, base)) {
        // 忽略返回值 (parse 失败时保持 0:0 默认值)
        [[maybe_unused]] bool ok =
            InplayScoreParser::ParseScore(score_str, rec.home_score_total, rec.away_score_total);
    }

    // --- period ---
    std::string period_str;
    if (ExtractStringValue(info_block, "period", period_str, base)) {
        rec.period = period_str;
    }

    // --- state (Goalserve 5位状态码) ---
    std::string state_str;
    if (ExtractStringValue(info_block, "state", state_str, base)) {
        rec.gs_state_code = state_str;
    }

    // --- minute / seconds (时钟) ---
    std::string minute_str;
    std::string seconds_str;
    if (ExtractStringValue(info_block, "minute", minute_str, base)) {
        // minute 有时是 "28:31" (分:秒) 形式 (SSOT §3.2 示例: "@minute": "86:31")
        // 也有时是纯数字 "28"
        const auto colon_pos = minute_str.find(':');
        if (colon_pos != std::string::npos) {
            // "86:31" 格式: 取冒号前为分钟
            const auto min_part = minute_str.substr(0, colon_pos);
            const auto sec_part = minute_str.substr(colon_pos + 1);
            std::int32_t min_val = 0, sec_val = 0;
            std::from_chars(min_part.data(), min_part.data() + min_part.size(), min_val);
            std::from_chars(sec_part.data(), sec_part.data() + sec_part.size(), sec_val);
            rec.elapsed_min = min_val;
            rec.elapsed_sec = sec_val;
        } else {
            std::int32_t min_val = 0;
            if (std::from_chars(minute_str.data(), minute_str.data() + minute_str.size(), min_val).ec ==
                std::errc{}) {
                rec.elapsed_min = min_val;
            }
        }
    }

    // seconds 单独字段 (inplay.goalserve.com info.seconds)
    if (ExtractStringValue(info_block, "seconds", seconds_str, base)) {
        std::int32_t sec_val = 0;
        if (std::from_chars(seconds_str.data(), seconds_str.data() + seconds_str.size(), sec_val).ec ==
            std::errc{}) {
            // 如果 minute 字段已含秒 (86:31 格式), 不再从 seconds 字段覆盖
            if (!rec.elapsed_sec.has_value()) {
                rec.elapsed_sec = sec_val;
            }
        }
    }

    // --- 4ts 填充 (R-20) ---
    rec.ts.data_source_ts_ns = data_source_ts_ns;  // Goalserve updated_ts → ns
    rec.ts.event_ts_ns = data_source_ts_ns;        // inplay 无独立事件时刻, = data_source
    rec.ts.ingestion_ts_ns = ingestion_ts_ns;
    rec.ts.as_of_ts_ns = ingestion_ts_ns;  // 读取时 ScoreSnapshotStore::Get 可覆盖
    rec.ts.ds_origin = goalserve::DataSourceTsOrigin::PayloadScoresTs;

    // sport
    rec.match_id.vendor = adapter::VendorId::Goalserve;

    // home_team / away_team: inplay info 无队名字段 (队名来自 inplay-mapping/pregame)
    // 填空串, 由映射物化 ETL (小余) 覆盖
    rec.home_team = "";
    rec.away_team = "";

    (void)sport;  // sport 保留给未来 period 语义分支扩展

    return true;
}

// ============================================================================
// §2 从顶层 JSON 提取 events 对象块
//
// 找 "events": { ... } 的外层花括号范围.
// ============================================================================
[[nodiscard]] std::string_view ExtractEventsBlock(std::string_view json) noexcept {
    const auto key_pos = json.find("\"events\":");
    if (key_pos == std::string_view::npos)
        return {};

    const auto brace_pos = json.find('{', key_pos);
    if (brace_pos == std::string_view::npos)
        return {};

    int depth = 0;
    for (std::size_t i = brace_pos; i < json.size(); ++i) {
        if (json[i] == '{')
            ++depth;
        else if (json[i] == '}') {
            --depth;
            if (depth == 0) {
                return json.substr(brace_pos, i - brace_pos + 1);
            }
        }
    }
    return {};  // 未闭合
}

// ============================================================================
// §3 枚举 events 块中的所有 event id (key)
//
// events 块: { "134181543": { ... }, "134181544": { ... } }
// 提取每个 key 和对应的 value 块起始位置.
// ============================================================================
// 返回: vector of (event_id_str, value_block_start_in_events_block)
struct EventEntry {
    std::string id;
    std::size_t block_start;  // 在 events_block 中 value 块 { 的位置
    std::size_t block_end;    // 在 events_block 中 value 块 } 的位置 (inclusive)
};

[[nodiscard]] std::vector<EventEntry> EnumerateEvents(std::string_view events_block) noexcept {
    std::vector<EventEntry> result;

    // 跳过最外层 {
    std::size_t pos = 1;
    while (pos < events_block.size()) {
        // 找下一个 "  (event id 键)
        while (pos < events_block.size() && events_block[pos] != '"')
            ++pos;
        if (pos >= events_block.size())
            break;

        // 读 event id (到下一个 ")
        ++pos;
        const std::size_t id_start = pos;
        while (pos < events_block.size() && events_block[pos] != '"')
            ++pos;
        if (pos >= events_block.size())
            break;
        std::string event_id(events_block.data() + id_start, pos - id_start);
        ++pos;  // 跳过结束 "

        // 跳过 ": 到 value 的 {
        while (pos < events_block.size() && events_block[pos] != '{')
            ++pos;
        if (pos >= events_block.size())
            break;

        // 找对应 value 块的结束 }
        const std::size_t blk_start = pos;
        int depth = 0;
        std::size_t blk_end = pos;
        for (std::size_t i = pos; i < events_block.size(); ++i) {
            if (events_block[i] == '{')
                ++depth;
            else if (events_block[i] == '}') {
                --depth;
                if (depth == 0) {
                    blk_end = i;
                    break;
                }
            }
        }

        result.push_back({std::move(event_id), blk_start, blk_end});
        pos = blk_end + 1;

        // 跳过 , 或 }
        while (pos < events_block.size() &&
               (events_block[pos] == ',' || std::isspace(static_cast<unsigned char>(events_block[pos]))))
            ++pos;

        // 如果到了最外层 }, 结束
        if (pos < events_block.size() && events_block[pos] == '}')
            break;
    }

    return result;
}

}  // anonymous namespace

// ============================================================================
// InplayScoreParser::Parse — 主入口实现
// ============================================================================
ParseResult InplayScoreParser::Parse(const std::string& json_body, goalserve::GoalserveSport sport,
                                     std::int64_t ingestion_ts_ns) noexcept {
    ParseResult result;
    result.sport = sport;

    const std::string_view json(json_body);

    // --- 提取 updated_ts (R-20 data_source_ts) ---
    std::int64_t updated_ts_ms = 0;
    if (!ExtractInt64Value(json, "updated_ts", updated_ts_ms)) {
        // R-20: payload 无 ts → IngestionFallback (告警, 仍继续解析)
        result.parse_errors.push_back("missing updated_ts: R-20 IngestionFallback");
        // data_source_ts_ns = ingestion_ts_ns (fallback, 违反 R-20 但不崩溃)
        updated_ts_ms = ingestion_ts_ns / 1'000'000LL;
    }
    result.updated_ts_ms = updated_ts_ms;

    const std::int64_t data_source_ts_ns = adapter::InplayTsMsToNs(updated_ts_ms);

    // R-20 守法检查: data_source_ts 不应超过 ingestion_ts
    // (如果超过说明时钟差异或 Goalserve ts 有误, 记录但不拒绝)
    if (data_source_ts_ns > ingestion_ts_ns && ingestion_ts_ns > 0) {
        result.parse_errors.push_back("R-20 warn: data_source_ts_ns > ingestion_ts_ns (clock skew?)");
    }

    // --- 提取 events 块 ---
    const auto events_block = ExtractEventsBlock(json);
    if (events_block.empty()) {
        result.parse_errors.push_back("missing or empty events block");
        return result;
    }

    // --- 枚举并解析每个 event ---
    const auto entries = EnumerateEvents(events_block);
    result.scores.reserve(entries.size());

    for (const auto& entry : entries) {
        const auto event_block =
            events_block.substr(entry.block_start, entry.block_end - entry.block_start + 1);

        adapter::GameScoreRecord rec;
        std::string err;
        if (!ParseEventInfo(event_block, sport, data_source_ts_ns, ingestion_ts_ns, rec, err)) {
            result.parse_errors.push_back("event " + entry.id + ": " + err);
            continue;
        }

        // 确保 inplay_match_id = event key (顶层 key 比 info.id 更可靠)
        if (rec.match_id.inplay_match_id.empty()) {
            rec.match_id.inplay_match_id = entry.id;
            rec.match_id.vendor_match_id = entry.id;
        }

        // R-20 守法自检
        if (!TsChainOk(rec.ts)) {
            result.parse_errors.push_back("event " + entry.id + ": R-20 ts chain fail (data_source_ts=0?)");
            // 仍加入 scores, 但调用方应 alert
        }

        result.scores.push_back(std::move(rec));
    }

    return result;
}

// ============================================================================
// ParseScore — "home:away" → int32, int32
// ============================================================================
bool InplayScoreParser::ParseScore(const std::string& score_str, std::int32_t& home_total,
                                   std::int32_t& away_total) noexcept {
    const auto colon = score_str.find(':');
    if (colon == std::string::npos)
        return false;

    const auto home_sv = std::string_view(score_str).substr(0, colon);
    const auto away_sv = std::string_view(score_str).substr(colon + 1);

    // 网球可能是 "6.3:2.1" — 取整数部分
    const auto home_dot = home_sv.find('.');
    const auto away_dot = away_sv.find('.');
    const auto home_int = (home_dot != std::string_view::npos) ? home_sv.substr(0, home_dot) : home_sv;
    const auto away_int = (away_dot != std::string_view::npos) ? away_sv.substr(0, away_dot) : away_sv;

    auto [hp, hec] = std::from_chars(home_int.data(), home_int.data() + home_int.size(), home_total);
    auto [ap, aec] = std::from_chars(away_int.data(), away_int.data() + away_int.size(), away_total);

    return hec == std::errc{} && aec == std::errc{};
}

// ============================================================================
// ParseTimeStatus — "0".."9"/"99" → TimeStatus
// ============================================================================
goalserve::TimeStatus InplayScoreParser::ParseTimeStatus(const std::string& ts_str) noexcept {
    int val = 0;
    const auto [ptr, ec] = std::from_chars(ts_str.data(), ts_str.data() + ts_str.size(), val);
    if (ec != std::errc{})
        return goalserve::TimeStatus::ToBeFixed;

    switch (val) {
        case 0:
            return goalserve::TimeStatus::NotStarted;
        case 1:
            return goalserve::TimeStatus::InPlay;
        case 2:
            return goalserve::TimeStatus::ToBeFixed;
        case 3:
            return goalserve::TimeStatus::Ended;
        case 4:
            return goalserve::TimeStatus::Postponed;
        case 5:
            return goalserve::TimeStatus::Cancelled;
        case 6:
            return goalserve::TimeStatus::Walkover;
        case 7:
            return goalserve::TimeStatus::Interrupted;
        case 8:
            return goalserve::TimeStatus::Abandoned;
        case 9:
            return goalserve::TimeStatus::Retired;
        case 99:
            return goalserve::TimeStatus::Removed;
        default:
            return goalserve::TimeStatus::ToBeFixed;
    }
}

// ============================================================================
// MapStatus — TimeStatus + period → EventScore.status 字符串 (小余方案 §3.3)
// ============================================================================
std::string InplayScoreParser::MapStatus(goalserve::TimeStatus ts, const std::string& period_str) noexcept {
    switch (ts) {
        case goalserve::TimeStatus::InPlay: {
            // 半场休息: period 含 "HT" / "Half" / "Half Time"
            const std::string_view p(period_str);
            if (p == "HT" || p.find("Half Time") != std::string_view::npos ||
                p.find("halftime") != std::string_view::npos || p == "Half") {
                return "halftime";
            }
            return "inplay";
        }
        case goalserve::TimeStatus::Ended:
            return "final";
        case goalserve::TimeStatus::NotStarted:
            return "pregame";
        // 异常态: MVP 先归 pregame (小余方案 §3.3, 产品待定)
        case goalserve::TimeStatus::Postponed:
        case goalserve::TimeStatus::Cancelled:
        case goalserve::TimeStatus::Walkover:
        case goalserve::TimeStatus::Interrupted:
        case goalserve::TimeStatus::Abandoned:
        case goalserve::TimeStatus::Retired:
        case goalserve::TimeStatus::Removed:
        case goalserve::TimeStatus::ToBeFixed:
        default:
            return "pregame";
    }
}

// ============================================================================
// TsChainOk — R-20 守法自检
// ============================================================================
bool InplayScoreParser::TsChainOk(const goalserve::FourTs& ts) noexcept {
    // data_source_ts 必须来自 Goalserve payload (非 IngestionFallback)
    if (ts.ds_origin == goalserve::DataSourceTsOrigin::IngestionFallback)
        return false;
    // 4ts 单调
    return ts.IsMonotonic();
}

}  // namespace stcpp::data::inplay
