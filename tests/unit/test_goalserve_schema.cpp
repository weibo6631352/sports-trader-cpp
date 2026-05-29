// tests/unit/test_goalserve_schema.cpp — Goalserve schema v0.1 单测 (W6 Wave 29)
//
// Owner: 小段 (goalserve-specialist, #37)
// Sprint-2 W6 Wave 29 — D-W5-03/05 schema 锁 + data-contract
//
// 与 test_goalserve_client.cpp 28 测试不重复:
//   T1: URL builder 8 sport × 5 host 40 combos (BuildUrlFromSchema 路径)
//   T2: TimeStatus 11 enum 闭合 (schema 层 kAllTimeStatus + 名称语义)
//   T3: validate_payload PM_WSS 字段校验 (R-20 PIT chain)
//   T4: validate_payload GS_OddsFeed 字段校验 (R-20 upstream / fallback)
//   T5: 8-9 家 bookmaker ABI 锁 (ADR-008, kBookmakerIds)
//   T6: 4 ts 字段位置正确性 (GetTsFieldSpec 各 PayloadType 不同)

#include <array>
#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include "stcpp/data/data_contract.hpp"
#include "stcpp/data/goalserve_schema.hpp"
#include "stcpp/data/odds_record.hpp"

using namespace stcpp::data::goalserve;

namespace {

constexpr std::int64_t kEvt = 1'700'000'000'000'000'000LL;
constexpr std::int64_t kDs = 1'700'000'000'001'000'000LL;
constexpr std::int64_t kIng = 1'700'000'000'002'000'000LL;
constexpr std::int64_t kAsOf = 1'700'000'000'003'000'000LL;

constexpr std::array<GoalserveHost, kNumHosts> kAllHosts{
    GoalserveHost::Www,       GoalserveHost::Inplay,        GoalserveHost::OddsFeed,
    GoalserveHost::LiveScore, GoalserveHost::InplayMapping,
};
constexpr std::array<GoalserveSport, kNumSports> kAllSports{
    GoalserveSport::Soccer,     GoalserveSport::Basketball,       GoalserveSport::Tennis,
    GoalserveSport::Volleyball, GoalserveSport::AmericanFootball, GoalserveSport::Esports,
    GoalserveSport::Hockey,     GoalserveSport::Baseball,
};

}  // namespace

// ============================================================
// T1: URL builder 40 combos — BuildUrlFromSchema 路径
// ============================================================
TEST(GoalserveSchemaT1, FortyCombosBuildUrlFromSchema) {
    std::size_t combos = 0;
    for (auto h : kAllHosts) {
        for (auto s : kAllSports) {
            GoalserveEndpoint ep = (h == GoalserveHost::Inplay) ? GoalserveEndpoint::InplayOdds
                                   : (h == GoalserveHost::InplayMapping)
                                       ? GoalserveEndpoint::InplayMapping
                                       : GoalserveEndpoint::PregameLiveScore;
            const std::string url = BuildUrlFromSchema(h, s, ep, "TESTKEY");
            EXPECT_FALSE(url.empty()) << "host=" << static_cast<int>(h) << " sport=" << static_cast<int>(s);
            const std::string_view base = HostBaseUrl(h);
            EXPECT_NE(url.find(base), std::string::npos) << "missing base: " << base;
            ++combos;
        }
    }
    EXPECT_EQ(combos, 40U);
}

TEST(GoalserveSchemaT1, InplaySlugsCorrect) {
    struct {
        GoalserveSport sport;
        std::string_view slug;
    } cases[]{
        {GoalserveSport::Basketball, "basket"},
        {GoalserveSport::AmericanFootball, "amfootball"},
        {GoalserveSport::Soccer, "soccer"},
    };
    for (const auto& [s, slug] : cases) {
        const std::string url = BuildUrlFromSchema(GoalserveHost::Inplay, s, GoalserveEndpoint::InplayOdds);
        EXPECT_NE(url.find(std::string("inplay-") + std::string(slug) + ".gz"), std::string::npos)
            << "slug wrong: " << slug;
    }
}

