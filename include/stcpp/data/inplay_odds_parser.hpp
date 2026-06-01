// include/stcpp/data/inplay_odds_parser.hpp — Goalserve inplay bet365 赔率解析 → 单源 de-vig fair
//
// Owner: 老雷 (GM) — 按文档接口建 (xiaoduan-goalserve-adapter-schema-v1 §odds + cross-source-mapping)
// last_review: 2026-05-31
//
// 真实结构 (xiaoduan-w8 §3.2 实测样本 inplay.goalserve.com/inplay-soccer.gz, 2026-05-28):
//   { "odds": { "<market_id>": { "name": "1x2 (Full Time)", "participants": {
//       "<pid>": { "name": "Home", "value_eu": "8.5", "suspend": "0" },
//       "<pid>": { "name": "Draw", "value_eu": "...",  "suspend": "0" },
//       "<pid>": { "name": "Away", "value_eu": "...",  "suspend": "0" } } } } }
//   pid = 长数字串 (非 1/2/3); participant 靠 name (Home/Draw/Away) 区分, 非位置 (Goalserve 不保证序)。
//   market_id "1"=1X2(Full Time), "27"=1X2(1st Half) (xiaoduan-w8 §4.3 字典)。
//   implied_p = 1.0/value_eu;  inplay 单源 bet365 → multiplicative de-vig (p_i = implied_i / Σ)。
//   home/YES = name=="Home" 腿; suspend=="1"/true 的腿剔除 (fail-closed: home/away 缺活跃腿 → invalid)。
//
// 加固史 (2026-05-31, 老雷 — 老板纠正"那份归档数据不全, inplay 有赔率"后核对真结构):
//   ① market_id 锚 `"<id>":` (旧 `"<id>"` 会误匹配 `"suspend":"1"` 暂停值)。
//   ② participant 按 name 匹配 (旧靠位置 implied[0]=home, Goalserve 换序则静默取错 → 喂模型错值)。
//   ③ suspend 感知 (暂停腿剔除)。无 name 的旧合成结构 → 位置回退 (兼容旧单测)。
//
// ⚠ 数据现状: inplay.goalserve.com/inplay-soccer.gz **有 odds** (老板确认 + w8 实测样本);
//   v2.1 的 NO_ODDS 是 www 节点 base feed (soccernew/home 只比分), 非此 EU Sofia odds 源。
//   待 inplay 白名单 (403) + 有在赛比赛时数据流入。结构按 w8 实测样本, 非想象。
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

// 提取 participant 的 "name" 值 (Home/Draw/Away)。无 → 空 view。
[[nodiscard]] inline std::string_view ExtractName(std::string_view obj) noexcept {
    const std::size_t k = obj.find("\"name\"");
    if (k == std::string_view::npos) return {};
    std::size_t pos = k + 6;
    while (pos < obj.size() && (obj[pos] == ':' || obj[pos] == ' ')) ++pos;
    if (pos >= obj.size() || obj[pos] != '"') return {};
    ++pos;
    const std::size_t start = pos;
    while (pos < obj.size() && obj[pos] != '"') ++pos;
    return (pos <= obj.size()) ? obj.substr(start, pos - start) : std::string_view{};
}

// 暂停? "suspend":"1" 或 "suspend":true → true (该腿剔除)。缺/"0"/false → false (活跃)。
[[nodiscard]] inline bool ExtractSuspended(std::string_view obj) noexcept {
    const std::size_t k = obj.find("\"suspend\"");
    if (k == std::string_view::npos) return false;
    std::size_t pos = k + 9;
    while (pos < obj.size() && (obj[pos] == ':' || obj[pos] == ' ' || obj[pos] == '"')) ++pos;
    return (pos < obj.size() && (obj[pos] == '1' || obj[pos] == 't'));  // "1" / true
}

}  // namespace inplay_odds_detail

