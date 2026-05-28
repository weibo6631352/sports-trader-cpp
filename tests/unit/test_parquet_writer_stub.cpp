// tests/unit/test_parquet_writer_stub.cpp — ParquetBatchWriter stub ABI lock
//
// Owner: 小田 (dwh-analyst, #24)
// last_review: 2026-05-28
// Wave: W6 Wave 30
//
// 目的:
//   - 不测 Parquet 真写入 (stub only)
//   - ABI lock: W7+ HC-06 真接时接口签名不变
//   - 验证与 FeatureSnapshot / TrainingLabel / MultiBookOddsRecord 兼容
//   - R-20 4 ts chain check enforce
//   - FlushResult::StubNotImplemented 占位正确
//
// ADR-010 §2.2: grandfather warnings 保持与 unit test 统一风格

#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <string>

#include "stcpp/data/parquet_writer.hpp"
#include "stcpp/ml/feature_snapshot.hpp"
#include "stcpp/ml/training_label.hpp"
#include "stcpp/data/odds_record.hpp"
#include "stcpp/data/data_contract.hpp"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static stcpp::ml::FeatureSnapshot make_valid_snapshot() {
    stcpp::ml::FeatureSnapshot fs;
    fs.event_ts        = 1'700'000'000'000'000'000LL;
    fs.data_source_ts  = fs.event_ts  + 1'000'000'000LL;   // +1s
    fs.ingestion_ts    = fs.data_source_ts + 50'000'000LL;  // +50ms
    fs.as_of_ts        = fs.ingestion_ts   + 5'000'000LL;   // +5ms
    fs.feature_snapshot_id = 0xDEADBEEF12345678ULL;
    fs.signal_id_u8    = 1;
    // feat_04 = Goalserve_devig_p_yes_fair (ADR-008, must not be NaN)
    fs.set(stcpp::ml::FeatureName::Goalserve_devig_p_yes_fair, 0.55f);
    fs.set(stcpp::ml::FeatureName::edge_bps, 350.0f);
    fs.set(stcpp::ml::FeatureName::PM_mid_bid, 0.52f);
    fs.set(stcpp::ml::FeatureName::PM_mid_ask, 0.54f);
    return fs;
}

static stcpp::ml::TrainingLabel make_valid_label(std::uint64_t snapshot_id) {
    stcpp::ml::TrainingLabel tl;
    // label ts must be > feature as_of_ts + 30s
    tl.event_ts        = 1'700'000'000'000'000'000LL + 3'600'000'000'000LL;  // +1h
    tl.data_source_ts  = tl.event_ts + 1'000'000'000LL;
    tl.ingestion_ts    = tl.data_source_ts + 50'000'000LL;
    tl.as_of_ts        = tl.ingestion_ts   + 5'000'000LL;
    tl.feature_snapshot_id = snapshot_id;
    tl.decision_taken  = true;
    tl.executed        = true;
    tl.filled_price    = 0.52;
    tl.filled_size_usdc = 100.0;
    tl.settlement_outcome = stcpp::ml::SettlementOutcome::Win;
    tl.realized_pnl_usdc  = 23.5;
    return tl;
}

static stcpp::data::goalserve::MultiBookOddsRecord make_valid_odds_record() {
    stcpp::data::goalserve::MultiBookOddsRecord rec;
    rec.ts.event_ts_ns        = 1'700'000'000'000'000'000LL;
    rec.ts.data_source_ts_ns  = rec.ts.event_ts_ns + 1'000'000'000LL;
    rec.ts.ingestion_ts_ns    = rec.ts.data_source_ts_ns + 50'000'000LL;
    rec.ts.as_of_ts_ns        = rec.ts.ingestion_ts_ns + 5'000'000LL;
    rec.ts.ds_origin = stcpp::data::goalserve::DataSourceTsOrigin::PayloadScoresTs;
    rec.sport    = stcpp::data::goalserve::GoalserveSport::Soccer;
    rec.match_id = "gs_match_001";
    rec.market_id = "1";
    // Fill all 8 bookmaker slots with valid odds
    for (std::size_t i = 0; i < stcpp::data::goalserve::kNumBookmakers; ++i) {
        rec.slots[i] = {1.85, 2.10, true};
    }
    return rec;
}

