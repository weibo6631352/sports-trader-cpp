// include/stcpp/data/settlement_record.hpp — 收盘/结算记录 + clob /markets JSON 解析
//
// Owner: 老雷 (GM) — 成果方案 v1 M2 (小余 ETL build-ready 设计落地)
// last_review: 2026-05-31
//
// 用途 (CLV 尺子 + 3b 权威结算的数据源):
//   轮询 clob /markets/{condition_id} → SettlementRecord。检测收盘 (accepting_orders true→false)
//   + 权威结算 (closed=true + tokens[].winner)。喂 paper_loop SetResolutionByCondition (3b 已接)
//   + 给 CLV close_fair 参考价。
//
// 红线:
//   R-20: data_source_ts 用 clob 自带 accepting_order_timestamp (禁本地 now() 替代上游)。
//   vendor-agnostic: 只含语义字段, 不泄 Polymarket 原始 JSON 结构进决策。
//
// 本头: 数据结构 + ParseMarketJson 纯函数 (无 IO/无线程, 可单测)。poller 线程 + daemon 装配下一步。
#pragma once

#include <array>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>

namespace stcpp::data {

struct SettlementRecord {
    std::string condition_id;
    // ---- 收盘检测 ----
    bool accepting_orders{true};  // clob accepting_orders (true→false 翻转 = 收盘)
    bool closed{false};           // clob closed (权威结算闸)
    // ---- 权威结算 (closed/winner 后填) ----
    std::int8_t settlement_value{-1};  // -1未知 / 0=NO赢(YES结算0) / 1=YES赢(YES结算1)
    std::string winner_token_id;       // tokens[i].winner=true 的 token_id
    // ---- R-20 上游时戳 (ISO→unix_ns; 0=未解析) ----
    std::int64_t accepting_order_ts_ns{0};  // accepting_order_timestamp (data_source_ts 锚)
    std::int64_t end_date_ts_ns{0};         // end_date_iso (event_ts 锚, 排定结束)
    bool valid{false};                      // 解析成功 (有 condition_id 或 token)

    // 收盘状态枚举 (喂 ResolutionEntry.status: 0=Open/1=Resolving/2=Resolved)。
    [[nodiscard]] std::uint8_t resolution_status() const noexcept {
        if (closed) return 2;            // Resolved
        if (!accepting_orders) return 1;  // Resolving (停接单但未链上结算)
        return 0;                         // Open
    }
};

namespace settlement_detail {

// ISO8601 "YYYY-MM-DDTHH:MM:SSZ" → unix ns (UTC)。解析失败 → 0。
[[nodiscard]] inline std::int64_t ParseIso8601ToUnixNs(std::string_view s) noexcept {
    if (s.size() < 20) return 0;
    auto d2 = [&](std::size_t i) -> int {
        if (i + 1 >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i])) ||
            !std::isdigit(static_cast<unsigned char>(s[i + 1])))
            return -1;
        return (s[i] - '0') * 10 + (s[i + 1] - '0');
    };
    auto d4 = [&](std::size_t i) -> int {
        int y = 0;
        for (std::size_t k = i; k < i + 4; ++k) {
            if (k >= s.size() || !std::isdigit(static_cast<unsigned char>(s[k]))) return -1;
            y = y * 10 + (s[k] - '0');
        }
        return y;
    };
    const int year = d4(0), mon = d2(5), day = d2(8), hh = d2(11), mm = d2(14), ss = d2(17);
    if (year < 1970 || mon < 1 || day < 1 || hh < 0 || mm < 0 || ss < 0) return 0;
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = mon - 1;
    tm.tm_mday = day;
    tm.tm_hour = hh;
    tm.tm_min = mm;
    tm.tm_sec = ss;
    const std::time_t t = timegm(&tm);  // UTC (禁 mktime 本地时区污染)
    if (t < 0) return 0;
    return static_cast<std::int64_t>(t) * 1'000'000'000LL;
}

