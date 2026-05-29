// stcpp/data/parquet_writer.hpp — ParquetBatchWriter stub (W6 Wave 30, 小田 DWH)
//
// Owner: 小田 (dwh-analyst, #24)
// last_review: 2026-05-28
//
// 目的:
//   ABI 占位 stub — W6 建立接口契约, W7+ HC-06 (ml-data-engineer 8/15 入职) 真接 Apache Arrow.
//   python notebook xiaotian_parquet_export.ipynb 为 spec ground truth.
//
// 红线:
//   ML-R2  Python offline 写 Parquet; C++ stub 不引入 Apache Arrow deps (W7+ HC-06 真接)
//   R-20   4 ts 全程透传 — flush_to_file 前必过 ts_chain_ok()
//   R-7    paper / live build 共享 lib (W7+ 真实现时)
//   ADR-008 feat_04 = Goalserve_devig_p_yes_fair (不再叫 Pinnacle_*)
//
// W6 stub 限制:
//   - 接收 WAL records 进内存 buffer (std::vector)
//   - flush_to_file() 返回 FlushResult::StubNotImplemented (W7+ 替换)
//   - 不引入 FetchContent Arrow / parquet-cpp (W6 不进 cmake deps)
//   - ABI interface 锁死, W7+ 真实现不改方法签名
//
// W7+ HC-06 实施清单 (保留注释作为 onboarding 输入):
//   1. add_library(stcpp_data_parquet STATIC parquet_writer.cpp)
//      target_link_libraries(... arrow parquet)  -- FetchContent 或 vcpkg
//   2. flush_to_file() 实现: batch → arrow::RecordBatch → parquet::arrow::WriteTable
//      compression = ZSTD, compression_level = 19, row_group_size = 64MB
//   3. partition path: data/paper_mldata/sport=X/market_type=Y/year=Z/week=W/part-NNN.parquet
//   4. schema metadata: schema_version, bookmaker_abi_version, r20_ts_columns, feat_abi_version
//
// 不耻下问:
//   - Apache Arrow C++ API @HC-06 入职后
//   - WAL replay 接口 @老王 (wal-framework)
//   - paper_mldata.wal 路径 @小邓 (ML hook)

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Forward declarations only — 不引入 Arrow headers (W7+ HC-06 真接)
// #include <arrow/table.h>          // W7+
// #include <parquet/arrow/writer.h> // W7+

#include "stcpp/data/odds_record.hpp"
#include "stcpp/ml/feature_snapshot.hpp"
#include "stcpp/ml/training_label.hpp"

