// src/stcpp/polymarket/clob_wss/orderbook_adapter.cpp
//
// Owner: 小冯 (#34)  主管: 小余 (D 数据基础设施部)
// ADR-037 data-model-strategy-vendor-agnostic 配套实施
// Wave 小余-ADR037 — Polymarket CLOB full-depth orderbook adapter v0.1
//
// 红线 enforce:
//   R-12: Compute() / OnDelta() / OnSnapshot() 无 IO / malloc / 锁
//   R-20: ts.data_source_ts_ns 来自上游 WssEvent FourTs, 不 now() 替代
//   R-7:  header-only types; 无 ExecutionMode 依赖

#include "stcpp/polymarket/clob_wss/orderbook_adapter.hpp"

#include <algorithm>
#include <cmath>

namespace stcpp::polymarket::clob_wss {

namespace detail {

// Pure computation helpers — no IO, no malloc, constexpr where possible

[[nodiscard]] inline bool finite_pos(double x) noexcept {
    return std::isfinite(x) && x > 0.0;
}

[[nodiscard]] inline bool finite(double x) noexcept {
    return std::isfinite(x);
}

// Clamp to [lo, hi]
[[nodiscard]] inline double clamp(double x, double lo, double hi) noexcept {
    return x < lo ? lo : (x > hi ? hi : x);
}

// Sum notional of first n levels (USD)
[[nodiscard]] inline double sum_depth(
    const std::array<stcpp::microstructure::OrderBookLevel,
                     kAdapterBookDepth>& side,
    std::uint8_t n) noexcept {
    double s = 0.0;
    for (std::uint8_t i = 0; i < n && i < kAdapterBookDepth; ++i) {
        if (finite_pos(side[i].size_usdc)) s += side[i].size_usdc;
    }
    return s;
}

// Sum notional of first min(n, 3) levels
[[nodiscard]] inline double sum_top3(
    const std::array<stcpp::microstructure::OrderBookLevel,
                     kAdapterBookDepth>& side,
    std::uint8_t n) noexcept {
    return sum_depth(side, std::min(n, static_cast<std::uint8_t>(3)));
}

}  // namespace detail

// ===========================================================================
// OrderBookAdapterState
// ===========================================================================

void OrderBookAdapterState::Reset() noexcept {
    bid_        = {};
    ask_        = {};
    bid_depth_  = 0;
    ask_depth_  = 0;
    last_trade_price_ = 0.0;
    last_trade_ts_ns_ = 0;
    state_      = AdapterBookState::kWaitingSnapshot;
}

void OrderBookAdapterState::FillSide(
    std::array<stcpp::microstructure::OrderBookLevel, kAdapterBookDepth>& dst,
    std::uint8_t& dst_n,
    const stcpp::microstructure::OrderBookLevel* src,
    std::uint8_t src_n) noexcept {
    dst_n = std::min(src_n, static_cast<std::uint8_t>(kAdapterBookDepth));
    for (std::uint8_t i = 0; i < dst_n; ++i) dst[i] = src[i];
    // zero-fill remaining slots
    for (std::uint8_t i = dst_n; i < kAdapterBookDepth; ++i) dst[i] = {};
}

void OrderBookAdapterState::UpsertLevel(
    std::array<stcpp::microstructure::OrderBookLevel, kAdapterBookDepth>& side,
    std::uint8_t& depth,
    const stcpp::microstructure::OrderBookLevel& lvl,
    bool ascending) noexcept {
    if (!detail::finite_pos(lvl.price)) return;

    constexpr double kPriceEps = 1e-7;

    // Try to find existing level at same price
    for (std::uint8_t i = 0; i < depth && i < kAdapterBookDepth; ++i) {
        if (std::abs(side[i].price - lvl.price) < kPriceEps) {
            if (lvl.size_usdc <= 0.0) {
                // Remove level: shift down
                for (std::uint8_t j = i; j + 1 < depth && j + 1 < kAdapterBookDepth; ++j) {
                    side[j] = side[j + 1];
                }
                if (depth > 0) { --depth; side[depth] = {}; }
            } else {
                side[i].size_usdc = lvl.size_usdc;
            }
            return;
        }
    }

    // Price not found: insert (keep sorted order; drop if no room)
    if (lvl.size_usdc <= 0.0) return;  // nothing to insert for a zero-size removal
    if (depth >= kAdapterBookDepth) return;  // no room (price too far from best)

    // Find insertion index (ascending: smallest price first; descending: largest first)
    std::uint8_t ins = depth;
    for (std::uint8_t i = 0; i < depth && i < kAdapterBookDepth; ++i) {
        bool insert_before = ascending
            ? (lvl.price < side[i].price)
            : (lvl.price > side[i].price);
        if (insert_before) { ins = i; break; }
    }
    // Shift right
    for (std::uint8_t i = depth; i > ins && i < kAdapterBookDepth; --i) {
        side[i] = side[i - 1];
    }
    side[ins] = lvl;
    ++depth;
}

OrderBookFeatures OrderBookAdapterState::OnSnapshot(
    const stcpp::microstructure::OrderBookLevel* bid_levels, std::uint8_t bid_n,
    const stcpp::microstructure::OrderBookLevel* ask_levels, std::uint8_t ask_n,
    const stcpp::microstructure::OrderBookTs& ts,
    double tick_size,
    double last_trade_price,
    std::int64_t last_trade_ts_ns) noexcept {

    FillSide(bid_, bid_depth_, bid_levels, bid_n);
    FillSide(ask_, ask_depth_, ask_levels, ask_n);
    tick_size_        = detail::finite_pos(tick_size) ? tick_size : stcpp::microstructure::TICK_01;
    last_trade_price_ = last_trade_price;
    last_trade_ts_ns_ = last_trade_ts_ns;
    state_            = AdapterBookState::kLive;

    return Compute(ts, /*is_snapshot=*/true);
}

OrderBookFeatures OrderBookAdapterState::OnDelta(
    const stcpp::microstructure::OrderBookLevel* bid_levels, std::uint8_t bid_n,
    const stcpp::microstructure::OrderBookLevel* ask_levels, std::uint8_t ask_n,
    const stcpp::microstructure::OrderBookTs& ts,
    double tick_size) noexcept {

    if (state_ == AdapterBookState::kWaitingSnapshot) {
        // Drop delta until snapshot received (reconnect / gap safety)
        OrderBookFeatures invalid{};
        invalid.valid = false;
        return invalid;
    }

    if (detail::finite_pos(tick_size)) tick_size_ = tick_size;

    // Upsert each delta level (ascending=false for bid, ascending=true for ask)
    for (std::uint8_t i = 0; i < bid_n; ++i) {
        UpsertLevel(bid_, bid_depth_, bid_levels[i], /*ascending=*/false);
    }
    for (std::uint8_t i = 0; i < ask_n; ++i) {
        UpsertLevel(ask_, ask_depth_, ask_levels[i], /*ascending=*/true);
    }

    return Compute(ts, /*is_snapshot=*/false);
}

OrderBookFeatures OrderBookAdapterState::Compute(
    const stcpp::microstructure::OrderBookTs& ts,
    bool is_snapshot) const noexcept {

    OrderBookFeatures out{};
    out.ts          = ts;
    out.is_snapshot = is_snapshot;
    out.tick_size   = tick_size_;
    out.last_trade_price  = last_trade_price_;
    out.last_trade_ts_ns  = last_trade_ts_ns_;
    out.bid_depth   = bid_depth_;
    out.ask_depth   = ask_depth_;

    // Copy levels
    for (std::uint8_t i = 0; i < bid_depth_ && i < kAdapterBookDepth; ++i) out.bid[i] = bid_[i];
    for (std::uint8_t i = 0; i < ask_depth_ && i < kAdapterBookDepth; ++i) out.ask[i] = ask_[i];

    // Depth aggregates
    out.total_depth_usdc_bid = detail::sum_depth(bid_, bid_depth_);
    out.total_depth_usdc_ask = detail::sum_depth(ask_, ask_depth_);
    out.top3_depth_usdc_bid  = detail::sum_top3(bid_, bid_depth_);
    out.top3_depth_usdc_ask  = detail::sum_top3(ask_, ask_depth_);

    // L1 microstructure (need at least best bid + best ask)
    if (bid_depth_ == 0 || ask_depth_ == 0) {
        out.valid = true;  // book valid but L1 features undefined
        return out;
    }

    const auto& bb = bid_[0];
    const auto& ba = ask_[0];

    if (!detail::finite_pos(bb.price) || !detail::finite_pos(ba.price)
        || ba.price <= bb.price) {
        // Crossed or locked book — set valid=true, but L1 features stay 0
        out.valid = true;
        return out;
    }

    out.mid    = 0.5 * (bb.price + ba.price);
    out.spread = ba.price - bb.price;
    out.spread_bps = static_cast<std::int32_t>(out.spread * 10000.0 + 0.5);

    // Imbalance = (bid_qty - ask_qty) / (bid_qty + ask_qty)  [L1 only]
    double bq  = detail::finite_pos(bb.size_usdc) ? bb.size_usdc : 0.0;
    double aq  = detail::finite_pos(ba.size_usdc) ? ba.size_usdc : 0.0;
    double sum = bq + aq;
    if (sum > 1e-12) {
        out.imbalance = (bq - aq) / sum;
        // Microprice = (bid_qty * best_ask + ask_qty * best_bid) / sum
        double micro_raw = (bq * ba.price + aq * bb.price) / sum;
        // §2.3 cap: |micro - mid| ≤ 2 ticks
        double cap   = 2.0 * tick_size_;
        double delta = micro_raw - out.mid;
        delta        = detail::clamp(delta, -cap, cap);
        out.microprice = out.mid + delta;
    } else {
        out.microprice = out.mid;
    }

    out.valid = true;
    return out;
}

// ===========================================================================
// OrderBookAdapter
// ===========================================================================

OrderBookAdapter::OrderBookAdapter(IOrderBookFeatureSink* sink) noexcept
    : sink_(sink) {
    tokens_ = {};
}

bool OrderBookAdapter::RegisterToken(std::string_view token_id) noexcept {
    if (token_count_ >= kAdapterMaxTokens) {
        ++drop_count_;
        return false;
    }
    // Check duplicate
    for (std::uint8_t i = 0; i < token_count_; ++i) {
        if (tokens_[i].token_id == token_id) return true;  // already registered
    }
    tokens_[token_count_].token_id = std::string(token_id);
    tokens_[token_count_].active   = true;
    tokens_[token_count_].state    = OrderBookAdapterState{};
    ++token_count_;
    return true;
}

void OrderBookAdapter::OnTransportReset() noexcept {
    for (std::uint8_t i = 0; i < token_count_; ++i) {
        if (tokens_[i].active) tokens_[i].state.Reset();
    }
}

void OrderBookAdapter::OnTokenReset(std::string_view token_id) noexcept {
    auto* entry = FindToken(token_id);
    if (entry) entry->state.Reset();
}

AdapterTokenEntry* OrderBookAdapter::FindToken(std::string_view token_id) noexcept {
    for (std::uint8_t i = 0; i < token_count_; ++i) {
        if (tokens_[i].active && tokens_[i].token_id == token_id) {
            return &tokens_[i];
        }
    }
    return nullptr;
}

// R-20: convert FourTs → OrderBookTs (preserving upstream timestamps verbatim)
stcpp::microstructure::OrderBookTs OrderBookAdapter::ToObTs(
    const stcpp::polymarket::wss::FourTs& fts) noexcept {
    stcpp::microstructure::OrderBookTs ts{};
    ts.event_ts_ns       = fts.event_ts_ns;
    ts.data_source_ts_ns = fts.data_source_ts_ns;
    ts.ingestion_ts_ns   = fts.ingestion_ts_ns;
    ts.as_of_ts_ns       = fts.as_of_ts_ns;
    return ts;
}

void OrderBookAdapter::ExtractBookLevels(
    const stcpp::polymarket::wss::OrderBookL2Update& book,
    stcpp::microstructure::OrderBookLevel* bid_out, std::uint8_t& bid_n,
    stcpp::microstructure::OrderBookLevel* ask_out, std::uint8_t& ask_n,
    double& tick_size_out,
    double& last_trade_price_out) noexcept {

    bid_n = 0;
    ask_n = 0;

    // tick_size: from WssEvent field (bps → double)
    tick_size_out = (book.tick_size_bps > 0)
        ? BpsToPrice(book.tick_size_bps)
        : stcpp::microstructure::TICK_01;

    // last_trade_price: from WssEvent field
    last_trade_price_out = (book.last_trade_price_bps > 0)
        ? BpsToPrice(book.last_trade_price_bps)
        : 0.0;

    // v0.1: WssEvent OrderBookL2Update does not yet carry decoded price levels
    // (full L2 array parsing deferred to W10 simdjson, @老李).
    // We synthesize best bid/ask from mid_bps ± spread_bps/2 as a placeholder.
    // When mid_bps is available, generate a synthetic 1-level book.
    if (book.mid_bps > 0 && book.spread_bps > 0) {
        double mid    = BpsToPrice(book.mid_bps);
        double spread = BpsToPrice(book.spread_bps);
        double half   = spread * 0.5;

        // bid: mid - half_spread, ask: mid + half_spread
        // size: use last_trade_price as rough proxy for notional (stub until W10)
        double proxy_size = (last_trade_price_out > 0.0) ? last_trade_price_out * 100.0 : 50.0;

        bid_out[0] = { mid - half, proxy_size };
        ask_out[0] = { mid + half, proxy_size };
        bid_n = 1;
        ask_n = 1;
    } else if (book.last_trade_price_bps > 0) {
        // Fallback: single level at last_trade_price with unit size
        double price = last_trade_price_out;
        if (price > 0.01 && price < 0.99) {
            double proxy_size = 50.0;
            bid_out[0] = { price - tick_size_out, proxy_size };
            ask_out[0] = { price + tick_size_out, proxy_size };
            bid_n = 1;
            ask_n = 1;
        }
    }
    // changes[] array: v0.1 stub — num_changes=0 always (W10 simdjson will fill)
    // When changes[] arrives, iterate and call UpsertLevel per entry.
}

void OrderBookAdapter::ProcessEvent(
    std::string_view token_id,
    const stcpp::polymarket::wss::WssEvent& ev) noexcept {

    using stcpp::polymarket::wss::SubTopic;

    if (ev.topic != SubTopic::kBook && ev.topic != SubTopic::kPriceChange) return;

    auto* entry = FindToken(token_id);
    if (!entry) {
        ++drop_count_;
        return;
    }

    const auto& book = ev.payload.book;
    auto ts = ToObTs(ev.ts());

    stcpp::microstructure::OrderBookLevel bid_lvls[kAdapterBookDepth]{};
    stcpp::microstructure::OrderBookLevel ask_lvls[kAdapterBookDepth]{};
    std::uint8_t bid_n = 0, ask_n = 0;
    double tick_size       = stcpp::microstructure::TICK_01;
    double last_trade_price = 0.0;

    ExtractBookLevels(book, bid_lvls, bid_n, ask_lvls, ask_n,
                      tick_size, last_trade_price);

    OrderBookFeatures features{};
    if (book.is_snapshot) {
        ++snapshot_count_;
        features = entry->state.OnSnapshot(
            bid_lvls, bid_n, ask_lvls, ask_n,
            ts, tick_size, last_trade_price, ts.data_source_ts_ns);
    } else {
        ++delta_count_;
        features = entry->state.OnDelta(
            bid_lvls, bid_n, ask_lvls, ask_n,
            ts, tick_size);
    }

    if (!features.valid) {
        ++drop_count_;
        return;
    }

    if (sink_) {
        if (!sink_->OnFeatures(features)) {
            ++drop_count_;
        }
    }
}

}  // namespace stcpp::polymarket::clob_wss