// 提取 "key":"value" 的字符串值 (无完整 JSON parser; 同 live_book_publisher 范式)。
[[nodiscard]] inline std::string_view ExtractString(std::string_view body, std::string_view key) noexcept {
    std::string needle = "\"";
    needle.append(key);
    needle.append("\"");
    std::size_t pk = body.find(needle);
    if (pk == std::string_view::npos) return {};
    std::size_t pos = pk + needle.size();
    while (pos < body.size() && (body[pos] == ':' || body[pos] == ' ')) ++pos;
    if (pos >= body.size() || body[pos] != '"') return {};
    ++pos;
    std::size_t end = body.find('"', pos);
    if (end == std::string_view::npos) return {};
    return body.substr(pos, end - pos);
}

// 提取 "key":true/false (bool; 默认 def)。
[[nodiscard]] inline bool ExtractBool(std::string_view body, std::string_view key, bool def) noexcept {
    std::string needle = "\"";
    needle.append(key);
    needle.append("\"");
    std::size_t pk = body.find(needle);
    if (pk == std::string_view::npos) return def;
    std::size_t pos = pk + needle.size();
    while (pos < body.size() && (body[pos] == ':' || body[pos] == ' ')) ++pos;
    if (body.compare(pos, 4, "true") == 0) return true;
    if (body.compare(pos, 5, "false") == 0) return false;
    return def;
}

}  // namespace settlement_detail

// ParseMarketJson — clob /markets/{cid} JSON → SettlementRecord。纯函数, 无 IO。
//   解析 closed/accepting_orders/时戳 + 扫 tokens[] 找 winner (outcome "Yes"/"No" → settlement_value)。
[[nodiscard]] inline SettlementRecord ParseMarketJson(std::string_view json,
                                                      std::string_view condition_id) noexcept {
    using namespace settlement_detail;
    SettlementRecord r;
    r.condition_id = std::string(condition_id);
    r.accepting_orders = ExtractBool(json, "accepting_orders", true);
    r.closed = ExtractBool(json, "closed", false);
    r.accepting_order_ts_ns = ParseIso8601ToUnixNs(ExtractString(json, "accepting_order_timestamp"));
    r.end_date_ts_ns = ParseIso8601ToUnixNs(ExtractString(json, "end_date_iso"));

    // 扫 tokens[]: 找 winner=true 的 token, 按其 outcome 定 settlement_value (YES-canonical)。
    //   tokens 是对象数组 [{"token_id":"..","outcome":"Yes","price":..,"winner":true}, ...]。
    //   逐 token 子对象切片: 找 "winner":true, 取同对象的 outcome + token_id。
    std::size_t tpos = json.find("\"tokens\"");
    if (tpos != std::string_view::npos) {
        std::size_t scan = tpos;
        // 逐个 token 对象 (depth-1 的 {...}) 扫描
        while (scan < json.size()) {
            std::size_t obj_start = json.find('{', scan);
            if (obj_start == std::string_view::npos) break;
            // 找匹配 '}' (token 对象不含嵌套, 简单找下一个 '}')
            std::size_t obj_end = json.find('}', obj_start);
            if (obj_end == std::string_view::npos) break;
            std::string_view tok = json.substr(obj_start, obj_end - obj_start + 1);
            // tokens 数组结束判定: 下一个 token 起点超过 ']'
            std::size_t arr_end = json.find(']', tpos);
            if (arr_end != std::string_view::npos && obj_start > arr_end) break;
            if (ExtractBool(tok, "winner", false)) {
                const std::string_view outcome = ExtractString(tok, "outcome");
                r.winner_token_id = std::string(ExtractString(tok, "token_id"));
                // YES-canonical: outcome "Yes" → 1, "No" → 0 (大小写不敏感首字母)
                if (!outcome.empty()) {
                    const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(outcome[0])));
                    r.settlement_value = (c == 'y') ? std::int8_t{1} : std::int8_t{0};
                }
                break;
            }
            scan = obj_end + 1;
        }
    }
    r.valid = !r.condition_id.empty();
    return r;
}

}  // namespace stcpp::data
