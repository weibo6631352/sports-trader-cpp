// stcpp/risk/position_ledger.hpp — PositionLedger read API v0.1 (老沈 Wave 76 W9 W4)
//
// 落: laohan-w9-w3-position-ledger-rest-api-spec-v1.md §2
//
// 设计约束:
//   - read API (get_*) 无持久锁: snapshot 取出后外部可自由读 (TC-01 快照一致性)
//   - 内部 token_positions_ map + condition_exposure_ map
//   - last_update_ts 严格透传 VirtualFill.as_of_ts_ns (R-20 红线, 禁 now())
//   - 线程安全: apply_fill shared_mutex write; read API shared_lock
//
// cite:
//   polymarket_ssot_cite: laoli-w8-polymarket-data-structure-ssot-v1.md §2.3 §3.3
//   goalserve_ssot_cite:  N/A
//   handshake_cite:       laoli-laoSun-handshake-v1.md §3
//   adr_cite:             ADR-027 Enforce-1
//
// 红线:
//   R-20 4 ts 透传 (禁本地 now() 替代上游 ts)
//   R-1  所有仓位更新必经 apply_fill (绕过 = P0)

#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "stcpp/execution/virtual_matcher.hpp"  // VirtualFill
#include "stcpp/risk/position_view.hpp"
#include "stcpp/strategy/signal_iface.hpp"       // Outcome

namespace stcpp::risk {

// PositionLedger — 仓位快照读 API + 注入入口
// 线程模型: 单 writer (apply_fill), 多 reader (get_*)
// 所有读 API 返回 snapshot copy, 不持锁到 caller 域
class PositionLedger {
 public:
    PositionLedger() = default;
    ~PositionLedger() = default;

    PositionLedger(PositionLedger const&)            = delete;
    PositionLedger& operator=(PositionLedger const&) = delete;
    PositionLedger(PositionLedger&&)                 = delete;
    PositionLedger& operator=(PositionLedger&&)      = delete;

    // ---------- Write API (R-1: 所有仓位变更必经此处) --------------------------

    // 注入一笔虚拟成交
    // last_update_ts 严格透传 fill.as_of_ts_ns (R-20)
    // condition_id + token_id 来自 PositionView 初始化时提供的映射
    void apply_fill(std::string const& condition_id,
                    std::string const& token_id,
                    Outcome             outcome,
                    execution::VirtualFill const& fill) noexcept;

    // ---------- Read API (老韩 spec §2, 快照一致性 TC-01) ----------------------

    // 所有持仓快照 (atomic copy, 返回后无锁)
    [[nodiscard]] std::vector<PositionView> get_all_positions() const noexcept;

    // 按 token_id 查单笔 (nullopt = 无仓)
    [[nodiscard]] std::optional<PositionView> get_position(std::string const& token_id) const noexcept;

    // per-outcome exposure: token_id → net_size_usdc (signed)
    [[nodiscard]] std::unordered_map<std::string, std::int64_t>
    get_per_outcome_exposure() const noexcept;

    // per-condition exposure: condition_id → net_size_usdc (signed, sum of all tokens)
    [[nodiscard]] std::unordered_map<std::string, std::int64_t>
    get_per_condition_exposure() const noexcept;

 private:
    // 内部存储 (老韩 spec §2)
    // token_positions_: token_id → PositionView (含 avg_entry_price + last_update_ts)
    // condition_exposure_: condition_id → net size (sum of all tokens under condition)
    mutable std::shared_mutex mu_;
    std::unordered_map<std::string, PositionView> token_positions_;
    std::unordered_map<std::string, std::int64_t> condition_exposure_;

    // Internal: update position on fill
    void update_position_locked_(std::string const& condition_id,
                                 std::string const& token_id,
                                 Outcome             outcome,
                                 std::int64_t        delta_usdc,
                                 double              fill_price,
                                 std::int64_t        as_of_ts_ns) noexcept;
};

}  // namespace stcpp::risk
