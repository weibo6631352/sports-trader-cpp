// stcpp/strategy/p0_01_goalserve_devig.hpp — P0-01 Goalserve multiplicative de-vig 信号 v0.1
//
// ADR-008 multiplicative de-vig — 替换 W4 Pinnacle 锚源, 改用 Goalserve 8-9 家 bookmaker.
// 小梁 GM 错 #9 ack + W6 Wave 28 重写 (小卢 IC pool E-035-02).
//
// 公式 (ADR-008):
//   单家 bookmaker i:
//     p_yes_raw_i  = 1 / odds_yes_i
//     p_no_raw_i   = 1 / odds_no_i
//     overround_i  = p_yes_raw_i + p_no_raw_i
//     p_yes_fair_i = p_yes_raw_i / overround_i       (multiplicative de-vig)
//   跨 N 家等权均值 (N >= 3):
//     p_yes_fair_avg = mean(p_yes_fair_i for i in 0..N-1)
//   最终 fair_value = p_yes_fair_avg
//
// 9 家 bookmaker (小段 v3 §7 ETL-12):
//   10Bet/14, WilliamHill/15, bet365/16, Marathon/17, Unibet/18,
//   BetVictor/65, 1xBet/105, Betano/144, 另 1 家 TBD (老彭 W6 EOW 实证)
//
// 5 触发条件 (AND, 任一不满足 → nullopt, 与 W4 P0-01 相同):
//   1. |PM_mid - p_yes_fair_avg| > 0.05  (5¢ 阈值)
//   2. PM book liquidity (top-3 levels) >= $2K
//   3. T_kickoff - now < 6h OR game.live==true
//   4. slippage.fill_rate >= 0.50
//   5. LiveSection ∈ {Live, Soon}
//
// 下注 size (单位 USDC, clip $5K, 整 cent):
//   edge        = |PM_mid - p_yes_fair_avg|
//   kelly_full  = edge / (1 - p_yes_fair_avg) / (PM_mid · (1 - PM_mid))
//   size        = min(0.25 · kelly_full · fill_rate · bankroll, $5K)
//
// SignalOutput:
//   side       = (PM_mid < p_yes_fair_avg) ? BuyYes : BuyNo
//   edge_bps   = round(edge · 10000)
//   confidence = clamp(edge / 0.10, 0, 1)
//
// 红线:
//   R-20   SignalContext 4 ts + feature_snapshot_id 必带, ts<=0 → nullopt
//   ML-R5  rule-based, 不调 ML
//   ADR-008 multiplicative only, 不上 Shin
//   老周 ABI lock: SignalContext / SignalOutput struct 不动
//   小邓 ML hook: feature_snapshot_id 不动
//   ML-R6 (ADR-014): W6 paper 数据收集, M2 (8/6) 后 shadow inference
//
// 共享类型: PmSnapshot / IPmSnapshotSource / MockPmSnapshotSource /
//           IGameStateSource / MockGameStateSource / 触发条件常量
//   → 来自 p0_01_pinnacle_no_vig.hpp (避免重复符号, 老周 ABI lock).

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

// W4 共享接口引入: PmSnapshot, IPmSnapshotSource, MockPmSnapshotSource,
//                  IGameStateSource, MockGameStateSource, GameState,
//                  触发条件常量 (EDGE_THRESHOLD / MIN_TOP3_LIQUIDITY_USDC 等).
#include "stcpp/strategy/live_section_classifier.hpp"
#include "stcpp/strategy/p0_01_pinnacle_no_vig.hpp"
#include "stcpp/strategy/signal_iface.hpp"

