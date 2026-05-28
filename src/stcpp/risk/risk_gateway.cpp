// stcpp/risk/risk_gateway.cpp — RiskGateway v0.1 (老韩 Sprint-2 W4 Wave 19)
//
// 21 reject rule short-circuit 优先级:
//   state → invalid_intent (含 PIT) → duplicate → stale_data → market →
//   liquidity (depth/fill/slippage) → position caps → signal → strategy_decayed
// emit 失败 → AUDIT_WAL_BACKPRESSURE; INTERNAL_ERROR 兜底.

#include "stcpp/risk/risk_gateway.hpp"

#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "stcpp/infra/wal/pit.hpp"

namespace stcpp::risk {

namespace {

[[nodiscard]] std::int64_t now_realtime_ns() noexcept {
    return infra::wal::pit::NowRealtimeNs();
}

// PIT 4 ts 5 violation 映射 (老韩 v0.3.1 + 老孙 v5.1)
[[nodiscard]] InvalidIntentSubReason pit_violation_to_sub(OrderIntent const& it,
                                                          std::int64_t now_ns) noexcept {
    if (it.event_ts_ns <= 0) {
        return InvalidIntentSubReason::BOOK_TS_ZERO;
    }
    if (it.data_source_ts_ns < it.event_ts_ns)        return InvalidIntentSubReason::TS_ORDER_VIOLATED;
    if (it.ingestion_ts_ns   < it.data_source_ts_ns)  return InvalidIntentSubReason::TS_ORDER_VIOLATED;
    if (it.as_of_ts_ns       < it.ingestion_ts_ns)    return InvalidIntentSubReason::TS_ORDER_VIOLATED;
    if (it.as_of_ts_ns       > now_ns)                return InvalidIntentSubReason::TS_FUTURE;
    return InvalidIntentSubReason::NONE;
}

[[nodiscard]] bool is_finite(double x) noexcept {
    return !(x != x) && x > -1e300 && x < 1e300;
}

// SlippageModel sub_reason → RM sub_reason (lib-local → risk::)
[[nodiscard]] InvalidIntentSubReason map_slippage_sub(numerical::InvalidIntentSubReason s) noexcept {
    using S = numerical::InvalidIntentSubReason;
    switch (s) {
        case S::BookTsZero:  return InvalidIntentSubReason::BOOK_TS_ZERO;
        case S::BookTsStale: return InvalidIntentSubReason::BOOK_TS_STALE;
        case S::NanOrInf:    return InvalidIntentSubReason::NAN_OR_INF;
        case S::Negative:    return InvalidIntentSubReason::NEGATIVE;
        case S::IllegalTick: return InvalidIntentSubReason::ILLEGAL_TICK;
        case S::None:        return InvalidIntentSubReason::NONE;
    }
    return InvalidIntentSubReason::NONE;
}

}  // namespace

// pImpl: 幂等 cache + per-market exposure / freshness / state / 信号缓存
struct RiskGateway::State_ {
    mutable std::mutex                                     mu;
    std::unordered_set<std::string>                        seen_signal_ids;
    std::unordered_map<std::string, std::int64_t>          market_exposure_usdc;
    std::unordered_map<std::string, std::uint32_t>         market_freshness_ms;
    std::unordered_map<std::string, MarketState>           market_state;
    std::unordered_map<std::string, bool>                  market_active;
    std::unordered_map<std::string, double>                signal_edge_ci_lower;
    std::unordered_map<std::string, double>                strategy_ev_ratio;
};

// ---------- ctor -------------------------------------------------------------

RiskGateway::RiskGateway(RiskConfig cfg,
                         std::shared_ptr<AuditEmitter> emitter) noexcept
    : cfg_(cfg), emitter_(std::move(emitter)), s_(std::make_unique<State_>()) {
    bankroll_usdc_.store(cfg.bankroll_usdc);
}

RiskGateway::~RiskGateway() = default;

// ---------- setter (W5 接真账本 / 真 ledger) ----------------------------------

void RiskGateway::set_market_exposure(std::string const& m, std::int64_t v) noexcept {
    std::lock_guard<std::mutex> g(s_->mu);
    s_->market_exposure_usdc[m] = v;
}
void RiskGateway::set_edge_ci_lower(std::string const& sig, double v) noexcept {
    std::lock_guard<std::mutex> g(s_->mu); s_->signal_edge_ci_lower[sig] = v;
}
void RiskGateway::set_strategy_ev_ratio(std::string const& sid, double r) noexcept {
    std::lock_guard<std::mutex> g(s_->mu); s_->strategy_ev_ratio[sid] = r;
}
void RiskGateway::set_market_freshness_ms(std::string const& m, std::uint32_t ms) noexcept {
    std::lock_guard<std::mutex> g(s_->mu); s_->market_freshness_ms[m] = ms;
}
void RiskGateway::set_market_state(std::string const& m, MarketState st) noexcept {
    std::lock_guard<std::mutex> g(s_->mu); s_->market_state[m] = st;
}
void RiskGateway::set_market_active(std::string const& m, bool active) noexcept {
    std::lock_guard<std::mutex> g(s_->mu); s_->market_active[m] = active;
}
void RiskGateway::clear_idempotency() noexcept {
    std::lock_guard<std::mutex> g(s_->mu); s_->seen_signal_ids.clear();
}

// ---------- audit_id 生成 (ULID stub: 6B ts ms-big-endian + 10B counter) ------

std::array<std::uint8_t, 16> RiskGateway::next_audit_id(std::int64_t now_ns) noexcept {
    // ULID monotonic stub. 单测用; 真上线 W5 接老孙 ULID generator + thread-local seq.
    static std::atomic<std::uint64_t> g_seq{0};
    std::array<std::uint8_t, 16> out{};
    auto const ts_ms = static_cast<std::uint64_t>(now_ns / 1'000'000LL);
    for (int i = 5; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(ts_ms >> ((5 - i) * 8) & 0xFF);
    }
    auto const seq = g_seq.fetch_add(1, std::memory_order_relaxed);
    for (int i = 0; i < 10; ++i) {
        out[6 + static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>(seq >> ((9 - i) * 8) & 0xFF);
    }
    return out;
}

// ---------- 21 rule helpers --------------------------------------------------

bool RiskGateway::check_state_(OrderIntent const& it, RiskDecision& d) const noexcept {
    auto const st = state_.load(std::memory_order_acquire);
    // 开仓在 HALTED / DRAIN / SAFE_MODE 一律拒. 平仓 (is_close) 在 DRAIN / SAFE_MODE 放行, HALTED 仍拒.
    switch (st) {
        case RmState::HALTED:
            d.reject = RejectCode::STATE_HALTED;
            return true;
        case RmState::DRAIN:
            if (!it.is_close) { d.reject = RejectCode::STATE_DRAIN; return true; }
            return false;
        case RmState::SAFE_MODE:
            if (!it.is_close) { d.reject = RejectCode::STATE_SAFE_MODE; return true; }
            return false;
        case RmState::RUNNING:
        case RmState::WARNING:
            return false;
    }
    return false;
}

bool RiskGateway::check_invalid_intent_(OrderIntent const& it, RiskDecision& d) const noexcept {
    auto const now = now_realtime_ns();
    // (a) R-20 4 ts PIT chain (5 violation)
    auto const pit_sub = pit_violation_to_sub(it, now);
    auto fail = [&](InvalidIntentSubReason s) noexcept {
        d.reject = RejectCode::INVALID_INTENT; d.sub_reason = s; return true;
    };
    if (pit_sub != InvalidIntentSubReason::NONE) return fail(pit_sub);
    if (it.book_snapshot_ts_ns <= 0)             return fail(InvalidIntentSubReason::BOOK_TS_ZERO);
    constexpr std::int64_t STALE_60S_NS = 60'000'000'000LL;
    if (now - it.book_snapshot_ts_ns > STALE_60S_NS) return fail(InvalidIntentSubReason::BOOK_TS_STALE);
    if (!is_finite(it.price) || !is_finite(it.book_depth_l1_usdc) || !is_finite(it.tick_size))
        return fail(InvalidIntentSubReason::NAN_OR_INF);
    if (it.size_usdc <= 0 || it.price <= 0.0 || it.price >= 1.0 ||
        it.book_depth_l1_usdc <= 0.0 || it.tick_size <= 0.0)
        return fail(InvalidIntentSubReason::NEGATIVE);
    // tick 仅 {0.001, 0.01}
    constexpr double TICK_TOL = 1e-9;
    double const a1 = std::fabs(it.tick_size - 0.001);
    double const a2 = std::fabs(it.tick_size - 0.01);
    if (a1 > TICK_TOL && a2 > TICK_TOL) return fail(InvalidIntentSubReason::ILLEGAL_TICK);
    if (it.feature_snapshot_id.empty() || it.market_id.empty() || it.signal_id.empty())
        return fail(InvalidIntentSubReason::TS_UNKNOWN_SRC);
    return false;
}

bool RiskGateway::check_duplicate_(OrderIntent const& it, RiskDecision& d) noexcept {
    std::lock_guard<std::mutex> g(s_->mu);
    auto const [it_, inserted] = s_->seen_signal_ids.insert(it.signal_id);
    (void)it_;
    if (!inserted) {
        d.reject = RejectCode::DUPLICATE_INTENT;
        return true;
    }
    return false;
}

bool RiskGateway::check_stale_data_(OrderIntent const& it, RiskDecision& d) const noexcept {
    std::lock_guard<std::mutex> g(s_->mu);

    // recon 全局 freshness (10s WARNING / 30s HALT, 不分 MarketState)
    auto const recon_ms = recon_freshness_ms_.load(std::memory_order_acquire);
    if (recon_ms > 30'000) {
        d.reject = RejectCode::STALE_DATA;
        return true;
    }

    // market freshness vs MarketState 阈值
    auto fs_it = s_->market_freshness_ms.find(it.market_id);
    if (fs_it == s_->market_freshness_ms.end()) {
        return false;  // 无数据 = 不判 stale (其他 rule 拦)
    }
    auto st_it = s_->market_state.find(it.market_id);
    MarketState const st = (st_it == s_->market_state.end()) ? MarketState::INPLAY_HOT : st_it->second;
    auto const th = threshold_of(st);
    if (fs_it->second > th.halt_ms) {
        d.reject = RejectCode::STALE_DATA;
        return true;
    }
    // WARNING: DEFERRED (此 v0.1 暂返 REJECTED + STALE_DATA, W5 真接 Decision::DEFERRED)
    // 这里保守先不开 DEFERRED 分支, 待小肖 Kelly 0.5x 落地后开 (v0.3 §14.5)
    return false;
}

bool RiskGateway::check_market_(OrderIntent const& it, RiskDecision& d) const noexcept {
    // MARKET_TYPE_NOT_ENABLED (MVP 简化: 全部归 moneyline, 后续接 market_metadata)
    if (!cfg_.enable_moneyline && !cfg_.enable_totals && !cfg_.enable_spreads) {
        d.reject = RejectCode::MARKET_TYPE_NOT_ENABLED;
        return true;
    }
    // MARKET_NOT_ACTIVE
    std::lock_guard<std::mutex> g(s_->mu);
    auto a_it = s_->market_active.find(it.market_id);
    if (a_it != s_->market_active.end() && !a_it->second) {
        d.reject = RejectCode::MARKET_NOT_ACTIVE;
        return true;
    }
    return false;
}

bool RiskGateway::check_liquidity_(OrderIntent const& it, RiskDecision& d) const noexcept {
    // 调 SlippageModel (小肖 v1)
    numerical::SlippageInput in{
        .order_size_usdc      = static_cast<double>(it.size_usdc),
        .quote_price          = it.price,
        .book_depth_l1_usdc   = it.book_depth_l1_usdc,
        .book_snapshot_ts_ns  = it.book_snapshot_ts_ns,
        .wall_now_ns          = now_realtime_ns(),
        .tick_size            = it.tick_size,
    };
    auto const out = numerical::SlippageModel::compute(in, numerical::SlippageMode::Linear);
    d.slippage_bps       = out.slippage_bps;
    d.expected_fill_rate = out.expected_fill_rate;

    switch (out.reject) {
        case numerical::RejectCode::Ok:
            break;
        case numerical::RejectCode::InvalidIntent:
            // 不应到这里 (check_invalid_intent_ 已过), 但 fail-closed
            d.reject     = RejectCode::INVALID_INTENT;
            d.sub_reason = map_slippage_sub(out.sub_reason);
            return true;
        case numerical::RejectCode::ExceedBookDepth:
            d.reject = RejectCode::EXCEED_BOOK_DEPTH;
            return true;
        case numerical::RejectCode::FillRateBelowFloor:
            d.reject = RejectCode::LOW_FILL_RATE;
            return true;
    }
    // EXCESSIVE_SLIPPAGE 兜底 (即使 fill_rate ok, slippage_bps 也可能爆)
    auto const slip_abs = out.slippage_bps < 0 ? -out.slippage_bps : out.slippage_bps;
    if (slip_abs > cfg_.excessive_slippage_bps) {
        d.reject = RejectCode::EXCESSIVE_SLIPPAGE;
        return true;
    }
    return false;
}

bool RiskGateway::check_position_caps_(OrderIntent const& it, RiskDecision& d) const noexcept {
    // EXCEED_PER_ORDER_CAP
    if (it.size_usdc > cfg_.per_order_cap_usdc) {
        d.reject = RejectCode::EXCEED_PER_ORDER_CAP;
        return true;
    }
    // EXCEED_MARKET_EXPOSURE
    std::lock_guard<std::mutex> g(s_->mu);
    auto e_it = s_->market_exposure_usdc.find(it.market_id);
    std::int64_t const cur = (e_it == s_->market_exposure_usdc.end()) ? 0 : e_it->second;
    if (cur + it.size_usdc > cfg_.market_exposure_cap_usdc) {
        d.reject = RejectCode::EXCEED_MARKET_EXPOSURE;
        return true;
    }
    // INSUFFICIENT_BANKROLL
    auto const br = bankroll_usdc_.load(std::memory_order_acquire);
    if (it.size_usdc > br) {
        d.reject = RejectCode::INSUFFICIENT_BANKROLL;
        return true;
    }
    // DAILY_LOSS_HALT
    auto const pnl = daily_pnl_usdc_.load(std::memory_order_acquire);
    if (pnl < 0 && -pnl >= cfg_.daily_loss_halt_usdc) {
        d.reject = RejectCode::DAILY_LOSS_HALT;
        return true;
    }
    // CONSEC_LOSS_HALT
    auto const cl = consec_loss_.load(std::memory_order_acquire);
    if (cl >= cfg_.consec_loss_halt_count) {
        d.reject = RejectCode::CONSEC_LOSS_HALT;
        return true;
    }
    return false;
}

bool RiskGateway::check_signal_(OrderIntent const& it, RiskDecision& d) const noexcept {
    std::lock_guard<std::mutex> g(s_->mu);
    auto ci_it = s_->signal_edge_ci_lower.find(it.signal_id);
    if (ci_it != s_->signal_edge_ci_lower.end()) {
        // EDGE_CI_NEGATIVE: CI 下界 ≤ floor → 拒
        if (ci_it->second <= cfg_.edge_ci_lower_floor) {
            d.reject = RejectCode::EDGE_CI_NEGATIVE;
            return true;
        }
        // EDGE_NEGATED_BY_SLIPPAGE: edge_lower (bps) < slippage_bps → 净 edge 负
        auto const edge_bps = static_cast<std::int32_t>(ci_it->second * 10'000.0);
        if (edge_bps < d.slippage_bps) {
            d.reject = RejectCode::EDGE_NEGATED_BY_SLIPPAGE;
            return true;
        }
    }
    return false;
}

bool RiskGateway::check_strategy_decayed_(OrderIntent const& it, RiskDecision& d) const noexcept {
    std::lock_guard<std::mutex> g(s_->mu);
    auto r_it = s_->strategy_ev_ratio.find(it.strategy_id);
    if (r_it == s_->strategy_ev_ratio.end()) return false;
    if (r_it->second < cfg_.strategy_decay_min_ev_ratio) {
        d.reject = RejectCode::STRATEGY_DECAYED;
        return true;
    }
    return false;
}

bool RiskGateway::emit_audit_(OrderIntent const& it, RiskDecision& d) noexcept {
    AuditRecord rec{
        .audit_id          = d.audit_id,
        .event_ts_ns       = it.event_ts_ns,
        .data_source_ts_ns = it.data_source_ts_ns,
        .ingestion_ts_ns   = it.ingestion_ts_ns,
        .as_of_ts_ns       = it.as_of_ts_ns,
        .reject            = d.reject,
        .sub_reason        = d.sub_reason,
        .decision          = d.decision,
        .market_id         = it.market_id,
        .signal_id         = it.signal_id,
    };
    if (!emitter_) return true;  // 测试 no-op
    return emitter_->emit(rec);
}

// ---------- evaluate 主入口 --------------------------------------------------

RiskDecision RiskGateway::evaluate(OrderIntent const& intent) noexcept {
    auto const t0 = now_realtime_ns();
    RiskDecision d{};
    d.decision       = Decision::APPROVED;
    d.reject         = RejectCode::INTERNAL_ERROR;
    d.sub_reason     = InvalidIntentSubReason::NONE;
    d.audit_id       = next_audit_id(t0);
    d.decision_ts_ns = t0;

    // 优先级 short-circuit (R-1 invariant: code != INVALID_INTENT → sub_reason = NONE)
    auto reject_here = [&]() noexcept {
        d.decision = Decision::REJECTED;
        if (!emit_audit_(intent, d)) { d.reject = RejectCode::AUDIT_WAL_BACKPRESSURE; }
        if (d.reject != RejectCode::INVALID_INTENT) d.sub_reason = InvalidIntentSubReason::NONE;
    };
    if (check_state_(intent, d))            { reject_here(); return d; }
    if (check_invalid_intent_(intent, d))   { reject_here(); return d; }
    if (check_duplicate_(intent, d))        { reject_here(); return d; }
    if (check_stale_data_(intent, d))       { reject_here(); return d; }
    if (check_market_(intent, d))           { reject_here(); return d; }
    if (check_liquidity_(intent, d))        { reject_here(); return d; }
    if (check_position_caps_(intent, d))    { reject_here(); return d; }
    if (check_signal_(intent, d))           { reject_here(); return d; }
    if (check_strategy_decayed_(intent, d)) { reject_here(); return d; }

    // 全过 APPROVED (R-1: 仍 emit audit 留痕)
    d.decision = Decision::APPROVED;
    d.reject   = RejectCode::INTERNAL_ERROR;  // 占位; APPROVED 时忽略
    if (!emit_audit_(intent, d)) {
        d.decision = Decision::REJECTED;
        d.reject   = RejectCode::AUDIT_WAL_BACKPRESSURE;
    }
    return d;
}

}  // namespace stcpp::risk
