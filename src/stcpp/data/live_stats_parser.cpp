// stcpp/data/live_stats_parser.cpp — Soccer live_stats KV parser 实现
//
// Owner: 小段 (goalserve-specialist, #37)
// Date:  2026-05-31
//
// 格式 (实测 livescore.goalserve.com/getfeed/.../soccernew/home, 2026-05-30):
//   <live_stats> 属性或子节点值:
//   "IDangerousAttacks=home:40,away:43|IOnTarget=home:3,away:6|
//    IPosession=home:57,away:43|IRedCard=home:0,away:0|
//    IYellowCard=home:2,away:1|ICorner=home:5,away:3|
//    IFreeKick=home:12,away:14|IAttacks=home:98,away:87|
//    IGoalKick=home:4,away:6|IThrowIn=home:17,away:19"
//
// 解析算法:
//   1. split '|' → segments
//   2. 每个 segment: split '=' → key + value_part
//   3. value_part: "home:N,away:N"
//      - split ',' → home_part + away_part
//      - home_part: strip "home:" prefix → from_chars
//      - away_part: strip "away:" prefix → from_chars
//   4. key → LiveStatsFields 字段映射 (switch on first char + length)
//
// 设计约束:
//   - 无动态内存分配 (string_view 全程)
//   - 无异常 (noexcept 全链)
//   - 未知 key 静默忽略 (前向兼容, Goalserve 可能新增字段)
//   - 负数/非数字 → 保持 -1 (ParseInt32 返回 false)

#include "stcpp/data/live_stats_parser.hpp"

#include <charconv>
#include <cstdint>
#include <string_view>

namespace stcpp::data::livescore {

namespace {

// ----------------------------------------------------------------------------
// strip_prefix — 如果 sv 以 prefix 开头, 返回去掉前缀后的部分; 否则返回 {}
// ----------------------------------------------------------------------------
[[nodiscard]] constexpr std::string_view strip_prefix(std::string_view sv,
                                                       std::string_view prefix) noexcept {
    if (sv.size() >= prefix.size() && sv.substr(0, prefix.size()) == prefix) {
        return sv.substr(prefix.size());
    }
    return {};
}

// ----------------------------------------------------------------------------
// trim_sv — 去掉前后 ASCII 空白 (space / tab / CR / LF)
// ----------------------------------------------------------------------------
[[nodiscard]] constexpr std::string_view trim_sv(std::string_view sv) noexcept {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' ||
                           sv.front() == '\r' || sv.front() == '\n')) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' ||
                           sv.back() == '\r' || sv.back() == '\n')) {
        sv.remove_suffix(1);
    }
    return sv;
}

// ----------------------------------------------------------------------------
// parse_int32 — std::string_view → int32_t via from_chars
//
// 返回 false = 空串 / 非数字 / 溢出; val 不修改 (保持 -1 sentinel).
// 不接受负数 (live_stats 值域 >= 0).
// ----------------------------------------------------------------------------
[[nodiscard]] bool parse_int32(std::string_view sv, std::int32_t& val) noexcept {
    sv = trim_sv(sv);
    if (sv.empty()) return false;
    // 拒绝负号 (live_stats 无负值)
    if (sv.front() == '-') return false;
    std::int32_t tmp = 0;
    const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), tmp);
    if (ec != std::errc{}) return false;
    val = tmp;
    return true;
}

// ----------------------------------------------------------------------------
// parse_home_away — 解析 "home:N,away:N" 格式的 value_part
//
// home_val / away_val: -1 = 该侧缺失/解析失败 (不修改传入值)
// 返回 true = 至少 home 侧解析成功
// ----------------------------------------------------------------------------
bool parse_home_away(std::string_view value_part,
                     std::int32_t& home_val,
                     std::int32_t& away_val) noexcept {
    value_part = trim_sv(value_part);
    if (value_part.empty()) return false;

    // 找 ',' 分隔 home 和 away
    const auto comma_pos = value_part.find(',');
    const auto home_part = (comma_pos != std::string_view::npos)
                               ? value_part.substr(0, comma_pos)
                               : value_part;
    const auto away_part = (comma_pos != std::string_view::npos)
                               ? value_part.substr(comma_pos + 1)
                               : std::string_view{};

    // home: strip "home:" prefix
    const auto home_num = strip_prefix(trim_sv(home_part), "home:");
    bool home_ok = !home_num.empty() && parse_int32(home_num, home_val);

    // away: strip "away:" prefix
    if (!away_part.empty()) {
        const auto away_num = strip_prefix(trim_sv(away_part), "away:");
        if (!away_num.empty()) {
            [[maybe_unused]] bool away_ok = parse_int32(away_num, away_val);
        }
    }

    return home_ok;
}