// ============================================================
// T2: TimeStatus 11 enum 闭合 (schema 层语义)
// ============================================================
TEST(GoalserveSchemaT2, ElevenStatusAllNamed) {
    EXPECT_EQ(kAllTimeStatus.size(), 11U);
    for (auto t : kAllTimeStatus) {
        EXPECT_NE(TimeStatusName(t), "Unknown") << "TimeStatus " << static_cast<int>(t) << " unnamed";
        EXPECT_FALSE(TimeStatusName(t).empty());
    }
}

TEST(GoalserveSchemaT2, ElevenValuesExact) {
    const std::array<std::uint8_t, 11> expected{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 99};
    std::array<std::uint8_t, 11> actual{};
    for (std::size_t i = 0; i < kAllTimeStatus.size(); ++i) {
        actual[i] = static_cast<std::uint8_t>(kAllTimeStatus[i]);
    }
    EXPECT_EQ(actual, expected);
}

// ============================================================
// T3: validate_payload PM_WSS (R-20 PIT chain)
// ============================================================
TEST(GoalserveSchemaT3, PmWssValidPasses) {
    auto r =
        validate_payload(kEvt, kDs, kIng, kAsOf, DataSourceTsOrigin::PayloadScoresTs, PayloadType::PM_WSS);
    EXPECT_TRUE(r.valid);
    EXPECT_TRUE(r.is_upstream_payload);
}

TEST(GoalserveSchemaT3, PmWssZeroEventTsRejected) {
    auto r =
        validate_payload(0LL, kDs, kIng, kAsOf, DataSourceTsOrigin::PayloadScoresTs, PayloadType::PM_WSS);
    EXPECT_FALSE(r.valid);
}

TEST(GoalserveSchemaT3, PmWssPitViolationIngestionBeforeDataSource) {
    auto r =
        validate_payload(kEvt, kDs, kDs - 1, kAsOf, DataSourceTsOrigin::PayloadScoresTs, PayloadType::PM_WSS);
    EXPECT_FALSE(r.valid);
}

TEST(GoalserveSchemaT3, PmWssIngestionFallbackNotUpstream) {
    auto r =
        validate_payload(kEvt, kDs, kIng, kAsOf, DataSourceTsOrigin::IngestionFallback, PayloadType::PM_WSS);
    EXPECT_TRUE(r.valid);
    EXPECT_FALSE(r.is_upstream_payload) << "IngestionFallback 应标记非 upstream";
}

// ============================================================
// T4: validate_payload GS_OddsFeed
// ============================================================
TEST(GoalserveSchemaT4, GsOddsFeedValidPasses) {
    auto r = validate_payload(kEvt, kDs, kIng, kAsOf, DataSourceTsOrigin::PayloadScoresTs,
                              PayloadType::GS_OddsFeed);
    EXPECT_TRUE(r.valid);
}

TEST(GoalserveSchemaT4, GsOddsFeedLastUpdateFallbackValid) {
    auto r = validate_payload(kEvt, kDs, kIng, kAsOf, DataSourceTsOrigin::PayloadLastUpdate,
                              PayloadType::GS_OddsFeed);
    EXPECT_TRUE(r.valid);
    EXPECT_TRUE(r.is_upstream_payload);
}

TEST(GoalserveSchemaT4, GsOddsFeedAsOfBeforeIngestionRejected) {
    auto r = validate_payload(kEvt, kDs, kIng, kIng - 1, DataSourceTsOrigin::PayloadScoresTs,
                              PayloadType::GS_OddsFeed);
    EXPECT_FALSE(r.valid);
}

TEST(GoalserveSchemaT4, GsOddsFeedDataSourceBeforeEventRejected) {
    auto r = validate_payload(kDs + 1, kDs, kIng, kAsOf, DataSourceTsOrigin::PayloadScoresTs,
                              PayloadType::GS_OddsFeed);
    EXPECT_FALSE(r.valid);
}

// ============================================================
// T5: bookmaker ABI 锁 (ADR-008, kBookmakerIds)
// ============================================================
TEST(GoalserveSchemaT5, AbiLockedEightBookmakers) {
    EXPECT_EQ(kNumBookmakers, 8U);
    EXPECT_EQ(kBookmakerIds.size(), 8U);
    EXPECT_EQ(kBookmakerNames.size(), 8U);
    EXPECT_EQ(kMinValidBookmakers, 3U);
}

