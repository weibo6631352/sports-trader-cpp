// include/stcpp/risk/arb_signal.hpp — 套利信号决策核 (Phase 3; 主计划 §2.3)
//
// Owner: 老雷 (GM) — 短时套利引擎 模块4/5
// last_review: 2026-06-01
//
// 把【模型 per-horizon 预测 (模块2)】+【风控门/sizing (模块3)】拼成一个可操作 ArbSignal (纯函数, 可测)。
//   遍历 horizon → 用 CI 下界算保守净 edge (ci_low − BE; 点估计禁直接 sizing) → 过 RJ-ARB 门 → sizing →
//   按【信号质量 = 置信度 × |edge| × 可成交深度】(老板 2026-06-01) 选最优 horizon。
//   设计哲学: 决策逻辑在此 (调用方), 模型只出信号 + CI; 阈值不内嵌模型。
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "stcpp/ml/mid_return_label.hpp"  // kWallHorizonsSec/Ns
#include "stcpp/ml/seq_arb_model.hpp"     // ArbPrediction
#include "stcpp/risk/arb_risk.hpp"
#include "stcpp/risk/arb_sizing.hpp"

namespace stcpp::risk {

// 决策时的市场状态 (调用方从 book / 账户 / 延迟监控填)。
struct ArbMarketState {
    double mid{0.0};
    double best_ask{0.0};            // 买入吃单价
    double best_bid{0.0};            // 卖出吃单价
    double exit_depth_usdc{0.0};     // L1-L5 可平仓累计深度
    double fee_roundtrip{0.0};       // 往返手续费 (价格单位; = 2×fee_per_share)
    double slip_est{0.0};            // 预估滑点 (价格单位)
    double bankroll_usdc{0.0};
    double max_notional_usdc{0.0};
    double lambda{0.10};
    std::int64_t pred_gen_ts_ns{0};
    std::int64_t now_ns{0};
    std::int64_t est_rtt_ns{0};
    int open_leg_count{0};
    int max_open_legs{0};
    double horizon_exposure_usdc{0.0};
    double horizon_cap_usdc{0.0};
};

struct ArbSignal {
    bool actionable{false};
    int horizon_idx{-1};
    int horizon_sec{0};
    double predicted_dmid{0.0};
    double ci_low{0.0};
    double confidence{0.0};
    double net_edge{0.0};            // 保守净 edge (ci_low方向 − BE), 价格单位
    double entry_px{0.0};
    double target_exit_px{0.0};
    double suggested_notional{0.0};
    double signal_quality{0.0};      // 置信度 × |net_edge| × min(深度,notional) (老板)
    ArbRejectCode reject{ArbRejectCode::None};  // 无可操作时的代表性拒因
};

namespace detail {
inline constexpr double kCiZ80 = 1.2816;  // 80% CI 单边 z (σ = (ci_high−ci_low)/(2z))
}

// ComputeArbSignal — 遍历 horizon 选最优可操作信号 (纯函数)。
[[nodiscard]] inline ArbSignal ComputeArbSignal(const ml::ArbPrediction& pred,
                                                const ArbMarketState& mkt) noexcept {
    ArbSignal best;
    if (!pred.ok) {
        best.reject = ArbRejectCode::PredCiCrossesZero;  // 无预测 = 无方向
        return best;
    }
    const double be = mkt.fee_roundtrip + mkt.slip_est + (mkt.best_ask - mkt.best_bid);  // 盈亏平衡门槛
    for (std::size_t h = 0; h < ml::kArbHorizonCount; ++h) {
        const auto& hp = pred.wall[h];
        if (!std::isfinite(hp.dmid) || !std::isfinite(hp.ci_low) || !std::isfinite(hp.ci_high)) continue;
        const bool is_long = hp.dmid > 0.0;
        // 保守 move (CI 下界方向): 多头取 ci_low, 空头取 −ci_high (favorable 方向的保守幅度)。
        const double cons_move = is_long ? hp.ci_low : -hp.ci_high;
        const double net_edge = cons_move - be;  // 扣 BE 后保守净 edge
        if (!(net_edge > 0.0)) continue;          // 不够本

        const double sigma = (hp.ci_high - hp.ci_low) / (2.0 * detail::kCiZ80);
        ArbGateInput gi;
        gi.pred_dmid = hp.dmid;
        gi.ci_low = hp.ci_low;
        gi.ci_high = hp.ci_high;
        gi.exit_depth_usdc = mkt.exit_depth_usdc;
        gi.pred_gen_ts_ns = mkt.pred_gen_ts_ns;
        gi.now_ns = mkt.now_ns;
        gi.horizon_ns = ml::kWallHorizonsNs[h];
        gi.est_rtt_ns = mkt.est_rtt_ns;
        gi.open_leg_count = mkt.open_leg_count;
        gi.max_open_legs = mkt.max_open_legs;
        gi.horizon_exposure_usdc = mkt.horizon_exposure_usdc;
        gi.horizon_cap_usdc = mkt.horizon_cap_usdc;

        ArbSizingInput si;
        si.edge_arb = net_edge;
        si.sigma_dmid = sigma;
        si.bankroll_usdc = mkt.bankroll_usdc;
        si.lambda = mkt.lambda;
        si.max_notional_usdc = mkt.max_notional_usdc;
        si.exit_depth_usdc = 0.0;  // 不在 sizing 里按深度缩; 由 gate RJ-ARB-2 判"出不来"(全平 or 拒)
        const auto so = ComputeArbSizing(si);  // desired Kelly (max_notional cap); gate 需 target_notional
        if (!so.valid) continue;
        gi.target_notional_usdc = so.notional_usdc;  // gate: exit_depth < desired → ExitDepthInsufficient

        const ArbRejectCode rj = CheckArbGate(gi);
        if (rj != ArbRejectCode::None) {
            if (best.reject == ArbRejectCode::None && !best.actionable) best.reject = rj;  // 记代表拒因
            continue;
        }
        const double depth_cap = std::min(mkt.exit_depth_usdc, so.notional_usdc);
        const double conf = std::isfinite(hp.confidence) ? hp.confidence : 0.5;  // stub 无校准 → 0.5
        const double quality = conf * std::abs(net_edge) * depth_cap;
        if (quality > best.signal_quality) {
            best.actionable = true;
            best.horizon_idx = static_cast<int>(h);
            best.horizon_sec = ml::kWallHorizonsSec[h];
            best.predicted_dmid = hp.dmid;
            best.ci_low = hp.ci_low;
            best.confidence = conf;
            best.net_edge = net_edge;
            best.entry_px = is_long ? mkt.best_ask : mkt.best_bid;
            best.target_exit_px = is_long ? (best.entry_px + cons_move) : (best.entry_px - cons_move);
            best.suggested_notional = so.notional_usdc;
            best.signal_quality = quality;
            best.reject = ArbRejectCode::None;
        }
    }
    return best;
}

}  // namespace stcpp::risk
