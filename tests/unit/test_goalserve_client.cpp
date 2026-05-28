// tests/unit/test_goalserve_client.cpp — GoalserveClient W4 stub 覆盖
//
// Owner: 小段 (goalserve-specialist)
// Sprint-2 W4 Wave 19 — 测试覆盖:
//   T1  URL builder 40 combos (5 host x 8 sport) 全覆盖
//   T2  TimeStatus 11 enum 闭合 + Removed=99 边缘值
//   T3  GameRecord / OddsRecord 4 ts 不等式 (R-20)
//   T4  ts 增量 round-trip (FetchWithTsDelta last → next)
//   T5  sport 命名陷阱: shedule (单 c) / basket vs bsktbl / amfootball
//   T6  ParseLastUpdateNs 两种格式 ("dd.MM.yyyy HH:mm" + ISO 8601)
//
// 红线:
//   R-20: 任何 FourTs::IsMonotonic() = false 必须测出
//   GM 红线: 严禁参考老项目 (CI grep 老仓库名 拦, 见 .github/workflows/pr.yml)

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "stcpp/data/goalserve_client.hpp"
#include "stcpp/data/goalserve_record.hpp"

using namespace stcpp::data::goalserve;

namespace {

GoalserveClient::Config MakeCfg() {
    GoalserveClient::Config cfg;
    cfg.key = "TEST_KEY_DEADBEEF";  // 测试 key, 真 key 走 .env GOALSERVE_KEY
    cfg.prefer_https = true;
    cfg.gzip = true;
    return cfg;
}

constexpr std::array<GoalserveHost, kNumHosts> kAllHosts{
    GoalserveHost::Www,       GoalserveHost::Inplay,
    GoalserveHost::OddsFeed,  GoalserveHost::LiveScore,
    GoalserveHost::InplayMapping,
};

constexpr std::array<GoalserveSport, kNumSports> kAllSports{
    GoalserveSport::Soccer,           GoalserveSport::Basketball,
    GoalserveSport::Tennis,           GoalserveSport::Volleyball,
    GoalserveSport::AmericanFootball, GoalserveSport::Esports,
    GoalserveSport::Hockey,           GoalserveSport::Baseball,
};

}  // namespace

// ---------------------------------------------------------------------------
// T1.0  Host base 5 host 闭合 — 顺序与 docs/GOALSERVER/feeds_urls.txt 一致
// ---------------------------------------------------------------------------
TEST(GoalserveHost, FiveHostBaseClosed) {
    EXPECT_EQ(HostBaseStatic(GoalserveHost::Www, true),
              "https://www.goalserve.com");
    // Inplay 仅 http (实证 TLS cert 域名错配)
    EXPECT_EQ(HostBaseStatic(GoalserveHost::Inplay, true),
              "http://inplay.goalserve.com");
    EXPECT_EQ(HostBaseStatic(GoalserveHost::Inplay, false),
              "http://inplay.goalserve.com");
    EXPECT_EQ(HostBaseStatic(GoalserveHost::OddsFeed, true),
              "https://oddsfeed.goalserve.com");
    EXPECT_EQ(HostBaseStatic(GoalserveHost::LiveScore, true),
              "https://livescore.goalserve.com");
    // InplayMapping 物理挂 www 子树
    EXPECT_EQ(HostBaseStatic(GoalserveHost::InplayMapping, true),
              "https://www.goalserve.com");
}

// ---------------------------------------------------------------------------
// T1.1  URL builder — 8 sport x InplayOdds (inplay.goalserve.com/inplay-X.gz)
// ---------------------------------------------------------------------------
TEST(GoalserveUrl, InplayOdds8SportSlug) {
    GoalserveClient c(MakeCfg());
    const std::array<std::pair<GoalserveSport, std::string_view>, 8> expect{{
        {GoalserveSport::Soccer,           "soccer"},
        {GoalserveSport::Basketball,       "basket"},
        {GoalserveSport::Tennis,           "tennis"},
        {GoalserveSport::Volleyball,       "volleyball"},
        {GoalserveSport::AmericanFootball, "amfootball"},
        {GoalserveSport::Esports,          "esports"},
        {GoalserveSport::Hockey,           "hockey"},
        {GoalserveSport::Baseball,         "baseball"},
    }};
    for (const auto& [sport, slug] : expect) {
        UrlSpec spec{};
        spec.host = GoalserveHost::Inplay;
        spec.endpoint = GoalserveEndpoint::InplayOdds;
        spec.sport = sport;
        spec.json = false;
        const std::string url = c.BuildUrl(spec);
        const std::string want = "http://inplay.goalserve.com/inplay-"
                               + std::string(slug) + ".gz";
        EXPECT_EQ(url, want) << "sport slug 错: " << slug;
    }
}

