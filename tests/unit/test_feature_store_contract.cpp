// tests/unit/test_feature_store_contract.cpp — Feature Store Contract ABI lock
//
// Owner: 小田 (dwh-analyst, #24)
// last_review: 2026-05-29
//
// 目的:
//   验证 feature_store_contract.hpp ABI 约束, 对齐消费契约 (小段/小冯 adapter 接口).
//
// 测试覆盖:
//   T1: FeatureStoreGameRow R-20 4 ts chain ok / violation
//   T2: FeatureStoreGameRow PIT ok / violation (as_of_ts <= now_ns)
//   T3: FeatureStoreGameRow from_game_record() — 4 ts 透传 + event_date 计算
//   T4: FeatureStoreGameRow bookmaker slots NaN 语义 (is_present / valid_bm_count)
//   T5: FeatureStoreBookRow R-20 4 ts chain + PIT
//   T6: FeatureStoreBookRow level_valid NaN 语义 (空 level != 0)
//   T7: validate_game_row 全路径 (valid / ts_violation / pit_violation / empty match_id)
//   T8: validate_book_row 全路径 (valid / ts_violation / pit_violation / bad token_side)
//   T9: kFeatureStoreSchemaVersion ABI 字符串稳定
//   T10: kOrderBookLevels / kNumBookmakers compile-time ABI 锁
//
// ADR-010 §2.2: grandfather warnings 与其他 unit test 统一风格

#include <cmath>
#include <limits>
#include <string>

#include <gtest/gtest.h>

#include "stcpp/data/data_contract.hpp"
#include "stcpp/data/feature_store_contract.hpp"
#include "stcpp/data/goalserve_record.hpp"

using namespace stcpp::data::feature_store;
using namespace stcpp::data::goalserve;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static constexpr std::int64_t kBaseEventTs = 1'700'000'000'000'000'000LL;           // ns
static constexpr std::int64_t kBaseDataSourceTs = kBaseEventTs + 1'000'000'000LL;   // +1s
static constexpr std::int64_t kBaseIngestionTs = kBaseDataSourceTs + 50'000'000LL;  // +50ms
static constexpr std::int64_t kBaseAsOfTs = kBaseIngestionTs + 5'000'000LL;         // +5ms
static constexpr std::int64_t kNowNs = kBaseAsOfTs + 1'000'000LL;                   // slightly after

static FeatureStoreGameRow make_valid_game_row() {
    FeatureStoreGameRow row;
    row.event_ts_ns = kBaseEventTs;
    row.data_source_ts_ns = kBaseDataSourceTs;
    row.ingestion_ts_ns = kBaseIngestionTs;
    row.as_of_ts_ns = kBaseAsOfTs;
    row.ds_origin = DataSourceTsOrigin::PayloadScoresTs;
    row.sport = "Soccer";
    row.event_date_epoch_days = 19681;  // 2023-11-15 approx
    row.market_type = "Moneyline";
    row.match_id = "match_001";
    row.league_id = "league_42";
    row.home_team = "TeamA";
    row.away_team = "TeamB";
    row.score_home_total = 1;
    row.score_away_total = 0;
    row.time_status = TimeStatus::InPlay;
    row.period = 2;
    row.elapsed_sec = 1500;
    return row;
}

static FeatureStoreBookRow make_valid_book_row() {
    FeatureStoreBookRow row;
    row.event_ts_ns = kBaseEventTs;
    row.data_source_ts_ns = kBaseDataSourceTs;
    row.ingestion_ts_ns = kBaseIngestionTs;
    row.as_of_ts_ns = kBaseAsOfTs;
    row.sport = "Soccer";
    row.event_date_epoch_days = 19681;
    row.market_type = "Moneyline";
    row.market_id = "0xdeadbeef";
    row.token_side = "YES";
    // 5 levels bid
    row.bid_price = {0.52, 0.51, 0.50, 0.49, 0.48};
    row.bid_size_usdc = {1000.0, 500.0, 300.0, 200.0, 100.0};
    // 5 levels ask
    row.ask_price = {0.54, 0.55, 0.56, 0.57, 0.58};
    row.ask_size_usdc = {800.0, 400.0, 250.0, 150.0, 80.0};
    row.mid = 0.53;
    row.spread_bps_f = 377.4;
    row.top3_depth_usdc = 3050.0;
    row.tick_size = 0.01;
    row.microprice = 0.529;
    row.imbalance = 0.11;
    row.last_trade_ts_ns = kBaseEventTs - 500'000'000LL;
    return row;
}

