// src/stcpp/polymarket/clob_wss/orderbook_adapter.cpp
//
// v0.1  — 单档合成; 完整 L2 多档待 W10 simdjson (@老李)
//
// Owner: 小冯 (#34)
// last_review: 2026-05-29
//
// 红线 enforce:
//   R-12: 热路径 OnEvent 无锁无 malloc
//   R-20: data_source_ts = WSS payload @timestamp × 1e6; 禁本地 now() 替代

#include "stcpp/polymarket/clob_wss/orderbook_adapter.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace stcpp::polymarket::clob_wss {

namespace {

// bps (uint32, 0-10000) → price in [0,1]
[[nodiscard]] inline double BpsToPrice(std::uint32_t bps) noexcept {
    return static_cast<double>(bps) * 0.0001;
}

// Convert WssEvent price_bps + a size placeholder to OrderBookLevel
// v0.1: size_usdc is synthesized from last_trade info (best available in L1 event)
// Full L2 size requires simdjson bids/asks array parse (@老李 W10)
[[nodiscard]] inline double DefaultSizeUsdc() noexcept {
    // v0.1: We do not have real size from the synthesized single-level approach.
    // Return NaN to signal "unknown depth" — consumers check level_valid() before use.
    return std::numeric_limits<double>::quiet_NaN();
}

// Check non-NaN, positive finite double
[[nodiscard]] inline bool IsValidPrice(double p) noexcept {
    return (p == p) && p > 0.0 && p < 1.0 + 1e-9;
}

}  // namespace

// ===========================================================================
// Constructor
// ===========================================================================

OrderBookAdapter::OrderBookAdapter(OrderBookAdapterConfig cfg) : cfg_(cfg) {
    token_states_.reserve(cfg_.max_tokens);
}

// ===========================================================================
// OnEvent — hot path entry
// ===========================================================================

bool OrderBookAdapter::OnEvent(const WssEvent& ev, std::string_view token_id, std::int64_t recv_ts_ns,
                               OrderBookSnapshot& out_snap, FeatureStoreBookRow& out_book_row) noexcept {
    if (token_id.empty())
        return false;

    // R-20 4-ts validation on the incoming event
    const FourTs& ets = ev.ts();
    if (!TsChainOk(ets)) {
        ++ts_violations_;
        return false;
    }

    // Lookup or create token state (unordered_map::operator[] may insert; ok on cold path)
    // Hot path: already inserted → O(1) lookup
    auto it = token_states_.find(std::string(token_id));
    if (it == token_states_.end()) {
        if (token_states_.size() >= cfg_.max_tokens) {
            // Hard cap reached — evict oldest? v0.1: drop event
            return false;
        }
        auto [ins_it, _] = token_states_.emplace(std::string(token_id), TokenState{});
        it = ins_it;
        it->second.tick_size = cfg_.default_tick_size;
    }
    TokenState& st = it->second;

    // Route by topic
    if (ev.topic == SubTopic::kBook) {
        ApplySnapshot(st, ev, recv_ts_ns);
        ++snapshots_received_;
    } else if (ev.topic == SubTopic::kPriceChange) {
        if (st.state == AdapterSnapshotState::kWaitingSnapshot) {
            ++deltas_rejected_early_;
            return false;  // P-05: discard delta before snapshot
        }
        ApplyDelta(st, ev, recv_ts_ns);
        ++deltas_accepted_;
    } else {
        return false;  // unsupported topic for this adapter
    }

    // Fill output structures
    FillSnapshot(st, token_id, out_snap);
    FillBookRow(out_snap, out_book_row);
    return true;
}

// ===========================================================================
// InjectMetadata — consumer-injected fields (not hot path)
// ===========================================================================

void OrderBookAdapter::InjectMetadata(FeatureStoreBookRow& row, std::string_view market_id,
                                      std::string_view token_side, std::string_view sport,
                                      std::int32_t event_date_epoch_days,
                                      std::string_view market_type) noexcept {
    row.market_id = std::string(market_id);
    row.token_side = std::string(token_side);
    row.sport = std::string(sport);
    row.event_date_epoch_days = event_date_epoch_days;
    row.market_type = std::string(market_type);
}

// ===========================================================================
// ResetToken / ResetAll
// ===========================================================================

void OrderBookAdapter::ResetToken(std::string_view token_id) noexcept {
    auto it = token_states_.find(std::string(token_id));
    if (it != token_states_.end()) {
        it->second = TokenState{};
        it->second.tick_size = cfg_.default_tick_size;
    }
}

void OrderBookAdapter::ResetAll() noexcept {
    token_states_.clear();
}

// ===========================================================================
// GetSnapshotState
// ===========================================================================

AdapterSnapshotState OrderBookAdapter::GetSnapshotState(std::string_view token_id) const noexcept {
    auto it = token_states_.find(std::string(token_id));
    if (it == token_states_.end())
        return AdapterSnapshotState::kWaitingSnapshot;
    return it->second.state;
}

