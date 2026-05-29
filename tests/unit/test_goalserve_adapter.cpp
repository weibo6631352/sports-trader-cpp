// tests/unit/test_goalserve_adapter.cpp — Goalserve adapter schema v0.1 ctest
//
// Owner: 小段 (goalserve-specialist, #37)
// Date:  2026-05-29
// Task:  ADR-037 数据地基 — vendor-agnostic adapter schema ctest
//
// 覆盖:
//   T1: InfoKind / VendorId / GameEventType enum 闭合
//   T2: R-20 四时间戳契约 — GameScoreRecord / OddsQuoteRecord
//   T3: 时间戳转换 helper — NetTicksToEpochNs / InplayTsMsToNs / PregameTsSecToNs
//   T4: OddsQuoteRecord.IsValid() 逻辑 (value > 1.0 + 非 suspend + R-20)
//   T5: GameEventType MapGoalserveEventType — 8 种类型 + VAR + Other
//   T6: AdaptBatch 四类 record 共存 + data_source_ts monotonic
//   T7: 3-way soccer odds — Home/Draw/Away 三个 OddsQuoteRecord 独立
//   T8: .NET ticks 边界 — ticks <= epoch_offset 返回 0

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "stcpp/data/goalserve_adapter.hpp"
#include "stcpp/data/goalserve_record.hpp"

using namespace stcpp::data::adapter;
using namespace stcpp::data::goalserve;

namespace {

// 统一测试常量 (单调 4 ts)
constexpr std::int64_t kEvt  = 1'700'000'000'000'000'000LL;  // event
constexpr std::int64_t kDs   = 1'700'000'000'001'000'000LL;  // data_source
constexpr std::int64_t kIng  = 1'700'000'000'002'000'000LL;  // ingestion
constexpr std::int64_t kAsOf = 1'700'000'000'003'000'000LL;  // as_of

FourTs MakeValidTs() {
    FourTs ts;
    ts.event_ts_ns       = kEvt;
    ts.data_source_ts_ns = kDs;
    ts.ingestion_ts_ns   = kIng;
    ts.as_of_ts_ns       = kAsOf;
    ts.ds_origin         = DataSourceTsOrigin::PayloadScoresTs;
    return ts;
}

}  // namespace

// ============================================================
// T1: enum 闭合
// ============================================================
TEST(AdapterSchema_T1, InfoKindValues) {
    EXPECT_EQ(static_cast<uint8_t>(InfoKind::Scores), 0U);
    EXPECT_EQ(static_cast<uint8_t>(InfoKind::Stats),  1U);
    EXPECT_EQ(static_cast<uint8_t>(InfoKind::Events), 2U);
    EXPECT_EQ(static_cast<uint8_t>(InfoKind::Odds),   3U);
}

TEST(AdapterSchema_T1, VendorIdValues) {
    EXPECT_EQ(static_cast<uint8_t>(VendorId::Goalserve),  0U);
    EXPECT_EQ(static_cast<uint8_t>(VendorId::Sportradar), 1U);
    EXPECT_EQ(static_cast<uint8_t>(VendorId::Pinnacle),   2U);
}

TEST(AdapterSchema_T1, GameEventTypeValues) {
    EXPECT_EQ(static_cast<uint8_t>(GameEventType::Goal),        0U);
    EXPECT_EQ(static_cast<uint8_t>(GameEventType::YellowCard),  1U);
    EXPECT_EQ(static_cast<uint8_t>(GameEventType::YellowRed),   2U);
    EXPECT_EQ(static_cast<uint8_t>(GameEventType::RedCard),     3U);
    EXPECT_EQ(static_cast<uint8_t>(GameEventType::Substitution),4U);
    EXPECT_EQ(static_cast<uint8_t>(GameEventType::ShotOnTarget),5U);
    EXPECT_EQ(static_cast<uint8_t>(GameEventType::Other),     255U);
}

TEST(AdapterSchema_T1, OddsSourceValues) {
    EXPECT_EQ(static_cast<uint8_t>(OddsSource::InplayGz),    0U);
    EXPECT_EQ(static_cast<uint8_t>(OddsSource::PregameOdds), 1U);
    EXPECT_EQ(static_cast<uint8_t>(OddsSource::RacingUk),    2U);
}

// ============================================================
// T2: R-20 四时间戳契约
// ============================================================
TEST(AdapterSchema_T2, GameScoreRecordR20Compliant) {
    GameScoreRecord r;
    r.ts = MakeValidTs();
    r.match_id.vendor_match_id = "6921246";
    r.home_team  = "Chelsea";
    r.away_team  = "Arsenal";
    r.home_score_total = 1;
    r.away_score_total = 0;
    EXPECT_TRUE(r.RespectsR20());
}

