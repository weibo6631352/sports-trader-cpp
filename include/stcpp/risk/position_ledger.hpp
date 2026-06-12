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
#include <utility>
#include <vector>

#include "stcpp/execution/virtual_matcher.hpp"  // VirtualFill
#include "stcpp/risk/fill_event.hpp"             // FillEvent (中性化重载)
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
    // engine: 引擎归属标签 ("sharp"/"flb"/...; 2026-06-12 per-engine 分仓加性追踪)。聚合账本不变,
    //   旁路维护 engine_pos_[(token,engine)] → RM per-engine cap + 离场各管各份。空串退化为单引擎兼容。
    void apply_fill(std::string const& condition_id,
                    std::string const& token_id,
                    Outcome             outcome,
                    execution::VirtualFill const& fill,
                    std::string const& engine = {}) noexcept;

    // 中性化重载 (老郭 R-4 审计放行): apply_fill 接中性 FillEvent (paper/live 共用形态)。
    //   R-11 守卫方向不变 (mode_tag!=0 → 拒, 仍 paper 专用账本)。VirtualFill 版委托至此。
    void apply_fill(std::string const& condition_id,
                    std::string const& token_id,
                    Outcome             outcome,
                    FillEvent const&    ev,
                    std::string const& engine = {}) noexcept;

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

    // ---------- per-engine 加性追踪 (2026-06-12 per-engine 分仓) -----------------
    //   聚合账本不变; 旁路维护 engine_pos_[(token,engine)] = signed size。供 RM per-engine cap
    //   (入场) + 离场各引擎只管自己那份。全平 (聚合 token→0) 时清该 token 所有引擎份。

    // 单引擎在某 token 的持仓 size (signed micro; 0 = 无)。离场各管各份用。
    [[nodiscard]] std::int64_t get_engine_position_size(std::string const& token_id,
                                                        std::string const& engine) const noexcept;

    // per-(condition, engine) 敞口: key = condition_id + '\x1f' + engine → signed size。
    //   按需从 engine_pos_ + 各 token 的 condition_id 聚合 (结算清份自动正确, 无第二增量表)。RM cap 用。
    [[nodiscard]] std::unordered_map<std::string, std::int64_t>
    get_per_condition_engine_exposure() const noexcept;

    // 单 (condition, engine) 敞口 (signed micro)。热路径直查 —— 避免 get_per_condition_engine_exposure
    //   每 tick 建整表 + map 分配 (性能审计 2026-06-12)。一次遍历求和, 无分配。
    [[nodiscard]] std::int64_t get_engine_condition_exposure(std::string const& condition_id,
                                                             std::string const& engine) const noexcept;

    // 某 token 上各引擎的 (engine, size)。结算逐引擎关仓 / 观测用。
    [[nodiscard]] std::vector<std::pair<std::string, std::int64_t>>
    get_token_engine_sizes(std::string const& token_id) const noexcept;

    // 快照持久化: engine_pos_ 原表拷贝 (key = token_id + '\x1f' + engine)。保存时写 PE 行。
    [[nodiscard]] std::unordered_map<std::string, std::int64_t> get_engine_pos_snapshot() const noexcept;

    // 快照恢复: 直接设 engine_pos_ 的某 (token,engine) 份 (仅恢复【归属层】, 聚合仓位仍经 apply_fill 的 P 行;
    //   不绕 R-1 核心写路径)。size==0 则 erase。
    void restore_engine_split(std::string const& token_id, std::string const& engine,
                              std::int64_t size) noexcept;

 private:
    // (token, engine) 复合键: token_id + '\x1f' + engine (token_id 十进制数字/engine 名均不含 \x1f)。
    [[nodiscard]] static std::string engine_key_(std::string const& token_id, std::string const& engine) noexcept;
    // 内部存储 (老韩 spec §2)
    // token_positions_: token_id → PositionView (含 avg_entry_price + last_update_ts)
    // condition_exposure_: condition_id → net size (sum of all tokens under condition)
    mutable std::shared_mutex mu_;
    std::unordered_map<std::string, PositionView> token_positions_;
    std::unordered_map<std::string, std::int64_t> condition_exposure_;
    // per-engine 加性表: engine_key_(token,engine) → signed size。聚合 token 全平时清该 token 全部引擎份。
    std::unordered_map<std::string, std::int64_t> engine_pos_;

    // Internal: update position on fill
    void update_position_locked_(std::string const& condition_id,
                                 std::string const& token_id,
                                 Outcome             outcome,
                                 std::int64_t        delta_usdc,
                                 double              fill_price,
                                 std::int64_t        as_of_ts_ns,
                                 std::string const&  engine) noexcept;
};

}  // namespace stcpp::risk
