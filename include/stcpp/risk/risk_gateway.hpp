// stcpp/risk/risk_gateway.hpp — RiskGateway v0.5 (老沈 W9 Wave 57 P0)
//
// v0.4 → v0.5 变更 (ADR-027 §4 enforce, 老韩 spec laohan-w9-orderintent-v05-spec-v1.md):
//   OrderIntent:
//     - market_id → condition_id (rename, ABI break #1)
//     - + token_id: string (新增, ABI break #2)
//     - + outcome: Outcome enum (新增, ABI break #3)
//     - is_buy: bool → side: Side enum (ABI break #4)
//   AuditRecord:
//     - market_id → condition_id
//     - + token_id: string
//     - + outcome: uint8_t
//     - + side: uint8_t
//   RiskConfig:
//     - + per_outcome_cap_usdc (R6.2b per-token cap)
//   RiskGateway:
//     - set_market_exposure() 保留 (兼容), 新增 set_condition_exposure() + set_outcome_exposure()
//     - + set_token_book_freshness_ms() (R8.4 book_snapshot_ts ↔ token_id 一致性)
//
// cite (ADR-027 §4 Enforce-1 强 enforce):
//   polymarket_ssot_cite: laoli-w8-polymarket-data-structure-ssot-v1.md §3 §4.1 §6
//   goalserve_ssot_cite:  N/A (OrderIntent 不接 Goalserve)
//   handshake_cite:       laoli-laoSun-handshake-v1.md §3 SignedOrder ABI lock (token_id + side)
//   adr_ref:              ADR-027 §4 Enforce-1/2/3/4
//
// 落: laohan-w9-orderintent-v05-spec-v1.md / laoli-w8-polymarket-data-structure-ssot-v1.md
//     laoli-laoSun-handshake-v1.md §3 / ADR-004 (reject 顺序)
//
// 红线: R-1 (必经 evaluate + emit audit) / R-7 (ExecutionMode build-time)
//       R-11 (paper 走 PaperAudit) / R-20 (4 ts PIT chain)
//
// ABI lock v1.7 (老高 W9 W4): Position/OrderIntent/Outcome/Side enum lock
//   stub: 老孙 SignerV52 v5.3 联调 + 老唐 WAL schema v1.3 联调 W9 W4 完成
//
// evaluate() 10 reject short-circuit 顺序 (SSOT = ADR-004, spec doc 不复刻):
//   1. state            (HALTED / DRAIN / SAFE_MODE — DRAIN/SAFE_MODE 平仓放行)
//   2. invalid_intent   (R-20 PIT 4 ts + 字段 + v0.5: token_id/condition_id/side 校验)
//   3. duplicate_intent
//   4. stale_data       (含 MarketState 5 档阈值 + recon 全局 + R8.4 book_token_id_mismatch)
//   5. market           (MARKET_TYPE_NOT_ENABLED / MARKET_NOT_ACTIVE)
//   6. position_caps    [ADR-004 前移] R6.2a per_condition + R6.2b per_outcome +
//                       EXCEED_PER_ORDER_CAP / INSUFFICIENT_BANKROLL / DAILY_LOSS_HALT /
//                       CONSEC_LOSS_HALT
//   7. liquidity        EXCEED_BOOK_DEPTH / LOW_FILL_RATE / EXCESSIVE_SLIPPAGE
//   8. signal           EDGE_CI_NEGATIVE / EDGE_NEGATED_BY_SLIPPAGE
//   9. strategy_decayed
//  10. AUDIT_WAL_BACKPRESSURE (emit 失败兜底)

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
#include "stcpp/strategy/signal_iface.hpp"

namespace stcpp::risk {

// Import Outcome and Side from strategy namespace into risk namespace
// (ABI lock v1.7: enum values固定, 老高 CI grep 守护)
using stcpp::strategy::Outcome;
using stcpp::strategy::Side;

// ---------- OrderIntent v0.5 (老沈 W9 Wave 57, ADR-027 §4 enforce) -------------
// ABI lock v1.7: 4 处 breaking (见 §3 laohan-w9-orderintent-v05-spec-v1.md)
struct OrderIntent {
    // ---- R-20 4 ts (不变, ADR R-20 红线) ----
    // event_ts <= data_source_ts <= ingestion_ts <= as_of_ts
    std::int64_t event_ts_ns{0};
    std::int64_t data_source_ts_ns{0};
    std::int64_t ingestion_ts_ns{0};
    std::int64_t as_of_ts_ns{0};

    // ---- 市场标识 (双主键, SSOT §2.3 concept 区分) ----
    // condition_id: bytes32 hex (0x 前缀, 66 char), market 级, per-condition cap 用
    // SSOT: laoli-w8-polymarket-data-structure-ssot-v1.md §2.3 §3.2 conditionId
    // ABI break #1: v0.4 market_id rename → condition_id
    std::string  condition_id;          // ← v0.4 market_id rename