// ---------------------------------------------------------------------------
// T1: FeatureStoreGameRow R-20 4 ts chain
// ---------------------------------------------------------------------------
TEST(FeatureStoreContract, T1_GameRow_TsChain_OkAndViolation) {
    auto row = make_valid_game_row();
    EXPECT_TRUE(row.ts_chain_ok());

    // violation: data_source_ts < event_ts
    auto bad = row;
    bad.data_source_ts_ns = bad.event_ts_ns - 1;
    EXPECT_FALSE(bad.ts_chain_ok());

    // violation: ingestion_ts < data_source_ts
    auto bad2 = row;
    bad2.ingestion_ts_ns = bad2.data_source_ts_ns - 1;
    EXPECT_FALSE(bad2.ts_chain_ok());

    // violation: as_of_ts < ingestion_ts
    auto bad3 = row;
    bad3.as_of_ts_ns = bad3.ingestion_ts_ns - 1;
    EXPECT_FALSE(bad3.ts_chain_ok());

    // violation: event_ts = 0
    auto bad4 = row;
    bad4.event_ts_ns = 0;
    EXPECT_FALSE(bad4.ts_chain_ok());
}

// ---------------------------------------------------------------------------
// T2: FeatureStoreGameRow PIT ok / violation
// ---------------------------------------------------------------------------
TEST(FeatureStoreContract, T2_GameRow_PIT_OkAndViolation) {
    auto row = make_valid_game_row();

    // pit_ok: as_of_ts <= now
    EXPECT_TRUE(row.pit_ok(kNowNs));

    // pit violation: as_of_ts > now_ns
    EXPECT_FALSE(row.pit_ok(kBaseAsOfTs - 1));

    // pit ok: as_of_ts == now_ns (exact equality is ok)
    EXPECT_TRUE(row.pit_ok(kBaseAsOfTs));
}

