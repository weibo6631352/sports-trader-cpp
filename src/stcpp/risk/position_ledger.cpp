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

std::string PositionLedger::engine_key_(std::string const& token_id, std::string const& engine) noexcept {
    std::string k;
    k.reserve(token_id.size() + 1 + engine.size());
    k.append(token_id);
    k.push_back('\x1f');
    k.append(engine);
    return k;
}

void PositionLedger::apply_fill(std::string const& condition_id, std::string const& token_id, Outcome outcome,
                                execution::VirtualFill const& fill, std::string const& engine) noexcept {
    // 仅处理成功成交 (BernoulliMissed / SlippageModelReject 不更新仓位)。
    //   reject 是 VirtualFill 专有语义 → 在此过滤; 中性 FillEvent 不带 reject (已成交事实)。
    if (fill.reject != execution::MatchReject::Ok)
        return;
    // VirtualFill → 中性 FillEvent (直拷无 cast; A1: fill_shares_micro 已 int64 micro, 不丢仓)。
    FillEvent ev;
    ev.filled_size_micro = fill.fill_shares_micro;
    ev.fill_price = fill.fill_price;
    ev.mode_tag = fill.mode_tag;  // R-11 载体平移
    ev.event_ts_ns = fill.event_ts_ns;
    ev.data_source_ts_ns = fill.data_source_ts_ns;
    ev.ingestion_ts_ns = fill.ingestion_ts_ns;
    ev.as_of_ts_ns = fill.as_of_ts_ns;  // R-20 透传
    apply_fill(condition_id, token_id, outcome, ev, engine);
}

void PositionLedger::apply_fill(std::string const& condition_id, std::string const& token_id, Outcome outcome,
                                FillEvent const& ev, std::string const& engine) noexcept {
    // R-11 (老韩 A2 红线3, 2026-06-13 修正为双向隔离): mode_tag 运行期 fail-closed —
    //   仅接受【匹配本账本运行模式】的成交 (paper 实例收 0 / live 实例收 1)。不匹配直接拒 (release 也 enforce)。
    //   旧实现写死「只收 0」→ live 实例 (单 binary live 运行) 把所有 live 成交丢弃 = 持仓永不入账 (真钱事故,
    //   Astros 单实证)。修正方向不变: paper fill 绝不进 live 账本 / live fill 绝不进 paper 账本。
    if (ev.mode_tag != accepted_mode_tag_)
        return;
    // size==0 最后防线 (调用方应已过滤成功/非零; 防漏判)。
    if (ev.filled_size_micro == 0)
        return;

    // side 语义同前: delta 符号由 filled_size_micro 决定 (平仓语义由 DRAIN state 保证)。
    // A1: filled_size_micro 已 int64 micro pUSD, 直存无 cast。R-20: 透传 as_of_ts_ns, 禁 now()。
    std::unique_lock<std::shared_mutex> lk(mu_);
    update_position_locked_(condition_id, token_id, outcome, ev.filled_size_micro, ev.fill_price, ev.as_of_ts_ns,
                            engine);
}

