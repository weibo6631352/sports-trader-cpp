// stcpp/data/goalserve_stub.cpp — GoalserveClient W4 stub 实现
//
// Owner: 小段 (goalserve-specialist)
// Sprint-2 W4 Wave 19 — header-only stub. 不真打 HTTP. W5 替换为 cpp-httplib.
//
// 落:
//   include/stcpp/data/goalserve_client.hpp 主接口
//   R-12: 此 stub 同步直接返回, W5 真接时 client 上移到 vCPU3 worker, 接口不变.
//   R-20: data_source_ts 用 mock scores@ts (ms epoch → ns); ingestion_ts 本地采.
//
// 测试覆盖在 tests/unit/test_goalserve_client.cpp.

#include "stcpp/data/goalserve_client.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <sstream>
#include <string>

namespace stcpp::data::goalserve {

// ---------------------------------------------------------------------------
// ctor
// ---------------------------------------------------------------------------
GoalserveClient::GoalserveClient(Config cfg) noexcept : cfg_(std::move(cfg)) {}

std::string_view GoalserveClient::HostBase(GoalserveHost h) const noexcept {
    return HostBaseStatic(h, cfg_.prefer_https);
}

// ---------------------------------------------------------------------------
// URL builder — 纯函数, 单测重点 (5 host x 8 sport)
//
// 路径模板 (与 docs/GOALSERVER/feeds_urls.txt 实证一致):
//   InplayOdds       : {inplay_base}/inplay-{sport_slug}.gz
//   InplayResults    : {inplay_base}/results/{yyyyMM}/{match_id}.json
//   InplayDict       : {inplay_base}/dictionaries/odds-markets/{pregame_slug}
//   PregameOdds      : {www_base}/getfeed/{key}/getodds/soccer?cat={cat}_10
//   PregameLiveScore : {www_base}/getfeed/{key}/{pregame_slug}/home
//   InplayMapping    : {www_base}/getfeed/{key}/{pregame_slug}/inplay-mapping
// ---------------------------------------------------------------------------
std::string GoalserveClient::BuildUrl(const UrlSpec& spec) const {
    std::ostringstream oss;
    oss << HostBase(spec.host);

    const auto inplay_slug   = SportInplaySlug(spec.sport);
    const auto pregame_slug  = SportPregameSlug(spec.sport);
    const auto odds_cat      = SportOddsCat(spec.sport);

    switch (spec.endpoint) {
        case GoalserveEndpoint::InplayOdds:
            oss << "/inplay-" << inplay_slug << ".gz";
            break;

        case GoalserveEndpoint::InplayResults:
            oss << "/results/"
                << spec.yyyymm.value_or("000000") << "/"
                << spec.match_id.value_or("0") << ".json";
            break;

        case GoalserveEndpoint::InplayDict:
            oss << "/dictionaries/odds-markets/" << pregame_slug;
            break;

        case GoalserveEndpoint::PregameOdds:
            oss << "/getfeed/" << cfg_.key
                << "/getodds/soccer?cat=" << odds_cat << "_10";
            break;

        case GoalserveEndpoint::PregameLiveScore:
            oss << "/getfeed/" << cfg_.key << "/" << pregame_slug << "/home";
            break;

        case GoalserveEndpoint::InplayMapping:
            oss << "/getfeed/" << cfg_.key << "/" << pregame_slug << "/inplay-mapping";
            break;
    }

    // 通用 query 拼接
    char sep = (spec.endpoint == GoalserveEndpoint::PregameOdds) ? '&' : '?';
    auto add_param = [&](std::string_view k, std::string_view v) {
        oss << sep << k << "=" << v;
        sep = '&';
    };
    if (spec.json && spec.endpoint != GoalserveEndpoint::PregameOdds) {
        add_param("json", "1");
    }
    if (spec.ts_ms.has_value()) {
        add_param("ts", std::to_string(*spec.ts_ms));
    }
    if (spec.date_start.has_value()) {
        add_param("date_start", *spec.date_start);
    }
    if (spec.date_end.has_value()) {
        add_param("date_end", *spec.date_end);
    }
    if (spec.league_filter.has_value()) {
        add_param("league", *spec.league_filter);
    }
    if (spec.bookmaker.has_value()) {
        add_param("bm", *spec.bookmaker);
    }
    if (spec.match_id.has_value()
        && spec.endpoint != GoalserveEndpoint::InplayResults) {
        add_param("match", *spec.match_id);
    }

    return oss.str();
}

// ---------------------------------------------------------------------------
// ingestion_ts — W4 stub: steady_clock; W5 接 CLOCK_MONOTONIC_RAW
// ---------------------------------------------------------------------------
std::int64_t GoalserveClient::NowIngestionNs() noexcept {
    timespec ts{};
#if defined(CLOCK_MONOTONIC_RAW)
    ::clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
#else
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    return static_cast<std::int64_t>(ts.tv_sec) * 1'000'000'000LL
         + static_cast<std::int64_t>(ts.tv_nsec);
}

// ---------------------------------------------------------------------------
// last_update 解析: 支持 "dd.MM.yyyy HH:mm" (Goalserve livescore 主格式)
// 与 ISO 8601 "yyyy-mm-ddTHH:MM:SSZ" (部分 fixture). 返回 0 = 失败.
// ---------------------------------------------------------------------------
std::int64_t GoalserveClient::ParseLastUpdateNs(std::string_view s) noexcept {
    if (s.size() < 10) return 0;
    std::tm tm{};
    // 复制到 0 结尾缓冲 (sscanf 需要)
    char buf[40];
    auto n = (s.size() < sizeof(buf) - 1) ? s.size() : sizeof(buf) - 1;
    for (std::size_t i = 0; i < n; ++i) buf[i] = s[i];
    buf[n] = '\0';

    int yyyy = 0, mon = 0, dd = 0, hh = 0, mm = 0, ss = 0;

    // "dd.MM.yyyy HH:mm"  (Goalserve 实证主路径)
    if (std::sscanf(buf, "%2d.%2d.%4d %2d:%2d",
                    &dd, &mon, &yyyy, &hh, &mm) == 5) {
        tm.tm_mday = dd;
        tm.tm_mon  = mon - 1;
        tm.tm_year = yyyy - 1900;
        tm.tm_hour = hh;
        tm.tm_min  = mm;
        tm.tm_sec  = 0;
    } else if (std::sscanf(buf, "%4d-%2d-%2dT%2d:%2d:%2d",
                           &yyyy, &mon, &dd, &hh, &mm, &ss) == 6) {
        // ISO 8601 (UTC, 末尾可有 Z)
        tm.tm_mday = dd;
        tm.tm_mon  = mon - 1;
        tm.tm_year = yyyy - 1900;
        tm.tm_hour = hh;
        tm.tm_min  = mm;
        tm.tm_sec  = ss;
    } else {
        return 0;
    }

    // Goalserve 文档未声明 last_update 时区; 实证发现以 UTC 给出.
    // 这里用 timegm (UTC). 若未来发现服务器本地化, 派单 @小余 重新校准.
#if defined(__APPLE__) || defined(__linux__)
    time_t t = timegm(&tm);
#else
    time_t t = ::_mkgmtime(&tm);
#endif
    if (t == static_cast<time_t>(-1)) return 0;
    return static_cast<std::int64_t>(t) * 1'000'000'000LL;
}

// ---------------------------------------------------------------------------
// Fetch — W4 stub: 返回 mock fixture per (host, endpoint, sport)
//
// W5 接 cpp-httplib 时 BODY/HEADER 从 HTTP response 填.
// 此处 mock 保证:
//   - data_source_ts_ns = ts_ms * 1e6 (上行优先, 不替代)
//   - ingestion_ts_ns   = NowIngestionNs() (本地)
//   - as_of_ts_ns       = REALTIME now (caller 决策点 W5 在更上层重写)
//   - 4 ts 不等式自然成立 (mock_ts_ms 远小于 now)
// ---------------------------------------------------------------------------
namespace {

constexpr std::int64_t kMockScoresTsMs = 1'732'000'000'000LL;  // 2024-11-19 04:26:40 UTC

inline std::int64_t NowRealtimeNs() noexcept {
    timespec ts{};
    ::clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<std::int64_t>(ts.tv_sec) * 1'000'000'000LL
         + static_cast<std::int64_t>(ts.tv_nsec);
}

// 8 sport 各一份最小 fixture body. 真接接 W5 后替换.
[[nodiscard]] std::string MockBody(GoalserveSport s,
                                   GoalserveEndpoint ep,
                                   std::int64_t scores_ts_ms) {
    std::ostringstream oss;
    oss << "<scores sport=\"" << SportInplaySlug(s)
        << "\" ts=\"" << scores_ts_ms << "\""
        << " endpoint=\"" << static_cast<int>(ep) << "\""
        << "><match id=\"mock-" << SportInplaySlug(s)
        << "-1\" last_update=\"19.11.2024 04:26\"/></scores>";
    return oss.str();
}

}  // namespace

GoalserveResponse GoalserveClient::Fetch(const UrlSpec& spec) {
    GoalserveResponse r;
    r.http_status = 200;

    // 增量协议: spec.ts_ms 已带 → 仅返回 mock 增量 (1/83 体积模拟)
    const bool delta = spec.ts_ms.has_value() && *spec.ts_ms > 0;
    const std::int64_t scores_ts_ms = kMockScoresTsMs + (delta ? 5000 : 0);

    r.body                 = MockBody(spec.sport, spec.endpoint, scores_ts_ms);
    r.is_delta             = delta;
    r.bytes_uncompressed   = delta ? (r.body.size() / 1) : (r.body.size() * 83);
    r.next_ts_ms           = scores_ts_ms;
    r.data_source_ts_ns    = TsMsToNs(scores_ts_ms);
    r.ingestion_ts_ns      = NowIngestionNs();
    r.as_of_ts_ns          = NowRealtimeNs();

    return r;
}

GoalserveResponse GoalserveClient::FetchWithTsDelta(GoalserveHost     host,
                                                    GoalserveEndpoint endpoint,
                                                    GoalserveSport    sport,
                                                    std::int64_t      last_ts_ms) {
    UrlSpec spec;
    spec.host     = host;
    spec.endpoint = endpoint;
    spec.sport    = sport;
    if (last_ts_ms > 0) {
        spec.ts_ms = last_ts_ms;
    }
    return Fetch(spec);
}

}  // namespace stcpp::data::goalserve