// ----------------------------------------------------------------------------
// apply_kv — 已知 key + home/away 值, 写入 LiveStatsFields
//
// key 匹配: Goalserve 实测键 (大小写敏感, 原始拼写含 IPosession 单 s).
// 未知 key 静默忽略 (前向兼容).
// ----------------------------------------------------------------------------
void apply_kv(std::string_view key,
              std::int32_t home_val,
              std::int32_t away_val,
              LiveStatsFields& out) noexcept {
    // 用 first char + length 快速分支, 避免全量字符串比较 O(N×M)
    if (key.empty()) return;

    switch (key[0]) {
        case 'I':
            if (key == "IDangerousAttacks") {
                out.dangerous_attacks_home = home_val;
                out.dangerous_attacks_away = away_val;
            } else if (key == "IOnTarget") {
                out.shots_on_target_home = home_val;
                out.shots_on_target_away = away_val;
            } else if (key == "IPosession") {
                // Goalserve 原始拼写: 单 s (IPosession, 非 IPossession)
                out.possession_home_pct = home_val;
                out.possession_away_pct = away_val;
            } else if (key == "IRedCard") {
                out.red_cards_home = home_val;
                out.red_cards_away = away_val;
            } else if (key == "IYellowCard") {
                out.yellow_cards_home = home_val;
                out.yellow_cards_away = away_val;
            } else if (key == "ICorner") {
                out.corners_home = home_val;
                out.corners_away = away_val;
            } else if (key == "IFreeKick") {
                out.free_kicks_home = home_val;
                out.free_kicks_away = away_val;
            } else if (key == "IAttacks") {
                out.attacks_home = home_val;
                out.attacks_away = away_val;
            } else if (key == "IGoalKick") {
                out.goal_kicks_home = home_val;
                out.goal_kicks_away = away_val;
            } else if (key == "IThrowIn") {
                out.throw_ins_home = home_val;
                out.throw_ins_away = away_val;
            }
            // else: 未知 key, 忽略 (前向兼容)
            break;
        default:
            // 非 I-prefix key: 忽略 (Goalserve 目前全部以 I 开头)
            break;
    }
}

}  // anonymous namespace

// ============================================================================
// LiveStatsParser::ParseKvPair
// ============================================================================
bool LiveStatsParser::ParseKvPair(std::string_view segment,
                                  std::string_view& key_out,
                                  std::int32_t& home_val,
                                  std::int32_t& away_val) noexcept {
    segment = trim_sv(segment);
    if (segment.empty()) return false;

    const auto eq_pos = segment.find('=');
    if (eq_pos == std::string_view::npos) return false;

    key_out = trim_sv(segment.substr(0, eq_pos));
    const auto value_part = segment.substr(eq_pos + 1);

    // home_val / away_val 初始值由调用方设定 (一般是 -1); 这里不重置
    return parse_home_away(value_part, home_val, away_val);
}

// ============================================================================
// LiveStatsParser::Parse
// ============================================================================
LiveStatsFields LiveStatsParser::Parse(std::string_view live_stats_str) noexcept {
    LiveStatsFields out{};  // 全 -1

    live_stats_str = trim_sv(live_stats_str);
    if (live_stats_str.empty()) return out;

    // split '|' → segments, 逐段解析
    std::string_view remaining = live_stats_str;
    while (!remaining.empty()) {
        const auto pipe_pos = remaining.find('|');
        const auto segment = (pipe_pos != std::string_view::npos)
                                 ? remaining.substr(0, pipe_pos)
                                 : remaining;
        remaining = (pipe_pos != std::string_view::npos)
                        ? remaining.substr(pipe_pos + 1)
                        : std::string_view{};

        // 解析单个 KV 段
        std::string_view key{};
        std::int32_t home_val = -1;
        std::int32_t away_val = -1;
        if (!ParseKvPair(segment, key, home_val, away_val)) {
            continue;  // 格式错误: 跳过此段
        }

        apply_kv(key, home_val, away_val, out);
    }

    return out;
}

}  // namespace stcpp::data::livescore