void PositionLedger::update_position_locked_(std::string const& condition_id, std::string const& token_id,
                                             Outcome outcome, std::int64_t delta_usdc, double fill_price,
                                             std::int64_t as_of_ts_ns, std::string const& engine) noexcept {
    auto it = token_positions_.find(token_id);
    if (it == token_positions_.end()) {
        // 新仓
        PositionView pv;
        pv.condition_id = condition_id;
        pv.token_id = token_id;
        pv.outcome = outcome;
        pv.net_shares_micro = delta_usdc;
        pv.avg_entry_price = (delta_usdc != 0 && fill_price > 0.0) ? fill_price : 0.0;
        pv.last_update_ts = as_of_ts_ns;  // R-20: 透传
        token_positions_.emplace(token_id, std::move(pv));
    } else {
        PositionView& pv = it->second;
        auto const old_size = pv.net_shares_micro;
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

        pv.net_shares_micro = new_size;
        pv.last_update_ts = as_of_ts_ns;  // R-20: 透传
    }

    // 更新 condition_exposure_ (signed sum)
    condition_exposure_[condition_id] += delta_usdc;

    // ---- per-engine 加性追踪 (聚合层以上不变) ----
    //   engine 空 → 退化单引擎 (不维护旁路表; 兼容老调用)。聚合 token 全平 → 清该 token 全部引擎份
    //   (结算/全卖统一收口: 各引擎份归零, per-condition-engine 敞口随之自动消)。
    if (!engine.empty()) {
        const auto agg_it = token_positions_.find(token_id);
        const std::int64_t agg_size = (agg_it != token_positions_.end()) ? agg_it->second.net_shares_micro : 0;
        if (agg_size == 0) {
            // 该 token 全平: 清所有引擎份 (key 前缀 = token_id + '\x1f')
            const std::string prefix = token_id + '\x1f';
            for (auto eit = engine_pos_.begin(); eit != engine_pos_.end();) {
                if (eit->first.compare(0, prefix.size(), prefix) == 0)
                    eit = engine_pos_.erase(eit);
                else
                    ++eit;
            }
        } else {
            const std::string k = engine_key_(token_id, engine);
            const std::int64_t v = (engine_pos_[k] += delta_usdc);
            if (v == 0)
                engine_pos_.erase(k);
        }
    }
}

// ---------- read API ---------------------------------------------------------

std::vector<PositionView> PositionLedger::get_all_positions() const noexcept {
    std::shared_lock<std::shared_mutex> lk(mu_);
    std::vector<PositionView> out;
    out.reserve(token_positions_.size());
    for (auto const& [_, pv] : token_positions_) {
        if (pv.net_shares_micro != 0)
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
        out[tid] = pv.net_shares_micro;
    }
    return out;
}

std::unordered_map<std::string, std::int64_t> PositionLedger::get_per_condition_exposure() const noexcept {
    std::shared_lock<std::shared_mutex> lk(mu_);
    return condition_exposure_;
}

// USD 名义 = Σ 股数(micro) × avg_entry_price。股数 micro × price(∈0..1) 仍是 micro USD。
std::unordered_map<std::string, std::int64_t> PositionLedger::get_per_outcome_notional() const noexcept {
    std::shared_lock<std::shared_mutex> lk(mu_);
    std::unordered_map<std::string, std::int64_t> out;
    out.reserve(token_positions_.size());
    for (auto const& [tid, pv] : token_positions_)
        out[tid] = static_cast<std::int64_t>(std::llround(static_cast<double>(pv.net_shares_micro) * pv.avg_entry_price));
    return out;
}

std::unordered_map<std::string, std::int64_t> PositionLedger::get_per_condition_notional() const noexcept {
    std::shared_lock<std::shared_mutex> lk(mu_);
    std::unordered_map<std::string, std::int64_t> out;
    for (auto const& [tid, pv] : token_positions_)
        out[pv.condition_id] +=
            static_cast<std::int64_t>(std::llround(static_cast<double>(pv.net_shares_micro) * pv.avg_entry_price));
    return out;
}

std::int64_t PositionLedger::get_engine_position_notional(std::string const& token_id,
                                                          std::string const& engine) const noexcept {
    std::shared_lock<std::shared_mutex> lk(mu_);
    auto eit = engine_pos_.find(engine_key_(token_id, engine));
    if (eit == engine_pos_.end())
        return 0;
    auto pit = token_positions_.find(token_id);
    const double avg = (pit != token_positions_.end()) ? pit->second.avg_entry_price : 0.0;
    return static_cast<std::int64_t>(std::llround(static_cast<double>(eit->second) * avg));
}

