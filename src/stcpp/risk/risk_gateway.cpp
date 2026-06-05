// stcpp/risk/risk_gateway.cpp — RiskGateway v0.6 (老沈 Wave 3 P0)
//   + v0.5 origin: W9 Wave 57 (老沈, 2026-05-28)
//
// Wave 3 实施 (2026-05-29, 老沈):
//   D1: DD -3% 软熔断 / -5% 硬 kill 分层 (GM §9 裁决 #1)
//   D2: check_invalid_intent_ TS_V2_MISSING/STALE/FUTURE 窗口校验 (laohan spec R3.6-R3.8)
//   D3: check_invalid_intent_ INVALID_BYTES32_FORMAT 格式校验 (laohan spec R3.9-R3.10)
//   D4: check_signal_ fee estimate net_edge 校验 (R-fee-2, kSportsTakerFeeRate=0.03)
//   D5: emit_audit_ 透传 timestamp_ms/metadata/builder → AuditRecord v1.4
//
// v0.5 变更:
//   OrderIntent: market_id → condition_id, +token_id, +outcome, is_buy → side
//   check_invalid_intent_: + token_id/condition_id 非空校验 (MISSING_TOKEN_ID / MISSING_CONDITION_ID)
//   check_position_caps_: R6.2a per_condition + R6.2b per_outcome (EXCEED_PER_OUTCOME_CAP)
//   check_stale_data_: + R8.4 book_token_id_mismatch (BOOK_TOKEN_ID_MISMATCH)
//   check_market_: 使用 condition_id 查 market_active / market_state
//   emit_audit_: market_id → condition_id + token_id + outcome + side
//   State_: + condition_exposure / token_exposure / token_book_freshness_ms
//   DRAIN 放行条件: is_close=true AND side=Sell (laohan spec §2.4)
//
// cite:
//   polymarket_ssot_cite: laoli-w8-polymarket-data-structure-ssot-v1.md §3 §4.1 §6
//   goalserve_ssot_cite:  N/A (OrderIntent 不接 Goalserve)
//   handshake_cite:       laoli-laoSun-handshake-v1.md §3 SignedOrder ABI lock
//   adr_ref:              ADR-027 §4 Enforce-1/2/3/4
//
// 10 reject rule short-circuit 顺序 (SSOT = ADR-004):
//   state → invalid_intent → duplicate → stale_data → market →
//   position_caps (R6.2a + R6.2b) → liquidity → signal → strategy_decayed → AUDIT_WAL_BACKPRESSURE

#include "stcpp/risk/risk_gateway.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/risk/rm_debug_snapshot.hpp"

namespace stcpp::risk {

namespace {

[[nodiscard]] std::int64_t now_realtime_ns() noexcept {
    return infra::wal::pit::NowRealtimeNs();
}

// PIT 4 ts violation mapping (老韩 v0.3.1 + 老孙 v5.1)
[[nodiscard]] InvalidIntentSubReason pit_violation_to_sub(OrderIntent const& it,
                                                          std::int64_t now_ns) noexcept {
    if (it.event_ts_ns <= 0) {
        return InvalidIntentSubReason::BOOK_TS_ZERO;
    }
    if (it.data_source_ts_ns < it.event_ts_ns)
        return InvalidIntentSubReason::TS_ORDER_VIOLATED;
    if (it.ingestion_ts_ns < it.data_source_ts_ns)
        return InvalidIntentSubReason::TS_ORDER_VIOLATED;
    if (it.as_of_ts_ns < it.ingestion_ts_ns)
        return InvalidIntentSubReason::TS_ORDER_VIOLATED;
    if (it.as_of_ts_ns > now_ns)
        return InvalidIntentSubReason::TS_FUTURE;
    return InvalidIntentSubReason::NONE;
}

[[nodiscard]] bool is_finite(double x) noexcept {
    return !(x != x) && x > -1e300 && x < 1e300;
}

// SlippageModel sub_reason → RM sub_reason
[[nodiscard]] InvalidIntentSubReason map_slippage_sub(numerical::InvalidIntentSubReason s) noexcept {
    using S = numerical::InvalidIntentSubReason;
    switch (s) {
        case S::BookTsZero:
            return InvalidIntentSubReason::BOOK_TS_ZERO;
        case S::BookTsStale:
            return InvalidIntentSubReason::BOOK_TS_STALE;
        case S::NanOrInf:
            return InvalidIntentSubReason::NAN_OR_INF;
        case S::Negative:
            return InvalidIntentSubReason::NEGATIVE;
        case S::IllegalTick:
            return InvalidIntentSubReason::ILLEGAL_TICK;
        case S::None:
            return InvalidIntentSubReason::NONE;
    }
    return InvalidIntentSubReason::NONE;
}

// v0.5: token_id 格式校验 (uint256 string: 纯数字, 最多 77 位)
// SSOT: laoli-w8-polymarket-data-structure-ssot-v1.md §2.3 §5 T-05
[[nodiscard]] bool is_valid_token_id(std::string const& tid) noexcept {
    if (tid.empty() || tid.size() > 77u)
        return false;
    for (char c : tid) {
        if (!std::isdigit(static_cast<unsigned char>(c)))
            return false;
    }
    return true;
}

// v0.6 Wave 3: bytes32 hex 格式校验 (spec R3.9/R3.10)
// 合法: ^0x[0-9a-f]{64}$ (66 chars, 小写 hex, 0x 前缀)
// cite: laohan-rm-v0.5-integration-spec-v1.md §7.2 R3.9/R3.10
[[nodiscard]] bool is_valid_bytes32_hex(std::string const& s) noexcept {
    if (s.size() != 66u)
        return false;
    if (s[0] != '0' || s[1] != 'x')
        return false;
    for (std::size_t i = 2; i < 66u; ++i) {
        char c = s[i];
        bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!ok)
            return false;
    }
    return true;
}

// v0.6 Wave 3 (R-fee-2): sports taker fee rate
// 硬编码常量, 不入 RiskConfig (防配置注入 — spec §5.3)
// cite: laohan-rm-v0.5-integration-spec-v1.md §2.3.2 + §5.3
inline constexpr double kSportsTakerFeeRate = 0.03;

}  // namespace