// ParseInplayOddsDevig — 从一个 event 的 odds JSON 抽指定 market 的 participants → 单源 de-vig。
//   odds_json: events.<id>.odds 节点 (或含它的更大串); market_id: 目标盘口 (查 w8 §4.3 字典, "1"=1X2全场)。
//   纯函数。按 name (Home/Draw/Away) 匹配腿; 无 name → 位置回退。暂停腿剔除。
//   home/away 缺活跃腿 / 无该 market → valid=false (fail-closed, 喂模型 NaN 不喂错值)。
[[nodiscard]] inline InplayOddsDevig ParseInplayOddsDevig(std::string_view odds_json,
                                                          std::string_view market_id) noexcept {
    using namespace inplay_odds_detail;
    InplayOddsDevig out;
    // ① 锚定 market KEY `"<id>":` (含冒号 → 不误匹配 `"suspend":"1"` / value 里的 "1")。
    std::string needle = "\"";
    needle.append(market_id);
    needle.append("\":");
    const std::size_t mpos = odds_json.find(needle);
    if (mpos == std::string_view::npos) return out;
    const std::size_t ppos = odds_json.find("\"participants\"", mpos);
    if (ppos == std::string_view::npos) return out;
    const std::size_t scan = odds_json.find('{', ppos + 14);  // participants 的 '{'
    if (scan == std::string_view::npos) return out;

    // ② 逐 participant 子对象, 收集 {name, value_eu, suspended} (保序, 供位置回退)。
    struct Leg {
        std::string_view name;
        double value_eu;
    };
    std::vector<Leg> legs;
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
                const std::string_view obj = odds_json.substr(obj_start, i - obj_start + 1);
                const double v = ExtractValueEu(obj);
                if (v > 1.0 && !ExtractSuspended(obj)) {  // 暂停腿剔除
                    legs.push_back({ExtractName(obj), v});
                }
                obj_start = std::string_view::npos;
            }
            if (depth == 0) break;  // participants 对象结束
        }
    }
    if (legs.size() < 2) return out;  // 至少 2-way (home+away)

    // ③ 按 name 匹配 Home/Away/Draw; 无 name → 位置回退 (first=home, last=away, mid=draw)。
    double home_eu = 0.0, away_eu = 0.0, draw_eu = 0.0;
    bool by_name = false;
    for (const auto& l : legs) {
        if (l.name == "Home") { home_eu = l.value_eu; by_name = true; }
        else if (l.name == "Away") { away_eu = l.value_eu; by_name = true; }
        else if (l.name == "Draw") { draw_eu = l.value_eu; }
    }
    if (!by_name) {  // 旧合成结构 / 无 name → 位置回退
        home_eu = legs.front().value_eu;
        away_eu = legs.back().value_eu;
        if (legs.size() >= 3) draw_eu = legs[1].value_eu;
    }
    if (!(home_eu > 1.0 && away_eu > 1.0)) return out;  // home/away 必须有活跃腿

    const double ih = 1.0 / home_eu;
    const double ia = 1.0 / away_eu;
    const double idr = (draw_eu > 1.0) ? (1.0 / draw_eu) : 0.0;
    const double sum = ih + ia + idr;
    if (!(sum > 0.0)) return out;
    out.n_participants = legs.size();
    out.home_fair = ih / sum;  // de-vig home(YES) 胜率
    out.away_fair = ia / sum;
    out.draw_fair = idr / sum;  // 无 draw → 0
    out.valid = true;
    return out;
}

namespace inplay_odds_detail {
// 大小写不敏感: hay 是否含 needle。
[[nodiscard]] inline bool IContains(std::string_view hay, std::string_view needle) noexcept {
    if (needle.empty() || needle.size() > hay.size()) return false;
    for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        bool ok = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            if (std::tolower(static_cast<unsigned char>(hay[i + j])) !=
                std::tolower(static_cast<unsigned char>(needle[j]))) {
                ok = false;
                break;
            }
        }
        if (ok) return true;
    }
    return false;
}
// 大小写不敏感全等。
[[nodiscard]] inline bool IEquals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}
}  // namespace inplay_odds_detail

