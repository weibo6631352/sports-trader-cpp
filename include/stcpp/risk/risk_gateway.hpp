// stcpp/risk/risk_gateway.hpp — RiskGateway v0.6 (老孙 Wave 104 P0)
//
// v0.5 → v0.6 变更 (Wave 104 P0, 与老唐 audit v1.4 + 老孙 SignerV62 spec 对齐):
//   OrderIntent:
//     - size_usdc rename → size_pUSD_micro (USDC.e → pUSD, ADR R-20 cite; 语义: micro = 1e-6)
//     - + timestamp_ms: int64_t (V2 EIP-712 Order.timestamp, 替代 nonce, ms)
//     - + metadata: string (V2 EIP-712 Order.metadata, bytes32 hex, 0x 前缀)
//     - + builder: string (V2 EIP-712 Order.builder optional, bytes32 hex, 0x 前缀)
//   (all other OrderIntent v0.5 fields: unchanged)
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
//   polymarket_ssot_cite: laoli-w9-w5-polymarket-market-research-update-v1.md §3.1 §3.4
//                         (V2 timestamp/metadata/builder; pUSD rename)
//   goalserve_ssot_cite:  N/A (OrderIntent 不接 Goalserve)
//   handshake_cite:       laoli-laoSun-handshake-v1.md §3 SignedOrder ABI lock (token_id + side)
//   laosun_v62_spec_cite: docs/RESEARCH/laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md §2.3
//   laotang_v14_cite:     include/stcpp/observability/audit_record.hpp v1.4
//   (timestamp_ms/metadata/builder/size_pUSD_micro) adr_ref:              ADR-027 §4 Enforce-1/2/3/4 /
//   ADR-029 / ADR-032 / ADR-034 v2.1
//
// 落: laohan-w9-orderintent-v05-spec-v1.md / laoli-w8-polymarket-data-structure-ssot-v1.md
//     laoli-laoSun-handshake-v1.md §3 / ADR-004 (reject 顺序)
//
// 红线: R-1 (必经 evaluate + emit audit) / R-7 (ExecutionMode build-time)
//       R-11 (paper 走 PaperAudit) / R-20 (4 ts PIT chain)
//       R-20: timestamp_ms 来自 OrderIntent (上游填入, 禁 RM 内 now() 替代)
//       spec-10: timestamp_ms = 0 → INVALID_INTENT/TS_V2_MISSING (transformer_v62 层校验)
//
// ABI lock v1.8 (Wave 104): OrderIntent v0.6 字段集锁定
//   stub: 老孙 SignerV62 v6.2 联调 + 老唐 WAL schema v1.4 联调
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
#include <vector>

#include "stcpp/domain/micro_pusd.hpp"
#include "stcpp/numerical/slippage_model.hpp"
#include "stcpp/risk/reject_enum.hpp"
#include "stcpp/strategy/signal_iface.hpp"

namespace stcpp::risk {

// Import Outcome and Side from strategy namespace into risk namespace
// (ABI lock v1.7: enum values固定, 老高 CI grep 守护)
using stcpp::strategy::Outcome;
using stcpp::strategy::Side;

// ---------- OrderIntent v0.6 (老孙 Wave 104 P0, V2 字段对齐 + pUSD rename) -------
// ABI lock v1.8: v0.5 基础上 +3 新增字段 + 1 rename
//   (与老唐 audit_record.hpp v1.4 + laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md §2.3 对齐)
//
// cite:
//   polymarket_ssot_cite: laoli-w9-w5-polymarket-market-research-update-v1.md §3.1
//                         (timestamp_ms/metadata/builder V2 新增; size_pUSD_micro rename)
//   laosun_v62_spec_cite: laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md §2.3 §5.1
//   laotang_v14_cite:     include/stcpp/observability/audit_record.hpp v1.4
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
    // ABI break #1 (v0.5): v0.4 market_id rename → condition_id
    std::string condition_id;  // ← v0.4 market_id rename