    // token_id: uint256 string (无 0x 前缀, 十进制, 最多 77 位), outcome 级
    // SSOT: laoli-w8-polymarket-data-structure-ssot-v1.md §2.3 §3.3 token_id
    // handshake cite: laoli-laoSun-handshake-v1.md §3 SignedOrder.token_id
    // EIP-712 Order.tokenId = uint256(token_id) — 下单 CLOB 一等公民
    // ABI break #2: v0.5 新增
    std::string  token_id;              // ← v0.5 新增

    // outcome: per-token outcome 语义标注 (与 token_id 冗余但 RM R6.2b + audit 用)
    // SSOT: laoli-w8-polymarket-data-structure-ssot-v1.md §3.3 tokens[i].outcome
    // ABI break #3: v0.5 新增
    Outcome      outcome{Outcome::Yes}; // ← v0.5 新增

    // side: BUY/SELL, 与 outcome 解耦
    // SSOT: laoli-laoSun-handshake-v1.md §3 SignedOrder.side (uint8, BUY=0/SELL=1)
    // ABI break #4: v0.4 is_buy:bool → Side enum
    Side         side{Side::Buy};       // ← v0.4 is_buy 替换

    // ---- 业务 ID ----
    std::string  strategy_id;
    std::string  signal_id;             // 幂等 key (不变)
    std::string  feature_snapshot_id;  // ML-R8 复盘锚 (不变)

    // ---- 定价 ----
    double       price{0.0};           // ∈ (0, 1)
    std::int64_t size_usdc{0};         // > 0 整 cent

    // ---- SlippageModel 入参 + book context ----
    // book_snapshot_ts_ns 必须与 token_id 对齐: 同一 token 的 book 快照
    // SSOT: laoli-w8-polymarket-data-structure-ssot-v1.md §3.4 /book?token_id=
    double       book_depth_l1_usdc{0.0};
    std::int64_t book_snapshot_ts_ns{0};   // R8 signal validity: book_ts ↔ token_id 对齐
    double       tick_size{0.01};           // per-token (SSOT §5 T-07: 不恒为 0.01)