// ---------------------------------------------------------------------------
// T1: ParquetBatchWriter 构造 + root_path ABI
// ---------------------------------------------------------------------------
TEST(ParquetWriterStub, T1_ConstructAndRootPath) {
    stcpp::data::ParquetBatchWriter writer("/tmp/test_parquet_stub", 4096);
    EXPECT_EQ(writer.root_path(), "/tmp/test_parquet_stub");
    EXPECT_EQ(writer.feature_buffer_size(), 0u);
    EXPECT_EQ(writer.odds_buffer_size(), 0u);
    EXPECT_EQ(writer.total_ts_violations(), 0u);
}

// ---------------------------------------------------------------------------
// T2: append FeatureSnapshot+TrainingLabel → buffer size +1, FlushResult::Ok
// ---------------------------------------------------------------------------
TEST(ParquetWriterStub, T2_AppendFeatureSnapshot_BufferIncrements) {
    stcpp::data::ParquetBatchWriter writer("/tmp/test_parquet_stub");
    auto fs = make_valid_snapshot();
    auto tl = make_valid_label(fs.feature_snapshot_id);

    auto res = writer.append(fs, tl, "Soccer", "Moneyline", 2024, 20);
    EXPECT_EQ(res, stcpp::data::FlushResult::Ok);
    EXPECT_EQ(writer.feature_buffer_size(), 1u);
    EXPECT_EQ(writer.total_ts_violations(), 0u);
}

// ---------------------------------------------------------------------------
// T3: R-20 violation on FeatureSnapshot → TsChainViolation, not buffered
// ---------------------------------------------------------------------------
TEST(ParquetWriterStub, T3_R20ViolationFeatureSnapshot_Rejected) {
    stcpp::data::ParquetBatchWriter writer("/tmp/test_parquet_stub");
    auto fs = make_valid_snapshot();
    auto tl = make_valid_label(fs.feature_snapshot_id);

    // Violate R-20: data_source_ts < event_ts
    fs.data_source_ts = fs.event_ts - 1;

    auto res = writer.append(fs, tl, "Soccer", "Moneyline", 2024, 20);
    EXPECT_EQ(res, stcpp::data::FlushResult::TsChainViolation);
    EXPECT_EQ(writer.feature_buffer_size(), 0u);
    EXPECT_EQ(writer.total_ts_violations(), 1u);
}

// ---------------------------------------------------------------------------
// T4: append MultiBookOddsRecord → FlushResult::Ok, odds buffer +1
// ---------------------------------------------------------------------------
TEST(ParquetWriterStub, T4_AppendMultiBookOdds_BufferIncrements) {
    stcpp::data::ParquetBatchWriter writer("/tmp/test_parquet_stub");
    auto rec = make_valid_odds_record();

    auto res = writer.append(rec, "Soccer", "Moneyline", 2024, 20);
    EXPECT_EQ(res, stcpp::data::FlushResult::Ok);
    EXPECT_EQ(writer.odds_buffer_size(), 1u);
    EXPECT_EQ(writer.total_ts_violations(), 0u);
}

// ---------------------------------------------------------------------------
// T5: flush_to_file stub → StubNotImplemented, buffer cleared
// ---------------------------------------------------------------------------
TEST(ParquetWriterStub, T5_FlushStub_ReturnsStubNotImplemented) {
    stcpp::data::ParquetBatchWriter writer("/tmp/test_parquet_stub");
    auto fs = make_valid_snapshot();
    auto tl = make_valid_label(fs.feature_snapshot_id);
    auto append_res = writer.append(fs, tl, "Soccer", "Moneyline", 2024, 20);
    EXPECT_EQ(append_res, stcpp::data::FlushResult::Ok);
    ASSERT_EQ(writer.feature_buffer_size(), 1u);

    auto res = writer.flush_to_file();
    EXPECT_EQ(res, stcpp::data::FlushResult::StubNotImplemented);
    EXPECT_EQ(writer.feature_buffer_size(), 0u);  // buffer cleared after stub flush
}

// ---------------------------------------------------------------------------
// T6: flush empty buffer → FlushResult::BufferEmpty
// ---------------------------------------------------------------------------
TEST(ParquetWriterStub, T6_FlushEmptyBuffer_ReturnsBufferEmpty) {
    stcpp::data::ParquetBatchWriter writer("/tmp/test_parquet_stub");
    auto res = writer.flush_to_file();
    EXPECT_EQ(res, stcpp::data::FlushResult::BufferEmpty);
}