// pImpl: 幂等 cache + per-market/condition/token exposure / freshness / state / signal
struct RiskGateway::State_ {
    mutable std::mutex mu;
    std::unordered_set<std::string> seen_signal_ids;
    // v0.4 兼容: market_id (= condition_id) → exposure
    std::unordered_map<std::string, std::int64_t> market_exposure_usdc;
    // v0.5: per-condition + per-token exposure
    std::unordered_map<std::string, std::int64_t> condition_exposure_usdc;
    std::unordered_map<std::string, std::int64_t> token_exposure_usdc;
    // 持仓管理 Stage 2 P0 (R6.2c 相关性集中度 cap): condition→event 映射 + per-event Σ|敞口|。
    //   event_gross 在 set_condition_exposure/set_condition_event 增量维护 (|new|−|old| 累加)。
    std::unordered_map<std::string, std::string> condition_event;     // condition_id → event_id
    std::unordered_map<std::string, std::int64_t> event_gross_usdc;   // event_id → Σ|condition_exposure| (micro)
    std::unordered_map<std::string, std::uint32_t> market_freshness_ms;
    // v0.5: per-token book freshness (R8.4)
    std::unordered_map<std::string, std::uint32_t> token_book_freshness_ms;
    std::unordered_map<std::string, MarketState> market_state;
    std::unordered_map<std::string, bool> market_active;
    std::unordered_map<std::string, double> signal_edge_ci_lower;
    std::unordered_map<std::string, double> strategy_ev_ratio;
};

// ---------- ctor -------------------------------------------------------------

RiskGateway::RiskGateway(RiskConfig cfg, std::shared_ptr<AuditEmitter> emitter) noexcept
    : cfg_(cfg), emitter_(std::move(emitter)), s_(std::make_unique<State_>()) {
    bankroll_usdc_.store(cfg.bankroll_usdc.v);  // c2b: MicroPUSD 字段 → atomic int64 micro 镜像
}

RiskGateway::~RiskGateway() = default;

// ---------- setters ----------------------------------------------------------

// A4: mark_fed_ — 记某红线被喂 (now_realtime_ns 自由函数在本 TU; relaxed store, 无锁,
//   独立于 s_->mu)。各 setter 末尾调; map 类红线任一 key 喂即标活 (last-any-key-fed)。
void RiskGateway::mark_fed_(FeedKey k) noexcept {
    last_fed_ns_[static_cast<std::size_t>(k)].store(now_realtime_ns(), std::memory_order_relaxed);
}

// ---- A4: 去 inline 的 4 个标量红线 setter (移 .cpp 以记 last_fed_ns) ----
void RiskGateway::set_daily_pnl(std::int64_t usdc) noexcept {
    daily_pnl_usdc_.store(usdc);
    mark_fed_(FeedKey::DailyPnl);
}
void RiskGateway::set_consec_loss(std::int32_t n) noexcept {
    consec_loss_.store(n);
    mark_fed_(FeedKey::ConsecLoss);
}
void RiskGateway::set_bankroll(std::int64_t usdc) noexcept {
    bankroll_usdc_.store(usdc);
    mark_fed_(FeedKey::Bankroll);
}
void RiskGateway::set_recon_freshness_ms(std::uint32_t ms) noexcept {
    recon_freshness_ms_.store(ms);
    mark_fed_(FeedKey::ReconFreshness);
}

void RiskGateway::set_market_exposure(std::string const& m, std::int64_t v) noexcept {
    {
        std::lock_guard<std::mutex> g(s_->mu);
        // v0.4 兼容: 同时写 market_exposure_usdc + condition_exposure_usdc
        s_->market_exposure_usdc[m] = v;
        s_->condition_exposure_usdc[m] = v;
    }
    mark_fed_(FeedKey::Exposure);
}

void RiskGateway::set_condition_exposure(std::string const& cid, std::int64_t v) noexcept {
    {
        std::lock_guard<std::mutex> g(s_->mu);
        // R6.2c: 维护 per-event Σ|敞口| (event_gross += |new| − |old|), 仅当该 condition 已注册 event。
        auto ce = s_->condition_exposure_usdc.find(cid);
        std::int64_t const old = (ce != s_->condition_exposure_usdc.end()) ? ce->second : 0;
        s_->condition_exposure_usdc[cid] = v;
        s_->market_exposure_usdc[cid] = v;  // keep compat map in sync
        auto ev = s_->condition_event.find(cid);
        if (ev != s_->condition_event.end()) {
            std::int64_t const abs_v = v < 0 ? -v : v;
            std::int64_t const abs_old = old < 0 ? -old : old;
            s_->event_gross_usdc[ev->second] += (abs_v - abs_old);
        }
    }
    mark_fed_(FeedKey::Exposure);
}