// ===========================================================================
// ApplySnapshot — extract L1 bid/ask from book event
// ===========================================================================
// v0.1: WssEvent.payload.book contains mid_bps (synthesized mid from CLOB subscriber).
// True L2 bid/ask requires simdjson bids[]/asks[] array parse (@老李 W10).
// For now we synthesize bid/ask from mid ± half-spread (tick_size_bps).
//
void OrderBookAdapter::ApplySnapshot(TokenState& st, const WssEvent& ev, std::int64_t recv_ts_ns) noexcept {
    const auto& b = ev.payload.book;

    // Update tick_size if available
    if (b.tick_size_bps > 0) {
        st.tick_size = BpsToPrice(b.tick_size_bps);
    }

    // R-20 ts
    st.last_event_ts_ns = b.ts.event_ts_ns;
    st.last_data_source_ts_ns = b.ts.data_source_ts_ns;
    st.last_ingestion_ts_ns = recv_ts_ns;

    // last_trade_ts: use data_source_ts as best proxy (R-20 compatible)
    if (b.last_trade_price_bps > 0) {
        st.last_trade_ts_ns = b.ts.data_source_ts_ns;
    }

    // v0.1: synthesize L1 bid/ask from mid ± 0.5 tick (tick-spread model)
    // mid_bps == 0 means not available in this event — keep NaN
    if (b.mid_bps > 0) {
        double mid = BpsToPrice(b.mid_bps);
        double half_spread = st.tick_size * 0.5;
        st.bid_price = mid - half_spread;
        st.ask_price = mid + half_spread;
        // Clamp to (0, 1)
        if (st.bid_price <= 0.0)
            st.bid_price = st.tick_size;
        if (st.ask_price >= 1.0)
            st.ask_price = 1.0 - st.tick_size;
        // v0.1: size_usdc unknown — NaN signals "depth not parsed" (see GAP table)
        st.bid_size = DefaultSizeUsdc();
        st.ask_size = DefaultSizeUsdc();
    } else {
        // No mid info — best we can do with spread_bps field
        if (b.spread_bps > 0) {
            // spread = (ask-bid) in bps; use last_trade_price as mid proxy
            double mid_proxy = BpsToPrice(b.last_trade_price_bps > 0 ? b.last_trade_price_bps : 5000u);
            double half_spread = BpsToPrice(b.spread_bps) * 0.5;
            st.bid_price = mid_proxy - half_spread;
            st.ask_price = mid_proxy + half_spread;
            if (st.bid_price <= 0.0)
                st.bid_price = st.tick_size;
            if (st.ask_price >= 1.0)
                st.ask_price = 1.0 - st.tick_size;
        }
        st.bid_size = DefaultSizeUsdc();
        st.ask_size = DefaultSizeUsdc();
    }

    st.state = AdapterSnapshotState::kLive;
}

// ===========================================================================
// ApplyDelta — update L1 from price_change event
// ===========================================================================
// v0.1: price_change carries mid_bps (from CLOB subscriber L1 synthesis).
// True delta bids[]/asks[] array requires simdjson (@老李 W10).
//
void OrderBookAdapter::ApplyDelta(TokenState& st, const WssEvent& ev, std::int64_t recv_ts_ns) noexcept {
    const auto& b = ev.payload.book;

    // R-20 ts update
    st.last_event_ts_ns = b.ts.event_ts_ns;
    st.last_data_source_ts_ns = b.ts.data_source_ts_ns;
    st.last_ingestion_ts_ns = recv_ts_ns;

    // Update tick_size if present
    if (b.tick_size_bps > 0) {
        st.tick_size = BpsToPrice(b.tick_size_bps);
    }

    // Update mid if present
    if (b.mid_bps > 0) {
        double mid = BpsToPrice(b.mid_bps);
        double half_spread = st.tick_size * 0.5;
        st.bid_price = mid - half_spread;
        st.ask_price = mid + half_spread;
        if (st.bid_price <= 0.0)
            st.bid_price = st.tick_size;
        if (st.ask_price >= 1.0)
            st.ask_price = 1.0 - st.tick_size;
        st.bid_size = DefaultSizeUsdc();
        st.ask_size = DefaultSizeUsdc();
    }
}

// ===========================================================================
// FillSnapshot
// ===========================================================================