TEST(GoalserveSchemaT5, AbiLockedIdValues) {
    EXPECT_EQ(kBookmakerIds[0], 14);
    EXPECT_EQ(kBookmakerIds[1], 15);
    EXPECT_EQ(kBookmakerIds[2], 16);
    EXPECT_EQ(kBookmakerIds[3], 17);
    EXPECT_EQ(kBookmakerIds[4], 18);
    EXPECT_EQ(kBookmakerIds[5], 65);
    EXPECT_EQ(kBookmakerIds[6], 105);
    EXPECT_EQ(kBookmakerIds[7], 144);
}

TEST(GoalserveSchemaT5, MultiBookOddsRecordSlotsAndR20) {
    MultiBookOddsRecord rec;
    EXPECT_EQ(rec.slots.size(), kNumBookmakers);
    EXPECT_EQ(rec.ValidBookmakerCount(), 0U);

    // 填满所有 slot
    for (auto& sl : rec.slots) {
        sl.odds_yes = 2.0;
        sl.odds_no = 1.9;
        sl.valid = true;
    }
    EXPECT_EQ(rec.ValidBookmakerCount(), kNumBookmakers);
}

TEST(GoalserveSchemaT5, MultiBookOddsRecordSerializeSize) {
    MultiBookOddsRecord rec;
    std::array<std::byte, MultiBookOddsRecord::max_serialized_size()> buf{};
    const std::size_t written = rec.serialize_into(std::span<std::byte>(buf));
    EXPECT_EQ(written, 248U);
}

// ============================================================
// T6: 4 ts 字段位置 per payload type
// ============================================================
TEST(GoalserveSchemaT6, PmWssPrimaryIsTsField) {
    const auto spec = GetTsFieldSpec(PayloadType::PM_WSS);
    EXPECT_EQ(spec.primary_field, "ts");
    EXPECT_EQ(spec.secondary_field, "timestamp");
}

TEST(GoalserveSchemaT6, GsOddsFeedPrimaryIsScoresTs) {
    const auto spec = GetTsFieldSpec(PayloadType::GS_OddsFeed);
    EXPECT_EQ(spec.primary_field, "scores@ts");
    EXPECT_EQ(spec.secondary_field, "match@last_update");
}

TEST(GoalserveSchemaT6, GsLiveScorePrimaryIsLastUpdate) {
    const auto spec = GetTsFieldSpec(PayloadType::GS_LiveScore);
    EXPECT_EQ(spec.primary_field, "match@last_update");
    EXPECT_EQ(spec.secondary_field, "http_date_header");
}

TEST(GoalserveSchemaT6, GsInplayPrimaryIsScoresTs) {
    const auto spec = GetTsFieldSpec(PayloadType::GS_Inplay);
    EXPECT_EQ(spec.primary_field, "scores@ts");
}

TEST(GoalserveSchemaT6, PmWssAndLiveScorePrimaryFieldsDiffer) {
    const auto pm = GetTsFieldSpec(PayloadType::PM_WSS);
    const auto ls = GetTsFieldSpec(PayloadType::GS_LiveScore);
    EXPECT_NE(pm.primary_field, ls.primary_field);
}

TEST(GoalserveSchemaT6, MultiBookOddsRecordR20Monotonic) {
    MultiBookOddsRecord rec;
    rec.ts.event_ts_ns = kEvt;
    rec.ts.data_source_ts_ns = kDs;
    rec.ts.ingestion_ts_ns = kIng;
    rec.ts.as_of_ts_ns = kAsOf;
    rec.ts.ds_origin = DataSourceTsOrigin::PayloadScoresTs;
    EXPECT_TRUE(rec.IsFourTsMonotonic());
    EXPECT_TRUE(rec.RespectsR20());

    rec.ts.ds_origin = DataSourceTsOrigin::IngestionFallback;
    EXPECT_FALSE(rec.RespectsR20()) << "IngestionFallback 违反 R-20";
}

TEST(GoalserveSchemaT6, MultiBookOddsRecordSerializeBufferTooSmall) {
    MultiBookOddsRecord rec;
    std::array<std::byte, 4> small{};
    EXPECT_EQ(rec.serialize_into(std::span<std::byte>(small)), 0U);
}