TEST(AdapterSchema_T2, GameScoreRecordIngestionFallbackFails) {
    GameScoreRecord r;
    r.ts = MakeValidTs();
    r.ts.ds_origin = DataSourceTsOrigin::IngestionFallback;
    EXPECT_FALSE(r.RespectsR20()) << "IngestionFallback 违反 R-20";
}

TEST(AdapterSchema_T2, OddsQuoteRecordR20Monotonic) {
    OddsQuoteRecord q;
    q.ts = MakeValidTs();
    q.bookmaker_name = "bet365";
    q.market_id      = "1";
    q.outcome        = "Home";
    q.value_eu       = 2.10;
    q.suspended      = false;
    EXPECT_TRUE(q.RespectsR20());
    EXPECT_TRUE(q.IsValid());
}

TEST(AdapterSchema_T2, GameStatsRecordR20Compliant) {
    GameStatsRecord s;
    s.ts = MakeValidTs();
    s.home_stats.shots_total = 8;
    s.away_stats.shots_total = 5;
    EXPECT_TRUE(s.RespectsR20());
}

TEST(AdapterSchema_T2, GameEventRecordR20Compliant) {
    GameEventRecord e;
    e.ts = MakeValidTs();
    e.event_type  = GameEventType::Goal;
    e.team_side   = "home";
    e.player_name = "Salah";
    e.minute      = 73;
    EXPECT_TRUE(e.RespectsR20());
}

TEST(AdapterSchema_T2, R20ViolationDataSourceBeforeEvent) {
    FourTs ts;
    ts.event_ts_ns       = kDs;      // event 比 ds 晚 → 违反
    ts.data_source_ts_ns = kEvt;
    ts.ingestion_ts_ns   = kIng;
    ts.as_of_ts_ns       = kAsOf;
    ts.ds_origin         = DataSourceTsOrigin::PayloadScoresTs;
    EXPECT_FALSE(ts.IsMonotonic());

    GameScoreRecord r;
    r.ts = ts;
    EXPECT_FALSE(r.RespectsR20());
}

// ============================================================
// T3: 时间戳转换 helper
// ============================================================
TEST(AdapterSchema_T3, InplayTsMsToNsExact) {
    // inplay updated_ts 是 Unix ms (毫秒级, 典型值是 1.7e12 量级)
    EXPECT_EQ(InplayTsMsToNs(0LL),                    0LL);
    EXPECT_EQ(InplayTsMsToNs(1LL),                    1'000'000LL);
    // 1732000000000 ms = 2024-11-19 04:26:40 UTC (来自 goalserve_stub.cpp kMockScoresTsMs)
    EXPECT_EQ(InplayTsMsToNs(1'732'000'000'000LL), 1'732'000'000'000'000'000LL);
}