// SelectMatchWinnerMarketId — 在 odds JSON 顶层 markets 里按 name 选"赛果胜负盘"的 market_id。
//   (小田 体育市场专家 2026-06-01: 各运动赛果盘 market_id 不稳定/跨字典漂移, 按 name 选更鲁棒。)
//   规则: 遍历每个 market → 取其 market 级 "name" → 先排 forbidden substr (set/game/handicap/half...)
//   → 再 IEquals 命中 allow_names 即返其 key。fail-closed: 全 miss 返 ""。
[[nodiscard]] inline std::string SelectMatchWinnerMarketId(
    std::string_view odds_json, const std::vector<std::string_view>& allow_names,
    const std::vector<std::string_view>& forbidden_substrs) noexcept {
    using namespace inplay_odds_detail;
    const std::size_t root = odds_json.find('{');
    if (root == std::string_view::npos) return {};
    std::size_t i = root + 1;
    while (i < odds_json.size()) {
        // 跳到下一 market key 的引号 (depth-1); 遇 odds 根 '}' 收尾。
        while (i < odds_json.size() && odds_json[i] != '"' && odds_json[i] != '}') ++i;
        if (i >= odds_json.size() || odds_json[i] == '}') break;
        const std::size_t kstart = i + 1;
        const std::size_t kend = odds_json.find('"', kstart);
        if (kend == std::string_view::npos) break;
        const std::string_view key = odds_json.substr(kstart, kend - kstart);
        const std::size_t objstart = odds_json.find('{', kend);
        if (objstart == std::string_view::npos) break;
        std::size_t j = objstart + 1;
        int d = 1;
        while (j < odds_json.size() && d > 0) {
            if (odds_json[j] == '{') ++d;
            else if (odds_json[j] == '}') --d;
            ++j;
        }
        const std::string_view market_obj = odds_json.substr(objstart, j - objstart);
        const std::string_view mname = inplay_odds_detail::ExtractName(market_obj);  // 第一个 name = market 级
        if (!mname.empty()) {
            bool forbidden = false;
            for (const auto& f : forbidden_substrs)
                if (IContains(mname, f)) { forbidden = true; break; }
            if (!forbidden) {
                for (const auto& a : allow_names)
                    if (IEquals(mname, a)) return std::string(key);
            }
        }
        i = j;  // 下一 market
    }
    return {};
}

// ParseInplayOddsDevigByName — 按 name allowlist 选赛果盘 → 复用 ParseInplayOddsDevig de-vig。
//   各运动通用 (soccer 3-way / tennis·basket 2-way), 替代硬编码 market_id。fail-closed: 选不到 → invalid。
[[nodiscard]] inline InplayOddsDevig ParseInplayOddsDevigByName(
    std::string_view odds_json, const std::vector<std::string_view>& allow_names,
    const std::vector<std::string_view>& forbidden_substrs) noexcept {
    const std::string mid = SelectMatchWinnerMarketId(odds_json, allow_names, forbidden_substrs);
    if (mid.empty()) return {};
    return ParseInplayOddsDevig(odds_json, mid);
}

// InplayYesCanonical — Goalserve home/away 视角 → Polymarket YES/对手 视角的翻转结果。
struct InplayYesCanonical {
    double yes_fair{-1.0};  // YES 边 (被交易盘口的 "YES" 结果) de-vig 胜率
    double opp_fair{-1.0};  // 对手边
};

// ToYesCanonical — 按 yes_is_home 把 Goalserve home/away fair 翻成 YES-canonical (消 home/YES 混淆)。
//   yes_is_home=true:  YES token = home 队胜 → yes=home_fair, opp=away_fair
//   yes_is_home=false: YES token = away 队胜 → yes=away_fair, opp=home_fair (镜像翻转!)
//   -1 (无 odds) 自然透传。纯函数, BR-1 回测/实盘共用, 与比分翻转 (yes_is_home) 同源, 保证
//   g_bm_inplay_fair 与 g_score_diff 视角一致 (否则 away=YES 盘口两特征方向相反)。
[[nodiscard]] inline InplayYesCanonical ToYesCanonical(bool yes_is_home, double home_fair,
                                                       double away_fair) noexcept {
    return yes_is_home ? InplayYesCanonical{home_fair, away_fair}
                       : InplayYesCanonical{away_fair, home_fair};
}

}  // namespace stcpp::data