    // token_id: uint256 string (无 0x 前缀, 十进制, 最多 77 位), outcome 级
    // SSOT: laoli-w8-polymarket-data-structure-ssot-v1.md §2.3 §3.3 token_id
    // handshake cite: laoli-laoSun-handshake-v1.md §3 SignedOrder.token_id
    // EIP-712 Order.tokenId = uint256(token_id) — 下单 CLOB 一等公民
    // ABI break #2 (v0.5): 新增
    std::string token_id;  // ← v0.5 新增

    // outcome: per-token outcome 语义标注 (与 token_id 冗余但 RM R6.2b + audit 用)
    // SSOT: laoli-w8-polymarket-data-structure-ssot-v1.md §3.3 tokens[i].outcome
    // ABI break #3 (v0.5): 新增
    Outcome outcome{Outcome::Yes};  // ← v0.5 新增

    // side: BUY/SELL, 与 outcome 解耦
    // SSOT: laoli-laoSun-handshake-v1.md §3 SignedOrder.side (uint8, BUY=0/SELL=1)
    // ABI break #4 (v0.5): v0.4 is_buy:bool → Side enum
    Side side{Side::Buy};  // ← v0.4 is_buy 替换

    // ---- 业务 ID ----
    std::string strategy_id;
    std::string signal_id;            // 幂等 key (不变)
    std::string feature_snapshot_id;  // ML-R8 复盘锚 (不变)

    // ---- 定价 ----
    double price{0.0};  // ∈ (0, 1)

    // v0.6: size_usdc rename → size_pUSD_micro (USDC.e → pUSD; micro = 1e-6 语义不变)
    // cite: laoli-w9-w5 §2.1 抵押品变更 + laosun-w10-w1 §5.1 + audit_record.hpp v1.4
    // ABI break #5 (v0.6): rename (old callers: use size_pUSD_micro)
    std::int64_t size_pUSD_micro{0};  // > 0 micro pUSD, ← v0.5 size_usdc rename

    // ---- SlippageModel 入参 + book context ----
    // book_snapshot_ts_ns 必须与 token_id 对齐: 同一 token 的 book 快照
    // SSOT: laoli-w8-polymarket-data-structure-ssot-v1.md §3.4 /book?token_id=
    double book_depth_l1_usdc{0.0};
    std::int64_t book_snapshot_ts_ns{0};  // R8 signal validity: book_ts ↔ token_id 对齐
    double tick_size{0.01};               // per-token (SSOT §5 T-07: 不恒为 0.01)

    // ---- V2 新增: timestamp_ms (替代 nonce, EIP-712 Order.timestamp) -----------
    // V2 CLOB 订单唯一性: 同地址同 ms 不可重复. Orchestrator 填入系统毫秒时间.
    // 禁 RM 内生成 now(): 调用方 (Orchestrator/策略层) 责任填入.
    // cite: laoli-w9-w5 §3.1 + laosun-w10-w1 §2.1 §8.4
    // spec-10: = 0 → transformer_v62 拒, INVALID_INTENT/TS_V2_MISSING
    // ABI break #6 (v0.6): 新增
    std::int64_t timestamp_ms{0};  // ← v0.6 新增 (V2 EIP-712 Order.timestamp)

    // ---- V2 新增: metadata (EIP-712 Order.metadata bytes32) -------------------
    // bytes32 hex string (0x 前缀 + 64 hex chars = 66 chars); 不用则填 bytes32(0) string.
    // cite: laoli-w9-w5 §3.1 + laosun-w10-w1 §2.1
    // spec-9 (transformer): ^0x[0-9a-f]{64}$ (66 chars) 格式校验
    // ABI break #7 (v0.6): 新增
    std::string metadata{"0x0000000000000000000000000000000000000000000000000000000000000000"};
    // ← v0.6 新增; 默认 bytes32(0)