// R6.2c: 注册 condition→event 映射 (持仓管理 Stage 2 P0)。幂等。
//   若该 condition 已有敞口, 把其当前 |敞口| 计入(或迁移到)对应 event_gross。
void RiskGateway::set_condition_event(std::string const& cid, std::string const& event_id) noexcept {
    if (event_id.empty()) return;
    std::lock_guard<std::mutex> g(s_->mu);
    auto it = s_->condition_event.find(cid);
    if (it != s_->condition_event.end() && it->second == event_id) return;  // 幂等: 未变
    // 当前该 condition 的 |敞口| (注册前 set_condition_exposure 未计入新 event)
    std::int64_t cur_abs = 0;
    auto ce = s_->condition_exposure_usdc.find(cid);
    if (ce != s_->condition_exposure_usdc.end()) cur_abs = ce->second < 0 ? -ce->second : ce->second;
    if (it != s_->condition_event.end()) {
        s_->event_gross_usdc[it->second] -= cur_abs;  // 从旧 event 扣除 (condition 改挂, 罕见)
    }
    s_->condition_event[cid] = event_id;
    s_->event_gross_usdc[event_id] += cur_abs;  // 计入新 event
}

void RiskGateway::set_outcome_exposure(std::string const& token_id, std::int64_t v) noexcept {
    {
        std::lock_guard<std::mutex> g(s_->mu);
        s_->token_exposure_usdc[token_id] = v;
    }
    mark_fed_(FeedKey::Exposure);
}

void RiskGateway::set_edge_ci_lower(std::string const& sig, double v) noexcept {
    {
        std::lock_guard<std::mutex> g(s_->mu);
        s_->signal_edge_ci_lower[sig] = v;
    }
    mark_fed_(FeedKey::EdgeCi);
}
void RiskGateway::set_strategy_ev_ratio(std::string const& sid, double r) noexcept {
    {
        std::lock_guard<std::mutex> g(s_->mu);
        s_->strategy_ev_ratio[sid] = r;
    }
    mark_fed_(FeedKey::StrategyEv);
}
void RiskGateway::set_market_freshness_ms(std::string const& m, std::uint32_t ms) noexcept {
    {
        std::lock_guard<std::mutex> g(s_->mu);
        s_->market_freshness_ms[m] = ms;
    }
    mark_fed_(FeedKey::Freshness);
}
void RiskGateway::set_token_book_freshness_ms(std::string const& token_id, std::uint32_t ms) noexcept {
    {
        std::lock_guard<std::mutex> g(s_->mu);
        s_->token_book_freshness_ms[token_id] = ms;
    }
    mark_fed_(FeedKey::Freshness);
}
void RiskGateway::set_market_state(std::string const& m, MarketState st) noexcept {
    {
        std::lock_guard<std::mutex> g(s_->mu);
        s_->market_state[m] = st;
    }
    mark_fed_(FeedKey::MarketState);
}
void RiskGateway::set_market_active(std::string const& m, bool active) noexcept {
    {
        std::lock_guard<std::mutex> g(s_->mu);
        s_->market_active[m] = active;
    }
    mark_fed_(FeedKey::MarketActive);
}
void RiskGateway::clear_idempotency() noexcept {
    std::lock_guard<std::mutex> g(s_->mu);
    s_->seen_signal_ids.clear();
}

// A4: feed-liveness 诊断快照 (非热路径; daemon 启动自检 + 周期巡检调用)。
std::vector<RiskGateway::FeedLivenessRow> RiskGateway::feed_liveness_report() const noexcept {
    static constexpr std::array<std::string_view, static_cast<std::size_t>(FeedKey::COUNT)> kNames = {
        "bankroll",  "daily_pnl", "consec_loss", "recon_freshness", "exposure",
        "freshness", "edge_ci",   "strategy_ev", "market_state",    "market_active",
    };
    std::vector<FeedLivenessRow> rows;
    rows.reserve(static_cast<std::size_t>(FeedKey::COUNT));
    for (std::size_t i = 0; i < static_cast<std::size_t>(FeedKey::COUNT); ++i) {
        auto const ts = last_fed_ns_[i].load(std::memory_order_relaxed);
        rows.push_back(FeedLivenessRow{kNames[i], ts, ts != 0});
    }
    return rows;
}

// 老沈 rm_debug_snapshot D1: 进程级全局 snapshot 指针 (单例)
// 线程安全: atomic store (release in attach/detach) / acquire (in evaluate)
// 生命周期: snap 必须比所有 RiskGateway 实例活得更长
// 调用约定: start-up 阶段 attach (单次); 销毁前 detach
namespace {
std::atomic<RmDebugSnapshot*> g_rm_debug_snapshot{nullptr};
}  // namespace

void attach_rm_debug_snapshot(RmDebugSnapshot* snap) noexcept {
    g_rm_debug_snapshot.store(snap, std::memory_order_release);
}

void detach_rm_debug_snapshot() noexcept {
    g_rm_debug_snapshot.store(nullptr, std::memory_order_release);
}

const RmDebugSnapshot* current_rm_debug_snapshot() noexcept {
    return g_rm_debug_snapshot.load(std::memory_order_acquire);
}

// ---------- audit_id 生成 (ULID stub) ----------------------------------------
//
// BUG-W5-001 patch: seq 段 shift 修正 (原 shift=72 是 uint64 UB)
// 见 老沈 W5 Wave 24 注释.

