// stcpp/data/goalserve_client.hpp — Goalserve REST adapter v0.1 (header-only + stub)
//
// Owner: 小段 (goalserve-specialist)
// Sprint-2 W4 Wave 19 — stub-only. W5 接 cpp-httplib 真打 HTTP (R-12 走 vCPU3 worker).
//
// 落:
//   docs/GOALSERVER/feeds_urls.txt           (PREGAME / livescore / inplay-mapping URL)
//   docs/GOALSERVER/inplay-feed-new.txt      (inplay.goalserve.com /dictionaries/ + 11 time_status)
//   docs/GOALSERVER/full_package_feed.txt    (key + ts ms-epoch 增量)
//   小段 v3 + v2.1 (5 host 实证 + 8 sport inplay 实证 + 4 ts 落位)
//
// 红线:
//   R-12: HTTP I/O 不允许 WSS event loop 内同步; client 提供 sync API + future-based async
//         (W4 stub 全 sync; W5 真接由 vCPU3 worker pool 调度)
//   R-20: payload 自带 ts 优先 (UPSTREAM_PAYLOAD), 禁本地 now() 替代 data_source_ts;
//         data_source_ts 来源排序 (高 → 低): scores@ts (ms epoch) → match@last_update
//         (ISO 8601 / "dd.MM.yyyy HH:mm" → epoch_ns) → 本地 ingestion_ts (告警 fallback)
//   GM 红线: 严禁参考老项目 (CI grep 老仓库名 拦, 见 .github/workflows/pr.yml)
//
// 不耻下问:
//   - ETL schema → @小余 (data-etl)
//   - 4 ts canon / audit schema → @老唐 (laotang-audit-schema-v1.1.md)
//   - HTTP client 选型 (cpp-httplib vs cpr) → @老李 / @老周, W5 决议
//
// ============================================================================

#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stcpp::data::goalserve {

// ---------------------------------------------------------------------------
// 1. Host enum — 5 host 实证闭合 (小段 v3 §3)
// ---------------------------------------------------------------------------
//
// www              : pregame / livescore / fixtures / odds (cat=*_10)  HTTP+HTTPS
// inplay           : inplay-{sport}.gz / results / dictionaries        HTTP only
// oddsfeed         : 历史 odds dump (低频)                              HTTPS
// livescore        : livescore.goalserve.com 镜像                       HTTPS
// inplay-mapping   : 仅 5 sport (soccer / esports / tennis_scores /
//                    basketball / baseball) 提供 inplay <-> pregame map  HTTPS
//
// 5 host 实证 W3 完成, 任何新发现的 host 必须 patch 此 enum + URL builder.
enum class GoalserveHost : std::uint8_t {
    Www            = 0,  // www.goalserve.com           (主)
    Inplay         = 1,  // inplay.goalserve.com        (gz / dictionaries / results)
    OddsFeed       = 2,  // oddsfeed.goalserve.com      (历史 dump)
    LiveScore      = 3,  // livescore.goalserve.com     (镜像)
    InplayMapping  = 4,  // www.goalserve.com/.../{sport}/inplay-mapping (5 sport)
};

// ---------------------------------------------------------------------------
// 2. Sport enum — 8 sport inplay 实证闭合 (小段 v3 §4)
//
// 命名陷阱: schedule 在 URL 拼写为 "shedule" (双 c 错, 单 c 对) — 见
//   docs/GOALSERVER/feeds_urls.txt L300 baseball/mlb_shedule
//                                  L479 football/nfl-shedule
// 工具方法 SportInplaySlug() / SportPregameSlug() 已对齐, 测试覆盖.
// ---------------------------------------------------------------------------
enum class GoalserveSport : std::uint8_t {
    Soccer            = 0,   // inplay-soccer.gz       / soccernew/home
    Basketball        = 1,   // inplay-basket.gz       / bsktbl/home
    Tennis            = 2,   // inplay-tennis.gz       / tennis_scores/home
    Volleyball        = 3,   // inplay-volleyball.gz   / volleyball/home
    AmericanFootball  = 4,   // inplay-amfootball.gz   / football/nfl-scores
    Esports           = 5,   // inplay-esports.gz      / esports/home
    Hockey            = 6,   // inplay-hockey.gz       / hockey/home
    Baseball          = 7,   // inplay-baseball.gz     / baseball/home
};