TEST(AdapterSchema_T3, PregameTsSecToNsExact) {
    // pregame @ts 是 Unix sec
    EXPECT_EQ(PregameTsSecToNs(0LL),    0LL);
    EXPECT_EQ(PregameTsSecToNs(1LL),    1'000'000'000LL);
    EXPECT_EQ(PregameTsSecToNs(1779925037LL), 1'779'925'037'000'000'000LL);
}

TEST(AdapterSchema_T3, NetTicksToEpochNsKnownValue) {
    // 从 docs/RESEARCH/xiaoduan-w8-goalserve-data-structure-ssot-v1.md §3.1:
    //   updated = "635712985484037849" → 这是 2014-09-30 附近 (示例值)
    // 公式验证: (ticks - 621355968000000000) / 10_000_000 = unix_sec
    // 用一个确定的值: 2026-01-01 00:00:00 UTC
    //   unix_sec = 1735689600
    //   ticks = 1735689600 * 10_000_000 + 621355968000000000
    //         = 17356896000000000 + 621355968000000000 = 638712864000000000
    constexpr std::int64_t ticks_2026_01_01 = 638'712'864'000'000'000LL;
    constexpr std::int64_t expected_ns      = 1'735'689'600LL * 1'000'000'000LL;
    EXPECT_EQ(NetTicksToEpochNs(ticks_2026_01_01), expected_ns);
}

TEST(AdapterSchema_T3, NetTicksZeroOrBelowEpochReturnsZero) {
    EXPECT_EQ(NetTicksToEpochNs(0LL),   0LL);
    EXPECT_EQ(NetTicksToEpochNs(621'355'968'000'000'000LL), 0LL)  // == epoch offset → 0
        << "ticks == epoch_offset 应返回 0 (unix_sec=0)";
    EXPECT_EQ(NetTicksToEpochNs(100LL), 0LL);  // 远小于 epoch offset
}

// ============================================================
// T4: OddsQuoteRecord.IsValid()
// ============================================================
TEST(AdapterSchema_T4, IsValidPass) {
    OddsQuoteRecord q;
    q.ts         = MakeValidTs();
    q.value_eu   = 1.95;
    q.suspended  = false;
    EXPECT_TRUE(q.IsValid());
}

TEST(AdapterSchema_T4, IsValidFailsOddsLeqOne) {
    OddsQuoteRecord q;
    q.ts        = MakeValidTs();
    q.value_eu  = 1.0;   // 必须 > 1.0
    q.suspended = false;
    EXPECT_FALSE(q.IsValid());

    q.value_eu = 0.0;
    EXPECT_FALSE(q.IsValid());
}

TEST(AdapterSchema_T4, IsValidFailsSuspended) {
    OddsQuoteRecord q;
    q.ts        = MakeValidTs();
    q.value_eu  = 2.50;
    q.suspended = true;   // 暂停 → 无效
    EXPECT_FALSE(q.IsValid());
}

TEST(AdapterSchema_T4, IsValidFailsIngestionFallback) {
    OddsQuoteRecord q;
    q.ts              = MakeValidTs();
    q.ts.ds_origin    = DataSourceTsOrigin::IngestionFallback;
    q.value_eu        = 2.50;
    q.suspended       = false;
    EXPECT_FALSE(q.IsValid()) << "IngestionFallback 违反 R-20 → IsValid=false";
}

// ============================================================
// T5: MapGoalserveEventType
// ============================================================
TEST(AdapterSchema_T5, GoalAndCardMapping) {
    EXPECT_EQ(MapGoalserveEventType("goal"),       GameEventType::Goal);
    EXPECT_EQ(MapGoalserveEventType("yellowcard"), GameEventType::YellowCard);
    EXPECT_EQ(MapGoalserveEventType("yellowred"),  GameEventType::YellowRed);
    EXPECT_EQ(MapGoalserveEventType("redcard"),    GameEventType::RedCard);
}

TEST(AdapterSchema_T5, SubstAndShotsMapping) {
    EXPECT_EQ(MapGoalserveEventType("subst"),           GameEventType::Substitution);
    EXPECT_EQ(MapGoalserveEventType("Shot On Target"),  GameEventType::ShotOnTarget);
    EXPECT_EQ(MapGoalserveEventType("Shot Off Target"), GameEventType::ShotOffTarget);
    EXPECT_EQ(MapGoalserveEventType("Corner"),          GameEventType::Corner);
    EXPECT_EQ(MapGoalserveEventType("Offside"),         GameEventType::Offside);
    EXPECT_EQ(MapGoalserveEventType("Penalty"),         GameEventType::Penalty);
}

TEST(AdapterSchema_T5, VARVariants) {
    EXPECT_EQ(MapGoalserveEventType("VAR – Referee decision cancelled"),  GameEventType::VAR);
    EXPECT_EQ(MapGoalserveEventType("VAR – Referee decision confirmed"),  GameEventType::VAR);
    EXPECT_EQ(MapGoalserveEventType("VAR – Card reviewed"),               GameEventType::VAR);
}

TEST(AdapterSchema_T5, UnknownMapsToOther) {
    EXPECT_EQ(MapGoalserveEventType(""),             GameEventType::Other);
    EXPECT_EQ(MapGoalserveEventType("bogus event"),  GameEventType::Other);
    EXPECT_EQ(MapGoalserveEventType("Delay in match"), GameEventType::Other);
}

// ============================================================
// T6: AdaptBatch 四类 record 共存 + data_source_ts 单调
// ============================================================
TEST(AdapterSchema_T6, AdaptBatchFourKindsCoexist) {
    AdaptBatch batch;
    batch.data_source_ts_ns = kDs;
    batch.ingestion_ts_ns   = kIng;
    batch.vendor            = VendorId::Goalserve;
    batch.endpoint_path     = GoalserveEndpointPath::InplayGz;

    // 塞入各类 record
    {
        GameScoreRecord r;
        r.ts = MakeValidTs();
        r.match_id.vendor_match_id = "134181543";  // inplay id 格式
        r.home_score_total = 1;
        r.away_score_total = 0;
        batch.scores.push_back(r);
    }
    {
        GameStatsRecord s;
        s.ts = MakeValidTs();
        s.match_id.vendor_match_id = "134181543";
        s.home_stats.possession_pct = 60;
        batch.stats.push_back(s);
    }
    {
        GameEventRecord e;
        e.ts = MakeValidTs();
        e.match_id.vendor_match_id = "134181543";
        e.event_type = GameEventType::Goal;
        e.team_side  = "home";
        e.minute     = 73;
        batch.events.push_back(e);
    }
    {
        OddsQuoteRecord q;
        q.ts              = MakeValidTs();
        q.match_id.vendor_match_id = "134181543";
        q.source          = OddsSource::InplayGz;
        q.bookmaker_name  = "bet365";
        q.market_id       = "1";
        q.market_name     = "1X2 (Full Time)";
        q.outcome         = "Home";
        q.value_eu        = 8.5;
        q.suspended       = false;
        batch.odds.push_back(q);
    }

    EXPECT_EQ(batch.scores.size(), 1U);
    EXPECT_EQ(batch.stats.size(),  1U);
    EXPECT_EQ(batch.events.size(), 1U);
    EXPECT_EQ(batch.odds.size(),   1U);

    // batch.data_source_ts_ns <= batch.ingestion_ts_ns (R-20)
    EXPECT_LE(batch.data_source_ts_ns, batch.ingestion_ts_ns);
}

TEST(AdapterSchema_T6, AdaptBatchVendorAndEndpointCorrect) {
    AdaptBatch batch;
    batch.vendor        = VendorId::Goalserve;
    batch.endpoint_path = GoalserveEndpointPath::PregameOdds;
    EXPECT_EQ(batch.vendor,        VendorId::Goalserve);
    EXPECT_EQ(batch.endpoint_path, GoalserveEndpointPath::PregameOdds);
}

// ============================================================
// T7: 3-way soccer odds — Home/Draw/Away 三个独立 OddsQuoteRecord
//
// 验证: 三个 outcome 各产生独立 record, de-vig 需在 strategy 层联合处理
// ============================================================
TEST(AdapterSchema_T7, SoccerThreeWayOddsIndependentRecords) {
    // 模拟 Carolina Hurricanes vs Montreal Canadiens hockey 1x2 (3-way)
    // (soccer 同理 — 同一 market_id, 三个 outcome)
    const std::string market_id   = "1";
    const std::string market_name = "1X2 (Full Time)";
    const std::string match_id_str = "134181543";

    auto make_quote = [&](const std::string& outcome, double eu) {
        OddsQuoteRecord q;
        q.ts              = MakeValidTs();
        q.match_id.vendor_match_id = match_id_str;
        q.source          = OddsSource::PregameOdds;
        q.bookmaker_id    = 16;   // bet365
        q.bookmaker_name  = "bet365";
        q.market_id       = market_id;
        q.market_name     = market_name;
        q.outcome         = outcome;
        q.value_eu        = eu;
        q.suspended       = false;
        return q;
    };

    std::vector<OddsQuoteRecord> three_way{
        make_quote("Home", 2.10),
        make_quote("Draw", 3.40),
        make_quote("Away", 3.75),
    };

    EXPECT_EQ(three_way.size(), 3U);
    for (const auto& q : three_way) {
        EXPECT_TRUE(q.IsValid())      << "outcome=" << q.outcome;
        EXPECT_EQ(q.market_id, "1")   << "同一 market_id";
        EXPECT_TRUE(q.RespectsR20())  << "outcome=" << q.outcome;
    }

    // implied prob overround (fair p: de-vig 在 strategy 层)
    double sum_implied = 0.0;
    for (const auto& q : three_way) {
        sum_implied += 1.0 / q.value_eu;
    }
    EXPECT_GT(sum_implied, 1.0) << "overround > 1 是正常的 (bookmaker margin)";
    EXPECT_LT(sum_implied, 1.20) << "overround < 1.20 是合理范围";
}

// ============================================================
// T8: .NET ticks 边界
// ============================================================
TEST(AdapterSchema_T8, NetTicksEqualEpochReturnsZero) {
    // epoch offset 本身 → unix_sec = 0 → ns = 0
    constexpr std::int64_t epoch_offset = 621'355'968'000'000'000LL;
    EXPECT_EQ(NetTicksToEpochNs(epoch_offset), 0LL);
}

TEST(AdapterSchema_T8, NetTicksNegativeReturnsZero) {
    EXPECT_EQ(NetTicksToEpochNs(-1LL), 0LL);
    EXPECT_EQ(NetTicksToEpochNs(std::int64_t{-1'000'000}), 0LL);
}

TEST(AdapterSchema_T8, NetTicksTypicalValue) {
    // 实际 sample 来自 docs/RESEARCH/xiaoduan-w8-goalserve-data-structure-ssot-v1.md §3.1:
    //   updated = "635712985484037849"
    // 转换: (635712985484037849 - 621355968000000000) / 10_000_000 = 1435701748 sec (2015-07-01 附近)
    constexpr std::int64_t ticks = 635'712'985'484'037'849LL;
    constexpr std::int64_t epoch_offset = 621'355'968'000'000'000LL;
    constexpr std::int64_t expected_sec = (ticks - epoch_offset) / 10'000'000LL;
    const std::int64_t result_ns = NetTicksToEpochNs(ticks);
    EXPECT_EQ(result_ns / 1'000'000'000LL, expected_sec);
    EXPECT_GT(result_ns, 0LL);
}
