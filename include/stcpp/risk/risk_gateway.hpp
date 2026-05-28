// stcpp/risk/risk_gateway.hpp — RiskGateway v0.1 (老韩 Sprint-2 W4 Wave 19)
//   + W5 Wave 24 ADR-004 patch (老沈, 2026-05-28): position_caps / liquidity 顺序互换
//
// 落: laohan-riskmanager-design-v0.3{,.1}.md / xiaoxiao-slippage-model-lib-v1.md
//     laotang-audit-schema-v1.1.md / laowang-wal-framework-cpp-interface-v1.md
//     ADR-004 (docs/ADR/2026-05-28-r07-r08-liquidity-vs-position-cap-priority.md)
//
// 红线: R-1 (必经 evaluate + emit audit) / R-7 (ExecutionMode build-time)
//       R-11 (paper 走 PaperAudit) / R-20 (4 ts PIT chain)
//
// evaluate() 21 reject short-circuit 顺序 (SSOT = ADR-004, spec doc 不复刻):
//   1. state            (HALTED / DRAIN / SAFE_MODE — DRAIN/SAFE_MODE 平仓放行)
//   2. invalid_intent   (R-20 PIT 4 ts + 字段 + 9 sub_reason)
//   3. duplicate_intent
//   4. stale_data       (含 MarketState 5 档阈值 + recon 全局)
//   5. market           (MARKET_TYPE_NOT_ENABLED / MARKET_NOT_ACTIVE)
//   6. position_caps    [ADR-004 前移] EXCEED_PER_ORDER_CAP / EXCEED_MARKET_EXPOSURE /
//                       INSUFFICIENT_BANKROLL / DAILY_LOSS_HALT / CONSEC_LOSS_HALT
//   7. liquidity        [ADR-004 后移] EXCEED_BOOK_DEPTH / LOW_FILL_RATE / EXCESSIVE_SLIPPAGE
//   8. signal           EDGE_CI_NEGATIVE / EDGE_NEGATED_BY_SLIPPAGE
//                       (依 d.slippage_bps, liquidity 已填)
//   9. strategy_decayed
//  10. AUDIT_WAL_BACKPRESSURE (emit 失败兜底, 由 evaluate() 主循环改 reject)
//
// W5+ TODO: 真接老王 WalWriter / 老周 PositionLedger / 小肖 Kelly+CI / 小袁 MarketStateClassifier

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>

#include "stcpp/numerical/slippage_model.hpp"
#include "stcpp/risk/reject_enum.hpp"

namespace stcpp::risk {

// ---------- 4 ts OrderIntent (R-20, 老孙 v5.1) -------------------------------

struct OrderIntent {
    // R-20 4 ts (event ≤ data_source ≤ ingestion ≤ as_of ≤ now)
    std::int64_t event_ts_ns{0};
    std::int64_t data_source_ts_ns{0};
    std::int64_t ingestion_ts_ns{0};
    std::int64_t as_of_ts_ns{0};

    // 业务字段
    std::string  market_id;            // Polymarket condition_id
    std::string  strategy_id;
    std::string  signal_id;            // 幂等 key
    std::string  feature_snapshot_id;  // ML-R8 复盘锚
    bool         is_buy{true};
    double       price{0.0};           // ∈ (0, 1)
    std::int64_t size_usdc{0};         // > 0 整 cent

    // SlippageModel 入参 (调用方填: BookSnapshotProvider @小袁 W5 接)
    double       book_depth_l1_usdc{0.0};
    std::int64_t book_snapshot_ts_ns{0};
    double       tick_size{0.01};

    // 平仓 / 开仓 flag (DRAIN 仅放行平仓)
    bool         is_close{false};
};

// ---------- 决策结果 ---------------------------------------------------------

enum class Decision : std::uint8_t { APPROVED = 0, REJECTED = 1, DEFERRED = 2 };

struct RiskDecision {
    Decision                     decision{Decision::APPROVED};
    RejectCode                   reject{RejectCode::INTERNAL_ERROR};
    InvalidIntentSubReason       sub_reason{InvalidIntentSubReason::NONE};
    std::array<std::uint8_t, 16> audit_id{};   // ULID, R-1: 非空 (即使 APPROVED)
    std::int64_t                 decision_ts_ns{0};
    // 透传 SlippageModel 输出 (audit 用)
    std::int32_t                 slippage_bps{0};
    double                       expected_fill_rate{0.0};

