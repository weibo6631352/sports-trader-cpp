// stcpp/strategy/signal_iface.hpp — Signal 抽象 v0.2 (W9 Wave 57 P0)
//
// 落: 小程 P0-01 spec v0.1 §1 (SignalContext + SignalOutput + ISignalEngine)
//
// v0.2 变更 (老沈 W9 Wave 57, ADR-027 §4 enforce):
//   - Outcome enum 新增 (Yes/No/Home/Draw/Away/Over/Under)
//   - Side enum 重写: Buy/Sell 替代 BuyYes/BuyNo (解耦 outcome 与 side)
//   - SignalContext: market_id 保留 (RM 层 condition_id 对齐在 risk_gateway.hpp)
//
// cite:
//   polymarket_ssot_cite: laoli-w8-polymarket-data-structure-ssot-v1.md §3 §4.1 §6
//   goalserve_ssot_cite:  N/A (OrderIntent 不直接对接 Goalserve; Outcome enum 扩展预留
//                             Home/Draw/Away 待小段 goalserve-data-structure-ssot 确认
//                             3-way market 后触发 L1)
//   handshake_cite:       laoli-laoSun-handshake-v1.md §3 SignedOrder ABI lock (token_id + side 字段)
//   adr_ref:              ADR-027 §4 Enforce-1/2/3/4
//
// 红线:
//   R-20 SignalContext 4 ts + feature_snapshot_id 必带 (调用方 PIT 校验)
//   ML-R5 rule-based, ISignalEngine 实现禁触 ML (含 P0-01 / P0-02)
//   R-11 mock 数据 → paper_audit (Pinnacle CSV W5 接老彭)
//
// 接口约定:
//   tick(ctx) 单次评估, 返 std::optional<SignalOutput>:
//     - 5 触发条件 AND 满足 → 返 SignalOutput (RM 上游)
//     - 任一条件不满足 → 返 nullopt (silent skip, 不构成 reject)
//   实现必 noexcept, fail-closed 走 nullopt (不抛).

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace stcpp::strategy {

// 信号 ID 全集 (老郭 ADR-003 spec 内策略枚举, P0 + P1 占位).
// 新增策略必 enum 这里 + audit 落, 不允许字符串 strategy_id 漂移.
enum class SignalId : std::uint8_t {
    P0_01_PinnacleNoVig       = 0,
    P0_02_ScorePriceMismatch  = 1,
    // P1-* 占位; W5+ 加
};

[[nodiscard]] constexpr std::string_view to_string(SignalId id) noexcept {
    switch (id) {
        case SignalId::P0_01_PinnacleNoVig:      return "P0_01_PinnacleNoVig";
        case SignalId::P0_02_ScorePriceMismatch: return "P0_02_ScorePriceMismatch";
    }
    return "unknown";
}

// Outcome — Polymarket token 级一等公民 (v0.5, ADR-027 §4)
// SSOT cite: laoli-w8-polymarket-data-structure-ssot-v1.md §2.1 §6.2
// 1 condition_id = 2..N token_id; Outcome 标注每个 token 的语义
// ABI lock v1.7: 枚举值固定, 不可随意改动 (老高 CI grep 守护)
enum class Outcome : std::uint8_t {
    Yes   = 0,  // Binary market (Moneyline / Props / 系列赛 binary)
    No    = 1,  // Binary market complement
    // 3-way 预留 (待小段 Goalserve SSOT §4 确认 soccer market 后触发 L1)
    Home  = 2,  // 未来: Soccer Moneyline 主场
    Draw  = 3,  // 未来: Soccer Moneyline 平局
    Away  = 4,  // 未来: Soccer Moneyline 客场
    // Totals 预留
    Over  = 5,  // 未来: Total Over
    Under = 6,  // 未来: Total Under
};

[[nodiscard]] constexpr std::string_view to_string(Outcome o) noexcept {
    switch (o) {
        case Outcome::Yes:   return "Yes";
        case Outcome::No:    return "No";
        case Outcome::Home:  return "Home";
        case Outcome::Draw:  return "Draw";
        case Outcome::Away:  return "Away";
        case Outcome::Over:  return "Over";
        case Outcome::Under: return "Under";
    }
    return "unknown";
}

// Side — BUY/SELL, 与 Outcome 解耦 (v0.5, ADR-027 §4)
// SSOT cite: laoli-laoSun-handshake-v1.md §3 SignedOrder.side (uint8, BUY=0/SELL=1)
// side=Buy+outcome=Yes 表达 BuyYes; side=Sell+outcome=Yes 表达 SellYes (平 Yes 仓)
// ABI lock v1.7: 枚举值固定 (Buy=0, Sell=1 对应 Polymarket EIP-712 Order.side)
enum class Side : std::uint8_t {
    Buy  = 0,   // Polymarket SignedOrder.side = 0 (BUY)
    Sell = 1,   // Polymarket SignedOrder.side = 1 (SELL)
};

[[nodiscard]] constexpr std::string_view to_string(Side s) noexcept {
    return s == Side::Buy ? "BUY" : "SELL";
}

// R-20 4 ts + feature_snapshot_id (老孙 v5.1 IPC, 老韩 v0.3.1 PIT)
struct SignalContext {
    std::int64_t event_ts_ns{0};
    std::int64_t data_source_ts_ns{0};
    std::int64_t ingestion_ts_ns{0};
    std::int64_t as_of_ts_ns{0};
    std::string  market_id;            // Polymarket condition_id
    std::string  feature_snapshot_id;  // ML-R8 复盘锚, R-20 hash
};

// 信号输出 (5 字段, 小程 spec v0.1 §1; v0.5: Side 枚举 Buy/Sell)
struct SignalOutput {
    SignalId     signal_id{SignalId::P0_01_PinnacleNoVig};
    Side         side{Side::Buy};   // v0.5: Buy (原 BuyYes) — outcome 由 token_id 决定
    std::int32_t edge_bps{0};            // round(edge * 10000)
    std::int64_t suggested_size_usdc{0}; // 整 cent, clip $5K
    double       confidence{0.0};        // clamp(edge / 0.10, 0, 1)
};

// 抽象基类 — 不调 ML (ML-R5), 不写 ledger (R-11), 不直接调 RM.
class ISignalEngine {
 public:
    ISignalEngine()                                          = default;
    ISignalEngine(ISignalEngine const&)                      = delete;
    ISignalEngine(ISignalEngine&&) noexcept                  = delete;
    ISignalEngine& operator=(ISignalEngine const&)           = delete;
    ISignalEngine& operator=(ISignalEngine&&) noexcept       = delete;
    virtual ~ISignalEngine()                                 = default;

    // 核心入口. 不抛, fail-closed → nullopt.
    [[nodiscard]] virtual std::optional<SignalOutput> tick(SignalContext const& ctx) noexcept = 0;

    // 调试 / audit 用
    [[nodiscard]] virtual SignalId id() const noexcept = 0;
};

}  // namespace stcpp::strategy
