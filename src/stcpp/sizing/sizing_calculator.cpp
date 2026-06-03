// stcpp/sizing/sizing_calculator.cpp — SizingCalculator v0.1 实现
//
// Owner: 小袁 (quant-microstructure)
// last_review: 2026-05-29
//
// SSOT:
//   docs/RESEARCH/xiaoliang-kelly-sizing-spec-v1.md
//   docs/RESEARCH/laohan-kelly-cap-cosign-v1.md (4 修正条款)
//
// 老韩 §2.3 两道独立门 (SSOT = risk_gateway.cpp check_signal_):
//   门 A (slippage): edge_ci_lower_bps < slippage_bps → NO_EDGE
//   门 B (fee):      edge_ci_lower − 0.03·p·(1−p) <= floor → NO_EDGE
//
// f* 分子用 net_ci_edge (= edge_ci_lower − fee_per_unit, CI 下界净 edge)
// 不用点估计 — 与 RM 放行口径完全同源 (老韩 §2.2 裁定)
//
// cap 链 (5 级, 取最小):
//   C1: PER_ORDER_CAP        = cfg.per_order_cap_usdc (引用, 不复制字面量)
//   C2: PER_OUTCOME_CAP      = cfg.per_outcome_cap_usdc
//   C3: CONDITION_EXPOSURE   = cfg.market_exposure_cap_usdc
//   C4: BANKROLL_FRACTION    = in.bankroll_usdc × kMaxBankrollFraction (运行期)
//   C5: FILL_RATE_FLOOR      = numerical::FILL_RATE_FLOOR (slippage_model.hpp L72)

#include "stcpp/sizing/sizing_calculator.hpp"

#include <algorithm>
#include <cmath>