    [[nodiscard]] bool is_approved()  const noexcept { return decision == Decision::APPROVED; }
    [[nodiscard]] bool is_rejected()  const noexcept { return decision == Decision::REJECTED; }
};

// ---------- 状态机 (v0.2 §4.1) -----------------------------------------------

enum class RmState : std::uint8_t {
    RUNNING   = 0,
    WARNING   = 1,
    HALTED    = 2,
    SAFE_MODE = 3,   // 启动 / 崩溃默认
    DRAIN     = 4,   // 只允许平仓
};

[[nodiscard]] constexpr std::string_view ToString(RmState s) noexcept {
    switch (s) {
        case RmState::RUNNING:   return "RUNNING";
        case RmState::WARNING:   return "WARNING";
        case RmState::HALTED:    return "HALTED";
        case RmState::SAFE_MODE: return "SAFE_MODE";
        case RmState::DRAIN:     return "DRAIN";
    }
    return "unknown";
}

// ---------- MarketState 5 档 + 阈值 (v0.3 §14.1) -----------------------------

enum class MarketState : std::uint8_t {
    INPLAY_HOT_CRIT = 0,  // NBA Q4 < 2min / NFL 2-min / MLB 8+ / NHL P3 close / OT
    INPLAY_HOT      = 1,  // 其余 inplay
    INPLAY_COLD     = 2,  // pause / replay review
    PREGAME         = 3,  // > 30min pre-game
    SETTLED         = 4,  // final
};

struct StaleThresholds {
    std::uint32_t warn_ms;
    std::uint32_t halt_ms;
};

[[nodiscard]] constexpr StaleThresholds threshold_of(MarketState s) noexcept {
    switch (s) {
        case MarketState::INPLAY_HOT_CRIT: return {200,    800};
        case MarketState::INPLAY_HOT:      return {500,  2'000};
        case MarketState::INPLAY_COLD:     return {2'000, 10'000};
        case MarketState::PREGAME:         return {5'000, 15'000};
        case MarketState::SETTLED:         return {10'000, 30'000};
    }
    return {200, 800};  // fail-safe 保守档
}

// D-06 红线: 任一 HALT ≤ 30,000ms (老韩 v0.3.1 static_assert 等价编译期保障)
static_assert(threshold_of(MarketState::INPLAY_HOT_CRIT).halt_ms <= 30'000);
static_assert(threshold_of(MarketState::INPLAY_HOT).halt_ms      <= 30'000);
static_assert(threshold_of(MarketState::INPLAY_COLD).halt_ms     <= 30'000);
static_assert(threshold_of(MarketState::PREGAME).halt_ms         == 15'000);
static_assert(threshold_of(MarketState::SETTLED).halt_ms         <= 30'000);

// ---------- AuditEmitter 抽象 (RM-internal port; W5 接 obs::AuditEmitter) ----

struct AuditRecord {
    std::array<std::uint8_t, 16> audit_id{};
    std::int64_t                 event_ts_ns{0};
    std::int64_t                 data_source_ts_ns{0};
    std::int64_t                 ingestion_ts_ns{0};
    std::int64_t                 as_of_ts_ns{0};
    RejectCode                   reject{RejectCode::INTERNAL_ERROR};
    InvalidIntentSubReason       sub_reason{InvalidIntentSubReason::NONE};
    Decision                     decision{Decision::APPROVED};
    std::string                  market_id;
    std::string                  signal_id;
};

class AuditEmitter {
 public:
    virtual ~AuditEmitter() = default;
    // 返 false → AUDIT_WAL_BACKPRESSURE (老韩 v0.3 §12.3 fail-closed)
    [[nodiscard]] virtual bool emit(AuditRecord const& rec) noexcept = 0;
};

// ---------- RiskGateway 主类 -------------------------------------------------

// 配置 (v0.1 起 minimal, W5+ 扩展)
struct RiskConfig {
    std::int64_t per_order_cap_usdc            = 10'000;
    std::int64_t market_exposure_cap_usdc      = 50'000;
    std::int64_t bankroll_usdc                 = 100'000;
    std::int64_t daily_loss_halt_usdc          = 5'000;
    std::int32_t consec_loss_halt_count        = 5;
    std::int32_t excessive_slippage_bps        = 200;        // 小肖 v1 默认
    double       edge_ci_lower_floor           = 0.0;        // CI 下界 > 0 才放行
    // 市场类型白名单 (MVP 只开 MONEYLINE; 用 bitmap 表示, 此处简化为 bool)
    bool         enable_moneyline              = true;
    bool         enable_totals                 = false;
    bool         enable_spreads                = false;
    // STRATEGY_DECAYED (v0.3 §16 OQ-D13)
    double       strategy_decay_min_ev_ratio   = 0.3;   // EV(realized) / EV(forecast) < 0.3 → DECAYED
};

class RiskGateway {
 public:
    // ctor: 注入 emitter (DI). emitter 由 W5 接 WalWriter, 当前测试用 InMemoryEmitter.
    explicit RiskGateway(RiskConfig cfg,
                         std::shared_ptr<AuditEmitter> emitter) noexcept;
    ~RiskGateway();  // 必出现在 .cpp, 让 pImpl unique_ptr<State_> 析构能见 State_ 全定义
    RiskGateway(RiskGateway const&)            = delete;
    RiskGateway& operator=(RiskGateway const&) = delete;
    RiskGateway(RiskGateway&&)                 = delete;
    RiskGateway& operator=(RiskGateway&&)      = delete;

    // 核心入口. noexcept (P0: 不允许抛, fail-closed 必返 reject).
    [[nodiscard]] RiskDecision evaluate(OrderIntent const& intent) noexcept;

    // 状态机操控 (人工 unlock / SIGTERM / chaos drill)
    void set_state(RmState s) noexcept { state_.store(s, std::memory_order_release); }
    [[nodiscard]] RmState state() const noexcept { return state_.load(std::memory_order_acquire); }

    // 仓位 / 盈亏注入 (W5 接老周 PositionLedger, 当前测试直接 setter)
    void set_market_exposure(std::string const& market_id, std::int64_t usdc) noexcept;
    void set_daily_pnl(std::int64_t usdc) noexcept { daily_pnl_usdc_.store(usdc); }
    void set_consec_loss(std::int32_t n)  noexcept { consec_loss_.store(n); }
    void set_bankroll(std::int64_t usdc)  noexcept { bankroll_usdc_.store(usdc); }

    // 信号注入 (W5 接小肖 Kelly+CI, 当前测试 setter)
    void set_edge_ci_lower(std::string const& signal_id, double v) noexcept;
    void set_strategy_ev_ratio(std::string const& strategy_id, double r) noexcept;

    // 数据源 freshness (W5 接小袁 BookSnapshotProvider, 当前测试 setter)
    void set_market_freshness_ms(std::string const& market_id, std::uint32_t ms) noexcept;
    void set_recon_freshness_ms(std::uint32_t ms) noexcept { recon_freshness_ms_.store(ms); }
    void set_market_state(std::string const& market_id, MarketState s) noexcept;
    void set_market_active(std::string const& market_id, bool active) noexcept;

    // 幂等 cache 直接清 (单测用)
    void clear_idempotency() noexcept;

    // 公开 helper (单测 + audit 用): ULID 生成 stub
    [[nodiscard]] static std::array<std::uint8_t, 16> next_audit_id(std::int64_t now_ns) noexcept;

 private:
    // ---- 21 reject rule helper (优先级 short-circuit; 命中即返回 reject) ----
    // 1-3 状态机
    [[nodiscard]] bool check_state_(OrderIntent const& it, RiskDecision& d) const noexcept;
    // 4 INVALID_INTENT (含 R-20 PIT 5 sub_reason + 字段 4 sub_reason)
    [[nodiscard]] bool check_invalid_intent_(OrderIntent const& it, RiskDecision& d) const noexcept;
    // 5 DUPLICATE_INTENT
    [[nodiscard]] bool check_duplicate_(OrderIntent const& it, RiskDecision& d) noexcept;
    // 6 STALE_DATA (含 MarketState 5 档)
    [[nodiscard]] bool check_stale_data_(OrderIntent const& it, RiskDecision& d) const noexcept;
    // 5 市场 (MARKET_TYPE_NOT_ENABLED / MARKET_NOT_ACTIVE)
    [[nodiscard]] bool check_market_(OrderIntent const& it, RiskDecision& d) const noexcept;
    // 6 仓位 / 资金 (ADR-004: 前移 — 公司红线先于市场状态)
    //   EXCEED_PER_ORDER_CAP / EXCEED_MARKET_EXPOSURE / INSUFFICIENT_BANKROLL /
    //   DAILY_LOSS_HALT / CONSEC_LOSS_HALT
    [[nodiscard]] bool check_position_caps_(OrderIntent const& it, RiskDecision& d) const noexcept;
    // 7 流动性 (ADR-004: 后移于 caps; 仍在 signal 前以填 d.slippage_bps)
    //   调 SlippageModel: EXCEED_BOOK_DEPTH / LOW_FILL_RATE / EXCESSIVE_SLIPPAGE
    [[nodiscard]] bool check_liquidity_(OrderIntent const& it, RiskDecision& d) const noexcept;
    // 8 信号 (EDGE_CI_NEGATIVE / EDGE_NEGATED_BY_SLIPPAGE)
    [[nodiscard]] bool check_signal_(OrderIntent const& it, RiskDecision& d) const noexcept;
    // 9 STRATEGY_DECAYED
    [[nodiscard]] bool check_strategy_decayed_(OrderIntent const& it, RiskDecision& d) const noexcept;

    // emit audit + 填 audit_id. 返 false → AUDIT_WAL_BACKPRESSURE.
    [[nodiscard]] bool emit_audit_(OrderIntent const& it, RiskDecision& d) noexcept;

    // ---- 配置 (ctor 起不可变) ----
    RiskConfig cfg_;

    // ---- 状态 (atomic, 单 writer-thread; 多 reader = monitor) ----
    std::atomic<RmState>       state_{RmState::SAFE_MODE};  // 启动默认 SAFE_MODE (v0.2 G8)
    std::atomic<std::int64_t>  daily_pnl_usdc_{0};
    std::atomic<std::int32_t>  consec_loss_{0};
    std::atomic<std::int64_t>  bankroll_usdc_{0};
    std::atomic<std::uint32_t> recon_freshness_ms_{0};
    std::atomic<std::uint64_t> audit_seq_{0};

    // emitter (DI)
    std::shared_ptr<AuditEmitter> emitter_;

    // 幂等 cache (W5 W6 LRU 接老吴, 当前 unordered_set + 测试体量足)
    // 注: pImpl 简化, 直接放 std::unordered_set string. 真上线换 SwissTable + LRU.
    struct State_;
    std::unique_ptr<State_> s_;
};

}  // namespace stcpp::risk
