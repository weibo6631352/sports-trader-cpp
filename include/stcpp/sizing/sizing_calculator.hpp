// stcpp/sizing/sizing_calculator.hpp — SizingCalculator v0.1
//
// Owner: 小袁 (quant-microstructure, C 量化研究部)
// last_review: 2026-05-29
//
// 数学口径 SSOT:
//   docs/RESEARCH/xiaoliang-kelly-sizing-spec-v1.md  (小梁, Kelly/cap 草案)
//   docs/RESEARCH/laohan-kelly-cap-cosign-v1.md      (老韩 RM 联签, 4 修正条款)
//
// 关键设计决策 (老韩裁定, 不可绕过):
//   1. CI gating: sizing gating 门 = RM gating 门 (同读 edge_ci_lower, 同 floor, 同 fee 公式)
//   2. fee / slippage 两道独立门 (不在同式相减, 与 RM check_signal_ 同源)
//   3. f* 分子用 net_ci_edge (= edge_ci_lower − fee_per_unit), 不用点估计
//   4. cap 注入 RiskConfig const& — 禁复制字面量 (老高 CI grep 守护)
//   5. bankroll 读 SizingInput.bankroll_usdc (运行期值, 防 drawdown 漂移)
//   6. MAX_BANKROLL_FRACTION = 0.10 (sizing-only 护栏, RM 无对应, follow-up §6 评审进 RiskConfig)
//
// 红线:
//   R-1  sizing 绝不绕 RM; suggested_notional 仅建议值, 必经 RiskGateway::evaluate 放行
//   R-7  ExecutionMode-agnostic (paper/live/backtest 共用同一公式)
//   R-11 纯函数, 不写 ledger / 不调 audit
//   fail-closed: valid=false → 全 0 输出, 不下单
//
// 5-cap 链 (取最小, 记录 capped_by):
//   C1: PER_ORDER     = RiskConfig.per_order_cap_usdc          (default 10,000)
//   C2: PER_OUTCOME   = RiskConfig.per_outcome_cap_usdc        (default 25,000)
//   C3: CONDITION     = RiskConfig.market_exposure_cap_usdc    (default 50,000)
//   C4: BANKROLL_FRAC = SizingInput.bankroll_usdc × 0.10      (sizing-only)
//   C5: FILL_RATE_FLOOR: fill_rate < 0.50 → advisory 置 0    (slippage_model.hpp L72)
//
// ============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "stcpp/numerical/slippage_model.hpp"  // FILL_RATE_FLOOR (0.50)
#include "stcpp/risk/risk_gateway.hpp"         // RiskConfig (cap 注入源)

namespace stcpp::sizing {

// ---------------------------------------------------------------------------
// 1. CappedBy 枚举 (与小梁 spec §2 / 老韩联签 §5.1 对齐)
// ---------------------------------------------------------------------------

enum class CappedBy : std::uint8_t {
    NONE = 0,                // Kelly 原值未触任何 cap
    NO_EDGE = 1,             // net_raw_edge <= 0 (CI gating / fee 门 / slippage 门)
    PER_ORDER_CAP = 2,       // 触 per-order 单注上限 (RiskConfig.per_order_cap_usdc)
    PER_OUTCOME_CAP = 3,     // 触 per-token 敞口上限 (RiskConfig.per_outcome_cap_usdc)
    CONDITION_EXPOSURE = 4,  // 触 per-condition 敞口上限 (RiskConfig.market_exposure_cap_usdc)
    BANKROLL_FRACTION = 5,   // 触 bankroll 比例上限 (sizing-only 0.10 护栏)
    FILL_RATE_FLOOR = 6,     // fill_rate < FILL_RATE_FLOOR (0.50) — advisory 置 0
};

// ---------------------------------------------------------------------------
// 2. SizingInput — 调用方填入 (R-20 时间戳由调用方携带, 本 lib 不验 ts)
// ---------------------------------------------------------------------------

struct SizingInput {
    // 定价
    double fair_value{0.0};  // p ∈ (kProbEps, kProbMax), 来自 FairValueEstimator
    double price{0.0};       // c ∈ (0,1), 入场价 (买方 ask / 卖方 bid)

    // CI 下界 (老韩裁定: gating 门用 CI 下界, 与 RM 同步保守)
    // 单位: 概率差 ∈ (−1, 1). 例: 0.05 = 500 bps edge CI 下界
    double edge_ci_lower{0.0};

    // 毛 edge bps (展示用, 不进 gating)
    double edge_bps{0.0};  // = |p − c| × 10000

    // 资金 (运行期值, 来自仓位账本 / RM 同源快照; 禁用 RiskConfig 静态默认值)
    double bankroll_usdc{0.0};  // > 0; 防 drawdown 漂移 (老韩 §1.3)

    // fill_rate / slippage (来自 FillRateModel / SlippageModel; 调用方已算)
    double fill_rate{1.0};     // ∈ [0,1], 来自 FillRateModel
    double slippage_bps{0.0};  // 来自 SlippageModel (门 A: edge_ci_lower < slippage)

    // 手续费系数 (per-market, gamma feeSchedule.rate; 门 B: net_ci = edge − rate×p×(1-p))。
    //   默认 0.03 = 体育保守 (= RM 同源); 调用方填真值。与 RM check_signal_ 同源 (必须同值)。
    double fee_rate_coef{0.03};

    // 当前敞口 (cap 2/3 headroom 计算用; 单位 USDC 与 RiskConfig cap 同)
    double current_token_exposure_usdc{0.0};      // 当前 token 累计敞口
    double current_condition_exposure_usdc{0.0};  // 当前 condition 累计敞口