// ---------------------------------------------------------------------------
// T3: FeatureStoreGameRow from_game_record() — 4 ts 透传 + event_date 计算
// ---------------------------------------------------------------------------
TEST(FeatureStoreContract, T3_GameRow_FromGameRecord_TsPassthrough) {
    GameRecord gr;
    gr.ts.event_ts_ns = kBaseEventTs;
    gr.ts.data_source_ts_ns = kBaseDataSourceTs;
    gr.ts.ingestion_ts_ns = kBaseIngestionTs;
    gr.ts.as_of_ts_ns = kBaseAsOfTs;
    gr.ts.ds_origin = DataSourceTsOrigin::PayloadScoresTs;
    gr.sport = GoalserveSport::Soccer;
    gr.match_id = "gs_001";
    gr.league_id = "la_liga";
    gr.home_team = "Real";
    gr.away_team = "Barca";
    gr.score.home_total = 2;
    gr.score.away_total = 1;
    gr.status = TimeStatus::InPlay;
    gr.period = std::uint8_t{2};
    gr.elapsed_sec = std::int32_t{3600};
    // scheduled_ts_ns: 2024-11-15 00:00:00 UTC ≈ 1731628800 * 1e9
    gr.scheduled_ts_ns = std::int64_t{1'731'628'800'000'000'000LL};

    auto row = FeatureStoreGameRow::from_game_record(gr, "Moneyline");

    // 4 ts 透传
    EXPECT_EQ(row.event_ts_ns, kBaseEventTs);
    EXPECT_EQ(row.data_source_ts_ns, kBaseDataSourceTs);
    EXPECT_EQ(row.ingestion_ts_ns, kBaseIngestionTs);
    EXPECT_EQ(row.as_of_ts_ns, kBaseAsOfTs);
    EXPECT_EQ(row.ds_origin, DataSourceTsOrigin::PayloadScoresTs);

    // event_date: 1731628800e9 / (86400 * 1e9) = 20051 (days)
    EXPECT_EQ(row.event_date_epoch_days,
              static_cast<std::int32_t>(1'731'628'800'000'000'000LL / (86400LL * 1'000'000'000LL)));

    // 业务字段
    EXPECT_EQ(row.match_id, "gs_001");
    EXPECT_EQ(row.home_team, "Real");
    EXPECT_EQ(row.away_team, "Barca");
    EXPECT_EQ(row.score_home_total, 2);
    EXPECT_EQ(row.score_away_total, 1);
    EXPECT_EQ(row.time_status, TimeStatus::InPlay);
    EXPECT_EQ(row.period, static_cast<std::uint8_t>(2));
    EXPECT_EQ(row.elapsed_sec, 3600);

    // market_type
    EXPECT_EQ(row.market_type, "Moneyline");

    // ts chain ok after construction
    EXPECT_TRUE(row.ts_chain_ok());

    // as_of_ts override test
    const std::int64_t override_ts = kBaseAsOfTs + 999'999LL;
    auto row2 = FeatureStoreGameRow::from_game_record(gr, "Totals", override_ts);
    EXPECT_EQ(row2.as_of_ts_ns, override_ts);
}

// ---------------------------------------------------------------------------
// T4: Bookmaker slots NaN 语义 + valid_bm_count
// ---------------------------------------------------------------------------
TEST(FeatureStoreContract, T4_GameRow_BookmakerSlots_NaNSemantics) {
    auto row = make_valid_game_row();

    // 默认: 全 NaN / false
    EXPECT_EQ(row.valid_bm_count(), 0u);
    for (std::size_t i = 0; i < kNumBookmakers; ++i) {
        EXPECT_FALSE(row.bm_slots[i].is_present());
    }

    // 填 3 家
    row.bm_slots[0] = {1.85, 2.10, true};  // 10bet
    row.bm_slots[2] = {1.90, 2.00, true};  // bet365
    row.bm_slots[4] = {1.88, 2.05, true};  // unibet
    EXPECT_EQ(row.valid_bm_count(), 3u);
    EXPECT_TRUE(row.bm_slots[0].is_present());
    EXPECT_FALSE(row.bm_slots[1].is_present());  // williamhill NaN

    // kMinValidBookmakers = 3, exactly meets threshold
    EXPECT_GE(row.valid_bm_count(), stcpp::data::goalserve::kMinValidBookmakers);
}

// ---------------------------------------------------------------------------
// T5: FeatureStoreBookRow R-20 4 ts chain + PIT
// ---------------------------------------------------------------------------
TEST(FeatureStoreContract, T5_BookRow_TsChainAndPIT) {
    auto row = make_valid_book_row();
    EXPECT_TRUE(row.ts_chain_ok());
    EXPECT_TRUE(row.pit_ok(kNowNs));

    // violation: ingestion_ts < data_source_ts
    auto bad = row;
    bad.ingestion_ts_ns = bad.data_source_ts_ns - 1;
    EXPECT_FALSE(bad.ts_chain_ok());

    // pit violation
    EXPECT_FALSE(row.pit_ok(kBaseAsOfTs - 1));
}

// ---------------------------------------------------------------------------
// T6: FeatureStoreBookRow level_valid NaN 语义
// ---------------------------------------------------------------------------
TEST(FeatureStoreContract, T6_BookRow_LevelValid_NaNSemantics) {
    auto row = make_valid_book_row();

    // level 0 valid
    EXPECT_TRUE(row.bid_level_valid(0));
    EXPECT_TRUE(row.ask_level_valid(0));

    // best bid/ask
    EXPECT_DOUBLE_EQ(row.best_bid(), 0.52);
    EXPECT_DOUBLE_EQ(row.best_ask(), 0.54);

    // Make level 2 invalid via NaN
    const double nan_val = std::numeric_limits<double>::quiet_NaN();
    row.bid_price[2] = nan_val;
    row.bid_size_usdc[2] = nan_val;
    EXPECT_FALSE(row.bid_level_valid(2));
    EXPECT_TRUE(row.bid_level_valid(1));  // level 1 still valid

    // out-of-bounds
    EXPECT_FALSE(row.bid_level_valid(kOrderBookLevels));
    EXPECT_FALSE(row.ask_level_valid(kOrderBookLevels + 1));
}

// ---------------------------------------------------------------------------
// T7: validate_game_row 全路径
// ---------------------------------------------------------------------------
TEST(FeatureStoreContract, T7_ValidateGameRow_FullPaths) {
    const std::int64_t now = kNowNs;

    // happy path
    {
        auto row = make_valid_game_row();
        auto r = validate_game_row(row, now);
        EXPECT_TRUE(r.valid);
        EXPECT_TRUE(r.ts_chain_ok);
        EXPECT_TRUE(r.pit_ok);
        EXPECT_TRUE(r.ds_origin_upstream);
        EXPECT_EQ(r.error_msg, "");
    }

    // ts chain violation
    {
        auto row = make_valid_game_row();
        row.data_source_ts_ns = row.event_ts_ns - 1;
        auto r = validate_game_row(row, now);
        EXPECT_FALSE(r.valid);
        EXPECT_FALSE(r.ts_chain_ok);
        EXPECT_NE(r.error_msg, "");
    }

    // PIT violation
    {
        auto row = make_valid_game_row();
        auto r = validate_game_row(row, row.as_of_ts_ns - 1);
        EXPECT_FALSE(r.valid);
        EXPECT_TRUE(r.ts_chain_ok);
        EXPECT_FALSE(r.pit_ok);
    }

    // empty match_id
    {
        auto row = make_valid_game_row();
        row.match_id = "";
        auto r = validate_game_row(row, now);
        EXPECT_FALSE(r.valid);
        EXPECT_TRUE(r.pit_ok);
        EXPECT_NE(r.error_msg, "");
    }

    // empty sport
    {
        auto row = make_valid_game_row();
        row.sport = "";
        auto r = validate_game_row(row, now);
        EXPECT_FALSE(r.valid);
    }

    // IngestionFallback origin — valid=true but ds_origin_upstream=false
    {
        auto row = make_valid_game_row();
        row.ds_origin = DataSourceTsOrigin::IngestionFallback;
        auto r = validate_game_row(row, now);
        EXPECT_TRUE(r.valid);  // 仍可写入 (带告警, R-20 文档说 "允许带 flag")
        EXPECT_FALSE(r.ds_origin_upstream);
    }
}

// ---------------------------------------------------------------------------
// T8: validate_book_row 全路径
// ---------------------------------------------------------------------------
TEST(FeatureStoreContract, T8_ValidateBookRow_FullPaths) {
    const std::int64_t now = kNowNs;

    // happy path YES
    {
        auto row = make_valid_book_row();
        auto r = validate_book_row(row, now);
        EXPECT_TRUE(r.valid);
        EXPECT_TRUE(r.ts_chain_ok);
        EXPECT_TRUE(r.pit_ok);
    }

    // happy path NO
    {
        auto row = make_valid_book_row();
        row.token_side = "NO";
        auto r = validate_book_row(row, now);
        EXPECT_TRUE(r.valid);
    }

    // ts chain violation
    {
        auto row = make_valid_book_row();
        row.ingestion_ts_ns = row.data_source_ts_ns - 1;
        auto r = validate_book_row(row, now);
        EXPECT_FALSE(r.valid);
        EXPECT_FALSE(r.ts_chain_ok);
    }

    // PIT violation
    {
        auto row = make_valid_book_row();
        auto r = validate_book_row(row, row.as_of_ts_ns - 1);
        EXPECT_FALSE(r.valid);
        EXPECT_FALSE(r.pit_ok);
    }

    // empty market_id
    {
        auto row = make_valid_book_row();
        row.market_id = "";
        auto r = validate_book_row(row, now);
        EXPECT_FALSE(r.valid);
    }

    // bad token_side ("yes" lowercase → reject)
    {
        auto row = make_valid_book_row();
        row.token_side = "yes";
        auto r = validate_book_row(row, now);
        EXPECT_FALSE(r.valid);
        EXPECT_NE(r.error_msg, "");
    }

    // bad token_side ("BOTH" → reject)
    {
        auto row = make_valid_book_row();
        row.token_side = "BOTH";
        auto r = validate_book_row(row, now);
        EXPECT_FALSE(r.valid);
    }
}

// ---------------------------------------------------------------------------
// T9: kFeatureStoreSchemaVersion ABI 字符串稳定
// ---------------------------------------------------------------------------
TEST(FeatureStoreContract, T9_SchemaVersion_ABI_Stable) {
    EXPECT_EQ(kFeatureStoreSchemaVersion, "fs-schema-v1.0");

    // GameRow 和 BookRow 都携带 schema_version 字段
    auto gr = make_valid_game_row();
    EXPECT_EQ(gr.schema_version, "fs-schema-v1.0");

    auto br = make_valid_book_row();
    EXPECT_EQ(br.schema_version, "fs-schema-v1.0");
}

// ---------------------------------------------------------------------------
// T10: kOrderBookLevels / kNumBookmakers 编译期 ABI 锁
// ---------------------------------------------------------------------------
TEST(FeatureStoreContract, T10_CompileTime_ABI_Constants) {
    // 与 orderbook.hpp kBookDepthLevels = 5 对齐
    EXPECT_EQ(kOrderBookLevels, 5u);

    // 与 data_contract.hpp kNumBookmakers = 8 对齐
    EXPECT_EQ(stcpp::data::goalserve::kNumBookmakers, 8u);

    // GameRow bm_slots 大小
    auto row = make_valid_game_row();
    EXPECT_EQ(row.bm_slots.size(), kNumBookmakers);

    // BookRow bid/ask level 大小
    auto br = make_valid_book_row();
    EXPECT_EQ(br.bid_price.size(), kOrderBookLevels);
    EXPECT_EQ(br.bid_size_usdc.size(), kOrderBookLevels);
    EXPECT_EQ(br.ask_price.size(), kOrderBookLevels);
    EXPECT_EQ(br.ask_size_usdc.size(), kOrderBookLevels);
}