std::array<std::uint8_t, 16> RiskGateway::next_audit_id(std::int64_t now_ns) noexcept {
    static std::atomic<std::uint64_t> g_seq{0};
    std::array<std::uint8_t, 16> out{};
    auto const ts_ms = static_cast<std::uint64_t>(now_ns / 1'000'000LL);
    for (int i = 0; i < 6; ++i) {
        out[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((ts_ms >> ((5 - i) * 8)) & 0xFFu);
    }
    auto const seq = g_seq.fetch_add(1, std::memory_order_relaxed);
    for (int i = 0; i < 6; ++i) {
        out[6 + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((seq >> ((5 - i) * 8)) & 0xFFu);
    }
    for (int i = 12; i < 16; ++i) {
        out[static_cast<std::size_t>(i)] = 0;
    }
    return out;
}

// ---------- 10 rule helpers --------------------------------------------------

// 1. state
bool RiskGateway::check_state_(OrderIntent const& it, RiskDecision& d) const noexcept {
    auto const st = state_.load(std::memory_order_acquire);
    // v0.5 DRAIN 放行条件: is_close=true AND side=Sell (laohan spec §2.4)
    switch (st) {
        case RmState::HALTED:
            d.reject = RejectCode::STATE_HALTED;
            return true;
        case RmState::DRAIN:
            // DRAIN: 仅 is_close=true + side=Sell 放行
            if (!it.is_close || it.side != Side::Sell) {
                d.reject = RejectCode::STATE_DRAIN;
                return true;
            }
            return false;
        case RmState::SAFE_MODE:
            if (!it.is_close) {
                d.reject = RejectCode::STATE_SAFE_MODE;
                return true;
            }
            return false;
        case RmState::RUNNING:
        case RmState::WARNING:
            return false;
    }
    return false;
}

// 2. invalid_intent
bool RiskGateway::check_invalid_intent_(OrderIntent const& it, RiskDecision& d) const noexcept {
    auto const now = now_realtime_ns();
    auto fail = [&](InvalidIntentSubReason s) noexcept -> bool {
        d.reject = RejectCode::INVALID_INTENT;
        d.sub_reason = s;
        return true;
    };

    // (a) R-20 4 ts PIT chain
    auto const pit_sub = pit_violation_to_sub(it, now);
    if (pit_sub != InvalidIntentSubReason::NONE)
        return fail(pit_sub);

    // (b) book_snapshot_ts
    if (it.book_snapshot_ts_ns <= 0)
        return fail(InvalidIntentSubReason::BOOK_TS_ZERO);
    constexpr std::int64_t STALE_60S_NS = 60'000'000'000LL;
    if (now - it.book_snapshot_ts_ns > STALE_60S_NS)
        return fail(InvalidIntentSubReason::BOOK_TS_STALE);

    // (c) NaN / Inf
    if (!is_finite(it.price) || !is_finite(it.book_depth_l1_usdc) || !is_finite(it.tick_size))
        return fail(InvalidIntentSubReason::NAN_OR_INF);

    // (d) 负值 / 零
    if (it.size_pUSD_micro <= 0 || it.price <= 0.0 || it.price >= 1.0 || it.book_depth_l1_usdc <= 0.0 ||
        it.tick_size <= 0.0)
        return fail(InvalidIntentSubReason::NEGATIVE);

    // (e) tick: {0.001, 0.01}
    constexpr double TICK_TOL = 1e-9;
    if (std::fabs(it.tick_size - 0.001) > TICK_TOL && std::fabs(it.tick_size - 0.01) > TICK_TOL)
        return fail(InvalidIntentSubReason::ILLEGAL_TICK);

    // (f) 必填字段
    if (it.feature_snapshot_id.empty() || it.signal_id.empty())
        return fail(InvalidIntentSubReason::TS_UNKNOWN_SRC);

    // (g) v0.5: condition_id 非空
    if (it.condition_id.empty())
        return fail(InvalidIntentSubReason::MISSING_CONDITION_ID);

    // (h) v0.5: token_id 非空 + 格式 (uint256 纯数字, ≤77 位)
    if (it.token_id.empty())
        return fail(InvalidIntentSubReason::MISSING_TOKEN_ID);
    if (!is_valid_token_id(it.token_id))
        return fail(InvalidIntentSubReason::INVALID_TOKEN_ID_FORMAT);

    // (i) v0.6 Wave 3: timestamp_ms V2 校验 (laohan spec R3.6/R3.7/R3.8)
    // RM 内部禁止 now() 替代 timestamp_ms — 仅用于 PIT 校验基准 (spec §5.2)
    {
        auto const now_ms = now / 1'000'000LL;  // ns → ms
        // R3.6: timestamp_ms == 0 → TS_V2_MISSING
        if (it.timestamp_ms == 0)
            return fail(InvalidIntentSubReason::TS_V2_MISSING);
        // R3.7: timestamp_ms < now_ms - 60_000 → TS_V2_STALE
        if (it.timestamp_ms < now_ms - 60'000LL)
            return fail(InvalidIntentSubReason::TS_V2_STALE);
        // R3.8: timestamp_ms > now_ms + 5_000 → TS_V2_FUTURE
        if (it.timestamp_ms > now_ms + 5'000LL)
            return fail(InvalidIntentSubReason::TS_V2_FUTURE);
    }

    // (j) v0.6 Wave 3: metadata bytes32 格式校验 (laohan spec R3.9)
    if (!is_valid_bytes32_hex(it.metadata))
        return fail(InvalidIntentSubReason::INVALID_BYTES32_FORMAT);

    // (k) v0.6 Wave 3: builder bytes32 格式校验 (laohan spec R3.10)
    if (!is_valid_bytes32_hex(it.builder))
        return fail(InvalidIntentSubReason::INVALID_BYTES32_FORMAT);

    return false;
}

// 3. duplicate
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

// 4. stale_data (含 R8.4 book_token_id_mismatch)
bool RiskGateway::check_stale_data_(OrderIntent const& it, RiskDecision& d) const noexcept {
    std::lock_guard<std::mutex> g(s_->mu);

    // recon 全局 freshness (30s HALT)
    auto const recon_ms = recon_freshness_ms_.load(std::memory_order_acquire);
    if (recon_ms > 30'000) {
        d.reject = RejectCode::STALE_DATA;
        return true;
    }

    // market freshness vs MarketState 阈值 (按 condition_id 查)
    // v0.5: 优先查 condition_id, 兼容 v0.4 market_id
    std::string const& key = it.condition_id.empty() ? it.condition_id : it.condition_id;
    auto fs_it = s_->market_freshness_ms.find(key);
    if (fs_it != s_->market_freshness_ms.end()) {
        auto st_it = s_->market_state.find(key);
        MarketState const st = (st_it == s_->market_state.end()) ? MarketState::INPLAY_HOT : st_it->second;
        auto const th = threshold_of(st);
        if (fs_it->second > th.halt_ms) {
            d.reject = RejectCode::STALE_DATA;
            return true;
        }
    }

    // R8.4 (v0.5): book_snapshot_ts ↔ token_id 一致性
    // 若 token_book_freshness_ms 已设置, 校验 token_id 对应 book 是否 stale
    // SSOT: laoli-w8-polymarket-data-structure-ssot-v1.md §4.1
    if (!it.token_id.empty()) {
        auto tbf_it = s_->token_book_freshness_ms.find(it.token_id);
        if (tbf_it != s_->token_book_freshness_ms.end()) {
            // 若 token book freshness 超 stale threshold (和 market 同档位), 拒
            auto st_it = s_->market_state.find(it.condition_id);
            MarketState const st =
                (st_it == s_->market_state.end()) ? MarketState::INPLAY_HOT : st_it->second;
            auto const th = threshold_of(st);
            if (tbf_it->second > th.halt_ms) {
                d.reject = RejectCode::STALE_DATA;
                d.sub_reason = InvalidIntentSubReason::BOOK_TOKEN_ID_MISMATCH;
                return true;
            }
        }
    }

    return false;
}

// 5. market
bool RiskGateway::check_market_(OrderIntent const& it, RiskDecision& d) const noexcept {
    // 盘口类型 enable gate 已删 (2026-05-31 老板「没意义的 gate 都删了」): 原 enable_xxx 是空壳
    //   (不按 order 的 market_type 逐单卡)。盘口准入移到定价层 (paper_loop: 非 moneyline 无专属定价 →
    //   fail-closed)。MARKET_TYPE_NOT_ENABLED 拒单码保留 (RJ- 码契约稳定), 仅不再产生。
    // 按 condition_id 查 market_active (市场关闭/结算 → 不下单; 真有意义的市场状态 gate)。
    std::lock_guard<std::mutex> g(s_->mu);
    std::string const& key = it.condition_id.empty() ? it.condition_id : it.condition_id;
    auto a_it = s_->market_active.find(key);
    if (a_it != s_->market_active.end() && !a_it->second) {
        d.reject = RejectCode::MARKET_NOT_ACTIVE;
        return true;
    }
    return false;
}

// 6. position_caps (ADR-004 前移)
bool RiskGateway::check_position_caps_(OrderIntent const& it, RiskDecision& d) const noexcept {
    // EXCEED_PER_ORDER_CAP (c2: cap 为 MicroPUSD; size 包 from_micro 同型比, 字节级零变)
    if (domain::MicroPUSD::from_micro(it.size_pUSD_micro) > cfg_.per_order_cap_usdc) {
        d.reject = RejectCode::EXCEED_PER_ORDER_CAP;
        return true;
    }

    std::lock_guard<std::mutex> g(s_->mu);

    // R6.2a: per_condition cap (v0.5, uses condition_id)
    // Falls back to market_exposure_usdc for v0.4 compat
    {
        std::int64_t cur = 0;
        auto ce_it = s_->condition_exposure_usdc.find(it.condition_id);
        if (ce_it != s_->condition_exposure_usdc.end()) {
            cur = ce_it->second;
        } else {
            // v0.4 compat fallback
            auto me_it = s_->market_exposure_usdc.find(it.condition_id);
            if (me_it != s_->market_exposure_usdc.end())
                cur = me_it->second;
        }
        // 老韩 H-1/H-2 (目标仓位范式): cap 按敞口**绝对值**比较, signed delta 由 side 定符号。
        //   旧码无视 side 恒 `cur + size` → 一旦卖出(应 −size)caps 被静默架空 (§8.1 #3 同型, 符号维度)。
        //   减仓(降 |敞口|)放行; 仅升敞口才比 cap; 反向穿零(多↔空跨0)M1 拒(controller clamp 兜底)。
        const std::int64_t delta = (it.side == Side::Sell) ? -it.size_pUSD_micro : it.size_pUSD_micro;
        const std::int64_t new_exp = cur + delta;
        if (cur != 0 && new_exp != 0 && ((cur > 0) != (new_exp > 0))) {
            d.reject = RejectCode::EXCEED_CONDITION_EXPOSURE;  // H-2 复用 caps 族 (老韩允许; M2 专用码)
            return true;
        }
        const std::int64_t abs_new = new_exp < 0 ? -new_exp : new_exp;
        const std::int64_t abs_cur = cur < 0 ? -cur : cur;
        if (abs_new > abs_cur && domain::MicroPUSD::from_micro(abs_new) > cfg_.market_exposure_cap_usdc) {
            d.reject = RejectCode::EXCEED_CONDITION_EXPOSURE;
            return true;
        }
    }

    // R6.2c: per-event 相关性集中度 cap (持仓管理 Stage 2 P0)。
    //   同赛事多盘合并 Σ|condition_exposure| (保守 ρ=1 上界, 不 netting) ≤ event_exposure_cap。
    //   仅 event 已注册 + 本单升本 condition |敞口| 时比 cap (减仓/未注册 event 放行, 纯加性安全)。
    if (cfg_.event_exposure_cap_usdc.v > 0) {
        auto ev_it = s_->condition_event.find(it.condition_id);
        if (ev_it != s_->condition_event.end()) {
            std::int64_t cur_cond = 0;
            auto cc_it = s_->condition_exposure_usdc.find(it.condition_id);
            if (cc_it != s_->condition_exposure_usdc.end()) cur_cond = cc_it->second;
            const std::int64_t delta_ev =
                (it.side == Side::Sell) ? -it.size_pUSD_micro : it.size_pUSD_micro;
            const std::int64_t new_cond = cur_cond + delta_ev;
            const std::int64_t abs_new_cond = new_cond < 0 ? -new_cond : new_cond;
            const std::int64_t abs_cur_cond = cur_cond < 0 ? -cur_cond : cur_cond;
            if (abs_new_cond > abs_cur_cond) {  // 仅升敞口才比 (减仓放行)
                std::int64_t ev_gross = 0;
                auto eg_it = s_->event_gross_usdc.find(ev_it->second);
                if (eg_it != s_->event_gross_usdc.end()) ev_gross = eg_it->second;
                // 替换本 condition 的贡献: event_gross − |cur_cond| + |new_cond|
                const std::int64_t ev_gross_after = ev_gross - abs_cur_cond + abs_new_cond;
                if (domain::MicroPUSD::from_micro(ev_gross_after) > cfg_.event_exposure_cap_usdc) {
                    d.reject = RejectCode::EXCEED_EVENT_EXPOSURE;
                    return true;
                }
            }
        }
    }

    // R6.2b: per_outcome cap (v0.5, uses token_id)
    // Only check if token_id is set and per_outcome_cap_usdc > 0
    if (!it.token_id.empty() && cfg_.per_outcome_cap_usdc.v > 0) {
        std::int64_t cur_tok = 0;
        auto te_it = s_->token_exposure_usdc.find(it.token_id);
        if (te_it != s_->token_exposure_usdc.end())
            cur_tok = te_it->second;
        // 老韩 H-1/H-2 (同 per_condition): signed delta + magnitude cap + 反向穿零拒。
        const std::int64_t delta_tok = (it.side == Side::Sell) ? -it.size_pUSD_micro : it.size_pUSD_micro;
        const std::int64_t new_tok = cur_tok + delta_tok;
        if (cur_tok != 0 && new_tok != 0 && ((cur_tok > 0) != (new_tok > 0))) {
            d.reject = RejectCode::EXCEED_PER_OUTCOME_CAP;  // H-2 复用 (M2 专用码)
            return true;
        }
        const std::int64_t abs_new_tok = new_tok < 0 ? -new_tok : new_tok;
        const std::int64_t abs_cur_tok = cur_tok < 0 ? -cur_tok : cur_tok;
        if (abs_new_tok > abs_cur_tok &&
            domain::MicroPUSD::from_micro(abs_new_tok) > cfg_.per_outcome_cap_usdc) {
            d.reject = RejectCode::EXCEED_PER_OUTCOME_CAP;
            return true;
        }
    }

    // INSUFFICIENT_BANKROLL
    auto const br = bankroll_usdc_.load(std::memory_order_acquire);
    if (it.size_pUSD_micro > br) {
        d.reject = RejectCode::INSUFFICIENT_BANKROLL;
        return true;
    }

    // DD 软 / 硬熔断 (GM §9 裁决 #1: -3% 软 / -5% 硬)
    // cite: laohan-rm-v0.5-integration-spec-v1.md §2.3.3 + laoshen-rm-v0.5-field-freeze-spec-v1.md §2.2
    // 硬 kill 的 set_state(HALTED) 在 evaluate() 主路径中执行 (check_position_caps_ 是 const)
    // 此处返回 DAILY_LOSS_HALT; evaluate() 检测后触发状态迁移
    auto const pnl = daily_pnl_usdc_.load(std::memory_order_acquire);
    if (pnl < 0) {
        auto const loss = -pnl;  // loss > 0

        // 硬阈值: daily_loss_halt_usdc 旧字段绝对值 (>0 时覆盖 hard_pct)
        // 若旧字段为 0, 则用 hard_pct × bankroll
        std::int64_t hard_threshold = 0;
        if (cfg_.daily_loss_halt_usdc.v > 0) {  // c2b: MicroPUSD; .v 取 micro 阈值 (整数比零变)
            hard_threshold = cfg_.daily_loss_halt_usdc.v;
        } else {
            hard_threshold = static_cast<std::int64_t>(static_cast<double>(br) * cfg_.daily_loss_hard_pct);
        }

        // 软阈值: daily_loss_soft_pct × bankroll
        std::int64_t const soft_threshold =
            static_cast<std::int64_t>(static_cast<double>(br) * cfg_.daily_loss_soft_pct);

        if (loss >= hard_threshold) {
            // -5% 硬 kill: 返回 DAILY_LOSS_HALT; evaluate() 主路径负责 set_state(HALTED)
            d.reject = RejectCode::DAILY_LOSS_HALT;
            return true;
        }
        if (loss >= soft_threshold) {
            // -3% 软熔断: 拒新开仓 (is_close=false), 放行平仓 (is_close=true)
            if (!it.is_close) {
                d.reject = RejectCode::DAILY_LOSS_HALT;
                return true;
            }
            // is_close=true → 放行 (继续后续检查)
        }
    }

    // CONSEC_LOSS_HALT
    auto const cl = consec_loss_.load(std::memory_order_acquire);
    if (cl >= cfg_.consec_loss_halt_count) {
        d.reject = RejectCode::CONSEC_LOSS_HALT;
        return true;
    }
    return false;
}

// 7. liquidity
bool RiskGateway::check_liquidity_(OrderIntent const& it, RiskDecision& d) const noexcept {
    numerical::SlippageInput in{
        // P1-9 (老韩 spec §1.2): size_pUSD_micro 是 micro(1e-6 pUSD); SlippageModel 的
        //   order_size_usdc 与 book_depth_l1_usdc 同为 whole pUSD 口径 (ρ=order/depth 需同量纲)。
        //   v0.6 size_usdc→size_pUSD_micro rename 漏 audit 的消费点 (cap 比较点 c2/c3 已修, 此处漏)。
        //   走 MicroPUSD::from_micro(...).to_pusd() = micro→whole 唯一合法转换通道 (A1 既有 API)。
        //   裸 (double)size_pUSD_micro 当 whole 喂 → ρ 放大 1e6 → 真实单全量误拒 EXCEED_BOOK_DEPTH。
        //   cite: docs/RESEARCH/laohan-a4-p19-rm-feedliveness-slippage-unit-spec-v1.md §1
        .order_size_usdc = domain::MicroPUSD::from_micro(it.size_pUSD_micro).to_pusd(),
        .quote_price = it.price,
        .book_depth_l1_usdc = it.book_depth_l1_usdc,
        .book_snapshot_ts_ns = it.book_snapshot_ts_ns,
        .wall_now_ns = now_realtime_ns(),
        .tick_size = it.tick_size,
    };
    auto const out = numerical::SlippageModel::compute(in, numerical::SlippageMode::Linear);
    d.slippage_bps = out.slippage_bps;
    d.expected_fill_rate = out.expected_fill_rate;

    switch (out.reject) {
        case numerical::RejectCode::Ok:
            break;
        case numerical::RejectCode::InvalidIntent:
            d.reject = RejectCode::INVALID_INTENT;
            d.sub_reason = map_slippage_sub(out.sub_reason);
            return true;
        case numerical::RejectCode::ExceedBookDepth:
            d.reject = RejectCode::EXCEED_BOOK_DEPTH;
            return true;
        case numerical::RejectCode::FillRateBelowFloor:
            d.reject = RejectCode::LOW_FILL_RATE;
            return true;
    }
    auto const slip_abs = out.slippage_bps < 0 ? -out.slippage_bps : out.slippage_bps;
    if (slip_abs > cfg_.excessive_slippage_bps) {
        d.reject = RejectCode::EXCESSIVE_SLIPPAGE;
        return true;
    }
    return false;
}

// 8. signal (含 fee estimate R-fee-2)
// cite: laohan-rm-v0.5-integration-spec-v1.md §2.3.2 KELLY_OVERSIZE
// fee estimate: 局部变量, 不写 OrderIntent / SignedOrder / EIP-712 (spec §5.3)
bool RiskGateway::check_signal_(OrderIntent const& it, RiskDecision& d) const noexcept {
    std::lock_guard<std::mutex> g(s_->mu);
    auto ci_it = s_->signal_edge_ci_lower.find(it.signal_id);
    if (ci_it != s_->signal_edge_ci_lower.end()) {
        double const edge_ci_lower = ci_it->second;

        if (edge_ci_lower <= cfg_.edge_ci_lower_floor) {
            d.reject = RejectCode::EDGE_CI_NEGATIVE;
            return true;
        }

        // Slippage check (existing)
        auto const edge_bps = static_cast<std::int32_t>(edge_ci_lower * 10'000.0);
        if (edge_bps < d.slippage_bps) {
            d.reject = RejectCode::EDGE_NEGATED_BY_SLIPPAGE;
            return true;
        }

        // R-fee-2: fee estimate net_edge 校验 (Wave 3 新增)
        // sports_taker_fee = size × kSportsTakerFeeRate × p × (1-p)
        // net_edge_after_fee = edge_ci_lower - fee / size
        //   = edge_ci_lower - kSportsTakerFeeRate × p × (1-p)
        // 注: fee 是局部变量, 不写出 intent/signedorder/eip712 (spec §5.3 安全隔离)
        // fee 系数 per-market (gamma feeSchedule.rate, 经 OrderIntent.fee_rate_coef 传入).
        //   官方禁硬编码 (docs.polymarket 2026-03-31): 体育 0.03 / 加密 0.072 / 老市场 0(免费) 各异.
        //   仍钳 [0,0.10] 防脏数据 (来源是 Polymarket 自家 gamma, 非不可信 RiskConfig; 钳是纵深防御).
        //   未填 (=0.03 默认) 时行为与旧 kSportsTakerFeeRate 逐位不变.
        {
            double const p = it.price;
            // 非有限 → 回退 kSportsTakerFeeRate (canonical 默认); 否则钳 [0,0.10] 防脏数据.
            double const raw_coef = std::isfinite(it.fee_rate_coef) ? it.fee_rate_coef : kSportsTakerFeeRate;
            double const fee_coef = std::clamp(raw_coef, 0.0, 0.10);
            double const fee_per_unit = fee_coef * p * (1.0 - p);
            double const net_edge_after_fee = edge_ci_lower - fee_per_unit;
            if (net_edge_after_fee <= cfg_.edge_ci_lower_floor) {
                d.reject = RejectCode::EDGE_NEGATED_BY_SLIPPAGE;  // 复用 (spec §2.3.2)
                return true;
            }
        }
    }
    return false;
}

// 9. strategy_decayed
bool RiskGateway::check_strategy_decayed_(OrderIntent const& it, RiskDecision& d) const noexcept {
    std::lock_guard<std::mutex> g(s_->mu);
    auto r_it = s_->strategy_ev_ratio.find(it.strategy_id);
    if (r_it == s_->strategy_ev_ratio.end())
        return false;
    if (r_it->second < cfg_.strategy_decay_min_ev_ratio) {
        d.reject = RejectCode::STRATEGY_DECAYED;
        return true;
    }
    return false;
}

// emit_audit_ (v0.6 Wave 3: + timestamp_ms + metadata + builder 透传)
// cite: laoshen-rm-v0.5-field-freeze-spec-v1.md §2.3.6 必填字段 + 可追溯红线
// APPROVED 和 REJECTED 均必须记录 timestamp_ms/metadata/builder (spec §2.3.6 末注)
bool RiskGateway::emit_audit_(OrderIntent const& it, RiskDecision& d) noexcept {
    AuditRecord rec{};
    rec.audit_id = d.audit_id;
    rec.event_ts_ns = it.event_ts_ns;
    rec.data_source_ts_ns = it.data_source_ts_ns;
    rec.ingestion_ts_ns = it.ingestion_ts_ns;
    rec.as_of_ts_ns = it.as_of_ts_ns;
    rec.reject = d.reject;
    rec.sub_reason = d.sub_reason;
    rec.decision = d.decision;
    rec.condition_id = it.condition_id;                   // v0.5: renamed from market_id
    rec.token_id = it.token_id;                           // v0.5: new
    rec.outcome = static_cast<std::uint8_t>(it.outcome);  // v0.5: new
    rec.side_val = static_cast<std::uint8_t>(it.side);    // v0.5: new
    rec.signal_id = it.signal_id;
    // v0.6 Wave 3: V2 CLOB 字段透传 (AuditRecord v1.4 §2.3.6 必填)
    rec.timestamp_ms = it.timestamp_ms;  // V2 EIP-712 Order.timestamp
    rec.metadata = it.metadata;          // bytes32 hex (V2 Order.metadata)
    rec.builder = it.builder;            // bytes32 hex (V2 Order.builder, optional)
    if (!emitter_)
        return true;
    return emitter_->emit(rec);
}

// ---------- evaluate 主入口 --------------------------------------------------

RiskDecision RiskGateway::evaluate(OrderIntent const& intent) noexcept {
    auto const t0 = now_realtime_ns();
    RiskDecision d{};
    d.decision = Decision::APPROVED;
    d.reject = RejectCode::INTERNAL_ERROR;
    d.sub_reason = InvalidIntentSubReason::NONE;
    d.audit_id = next_audit_id(t0);
    d.decision_ts_ns = t0;

    auto reject_here = [&]() noexcept {
        d.decision = Decision::REJECTED;
        if (!emit_audit_(intent, d)) {
            d.reject = RejectCode::AUDIT_WAL_BACKPRESSURE;
        }
        if (d.reject != RejectCode::INVALID_INTENT)
            d.sub_reason = InvalidIntentSubReason::NONE;

        // 老沈 rm_debug_snapshot D1: 一次 ring 写, 不持锁, 不分配, fail-open
        // 红线: 仅在 REJECTED 确定后调用; 不反压热路径; p99 增量 < 1us
        // 安全: build_reject_row 只读 allowlist 字段 (token_id/sig/nonce 不投影)
        // 全局 singleton 指针: 热路径 acquire-load; attach 在 start-up release-store
        auto* snap = g_rm_debug_snapshot.load(std::memory_order_acquire);
        if (snap != nullptr) {
            RejectRow row = build_reject_row(d, intent);
            snap->push_reject(row);
        }
    };

    if (check_state_(intent, d)) {
        reject_here();
        return d;
    }
    if (check_invalid_intent_(intent, d)) {
        reject_here();
        return d;
    }
    if (check_duplicate_(intent, d)) {
        reject_here();
        return d;
    }
    if (check_stale_data_(intent, d)) {
        reject_here();
        return d;
    }
    if (check_market_(intent, d)) {
        reject_here();
        return d;
    }
    // ADR-004: position_caps 前移 (红线先于市场客观状态)
    if (check_position_caps_(intent, d)) {
        // DD 硬 kill 升级: DAILY_LOSS_HALT + 损失超 hard_threshold → HALTED
        // check_position_caps_ 是 const, 状态迁移在此处执行
        if (d.reject == RejectCode::DAILY_LOSS_HALT) {
            auto const br = bankroll_usdc_.load(std::memory_order_acquire);
            auto const pnl = daily_pnl_usdc_.load(std::memory_order_acquire);
            if (pnl < 0) {
                std::int64_t hard_threshold =
                    (cfg_.daily_loss_halt_usdc.v > 0)  // c2b: 与 check_position_caps_ DD 块同步取 .v
                        ? cfg_.daily_loss_halt_usdc.v
                        : static_cast<std::int64_t>(static_cast<double>(br) * cfg_.daily_loss_hard_pct);
                if (-pnl >= hard_threshold) {
                    state_.store(RmState::HALTED, std::memory_order_release);
                }
            }
        }
        reject_here();
        return d;
    }
    if (check_liquidity_(intent, d)) {
        reject_here();
        return d;
    }
    if (check_signal_(intent, d)) {
        reject_here();
        return d;
    }
    if (check_strategy_decayed_(intent, d)) {
        reject_here();
        return d;
    }

    // APPROVED
    d.decision = Decision::APPROVED;
    d.reject = RejectCode::INTERNAL_ERROR;  // 占位; APPROVED 时忽略
    if (!emit_audit_(intent, d)) {
        d.decision = Decision::REJECTED;
        d.reject = RejectCode::AUDIT_WAL_BACKPRESSURE;
    }
    return d;
}

}  // namespace stcpp::risk