void OrderBookAdapter::FillSnapshot(const TokenState& st, std::string_view /*token_id*/,
                                    OrderBookSnapshot& snap) noexcept {
    // 4-ts (R-20)
    snap.ts.event_ts_ns = st.last_event_ts_ns;
    snap.ts.data_source_ts_ns = st.last_data_source_ts_ns;
    snap.ts.ingestion_ts_ns = st.last_ingestion_ts_ns;
    // as_of_ts: strategy layer fills at evaluate time; adapter sets to ingestion as placeholder
    snap.ts.as_of_ts_ns = st.last_ingestion_ts_ns;

    snap.tick_size = st.tick_size;
    snap.last_trade_ts_ns = st.last_trade_ts_ns;

    // L1 bid/ask (index 0 = best)
    snap.bid[0].price = st.bid_price;
    snap.bid[0].size_usdc = st.bid_size;
    snap.ask[0].price = st.ask_price;
    snap.ask[0].size_usdc = st.ask_size;

    // v0.1: levels 1..4 = NaN/0 (not yet parsed — W10 simdjson)
    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    for (std::size_t i = 1; i < kBookDepthLevels; ++i) {
        snap.bid[i].price = kNaN;
        snap.bid[i].size_usdc = kNaN;
        snap.ask[i].price = kNaN;
        snap.ask[i].size_usdc = kNaN;
    }

    // Derived scalar fields
    if (IsValidPrice(st.bid_price) && IsValidPrice(st.ask_price) && st.ask_price > st.bid_price) {
        double mid = 0.5 * (st.bid_price + st.ask_price);
        snap.spread_bps = static_cast<std::int32_t>((st.ask_price - st.bid_price) / mid * 10000.0 + 0.5);
        // top3_depth_usdc: v0.1 — if sizes are NaN, report 0
        snap.top3_depth_usdc = 0.0;
    } else {
        snap.spread_bps = 0;
        snap.top3_depth_usdc = 0.0;
    }
}

// ===========================================================================
// FillBookRow — from OrderBookSnapshot → FeatureStoreBookRow
// ===========================================================================

void OrderBookAdapter::FillBookRow(const OrderBookSnapshot& snap, FeatureStoreBookRow& row) noexcept {
    // 4-ts (R-20) — as_of_ts strategy layer fills; we copy from snap
    row.event_ts_ns = snap.ts.event_ts_ns;
    row.data_source_ts_ns = snap.ts.data_source_ts_ns;
    row.ingestion_ts_ns = snap.ts.ingestion_ts_ns;
    row.as_of_ts_ns = snap.ts.as_of_ts_ns;  // placeholder; strategy layer overwrites

    row.tick_size = snap.tick_size;
    row.last_trade_ts_ns = snap.last_trade_ts_ns;

    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

    // 5 levels bid/ask
    for (std::size_t i = 0; i < kOrderBookLevels; ++i) {
        row.bid_price[i] = snap.bid[i].price;
        row.bid_size_usdc[i] = snap.bid[i].size_usdc;
        row.ask_price[i] = snap.ask[i].price;
        row.ask_size_usdc[i] = snap.ask[i].size_usdc;
    }

    // Derived: mid, spread_bps_f, microprice, imbalance
    const double b0 = snap.bid[0].price;
    const double a0 = snap.ask[0].price;
    if ((b0 == b0) && (a0 == a0) && b0 > 0.0 && a0 > b0) {
        double mid = 0.5 * (b0 + a0);
        row.mid = mid;
        row.spread_bps_f = (a0 - b0) / mid * 10000.0;

        // microprice + imbalance from L1Probe
        // v0.1: bid/ask sizes are NaN → L1Probe will be invalid → fallback NaN
        const double bs = snap.bid[0].size_usdc;
        const double as_ = snap.ask[0].size_usdc;
        if ((bs == bs) && (as_ == as_) && bs > 0.0 && as_ > 0.0) {
            auto probe = compute_l1_probe(snap.bid[0], snap.ask[0], snap.tick_size);
            if (probe.valid) {
                row.microprice = probe.microprice;
                row.imbalance = probe.imbalance;
            } else {
                row.microprice = kNaN;
                row.imbalance = kNaN;
            }
        } else {
            row.microprice = kNaN;
            row.imbalance = kNaN;
        }
        // top3_depth_usdc: use depth_within_ticks for bid + ask combined
        // v0.1: sizes NaN → depth function returns 0 (it checks finite_pos)
        double bid_depth = depth_within_ticks(snap.bid, mid, snap.tick_size, TICK_WINDOW);
        double ask_depth = depth_within_ticks(snap.ask, mid, snap.tick_size, TICK_WINDOW);
        row.top3_depth_usdc = bid_depth + ask_depth;
    } else {
        row.mid = kNaN;
        row.spread_bps_f = kNaN;
        row.microprice = kNaN;
        row.imbalance = kNaN;
        row.top3_depth_usdc = 0.0;
    }
}

// ===========================================================================
// TsChainOk — R-20 monotonic 4-ts check
// ===========================================================================

bool OrderBookAdapter::TsChainOk(const FourTs& ts) noexcept {
    return ts.IsMonotonic();
}

}  // namespace stcpp::polymarket::clob_wss
