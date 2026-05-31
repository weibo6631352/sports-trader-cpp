// stcpp/data/inplay_score_parser.cpp — Goalserve inplay-<sport>.gz score parser 实现
//
// Owner: 小段 (goalserve-specialist, #37)
// Date:  2026-05-29
// Last-updated: 2026-05-30 (security harden: max_depth/escape-guard/log-injection, 小白审计 §1.3-B/C)
// Task:  小余接入方案 v1 §3 采集段 + W5 真实 HTTP 接线完成
//
// 解析策略:
//   Goalserve inplay JSON 结构固定 (SSOT §3.2), 用轻量手写字段提取,
//   无需引入 nlohmann/json 或 simdjson (零外部依赖, 环境无 HTTP 时测试仍可跑).
//
//   提取算法: JSON key 扫描 — 找 "\"key\":" 后提取值 (string/number).
//   局限: 仅适用于 Goalserve inplay 这类结构扁平的 JSON.
//
// R-20 时间戳守法:
//   data_source_ts_ns = updated_ts (ms) × 1e6  — PayloadScoresTs (优先)
//   event_ts_ns       = start_ts (Unix sec) × 1e9 — 比赛排定开始 (真实 feed 字段)
//                       start_ts 为空串时回落 data_source_ts
//   ingestion_ts_ns   = caller 传入
//   as_of_ts_ns       = ingestion_ts_ns (读取时由 store 覆盖)
//   禁止: 内部调用 now() 替代 data_source_ts
//
// 真实 feed 字段修正 (2026-05-29 实测 inplay.goalserve.com):
//   1. time_status 字段不存在 — 真实 inplay feed 无此字段.
//      替代: 所有出现在 feed 中的事件即为 InPlay 状态 (feed 只含进行中比赛).
//      state 字段 (5位码, e.g. "21000") 存入 gs_state_code 供上层使用.
//   2. name 字段 = "HomeTeam vs AwayTeam" — 解析队名.
//   3. start_ts 字段 = Unix 秒时间戳 — 用作 event_ts.
//   4. seconds 字段格式 = "分:秒" (e.g. "89:21") — 非纯秒.
//
// sport 差异 (MVP: Soccer / Basketball / Tennis):
//   Soccer:     period = "1st Half"/"2nd Half"/"Half Time"/"Extra Time"
//   Basketball: period = "1st Quarter"/.."4th Quarter"/"OT"
//   Tennis:     period = "Set N"
//
// 真实 HTTP 接线 (W5 完成):
//   inplay_feed_thread.cpp: HTTP GET + gzip decompress
//   → InplayScoreParser::Parse(body, sport, recv_ns) → ScoreSnapshotStore::Publish

#include "stcpp/data/inplay_score_parser.hpp"

