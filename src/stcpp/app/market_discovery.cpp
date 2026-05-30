// src/stcpp/app/market_discovery.cpp — Gamma 体育市场发现实现
//
// Owner: 老雷 (GM) — 从 debug_server_main.cpp 抽出 (PaperDaemon 重构, 老郭 §A.1)
// last_review: 2026-05-30
//
// 逐字搬迁自 debug_server_main.cpp 匿名 namespace, 解析逻辑行为不变.
// 老周补充: Fetch (popen IO) 与 Parse (纯函数) 拆开, Parse 层喂 fixture 可单测.
// ToS: 只读公开 gamma REST, curl 自动读 HTTPS_PROXY/HTTP_PROXY, 不下单.

#include "stcpp/app/market_discovery.hpp"

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <ctime>

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
        ev.sport = ExtractJsonStr(event_obj, "sport");
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

            if (dm.condition_id.empty())
                continue;
            if (!ExtractClobTokenIds(mobj, dm.token0_id, dm.token1_id))
                continue;
            if (dm.token0_id.empty() || dm.token1_id.empty())
                continue;

            // A0 映射桥锚定字段 (best-effort; 缺失不阻塞发现, 仅降匹配率)
            (void)ExtractOutcomes(mobj, dm.outcome0_name, dm.outcome1_name);
            dm.game_start_ts_sec = ParseGammaTimeToEpochSec(ExtractJsonStr(mobj, "gameStartTime"));

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

        if (dm.condition_id.empty())
            continue;
        if (!ExtractClobTokenIds(mobj, dm.token0_id, dm.token1_id))
            continue;
        if (dm.token0_id.empty() || dm.token1_id.empty())
            continue;

        // A0 映射桥锚定字段 (best-effort)
        (void)ExtractOutcomes(mobj, dm.outcome0_name, dm.outcome1_name);
        dm.game_start_ts_sec = ParseGammaTimeToEpochSec(ExtractJsonStr(mobj, "gameStartTime"));

        // Wrap in synthetic event (event_id = condition_id, slug/title = question)
        DiscoveredEvent ev;
        ev.event_id = dm.condition_id;
        ev.slug = ExtractJsonStr(mobj, "slug");
        ev.title = question;
        ev.sport = ExtractJsonStr(mobj, "sport");
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

std::string FetchGammaEvents() {
    // tag_id=1 = Sports 标签 (纯体育池, laoli SSOT §6); ascending=false 让近期/未来
    // 开赛的单场比赛优先 (含 moneyline/totals/spreads), 而非赛季级 outright 夺冠盘.
    // 实测: ascending=true 首批全是 outright (sportsMarketType 空); ascending=false
    // 首批含 moneyline 71 / totals 111 / spreads 56 → 盘口识别率真实 > 0.
    const std::string url =
        "https://gamma-api.polymarket.com/events"
        "?tag_id=1&closed=false&active=true&limit=100&order=startDate&ascending=false";
    std::fprintf(stderr, "[live_discover] GET %s\n", url.c_str());
    std::string json_buf = CurlGet(url);
    if (json_buf.empty()) {
        std::fprintf(stderr, "[live_discover] ERROR: empty response from gamma /events\n");
    }
    return json_buf;
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
    return ParseSportsEvents(FetchGammaEvents(), max_events);
}

std::vector<DiscoveredEvent> DiscoverSportsMarketsFlat(int max_markets) {
    return ParseSportsMarketsFlat(FetchGammaMarketsFlat(), max_markets);
}

}  // namespace stcpp::app
