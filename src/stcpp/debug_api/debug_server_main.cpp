// src/stcpp/debug_api/debug_server_main.cpp — 观测/调试 API server 可运行入口
// Owner: 小卢 (senior-ic-pool)
// 小冯 (#34) 2026-05-29: live-only 重构 — 只有真实 WSS 模式
//   - 删除 --real/--replay/--empty/--demo 全部模式开关
//   - gamma 发现走 /events (而非 /markets), 结构正确: Event→Market(condition)→Token
//   - 注入 EventInfo 到 RealStateProvider.set_events()
//   - data_source 恒 "live"; positions/pnl/quote 无 paper 源 → 空/未接入
// 小段 (#37) 2026-05-29: Goalserve inplay 常开集成
//   - InplayFeedThread 启动时无条件拉起 (soccer/basketball/tennis)
//   - ScoreSnapshotStore → RealStateProvider.score() 真实比分
//   - 无 --score-live flag: score 数据源与 book 同级, 均为 live
// 关联:
//   server.hpp (HttpServer)
//   real_state_provider.hpp (RealStateProvider — book/events/score 接真)
//   live_wss_transport.hpp  (LiveWssTransport — POSIX+OpenSSL+HTTP CONNECT proxy)
//   live_book_publisher.hpp (LiveBookPublisher — CLOB book JSON → OrderBookFeatures → hub)
//   inplay_feed_thread.hpp  (InplayFeedThread — Goalserve inplay HTTP poll → ScoreSnapshotStore)
//   docs/RESEARCH/laoli-events-ws-mapping-spec-v1.md (Events→Market→Token 字段映射)
//   ADR-038 (观测 API) / R-12 (server 独立线程) / R-20 (4 时间戳契约)
//   frontend/ 观测看板走独立 Vite dev server (localhost:3000), 跨域调此 API
//
// CLI (live-only):
//   stcpp_debug_server [--port N] [--host ADDR] [--verbose]
//     --port N      监听端口 (默认 8080)
//     --host ADDR   绑定地址 (默认 127.0.0.1; ADR-038 §5 安全默认)
//     --verbose     WSS/parser 调试日志
//
// 启动流程:
//   1. gamma /events 发现活跃体育 event → condition_id + clobTokenIds (双 token)
//   2. 构造 OrderBookSnapshotHub + ScoreSnapshotStore + RealStateProvider
//   3. InplayFeedThread 启动 (Goalserve inplay, soccer/basketball/tennis, R-12)
//   4. LiveWssTransport + LiveBookPublisher → 订阅 CLOB market WSS → hub.Publish()
//   5. HttpServer 起在独立线程 (R-12)
//   6. 主线程等待 SIGINT/SIGTERM; 停止时 join 所有后台线程
//
// data_source 恒 "live":
//   book/event: 真实 Polymarket CLOB WSS + gamma /events
//   score: 真实 Goalserve inplay feed (soccer/basketball/tennis)
//   positions/pnl/quote: 无 paper/replay 源 → 空 (前端灰显)
//
// 安全: 默认 127.0.0.1 only; 只读 endpoint; 黑名单字段物理不在 schema 中。
// 模式: build-time STCPP_EXEC_MODE_STR 决定 mode 字段。
// ToS: 只读公开 book channel, 尊重速率限制, 不下单。

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "stcpp/data/inplay_feed_thread.hpp"    // InplayFeedThread (小段 W5, 常开)
#include "stcpp/data/score_snapshot_store.hpp"  // ScoreSnapshotStore (小段)
#include "stcpp/risk/ledger_snapshot_hub.hpp"   // LedgerSnapshotHub
#include "stcpp/sizing/quote_snapshot_hub.hpp"  // QuoteSnapshotHub

