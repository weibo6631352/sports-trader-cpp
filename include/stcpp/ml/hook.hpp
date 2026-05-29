// stcpp/ml/hook.hpp — MLDataHook v0.1 (W4 Wave 20 小邓)
//
// 落:
//   xiaodeng-ml-data-infra-v1.md §1.1 (数据 lake 目录 + features/snapshot + signals/shadow_signal)
//   xiaodeng-ml-roadmap-v2.md §3 (阶段 1 shadow path: paper-only, 0 PnL 影响)
//   laowang-wal-framework-v0.2.md §3 (4 类 WAL, ShadowAudit owner: 小蒋 / 小邓)
//
// 角色: paper engine / live engine 旁路观察者. 在 4 个关键事件抓拍 FeatureSnapshot + TrainingLabel,
//       异步落 mldata WAL (复用 WalKind::ShadowAudit). 主线程不阻塞 (R-12).
//
// 红线:
//   ML-R1  hook 不参与 RM 决策, on_*() 调用方 不可 假设 hook 返 false 会 reject (本类 noexcept void)
//   ML-R2  paper 期 hook 抓 paper 决策, live 期抓 live 决策, FeatureSnapshot 不喂回 OrderIntent
//   ML-R4  binary 0 行 Python
//   ML-R8  on_signal_compute 入口 enforce feature_snapshot_id != 0
//   R-11   build-time path_prefix: STCPP_EXEC_MODE_paper → /var/lib/stcpp/shadow/paper/
//                                  STCPP_EXEC_MODE_live  → /var/lib/stcpp/shadow/live/
//          paper 期 hook 严禁触碰 /var/lib/stcpp/exec/ (position) 或 /var/lib/stcpp/audit/ (risk)
//   R-12   on_*() 同步路径 ≤ 1us (WAL framework SPSC try_push)
//          ring 满 → silent drop (ML-R 不阻塞主, hook overflow 不能拖死 trader)
//   R-20   FeatureSnapshot + TrainingLabel 4 ts 必带, ts_chain_ok 失败 → drop + counter++
//
// API:
//   - 4 个 on_*() 入口, 各对应 paper engine / live engine 的一个 lifecycle 事件
//   - 内部缓 FeatureSnapshot (audit_id → snapshot), on_fill / on_settle 时回填 TrainingLabel
//   - 注: v0.1 缓存用 unordered_map (W5 切 SwissTable + LRU 边界, 防 OOM)
//
// 不耻下问:
//   - SignalContext / SignalOutput 字段全集 @小程 (P0-01 spec)
//   - RiskDecision @老韩
//   - VirtualFill @小蒋
//   - SettlementEvent 是否需新建 @老胡 PM ack — v0.1 用 enum + 几个 ts 字段 inline, 不建 module

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "stcpp/execution/virtual_matcher.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/infra/wal/wal_writer.hpp"
#include "stcpp/ml/feature_snapshot.hpp"
#include "stcpp/ml/training_label.hpp"
#include "stcpp/risk/risk_gateway.hpp"
#include "stcpp/strategy/signal_iface.hpp"

namespace stcpp::ml {

// ---------- R-11 build-time WAL kind / path (ShadowAudit, paper 走 shadow/paper/) ----

[[nodiscard]] constexpr stcpp::infra::wal::WalKind MlDataWalKind() noexcept {
    // mldata 一律走 ShadowAudit (老王 framework owner 注释明确 owner: 小邓)
    return stcpp::infra::wal::WalKind::ShadowAudit;
}

// 默认 path_prefix — caller 可覆盖. R-11 仍然在 framework Open() 检查 starts_with(kPathRoots).
[[nodiscard]] constexpr std::string_view DefaultMlDataPathPrefix() noexcept {
#if defined(STCPP_EXEC_MODE_live)
    return "/var/lib/stcpp/shadow/live/mldata";
#elif defined(STCPP_EXEC_MODE_paper) || defined(STCPP_EXEC_MODE_backtest)
    return "/var/lib/stcpp/shadow/paper/mldata";
#else
    return "/var/lib/stcpp/shadow/paper/mldata";
#endif
}

// ---------- Settlement event (v0.1 inline POD — 老胡 PM ack 不建 Settlement 模块) ----

struct SettlementEvent {
    // 4 ts (settle 端)
    std::int64_t event_ts = 0;        // match_end_ts
    std::int64_t data_source_ts = 0;  // PM resolve event
    std::int64_t ingestion_ts = 0;
    std::int64_t as_of_ts = 0;