namespace stcpp::strategy {

// ---------------------------------------------------------------------------
// BookmakerOdds — 单家 bookmaker yes/no 十进制赔率 (mock + 真实来源通用)
//
// 字段与 OddsRecord.value 对齐: odds_yes = outcome=="home"|"over",
//                               odds_no  = outcome=="away"|"under".
// 老彭 W6 EOW 真实 fixture 接入后替换 mock; 接口不变.
// ---------------------------------------------------------------------------
struct BookmakerOdds {
    std::int32_t bookmaker_id{0};    // 小段 ETL-12: 14/15/16/17/18/65/105/144
    double       odds_yes{0.0};      // decimal odds YES (home / over)
    double       odds_no{0.0};       // decimal odds NO  (away / under)
    std::int64_t snapshot_ts_ns{0};  // R-20 data_source_ts (来自 bookmaker @ts)
};

// ---------------------------------------------------------------------------
// DevigResult — multiplicative de-vig 数值结果 (导出便于单测)
//
// 对应 ADR-008 公式完整中间值:
//   books_used     : 实际参与均值计算的 bookmaker 数 (≥ 3 才有效)
//   overround_avg  : 跨 books 平均 overround (ML feature: Goalserve_overround_avg)
//   p_yes_fair_avg : 最终 fair value (ML feature: Goalserve_devig_p_yes_fair)
//   valid          : books_used >= 3 且 p_yes_fair_avg ∈ (0, 1)
// ---------------------------------------------------------------------------
struct DevigResult {
    std::size_t books_used{0};
    double      overround_avg{0.0};
    double      p_yes_fair_avg{0.0};
    bool        valid{false};
};

// ---------------------------------------------------------------------------
// ADR-008 新增常量 (W4 触发条件常量沿用 p0_01_pinnacle_no_vig.hpp, 不重复声明)
// ---------------------------------------------------------------------------
inline constexpr std::size_t MIN_BOOKMAKERS = 3;  // < 3 家 → fallback valid=false (小梁 P1 基线)

// ---------------------------------------------------------------------------
// compute_multiplicative_devig — ADR-008 核心算法 (~30 行)
//
// 入参: span<const BookmakerOdds>, 跳过 odds_yes/no <= 0 或 overround <= 0 的行.
// 返回: DevigResult (valid=false 时 p_yes_fair_avg=0).
// 最少 MIN_BOOKMAKERS (3) 家有效 bookmaker 才计算均值.
// noexcept: 无堆分配, 纯 FP 运算.
// ---------------------------------------------------------------------------
[[nodiscard]] DevigResult compute_multiplicative_devig(
    std::span<const BookmakerOdds> bookmakers) noexcept;

// ---------------------------------------------------------------------------
// IGoalserveOddsSource — Goalserve 报价抽象 (W6 接老彭 CSV, 未来切 WSS)
//
// market_id → 多家 bookmaker 报价列表 (BookmakerOdds[]).
// 实现: 从 OddsRecord[] 按 market_id + "home"/"away" outcome 聚合.
// ---------------------------------------------------------------------------
class IGoalserveOddsSource {
 public:
    IGoalserveOddsSource()                                             = default;
    IGoalserveOddsSource(IGoalserveOddsSource const&)                  = delete;
    IGoalserveOddsSource(IGoalserveOddsSource&&) noexcept              = delete;
    IGoalserveOddsSource& operator=(IGoalserveOddsSource const&)       = delete;
    IGoalserveOddsSource& operator=(IGoalserveOddsSource&&) noexcept   = delete;
    virtual ~IGoalserveOddsSource()                                    = default;

    // 查询 market_id 对应的所有 bookmaker 报价.
    // 返回 false: market 不存在或无有效报价 (调用方走 nullopt).
    [[nodiscard]] virtual bool lookup(
        std::string const& market_id,
        std::vector<BookmakerOdds>& out) const noexcept = 0;
};

// In-memory mock (单测 + W6 paper run; 老彭 CSV W6 EOW 接入后切真源)
class MockGoalserveOddsSource : public IGoalserveOddsSource {
 public:
    void put(std::string market_id, std::vector<BookmakerOdds> odds);

    [[nodiscard]] bool lookup(
        std::string const& market_id,
        std::vector<BookmakerOdds>& out) const noexcept override;

    [[nodiscard]] std::size_t size() const noexcept { return book_.size(); }

 private:
    std::unordered_map<std::string, std::vector<BookmakerOdds>> book_;
};

// ---------------------------------------------------------------------------
// GoalserveDevigSignal — P0-01 信号 v0.1 (ADR-008 multiplicative de-vig)
//
// 改名: PinnacleNoVigSignal → GoalserveDevigSignal
// 算法: Pinnacle single-source no-vig → Goalserve 8-9 家 avg multiplicative de-vig
// SignalContext / SignalOutput: 不动 (老周 ABI lock)
// feature_snapshot_id: 不动 (小邓 ML hook ABI key)
// 共享接口: IPmSnapshotSource / IGameStateSource 来自 p0_01_pinnacle_no_vig.hpp
// ---------------------------------------------------------------------------
class GoalserveDevigSignal final : public ISignalEngine {
 public:
    GoalserveDevigSignal(IGoalserveOddsSource const& goalserve,
                         IPmSnapshotSource const& pm,
                         IGameStateSource const& games,
                         std::int64_t bankroll_usdc) noexcept;

    [[nodiscard]] std::optional<SignalOutput> tick(SignalContext const& ctx) noexcept override;

    [[nodiscard]] SignalId id() const noexcept override {
        return SignalId::P0_01_PinnacleNoVig;  // ABI 不动 (老周 lock)
    }

    // paper run 期间 bankroll 由 RM 注入; 单测 deterministic
    void set_bankroll(std::int64_t usdc) noexcept { bankroll_usdc_ = usdc; }

 private:
    [[nodiscard]] bool validate_context_(SignalContext const& ctx) const noexcept;

    IGoalserveOddsSource const& goalserve_;
    IPmSnapshotSource const&    pm_;
    IGameStateSource const&     games_;
    std::int64_t                bankroll_usdc_;
};

}  // namespace stcpp::strategy