#include "stcpp/data/inplay_odds_parser.hpp"  // ParseInplayOddsDevig (inplay bet365 单源 de-vig)

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace stcpp::data::inplay {

namespace {

// ============================================================================
// §-1 安全常量 + 安全工具 (小白审计 §1.3-B/C, 2026-05-30)
//
// kMaxJsonDepth: 花括号深度上限 (ExtractEventsBlock / ParseEventInfo / EnumerateEvents)
//   防攻击者发 `{{{{×N` 深嵌套触发 CPU 耗尽.
//   真实 inplay JSON 深度约 3-5 层; 32 是极端安全上限.
//
// SanitizeId — 外部 id 字符串清洗:
//   截断到 64 字节, 仅保留 [A-Za-z0-9_\-] (白名单), 其余替换为 '?'.
//   用于: 拼入 parse_errors 日志串, 防 log injection (CRLF / 控制字符 / 格式符).
// ============================================================================
static constexpr int kMaxJsonDepth = 32;

[[nodiscard]] std::string SanitizeId(const std::string& raw) noexcept {
    constexpr std::size_t kMaxIdLen = 64;
    const std::size_t take = std::min(raw.size(), kMaxIdLen);
    std::string out;
    out.reserve(take);
    for (std::size_t i = 0; i < take; ++i) {
        const char c = raw[i];
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                        c == '_' || c == '-';
        out.push_back(ok ? c : '?');
    }
    return out;
}

// ============================================================================
// §0 极简 JSON 字段提取工具 (针对 Goalserve inplay 固定结构)
//
// 真实 Goalserve inplay JSON (2026-05-29 实测):
//   {
//     "bm": "bet365",
//     "updated_ts": 1780066172550,
//     "events": {
//       "134261101": {
//         "info": { "id":"134261101","mid":"..","bet365id":"..","name":"HomeTeam vs AwayTeam",
//                   "sport":"soccer","league_id":"18235","league":"Brazil ...",
//                   "start_time":"13:00","start_date":"29.05.2026",
//                   "start_ts":"1780059600","start_ts_utc":"1780059600",
//                   "period":"2nd Half","score":"2:1","state":"21000",
//                   "minute":"89","seconds":"89:21" },
//                   注: time_status 字段不存在于真实 feed!
//         "odds": { ... }
//       }
//     }
//   }
// ============================================================================

// 从 JSON 文本中找 "key": 后的字符串值 (去引号)
// 返回 true = 找到. result 填入找到的字符串值.
// 只查第一次出现 (从 search_start 位置开始).
//
// 安全加固 (小白审计 §1.3-C, 2026-05-30):
//   转义边界 bug 修: 原 `if (json[end]=='\\') ++end;` 在字符串末尾恰好是 `\`
//   时 ++end 越界一位, 导致吃掉闭合引号 → key/value 错位.
//   修复: 显式 `if (end + 1 < json.size())` 守护后再跳过转义字符, 否则中止.
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
            if (json[end] == '\\') {
                // 转义边界守护: 必须确认后一位存在且不是字符串边界
                if (end + 1 < json.size()) {
                    ++end;  // 跳过转义字符 (e.g. \", \\, \n)
                } else {
                    // 末尾孤立 \: 截断, 不越界
                    break;
                }
            }
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

// ExtractInt32Value — 从 JSON 文本中找 "key": 后的 int32 值
// 注: 当前 parser 通过 string 路径提取整数字段 (from_chars 内联), 此函数保留备用.
// NOLINTNEXTLINE(misc-unused-parameters)
[[maybe_unused]] [[nodiscard]] static bool ExtractInt32Value(std::string_view json, std::string_view key,
                                                             std::int32_t& result,
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
    // 安全加固: max_depth 防深嵌套 CPU 耗尽 (小白审计 §1.3-C)
    int depth = 0;
    std::size_t info_end = brace_pos;
    for (std::size_t i = brace_pos; i < event_block.size(); ++i) {
        if (event_block[i] == '{') {
            ++depth;
            if (depth > kMaxJsonDepth) {
                error_out = "info block depth exceeded max_depth=" + std::to_string(kMaxJsonDepth);
                return false;
            }
        } else if (event_block[i] == '}') {
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

    // --- name: "HomeTeam vs AwayTeam" → home_team / away_team ---
    // 真实 feed (2026-05-29 实测): info.name 含队名, 格式 "Home vs Away"
    std::string name_str;
    if (ExtractStringValue(info_block, "name", name_str, base)) {
        const auto vs_pos = name_str.find(" vs ");
        if (vs_pos != std::string::npos) {
            rec.home_team = name_str.substr(0, vs_pos);
            rec.away_team = name_str.substr(vs_pos + 4);
        }
    }

    // --- time_status (真实 feed 2026-05-29: 此字段不存在!) ---
    // inplay feed 本质上只含进行中的比赛, 因此所有 event 均为 InPlay.
    // state 字段 (5位码) 可进一步区分 clock-running vs clock-stopped.
    // time_status 为 legacy 字段, 新版 feed 已移除; 此处兼容检测.
    std::string ts_str;
    if (ExtractStringValue(info_block, "time_status", ts_str, base)) {
        // 老格式 feed 有此字段 — 按原有逻辑解析
        rec.status = InplayScoreParser::ParseTimeStatus(ts_str);
    } else {
        // 真实 feed: 无 time_status — inplay 事件默认 InPlay
        // (state 字段含 5位状态码, 存入 gs_state_code 供上层扩展)
        rec.status = goalserve::TimeStatus::InPlay;
    }

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
    // 真实 feed (2026-05-29): "seconds":"89:21" — 格式与 minute 字段相同 (分:秒)
    // 只在 minute 字段未含秒时才从 seconds 字段提取
    if (!rec.elapsed_sec.has_value() && ExtractStringValue(info_block, "seconds", seconds_str, base)) {
        const auto sec_colon = seconds_str.find(':');
        if (sec_colon != std::string::npos) {
            // "89:21" 格式: 取冒号后的秒部分
            const auto sec_part = seconds_str.substr(sec_colon + 1);
            std::int32_t sec_val = 0;
            if (std::from_chars(sec_part.data(), sec_part.data() + sec_part.size(), sec_val).ec ==
                std::errc{}) {
                rec.elapsed_sec = sec_val;
            }
        } else {
            // 纯数字格式 (兼容旧格式)
            std::int32_t sec_val = 0;
            if (std::from_chars(seconds_str.data(), seconds_str.data() + seconds_str.size(), sec_val).ec ==
                std::errc{}) {
                rec.elapsed_sec = sec_val;
            }
        }
    }

    // --- start_ts: Unix 秒 → event_ts_ns (R-20: 比赛排定开始时刻) ---
    // 真实 feed 字段: "start_ts":"1780059600" (Unix sec string)
    // 比 data_source_ts 更精确地表示赛事时间戳 (R-20: event_ts 语义)
    std::int64_t event_ts_ns = data_source_ts_ns;  // fallback
    std::string start_ts_str;
    if (ExtractStringValue(info_block, "start_ts", start_ts_str, base) && !start_ts_str.empty()) {
        std::int64_t start_ts_sec = 0;
        const auto [ptr, ec] =
            std::from_chars(start_ts_str.data(), start_ts_str.data() + start_ts_str.size(), start_ts_sec);
        if (ec == std::errc{} && start_ts_sec > 0) {
            event_ts_ns = start_ts_sec * 1'000'000'000LL;
        }
    }

    // --- 4ts 填充 (R-20) ---
    // event_ts  = start_ts (比赛排定开始时刻, 来自 payload)
    // R-20 chain: event_ts ≤ data_source_ts ≤ ingestion_ts
    // 如果 start_ts > data_source_ts (赛前 feed 进来), 则 event_ts = data_source_ts (保守)
    if (event_ts_ns > data_source_ts_ns) {
        event_ts_ns = data_source_ts_ns;
    }
    rec.ts.event_ts_ns = event_ts_ns;
    rec.ts.data_source_ts_ns = data_source_ts_ns;  // Goalserve updated_ts → ns
    rec.ts.ingestion_ts_ns = ingestion_ts_ns;
    rec.ts.as_of_ts_ns = ingestion_ts_ns;  // 读取时 ScoreSnapshotStore::Get 可覆盖
    rec.ts.ds_origin = goalserve::DataSourceTsOrigin::PayloadScoresTs;

    // sport
    rec.match_id.vendor = adapter::VendorId::Goalserve;

    // home_team / away_team: 来自 info.name "HomeTeam vs AwayTeam" (上面已解析)
    // 如果 name 字段缺失或格式不对, 留空串 (由 mapping ETL 小余覆盖)

    (void)sport;  // sport 保留给未来 period 语义分支扩展

    return true;
}

// ============================================================================
// §2 从顶层 JSON 提取 events 对象块
//
// 找 "events": { ... } 的外层花括号范围.
//
// 安全加固 (小白审计 §1.3-C, 2026-05-30):
//   max_depth 防护: 攻击者发 `{{{{×10万` 深嵌套触发 CPU 耗尽.
//   上限 kMaxJsonDepth=32, 超限中止返回空串.
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
        if (json[i] == '{') {
            ++depth;
            if (depth > kMaxJsonDepth) {
                std::fprintf(stderr,
                             "[inplay_parser] ExtractEventsBlock: depth exceeded %d (json_sz=%zu), abort\n",
                             kMaxJsonDepth, json.size());
                return {};  // 深度超限, 拒绝
            }
        } else if (json[i] == '}') {
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
        // 安全加固: max_depth 防深嵌套 CPU 耗尽 (小白审计 §1.3-C)
        const std::size_t blk_start = pos;
        int depth = 0;
        std::size_t blk_end = pos;
        bool depth_exceeded = false;
        for (std::size_t i = pos; i < events_block.size(); ++i) {
            if (events_block[i] == '{') {
                ++depth;
                if (depth > kMaxJsonDepth) {
                    depth_exceeded = true;
                    break;
                }
            } else if (events_block[i] == '}') {
                --depth;
                if (depth == 0) {
                    blk_end = i;
                    break;
                }
            }
        }
        if (depth_exceeded) {
            std::fprintf(stderr, "[inplay_parser] EnumerateEvents: event '%s' depth exceeded %d, skip\n",
                         event_id.c_str(), kMaxJsonDepth);
            break;  // 整个 events 块异常, 停止枚举
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
            // log injection 防护: entry.id 来自外部 feed, 截断+白名单过滤 (小白审计 §1.3-C)
            result.parse_errors.push_back("event " + SanitizeId(entry.id) + ": " + err);
            continue;
        }

        // 确保 inplay_match_id = event key (顶层 key 比 info.id 更可靠)
        if (rec.match_id.inplay_match_id.empty()) {
            rec.match_id.inplay_match_id = entry.id;
            rec.match_id.vendor_match_id = entry.id;
        }

        // R-20 守法自检
        if (!TsChainOk(rec.ts)) {
            result.parse_errors.push_back("event " + SanitizeId(entry.id) +
                                          ": R-20 ts chain fail (data_source_ts=0?)");
            // 仍加入 scores, 但调用方应 alert
        }

        // inplay bet365 odds → 单源 de-vig 三边 fair (soccer 1X2 全场 market_id="1")。
        //   双边/三边完整透传 (home/away/draw), 不丢信息 — orientation 在 paper_loop 按
        //   yes_is_home 翻成 YES-canonical (Goalserve home 视角 ≠ Polymarket YES 视角, 不可混淆)。
        //   从同一 event_block 切 odds 节点 (与 info 共享 updated_ts, 不引入新 ts, R-20 守法)。
        //   无 odds plan / market 缺 → -1.0 (sentinel, 与 game_row 默认对齐)。
        double home_fair = -1.0, away_fair = -1.0, draw_fair = -1.0;
        if (sport == goalserve::GoalserveSport::Soccer) {
            const auto odds_key = event_block.find("\"odds\":");
            if (odds_key != std::string_view::npos) {
                const auto ob = event_block.find('{', odds_key);
                if (ob != std::string_view::npos) {
                    int d = 0;
                    std::size_t oe = ob;
                    for (std::size_t i = ob; i < event_block.size(); ++i) {
                        if (event_block[i] == '{') {
                            if (++d > kMaxJsonDepth) {
                                d = -1;
                                break;
                            }
                        } else if (event_block[i] == '}') {
                            if (--d == 0) {
                                oe = i;
                                break;
                            }
                        }
                    }
                    if (d == 0) {
                        const auto devig = ParseInplayOddsDevig(
                            event_block.substr(ob, oe - ob + 1), kSoccerMarketId1x2Fulltime);
                        if (devig.valid) {
                            home_fair = devig.home_fair;
                            away_fair = devig.away_fair;
                            draw_fair = devig.draw_fair;  // 无平局市场 = 0 (binary)
                        }
                    }
                }
            }
        }

        result.scores.push_back(std::move(rec));
        result.inplay_home_fairs.push_back(home_fair);  // 1:1 对齐 scores
        result.inplay_away_fairs.push_back(away_fair);
        result.inplay_draw_fairs.push_back(draw_fair);
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