std::int64_t PositionLedger::get_engine_condition_notional(std::string const& condition_id,
                                                           std::string const& engine) const noexcept {
    std::shared_lock<std::shared_mutex> lk(mu_);
    std::string suffix;
    suffix.push_back('\x1f');
    suffix.append(engine);
    std::int64_t sum = 0;
    for (auto const& [k, shares] : engine_pos_) {
        if (k.size() <= suffix.size() ||
            k.compare(k.size() - suffix.size(), suffix.size(), suffix) != 0)
            continue;
        auto pit = token_positions_.find(k.substr(0, k.size() - suffix.size()));
        if (pit != token_positions_.end() && pit->second.condition_id == condition_id)
            sum += static_cast<std::int64_t>(std::llround(static_cast<double>(shares) * pit->second.avg_entry_price));
    }
    return sum;
}

// ---------- per-engine 加性追踪 read API ------------------------------------

std::int64_t PositionLedger::get_engine_position_size(std::string const& token_id,
                                                      std::string const& engine) const noexcept {
    std::shared_lock<std::shared_mutex> lk(mu_);
    auto it = engine_pos_.find(engine_key_(token_id, engine));
    return (it != engine_pos_.end()) ? it->second : 0;
}

std::unordered_map<std::string, std::int64_t>
PositionLedger::get_per_condition_engine_exposure() const noexcept {
    std::shared_lock<std::shared_mutex> lk(mu_);
    std::unordered_map<std::string, std::int64_t> out;
    out.reserve(engine_pos_.size());
    for (auto const& [k, size] : engine_pos_) {
        // k = token_id + '\x1f' + engine → 取 token_id 查 condition_id, 重组 condition + '\x1f' + engine。
        const auto sep = k.find('\x1f');
        if (sep == std::string::npos)
            continue;
        const std::string token_id = k.substr(0, sep);
        const std::string engine = k.substr(sep + 1);
        auto pit = token_positions_.find(token_id);
        if (pit == token_positions_.end())
            continue;
        std::string ckey = pit->second.condition_id;
        ckey.push_back('\x1f');
        ckey.append(engine);
        out[ckey] += size;
    }
    return out;
}

std::int64_t PositionLedger::get_engine_condition_exposure(std::string const& condition_id,
                                                          std::string const& engine) const noexcept {
    std::shared_lock<std::shared_mutex> lk(mu_);
    std::string suffix;
    suffix.push_back('\x1f');
    suffix.append(engine);
    std::int64_t sum = 0;
    for (auto const& [k, size] : engine_pos_) {
        // k = token_id + '\x1f' + engine; 先匹配 engine 后缀, 再查该 token 的 condition_id。
        if (k.size() <= suffix.size() ||
            k.compare(k.size() - suffix.size(), suffix.size(), suffix) != 0)
            continue;
        auto pit = token_positions_.find(k.substr(0, k.size() - suffix.size()));
        if (pit != token_positions_.end() && pit->second.condition_id == condition_id)
            sum += size;
    }
    return sum;
}

std::vector<std::pair<std::string, std::int64_t>>
PositionLedger::get_token_engine_sizes(std::string const& token_id) const noexcept {
    std::shared_lock<std::shared_mutex> lk(mu_);
    std::vector<std::pair<std::string, std::int64_t>> out;
    const std::string prefix = token_id + '\x1f';
    for (auto const& [k, size] : engine_pos_) {
        if (k.compare(0, prefix.size(), prefix) == 0)
            out.emplace_back(k.substr(prefix.size()), size);
    }
    return out;
}

std::unordered_map<std::string, std::int64_t> PositionLedger::get_engine_pos_snapshot() const noexcept {
    std::shared_lock<std::shared_mutex> lk(mu_);
    return engine_pos_;
}

void PositionLedger::restore_engine_split(std::string const& token_id, std::string const& engine,
                                          std::int64_t size) noexcept {
    std::unique_lock<std::shared_mutex> lk(mu_);
    const std::string k = engine_key_(token_id, engine);
    if (size == 0)
        engine_pos_.erase(k);
    else
        engine_pos_[k] = size;
}

}  // namespace stcpp::risk
