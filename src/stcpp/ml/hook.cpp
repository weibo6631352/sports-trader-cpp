// stcpp/ml/hook.cpp — MLDataHook v0.1 实现 (W4 Wave 20 小邓)
//
// 落: xiaodeng-ml-data-infra-v1.md §1.3 + roadmap §3
// 红线: ML-R1/2/4/8 + R-11 + R-12 + R-20
//
// Append 走 framework WalWriter (老王 v0.2 skeleton + W4 SPSC ring TODO).
// W4 skeleton 期: framework Append() 是 PIT + watermark 闭环, 真 ring 未接;
// 因此 R-12 ≤ 1us 现在 *已* 达标 (skeleton path 仅 PIT 100ns + atomic seq).

#include "stcpp/ml/hook.hpp"

#include <cstring>
#include <string>

namespace stcpp::ml {

namespace {

// 把 16B audit_id 转成 std::string 当 unordered_map key (W6 切定长 array hash).
[[nodiscard]] inline std::string key_from_audit_id(std::array<std::uint8_t, 16> const& id) noexcept {
    return std::string(reinterpret_cast<const char*>(id.data()), id.size());
}

}  // namespace

// ---------- emit helpers ----------------------------------------------------

void MLDataHook::emit_feature_(FeatureSnapshot const& snap) noexcept {
    if (feature_writer_ == nullptr) {
        stats_.dropped_writer_null.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    auto r = feature_writer_->Append(snap);
    if (!r) {
        // 失败分类: PIT / Backpressure / Io 都 silent drop (R-12 不阻塞主线程)
        auto e = r.error();
        if (e == stcpp::infra::wal::WalError::PitViolation) {
            stats_.dropped_pit_fail.fetch_add(1, std::memory_order_relaxed);
        } else if (e == stcpp::infra::wal::WalError::Backpressure) {
            stats_.dropped_backpressure.fetch_add(1, std::memory_order_relaxed);
        } else {
            // Io / FsyncFailed — ML data 是 best-effort, 不上报 RM
            stats_.dropped_backpressure.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void MLDataHook::emit_label_(TrainingLabel const& label) noexcept {
    if (label_writer_ == nullptr) {
        stats_.dropped_writer_null.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    auto r = label_writer_->Append(label);
    if (!r) {
        auto e = r.error();
        if (e == stcpp::infra::wal::WalError::PitViolation) {
            stats_.dropped_pit_fail.fetch_add(1, std::memory_order_relaxed);
        } else {
            stats_.dropped_backpressure.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

// ---------- 4 钩子入口 ------------------------------------------------------

void MLDataHook::on_signal_compute(stcpp::strategy::SignalContext const& ctx,
                                   FeatureSnapshot const& snap) noexcept {
    // ML-R8: feature_snapshot_id 必带
    if (snap.feature_snapshot_id == 0) {
        stats_.dropped_id_zero.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // R-20: 4 ts 不等式
    if (!snap.ts_chain_ok()) {
        stats_.dropped_pit_fail.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // sanity: 4 ts 必须与 SignalContext 一致 (caller 责任, 此处不强校 — paper engine 由小蒋统一注入)
    // 但 ts_chain_ok 自检已经 enforce 整条链, 不重复.
    (void)ctx;

    emit_feature_(snap);
    cache_for_join(snap);
    stats_.signals_recorded.fetch_add(1, std::memory_order_relaxed);
}

void MLDataHook::on_risk_decision(stcpp::risk::RiskDecision const& decision,
                                  FeatureSnapshot const& snap) noexcept {
    if (snap.feature_snapshot_id == 0) {
        stats_.dropped_id_zero.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (!snap.ts_chain_ok()) {
        stats_.dropped_pit_fail.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // 决策事件 — 抓 FeatureSnapshot (含 decision 后状态: slippage_bps / fill_rate 等),
    // 并把 RM 决策结果写到 cache, 给 on_settle 时 join.
    (void)decision;
    emit_feature_(snap);
    cache_for_join(snap);
    stats_.decisions_recorded.fetch_add(1, std::memory_order_relaxed);
}

void MLDataHook::on_fill(stcpp::execution::VirtualFill const& fill, FeatureSnapshot const& snap) noexcept {
    if (snap.feature_snapshot_id == 0) {
        stats_.dropped_id_zero.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (!snap.ts_chain_ok()) {
        stats_.dropped_pit_fail.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // fill 事件 — 抓 final FeatureSnapshot, cache 更新 (含 fill_price / fill_size).
    emit_feature_(snap);

    // 缓存中插入 fill 结果 (settlement 时 join 用)
    // v0.1: 简单覆盖 cache (cache_for_join 内部 upsert)
    FeatureSnapshot enriched = snap;
    // 注: fill_price / fill_size 不在 32 feature 中 (是 label 字段), 此处不动 features.
    cache_for_join(enriched);

    // 顺手写一条 partial TrainingLabel (decision_taken + executed + fill 字段, outcome 未知)
    TrainingLabel partial{};
    partial.event_ts = snap.event_ts;
    partial.data_source_ts = snap.data_source_ts;
    partial.ingestion_ts = snap.ingestion_ts;
    partial.as_of_ts = snap.as_of_ts;  // 注: 真 label_ts 在 on_settle, 此处先用 fill 的 as_of
    partial.feature_snapshot_id = snap.feature_snapshot_id;
    partial.audit_id_bytes = snap.audit_id_bytes;
    partial.decision_taken = true;
    partial.executed = (fill.reject == stcpp::execution::MatchReject::Ok);
    partial.filled_price = fill.fill_price;
    // A1: fill_size_usdc 现为 micro; training_label 用 whole pUSD → /1e6 (unit-contract-ok: micro→pUSD)
    partial.filled_size_usdc = static_cast<double>(fill.fill_size_usdc) / 1'000'000.0;
    partial.settlement_outcome = SettlementOutcome::Pending;
    partial.realized_pnl_usdc = 0.0;
    emit_label_(partial);

    stats_.fills_recorded.fetch_add(1, std::memory_order_relaxed);
}

void MLDataHook::on_settle(SettlementEvent const& settle) noexcept {
    if (settle.feature_snapshot_id == 0) {
        stats_.dropped_id_zero.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // 找 cache 中的原 snapshot (join key = audit_id_bytes)
    const auto key = key_from_audit_id(settle.audit_id_bytes);
    const auto it = join_cache_.find(key);

    TrainingLabel lbl{};
    lbl.event_ts = settle.event_ts;
    lbl.data_source_ts = settle.data_source_ts;
    lbl.ingestion_ts = settle.ingestion_ts;
    lbl.as_of_ts = settle.as_of_ts;
    lbl.feature_snapshot_id = settle.feature_snapshot_id;
    lbl.audit_id_bytes = settle.audit_id_bytes;
    lbl.decision_taken = (it != join_cache_.end());  // 有 cache 表示曾产生信号
    // executed / fill_price / fill_size 由 on_fill 那条 partial label 给; 这里不重复.
    lbl.settlement_outcome = settle.outcome;
    lbl.realized_pnl_usdc = settle.realized_pnl_usdc;

    if (!lbl.ts_chain_ok()) {
        // 注: on_settle 入口 *允许* settle ts 不满足 R-20 (例如 match_end_ts 早于 PM event_ts),
        //     这种 corner 在生产里走慢路径诊断 + drift counter.
        stats_.dropped_pit_fail.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    emit_label_(lbl);
    stats_.settlements_recorded.fetch_add(1, std::memory_order_relaxed);

    // 结算完成 — evict cache (LRU 触发, v0.1 直接 erase)
    if (it != join_cache_.end()) {
        join_cache_.erase(it);
    }
}

void MLDataHook::cache_for_join(FeatureSnapshot const& snap) noexcept {
    // v0.1: 直接 upsert; W6 切 LRU + 容量上限 (防 OOM, paper 跑长 → cache 漂高).
    const auto key = key_from_audit_id(snap.audit_id_bytes);
    join_cache_[key] = snap;
}

}  // namespace stcpp::ml

// ---------- 模板显式实例化 (复用老王 framework, 同 audit_emitter 模式) -------
//
// FeatureSnapshot / TrainingLabel 都满足 WalRecord concept (header static_assert).
// framework wal_writer.cpp 是 single-TU 模板定义, owner 自显式实例化.

#include "src/stcpp/infra/wal/wal_writer.cpp"

namespace stcpp::infra::wal {
template class WalWriter<stcpp::ml::FeatureSnapshot>;
template class WalWriter<stcpp::ml::TrainingLabel>;
}  // namespace stcpp::infra::wal
