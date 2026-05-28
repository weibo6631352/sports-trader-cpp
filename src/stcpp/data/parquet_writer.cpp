// stcpp/data/parquet_writer.cpp — ParquetBatchWriter stub impl (W6 Wave 30, 小田 DWH)
//
// Owner: 小田 (dwh-analyst, #24)
// last_review: 2026-05-28
//
// W6 stub 行为:
//   - append(): R-20 ts chain check → push_back to buffer
//   - flush_to_file(): 返回 StubNotImplemented (不写磁盘)
//
// W7+ HC-06 (ml-data-engineer 8/15 入职) 实施:
//   将此 .cpp 替换为真实 Apache Arrow 实现. 接口签名 (.hpp) 不变.
//   参考: xiaotian_parquet_export.ipynb Python 版本作为 spec ground truth.

#include "stcpp/data/parquet_writer.hpp"

#include <cstring>  // strnlen

namespace stcpp::data {

// ---------------------------------------------------------------------------
// append(FeatureSnapshot, TrainingLabel)
// ---------------------------------------------------------------------------
FlushResult ParquetBatchWriter::append(
    const ml::FeatureSnapshot& fs,
    const ml::TrainingLabel&   tl,
    std::string_view           sport,
    std::string_view           market_type,
    std::int32_t               year,
    std::int32_t               week)
{
    // R-20 check — 两端 ts chain 都必须满足
    if (!fs.ts_chain_ok() || !tl.ts_chain_ok()) {
        ++ts_violations_;
        return FlushResult::TsChainViolation;
    }

    feature_buf_.push_back(
        FeatureSnapshotRecord::from_snapshots(fs, tl, sport, market_type, year, week));

    // Auto-flush at capacity (stub: just clear — W7+ 真写磁盘)
    if (feature_buf_.size() >= buffer_capacity_) {
        return flush_to_file();
    }
    return FlushResult::Ok;
}

// ---------------------------------------------------------------------------
// append(MultiBookOddsRecord)
// ---------------------------------------------------------------------------
FlushResult ParquetBatchWriter::append(
    const data::goalserve::MultiBookOddsRecord& rec,
    std::string_view                            sport,
    std::string_view                            market_type,
    std::int32_t                                year,
    std::int32_t                                week)
{
    // R-20 check
    if (!rec.IsFourTsMonotonic()) {
        ++ts_violations_;
        return FlushResult::TsChainViolation;
    }

    MultiBookOddsParquetRecord r;
    r.event_ts       = rec.event_ts_ns();
    r.data_source_ts = rec.data_source_ts_ns();
    r.ingestion_ts   = rec.ingestion_ts_ns();
    r.as_of_ts       = rec.as_of_ts_ns();
    r.ds_origin      = static_cast<std::uint8_t>(rec.ts.ds_origin);
    r.sport          = std::string(sport);
    r.market_type    = std::string(market_type);
    r.year           = year;
    r.week           = week;
    r.match_id       = rec.match_id;
    r.market_id_str  = rec.market_id;
    r.crc32c         = rec.crc32c;

    // wide format: slots[i] → yes_odds[i], no_odds[i], valid_bm[i]
    // ABI 锁: kBookmakerIds[0..7] = [10bet, williamhill, bet365, marathon,
    //                                unibet, betvictor, 1xbet, betano]
    for (std::size_t i = 0; i < data::goalserve::kNumBookmakers; ++i) {
        r.yes_odds[i] = rec.slots[i].odds_yes;
        r.no_odds[i]  = rec.slots[i].odds_no;
        r.valid_bm[i] = rec.slots[i].valid;
    }

    odds_buf_.push_back(r);

    if (odds_buf_.size() >= buffer_capacity_) {
        return flush_to_file();
    }
    return FlushResult::Ok;
}

// ---------------------------------------------------------------------------
// flush_to_file — W6 stub: 返回 StubNotImplemented
//
// W7+ HC-06 实施时: 将 feature_buf_ / odds_buf_ 转 arrow::Table →
//   parquet::arrow::WriteTable(compression=ZSTD, level=19, row_group=64MB)
//   写入: root_path_ / sport=X/market_type=Y/year=Z/week=W/part-NNN.parquet
// ---------------------------------------------------------------------------
FlushResult ParquetBatchWriter::flush_to_file()
{
    if (feature_buf_.empty() && odds_buf_.empty()) {
        return FlushResult::BufferEmpty;
    }

    // W6 stub: 不写磁盘, 仅递增计数器
    ++flush_counter_;

    // W7+ 在此处调用 Arrow WriteTable (不改 .hpp ABI)
    // Example (pseudocode, HC-06 入职后实施):
    //
    //   auto schema = build_arrow_schema();   // 对应 xiaotian_parquet_export.ipynb fields
    //   auto table  = records_to_arrow_table(feature_buf_, schema);
    //   auto partition_key = PartitionKey{sport, market_type, year, week};
    //   auto path = root_path_ + "/" + partition_key.to_path_fragment()
    //             + "/part-" + std::to_string(flush_counter_) + ".parquet";
    //   parquet::WriterProperties props = parquet::WriterProperties::Builder()
    //       .compression(parquet::Compression::ZSTD)
    //       .compression_level(19)
    //       .build();
    //   auto os = arrow::io::FileOutputStream::Open(path).ValueOrDie();
    //   parquet::arrow::WriteTable(*table, arrow::default_memory_pool(),
    //                              os, /*chunk_size=*/64*1024*1024, props, ...);
    //   os->Close();

    // Stub: clear buffer after "flush"
    feature_buf_.clear();
    odds_buf_.clear();

    return FlushResult::StubNotImplemented;
}

}  // namespace stcpp::data