// feat/xiaoxiao-paper-loop: 最小 paper 交易循环 (2026-05-30)
// 消费真实 live book → paper 成交 → LedgerSnapshotHub/QuoteSnapshotHub
// R-11: paper 不污染真账本; R-12: 独立线程; R-20: 4 ts 透传
// ToS: 仅 paper 虚拟成交, 不向 Polymarket CLOB 下单
#include "stcpp/paper/paper_loop.hpp"              // PaperLoop (小肖)
#include "stcpp/pricing/fair_value_estimator.hpp"  // BaselineFairValueModel
#include "stcpp/risk/position_ledger.hpp"          // PositionLedger (paper 专用)
#include "stcpp/risk/rm_debug_snapshot.hpp"        // RmDebugSnapshot + attach/detach

#include "src/stcpp/debug_api/live_book_publisher.hpp"  // CLOB book → hub
#include "src/stcpp/debug_api/live_wss_transport.hpp"   // 真实 WSS transport
#include "src/stcpp/debug_api/real_state_provider.hpp"
#include "src/stcpp/debug_api/server.hpp"
#include "src/stcpp/debug_api/state_provider.hpp"

namespace {

std::atomic<bool> g_stop{false};

void handle_sigint(int /*sig*/) {
    g_stop.store(true, std::memory_order_release);
}

stcpp::debug_api::ExecMode mode_from_build() noexcept {
    using stcpp::debug_api::ExecMode;
    if (std::strcmp(STCPP_EXEC_MODE_STR, "live") == 0) {
        return ExecMode::Live;
    }
    if (std::strcmp(STCPP_EXEC_MODE_STR, "backtest") == 0) {
        return ExecMode::Backtest;
    }
    return ExecMode::Paper;
}

// ---------------------------------------------------------------------------
// Gamma /events discovery helpers
//
// 走 /events (而非 /markets): 正确的 Polymarket 体育层次结构是
//   Event → markets[] → conditionId + clobTokenIds[2]
// GM 实测确认: 发现走 /events 接口.
//
// curl via popen: 非热路径, 启动时调用一次.
// 代理: curl 自动读 HTTPS_PROXY/HTTP_PROXY 环境变量.
// ---------------------------------------------------------------------------

// 最小 JSON string 提取 (no malloc, in-place parse)
static std::string ExtractJsonStr(const std::string& json, const std::string& key) {
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

// Extract clobTokenIds — handles TWO gamma API encodings:
//   Native array:  "clobTokenIds":["tok0","tok1"]
//   JSON string:   "clobTokenIds":"[\"tok0\",\"tok1\"]"  (gamma /events encodes as string)
// Returns true and fills tok0/tok1 if at least 2 tokens found.
static bool ExtractClobTokenIds(const std::string& obj, std::string& tok0, std::string& tok1) {
    const std::string needle = "\"clobTokenIds\":";
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
    tok0 = tokens[0];
    tok1 = tokens[1];
    return true;
}

// Extract sportsMarketType → normalize to moneyline/spread/totals/outright/prop/series/unknown
static std::string NormalizeSportsMarketType(const std::string& raw) {
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

// Per-market entry discovered from gamma /events
struct DiscoveredMarket {
    std::string condition_id;
    std::string question;
    std::string group_item_title;
    std::string sports_market_type;
    std::string token0_id;
    std::string token1_id;
};

// Per-event entry
struct DiscoveredEvent {
    std::string event_id;
    std::string slug;
    std::string title;
    std::string sport;
    std::string neg_risk_market_id;
    std::vector<DiscoveredMarket> markets;
};

// Extract next balanced { } object from s starting at pos
// Returns {start, end} or {npos, npos}
static std::pair<std::size_t, std::size_t> ExtractNextObject(const std::string& s, std::size_t pos) {
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
static std::string ExtractMarketsArray(const std::string& event_obj) {
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

// gamma /events REST discovery
static std::vector<DiscoveredEvent> DiscoverSportsEvents(int max_events = 5) {
    std::vector<DiscoveredEvent> result;

    const std::string url =
        "https://gamma-api.polymarket.com/events"
        "?closed=false&active=true&limit=20";
    const std::string cmd = "curl -s --max-time 15 \"" + url + "\" 2>/dev/null";

    std::fprintf(stderr, "[live_discover] GET %s\n", url.c_str());

    FILE* fp = ::popen(cmd.c_str(), "r");
    if (!fp) {
        std::fprintf(stderr, "[live_discover] ERROR: popen curl failed\n");
        return result;
    }

    std::string json_buf;
    char chunk[8192];
    while (std::fgets(chunk, sizeof(chunk), fp)) {
        json_buf.append(chunk);
    }
    ::pclose(fp);

    if (json_buf.empty()) {
        std::fprintf(stderr, "[live_discover] ERROR: empty response from gamma /events\n");
        return result;
    }

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

            ev.markets.push_back(std::move(dm));
        }

        if (ev.markets.empty())
            continue;
        result.push_back(std::move(ev));
    }

    return result;
}

// Fallback: gamma /markets flat discovery (when /events has no sports)
// Groups flat markets into synthetic DiscoveredEvent objects (one event per market).
// Used when current gamma /events returns no sports content.
static std::vector<DiscoveredEvent> DiscoverSportsMarketsFlat(int max_markets = 10) {
    std::vector<DiscoveredEvent> result;

    static const char* kSportsKw[] = {
        "nba",       "nfl",       "mlb",        "nhl",        "ncaa",         "mls",
        "soccer",    "football",  "basketball", "baseball",   "hockey",       "premier league",
        "champions", "world cup", "euro",       "tennis",     "wimbledon",    "cricket",
        "ufc",       "mma",       "stanley",    "super bowl", "world series", nullptr};

    const std::string url =
        "https://gamma-api.polymarket.com/markets"
        "?closed=false&active=true&limit=50";
    const std::string cmd = "curl -s --max-time 15 \"" + url + "\" 2>/dev/null";
    std::fprintf(stderr, "[live_discover] fallback GET %s\n", url.c_str());

    FILE* fp = ::popen(cmd.c_str(), "r");
    if (!fp)
        return result;
    std::string json_buf;
    char chunk[8192];
    while (std::fgets(chunk, sizeof(chunk), fp))
        json_buf.append(chunk);
    ::pclose(fp);
    if (json_buf.empty())
        return result;

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

}  // namespace

int main(int argc, char** argv) {
    using namespace stcpp::debug_api;

    std::uint16_t port = 8080;
    std::string host = "127.0.0.1";
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) {
            port = static_cast<std::uint16_t>(std::stoi(argv[++i]));
        } else if (a == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (a == "--verbose" || a == "-v") {
            verbose = true;
        } else if (a == "--help" || a == "-h") {
            std::printf(
                "usage: stcpp_debug_server [--port N] [--host ADDR] [--verbose]\n"
                "  --port N      listen port (default 8080)\n"
                "  --host ADDR   bind address (default 127.0.0.1)\n"
                "  --verbose     extra WSS/parser debug logging\n"
                "\n"
                "live (always on, no mode flags):\n"
                "  Polymarket book feed:\n"
                "    gamma /events discovery -> active sports Event/Market/Token\n"
                "    subscribe CLOB WSS market channel (book + price_change)\n"
                "    feed true book data into OrderBookSnapshotHub\n"
                "    R-12: WSS io_thread_ independent, hub.Publish() atomic\n"
                "    R-20: data_source_ts=CLOB timestamp(ms)*1e6 (UPSTREAM_PAYLOAD)\n"
                "    ToS: read-only public market data, no order placement\n"
                "  Goalserve score feed:\n"
                "    InplayFeedThread: HTTP poll inplay.goalserve.com (soccer/basketball/tennis)\n"
                "    -> ScoreSnapshotStore -> /api/v1/score/<event_id>\n"
                "    R-12: independent per-sport thread, non-blocking Publish\n"
                "    R-20: data_source_ts=updated_ts(ms)*1e6, event_ts=start_ts(s)*1e9\n"
                "  Reads HTTPS_PROXY/HTTP_PROXY env vars for proxy config.\n"
                "  (frontend served by Vite dev server, not this process)\n");
            return 0;
        } else {
            std::fprintf(stderr, "[debug_server] unknown arg: %s (try --help)\n", a.c_str());
            return 2;
        }
    }

    const ExecMode mode = mode_from_build();

    // -------------------------------------------------------------------------
    // Step 1: gamma /events discovery
    // -------------------------------------------------------------------------
    std::printf("[debug_server] live: gamma /events 发现活跃体育市场...\n");
    std::fflush(stdout);

    auto discovered = DiscoverSportsEvents(/*max_events=*/5);

    // Fallback: if /events returned no sports, try /markets flat (NHL futures etc.)
    if (discovered.empty()) {
        std::printf("[debug_server] /events 无体育 event, 回退 /markets 平铺发现...\n");
        std::fflush(stdout);
        discovered = DiscoverSportsMarketsFlat(/*max_markets=*/10);
    }

    MarketTokenMap token_map;
    MarketInfoMap market_catalog;  // P1-1: condition_id → MarketInfo 目录
    std::vector<EventInfo> event_infos;

    if (discovered.empty()) {
        std::fprintf(stderr,
                     "[debug_server] WARNING: gamma /events + /markets 均无体育市场, "
                     "hub 将保持空状态 → book 端点返回 found=false.\n");
    } else {
        std::printf("[debug_server] 发现 %zu 个体育 event:\n", discovered.size());
        for (const auto& ev : discovered) {
            std::printf("[debug_server]  event: %.40s | slug=%.30s | sport=%s\n", ev.title.c_str(),
                        ev.slug.c_str(), ev.sport.c_str());

            EventInfo ei;
            ei.event_id = ev.event_id;
            ei.slug = ev.slug;
            ei.title = ev.title;
            ei.sport = ev.sport;
            ei.neg_risk_market_id = ev.neg_risk_market_id;

            for (const auto& dm : ev.markets) {
                std::printf("[debug_server]    market %.28s... | type=%s | gi=%s\n", dm.condition_id.c_str(),
                            dm.sports_market_type.c_str(), dm.group_item_title.c_str());
                std::printf("[debug_server]      tok0: %.30s...\n", dm.token0_id.c_str());
                std::printf("[debug_server]      tok1: %.30s...\n", dm.token1_id.c_str());
                token_map[dm.condition_id] = {dm.token0_id, dm.token1_id};
                ei.condition_ids.push_back(dm.condition_id);

                // P1-1: 填充 MarketInfo catalog (gamma 发现的真实元信息)
                MarketInfo mi;
                mi.found = true;
                mi.condition_id = dm.condition_id;
                mi.market_id = dm.condition_id;  // deprecated alias
                mi.tick_size = 0.01;             // Polymarket 默认 tick (gamma 字段缺省时用此值)
                mi.fee_rate = 0.0;               // outright/futures: 无 maker fee
                mi.neg_risk = !ev.neg_risk_market_id.empty();
                mi.neg_risk_market_id = ev.neg_risk_market_id;
                mi.accepting_orders = true;  // gamma active=true 时默认接单
                mi.active = true;
                mi.closed = false;
                mi.resolved = false;
                mi.source = "polymarket";
                mi.event_id = ev.event_id;
                mi.slug = ev.slug;
                mi.polymarket_url = ev.slug.empty() ? "" : ("https://polymarket.com/event/" + ev.slug);
                mi.sports_market_type = dm.sports_market_type;
                mi.group_item_title = dm.group_item_title;
                // tokens[]: YES (tok0) + NO (tok1)
                TokenInfo tk0;
                tk0.token_id = dm.token0_id;
                tk0.outcome = "Yes";
                tk0.price = 0.0;  // 实时价从 book hub 读; MarketInfo 仅存元数据
                tk0.winner = false;
                TokenInfo tk1;
                tk1.token_id = dm.token1_id;
                tk1.outcome = "No";
                tk1.price = 0.0;
                tk1.winner = false;
                mi.tokens.push_back(std::move(tk0));
                mi.tokens.push_back(std::move(tk1));

                market_catalog[dm.condition_id] = std::move(mi);
            }
            event_infos.push_back(std::move(ei));
        }
        std::printf("[debug_server] P1-1: MarketInfo catalog 已填充 %zu 条目\n", market_catalog.size());
    }

    // Collect all token_ids for WSS subscription
    std::vector<std::string> all_token_ids;
    all_token_ids.reserve(token_map.size() * 2);
    for (const auto& [cond_id, tok_pair] : token_map) {
        all_token_ids.push_back(tok_pair.first);
        all_token_ids.push_back(tok_pair.second);
    }

    // -------------------------------------------------------------------------
    // Step 2: construct hub + ScoreSnapshotStore + RealStateProvider
    // -------------------------------------------------------------------------
    auto hub_owned = std::make_unique<stcpp::polymarket::clob_wss::OrderBookSnapshotHub>();
    auto score_store_owned = std::make_unique<stcpp::data::ScoreSnapshotStore>();
    auto ledger_hub_owned = std::make_unique<stcpp::risk::LedgerSnapshotHub>();
    auto quote_hub_owned = std::make_unique<stcpp::sizing::QuoteSnapshotHub>();

    // ---- feat/xiaoxiao-paper-loop: paper 交易循环依赖对象 (2026-05-30) ----
    // R-11: paper PositionLedger 独立实例, 与 live 路径物理隔离
    // R-11: RmDebugSnapshot 注入 attach_rm_debug_snapshot, 供 RM 内部 push_reject
    auto paper_position_ledger = std::make_unique<stcpp::risk::PositionLedger>();
    auto paper_rm_snap = std::make_unique<stcpp::risk::RmDebugSnapshot>();
    stcpp::risk::attach_rm_debug_snapshot(paper_rm_snap.get());

    // paper RiskGateway (paper 专用; 与 live RM 隔离; 无 WAL emitter — M1 audit 落简化)
    // M1: 使用 InMemory null emitter (不落 WAL); 后续 M2 接 WalWriter<PaperAudit>
    class NullAuditEmitter final : public stcpp::risk::AuditEmitter {
    public:
        bool emit(stcpp::risk::AuditRecord const& /*rec*/) noexcept override { return true; }
    };
    auto paper_audit_emitter = std::make_shared<NullAuditEmitter>();
    stcpp::risk::RiskConfig paper_rm_cfg;  // 默认 cap (per_order=10K, bankroll=100K)
    // paper_rm_cfg: 降低阈值以便 paper demo 产生成交 (M1 调试)
    paper_rm_cfg.per_order_cap_usdc = 10;        // 10 pUSD demo cap
    paper_rm_cfg.market_exposure_cap_usdc = 50;  // 50 pUSD
    paper_rm_cfg.per_outcome_cap_usdc = 25;      // 25 pUSD
    paper_rm_cfg.bankroll_usdc = 1000;           // 1K pUSD demo bankroll
    paper_rm_cfg.edge_ci_lower_floor = -1.0;     // M1 放宽 CI 门 (所有 edge 均放行)
    paper_rm_cfg.enable_moneyline = true;
    // R-12: recon freshness 设置极大 (不触发 STALE_DATA; M1 无 recon 数据源)
    auto paper_rm = std::make_unique<stcpp::risk::RiskGateway>(paper_rm_cfg, paper_audit_emitter);

    // BaselineFairValueModel (小肖 pricing v0.1; 先验 sigmoid)
    stcpp::pricing::ScorePriorParams fv_params{0.30, 0.50};
    auto paper_fv_model = std::make_unique<stcpp::pricing::BaselineFairValueModel>(fv_params, 0.20);

    // PaperLoopConfig
    stcpp::paper::PaperLoopConfig paper_loop_cfg;
    paper_loop_cfg.tick_interval_ms = 500;  // 500ms 一次 tick (调试友好)
    paper_loop_cfg.bankroll_usdc = 1000.0;  // 1K pUSD demo
    paper_loop_cfg.n_effective = 30;
    paper_loop_cfg.z_90 = 1.645;
    paper_loop_cfg.strategy_id = "paper-demo-v1";
    paper_loop_cfg.set_rm_running = true;

    // 构造 PaperLoop (注入所有依赖)
    // R-12: PaperLoop 内部为 std::jthread, 不进 WSS event loop
    // R-11: paper_position_ledger 与 live 路径物理隔离
    // ToS: 仅 paper 虚拟成交, 不向 Polymarket CLOB 下单
    auto paper_loop = std::make_unique<stcpp::paper::PaperLoop>(
        *hub_owned, *paper_rm, *paper_position_ledger, *ledger_hub_owned, *quote_hub_owned,
        paper_rm_snap.get(), *paper_fv_model,
        token_map,  // condition_id → (token0_id, token1_id)
        paper_loop_cfg);
    // ---- paper loop 对象构造完成; Start() 在 WSS 建立后调用 (Step 4b) ----

    stcpp::risk::RiskConfig risk_cfg;

    auto real_provider = std::make_unique<RealStateProvider>(*hub_owned,
                                                             /*snap=*/paper_rm_snap.get(),
                                                             /*score_store=*/score_store_owned.get(),
                                                             /*token_map=*/token_map, risk_cfg, mode,
                                                             /*ledger_hub=*/ledger_hub_owned.get(),
                                                             /*quote_hub=*/quote_hub_owned.get());

    // Inject EventInfo (G-FREEZE-W 只增: set_events 非热路径, 启动时调用一次)
    real_provider->set_events(std::move(event_infos));

    // P1-1: 注入 MarketInfo catalog (gamma 发现结果, 启动时注入一次, 只读)
    real_provider->set_market_catalog(std::move(market_catalog));

    // P1-2/P1-3: LiveMetricsHooks 注入 (uptime/rm_reject/fill/staleness/wss)
    // live_transport 在 Step 4 构建, 此处先用空 hooks; Step 4 后更新 (re-inject)
    // 注: start_tp 记录此刻 (server 构建前, 与 HttpServer::start_time_ 同量级)
    LiveMetricsHooks metrics_hooks;
    metrics_hooks.start_tp = std::chrono::steady_clock::now();
    metrics_hooks.fill_counter = &paper_loop->stats().fills_completed;
    // wss_transport 在 Step 4 填充; 见下面 re-inject
    real_provider->set_live_metrics_hooks(metrics_hooks);

    // -------------------------------------------------------------------------
    // Step 3: InplayFeedThread — Goalserve score feed (常开, R-12 合规)
    //
    // 无条件启动: score 与 book 同为 live 数据源, 无 flag 控制.
    // R-12: 独立 per-sport 线程, 绝不阻塞 WSS event loop.
    // R-20: data_source_ts=updated_ts(ms)*1e6, event_ts=start_ts(s)*1e9.
    // -------------------------------------------------------------------------
    stcpp::data::InplayFeedConfig feed_cfg;
    feed_cfg.sports = {
        stcpp::data::goalserve::GoalserveSport::Soccer,
        stcpp::data::goalserve::GoalserveSport::Basketball,
        stcpp::data::goalserve::GoalserveSport::Tennis,
    };
    auto inplay_feed = std::make_unique<stcpp::data::InplayFeedThread>(*score_store_owned, feed_cfg);
    inplay_feed->Start();
    std::printf("[debug_server] Goalserve InplayFeedThread 启动 (soccer/basketball/tennis, R-12)\n");
    std::fflush(stdout);

    // -------------------------------------------------------------------------
    // Step 4: LiveWssTransport + LiveBookPublisher
    // -------------------------------------------------------------------------
    std::unique_ptr<LiveWssTransport> live_transport;
    std::unique_ptr<LiveBookPublisher> live_publisher;

    if (!all_token_ids.empty()) {
        live_publisher = std::make_unique<LiveBookPublisher>(*hub_owned, all_token_ids, verbose);
        live_transport = std::make_unique<LiveWssTransport>(verbose);

        // P1-2/P1-3 re-inject: live_transport 已构建, 更新 hooks 中的 wss_transport 指针
        // set_live_metrics_hooks 是值拷贝 (LiveMetricsHooks 轻量结构), 直接覆盖
        metrics_hooks.wss_transport = live_transport.get();
        real_provider->set_live_metrics_hooks(metrics_hooks);

        // on_text_frame_ → LiveBookPublisher::OnFrame (同步, < 100us, R-12)
        live_transport->SetOnTextFrame(
            [&pub = *live_publisher](std::string_view payload, std::int64_t recv_ts) {
                pub.OnFrame(payload, recv_ts);
            });

        live_transport->SetOnConnected([&all_token_ids, &transport = *live_transport]() {
            std::printf("[debug_server] WSS CONNECTED, 订阅 %zu tokens...\n", all_token_ids.size());
            std::fflush(stdout);
            // CLOB market channel subscribe: {"type":"Market","assets_ids":[...]}
            // GM 实测: type="Market" (大写 M), field name "assets_ids"
            std::string sub = R"({"type":"Market","assets_ids":[)";
            bool first = true;
            for (const auto& tid : all_token_ids) {
                if (!first)
                    sub.push_back(',');
                sub.push_back('"');
                sub.append(tid);
                sub.push_back('"');
                first = false;
            }
            sub.append("]}");
            transport.AsyncSendText(sub);
        });

        live_transport->SetOnDisconnected([](std::string_view reason) {
            std::fprintf(stderr, "[debug_server] WSS DISCONNECTED: %s\n", std::string(reason).c_str());
            std::fflush(stderr);
        });

        const std::string wss_url = "wss://ws-subscriptions-clob.polymarket.com/ws/market";
        std::printf("[debug_server] 连接 %s ...\n", wss_url.c_str());
        std::fflush(stdout);
        live_transport->AsyncConnect(wss_url);
        std::printf("[debug_server] WSS io_thread_ 已启动, 等待 book 数据 (通常 1-5s)...\n");
        std::fflush(stdout);
    } else {
        std::fprintf(stderr,
                     "[debug_server] WARNING: 无 token 可订阅 (发现失败), "
                     "WSS 未启动, book 回落 found=false.\n");
    }

    // -------------------------------------------------------------------------
    // Step 4b: PaperLoop — 启动 paper 交易循环 (feat/xiaoxiao-paper-loop)
    //
    // 在 WSS io_thread_ 启动后再启动 PaperLoop, 确保 hub_ 已就位.
    // R-12: PaperLoop 独立线程 (std::jthread), 不进 WSS event loop
    // R-11: paper_position_ledger 与 live PositionLedger 物理隔离
    // R-20: 4 ts 来自 hub 快照 (真实 Polymarket CLOB WSS 时间戳)
    // ToS: 仅 paper 虚拟成交 (VirtualFill), 不向 Polymarket CLOB 下单
    // -------------------------------------------------------------------------
    std::printf("[debug_server] 启动 paper 交易循环 (PaperLoop, 独立线程, 500ms tick)...\n");
    std::printf("[debug_server] [paper] R-11 隔离: PositionLedger 独立实例 (非 live 账本)\n");
    std::printf("[debug_server] [paper] R-20 透传: data_source_ts_ns 来自 Polymarket CLOB hub 快照\n");
    std::printf("[debug_server] [paper] ToS: 仅 VirtualFill, 不向 CLOB 下单\n");
    std::fflush(stdout);
    paper_loop->Start();

    // -------------------------------------------------------------------------
    // Step 5: HttpServer (R-12: 独立 server_thread_)
    // -------------------------------------------------------------------------
    HttpServer server{port, real_provider.get(), host.c_str()};
    server.start();

    if (!server.is_running()) {
        std::fprintf(stderr, "[debug_server] FATAL: 无法在 %s:%u 启动 (端口被占用?)\n", host.c_str(),
                     static_cast<unsigned>(port));
        return 1;
    }

    std::signal(SIGINT, handle_sigint);
    std::signal(SIGTERM, handle_sigint);

    std::printf("[debug_server] 观测/调试 API @ http://%s:%u  (mode=%s, data=live)\n", host.c_str(),
                static_cast<unsigned>(port), STCPP_EXEC_MODE_STR);
    std::printf("[debug_server] live: book/event=真实 Polymarket CLOB WSS + gamma /events\n");
    std::printf("[debug_server]       score=真实 Goalserve inplay feed (soccer/basketball/tennis)\n");
    std::printf("[debug_server]       positions/pnl/quote: paper 交易循环驱动 (PaperLoop, 500ms tick)\n");
    std::printf("[debug_server] endpoints: /healthz /version /status /metrics\n");
    std::printf("[debug_server]            /api/v1/events\n");
    std::printf("[debug_server]            /api/v1/{market/<cond>,book/<cond>}\n");
    std::printf(
        "[debug_server]            /api/v1/{positions,pnl/*,risk/rejects,"
        "gate/paper,quote/<cond>,score/<event>}\n");
    std::printf("[debug_server] (前端走独立 Vite dev server, 跨域 CORS 已开放)\n");
    std::printf("[debug_server] Ctrl-C 停止\n");
    std::fflush(stdout);

    // -------------------------------------------------------------------------
    // Step 6: main loop
    // -------------------------------------------------------------------------
    while (!g_stop.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::printf("\n[debug_server] 收到停止信号, 关闭...\n");
    server.stop();

    // 停止 PaperLoop (先于 hub / ledger_hub / rm 析构; Stop() 内含 jthread join)
    std::printf("[debug_server] 停止 PaperLoop...\n");
    paper_loop->Stop();
    std::printf("[debug_server] PaperLoop 已停止 (ticks=%llu approved=%llu fills=%llu)\n",
                static_cast<unsigned long long>(paper_loop->stats().ticks_total.load()),
                static_cast<unsigned long long>(paper_loop->stats().orders_approved.load()),
                static_cast<unsigned long long>(paper_loop->stats().fills_completed.load()));

    // R-11: 注销 RmDebugSnapshot 全局 hook (在 paper_rm_snap 析构前)
    stcpp::risk::detach_rm_debug_snapshot();

    // 停止 InplayFeedThread (先于 score_store 析构; Stop() 内含 join)
    std::printf("[debug_server] 停止 InplayFeedThread...\n");
    inplay_feed->Stop();
    std::printf("[debug_server] InplayFeedThread 已停止\n");

    // 停止 LiveWssTransport (Close() 内含 join io_thread_ + send_thread_)
    if (live_transport) {
        std::printf("[debug_server] 停止 LiveWssTransport...\n");
        live_transport->Close();
        if (live_publisher) {
            std::printf(
                "[debug_server] LiveBookPublisher 统计: "
                "frames_received=%llu, books_published=%llu, frames_dropped=%llu\n",
                static_cast<unsigned long long>(live_publisher->frames_received()),
                static_cast<unsigned long long>(live_publisher->books_published()),
                static_cast<unsigned long long>(live_publisher->frames_dropped()));
        }
        std::printf("[debug_server] LiveWssTransport 已停止\n");
    }

    std::printf("[debug_server] 已停止\n");
    return 0;
}