    // ---- V2 新增: builder (EIP-712 Order.builder bytes32, optional) -----------
    // bytes32 hex string (0x 前缀 + 64 hex chars = 66 chars); gasless relayer.
    // 不用则填 bytes32(0) string.
    // cite: laoli-w9-w5 §3.1 + laosun-w10-w1 §2.1
    // spec-9 (transformer): ^0x[0-9a-f]{64}$ (66 chars) 格式校验
    // ABI break #8 (v0.6): 新增
    std::string builder{"0x0000000000000000000000000000000000000000000000000000000000000000"};
    // ← v0.6 新增; 默认 bytes32(0)

    // ---- 平仓标志 (保留, DRAIN 模式依赖) ----
    // 平仓语义: side=Sell + token_id = 持仓 token
    // 示例: 持 YES 仓平仓 = side=Sell + outcome=Yes + token_id=YES_token_id
    bool is_close{false};  // 保留 (DRAIN 仅放行 is_close=true)
};

// ---------- 决策结果 ---------------------------------------------------------

enum class Decision : std::uint8_t { APPROVED = 0, REJECTED = 1, DEFERRED = 2 };

struct RiskDecision {
    Decision decision{Decision::APPROVED};
    RejectCode reject{RejectCode::INTERNAL_ERROR};
    InvalidIntentSubReason sub_reason{InvalidIntentSubReason::NONE};
    std::array<std::uint8_t, 16> audit_id{};  // ULID, R-1: 非空 (即使 APPROVED)
    std::int64_t decision_ts_ns{0};
    // 透传 SlippageModel 输出 (audit 用)
    std::int32_t slippage_bps{0};
    double expected_fill_rate{0.0};

    [[nodiscard]] bool is_approved() const noexcept { return decision == Decision::APPROVED; }
    [[nodiscard]] bool is_rejected() const noexcept { return decision == Decision::REJECTED; }
};

// ---------- 状态机 (v0.2 §4.1) -----------------------------------------------

enum class RmState : std::uint8_t {
    RUNNING = 0,
    WARNING = 1,
    HALTED = 2,
    SAFE_MODE = 3,  // 启动 / 崩溃默认
    DRAIN = 4,      // 只允许平仓
};

[[nodiscard]] constexpr std::string_view ToString(RmState s) noexcept {
    switch (s) {
        case RmState::RUNNING:
            return "RUNNING";
        case RmState::WARNING:
            return "WARNING";
        case RmState::HALTED:
            return "HALTED";
        case RmState::SAFE_MODE:
            return "SAFE_MODE";
        case RmState::DRAIN:
            return "DRAIN";
    }
    return "unknown";
}

// ---------- MarketState 5 档 + 阈值 (v0.3 §14.1) -----------------------------

enum class MarketState : std::uint8_t {
    INPLAY_HOT_CRIT = 0,  // NBA Q4 < 2min / NFL 2-min / MLB 8+ / NHL P3 close / OT
    INPLAY_HOT = 1,       // 其余 inplay
    INPLAY_COLD = 2,      // pause / replay review
    PREGAME = 3,          // > 30min pre-game
    SETTLED = 4,          // final
};

struct StaleThresholds {
    std::uint32_t warn_ms;
    std::uint32_t halt_ms;
};