namespace stcpp::data {

// ---------------------------------------------------------------------------
// FlushResult — flush_to_file 返回码
// ---------------------------------------------------------------------------
enum class FlushResult : std::uint8_t {
    Ok = 0,                  // W7+ 真实现成功
    StubNotImplemented = 1,  // W6 stub: 未实现, 正常占位
    TsChainViolation = 2,    // R-20: 某条 record ts 链不等式不满足
    BufferEmpty = 3,         // buffer 为空, 无需 flush
    IoError = 4,             // W7+: 文件写入失败
};

[[nodiscard]] constexpr std::string_view to_string(FlushResult r) noexcept {
    switch (r) {
        case FlushResult::Ok:
            return "Ok";
        case FlushResult::StubNotImplemented:
            return "StubNotImplemented";
        case FlushResult::TsChainViolation:
            return "TsChainViolation";
        case FlushResult::BufferEmpty:
            return "BufferEmpty";
        case FlushResult::IoError:
            return "IoError";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// PartitionKey — Hive-style 分区路径计算辅助
// ---------------------------------------------------------------------------
struct PartitionKey {
    std::string sport;        // e.g. "Soccer"
    std::string market_type;  // e.g. "Moneyline"
    std::int32_t year = 0;    // e.g. 2024
    std::int32_t week = 0;    // ISO week 1-52

    // 生成路径 fragment: sport=Soccer/market_type=Moneyline/year=2024/week=20
    [[nodiscard]] std::string to_path_fragment() const {
        return "sport=" + sport + "/market_type=" + market_type + "/year=" + std::to_string(year) +
               "/week=" + std::to_string(week);
    }
};

// ---------------------------------------------------------------------------
// FeatureSnapshotRecord — Parquet 行 POD (stub buffer element)
//
// 与 ml::FeatureSnapshot ABI 对齐, 加入分区键字段.
// W7+: HC-06 将此 struct 直接转 arrow::RecordBatch row.
// ---------------------------------------------------------------------------
struct FeatureSnapshotRecord {
    // R-20 4 ts (ns)
    std::int64_t event_ts = 0;
    std::int64_t data_source_ts = 0;
    std::int64_t ingestion_ts = 0;
    std::int64_t as_of_ts = 0;

    // business keys
    std::uint64_t feature_snapshot_id = 0;
    std::uint8_t signal_id_u8 = 0;
    std::string market_id;

    // partition keys
    std::string sport;
    std::string market_type;
    std::int32_t year = 0;
    std::int32_t week = 0;

    // 32 features (float32, NaN = missing, ADR-008: [4]=Goalserve_devig_p_yes_fair)
    float feat[32]{};

    // TrainingLabel fields (joined at Parquet write time)
    std::int64_t label_event_ts = 0;
    std::int64_t label_data_source_ts = 0;
    std::int64_t label_ingestion_ts = 0;
    std::int64_t label_as_of_ts = 0;
    bool decision_taken = false;
    bool executed = false;
    double filled_price = 0.0;
    double filled_size_usdc = 0.0;
    std::uint8_t settlement_outcome = 0;  // SettlementOutcome cast
    double realized_pnl_usdc = 0.0;

    // --- 4 ts chain check (R-20) ---
    [[nodiscard]] bool ts_chain_ok() const noexcept {
        return (event_ts > 0) && (data_source_ts >= event_ts) && (ingestion_ts >= data_source_ts) &&
               (as_of_ts >= ingestion_ts);
    }

    // --- Construct from FeatureSnapshot + TrainingLabel (用于 WAL replay → Parquet) ---
    static FeatureSnapshotRecord from_snapshots(const ml::FeatureSnapshot& fs, const ml::TrainingLabel& tl,
                                                std::string_view sport_sv, std::string_view market_type_sv,
                                                std::int32_t year_val, std::int32_t week_val) noexcept {
        FeatureSnapshotRecord r;
        r.event_ts = fs.event_ts;
        r.data_source_ts = fs.data_source_ts;
        r.ingestion_ts = fs.ingestion_ts;
        r.as_of_ts = fs.as_of_ts;
        r.feature_snapshot_id = fs.feature_snapshot_id;
        r.signal_id_u8 = fs.signal_id_u8;
        r.market_id = std::string(fs.market_id.data(), strnlen(fs.market_id.data(), fs.market_id.size()));
        r.sport = std::string(sport_sv);
        r.market_type = std::string(market_type_sv);
        r.year = year_val;
        r.week = week_val;

        for (std::size_t i = 0; i < 32; ++i)
            r.feat[i] = fs.features[i];

        r.label_event_ts = tl.event_ts;
        r.label_data_source_ts = tl.data_source_ts;
        r.label_ingestion_ts = tl.ingestion_ts;
        r.label_as_of_ts = tl.as_of_ts;
        r.decision_taken = tl.decision_taken;
        r.executed = tl.executed;
        r.filled_price = tl.filled_price;
        r.filled_size_usdc = tl.filled_size_usdc;
        r.settlement_outcome = static_cast<std::uint8_t>(tl.settlement_outcome);
        r.realized_pnl_usdc = tl.realized_pnl_usdc;
        return r;
    }
};

// ---------------------------------------------------------------------------
// MultiBookOddsParquetRecord — wide-format (8 yes + 8 no + 8 valid cols)
// ABI 锁: 列顺序与 data_contract.hpp kBookmakerIds[0..7] 1:1
// ---------------------------------------------------------------------------
struct MultiBookOddsParquetRecord {
    // R-20 4 ts
    std::int64_t event_ts = 0;
    std::int64_t data_source_ts = 0;
    std::int64_t ingestion_ts = 0;
    std::int64_t as_of_ts = 0;
    std::uint8_t ds_origin = 0;

    // partition keys
    std::string sport;
    std::string market_type;
    std::int32_t year = 0;
    std::int32_t week = 0;
    std::string match_id;
    std::string market_id_str;

    std::uint32_t crc32c = 0;

    // wide format: 8 bookmakers × (yes_odds, no_odds, valid)
    // index order matches kBookmakerIds: [10bet, williamhill, bet365, marathon,
    //                                    unibet, betvictor, 1xbet, betano]
    double yes_odds[8]{};
    double no_odds[8]{};
    bool valid_bm[8]{};

    [[nodiscard]] bool ts_chain_ok() const noexcept {
        return (event_ts > 0) && (data_source_ts >= event_ts) && (ingestion_ts >= data_source_ts) &&
               (as_of_ts >= ingestion_ts);
    }

    [[nodiscard]] std::size_t valid_bookmaker_count() const noexcept {
        std::size_t n = 0;
        for (bool v : valid_bm)
            if (v)
                ++n;
        return n;
    }
};

// ---------------------------------------------------------------------------
// ParquetBatchWriter — W6 stub (interface ABI lock, W7+ HC-06 真实现)
//
// 设计:
//   - append(): 接 WAL record → 内存 buffer (O(1))
//   - flush_to_file(): W6 stub 返回 StubNotImplemented; W7+ 写真 Parquet
//   - R-20 check: append 时校验 ts_chain_ok(), 违规返 TsChainViolation
//   - thread-safety: 单线程消费 (W6 offline notebook 用); W7+ 如需多线程加锁
//
// HC-06 入职 onboarding: 不改此 .hpp ABI, 只改 .cpp 实现
// ---------------------------------------------------------------------------
class ParquetBatchWriter {
public:
    explicit ParquetBatchWriter(std::string root_path, std::size_t buffer_capacity = 4096)
        : root_path_(std::move(root_path)), buffer_capacity_(buffer_capacity) {
        feature_buf_.reserve(buffer_capacity_);
        odds_buf_.reserve(buffer_capacity_);
    }

    // non-copyable (buffer owns records)
    ParquetBatchWriter(const ParquetBatchWriter&) = delete;
    ParquetBatchWriter& operator=(const ParquetBatchWriter&) = delete;
    ParquetBatchWriter(ParquetBatchWriter&&) = default;
    ParquetBatchWriter& operator=(ParquetBatchWriter&&) = default;

    ~ParquetBatchWriter() = default;

    // ---- FeatureSnapshot + TrainingLabel append ----
    [[nodiscard]] FlushResult append(const ml::FeatureSnapshot& fs, const ml::TrainingLabel& tl,
                                     std::string_view sport, std::string_view market_type, std::int32_t year,
                                     std::int32_t week);

    // ---- MultiBookOddsRecord append (历史回填) ----
    [[nodiscard]] FlushResult append(const data::goalserve::MultiBookOddsRecord& rec, std::string_view sport,
                                     std::string_view market_type, std::int32_t year, std::int32_t week);

    // ---- Flush buffer → Parquet file ----
    // W6 stub: 返回 StubNotImplemented (不写文件)
    // W7+: 写 zstd-19 Parquet 到 root_path_ / partition_key.to_path_fragment() / part-NNN.parquet
    [[nodiscard]] FlushResult flush_to_file();

    // ---- Query buffer state ----
    [[nodiscard]] std::size_t feature_buffer_size() const noexcept { return feature_buf_.size(); }
    [[nodiscard]] std::size_t odds_buffer_size() const noexcept { return odds_buf_.size(); }
    [[nodiscard]] std::size_t total_ts_violations() const noexcept { return ts_violations_; }
    [[nodiscard]] const std::string& root_path() const noexcept { return root_path_; }

    // ---- Clear buffer (e.g. after successful flush) ----
    void clear() noexcept {
        feature_buf_.clear();
        odds_buf_.clear();
    }

private:
    std::string root_path_;
    std::size_t buffer_capacity_;
    std::vector<FeatureSnapshotRecord> feature_buf_;
    std::vector<MultiBookOddsParquetRecord> odds_buf_;
    std::size_t ts_violations_ = 0;
    std::size_t flush_counter_ = 0;  // for part-NNN filename
};

}  // namespace stcpp::data