inline constexpr std::size_t kNumHosts  = 5;
inline constexpr std::size_t kNumSports = 8;

// inplay.goalserve.com URL slug (与 docs/GOALSERVER/inplay-feed-new.txt L1-L9 一致)
[[nodiscard]] constexpr std::string_view SportInplaySlug(GoalserveSport s) noexcept {
    switch (s) {
        case GoalserveSport::Soccer:           return "soccer";
        case GoalserveSport::Basketball:       return "basket";          // 简写 != bsktbl
        case GoalserveSport::Tennis:           return "tennis";
        case GoalserveSport::Volleyball:       return "volleyball";
        case GoalserveSport::AmericanFootball: return "amfootball";      // 注: am 前缀
        case GoalserveSport::Esports:          return "esports";
        case GoalserveSport::Hockey:           return "hockey";
        case GoalserveSport::Baseball:         return "baseball";
    }
    return "";
}

// www.goalserve.com /<slug>/ 主路径 (与 feeds_urls.txt 一致, 不是 inplay 命名)
[[nodiscard]] constexpr std::string_view SportPregameSlug(GoalserveSport s) noexcept {
    switch (s) {
        case GoalserveSport::Soccer:           return "soccernew";
        case GoalserveSport::Basketball:       return "bsktbl";          // 注: 与 inplay/basket 不同
        case GoalserveSport::Tennis:           return "tennis_scores";
        case GoalserveSport::Volleyball:       return "volleyball";
        case GoalserveSport::AmericanFootball: return "football";        // www 用 football
        case GoalserveSport::Esports:          return "esports";
        case GoalserveSport::Hockey:           return "hockey";
        case GoalserveSport::Baseball:         return "baseball";
    }
    return "";
}

// getodds/soccer?cat={X}_10 中的 X (注意所有 sport odds 都挂在 /getodds/soccer 路径下)
[[nodiscard]] constexpr std::string_view SportOddsCat(GoalserveSport s) noexcept {
    switch (s) {
        case GoalserveSport::Soccer:           return "soccer";
        case GoalserveSport::Basketball:       return "basket";
        case GoalserveSport::Tennis:           return "tennis";
        case GoalserveSport::Volleyball:       return "volleyball";
        case GoalserveSport::AmericanFootball: return "football";
        case GoalserveSport::Esports:          return "esports";
        case GoalserveSport::Hockey:           return "hockey";
        case GoalserveSport::Baseball:         return "baseball";
    }
    return "";
}

// ---------------------------------------------------------------------------
// 3. TimeStatus enum — 11 状态闭合 (docs/GOALSERVER/inplay-feed-new.txt L16-L27)
// ---------------------------------------------------------------------------
enum class TimeStatus : std::uint8_t {
    NotStarted   = 0,
    InPlay       = 1,
    ToBeFixed    = 2,
    Ended        = 3,
    Postponed    = 4,
    Cancelled    = 5,
    Walkover     = 6,
    Interrupted  = 7,
    Abandoned    = 8,
    Retired      = 9,
    Removed      = 99,
};

// 11 enum 闭合校验 (gtest 覆盖) — 任何 Goalserve 新增 status 必须 patch + 升级
inline constexpr std::array<TimeStatus, 11> kAllTimeStatus{
    TimeStatus::NotStarted,  TimeStatus::InPlay,      TimeStatus::ToBeFixed,
    TimeStatus::Ended,       TimeStatus::Postponed,   TimeStatus::Cancelled,
    TimeStatus::Walkover,    TimeStatus::Interrupted, TimeStatus::Abandoned,
    TimeStatus::Retired,     TimeStatus::Removed,
};