namespace stcpp::sizing {

namespace {

// 辅助: 输入合法性 (NaN/Inf / 边界检查)
[[nodiscard]] bool validate_input(SizingInput const& in) noexcept {
    // NaN / Inf 防护
    if (!std::isfinite(in.fair_value) || !std::isfinite(in.price) || !std::isfinite(in.edge_ci_lower) ||
        !std::isfinite(in.bankroll_usdc) || !std::isfinite(in.fill_rate) || !std::isfinite(in.slippage_bps) ||
        !std::isfinite(in.current_token_exposure_usdc) ||
        !std::isfinite(in.current_condition_exposure_usdc)) {
        return false;
    }
    // fair_value ∈ (0, 1) (kProbEps ~ 1e-6 < p < 1-kProbEps; 宽松用 1e-9 边界)
    static constexpr double kProbBound = 1e-9;
    if (in.fair_value <= kProbBound || in.fair_value >= 1.0 - kProbBound) {
        return false;
    }
    // price ∈ (0, 1)
    if (in.price <= kProbBound || in.price >= 1.0 - kProbBound) {
        return false;
    }
    // bankroll > 0
    if (in.bankroll_usdc <= 0.0) {
        return false;
    }
    // fill_rate ∈ [0, 1]
    if (in.fill_rate < 0.0 || in.fill_rate > 1.0) {
        return false;
    }
    // slippage_bps >= 0
    if (in.slippage_bps < 0.0) {
        return false;
    }
    return true;
}

// 构造全 0 fail-closed 输出 (valid=false; capped_by 由调用方设)
[[nodiscard]] SizingOutput make_fail(SizingInput const& in, CappedBy cap = CappedBy::NO_EDGE) noexcept {
    SizingOutput out;
    out.valid = false;
    out.capped_by = cap;
    out.snapshot = in;
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// is_valid_input_ (static, 公开给 compute)
// ---------------------------------------------------------------------------

bool SizingCalculator::is_valid_input_(SizingInput const& in) noexcept {
    return validate_input(in);
}

// ---------------------------------------------------------------------------
// compute — 主入口 (noexcept, fail-closed)
// ---------------------------------------------------------------------------

SizingOutput SizingCalculator::compute(risk::RiskConfig const& cfg, SizingInput const& in) noexcept {
    // ------------------------------------------------------------------
    // Step 0: 输入校验 (fail-closed)
    // ------------------------------------------------------------------
    if (!is_valid_input_(in)) {
        return make_fail(in, CappedBy::NO_EDGE);
    }

    // P0-6 (老韩 canonical p=price 裁定): fee 用入场价 c, 非 fair_value。fair_value 在 sizing 内
    //   无其他消费点 (edge 走 edge_ci_lower, Kelly 分子走 net_ci_edge, 分母走 c), 故不再取局部 p。
    double const c = in.price;  // 入场价 (CI gating fee 门 + Kelly 分母均用 c)

    // no_edge_gate (老板 2026-06-03「把门都去了, 虚拟盘调模型」): 跳过 Step1/2/3 edge 门。
    //   仅 paper daemon 显式置 true; 实盘/RM/契约测试 = false 不受影响。
    double const floor = cfg.edge_ci_lower_floor;
    if (!in.no_edge_gate) {
        // ------------------------------------------------------------------
        // Step 1: CI gating 门 (老韩 §2.1 裁定 — 与 RM check_signal_ L564 同源)
        //   edge_ci_lower <= edge_ci_lower_floor → NO_EDGE
        // ------------------------------------------------------------------
        if (in.edge_ci_lower <= floor) {
            return make_fail(in, CappedBy::NO_EDGE);
        }

        // ------------------------------------------------------------------
        // Step 2: 门 A (slippage, 老韩 §2.3 — RM check_signal_ L571 同源)
        //   edge_ci_lower(bps) < slippage_bps → NO_EDGE
        // ------------------------------------------------------------------
        double const edge_ci_lower_bps = in.edge_ci_lower * 10'000.0;
        if (edge_ci_lower_bps < in.slippage_bps) {
            return make_fail(in, CappedBy::NO_EDGE);
        }
    }

    // ------------------------------------------------------------------
    // Step 3: 门 B (fee, 老韩 §2.3 — RM check_signal_ L587 同源)
    //   net_ci_edge = edge_ci_lower − fee_per_unit
    //   fee_per_unit = kSportsTakerFeeRate × c × (1−c)  ← P0-6: 用入场价 c (= RM canonical),
    //     原用 fair_value 与 RM 不一致, 极小 edge 下方向分歧 (37/9863, 老韩裁定 fee 锚 price)
    //   net_ci_edge <= floor → NO_EDGE
    // ------------------------------------------------------------------
    double const net_ci_edge = compute_net_ci_edge(in.edge_ci_lower, c, in.fee_rate_coef);
    if (!in.no_edge_gate && net_ci_edge <= floor) {
        return make_fail(in, CappedBy::NO_EDGE);
    }
    // no_edge_gate 仍要 net_ci_edge>0 (否则 Step5 kelly<=0 → NO_EDGE): 不在保证亏 (edge<fee) 的盘交易。

    // ------------------------------------------------------------------
    // Step 4: FILL_RATE_FLOOR advisory (门 C, 老韩联签 §1 FILL_RATE_FLOOR=0.50)
    //   fill_rate < FILL_RATE_FLOOR → capped_by=FILL_RATE_FLOOR, advisory 置 0
    //   注: RM check_liquidity_ L543 LOW_FILL_RATE 也会拒; sizing 比 RM 先置 0 = 更保守
    // ------------------------------------------------------------------
    if (in.fill_rate < numerical::FILL_RATE_FLOOR) {
        SizingOutput out = make_fail(in, CappedBy::FILL_RATE_FLOOR);
        // fill_rate_floor 触发: 提供 net_ci_edge debug 信息 (capped_by 已设)
        out.net_ci_edge = net_ci_edge;
        return out;
    }

    // ------------------------------------------------------------------
    // Step 5: f* 计算 (用 net_ci_edge 作分子 — 老韩 §2.2)
    //   buy YES: f*_full = net_ci_edge / (1 − c)
    //   buy NO:  f*_full = net_ci_edge / c
    // ------------------------------------------------------------------
    double const denom = kelly_denom_(c, in.buy_yes);
    // denom > 0: c ∈ (kProbBound, 1-kProbBound) 已由 validate_input 保证
    double const kelly_full = net_ci_edge / denom;

    // kelly_full 应 > 0 (net_ci_edge > 0, denom > 0); 防极端浮点误差
    if (kelly_full <= 0.0 || !std::isfinite(kelly_full)) {
        return make_fail(in, CappedBy::NO_EDGE);
    }

    // clamp [0, 1] (理论上 f* ≤ 1 for valid binary market; 超出则 cap)
    double const kelly_full_clamped = (kelly_full > 1.0) ? 1.0 : kelly_full;

    // ------------------------------------------------------------------
    // Step 6: Fractional Kelly λ = 0.25 (quarter Kelly; 小梁 §1.3)
    // ------------------------------------------------------------------
    double const kelly_fractional = kLambdaBase * kelly_full_clamped;

    // ------------------------------------------------------------------
    // Step 7: 意图名义额 (未 cap)
    // ------------------------------------------------------------------
    double const notional_kelly = kelly_fractional * in.bankroll_usdc;

    // ------------------------------------------------------------------
    // Step 8: 5 cap 链 (取最小, 记录 capped_by — 老韩 §1 + 小梁 §1.5)
    // ------------------------------------------------------------------

    // Cap 1: PER_ORDER — RiskConfig.per_order_cap_usdc (引用值, 非字面量)
    // c3 (P0-2 根治): cap 为 MicroPUSD(micro); notional_kelly 是 whole pUSD, 故 cap 用 .to_pusd()
    //   (÷1e6) 转 whole 同量纲比。终结「micro raw 当 whole 比」第二现场失配 (小袁核实)。
    double const cap1_limit = cfg.per_order_cap_usdc.to_pusd();
    double notional_c1 = notional_kelly;
    CappedBy capped = CappedBy::NONE;

    if (notional_c1 > cap1_limit) {
        notional_c1 = cap1_limit;
        capped = CappedBy::PER_ORDER_CAP;
    }

    // Cap 2: PER_OUTCOME — RiskConfig.per_outcome_cap_usdc
    // headroom = max(0, per_outcome_cap − current_token_exposure)
    {
        double const cap2_limit = cfg.per_outcome_cap_usdc.to_pusd();  // c3: micro→whole 同量纲
        double const headroom2 = cap2_limit - in.current_token_exposure_usdc;
        double const effective2 = (headroom2 > 0.0) ? headroom2 : 0.0;
        if (notional_c1 > effective2) {
            notional_c1 = effective2;
            capped = CappedBy::PER_OUTCOME_CAP;
        }
    }

    // Cap 3: CONDITION_EXPOSURE — RiskConfig.market_exposure_cap_usdc
    {
        double const cap3_limit = cfg.market_exposure_cap_usdc.to_pusd();  // c3: micro→whole 同量纲
        double const headroom3 = cap3_limit - in.current_condition_exposure_usdc;
        double const effective3 = (headroom3 > 0.0) ? headroom3 : 0.0;
        if (notional_c1 > effective3) {
            notional_c1 = effective3;
            capped = CappedBy::CONDITION_EXPOSURE;
        }
    }

    // Cap 4: BANKROLL_FRACTION (sizing-only 护栏; RM 无对应; 老韩联签 §1.4 允许更严)
    // 基数用 in.bankroll_usdc (运行期, 防 drawdown 漂移; 老韩 §1.3)
    {
        double const cap4_limit = in.bankroll_usdc * kMaxBankrollFraction;
        if (notional_c1 > cap4_limit) {
            notional_c1 = cap4_limit;
            capped = CappedBy::BANKROLL_FRACTION;
        }
    }

    // 最终 suggested_notional (意图名义额; 不得为负)
    double const suggested_notional = (notional_c1 > 0.0) ? notional_c1 : 0.0;

    // ------------------------------------------------------------------
    // Step 9: effective_notional = suggested × fill_rate (展示用)
    //   fill_rate 仅折扣展示, 不改变 cap 判定 (小梁 §1.4)
    // ------------------------------------------------------------------
    double const effective_notional = suggested_notional * in.fill_rate;

    // ------------------------------------------------------------------
    // Step 10: 组装输出
    // ------------------------------------------------------------------
    SizingOutput out;
    out.kelly_full = kelly_full_clamped;
    out.kelly_fractional = kelly_fractional;
    out.suggested_notional = suggested_notional;
    out.effective_notional = effective_notional;
    out.capped_by = capped;
    out.net_ci_edge = net_ci_edge;
    out.valid = true;
    out.snapshot = in;

    return out;
}

}  // namespace stcpp::sizing
