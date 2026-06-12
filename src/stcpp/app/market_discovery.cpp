// src/stcpp/app/market_discovery.cpp — Gamma 体育市场发现实现
//
// Owner: 老雷 (GM) — 从 debug_server_main.cpp 抽出 (TraderDaemon 重构, 老郭 §A.1)
// last_review: 2026-05-30
//
// 逐字搬迁自 debug_server_main.cpp 匿名 namespace, 解析逻辑行为不变.
// 老周补充: Fetch (popen IO) 与 Parse (纯函数) 拆开, Parse 层喂 fixture 可单测.
// ToS: 只读公开 gamma REST, curl 自动读 HTTPS_PROXY/HTTP_PROXY, 不下单.

#include "stcpp/app/market_discovery.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <cstdlib>
#include <ctime>
#include <unordered_set>

namespace stcpp::app {

namespace discovery_detail {

// 最小 JSON string 提取 (no malloc, in-place parse)
std::string ExtractJsonStr(const std::string& json, const std::string& key) {
    const std::string needle1 = "\"" + key + "\":\"";
    const std::string needle2 = "\"" + key + "\": \"";
    std::size_t pos = json.find(needle1);
    const std::size_t pos2 = json.find(needle2);
    std::size_t nlen = 0;
    if (pos != std::string::npos) {
        nlen = needle1.size();
    } else if (pos2 != std::string::npos) {
        pos = pos2;
        nlen = needle2.size();
    } else {
        return "";
    }
    pos += nlen;
    std::size_t end = pos;
    bool esc = false;
    while (end < json.size()) {
        if (esc) {
            esc = false;
            ++end;
            continue;
        }
        if (json[end] == '\\') {
            esc = true;
            ++end;
            continue;
        }
        if (json[end] == '"')
            break;
        ++end;
    }
    return json.substr(pos, end - pos);
}

static double ExtractJsonNumIn(const std::string& json, const std::string& key, std::size_t scan_from,
                               std::size_t scan_to, double fallback);  // 前置声明 (定义在下方)

// Extract nested "sport" object → 联赛码 (sport.sport, e.g. "nba"/"bkcba") + 稳定 id (sport.id)。
// 真实 gamma: "sport":{"id":34,"sport":"nba",...} (对象, 非 string)。找 "sport":{ 起的平衡对象再取内层。
struct SportObj {
    std::string code;
    std::int64_t id{0};
};
static SportObj ExtractSportObject(const std::string& json) {
    SportObj out;
    const std::string needle = "\"sport\":{";
    std::size_t pos = json.find(needle);
    if (pos == std::string::npos)
        return out;
    std::size_t start = pos + needle.size() - 1;  // 指向 '{'
    std::size_t depth = 0, i = start;
    bool in_str = false, esc = false;
    for (; i < json.size(); ++i) {
        const char c = json[i];
        if (esc) {
            esc = false;
            continue;
        }
        if (c == '\\') {
            esc = true;
            continue;
        }
        if (c == '"') {
            in_str = !in_str;
            continue;
        }
        if (in_str)
            continue;
        if (c == '{') {
            ++depth;
        } else if (c == '}') {
            if (--depth == 0) {
                ++i;
                break;
            }
        }
    }
    const std::string obj = json.substr(start, i - start);
    out.code = ExtractJsonStr(obj, "sport");                                              // 联赛码
    out.id = static_cast<std::int64_t>(ExtractJsonNumIn(obj, "id", 0, obj.size(), 0.0));  // sport.id
    return out;
}

// Extract a numeric value under `key` ("key":<num> 或 "key": <num>). 找不到/非法返 fallback.
// 只在 [scan_from, scan_to) 区间内找 (用于限定 feeSchedule 子对象). 处理负号/小数/科学计数.
static double ExtractJsonNumIn(const std::string& json, const std::string& key, std::size_t scan_from,
                               std::size_t scan_to, double fallback) {
    const std::string needle = "\"" + key + "\":";
    std::size_t pos = json.find(needle, scan_from);
    if (pos == std::string::npos || pos >= scan_to)
        return fallback;
    pos += needle.size();
    while (pos < scan_to && (json[pos] == ' ' || json[pos] == '\t'))
        ++pos;
    if (pos < scan_to && json[pos] == '"')
        ++pos;  // 容忍引号包裹数字 (gamma liquidity/volume 为 string decimal; strtod 在尾引号停)
    if (pos >= scan_to)
        return fallback;
    const char* start = json.c_str() + pos;
    char* end = nullptr;
    const double v = std::strtod(start, &end);
    if (end == start)
        return fallback;  // 非数字 (e.g. null/true)
    return v;
}

// gamma 手续费系数提取 — feeSchedule.rate + feesEnabled (见 hpp 注释).
double ExtractFeeRateCoef(const std::string& obj, double fallback_default) {
    // feesEnabled:false → 老市场免费, fee=0 (硬编 0.03 会错杀薄利单).
    const std::size_t fe = obj.find("\"feesEnabled\":");
    if (fe != std::string::npos) {
        // 取 "feesEnabled": 后第一个非空白 token 是否 'f' (false).
        std::size_t p = fe + std::string("\"feesEnabled\":").size();
        while (p < obj.size() && (obj[p] == ' ' || obj[p] == '\t'))
            ++p;
        if (p < obj.size() && obj[p] == 'f')
            return 0.0;
    }
    // feeSchedule.rate — 限定在 feeSchedule {...} 子对象内取 rate (防撞外层同名 key).
    const std::size_t fs = obj.find("\"feeSchedule\":");
    if (fs != std::string::npos) {
        const std::size_t brace = obj.find('{', fs);
        if (brace != std::string::npos) {
            const std::size_t close = obj.find('}', brace);
            const std::size_t scan_to = (close == std::string::npos) ? obj.size() : close;
            const double rate = ExtractJsonNumIn(obj, "rate", brace, scan_to, -1.0);
            if (rate >= 0.0)
                return std::clamp(rate, 0.0, 0.10);  // 钳脏数据
        }
    }
    return std::clamp(fallback_default, 0.0, 0.10);
}

// Extract a 2-string array under `key` — handles TWO gamma API encodings:
//   Native array:  "key":["a","b"]
//   JSON string:   "key":"[\"a\",\"b\"]"  (gamma /events encodes as string)
// Returns true and fills out0/out1 if at least 2 items found.
bool ExtractTwoStringArray(const std::string& obj, const std::string& key, std::string& out0,
                           std::string& out1) {
    const std::string needle = "\"" + key + "\":";
    std::size_t arr_start = obj.find(needle);
    if (arr_start == std::string::npos)
        return false;
    arr_start += needle.size();
    while (arr_start < obj.size() && (obj[arr_start] == ' ' || obj[arr_start] == '\n'))
        ++arr_start;
    if (arr_start >= obj.size())
        return false;

    // Determine content to scan: '[' (native) or '"[...' (JSON-encoded string)
    std::string content;
    if (obj[arr_start] == '[') {
        // Native JSON array — scan directly until ']'
        std::size_t end = obj.find(']', arr_start);
        if (end == std::string::npos)
            return false;
        content = obj.substr(arr_start, end - arr_start + 1);
    } else if (obj[arr_start] == '"') {
        // JSON-encoded string: "[\\"tok0\\",\\"tok1\\"]"
        // Extract the string value, then unescape \" → "
        ++arr_start;  // skip opening "
        std::size_t str_end = arr_start;
        bool esc = false;
        while (str_end < obj.size()) {
            if (esc) {
                esc = false;
                ++str_end;
                continue;
            }
            if (obj[str_end] == '\\') {
                esc = true;
                ++str_end;
                continue;
            }
            if (obj[str_end] == '"')
                break;
            ++str_end;
        }
        // Unescape: replace backslash-quote with quote, double-backslash with single
        content.reserve(str_end - arr_start);
        bool e = false;
        for (std::size_t i = arr_start; i < str_end; ++i) {
            if (e) {
                if (obj[i] == '"')
                    content.push_back('"');
                else if (obj[i] == '\\')
                    content.push_back('\\');
                else {
                    content.push_back('\\');
                    content.push_back(obj[i]);
                }
                e = false;
            } else if (obj[i] == '\\') {
                e = true;
            } else {
                content.push_back(obj[i]);
            }
        }
    } else {
        return false;
    }

    // Now content is like: ["tok0","tok1"]
    // Skip leading '[', extract quoted strings
    std::vector<std::string> tokens;
    std::size_t pos = 0;
    // find '['
    while (pos < content.size() && content[pos] != '[')
        ++pos;
    if (pos < content.size())
        ++pos;  // skip '['

    while (pos < content.size() && tokens.size() < 2) {
        while (pos < content.size() && content[pos] != '"' && content[pos] != ']')
            ++pos;
        if (pos >= content.size() || content[pos] == ']')
            break;
        ++pos;  // skip opening "
        std::size_t end = pos;
        while (end < content.size() && content[end] != '"')
            ++end;
        if (end > pos)
            tokens.push_back(content.substr(pos, end - pos));
        pos = end + 1;
    }
    if (tokens.size() < 2)
        return false;
    out0 = tokens[0];
    out1 = tokens[1];
    return true;
}

// clobTokenIds (YES/NO token ids) — 委托 ExtractTwoStringArray.
bool ExtractClobTokenIds(const std::string& obj, std::string& tok0, std::string& tok1) {
    return ExtractTwoStringArray(obj, "clobTokenIds", tok0, tok1);
}

// outcomes (moneyline 即两队名) — 委托 ExtractTwoStringArray.
bool ExtractOutcomes(const std::string& obj, std::string& out0, std::string& out1) {
    return ExtractTwoStringArray(obj, "outcomes", out0, out1);
}

// 解析 gamma 时间 → Unix 秒 (UTC). 支持 "YYYY-MM-DD HH:MM:SS+00" 与 ISO "YYYY-MM-DDTHH:MM:SS...Z".
// 只取前 19 字符的 Y-M-D H:M:S (分隔符 ' ' 或 'T'), 用 timegm 算 UTC epoch. 失败返 0.
std::int64_t ParseGammaTimeToEpochSec(const std::string& s) {
    if (s.size() < 19)
        return 0;
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    // 容忍日期与时间之间的分隔符 (' ' 或 'T'): 用 %d 间分隔, 中间符号单独 scan.
    char sep = 0;
    if (std::sscanf(s.c_str(), "%4d-%2d-%2d%c%2d:%2d:%2d", &y, &mo, &d, &sep, &h, &mi, &se) != 7)
        return 0;
    if (sep != ' ' && sep != 'T')
        return 0;
    if (y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31)
        return 0;
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_sec = se;
    const std::time_t epoch = ::timegm(&tm);  // UTC (gamma 时间均 UTC: +00 / Z)
    if (epoch < 0)
        return 0;
    return static_cast<std::int64_t>(epoch);
}

// Extract sportsMarketType → normalize to moneyline/spread/totals/outright/prop/series/unknown
std::string NormalizeSportsMarketType(const std::string& raw) {
    std::string s = raw;
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (s.find("money") != std::string::npos || s == "moneyline")
        return "moneyline";
    if (s.find("spread") != std::string::npos)
        return "spread";
    if (s.find("total") != std::string::npos || s.find("over") != std::string::npos)
        return "totals";
    if (s.find("outright") != std::string::npos || s.find("futures") != std::string::npos)
        return "outright";
    if (s.find("prop") != std::string::npos)
        return "prop";
    if (s.find("series") != std::string::npos)
        return "series";
    if (!raw.empty())
        return raw;
    return "unknown";
}

// Extract next balanced { } object from s starting at pos
// Returns {start, end} or {npos, npos}
std::pair<std::size_t, std::size_t> ExtractNextObject(const std::string& s, std::size_t pos) {
    while (pos < s.size() && s[pos] != '{') {
        if (s[pos] == ']')
            return {std::string::npos, std::string::npos};
        ++pos;
    }
    if (pos >= s.size())
        return {std::string::npos, std::string::npos};

    std::size_t start = pos;
    int depth = 0;
    bool in_str = false, esc = false;
    std::size_t end = pos;
    for (std::size_t i = pos; i < s.size(); ++i) {
        char c = s[i];
        if (esc) {
            esc = false;
            continue;
        }
        if (in_str) {
            if (c == '\\')
                esc = true;
            else if (c == '"')
                in_str = false;
            continue;
        }
        if (c == '"') {
            in_str = true;
            continue;
        }
        if (c == '{')
            ++depth;
        else if (c == '}') {
            --depth;
            if (depth == 0) {
                end = i;
                break;
            }
        }
    }
    if (end <= start)
        return {std::string::npos, std::string::npos};
    return {start, end};
}

// Extract "markets": [...] sub-array text from event object
std::string ExtractMarketsArray(const std::string& event_obj) {
    const std::string needle = "\"markets\":";
    std::size_t pos = event_obj.find(needle);
    if (pos == std::string::npos)
        return "";
    pos += needle.size();
    while (pos < event_obj.size() && (event_obj[pos] == ' ' || event_obj[pos] == '\n'))
        ++pos;
    if (pos >= event_obj.size() || event_obj[pos] != '[')
        return "";

    std::size_t start = pos;
    int depth = 0;
    bool in_str = false, esc = false;
    std::size_t end = pos;
    for (std::size_t i = pos; i < event_obj.size(); ++i) {
        char c = event_obj[i];
        if (esc) {
            esc = false;
            continue;
        }
        if (in_str) {
            if (c == '\\')
                esc = true;
            else if (c == '"')
                in_str = false;
            continue;
        }
        if (c == '"') {
            in_str = true;
            continue;
        }
        if (c == '[')
            ++depth;
        else if (c == ']') {
            --depth;
            if (depth == 0) {
                end = i;
                break;
            }
        }
    }
    if (end <= start)
        return "";
    return event_obj.substr(start, end - start + 1);
}

}  // namespace discovery_detail

// ---------------------------------------------------------------------------
// Parse 层 — 纯函数 (喂 JSON 字符串, 无网络)
// ---------------------------------------------------------------------------

std::vector<DiscoveredEvent> ParseSportsEvents(const std::string& json_buf, int max_events) {
    using namespace discovery_detail;
    std::vector<DiscoveredEvent> result;
    if (json_buf.empty())
        return result;

    std::size_t pos = 0;
    while (pos < json_buf.size() && static_cast<int>(result.size()) < max_events) {
        auto [ev_start, ev_end] = ExtractNextObject(json_buf, pos);
        if (ev_start == std::string::npos)
            break;

        std::string event_obj = json_buf.substr(ev_start, ev_end - ev_start + 1);
        pos = ev_end + 1;

        DiscoveredEvent ev;
        ev.event_id = ExtractJsonStr(event_obj, "id");
        ev.slug = ExtractJsonStr(event_obj, "slug");
        ev.title = ExtractJsonStr(event_obj, "title");
        ev.icon_url = ExtractJsonStr(event_obj, "icon");  // 赛事图 (前端事件头)
        if (ev.icon_url.empty()) ev.icon_url = ExtractJsonStr(event_obj, "image");
        // 真实 sport 是对象 {id, sport, ...}: 取联赛码 (sport.sport) + 稳定 id (sport.id)。
        const SportObj so = ExtractSportObject(event_obj);
        ev.sport_code = so.code;  // "nba"/"bkcba"/"atp"/...
        ev.sport_id = so.id;      // 34/104/45/... → cat_league (NBA≠CBA)
        // is_sports 过滤兜底: 真对象码 > 旧 string sport (兼容) > tag。
        ev.sport = !so.code.empty() ? so.code : ExtractJsonStr(event_obj, "sport");
        if (ev.sport.empty())
            ev.sport = ExtractJsonStr(event_obj, "tag");
        ev.neg_risk_market_id = ExtractJsonStr(event_obj, "negRiskMarketID");

        if (ev.event_id.empty())
            continue;

        // Sports filter
        const std::string title_l = [&]() {
            std::string s = ev.title + " " + ev.sport;
            for (char& c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }();
        static const char* kSportsKw[] = {
            "nba",       "nfl",       "mlb",        "nhl",      "ncaa",      "mls",
            "soccer",    "football",  "basketball", "baseball", "hockey",    "premier league",
            "champions", "world cup", "euro",       "tennis",   "wimbledon", "cricket",
            "ufc",       "mma",       nullptr};
        bool is_sports = (!ev.sport.empty() && ev.sport != "null");
        if (!is_sports) {
            for (int k = 0; kSportsKw[k] != nullptr; ++k) {
                if (title_l.find(kSportsKw[k]) != std::string::npos) {
                    is_sports = true;
                    break;
                }
            }
        }
        if (!is_sports)
            continue;

        // Parse markets[] sub-array
        const std::string markets_arr = ExtractMarketsArray(event_obj);
        if (markets_arr.empty())
            continue;

        std::size_t mpos = 1;  // skip '['
        while (mpos < markets_arr.size()) {
            auto [ms, me] = ExtractNextObject(markets_arr, mpos);
            if (ms == std::string::npos)
                break;

            std::string mobj = markets_arr.substr(ms, me - ms + 1);
            mpos = me + 1;

            DiscoveredMarket dm;
            dm.condition_id = ExtractJsonStr(mobj, "conditionId");
            dm.question = ExtractJsonStr(mobj, "question");
            dm.group_item_title = ExtractJsonStr(mobj, "groupItemTitle");
            dm.sports_market_type = NormalizeSportsMarketType(ExtractJsonStr(mobj, "sportsMarketType"));
            dm.line = ExtractJsonNumIn(mobj, "line", 0, mobj.size(),
                                       std::numeric_limits<double>::quiet_NaN());  // totals/spreads 线值
            const double knan = std::numeric_limits<double>::quiet_NaN();
            dm.volume_24h = ExtractJsonNumIn(mobj, "volume24hr", 0, mobj.size(), knan);  // 市场活跃度
            dm.liquidity = ExtractJsonNumIn(mobj, "liquidity", 0, mobj.size(), knan);    // book 流动性

            if (dm.condition_id.empty())
                continue;
            if (!ExtractClobTokenIds(mobj, dm.token0_id, dm.token1_id))
                continue;
            if (dm.token0_id.empty() || dm.token1_id.empty())
                continue;
            // 2026-06-02 老板「和官方对齐」: 不再发现层剔除 completed/死盘 —— 全部 active 盘都发现,
            //   与 Polymarket 一致。已完赛盘由前端 game_state 徽章(已结束/已结算)标清 + 「隐藏无簿」管显示。

            // A0 映射桥锚定字段 (best-effort; 缺失不阻塞发现, 仅降匹配率)
            (void)ExtractOutcomes(mobj, dm.outcome0_name, dm.outcome1_name);
            dm.game_start_ts_sec = ParseGammaTimeToEpochSec(ExtractJsonStr(mobj, "gameStartTime"));
            dm.end_ts_sec = ParseGammaTimeToEpochSec(ExtractJsonStr(mobj, "endDate"));
            dm.fee_rate_coef = ExtractFeeRateCoef(mobj);  // gamma feeSchedule.rate (R-fee-2 真值)

            ev.markets.push_back(std::move(dm));
        }

        if (ev.markets.empty())
            continue;
        result.push_back(std::move(ev));
    }

    return result;
}

std::vector<DiscoveredEvent> ParseSportsMarketsFlat(const std::string& json_buf, int max_markets) {
    using namespace discovery_detail;
    std::vector<DiscoveredEvent> result;
    if (json_buf.empty())
        return result;

    static const char* kSportsKw[] = {
        "nba",       "nfl",       "mlb",        "nhl",        "ncaa",         "mls",
        "soccer",    "football",  "basketball", "baseball",   "hockey",       "premier league",
        "champions", "world cup", "euro",       "tennis",     "wimbledon",    "cricket",
        "ufc",       "mma",       "stanley",    "super bowl", "world series", nullptr};

    int count = 0;
    std::size_t pos = 0;
    while (pos < json_buf.size() && count < max_markets) {
        auto [ms, me] = ExtractNextObject(json_buf, pos);
        if (ms == std::string::npos)
            break;
        std::string mobj = json_buf.substr(ms, me - ms + 1);
        pos = me + 1;

        const std::string question = ExtractJsonStr(mobj, "question");
        const std::string ql = [&]() {
            std::string s = question;
            for (char& c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }();
        bool is_sports = false;
        for (int k = 0; kSportsKw[k] != nullptr; ++k) {
            if (ql.find(kSportsKw[k]) != std::string::npos) {
                is_sports = true;
                break;
            }
        }
        if (!is_sports)
            continue;

        DiscoveredMarket dm;
        dm.condition_id = ExtractJsonStr(mobj, "conditionId");
        dm.question = question;
        dm.group_item_title = ExtractJsonStr(mobj, "groupItemTitle");
        dm.sports_market_type = NormalizeSportsMarketType(ExtractJsonStr(mobj, "sportsMarketType"));
        dm.line = ExtractJsonNumIn(mobj, "line", 0, mobj.size(),
                                   std::numeric_limits<double>::quiet_NaN());  // totals/spreads 线值
        const double knan2 = std::numeric_limits<double>::quiet_NaN();
        dm.volume_24h = ExtractJsonNumIn(mobj, "volume24hr", 0, mobj.size(), knan2);
        dm.liquidity = ExtractJsonNumIn(mobj, "liquidity", 0, mobj.size(), knan2);

        if (dm.condition_id.empty())
            continue;
        if (!ExtractClobTokenIds(mobj, dm.token0_id, dm.token1_id))
            continue;
        if (dm.token0_id.empty() || dm.token1_id.empty())
            continue;
        // 2026-06-02 老板「和官方对齐」: 不再剔除 completed/死盘, 全 active 盘发现 (同 Polymarket)。

        // A0 映射桥锚定字段 (best-effort)
        (void)ExtractOutcomes(mobj, dm.outcome0_name, dm.outcome1_name);
        dm.game_start_ts_sec = ParseGammaTimeToEpochSec(ExtractJsonStr(mobj, "gameStartTime"));
        dm.end_ts_sec = ParseGammaTimeToEpochSec(ExtractJsonStr(mobj, "endDate"));
        dm.fee_rate_coef = ExtractFeeRateCoef(mobj);  // gamma feeSchedule.rate (R-fee-2 真值)

        // Wrap in synthetic event (event_id = condition_id, slug/title = question)
        DiscoveredEvent ev;
        ev.event_id = dm.condition_id;
        ev.slug = ExtractJsonStr(mobj, "slug");
        ev.title = question;
        ev.icon_url = ExtractJsonStr(mobj, "icon");  // 赛事图 (前端事件头)
        if (ev.icon_url.empty()) ev.icon_url = ExtractJsonStr(mobj, "image");
        const SportObj so2 = ExtractSportObject(mobj);
        ev.sport_code = so2.code;
        ev.sport_id = so2.id;
        ev.sport = !so2.code.empty() ? so2.code : ExtractJsonStr(mobj, "sport");
        ev.neg_risk_market_id = ExtractJsonStr(mobj, "negRiskMarketID");
        ev.markets.push_back(std::move(dm));
        result.push_back(std::move(ev));
        ++count;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Fetch 层 — popen curl (IO, 非热路径, 启动一次)
// ---------------------------------------------------------------------------

namespace {

// 公共 popen curl helper: GET url → 原始响应字符串 (失败返 "").
std::string CurlGet(const std::string& url) {
    const std::string cmd = "curl -s --max-time 15 \"" + url + "\" 2>/dev/null";
    FILE* fp = ::popen(cmd.c_str(), "r");
    if (!fp) {
        std::fprintf(stderr, "[live_discover] ERROR: popen curl failed\n");
        return "";
    }
    std::string json_buf;
    char chunk[8192];
    while (std::fgets(chunk, sizeof(chunk), fp)) {
        json_buf.append(chunk);
    }
    ::pclose(fp);
    return json_buf;
}

}  // namespace

std::string FetchGammaEvents(int offset) {
    // tag_id=1 = Sports 标签 (纯体育池, laoli SSOT §6); ascending=false 让近期/未来
    // 开赛的单场比赛优先 (含 moneyline/totals/spreads), 而非赛季级 outright 夺冠盘.
    // 实测: ascending=true 首批全是 outright (sportsMarketType 空); ascending=false
    // 首批含 moneyline 71 / totals 111 / spreads 56 → 盘口识别率真实 > 0.
    // offset 分页 (老板 2026-06-01「不要限制, 搞大, 验证期」): DiscoverSportsEvents 逐页扫到空。
    const std::string url =
        "https://gamma-api.polymarket.com/events"
        "?tag_id=1&closed=false&active=true&limit=100&order=startDate&ascending=false"
        "&offset=" + std::to_string(offset);
    std::fprintf(stderr, "[live_discover] GET %s\n", url.c_str());
    std::string json_buf = CurlGet(url);
    if (json_buf.empty() && offset == 0) {
        std::fprintf(stderr, "[live_discover] ERROR: empty response from gamma /events\n");
    }
    return json_buf;
}

// 发现「正在打」的单场 (gamma event.live=true). 这是 in-play 交易的命门:
//   默认 FetchGammaEvents 按 startDate 倒序, 而 live 单场的 event.startDate = 上架日 (陈旧, 可早一个月),
//   被排到列表最底永远捞不到 → EventMatcher 0/N 匹配 → game 特征全空 → 不产 fair → 0 成交 (根因实证).
//   live=true 精确返回 gameStartTime≈now 的在打比赛 (Cruzeiro/Cubs/...), 各盘口齐 (moneyline/spreads/totals),
//   且正是 Goalserve in-play 比分覆盖的那批 → 队名+kickoff 都对得上, 是「盘口↔直播员对接」的正确数据源.
//   ToS: 只读公开 gamma REST, 不下单.
std::string FetchGammaLiveEvents() {
    const std::string url =
        "https://gamma-api.polymarket.com/events"
        "?tag_id=1&closed=false&active=true&live=true&limit=100";
    std::fprintf(stderr, "[live_discover] GET %s\n", url.c_str());
    return CurlGet(url);
}

std::string FetchGammaMarketsFlat() {
    const std::string url =
        "https://gamma-api.polymarket.com/markets"
        "?closed=false&active=true&limit=50";
    std::fprintf(stderr, "[live_discover] fallback GET %s\n", url.c_str());
    return CurlGet(url);
}

// ---------------------------------------------------------------------------
// Discover 层 — Fetch + Parse 便捷封装
// ---------------------------------------------------------------------------

std::vector<DiscoveredEvent> DiscoverSportsEvents(int max_events) {
    // ⚠ 2026-06-01 修正 (老板「不比官方少就行」+ 对照 polymarket.com/sports/live 审查):
    //   旧实现依赖 gamma `event.live` 布尔 → **实测该字段恒为 None/缺失**, gamma 不用它标记在打比赛。
    //   故旧 FetchGammaLiveEvents (`&live=true`) 恒返 0; 且只有那批被 set e.live=true → 从广查询来的
    //   真·在打比赛被标 e.live=false → 前端 live 过滤把它们藏掉 → 「比官方少」。根因实证。
    //   正解: live 由 **market.gameStartTime <= now** 判 (单场真实开赛 ts), 对所有 event 统一计算;
    //   且不再按「开赛>1h 丢弃」截断 (那会让 upcoming 比官方少) — 改为保留全部单场盘, 由 max_events 兜底。
    const std::int64_t now_sec = static_cast<std::int64_t>(std::time(nullptr));

    // 订阅范围: 只留【已开赛 in-progress】+【即将开赛 ≤1h】; 剔除 远期(>1h)/已结束/outright(无开赛 ts)。
    constexpr std::int64_t kPreKickoffWindowSec = 3600;  // 开赛前 ≤1h 起订阅 (imminent)
    // 开赛后窗口 (2026-06-02 老板「有了结束退订机制, 窗口应放长更安全」): 3.5h→8h。
    //   角色转变: 真正结束的盘由 Goalserve final 检测「及时」退订(不靠窗口); 此窗口仅作"安全上限",
    //   避免误杀还在打的长盘 —— 5 盘网球/棒球加时 ~4-5h、cricket ODI ~8h、电竞 BO5 ~3-4h, 3.5h 会
    //   把它们当结束踢掉(发现窗口在匹配前过滤, 匹配上的长盘也误踢)。8h 覆盖最长 ODI。代价: 未匹配
    //   又打完的盘最长挂 8h(不可交易, hub 已扩 2048 有空间), 可接受。
    constexpr std::int64_t kLiveWindowSec = 8 * 3600;  // 8h 安全上限
    // 单 event 是否在订阅窗口 (in-progress 或 imminent ≤1h, 未结束)。pagination early-stop + 终筛共用。
    auto in_window = [now_sec](const DiscoveredEvent& e, std::int64_t& earliest_kickoff_out) -> bool {
        std::int64_t earliest_kickoff = 0, latest_end = 0;
        for (const auto& m : e.markets) {
            if (m.game_start_ts_sec > 0 &&
                (earliest_kickoff == 0 || m.game_start_ts_sec < earliest_kickoff))
                earliest_kickoff = m.game_start_ts_sec;
            if (m.end_ts_sec > latest_end) latest_end = m.end_ts_sec;
        }
        earliest_kickoff_out = earliest_kickoff;
        if (earliest_kickoff == 0) return false;                                  // outright/无开赛 ts
        if (earliest_kickoff > now_sec + kPreKickoffWindowSec) return false;      // 太早 (>1h)
        if (latest_end > 0 && latest_end <= now_sec) return false;               // gamma endDate 已过
        if (now_sec > earliest_kickoff + kLiveWindowSec) return false;           // 开赛超 6h 推定结束
        return true;
    };

    // 2026-06-01 (老板「我们有参数限定扫描呀, 在比赛中的」+ 实测 live+imminent 全在前 ~7 页, 深页全 0):
    //   边扫边筛 + early-stop: 连续 kEmptyPageStop 页无 live/imminent → 停 (深页全远期/已结束, 扫了浪费)。
    //   把扫描从固定 26 页砍到 ~10 页 (随实时分布自适应), 让 discovery 够轻 → 能高频跑 (rediscover 2s)。
    constexpr int kPageSize = 100;
    constexpr int kHardScanCap = 5000;   // 安全护栏 (防 gamma 异常无限翻页)
    // 连续 N 页无 live/imminent → 停。实测在打/即将全在前 ~7 页 + 最大空档仅 1 页; 原设 10 (10× 余量),
    //   2026-06-12 老板「不用十页限制, 50页吧」→ 50 (彻底避免漏掉"深页上架的在打比赛"; 实测完整扫
    //   vs early-stop 0 遗漏, 此为额外保险)。代价: 每轮 rediscover 多扫 ~40 页深页 (gamma 顺序拉,
    //   扫描时长自然托底负载, 见 trader_daemon.hpp rediscover_interval_sec 注)。
    constexpr int kEmptyPageStop = 50;
    std::vector<DiscoveredEvent> kept;
    std::unordered_set<std::string> seen;
    int pages = 0, empty_streak = 0;
    std::size_t raw_scanned = 0, live_count = 0;
    for (int offset = 0; offset < kHardScanCap && static_cast<int>(kept.size()) < max_events;
         offset += kPageSize) {
        std::vector<DiscoveredEvent> page = ParseSportsEvents(FetchGammaEvents(offset), kPageSize);
        ++pages;
        if (page.empty()) break;  // gamma 翻到底
        std::size_t page_new = 0, page_in_window = 0;
        for (auto& e : page) {
            if (!seen.insert(e.event_id).second) continue;  // 跨页去重
            ++page_new;
            ++raw_scanned;
            std::int64_t ek = 0;
            if (in_window(e, ek)) {
                e.live = (ek <= now_sec);  // 已开赛 = 在打 (前端默认过滤 + Goalserve 映射优先级)
                if (e.live) ++live_count;
                ++page_in_window;
                kept.push_back(std::move(e));
                if (static_cast<int>(kept.size()) >= max_events) break;
            }
        }
        if (page_new == 0) break;  // 本页无新 event → gamma 枯竭
        if (page_in_window == 0) {
            if (++empty_streak >= kEmptyPageStop) break;  // 连续 N 页无在打/即将 → 深页全远期, 停
        } else {
            empty_streak = 0;
        }
    }
    std::fprintf(stderr,
                 "[live_discover] 订阅范围 %zu event (扫 %d 页/%zu raw, early-stop; "
                 "在打/live=%zu + imminent≤1h=%zu)\n",
                 kept.size(), pages, raw_scanned, live_count, kept.size() - live_count);
    return kept;
}

std::vector<DiscoveredEvent> DiscoverSportsMarketsFlat(int max_markets) {
    return ParseSportsMarketsFlat(FetchGammaMarketsFlat(), max_markets);
}

}  // namespace stcpp::app