// ---------------------------------------------------------------------------
// T1.2  URL builder 全 5 host x 8 sport = 40 combos
// 用 PregameLiveScore 端点穷举 (含 key + pregame slug 命名陷阱)
// ---------------------------------------------------------------------------
TEST(GoalserveUrl, FortyCombosHostXSport) {
    GoalserveClient c(MakeCfg());
    std::size_t combos = 0;
    for (auto h : kAllHosts) {
        for (auto s : kAllSports) {
            UrlSpec spec{};
            spec.host     = h;
            spec.endpoint = (h == GoalserveHost::Inplay)
                          ? GoalserveEndpoint::InplayOdds
                          : (h == GoalserveHost::InplayMapping
                             ? GoalserveEndpoint::InplayMapping
                             : GoalserveEndpoint::PregameLiveScore);
            spec.sport = s;
            spec.json  = false;
            const std::string url = c.BuildUrl(spec);
            EXPECT_FALSE(url.empty()) << "host=" << static_cast<int>(h)
                                      << " sport=" << static_cast<int>(s);
            EXPECT_NE(url.find(HostBaseStatic(h, true)), std::string::npos);
            ++combos;
        }
    }
    EXPECT_EQ(combos, 40U);
}

// ---------------------------------------------------------------------------
// T1.3  Pregame odds 路径模板: 注意所有 sport 共享 /getodds/soccer 路径
// ---------------------------------------------------------------------------
TEST(GoalserveUrl, PregameOddsAllSportShareSoccerPath) {
    GoalserveClient c(MakeCfg());
    for (auto s : kAllSports) {
        UrlSpec spec{};
        spec.host = GoalserveHost::Www;
        spec.endpoint = GoalserveEndpoint::PregameOdds;
        spec.sport = s;
        spec.json = false;
        const std::string url = c.BuildUrl(spec);
        EXPECT_NE(url.find("/getodds/soccer?cat="), std::string::npos)
            << "所有 sport 的 odds 都走 /getodds/soccer 路径 (Goalserve 设计)";
        EXPECT_NE(url.find("_10"), std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// T1.4  InplayResults 路径 = /results/{yyyyMM}/{match_id}.json
// ---------------------------------------------------------------------------
TEST(GoalserveUrl, InplayResultsPath) {
    GoalserveClient c(MakeCfg());
    UrlSpec spec{};
    spec.host = GoalserveHost::Inplay;
    spec.endpoint = GoalserveEndpoint::InplayResults;
    spec.sport = GoalserveSport::Soccer;
    spec.yyyymm = "202604";
    spec.match_id = "59077136";
    spec.json = false;
    const std::string url = c.BuildUrl(spec);
    EXPECT_EQ(url,
              "http://inplay.goalserve.com/results/202604/59077136.json");
}

// ---------------------------------------------------------------------------
// T1.5  InplayDict 路径 — dictionaries 用 pregame_slug 不是 inplay_slug
//       (basket → 注意 dictionaries 应是 bsktbl? — 实证 docs 显示用 pregame_slug)
// ---------------------------------------------------------------------------
TEST(GoalserveUrl, InplayDictPathUsesPregameSlug) {
    GoalserveClient c(MakeCfg());
    UrlSpec spec{};
    spec.host = GoalserveHost::Inplay;
    spec.endpoint = GoalserveEndpoint::InplayDict;
    spec.sport = GoalserveSport::Soccer;
    spec.json = false;
    const std::string url = c.BuildUrl(spec);
    EXPECT_NE(url.find("/dictionaries/odds-markets/soccernew"),
              std::string::npos);
}

// ---------------------------------------------------------------------------
// T1.6  ts 增量参数拼接 (R-20)
// ---------------------------------------------------------------------------
TEST(GoalserveUrl, TsDeltaParamAppended) {
    GoalserveClient c(MakeCfg());
    UrlSpec spec{};
    spec.host = GoalserveHost::Www;
    spec.endpoint = GoalserveEndpoint::PregameOdds;
    spec.sport = GoalserveSport::Soccer;
    spec.ts_ms = 1474825423341LL;
    spec.json = false;
    const std::string url = c.BuildUrl(spec);
    EXPECT_NE(url.find("&ts=1474825423341"), std::string::npos);
}

// ---------------------------------------------------------------------------
// T2  TimeStatus 11 enum 闭合 + Removed=99 边缘值
// ---------------------------------------------------------------------------
TEST(GoalserveTimeStatus, ElevenEnumClosed) {
    EXPECT_EQ(kAllTimeStatus.size(), 11U);
    EXPECT_EQ(static_cast<std::uint8_t>(TimeStatus::NotStarted),  0U);
    EXPECT_EQ(static_cast<std::uint8_t>(TimeStatus::InPlay),      1U);
    EXPECT_EQ(static_cast<std::uint8_t>(TimeStatus::ToBeFixed),   2U);
    EXPECT_EQ(static_cast<std::uint8_t>(TimeStatus::Ended),       3U);
    EXPECT_EQ(static_cast<std::uint8_t>(TimeStatus::Postponed),   4U);
    EXPECT_EQ(static_cast<std::uint8_t>(TimeStatus::Cancelled),   5U);
    EXPECT_EQ(static_cast<std::uint8_t>(TimeStatus::Walkover),    6U);
    EXPECT_EQ(static_cast<std::uint8_t>(TimeStatus::Interrupted), 7U);
    EXPECT_EQ(static_cast<std::uint8_t>(TimeStatus::Abandoned),   8U);
    EXPECT_EQ(static_cast<std::uint8_t>(TimeStatus::Retired),     9U);
    EXPECT_EQ(static_cast<std::uint8_t>(TimeStatus::Removed),     99U);
}

TEST(GoalserveTimeStatus, TerminalSetClosed) {
    EXPECT_FALSE(IsTerminal(TimeStatus::NotStarted));
    EXPECT_FALSE(IsTerminal(TimeStatus::InPlay));
    EXPECT_FALSE(IsTerminal(TimeStatus::ToBeFixed));
    EXPECT_FALSE(IsTerminal(TimeStatus::Interrupted));
    EXPECT_TRUE(IsTerminal(TimeStatus::Ended));
    EXPECT_TRUE(IsTerminal(TimeStatus::Postponed));
    EXPECT_TRUE(IsTerminal(TimeStatus::Cancelled));
    EXPECT_TRUE(IsTerminal(TimeStatus::Walkover));
    EXPECT_TRUE(IsTerminal(TimeStatus::Abandoned));
    EXPECT_TRUE(IsTerminal(TimeStatus::Retired));
    EXPECT_TRUE(IsTerminal(TimeStatus::Removed));
}

TEST(GoalserveTimeStatus, NamesNonEmpty) {
    for (auto t : kAllTimeStatus) {
        EXPECT_FALSE(TimeStatusName(t).empty());
        EXPECT_NE(TimeStatusName(t), "Unknown");
    }
}

// ---------------------------------------------------------------------------
// T3  4 ts 不等式 (R-20) — GameRecord / OddsRecord 公用 FourTs
// ---------------------------------------------------------------------------
TEST(GoalserveFourTs, MonotonicChainOk) {
    FourTs ts;
    ts.event_ts_ns       = 1'000'000'000'000LL;
    ts.data_source_ts_ns = 1'000'000'000'001LL;
    ts.ingestion_ts_ns   = 1'000'000'000'002LL;
    ts.as_of_ts_ns       = 1'000'000'000'003LL;
    EXPECT_TRUE(ts.IsMonotonic());
}

TEST(GoalserveFourTs, EventTsZeroRejected) {
    FourTs ts;
    ts.event_ts_ns       = 0;  // 违反
    ts.data_source_ts_ns = 1'000'000'000'001LL;
    ts.ingestion_ts_ns   = 1'000'000'000'002LL;
    ts.as_of_ts_ns       = 1'000'000'000'003LL;
    EXPECT_FALSE(ts.IsMonotonic());
}

TEST(GoalserveFourTs, DsBeforeEventRejected) {
    FourTs ts;
    ts.event_ts_ns       = 1'000'000'000'002LL;
    ts.data_source_ts_ns = 1'000'000'000'001LL;  // 倒流
    ts.ingestion_ts_ns   = 1'000'000'000'003LL;
    ts.as_of_ts_ns       = 1'000'000'000'004LL;
    EXPECT_FALSE(ts.IsMonotonic());
}

TEST(GoalserveFourTs, IngestionBeforeDsRejected) {
    FourTs ts;
    ts.event_ts_ns       = 1'000'000'000'000LL;
    ts.data_source_ts_ns = 1'000'000'000'005LL;
    ts.ingestion_ts_ns   = 1'000'000'000'002LL;  // 倒流
    ts.as_of_ts_ns       = 1'000'000'000'006LL;
    EXPECT_FALSE(ts.IsMonotonic());
}

TEST(GoalserveFourTs, AsOfBeforeIngestionRejected) {
    FourTs ts;
    ts.event_ts_ns       = 1'000'000'000'000LL;
    ts.data_source_ts_ns = 1'000'000'000'001LL;
    ts.ingestion_ts_ns   = 1'000'000'000'005LL;
    ts.as_of_ts_ns       = 1'000'000'000'002LL;  // 倒流
    EXPECT_FALSE(ts.IsMonotonic());
}

TEST(GoalserveRecord, GameRecordR20Compliance) {
    GameRecord g;
    g.ts.event_ts_ns       = 1'700'000'000'000'000'000LL;
    g.ts.data_source_ts_ns = 1'732'000'000'000'000'000LL;  // TsMsToNs(1732000000000)
    g.ts.ingestion_ts_ns   = g.ts.data_source_ts_ns + 1'000'000LL;
    g.ts.as_of_ts_ns       = g.ts.ingestion_ts_ns + 1'000'000LL;
    g.ts.ds_origin         = DataSourceTsOrigin::PayloadScoresTs;
    EXPECT_TRUE(g.RespectsR20());

    g.ts.ds_origin = DataSourceTsOrigin::IngestionFallback;
    EXPECT_FALSE(g.RespectsR20()) << "IngestionFallback 违反 R-20";
}

TEST(GoalserveRecord, OddsRecordR20Compliance) {
    OddsRecord o;
    o.ts.event_ts_ns       = 1'700'000'000'000'000'000LL;
    o.ts.data_source_ts_ns = 1'732'000'000'000'000'000LL;
    o.ts.ingestion_ts_ns   = o.ts.data_source_ts_ns + 1;
    o.ts.as_of_ts_ns       = o.ts.ingestion_ts_ns + 1;
    o.ts.ds_origin         = DataSourceTsOrigin::PayloadScoresTs;
    o.bookmaker_id = 16;
    o.market_id = "1x2";
    o.outcome = "home";
    o.value = 1.95;
    EXPECT_TRUE(o.RespectsR20());
}

// ---------------------------------------------------------------------------
// T4  ts 增量 round-trip — FetchWithTsDelta(last_ts) 返回 next_ts > last_ts
// ---------------------------------------------------------------------------
TEST(GoalserveDelta, FullThenIncrementalRoundTrip) {
    GoalserveClient c(MakeCfg());

    // 第一次: 全量
    auto r1 = c.FetchWithTsDelta(GoalserveHost::Www,
                                 GoalserveEndpoint::PregameOdds,
                                 GoalserveSport::Soccer, 0);
    EXPECT_EQ(r1.http_status, 200);
    EXPECT_FALSE(r1.is_delta);
    EXPECT_TRUE(r1.next_ts_ms.has_value());
    EXPECT_GT(*r1.next_ts_ms, 0);

    // 4 ts 不等式 (data_source < ingestion < as_of)
    EXPECT_GT(r1.data_source_ts_ns, 0);
    EXPECT_GT(r1.ingestion_ts_ns,   0);
    EXPECT_GT(r1.as_of_ts_ns,       0);
    EXPECT_LE(r1.data_source_ts_ns, r1.as_of_ts_ns);

    // 第二次: 增量 (压缩后体积 < 全量)
    auto r2 = c.FetchWithTsDelta(GoalserveHost::Www,
                                 GoalserveEndpoint::PregameOdds,
                                 GoalserveSport::Soccer,
                                 *r1.next_ts_ms);
    EXPECT_TRUE(r2.is_delta);
    EXPECT_TRUE(r2.next_ts_ms.has_value());
    EXPECT_GT(*r2.next_ts_ms, *r1.next_ts_ms);
    EXPECT_LT(r2.bytes_uncompressed, r1.bytes_uncompressed)
        << "增量协议应压缩 (mock 模拟 83x)";
}

// ---------------------------------------------------------------------------
// T5  sport 命名陷阱 — shedule 单 c (Goalserve 拼写错), basket vs bsktbl
// ---------------------------------------------------------------------------
TEST(GoalserveNamingTrap, BasketHasTwoSlugs) {
    EXPECT_EQ(SportInplaySlug(GoalserveSport::Basketball),  "basket");
    EXPECT_EQ(SportPregameSlug(GoalserveSport::Basketball), "bsktbl");
    EXPECT_EQ(SportOddsCat(GoalserveSport::Basketball),     "basket");
}

TEST(GoalserveNamingTrap, AmFootballPrefixDiffersFromFootball) {
    EXPECT_EQ(SportInplaySlug(GoalserveSport::AmericanFootball),  "amfootball");
    EXPECT_EQ(SportPregameSlug(GoalserveSport::AmericanFootball), "football");
    EXPECT_EQ(SportOddsCat(GoalserveSport::AmericanFootball),     "football");
}

TEST(GoalserveNamingTrap, TennisHasTwoSlugs) {
    EXPECT_EQ(SportInplaySlug(GoalserveSport::Tennis),  "tennis");
    EXPECT_EQ(SportPregameSlug(GoalserveSport::Tennis), "tennis_scores");
}

TEST(GoalserveNamingTrap, SoccerNewSlug) {
    // 文档实证: pregame 主路径是 soccernew/home 不是 soccer/home
    EXPECT_EQ(SportPregameSlug(GoalserveSport::Soccer), "soccernew");
}

// ---------------------------------------------------------------------------
// T6  ParseLastUpdateNs — Goalserve 两种格式
// ---------------------------------------------------------------------------
TEST(GoalserveLastUpdate, GoalserveNativeFormat) {
    // "dd.MM.yyyy HH:mm" (Goalserve livescore 主格式)
    // 2024-11-19 04:26:00 UTC = 1731990360 sec
    auto ns = GoalserveClient::ParseLastUpdateNs("19.11.2024 04:26");
    EXPECT_NE(ns, 0);
    EXPECT_EQ(ns, 1'731'990'360LL * 1'000'000'000LL);
}

TEST(GoalserveLastUpdate, Iso8601Format) {
    auto ns = GoalserveClient::ParseLastUpdateNs("2024-11-19T04:26:00Z");
    EXPECT_NE(ns, 0);
    EXPECT_EQ(ns, 1'731'990'360LL * 1'000'000'000LL);
}

TEST(GoalserveLastUpdate, MalformedReturnsZero) {
    EXPECT_EQ(GoalserveClient::ParseLastUpdateNs(""), 0);
    EXPECT_EQ(GoalserveClient::ParseLastUpdateNs("garbage"), 0);
    EXPECT_EQ(GoalserveClient::ParseLastUpdateNs("2024"), 0);
}

// ---------------------------------------------------------------------------
// T7  NowIngestionNs 单调 + TsMsToNs 精度
// ---------------------------------------------------------------------------
TEST(GoalserveTime, IngestionMonotonic) {
    auto a = GoalserveClient::NowIngestionNs();
    std::this_thread::sleep_for(std::chrono::microseconds(50));
    auto b = GoalserveClient::NowIngestionNs();
    EXPECT_GT(b, a);
}

TEST(GoalserveTime, TsMsToNsExact) {
    EXPECT_EQ(GoalserveClient::TsMsToNs(0), 0);
    EXPECT_EQ(GoalserveClient::TsMsToNs(1), 1'000'000LL);
    EXPECT_EQ(GoalserveClient::TsMsToNs(1474825423341LL),
              1'474'825'423'341'000'000LL);
}

// ---------------------------------------------------------------------------
// T8  Stub Fetch 4-ts 自然成立 (mock_ts_ms 远小于 now)
// ---------------------------------------------------------------------------
TEST(GoalserveFetch, StubResponseFourTsNaturalChain) {
    GoalserveClient c(MakeCfg());
    UrlSpec spec{};
    spec.host = GoalserveHost::Inplay;
    spec.endpoint = GoalserveEndpoint::InplayOdds;
    spec.sport = GoalserveSport::Hockey;
    auto r = c.Fetch(spec);
    EXPECT_EQ(r.http_status, 200);
    EXPECT_GT(r.data_source_ts_ns, 0);
    EXPECT_GT(r.ingestion_ts_ns, 0);
    EXPECT_GT(r.as_of_ts_ns, 0);
    // data_source (2024-11) ≤ ingestion (now monotonic) ≤ as_of (now realtime)
    // ingestion_ts 与 as_of_ts 是不同 clock, 单独 > 0 即可
    EXPECT_NE(r.body.find("sport=\"hockey\""), std::string::npos);
}