    std::array<std::uint8_t, 16> audit_id_bytes{};
    std::uint64_t feature_snapshot_id = 0;
    SettlementOutcome outcome = SettlementOutcome::Pending;
    double realized_pnl_usdc = 0.0;
};

// ---------- Hook stats (counter; 不落盘, 监控用) ------------------------------

struct HookStats {
    std::atomic<std::uint64_t> signals_recorded{0};
    std::atomic<std::uint64_t> decisions_recorded{0};
    std::atomic<std::uint64_t> fills_recorded{0};
    std::atomic<std::uint64_t> settlements_recorded{0};
    std::atomic<std::uint64_t> dropped_pit_fail{0};      // R-20 ts_chain_ok 失败
    std::atomic<std::uint64_t> dropped_id_zero{0};       // ML-R8 feature_snapshot_id == 0
    std::atomic<std::uint64_t> dropped_backpressure{0};  // ring 满
    std::atomic<std::uint64_t> dropped_writer_null{0};
};

// ---------- MLDataHook ------------------------------------------------------

class MLDataHook {
public:
    using FeatureWriterT = stcpp::infra::wal::WalWriter<FeatureSnapshot>;
    using LabelWriterT = stcpp::infra::wal::WalWriter<TrainingLabel>;

    // ctor: 注入两个 writer (caller 已 Open 到 ShadowAudit kind + path_prefix).
    // writer 可为 nullptr (单测 / mode-disabled), 此时 on_*() 仍 noexcept void, 计 dropped_writer_null.
    MLDataHook(FeatureWriterT* feature_writer, LabelWriterT* label_writer) noexcept
        : feature_writer_(feature_writer), label_writer_(label_writer) {}

    MLDataHook(const MLDataHook&) = delete;
    MLDataHook& operator=(const MLDataHook&) = delete;

    // ---- 4 个钩子入口 (R-12: 同步 ≤ 1us; 不抛; ring 满 silent drop) ----

    // 1) 信号 tick 完成时调 (含 nullopt — 不下注的也抓 feature, 用于 regret learning)
    void on_signal_compute(stcpp::strategy::SignalContext const& ctx, FeatureSnapshot const& snap) noexcept;

    // 2) RM 决策完成时调 (APPROVED / REJECTED / DEFERRED 都抓)
    void on_risk_decision(stcpp::risk::RiskDecision const& decision, FeatureSnapshot const& snap) noexcept;

    // 3) 撮合 fill 时调 (paper: VirtualFill; live: RealFill 同 schema)
    void on_fill(stcpp::execution::VirtualFill const& fill, FeatureSnapshot const& snap) noexcept;

    // 4) 比赛结算时调 — 写 TrainingLabel
    void on_settle(SettlementEvent const& settle) noexcept;

    // ---- 监控 / 测试 ----
    [[nodiscard]] HookStats const& stats() const noexcept { return stats_; }

    // 单测用: 注入伪 SettlementEvent 时缓 FeatureSnapshot 的 settlement 时 join.
    // v0.1: 缓 audit_id_bytes (16B) → feature_snapshot 全量, 用于 on_settle 时合并写 label.
    // 注: 真上线 W6 切 SwissTable + LRU, v0.1 用 unordered_map 测试体量足.
    void cache_for_join(FeatureSnapshot const& snap) noexcept;

    // 单测用: 查 cache size (LRU 监控同款)
    [[nodiscard]] std::size_t cache_size() const noexcept { return join_cache_.size(); }

    // 单测用: 清 cache (settle 完成应 evict, 默认未实现 — 简化)
    void clear_cache() noexcept { join_cache_.clear(); }

private:
    // 内部 emit (R-12 try_push 路径, 不阻塞)
    void emit_feature_(FeatureSnapshot const& snap) noexcept;
    void emit_label_(TrainingLabel const& label) noexcept;

    FeatureWriterT* feature_writer_;
    LabelWriterT* label_writer_;
    HookStats stats_;

    // join cache (audit_id 16B → FeatureSnapshot)
    // v0.1 用 std::string (16 char) 作 key — 简化, W6 切定长 array hash
    std::unordered_map<std::string, FeatureSnapshot> join_cache_;
};

}  // namespace stcpp::ml