[[nodiscard]] constexpr std::string_view TimeStatusName(TimeStatus t) noexcept {
    switch (t) {
        case TimeStatus::NotStarted:  return "NotStarted";
        case TimeStatus::InPlay:      return "InPlay";
        case TimeStatus::ToBeFixed:   return "ToBeFixed";
        case TimeStatus::Ended:       return "Ended";
        case TimeStatus::Postponed:   return "Postponed";
        case TimeStatus::Cancelled:   return "Cancelled";
        case TimeStatus::Walkover:    return "Walkover";
        case TimeStatus::Interrupted: return "Interrupted";
        case TimeStatus::Abandoned:   return "Abandoned";
        case TimeStatus::Retired:     return "Retired";
        case TimeStatus::Removed:     return "Removed";
    }
    return "Unknown";
}

[[nodiscard]] constexpr bool IsTerminal(TimeStatus t) noexcept {
    // 终态: 不再产生新 odds tick, 应从 inplay feed 移除
    return t == TimeStatus::Ended      || t == TimeStatus::Postponed
        || t == TimeStatus::Cancelled  || t == TimeStatus::Walkover
        || t == TimeStatus::Abandoned  || t == TimeStatus::Retired
        || t == TimeStatus::Removed;
}

// ---------------------------------------------------------------------------
// 4. Endpoint 类型 — 区分 inplay / pregame_odds / livescore / mapping / results
// ---------------------------------------------------------------------------
enum class GoalserveEndpoint : std::uint8_t {
    InplayOdds       = 0,   // inplay.goalserve.com/inplay-{slug}.gz
    InplayResults    = 1,   // inplay.goalserve.com/results/{yyyyMM}/{match_id}.json
    InplayDict       = 2,   // inplay.goalserve.com/dictionaries/{type}/{sport}
    PregameOdds      = 3,   // www.goalserve.com/.../getodds/soccer?cat={X}_10
    PregameLiveScore = 4,   // www.goalserve.com/.../{slug}/home  (今日 + 历史 d-N)
    InplayMapping    = 5,   // www.goalserve.com/.../{slug}/inplay-mapping (5 sport)
};

// ---------------------------------------------------------------------------
// 5. UrlSpec — 构造完整 URL 所需的全部输入
// ---------------------------------------------------------------------------
struct UrlSpec {
    GoalserveHost     host;
    GoalserveEndpoint endpoint;
    GoalserveSport    sport;
    std::optional<std::int64_t> ts_ms;          // R-20 增量 (scores@ts ms epoch)
    std::optional<std::string>  match_id;       // InplayResults / filter
    std::optional<std::string>  yyyymm;         // InplayResults (e.g. "202604")
    std::optional<std::string>  date_start;     // dd.MM.yyyy
    std::optional<std::string>  date_end;
    std::optional<std::string>  league_filter;  // 逗号分隔, "_" 前缀 = gid
    std::optional<std::string>  bookmaker;      // 逗号分隔
    bool                        json = true;    // ?json=1
};

// ---------------------------------------------------------------------------
// 6. Mock response — W4 stub 返回, W5 真接换成 HTTP response wrap
// ---------------------------------------------------------------------------
struct GoalserveResponse {
    int                  http_status      = 0;
    std::string          body;                  // 原始 XML / JSON
    std::int64_t         data_source_ts_ns = 0; // R-20 来自 payload (scores@ts)
    std::int64_t         ingestion_ts_ns   = 0; // R-20 本地 MONOTONIC_RAW 收完
    std::int64_t         as_of_ts_ns       = 0; // R-20 REALTIME, decision 用
    bool                 is_delta          = false; // true = ts 增量响应 (压缩 83x)
    std::size_t          bytes_uncompressed = 0;
    std::optional<std::int64_t> next_ts_ms;       // 下一次增量用
};

