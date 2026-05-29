// stcpp/execution/virtual_matcher.cpp — VirtualMatcher Mode A++ + Mode A (Wave 3) 实现
//
// 落: xiaojiang-paper-engine-skeleton-v1.md §4
//     xiaocheng-p0-02-alpha-v2-backtest-spec-v0.2.md §2.1 (slippage ~0.3%)
// 红线: R-7 / R-11 / R-20 (见 .hpp 头注)
// W6: VirtualFill 加 market_id/outcome 透传 (@小蒋 Wave 29)
// Wave 3: MatchWithBook() Mode A 真实 CLOB depth 撮合 (禁止理想全成交)

#include "stcpp/execution/virtual_matcher.hpp"

#include <algorithm>
#include <cstring>

namespace stcpp::execution {

namespace {

[[nodiscard]] inline double clamp01(double x) noexcept {
    if (x < 0.0) return 0.0;
    if (x > 1.0) return 1.0;
    return x;
}

}  // namespace

VirtualFill VirtualMatcher::Match(const VirtualOrder& order) noexcept {
    VirtualFill out;
    // R-20 透传 4 ts
    out.event_ts_ns       = order.event_ts_ns;
    out.data_source_ts_ns = order.data_source_ts_ns;
    out.ingestion_ts_ns   = order.ingestion_ts_ns;
    out.as_of_ts_ns       = order.as_of_ts_ns;
    out.fill_ts_ns        = order.wall_now_ns;
    out.audit_wal_kind    = infra::wal::WalKind::PaperAudit;  // R-11

    // W6: market_id / outcome 从 VirtualOrder 透传 (@小蒋 Wave 29)
    // market_id: string_view → array<char,32> (null-padded, 截断至 32B)
    {
        const std::size_t mlen =
            std::min(order.market_id.size(), static_cast<std::size_t>(32));
        std::memcpy(out.market_id.data(), order.market_id.data(), mlen);
        // remaining bytes already zero (VirtualFill default-init)
    }
    // outcome: string_view "YES"→0, 其他→1 (与 PositionRecord.outcome 对齐)
    out.outcome = (order.outcome == "YES") ? std::uint8_t{0} : std::uint8_t{1};

    // 1) SlippageModel.compute
    numerical::SlippageInput sin;
    sin.order_size_usdc      = order.size_usdc;
    sin.quote_price          = order.quote_price;
    sin.book_depth_l1_usdc   = order.book_depth_l1_usdc;
    sin.book_snapshot_ts_ns  = order.event_ts_ns;
    sin.wall_now_ns          = order.wall_now_ns;
    sin.tick_size            = order.tick_size;

    const auto so = numerical::SlippageModel::compute(sin, numerical::SlippageMode::Linear);
    out.expected_fill_rate = so.expected_fill_rate;
    out.slippage_bps       = so.slippage_bps;
    out.fill_price         = so.expected_fill_price;

    if (so.reject != numerical::RejectCode::Ok) {
        out.reject         = MatchReject::SlippageModelReject;
        out.fill_size_usdc = 0.0;
        out.p_fill_clamped = 0.0;
        out.bernoulli_draw = false;
        return out;
    }

    // 2) p_fill = clamp(rate, FLOOR, CAP)  (Mode A++ 小袁 microstructure v1)
    const double rate01      = clamp01(so.expected_fill_rate);
    const double p_clamped   = std::min(kFillRateCap, std::max(kFillRateFloor, rate01));
    out.p_fill_clamped       = p_clamped;

    // 3) Bernoulli(p_clamped) draw
    double u;
    if (has_override_) {
        u = uniform_override_;
    } else {
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        u = dist(rng_);
    }
    const bool draw = (u < p_clamped);
    out.bernoulli_draw = draw;

    if (!draw) {
        out.reject         = MatchReject::BernoulliMissed;
        out.fill_size_usdc = 0.0;
        return out;
    }

    // 4) Fill: 按 expected_fill_rate 折扣 size (而非 p_clamped, 因 cap 上限只是 Bernoulli 参数,
    //    实际 size 还是按真实 model rate, 保留 audit 可还原性)
    out.reject         = MatchReject::Ok;
    out.fill_size_usdc = order.size_usdc * rate01;
    return out;
}

// ---------------------------------------------------------------------------
// MatchWithBook — Wave 3 Mode A: 真实 CLOB depth + queue position fill
// ---------------------------------------------------------------------------
VirtualFill VirtualMatcher::MatchWithBook(const VirtualOrderWithBook& order) noexcept {
    VirtualFill out;

    // R-20 透传 4 ts
    out.event_ts_ns       = order.event_ts_ns;
    out.data_source_ts_ns = order.data_source_ts_ns;
    out.ingestion_ts_ns   = order.ingestion_ts_ns;
    out.as_of_ts_ns       = order.as_of_ts_ns;
    out.fill_ts_ns        = order.wall_now_ns;
    out.audit_wal_kind    = infra::wal::WalKind::PaperAudit;  // R-11 硬填

    // market_id / outcome 透传
    {
        const std::size_t mlen =
            std::min(order.market_id.size(), static_cast<std::size_t>(32));
        std::memcpy(out.market_id.data(), order.market_id.data(), mlen);
    }
    out.outcome = (order.outcome == "YES") ? std::uint8_t{0} : std::uint8_t{1};

    // === Step 1: FillRateModel::compute_from_clob_book ===
    //
    // fill_intent 中的 size_usdc 使用 order.size_usdc (而非 fill_intent 内置, 保持一致)
    microstructure::FillIntent intent_with_size = order.fill_intent;
    intent_with_size.size_usdc = order.size_usdc;

    const auto clob_out = microstructure::FillRateModel::compute_from_clob_book(
        order.book, order.probe, intent_with_size);

    // ClobModelReject: InvalidSnapshot 或 InvalidIntent → 拒单
    if (clob_out.reject == microstructure::FillRateReject::InvalidSnapshot) {
        out.reject         = MatchReject::InvalidBookSnapshot;
        out.fill_size_usdc = 0.0;
        return out;
    }
    if (clob_out.reject == microstructure::FillRateReject::InvalidIntent) {
        out.reject         = MatchReject::ClobModelReject;
        out.fill_size_usdc = 0.0;
        return out;
    }
    // BelowFloor: 标记但继续, fill_rate < 0.50 会在下面 clamp 到 FLOOR

    out.expected_fill_rate = clob_out.fill_rate;
    out.slippage_bps       = clob_out.slippage_bps;

    // === Step 2: p_fill = clamp(fill_rate, FLOOR=0.50, CAP=0.90) ===
    const double rate01    = clamp01(clob_out.fill_rate);
    const double p_clamped = std::min(kFillRateClobCap, std::max(kFillRateFloor, rate01));
    out.p_fill_clamped     = p_clamped;

    // BelowFloor 拒单 (fill_rate < FLOOR 即使 clamp 也无意义)
    if (clob_out.reject == microstructure::FillRateReject::BelowFloor) {
        out.reject         = MatchReject::ClobModelReject;
        out.fill_size_usdc = 0.0;
        out.bernoulli_draw = false;
        return out;
    }

    // === Step 3: fill_price = price ± slippage ===
    //
    // Mode A: slippage 是 ClobFillOutput 直接给的保守估算 (~0.3%)
    // Buy: fill_price 比 quote 高一点 (taker 吃单加价)
    // Sell: fill_price 比 quote 低一点
    const double quote_price = intent_with_size.price;
    const double slippage    = clob_out.slippage_rate;  // ∈ [0.00001, 0.005]
    if (intent_with_size.side == microstructure::Side::Buy) {
        out.fill_price = quote_price + slippage;
    } else {
        out.fill_price = quote_price - slippage;
    }
    // clamp fill_price to (0, 1)
    if (out.fill_price <= 0.0) out.fill_price = 0.001;
    if (out.fill_price >= 1.0) out.fill_price = 0.999;

    // === Step 4: fill_size = size_usdc × p_fill ===
    //
    // 关键约束: 禁止理想化全成交 (CPO 要求, 基于真实 depth 的部分成交)
    // p_fill = p_clamped ∈ [0.50, 0.90], 所以 fill_size < size_usdc (保证部分成交逻辑)
    out.reject         = MatchReject::Ok;
    out.bernoulli_draw = true;  // Mode A: 有成交
    out.fill_size_usdc = order.size_usdc * p_clamped;

    return out;
}

}  // namespace stcpp::execution
