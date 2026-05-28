// stcpp/strategy/signal_iface.hpp — Signal 抽象 v0.1 (Sprint-2 W4)
//
// 落: 小程 P0-01 spec v0.1 §1 (SignalContext + SignalOutput + ISignalEngine)
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

// BUY_YES / BUY_NO — Polymarket binary outcome
enum class Side : std::uint8_t {
    BuyYes = 0,
    BuyNo  = 1,
};

[[nodiscard]] constexpr std::string_view to_string(Side s) noexcept {
    return s == Side::BuyYes ? "BUY_YES" : "BUY_NO";
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

// 信号输出 (5 字段, 小程 spec v0.1 §1)
struct SignalOutput {
    SignalId     signal_id{SignalId::P0_01_PinnacleNoVig};
    Side         side{Side::BuyYes};
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