// ---------------------------------------------------------------------------
// 7. GoalserveClient — 主类 (W4 stub, W5 真接 HTTP)
//
// 线程模型: 调用方负责. W5 此类移到 vCPU3 worker pool 内, 永不在 WSS event loop 调用.
//
// 时间戳契约 (R-20):
//   - data_source_ts: payload 自带 (scores@ts ms epoch → ns) 优先;
//                     若 payload 无 ts (e.g. livescore HTML), parse match@last_update
//                     ISO 8601 / "dd.MM.yyyy HH:mm" → epoch_ns
//   - ingestion_ts:   client 本地 CLOCK_MONOTONIC_RAW, HTTP body 收完即采
//   - as_of_ts:       caller 决策点采 CLOCK_REALTIME (本类不采)
//   - event_ts:       caller 从 GameRecord/OddsRecord 解析 (本类提供 helper)
// ---------------------------------------------------------------------------
class GoalserveClient {
public:
    // 配置 — key 走 .env, base url 5 host 编码在 BuildUrl, 不允许动态切换 host
    struct Config {
        std::string key;          // 87ff5e514c6c415be05908deb2f15526 (.env GOALSERVE_KEY)
        bool        prefer_https = true;
        bool        gzip         = true;
        std::chrono::milliseconds timeout{5000};
    };

    explicit GoalserveClient(Config cfg) noexcept;

    // URL builder — 纯函数, 单测重点 (5 host x 8 sport x N endpoint)
    [[nodiscard]] std::string BuildUrl(const UrlSpec& spec) const;

    // W4 stub fetch — 不打 HTTP, 返回 mock fixture (8 sport 各一份)
    // W5 接 cpp-httplib 时此函数实现切真 HTTP, 接口不变.
    [[nodiscard]] GoalserveResponse Fetch(const UrlSpec& spec);

    // ts 增量便捷封装 (R-20 增量协议 压缩 83x)
    //   last_ts_ms = 0 → 第一次全量, 返回 next_ts_ms
    //   last_ts_ms > 0 → 仅返回该 ts 之后的变更
    [[nodiscard]] GoalserveResponse FetchWithTsDelta(
        GoalserveHost     host,
        GoalserveEndpoint endpoint,
        GoalserveSport    sport,
        std::int64_t      last_ts_ms);

    // helper: 解析 scores@ts ms epoch → ns (R-20 data_source_ts 主路径)
    [[nodiscard]] static std::int64_t TsMsToNs(std::int64_t ts_ms) noexcept {
        return ts_ms * 1'000'000LL;
    }

    // helper: ISO 8601 / "dd.MM.yyyy HH:mm" → epoch_ns (R-20 fallback)
    // 返回 0 = parse 失败, caller 退到 ingestion_ts fallback + 告警
    [[nodiscard]] static std::int64_t ParseLastUpdateNs(std::string_view s) noexcept;

    // 本地 ingestion_ts 采集 (CLOCK_MONOTONIC_RAW 对应 ns; W4 stub 用 steady_clock 顶替)
    [[nodiscard]] static std::int64_t NowIngestionNs() noexcept;

    // host base — 私 API, 测试可调
    [[nodiscard]] std::string_view HostBase(GoalserveHost h) const noexcept;

private:
    Config cfg_;
};

// ---------------------------------------------------------------------------
// 8. Host base table (5 host 闭合, 与 docs/GOALSERVER/feeds_urls.txt 一致)
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr std::string_view HostBaseStatic(GoalserveHost h, bool https) noexcept {
    switch (h) {
        case GoalserveHost::Www:
            return https ? "https://www.goalserve.com" : "http://www.goalserve.com";
        case GoalserveHost::Inplay:
            // 实证: inplay.goalserve.com 多数 endpoint 仅 http 可达 (TLS cert 域名错配)
            return "http://inplay.goalserve.com";
        case GoalserveHost::OddsFeed:
            return https ? "https://oddsfeed.goalserve.com" : "http://oddsfeed.goalserve.com";
        case GoalserveHost::LiveScore:
            return https ? "https://livescore.goalserve.com" : "http://livescore.goalserve.com";
        case GoalserveHost::InplayMapping:
            // inplay-mapping 物理挂在 www 路径下, 视为 www 子树
            return https ? "https://www.goalserve.com" : "http://www.goalserve.com";
    }
    return "";
}

}  // namespace stcpp::data::goalserve