// ---------------------------------------------------------------------------
// T7: PartitionKey to_path_fragment ABI (xiaotian-parquet-partition-v1.md §2.1)
// ---------------------------------------------------------------------------
TEST(ParquetWriterStub, T7_PartitionKeyPathFragment_Format) {
    stcpp::data::PartitionKey pk{"Soccer", "Moneyline", 2024, 20};
    const std::string frag = pk.to_path_fragment();
    EXPECT_EQ(frag, "sport=Soccer/market_type=Moneyline/year=2024/week=20");
}

// ---------------------------------------------------------------------------
// T8: FeatureSnapshotRecord from_snapshots — 4 ts 透传 + feat_04 ADR-008
// ---------------------------------------------------------------------------
TEST(ParquetWriterStub, T8_FeatureSnapshotRecord_FromSnapshots_TsAndFeat04) {
    auto fs = make_valid_snapshot();
    auto tl = make_valid_label(fs.feature_snapshot_id);

    auto rec = stcpp::data::FeatureSnapshotRecord::from_snapshots(
        fs, tl, "Basketball", "Totals", 2025, 5);

    // R-20 4 ts 透传
    EXPECT_EQ(rec.event_ts,       fs.event_ts);
    EXPECT_EQ(rec.data_source_ts, fs.data_source_ts);
    EXPECT_EQ(rec.ingestion_ts,   fs.ingestion_ts);
    EXPECT_EQ(rec.as_of_ts,       fs.as_of_ts);
    EXPECT_TRUE(rec.ts_chain_ok());

    // ADR-008 feat_04 = Goalserve_devig_p_yes_fair (index 4)
    EXPECT_FALSE(std::isnan(rec.feat[4]));
    EXPECT_FLOAT_EQ(rec.feat[4], 0.55f);

    // partition keys
    EXPECT_EQ(rec.sport,       "Basketball");
    EXPECT_EQ(rec.market_type, "Totals");
    EXPECT_EQ(rec.year, 2025);
    EXPECT_EQ(rec.week, 5);

    // TrainingLabel fields
    EXPECT_EQ(rec.settlement_outcome,
              static_cast<std::uint8_t>(stcpp::ml::SettlementOutcome::Win));
    EXPECT_DOUBLE_EQ(rec.realized_pnl_usdc, 23.5);
}

// ---------------------------------------------------------------------------
// T9: MultiBookOddsParquetRecord ABI — 8 bookmakers wide format
// ---------------------------------------------------------------------------
TEST(ParquetWriterStub, T9_MultiBookOddsParquetRecord_8BookmakerSlots) {
    auto rec = make_valid_odds_record();
    stcpp::data::MultiBookOddsParquetRecord pr;
    pr.event_ts       = rec.event_ts_ns();
    pr.data_source_ts = rec.data_source_ts_ns();
    pr.ingestion_ts   = rec.ingestion_ts_ns();
    pr.as_of_ts       = rec.as_of_ts_ns();
    pr.sport = "Soccer"; pr.market_type = "Moneyline"; pr.year = 2024; pr.week = 20;

    for (std::size_t i = 0; i < stcpp::data::goalserve::kNumBookmakers; ++i) {
        pr.yes_odds[i] = rec.slots[i].odds_yes;
        pr.no_odds[i]  = rec.slots[i].odds_no;
        pr.valid_bm[i] = rec.slots[i].valid;
    }

    EXPECT_TRUE(pr.ts_chain_ok());
    EXPECT_EQ(pr.valid_bookmaker_count(), 8u);
    // ABI: wide format has exactly 8 slots
    EXPECT_EQ(stcpp::data::goalserve::kNumBookmakers, 8u);
}

// ---------------------------------------------------------------------------
// T10: FlushResult to_string ABI (string repr 稳定)
// ---------------------------------------------------------------------------
TEST(ParquetWriterStub, T10_FlushResultToString_Stable) {
    EXPECT_EQ(stcpp::data::to_string(stcpp::data::FlushResult::Ok),
              "Ok");
    EXPECT_EQ(stcpp::data::to_string(stcpp::data::FlushResult::StubNotImplemented),
              "StubNotImplemented");
    EXPECT_EQ(stcpp::data::to_string(stcpp::data::FlushResult::TsChainViolation),
              "TsChainViolation");
    EXPECT_EQ(stcpp::data::to_string(stcpp::data::FlushResult::BufferEmpty),
              "BufferEmpty");
}