    // ---- 平仓标志 (保留, DRAIN 模式依赖) ----
    // 平仓语义: side=Sell + token_id = 持仓 token
    // 示例: 持 YES 仓平仓 = side=Sell + outcome=Yes + token_id=YES_token_id
    bool         is_close{false};      // 保留 (DRAIN 仅放行 is_close=true)
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

// D-06 红线: 任一 HALT <= 30,000ms (老韩 v0.3.1 static_assert 等价编译期保障)
static_assert(threshold_of(MarketState::INPLAY_HOT_CRIT).halt_ms <= 30'000);
static_assert(threshold_of(MarketState::INPLAY_HOT).halt_ms      <= 30'000);
static_assert(threshold_of(MarketState::INPLAY_COLD).halt_ms     <= 30'000);
static_assert(threshold_of(MarketState::PREGAME).halt_ms         == 15'000);
static_assert(threshold_of(MarketState::SETTLED).halt_ms         <= 30'000);

// ---------- AuditEmitter 抽象 (RM-internal port; W5 接 obs::AuditEmitter) ----

// AuditRecord v0.5 (老沈 W9 Wave 57, 老唐 WAL schema v1.3 联动 stub)
// WAL schema v1.3: market_id → condition_id, + token_id + outcome + side
// ABI lock v1.7 stub: sizeof(AuditRecord) static_assert 在老唐 WAL schema v1.3 完成后添加
struct AuditRecord {
    std::array<std::uint8_t, 16> audit_id{};
    std::int64_t                 event_ts_ns{0};
    std::int64_t                 data_source_ts_ns{0};
    std::int64_t                 ingestion_ts_ns{0};
    std::int64_t                 as_of_ts_ns{0};
    RejectCode                   reject{RejectCode::INTERNAL_ERROR};
    InvalidIntentSubReason       sub_reason{InvalidIntentSubReason::NONE};
    Decision                     decision{Decision::APPROVED};
    // v0.5: market_id → condition_id
    std::string                  condition_id;  // ← v0.4 market_id rename
    // v0.5: 新增
    std::string                  token_id;      // ← v0.5 新增 (老唐 WAL v1.3 联动 stub)
    std::uint8_t                 outcome{0};    // Outcome enum 底层值 (← v0.5 新增)
    std::uint8_t                 side_val{0};   // Side enum 底层值 (← v0.5 新增)
    std::string                  signal_id;
};

class AuditEmitter {
 public:
    virtual ~AuditEmitter() = default;
    // 返 false → AUDIT_WAL_BACKPRESSURE (老韩 v0.3 §12.3 fail-closed)
    [[nodiscard]] virtual bool emit(AuditRecord const& rec) noexcept = 0;
};

// ---------- RiskGateway 主类 -------------------------------------------------

// 配置 v0.5 (老沈 W9 Wave 57)
struct RiskConfig {
    std::int64_t per_order_cap_usdc            = 10'000;
    std::int64_t market_exposure_cap_usdc      = 50'000;   // v0.5: per-condition cap
    std::int64_t per_outcome_cap_usdc          = 25'000;   // v0.5 新增: per-token cap (R6.2b)
    std::int64_t bankroll_usdc                 = 100'000;
    std::int64_t daily_loss_halt_usdc          = 5'000;
    std::int32_t consec_loss_halt_count        = 5;
    std::int32_t excessive_slippage_bps        = 200;      // 小肖 v1 默认
    double       edge_ci_lower_floor           = 0.0;      // CI 下界 > 0 才放行
    // 市场类型白名单 (MVP 只开 MONEYLINE; 用 bitmap 表示, 此处简化为 bool)
    bool         enable_moneyline              = true;
    bool         enable_totals                 = false;
    bool         enable_spreads                = false;
    // STRATEGY_DECAYED (v0.3 §16 OQ-D13)
    double       strategy_decay_min_ev_ratio   = 0.3;   // EV(realized) / EV(forecast) < 0.3
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
    // v0.4 兼容接口 (market_id 映射到 condition_id)
    void set_market_exposure(std::string const& market_id, std::int64_t usdc) noexcept;
    // v0.5 新接口: per-condition + per-outcome 双维度
    void set_condition_exposure(std::string const& condition_id, std::int64_t usdc) noexcept;
    void set_outcome_exposure(std::string const& token_id, std::int64_t usdc) noexcept;

    void set_daily_pnl(std::int64_t usdc) noexcept { daily_pnl_usdc_.store(usdc); }
    void set_consec_loss(std::int32_t n)  noexcept { consec_loss_.store(n); }
    void set_bankroll(std::int64_t usdc)  noexcept { bankroll_usdc_.store(usdc); }

    // 信号注入 (W5 接小肖 Kelly+CI, 当前测试 setter)
    void set_edge_ci_lower(std::string const& signal_id, double v) noexcept;
    void set_strategy_ev_ratio(std::string const& strategy_id, double r) noexcept;

    // 数据源 freshness (W5 接小袁 BookSnapshotProvider, 当前测试 setter)
    void set_market_freshness_ms(std::string const& market_id, std::uint32_t ms) noexcept;
    // v0.5 新增: per-token book freshness (R8.4)
    void set_token_book_freshness_ms(std::string const& token_id, std::uint32_t ms) noexcept;
    void set_recon_freshness_ms(std::uint32_t ms) noexcept { recon_freshness_ms_.store(ms); }
    void set_market_state(std::string const& market_id, MarketState s) noexcept;
    void set_market_active(std::string const& market_id, bool active) noexcept;

    // 幂等 cache 直接清 (单测用)
    void clear_idempotency() noexcept;

    // 公开 helper (单测 + audit 用): ULID 生成 stub
    [[nodiscard]] static std::array<std::uint8_t, 16> next_audit_id(std::int64_t now_ns) noexcept;

 private:
    // ---- 10 reject rule helper (优先级 short-circuit; 命中即返回 reject) ----
    // 1. 状态机 (HALTED / DRAIN / SAFE_MODE)
    [[nodiscard]] bool check_state_(OrderIntent const& it, RiskDecision& d) const noexcept;
    // 2. INVALID_INTENT (含 R-20 PIT 4 ts + 字段 + v0.5: token_id/condition_id/side)
    [[nodiscard]] bool check_invalid_intent_(OrderIntent const& it, RiskDecision& d) const noexcept;
    // 3. DUPLICATE_INTENT
    [[nodiscard]] bool check_duplicate_(OrderIntent const& it, RiskDecision& d) noexcept;
    // 4. STALE_DATA (含 MarketState 5 档 + R8.4 book_token_id_mismatch)
    [[nodiscard]] bool check_stale_data_(OrderIntent const& it, RiskDecision& d) const noexcept;
    // 5. 市场 (MARKET_TYPE_NOT_ENABLED / MARKET_NOT_ACTIVE)
    [[nodiscard]] bool check_market_(OrderIntent const& it, RiskDecision& d) const noexcept;
    // 6. 仓位 / 资金 (ADR-004: 前移)
    //   R6.2a EXCEED_CONDITION_EXPOSURE / R6.2b EXCEED_PER_OUTCOME_CAP /
    //   EXCEED_PER_ORDER_CAP / INSUFFICIENT_BANKROLL / DAILY_LOSS_HALT / CONSEC_LOSS_HALT
    [[nodiscard]] bool check_position_caps_(OrderIntent const& it, RiskDecision& d) const noexcept;
    // 7. 流动性 (ADR-004: 后移于 caps; 仍在 signal 前以填 d.slippage_bps)
    [[nodiscard]] bool check_liquidity_(OrderIntent const& it, RiskDecision& d) const noexcept;
    // 8. 信号 (EDGE_CI_NEGATIVE / EDGE_NEGATED_BY_SLIPPAGE)
    [[nodiscard]] bool check_signal_(OrderIntent const& it, RiskDecision& d) const noexcept;
    // 9. STRATEGY_DECAYED
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

    // pImpl (含 map-based state: exposure / freshness / signal 等)
    struct State_;
    std::unique_ptr<State_> s_;
};

}  // namespace stcpp::risk
