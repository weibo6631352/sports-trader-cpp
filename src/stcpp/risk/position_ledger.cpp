// stcpp/risk/position_ledger.cpp — PositionLedger read API 实现 (老沈 Wave 76 W9 W4)
//
// 落: laohan-w9-w3-position-ledger-rest-api-spec-v1.md §2
//
// 红线:
//   R-20 last_update_ts 严格透传 VirtualFill.as_of_ts_ns, 禁 now()
//   R-1  apply_fill 是唯一合法写入路径
//
// 线程安全:
//   apply_fill: unique_lock (single writer 假设, vCPU3 hot path)
//   get_*:      shared_lock (snapshot copy; caller 侧无锁)
//
// avg_entry_price VWAP 计算:
//   新仓: avg = fill_price
//   加仓: avg = (old_size * old_avg + delta * fill_price) / new_size
//   减仓 (平仓): avg 不变 (realized_pnl 由调用方结算, 此层只维护持仓)
//   全平: avg 归零
//
// cite:
//   polymarket_ssot_cite: laoli-w8-polymarket-data-structure-ssot-v1.md §2.3 §3.3
//   goalserve_ssot_cite:  N/A
//   handshake_cite:       laoli-laoSun-handshake-v1.md §3
//   adr_cite:             ADR-027 Enforce-1

#include "stcpp/risk/position_ledger.hpp"

#include <cmath>

namespace stcpp::risk {

// ---------- apply_fill -------------------------------------------------------

void PositionLedger::apply_fill(std::string const& condition_id, std::string const& token_id, Outcome outcome,
                                execution::VirtualFill const& fill) noexcept {
    // R-11 (老韩 A2 红线3): mode_tag 运行期 fail-closed — 仅 paper fill (mode_tag==0) 记账.
    //   非 paper fill (mode_tag!=0) 直接拒, 不写仓位. release build 也 enforce
    //   (paper_loop 的 debug assert 不够; 这里是 R-11「不污染真账本」的运行期实际守卫).
    if (fill.mode_tag != 0)
        return;

    // 仅处理成功成交 (BernoulliMissed / SlippageModelReject 不更新仓位)
    if (fill.reject != execution::MatchReject::Ok)
        return;
    if (fill.fill_size_usdc == 0)
        return;

    // side 语义: VirtualFill 无 side 字段; 调用方约定:
    //   BUY  → delta = +fill_size_usdc (round to int, signed)
    //   SELL → delta = -fill_size_usdc
    // Wave 76: PositionLedger apply_fill 仅接 delta 符号由 fill_size_usdc 决定
    // 平仓 (SELL) 调用方传 fill_size_usdc 为正值, delta_usdc 负由 is_close 派送:
    // 此处统一用 +fill_size_usdc; 平仓语义由 REST /drain 端 DRAIN state 保证
    // TODO W9 W5: 当 side 信息透传入 VirtualFill 后更新符号逻辑
    // A1: fill_size_usdc 已是 int64 micro pUSD, 直存无 cast (消原 (int64)whole 的 <1pUSD 截断丢仓)。
    auto const delta_raw = fill.fill_size_usdc;

    // R-20: 严格透传 as_of_ts_ns, 禁 now()
    auto const ts = fill.as_of_ts_ns;

    std::unique_lock<std::shared_mutex> lk(mu_);
    update_position_locked_(condition_id, token_id, outcome, delta_raw, fill.fill_price, ts);
}

void PositionLedger::update_position_locked_(std::string const& condition_id, std::string const& token_id,
                                             Outcome outcome, std::int64_t delta_usdc, double fill_price,
                                             std::int64_t as_of_ts_ns) noexcept {
    auto it = token_positions_.find(token_id);
    if (it == token_positions_.end()) {
        // 新仓
        PositionView pv;
        pv.condition_id = condition_id;
        pv.token_id = token_id;
        pv.outcome = outcome;
        pv.size_usdc = delta_usdc;
        pv.avg_entry_price = (delta_usdc != 0 && fill_price > 0.0) ? fill_price : 0.0;
        pv.last_update_ts = as_of_ts_ns;  // R-20: 透传
        token_positions_.emplace(token_id, std::move(pv));
    } else {
        PositionView& pv = it->second;
        auto const old_size = pv.size_usdc;
        auto const new_size = old_size + delta_usdc;

        // avg_entry_price VWAP (加仓更新, 减仓保持)
        if (delta_usdc > 0 && old_size >= 0 && fill_price > 0.0) {
            // 多仓加仓: VWAP
            if (old_size == 0) {
                pv.avg_entry_price = fill_price;
            } else {
                pv.avg_entry_price = (static_cast<double>(old_size) * pv.avg_entry_price +
                                      static_cast<double>(delta_usdc) * fill_price) /
                                     static_cast<double>(new_size);
            }
        }
        // 全平: 归零
        if (new_size == 0)
            pv.avg_entry_price = 0.0;

        pv.size_usdc = new_size;
        pv.last_update_ts = as_of_ts_ns;  // R-20: 透传
    }

    // 更新 condition_exposure_ (signed sum)
    condition_exposure_[condition_id] += delta_usdc;
}

// ---------- read API ---------------------------------------------------------

std::vector<PositionView> PositionLedger::get_all_positions() const noexcept {
    std::shared_lock<std::shared_mutex> lk(mu_);
    std::vector<PositionView> out;
    out.reserve(token_positions_.size());
    for (auto const& [_, pv] : token_positions_) {
        if (pv.size_usdc != 0)
            out.push_back(pv);
    }
    return out;
}

std::optional<PositionView> PositionLedger::get_position(std::string const& token_id) const noexcept {
    std::shared_lock<std::shared_mutex> lk(mu_);
    auto it = token_positions_.find(token_id);
    if (it == token_positions_.end())
        return std::nullopt;
    return it->second;
}

std::unordered_map<std::string, std::int64_t> PositionLedger::get_per_outcome_exposure() const noexcept {
    std::shared_lock<std::shared_mutex> lk(mu_);
    std::unordered_map<std::string, std::int64_t> out;
    out.reserve(token_positions_.size());
    for (auto const& [tid, pv] : token_positions_) {
        out[tid] = pv.size_usdc;
    }
    return out;
}

std::unordered_map<std::string, std::int64_t> PositionLedger::get_per_condition_exposure() const noexcept {
    std::shared_lock<std::shared_mutex> lk(mu_);
    return condition_exposure_;
}

}  // namespace stcpp::risk
