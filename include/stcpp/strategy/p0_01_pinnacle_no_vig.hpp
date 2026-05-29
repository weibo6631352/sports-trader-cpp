// stcpp/strategy/p0_01_pinnacle_no_vig.hpp — P0-01 Pinnacle no-vig 信号 v0.1
//
// 落: 小程 P0-01 spec v0.1 (no-vig multiplicative + 5 触发条件 AND + Kelly·0.25)
//
// 红线:
//   R-20   SignalContext 4 ts + feature_snapshot_id 必带, ts<=0 → nullopt
//   ML-R5  rule-based, 不调 ML
//   R-11   Pinnacle 数据 mock 进 paper_audit (W5 接老彭 CSV → 真接口)
//
// 5 触发条件 (AND, 任一不满足 → nullopt):
//   1. |PM_mid - p_yes_fair| > 0.05
//   2. PM book liquidity (top-3 levels) >= $2K
//   3. T_kickoff - now < 6h OR game.live==true
//   4. slippage.fill_rate >= 0.50
//   5. LiveSection ∈ {Live, Soon}
//
// 下注 size (单位 USDC, clip $5K, 整 cent):
//   edge        = |PM_mid - p_yes_fair|
//   kelly_full  = edge / (1 - p_yes_fair) / (PM_mid · (1 - PM_mid))
//   size        = min(0.25 · kelly_full · fill_rate · bankroll, $5K)
//
// SignalOutput:
//   side       = (PM_mid < p_yes_fair) ? BuyYes : BuyNo
//   edge_bps   = round(edge · 10000)
//   confidence = clamp(edge / 0.10, 0, 1)

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include "stcpp/strategy/live_section_classifier.hpp"
#include "stcpp/strategy/signal_iface.hpp"

// 前向声明 (SlippageModel / RM / Goalserve client 实接口 W5 接, 当前 header 不依赖)
namespace stcpp::numerical {
class SlippageModel;
}
namespace stcpp::risk {
class RiskGateway;
}

namespace stcpp::strategy {

// no-vig 公式数值结果 (导出便于单测)
struct NoVigResult {
    double p_yes_raw{0.0};
    double p_no_raw{0.0};
    double overround{0.0};  // p_yes_raw + p_no_raw, 通常 1.02-1.05
    double p_yes_fair{0.0};
    bool valid{false};  // decimal_yes/no > 1 + finite 检查通过
};

// multiplicative no-vig (小程 spec v0.1 §公式).
// decimal_yes / decimal_no <= 1.0 / NaN / Inf → valid=false (调用方走 nullopt).
[[nodiscard]] NoVigResult compute_no_vig(double decimal_yes, double decimal_no) noexcept;

// Pinnacle 报价 mock (W5 接老彭 CSV / WS).
// market_id -> (decimal_yes, decimal_no, snapshot_ts_ns).
// 缺 market_id → 返 false (调用方 nullopt).
struct PinnacleQuote {
    double decimal_yes{0.0};
    double decimal_no{0.0};
    std::int64_t snapshot_ts_ns{0};
};

class IPinnacleSource {
public:
    IPinnacleSource() = default;
    IPinnacleSource(IPinnacleSource const&) = delete;
    IPinnacleSource(IPinnacleSource&&) noexcept = delete;
    IPinnacleSource& operator=(IPinnacleSource const&) = delete;
    IPinnacleSource& operator=(IPinnacleSource&&) noexcept = delete;
    virtual ~IPinnacleSource() = default;