[[nodiscard]] constexpr StaleThresholds threshold_of(MarketState s) noexcept {
    switch (s) {
        case MarketState::INPLAY_HOT_CRIT:
            return {200, 800};
        case MarketState::INPLAY_HOT:
            return {500, 2'000};
        case MarketState::INPLAY_COLD:
            return {2'000, 10'000};
        case MarketState::PREGAME:
            return {5'000, 15'000};
        case MarketState::SETTLED:
            return {10'000, 30'000};
    }
    return {200, 800};  // fail-safe 保守档
}

// D-06 红线: 任一 HALT <= 30,000ms (老韩 v0.3.1 static_assert 等价编译期保障)
static_assert(threshold_of(MarketState::INPLAY_HOT_CRIT).halt_ms <= 30'000);
static_assert(threshold_of(MarketState::INPLAY_HOT).halt_ms <= 30'000);
static_assert(threshold_of(MarketState::INPLAY_COLD).halt_ms <= 30'000);
static_assert(threshold_of(MarketState::PREGAME).halt_ms == 15'000);
static_assert(threshold_of(MarketState::SETTLED).halt_ms <= 30'000);

// ---------- AuditEmitter 抽象 (RM-internal port; W5 接 obs::AuditEmitter) ----

// AuditRecord v0.6 (老沈 Wave 3, 老唐 WAL schema v1.4 对齐)
// v0.5: market_id → condition_id, + token_id + outcome + side
// v0.6 Wave 3: + timestamp_ms + metadata + builder (V2 CLOB 字段透传, spec §2.3.6)
// cite: laoshen-rm-v0.5-field-freeze-spec-v1.md §2.3.6 必填字段表
// cite: laohan-rm-v0.5-integration-spec-v1.md §4 T-v06 audit 要求
// ABI lock v1.8 stub: sizeof(AuditRecord) static_assert 在老唐 WAL schema v1.4 完成后添加
struct AuditRecord {
    std::array<std::uint8_t, 16> audit_id{};
    std::int64_t event_ts_ns{0};
    std::int64_t data_source_ts_ns{0};
    std::int64_t ingestion_ts_ns{0};
    std::int64_t as_of_ts_ns{0};
    RejectCode reject{RejectCode::INTERNAL_ERROR};
    InvalidIntentSubReason sub_reason{InvalidIntentSubReason::NONE};
    Decision decision{Decision::APPROVED};
    // v0.5: market_id → condition_id
    std::string condition_id;  // ← v0.4 market_id rename
    // v0.5: 新增
    std::string token_id;      // ← v0.5 新增 (老唐 WAL v1.3 联动 stub)
    std::uint8_t outcome{0};   // Outcome enum 底层值 (← v0.5 新增)
    std::uint8_t side_val{0};  // Side enum 底层值 (← v0.5 新增)
    std::string signal_id;
    // v0.6 Wave 3: V2 CLOB 字段透传 (AuditRecord v1.4 对齐, spec §2.3.6 必填)
    std::int64_t timestamp_ms{0};  // ← v0.6 Wave 3 新增 (V2 EIP-712 Order.timestamp, ms)
    std::string metadata;          // ← v0.6 Wave 3 新增 (bytes32 hex, V2 Order.metadata)
    std::string builder;           // ← v0.6 Wave 3 新增 (bytes32 hex, V2 Order.builder)
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
    // P0-2 c2/c3 (老郭 ADR-041 L1 + 老韩 cap 真值 SSOT, 2026-05-30): 3 cap 字段 MicroPUSD 强类型
    //   (micro pUSD; RM 直接比 size_pUSD_micro)。c3 校准默认值 = 老韩 RM 主权终值
    //   (docs/RESEARCH/laohan-p0-2-c3-cap-truth-ssot-v1.md §1; 锚 bankroll 意图 100k pUSD):
    //     per_order 1000 pUSD (1%) ≤ per_outcome 2000 pUSD (2%) ≤ per_condition 5000 pUSD (5%)
    //   序约束 (单笔 ≤ 单 token ≤ 单 condition) 否则下层 cap 死代码。原默认 10'000 micro=0.01pUSD
    //   荒谬已废。值用 _upusd (micro 直读)。bankroll/daily_loss 仍 int64 留 c2b (勿在 c3 漏改)。
    domain::MicroPUSD per_order_cap_usdc{1'000'000'000};        // 1000 pUSD (micro)
    domain::MicroPUSD market_exposure_cap_usdc{5'000'000'000};  // 5000 pUSD (micro); per-condition
    domain::MicroPUSD per_outcome_cap_usdc{2'000'000'000};      // 2000 pUSD (micro); per-token (R6.2b)
    // c2b (P0-2, 老韩 spec): bankroll/daily_loss 转 MicroPUSD (micro 真值, 锚 bankroll 意图 100k pUSD)。
    //   原默认 100'000/5'000 名带 _usdc 实为 micro=0.1/0.005pUSD 荒谬 (同 cap bug); 校准真值。
    //   atomic bankroll_usdc_ 保持 int64 micro (老姜), ctor .v 灌; set_bankroll(int64) 签名不变。
    domain::MicroPUSD bankroll_usdc{100'000'000'000};  // 100k pUSD (micro)
    // DD 软 / 硬熔断阈值 (GM §9 裁决 #1: -3% 软 / -5% 硬)
    // daily_loss_soft_pct: 跌破后拒新开仓 (is_close=false), 放平仓 (is_close=true)
    // daily_loss_hard_pct: 跌破后 → HALTED, 全拒含平仓, 人工解除
    // daily_loss_halt_usdc: 保留向后兼容 (旧测试); 若 >0 则覆盖 hard_pct 绝对值
    double daily_loss_soft_pct = 0.03;  // v0.6 Wave 3 新增: -3% 软熔断
    double daily_loss_hard_pct = 0.05;  // v0.6 Wave 3 新增: -5% 硬 kill
    // c2b: 5'000'000'000 micro = 5k pUSD = hard_pct 0.05 × 100k (默认绝对值与 pct 分支同阈, 行为零变)
    domain::MicroPUSD daily_loss_halt_usdc{5'000'000'000};  // 5k pUSD (micro); 覆盖 hard_pct
    std::int32_t consec_loss_halt_count = 5;
    std::int32_t excessive_slippage_bps = 200;  // 小肖 v1 默认
    double edge_ci_lower_floor = 0.0;           // CI 下界 > 0 才放行
    // 市场类型白名单 (MVP 只开 MONEYLINE; 用 bitmap 表示, 此处简化为 bool)
    bool enable_moneyline = true;
    bool enable_totals = false;
    bool enable_spreads = false;
    // STRATEGY_DECAYED (v0.3 §16 OQ-D13)
    double strategy_decay_min_ev_ratio = 0.3;  // EV(realized) / EV(forecast) < 0.3
};

class RiskGateway {
public:
    // ctor: 注入 emitter (DI). emitter 由 W5 接 WalWriter, 当前测试用 InMemoryEmitter.
    explicit RiskGateway(RiskConfig cfg, std::shared_ptr<AuditEmitter> emitter) noexcept;
    ~RiskGateway();  // 必出现在 .cpp, 让 pImpl unique_ptr<State_> 析构能见 State_ 全定义
    RiskGateway(RiskGateway const&) = delete;
    RiskGateway& operator=(RiskGateway const&) = delete;
    RiskGateway(RiskGateway&&) = delete;
    RiskGateway& operator=(RiskGateway&&) = delete;

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

    // A4 (老韩 spec §2): 去 inline — body 移 .cpp 以记 last_fed_ns_ (now_realtime_ns 在 .cpp;
    //   避免给 ABI-locked hpp 加 pit.hpp 依赖)。非热路径 (每 tick 喂一次), out-of-line 开销可忽略。
    void set_daily_pnl(std::int64_t usdc) noexcept;
    void set_consec_loss(std::int32_t n) noexcept;
    void set_bankroll(std::int64_t usdc) noexcept;

    // 信号注入 (W5 接小肖 Kelly+CI, 当前测试 setter)
    void set_edge_ci_lower(std::string const& signal_id, double v) noexcept;
    void set_strategy_ev_ratio(std::string const& strategy_id, double r) noexcept;

    // 数据源 freshness (W5 接小袁 BookSnapshotProvider, 当前测试 setter)
    void set_market_freshness_ms(std::string const& market_id, std::uint32_t ms) noexcept;
    // v0.5 新增: per-token book freshness (R8.4)
    void set_token_book_freshness_ms(std::string const& token_id, std::uint32_t ms) noexcept;
    void set_recon_freshness_ms(std::uint32_t ms) noexcept;  // A4: 去 inline (记 last_fed_ns)
    void set_market_state(std::string const& market_id, MarketState s) noexcept;
    void set_market_active(std::string const& market_id, bool active) noexcept;

    // 幂等 cache 直接清 (单测用)
    void clear_idempotency() noexcept;

    // 公开 helper (单测 + audit 用): ULID 生成 stub
    [[nodiscard]] static std::array<std::uint8_t, 16> next_audit_id(std::int64_t now_ns) noexcept;

    // ---- A4 feed-liveness 自检 (老韩 spec §2; retro synthesis §3) ----
    //   病: 上游忘喂某红线 → RM 拿默认值「假装已活」静默放行 = 风控纸面化。
    //   机制: 每红线 setter 末尾 O(1) relaxed store last_fed_ns_ (热路径零感, 不碰 R-12 锁红线);
    //         report() 仅启动期 + 周期巡检调 (绝不进 evaluate 热路径)。
    //   标量红线 = last-fed; map 类红线 = last-any-key-fed (比 ever-fed 更informative: maps 也得 staleness)。
    enum class FeedKey : std::uint8_t {
        Bankroll = 0,  // 标量
        DailyPnl,
        ConsecLoss,
        ReconFreshness,
        Exposure,      // map 类 (condition / outcome / market_exposure 任一喂 → 标活)
        Freshness,     // map 类 (market / token book freshness 任一喂)
        EdgeCi,        // map 类 (per-signal)
        StrategyEv,    // map 类 (per-strategy)
        MarketState,   // map 类
        MarketActive,  // map 类
        COUNT
    };
    struct FeedLivenessRow {
        std::string_view key;      // 红线名
        std::int64_t last_fed_ns;  // 0 = 从未喂过
        bool ever_fed;             // last_fed_ns != 0
    };
    // 诊断快照 (非热路径): 各红线 (是否喂过 + 上次喂 ts)。daemon 启动自检 + 周期巡检调用。
    [[nodiscard]] std::vector<FeedLivenessRow> feed_liveness_report() const noexcept;

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

    // A4: 记某红线被喂 (O(1) relaxed store now_realtime_ns; .cpp 定义, 因 now_realtime_ns 在 .cpp)。
    void mark_fed_(FeedKey k) noexcept;

    // ---- 配置 (ctor 起不可变) ----
    RiskConfig cfg_;

    // ---- 状态 (atomic, 单 writer-thread; 多 reader = monitor) ----
    std::atomic<RmState> state_{RmState::SAFE_MODE};  // 启动默认 SAFE_MODE (v0.2 G8)
    std::atomic<std::int64_t> daily_pnl_usdc_{0};
    std::atomic<std::int32_t> consec_loss_{0};
    std::atomic<std::int64_t> bankroll_usdc_{0};
    std::atomic<std::uint32_t> recon_freshness_ms_{0};
    std::atomic<std::uint64_t> audit_seq_{0};

    // A4 feed-liveness: 每红线上次被喂的 wall ns (0 = 从未喂)。relaxed store/load, 私有诊断成员,
    //   不进任何序列化 struct (RiskConfig/AuditRecord/OrderIntent 布局零变, ABI-中性)。
    std::array<std::atomic<std::int64_t>, static_cast<std::size_t>(FeedKey::COUNT)> last_fed_ns_{};

    // emitter (DI)
    std::shared_ptr<AuditEmitter> emitter_;

    // pImpl (含 map-based state: exposure / freshness / signal 等)
    struct State_;
    std::unique_ptr<State_> s_;
};

}  // namespace stcpp::risk