    // 方向 (true = buy YES / p > c; false = buy NO / p < c)
    // 调用方按 fair_value vs price 符号设置, SizingCalculator 仅用 edge_ci_lower 绝对值算幅度
    bool buy_yes{true};

    // v1 留接口 (不乘, 参见小梁 §1.3 v2 候选)
    double model_conf{1.0};  // ∈ [0,1], v1 不乘 λ (接口预留)

    // no_edge_gate (老板 2026-06-03「把门都去了, 虚拟盘专门调模型」): 跳过 Step1/2/3 edge 门
    //   (CI floor / slippage / fee 门), 让模型/sharp 任意正净 edge 都成交 → 全反馈供调模型。
    //   仍保 Step0 输入校验 (NaN/边界) + Step5 (kelly>0 = net_ci_edge>0, 不在保证亏的 edge 上交易)。
    //   默认 false: 实盘/RM/契约测试路径不受影响 (仅 paper daemon 显式置 true)。
    bool no_edge_gate{false};
};

// ---------------------------------------------------------------------------
// 3. SizingOutput — 纯值输出 (fail-closed: valid=false 时全 0)
// ---------------------------------------------------------------------------

struct SizingOutput {
    // Kelly 比例 (∈ [0,1], 无方向符号; 方向在调用方按 SizingInput.buy_yes 决定)
    double kelly_full{0.0};        // f*_full = net_ci_edge / denom (CI 下界净 Kelly)
    double kelly_fractional{0.0};  // λ × f*_full, λ = 0.25 (quarter Kelly)

    // 名义额 (USDC)
    double suggested_notional{0.0};  // 过完 5 cap 的意图名义额 — /api/v1/quote 真值来源
    double effective_notional{0.0};  // suggested_notional × fill_rate (预期实际成交, 展示用)

    // cap 记录
    CappedBy capped_by{CappedBy::NONE};

    // net_ci_edge = edge_ci_lower − fee_per_unit (debug / audit / RM 一致性单测)
    // 与 RM check_signal_ net_edge_after_fee 同源 (±1e-9 容差)
    double net_ci_edge{0.0};

    // valid=false: 输入含 NaN/Inf / fair_value/price/edge_ci_lower 越界 → fail-closed, 全 0
    bool valid{false};

    // 输入快照回填 (audit; R-20 时间戳由调用方传入并透传到此)
    SizingInput snapshot{};
};

// ---------------------------------------------------------------------------
// 4. SizingCalculator — 纯静态, noexcept, 无堆分配, mode-agnostic (R-7)
// ---------------------------------------------------------------------------

// sizing-only 护栏 (RM 无对应字段; 本期不进 RiskConfig — 老韩联签 §1.4)
// follow-up: W6 校准后随 λ_eff × model_conf 一并评审是否入 RM
inline constexpr double kMaxBankrollFraction = 0.10;  // 单注 ≤ 10% bankroll

// λ_base (老板 2026-06-01 直接定 0.35; 原 0.25 quarter Kelly)。paper 灰度: 直接跑看真实 maxDD
//   (paper 非真钱, 红线只卡真钱开闸)。真钱开闸前需拿 paper 实测 maxDD≤15% 找老韩 RM 联签。
inline constexpr double kLambdaBase = 0.35;

// fee 率同源常量 (与 RM risk_gateway.cpp kSportsTakerFeeRate 同值; 防自定义漂移)
// 若此常量与 RM 同 TU 共享则引用同一 header (老周/老沈 跨 TU 提公共 header 时更新)
// 目前 risk_gateway.cpp 内 file-scope, 此处同值声明; 老高 CI grep 守护不允许不同值出现
inline constexpr double kSportsTakerFeeRate = 0.03;  // taker fee 3%, 与 RM 同源

class SizingCalculator {
public:
    // 主入口 (noexcept, fail-closed)
    // cfg: 注入 RiskConfig const& — cap 数值从 cfg 读, 禁复制字面量 (老韩 §3 强制)
    // in:  SizingInput (调用方填; 见 §2)
    [[nodiscard]] static SizingOutput compute(risk::RiskConfig const& cfg, SizingInput const& in) noexcept;

    // 净 edge 计算 helper (暴露给单测 / RM 一致性验证)
    // net_ci_edge = edge_ci_lower − fee_coef × p × (1 − p)
    // fee_coef: per-market (gamma feeSchedule.rate; 默认 kSportsTakerFeeRate 保留旧行为)。
    //   钳 [0,0.10] 与 RM check_signal_ 同源 (官方禁硬编码 docs.polymarket)。
    [[nodiscard]] static double compute_net_ci_edge(double edge_ci_lower, double p,
                                                    double fee_coef = kSportsTakerFeeRate) noexcept {
        if (!std::isfinite(edge_ci_lower) || !std::isfinite(p) || !std::isfinite(fee_coef)) {
            return 0.0;
        }
        double const coef = std::clamp(fee_coef, 0.0, 0.10);
        double const fee_per_unit = coef * p * (1.0 - p);
        return edge_ci_lower - fee_per_unit;
    }

private:
    // 数值合法性校验 (fail-closed)
    [[nodiscard]] static bool is_valid_input_(SizingInput const& in) noexcept;

    // Kelly 分母 (buy YES: 1-c; buy NO: c)
    [[nodiscard]] static double kelly_denom_(double c, bool buy_yes) noexcept {
        return buy_yes ? (1.0 - c) : c;
    }
};

}  // namespace stcpp::sizing
