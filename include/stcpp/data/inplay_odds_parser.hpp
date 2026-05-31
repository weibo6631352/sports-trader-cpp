// include/stcpp/data/inplay_odds_parser.hpp — Goalserve inplay bet365 赔率解析 → 单源 de-vig fair
//
// Owner: 老雷 (GM) — 按文档接口建 (xiaoduan-goalserve-adapter-schema-v1 §odds + cross-source-mapping)
// last_review: 2026-05-31
//
// 文档结构 (events.<id>.odds.<market_id>.participants.<pid>):
//   { "odds": { "<market_id>": { "name": "...", "participants": {
//       "<pid_home>": { "value_eu": "1.85", "suspend": false, ... },
//       "<pid_draw>": { "value_eu": "3.40", ... },
//       "<pid_away>": { "value_eu": "4.20", ... } } } } }
//   implied_p = 1.0 / value_eu;  inplay = 单源 bet365 → multiplicative de-vig (p_i = implied_i / Σ)。
//   home/YES = participants 第一个 (cross-source-mapping: "Home"→YES token)。
//
// ⚠ 数据现状 (xiaoduan-goalserve-odds-by-sport-v2.1): 当前 key **无 odds plan**, 五大运动 NO_ODDS。
//   本 parser 代码就位, 待 odds plan 升级 + inplay 白名单后数据流入。结构按文档, 非想象。
//
// 红线: 纯函数无 IO; vendor-agnostic (输出语义 fair prob, 不泄原始结构)。
#pragma once

#include <cctype>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stcpp::data {

// 单个 market 的 de-vig 结果 (YES-canonical: home_fair = home 胜 = YES)。
struct InplayOddsDevig {
    double home_fair{0.0};  // de-vig home(YES) 胜率
    double away_fair{0.0};  // de-vig away 胜率 (2-way: = 1-home; 3-way: 含 draw 后)
    double draw_fair{0.0};  // 平局 (无平局市场 = 0)
    std::size_t n_participants{0};
    bool valid{false};
};

namespace inplay_odds_detail {

// 提取 value_eu (decimal odds string) → double。无效 → 0。
[[nodiscard]] inline double ExtractValueEu(std::string_view participant_obj) noexcept {
    const std::size_t k = participant_obj.find("\"value_eu\"");
    if (k == std::string_view::npos) return 0.0;
    std::size_t pos = k + 10;
    while (pos < participant_obj.size() && (participant_obj[pos] == ':' || participant_obj[pos] == ' '))
        ++pos;
    const bool quoted = (pos < participant_obj.size() && participant_obj[pos] == '"');
    if (quoted) ++pos;
    char buf[32];
    std::size_t n = 0;
    while (pos < participant_obj.size() && n + 1 < sizeof(buf) &&
           (std::isdigit(static_cast<unsigned char>(participant_obj[pos])) || participant_obj[pos] == '.'))
        buf[n++] = participant_obj[pos++];
    if (n == 0) return 0.0;
    buf[n] = '\0';
    return std::strtod(buf, nullptr);
}

}  // namespace inplay_odds_detail

// ParseInplayOddsDevig — 从一个 event 的 odds JSON 抽指定 market 的 participants value_eu → 单源 de-vig。
//   odds_json: events.<id>.odds 节点 (或含它的更大串); market_id: 目标盘口 (1x2/moneyline, 查字典定)。
//   纯函数。无该 market / value_eu 全无效 → valid=false (fail-closed)。
[[nodiscard]] inline InplayOddsDevig ParseInplayOddsDevig(std::string_view odds_json,
                                                          std::string_view market_id) noexcept {
    using namespace inplay_odds_detail;
    InplayOddsDevig out;
    // 定位 "<market_id>" 节点。
    std::string needle = "\"";
    needle.append(market_id);
    needle.append("\"");
    const std::size_t mpos = odds_json.find(needle);
    if (mpos == std::string_view::npos) return out;
    // participants 节点。
    const std::size_t ppos = odds_json.find("\"participants\"", mpos);
    if (ppos == std::string_view::npos) return out;
    // 逐 participant 子对象抽 value_eu (顺序 = home/[draw]/away, cross-source-mapping)。
    std::vector<double> implied;
    std::size_t scan = odds_json.find('{', ppos + 14);  // participants 的 '{'
    if (scan == std::string_view::npos) return out;
    std::size_t depth = 0;
    std::size_t obj_start = std::string_view::npos;
    for (std::size_t i = scan; i < odds_json.size(); ++i) {
        const char c = odds_json[i];
        if (c == '{') {
            if (depth == 1 && obj_start == std::string_view::npos) obj_start = i;
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 1 && obj_start != std::string_view::npos) {
                const double v = ExtractValueEu(odds_json.substr(obj_start, i - obj_start + 1));
                if (v > 1.0) implied.push_back(1.0 / v);  // implied prob
                obj_start = std::string_view::npos;
            }
            if (depth == 0) break;  // participants 对象结束
        }
    }
    if (implied.size() < 2) return out;  // 至少 2-way
    double sum = 0.0;
    for (double p : implied) sum += p;
    if (!(sum > 0.0)) return out;
    out.n_participants = implied.size();
    out.home_fair = implied[0] / sum;             // 第一个 = home/YES
    out.away_fair = implied[implied.size() - 1] / sum;  // 最后 = away
    if (implied.size() >= 3) out.draw_fair = implied[1] / sum;  // 中间 = draw (3-way)
    out.valid = true;
    return out;
}

}  // namespace stcpp::data