    [[nodiscard]] virtual bool lookup(std::string const& market_id, PinnacleQuote& out) const noexcept = 0;
};

// In-memory mock (W3 单测 + W4 paper run; W5 切真 CSV / WS)
class MockPinnacleSource : public IPinnacleSource {
public:
    void put(std::string market_id, PinnacleQuote q);
    [[nodiscard]] bool lookup(std::string const& market_id, PinnacleQuote& out) const noexcept override;
    [[nodiscard]] std::size_t size() const noexcept { return book_.size(); }

private:
    std::unordered_map<std::string, PinnacleQuote> book_;
};

// Polymarket 端 snapshot (W5 接小袁 BookSnapshotProvider, 当前 POD).
struct PmSnapshot {
    double mid{0.0};                  // mid 价 [0, 1]
    double top3_liquidity_usdc{0.0};  // top-3 levels 总 USDC
    double expected_fill_rate{0.0};   // 来自 SlippageModel, ∈ [0, 1]
    bool valid{false};
};

class IPmSnapshotSource {
public:
    IPmSnapshotSource() = default;
    IPmSnapshotSource(IPmSnapshotSource const&) = delete;
    IPmSnapshotSource(IPmSnapshotSource&&) noexcept = delete;
    IPmSnapshotSource& operator=(IPmSnapshotSource const&) = delete;
    IPmSnapshotSource& operator=(IPmSnapshotSource&&) noexcept = delete;
    virtual ~IPmSnapshotSource() = default;

    [[nodiscard]] virtual bool lookup(std::string const& market_id, PmSnapshot& out) const noexcept = 0;
};

class MockPmSnapshotSource : public IPmSnapshotSource {
public:
    void put(std::string market_id, PmSnapshot s);
    [[nodiscard]] bool lookup(std::string const& market_id, PmSnapshot& out) const noexcept override;

private:
    std::unordered_map<std::string, PmSnapshot> book_;
};

// GameState 来源 (W5 接 Goalserve client; 当前 POD).
class IGameStateSource {
public:
    IGameStateSource() = default;
    IGameStateSource(IGameStateSource const&) = delete;
    IGameStateSource(IGameStateSource&&) noexcept = delete;
    IGameStateSource& operator=(IGameStateSource const&) = delete;
    IGameStateSource& operator=(IGameStateSource&&) noexcept = delete;
    virtual ~IGameStateSource() = default;

    [[nodiscard]] virtual bool lookup(std::string const& market_id, GameState& out) const noexcept = 0;
};

class MockGameStateSource : public IGameStateSource {
public:
    void put(std::string market_id, GameState g);
    [[nodiscard]] bool lookup(std::string const& market_id, GameState& out) const noexcept override;

private:
    std::unordered_map<std::string, GameState> book_;
};

// === 常量 (小程 spec v0.1) ===
inline constexpr double EDGE_THRESHOLD = 0.05;  // 5¢
inline constexpr double MIN_TOP3_LIQUIDITY_USDC = 2'000.0;
inline constexpr double MIN_FILL_RATE = 0.50;
inline constexpr double KELLY_FRACTION = 0.25;
inline constexpr std::int64_t MAX_SIZE_USDC = 5'000;  // 整 USDC
inline constexpr double CONFIDENCE_NORMALIZER = 0.10;
inline constexpr double MIN_DECIMAL_ODDS = 1.0 + 1e-9;
inline constexpr double PM_MID_EPS = 1e-6;

class PinnacleNoVigSignal final : public ISignalEngine {
public:
    // bankroll_usdc by ref (atomic snapshot W5 接老韩 RM, 当前直传 i64).
    PinnacleNoVigSignal(IPinnacleSource const& pinnacle, IPmSnapshotSource const& pm,
                        IGameStateSource const& games, std::int64_t bankroll_usdc) noexcept;

    [[nodiscard]] std::optional<SignalOutput> tick(SignalContext const& ctx) noexcept override;

    [[nodiscard]] SignalId id() const noexcept override { return SignalId::P0_01_PinnacleNoVig; }

    // setter (paper run 期间 bankroll 由 RM 注入; 单测 deterministic)
    void set_bankroll(std::int64_t usdc) noexcept { bankroll_usdc_ = usdc; }

private:
    [[nodiscard]] bool validate_context_(SignalContext const& ctx) const noexcept;

    IPinnacleSource const& pinnacle_;
    IPmSnapshotSource const& pm_;
    IGameStateSource const& games_;
    std::int64_t bankroll_usdc_;
};

}  // namespace stcpp::strategy
