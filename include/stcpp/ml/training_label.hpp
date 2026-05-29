// stcpp/ml/training_label.hpp — TrainingLabel v0.1 (W4 Wave 20 小邓)
//
// 落:
//   xiaodeng-ml-data-infra-v1.md §1.3 (labels/outcome_binary table)
//   xiaodeng-ml-roadmap-v2.md §3 (4 label 类型 + 30s 训练 join 间隔)
//
// 红线:
//   ML-R8 必带 feature_snapshot_id (join key)
//   R-11  paper / live 标签物理隔离 (由 hook WAL kind 承担, 本 POD 不区分)
//   R-20  4 ts (label 侧仅需 settlement_ts, settlement_ts ≥ feature.as_of_ts + 30s)
//
// 设计:
//   - 4 阶段标签 (decision / executed / fill / settle), 同 feature_snapshot_id 串联
//   - settlement_outcome 用于分类 (Win/Loss/Push), realized_pnl_usdc 用于回归 / regret
//   - immutable POD, 满足 WalRecord concept
//
// 不耻下问:
//   - settlement 模块是否需要新建 @老胡 PM ack — 当前用 enum 暂存, 等比赛结算时 hook on_settle 写入
//   - join key 工程 @老周 PIT 工程 (feature_snapshot_id 同步)

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

#include "stcpp/infra/wal/wal_writer.hpp"  // WalRecord concept

namespace stcpp::ml {

// ---------- Settlement outcome ---------------------------------------------

enum class SettlementOutcome : std::uint8_t {
    Pending = 0,  // 尚未结算
    Win = 1,      // YES 中标 (从 BuyYes 角度)
    Loss = 2,
    Push = 3,  // 取消 / 平局
    Void = 4,  // UMA 挑战驳回 / 市场失效
};

[[nodiscard]] constexpr std::string_view to_string(SettlementOutcome o) noexcept {
    switch (o) {
        case SettlementOutcome::Pending:
            return "Pending";
        case SettlementOutcome::Win:
            return "Win";
        case SettlementOutcome::Loss:
            return "Loss";
        case SettlementOutcome::Push:
            return "Push";
        case SettlementOutcome::Void:
            return "Void";
    }
    return "unknown";
}

// ---------- TrainingLabel POD ----------------------------------------------
//
// Layout:
//   - 4 ts (label 端: 全用 settlement 链路 ts, 含 event_ts=match_end_ts 等)
//   - feature_snapshot_id (join key, ML-R8)
//   - 4 阶段标志: decision_taken / executed / fill_* / settlement_*
//   - realized_pnl_usdc (回归 target)

struct TrainingLabel {
    // ---- R-20 4 ts (settlement 端) ----
    // event_ts        : 比赛真实结束 ts (Goalserve match.match_end_ts)
    // data_source_ts  : Polymarket settle event ts (UMA / market resolution)
    // ingestion_ts    : 本地接收 settle event
    // as_of_ts        : label 写入 ts (label_ts)
    std::int64_t event_ts = 0;
    std::int64_t data_source_ts = 0;
    std::int64_t ingestion_ts = 0;
    std::int64_t as_of_ts = 0;

    // ---- 锚 ----
    std::uint64_t feature_snapshot_id = 0;          // join FeatureSnapshot
    std::array<std::uint8_t, 16> audit_id_bytes{};  // ULID, 与 RM/signer 同链

    // ---- 4 阶段 ----
    bool decision_taken = false;    // signal triggered
    bool executed = false;          // RM approved + signer + matcher fill
    double filled_price = 0.0;      // VirtualFill.fill_price
    double filled_size_usdc = 0.0;  // VirtualFill.fill_size_usdc

    SettlementOutcome settlement_outcome = SettlementOutcome::Pending;
    double realized_pnl_usdc = 0.0;  // 含手续费净 PnL (paper: 模拟; live: 真账本)

    // ---- WalRecord concept 适配 ----
    [[nodiscard]] std::int64_t event_ts_ns() const noexcept { return event_ts; }
    [[nodiscard]] std::int64_t data_source_ts_ns() const noexcept { return data_source_ts; }
    [[nodiscard]] std::int64_t ingestion_ts_ns() const noexcept { return ingestion_ts; }
    [[nodiscard]] std::int64_t as_of_ts_ns() const noexcept { return as_of_ts; }
    [[nodiscard]] std::array<std::uint8_t, 16> audit_id() const noexcept { return audit_id_bytes; }

    [[nodiscard]] std::size_t serialize_into(std::span<std::byte> out) const noexcept {
        const std::size_t n = sizeof(TrainingLabel);
        if (out.size() < n)
            return 0;
        std::memcpy(out.data(), this, n);
        return n;
    }
    static constexpr std::size_t max_serialized_size() noexcept { return sizeof(TrainingLabel); }

    [[nodiscard]] bool ts_chain_ok() const noexcept {
        return (event_ts > 0) && (data_source_ts >= event_ts) && (ingestion_ts >= data_source_ts) &&
               (as_of_ts >= ingestion_ts);
    }
};

static_assert(sizeof(TrainingLabel) <= 65535, "TrainingLabel ≤ u16 LEN");
static_assert(stcpp::infra::wal::WalRecord<TrainingLabel>, "TrainingLabel 必须满足 WalRecord concept");

}  // namespace stcpp::ml
