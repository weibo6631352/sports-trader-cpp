// include/stcpp/control/position_controller.hpp — 目标仓位控制器 (纯函数核, header-only)
//
// Owner: 老雷 (GM) — 实施 docs/RESEARCH/laolei-target-position-controller-spec-v1.md
// last_review: 2026-05-31
//
// 范式 (老板 2026-05-31): 没有进/出场/止损概念; 模型输出目标仓位 + 价格界限;
//   控制器连续把当前仓位调到目标 (order = 目标 − 现仓), 限价执行绝不追价。
//
// 主权评审 (2026-05-31):
//   老周 (架构): 抽独立纯函数, 回测/实盘共用 (BR-1); 限价 = 控制器前置门 (撮合器 ABI 零改)。
//   小梁 (Kelly): reservation 公式 + min_rebalance 死区; Kelly notional 当目标仓位理论成立。
//   老韩 (RM): v1 只做多侧 (空头 clamp 0); 减仓单不绕 RM; signed cap magnitude 修复同批落 (H-1)。
//
// v1 范围 (三主权签字): 被选边 买增 + 卖减 + 限价不追。不反向、不开空 (→ M2)。
// 纯函数纪律 (老周): 无 IO / 无锁 / 无 now(); 数据由调用方 (TickOne) 取好传入。R-12 不触碰。
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "stcpp/strategy/signal_iface.hpp"  // strategy::Side

namespace stcpp::control {

// 控制器为什么不动 (act=false 时的诊断; audit/stat 用)
enum class NoActReason : std::uint8_t {
    None = 0,        // act=true
    BelowThreshold,  // |gap| < min_rebalance 死区 (防抖, 小梁 Q-梁-2)
    NotMarketable,   // 限价不可成交 (best 价劣于 reservation; 不追价, 老周 Q-周-1)
    ZeroGap,         // 目标 == 现仓
    InvalidInput,    // 非有限输入 (fail-closed)
};

// 控制器输入 (调用方在 TickOne 组装; 全 pUSD 同量纲, 被选边视角)。
struct ControlInput {
    double target_pusd{0.0};          // 目标仓位 (被选边 long; v1 ≥0, 空头由 allow_short 控制)
    double current_pusd{0.0};         // 当前持仓 (被选边 token 现有 long; v1 ≥0)
    double reservation_buy_px{0.0};   // 买入保留价上界 (best_ask ≤ 它才买; 小梁公式)
    double reservation_sell_px{1.0};  // 卖出保留价下界 (best_bid ≥ 它才卖)
    double best_ask{0.0};             // 被选边市场 best ask
    double best_bid{0.0};             // 被选边市场 best bid
    double min_rebalance_pusd{1.0};   // 防抖死区 (绝对 pUSD; 小梁 = max(1, 0.1·|target|))
    double per_order_cap_pusd{0.0};   // 单笔上限 (clamp; RM per_order_cap 同源)
    bool allow_short{false};          // v1=false (空头 clamp 0); M2 开
    bool force_cross{false};          // 强制穿越 (小梁 Q-梁-2: |Δfair|>0.02 → 绕死区; 比分大跳不堵)
    // 预测驱动平仓 (2026-06-04 老板「双边预测给出的双边仓位管理」): 减仓 (target<current, 预测说该减/flat)
    //   时, 不要求 best_bid ≥ reservation_sell(fair+margin, 做市「卖高」价 → 收敛到 fair 永不触发 → 持到结算),
    //   改 best_bid 可成交即平 (仓位随预测回 flat = 收敛兑现)。≤false (默认) = 原做市卖高语义 (契约测试不变);
    //   生产 daemon 置 true。买侧 + 加仓不受影响。
    bool predictive_unwind{false};
};

// 控制器输出 (TickOne 据此构造 OrderIntent 或 skip)。
struct ControlAction {
    bool act{false};                           // false = 本 tick 不动
    strategy::Side side{strategy::Side::Buy};  // act 时的方向
    double size_pusd{0.0};                     // 下单量 (≥0; 已 clamp cap + 不超持仓)
    double limit_price{0.0};                   // 限价 = reservation (绝不 market)
    bool is_close{false};                      // 减仓/平仓 (降敞口; RM cap 放行依据)
    NoActReason reason{NoActReason::None};
};

// ---------------------------------------------------------------------------
// reservation 公式 (小梁 Q-梁-1, 2026-05-31 三主权评审) — BR-1 共用 (回测/实盘/paper)。
//   required_margin = max(margin_floor, z × sqrt(fair·(1−fair)/n_eff))
//   reservation_buy  = fair − fee_per_unit(exec_ask) − required_margin  (买不超过此价)
//   reservation_sell = fair + fee_per_unit(exec_bid) + required_margin  (卖不低于此价)
//   fee_per_unit(p)  = fee_coef × p × (1−p)   (R-fee-2: per-market gamma feeSchedule.rate)
// 老板「价格涨了还硬买就赔」: reservation 是净 edge=0 的临界价, 越界即无 edge → 限价不追。
// ---------------------------------------------------------------------------
struct ReservationInput {
    double fair{0.5};         // 被选边 fair prob ∈ (0,1) (p_fair_selected)
    double exec_ask{0.0};     // 被选边 best ask (买入 fee 锚 + 执行触价)
    double exec_bid{0.0};     // 被选边 best bid (卖出 fee 锚 + 执行触价)
    double fee_coef{0.03};    // per-market 手续费系数 (gamma feeSchedule.rate)
    double margin_floor{0.0}; // required_margin 下限 (小梁: edge_ci_lower_floor 同源)
    double z{1.645};          // CI z (90% = 1.645)
    int n_eff{200};           // 有效样本数
    // noise_free (2026-06-04 老板「跑通赔率 edge 线」): fair 是无抽样噪声【点估计】(sharp bet365 de-vig /
    //   paper_no_edge_gates 调模型模式) → required_margin 跳过二项 z×σ 惩罚, 仅留 margin_floor (半 vig 经济
    //   地基)。否则 (score-prior/ML 噪声估计) 仍 max(floor, z×σ)。与 edge_ci 同源判据 (ResolveEdgeCiLower),
    //   消「sizing 说买 / reservation 说噪声不让买」的双标 —— sharp 进不了可下单侧的真因。
    bool noise_free{false};
};

struct ReservationPrices {
    double buy_px{0.0};          // reservation_buy
    double sell_px{1.0};         // reservation_sell
    double required_margin{0.0}; // 安全边际 (观测/训练)
};

[[nodiscard]] inline ReservationPrices ComputeReservation(const ReservationInput& in) noexcept {
    ReservationPrices out;
    // fail-closed: 非有限 / 退化 → buy_px=0 (永不可买) + sell_px=1 (永不可卖)。
    if (!std::isfinite(in.fair) || in.fair <= 0.0 || in.fair >= 1.0 || in.n_eff <= 0) {
        out.buy_px = 0.0;
        out.sell_px = 1.0;
        out.required_margin = 0.0;
        return out;
    }
    const double sigma = std::sqrt(in.fair * (1.0 - in.fair) / static_cast<double>(in.n_eff));
    const double floor = std::isfinite(in.margin_floor) ? in.margin_floor : 0.0;
    // noise_free: sharp 点估计 / 调模型模式 → 跳过二项 z×σ (对点估计是错模型, 见 ResolveEdgeCiLower),
    //   仅留 margin_floor (半 vig 经济地基, 防保证亏交易)。否则噪声估计仍 max(floor, z×σ)。
    out.required_margin = in.noise_free ? floor : std::max(floor, in.z * sigma);
    const double coef = std::isfinite(in.fee_coef) ? std::max(0.0, in.fee_coef) : 0.0;
    // fee 锚在各自触价 (买 ask / 卖 bid); 触价非有限则退回 fair 锚 (保守)。
    const double pa = (std::isfinite(in.exec_ask) && in.exec_ask > 0.0 && in.exec_ask < 1.0) ? in.exec_ask : in.fair;
    const double pb = (std::isfinite(in.exec_bid) && in.exec_bid > 0.0 && in.exec_bid < 1.0) ? in.exec_bid : in.fair;
    const double fee_buy = coef * pa * (1.0 - pa);
    const double fee_sell = coef * pb * (1.0 - pb);
    out.buy_px = in.fair - fee_buy - out.required_margin;
    out.sell_px = in.fair + fee_sell + out.required_margin;
    return out;
}

// Decide — 纯函数: 目标仓位 + 限价 → 控制动作。无副作用。
//   gap = target − current; 死区内不动; 限价不可成交不动; 否则 买增(gap>0)/卖减(gap<0)。
//   v1: 空头 clamp 0 (老韩 H); 卖不超过持仓 (不开空); 减仓 is_close=true (RM cap 放行)。
[[nodiscard]] inline ControlAction Decide(const ControlInput& in) noexcept {
    ControlAction a;

    // fail-closed: 非有限输入 → 不动
    if (!std::isfinite(in.target_pusd) || !std::isfinite(in.current_pusd) || !std::isfinite(in.best_ask) ||
        !std::isfinite(in.best_bid)) {
        a.reason = NoActReason::InvalidInput;
        return a;
    }

    // v1 安全: 空头目标 clamp 0 (老韩 H-1/H-2; M2 才开空)。
    double target = in.target_pusd;
    if (!in.allow_short && target < 0.0) {
        target = 0.0;
    }

    const double current = in.current_pusd;
    const double gap = target - current;
    const double abs_gap = std::abs(gap);

    // 防抖死区 (小梁 Q-梁-2): |gap| 太小不动 (避免高频小额 rebalance 被 fee 侵蚀)。
    //   强制穿越 (小梁 Q-梁-2): fair 大跳 (|Δfair|>0.02, e.g. 进球) → force_cross 绕死区, 不堵突变。
    const double threshold = (in.min_rebalance_pusd > 0.0) ? in.min_rebalance_pusd : 0.0;
    if (!in.force_cross && abs_gap < threshold) {
        a.reason = NoActReason::BelowThreshold;
        return a;
    }

    const double cap = (in.per_order_cap_pusd > 0.0) ? in.per_order_cap_pusd : abs_gap;

    if (gap > 0.0) {
        // 买增: 限价不追 — best_ask 必须 ≤ 买入保留价 (老周 Q-周-1 前置门)。
        if (!(in.best_ask > 0.0 && in.best_ask <= in.reservation_buy_px)) {
            a.reason = NoActReason::NotMarketable;
            return a;
        }
        a.act = true;
        a.side = strategy::Side::Buy;
        a.size_pusd = std::min(abs_gap, cap);
        a.limit_price = in.reservation_buy_px;
        a.is_close = false;
    } else {
        // 卖减/平仓: target<current = 预测说该减仓 (edge 收敛/翻转 → 仓位回 flat)。
        //   predictive_unwind (老板「双边预测的双边仓位管理」): best_bid 可成交即平 (随预测回 flat = 收敛兑现);
        //     原门 best_bid ≥ reservation_sell(fair+margin) 是做市「卖高」价, 市场收敛到 fair 永不触发 → 只能
        //     持到结算 (= alpha 退化成赌博)。买侧已保证 entry ≤ reservation_buy ≤ bid (减仓时市场≥fair) → 不锁亏。
        //   默认 (做市): 维持「卖高」语义。
        const bool marketable = in.predictive_unwind ? (in.best_bid > 0.0)
                                                      : (in.best_bid > 0.0 && in.best_bid >= in.reservation_sell_px);
        if (!marketable) {
            a.reason = NoActReason::NotMarketable;
            return a;
        }
        // v1 不开空: 卖出量不超过当前持仓 (current ≥0)。
        double sell_sz = std::min(abs_gap, cap);
        if (!in.allow_short) {
            sell_sz = std::min(sell_sz, std::max(0.0, current));
        }
        if (sell_sz <= 0.0) {
            a.reason = NoActReason::ZeroGap;
            return a;
        }
        a.act = true;
        a.side = strategy::Side::Sell;
        a.size_pusd = sell_sz;
        a.limit_price = in.predictive_unwind ? in.best_bid : in.reservation_sell_px;  // 平仓 marketable / 做市卖高
        a.is_close = true;  // 减仓 (降敞口 → RM cap 放行)
    }

    if (a.size_pusd <= 0.0) {
        a.act = false;
        a.reason = NoActReason::ZeroGap;
    }
    return a;
}

// ============================================================================
// edge-生命周期乘子 (持仓管理 Stage 2, 老板 2026-06-05「sharp 速度/收敛接进决策」+ 量化 Round1
//   「target 别裸喂瞬时 edge」)。对【sharp 自身时序】做无预测描述统计 → [floor,1] 量级乘子, 缩小
//   target 以抑制噪声驱动的过度交易。BR-1 纯函数 (回测=实盘同逻辑)。
//
// 架构界线 (老郭 2026-06-05 仲裁, 守「方向真值=赔率源 sharp; 量化不可靠永不驱动方向」):
//   ✓ 合法: 描述 sharp 自己的 Vol(抖动)/ConvergenceRate(收敛对错), 只调【量级】∈[floor,1]。
//   ✗ 越界: 乘子 >1 (放大过 Kelly 上界) / 让 target 反号 (预测翻转) / 用 ML 预测未来 fair。
//   → 本函数恒 ∈[floor,1], 不碰符号; 调用方乘到 |target|, 方向仍由 sharp 低估边决定。
// ============================================================================
struct LifecycleInput {
    double sharp_vol{std::numeric_limits<double>::quiet_NaN()};        // SharpFairTrack::Vol(w) prob RMS
    double sharp_conv_rate{std::numeric_limits<double>::quiet_NaN()};  // ConvergenceRate(w): <0收敛 >0发散
    std::int32_t sample_count{0};                                      // 窗口样本数 (不足→fail-open)
};

struct LifecycleConfig {
    bool enabled{true};
    double vol_ref{0.02};         // 参考 sharp 抖动 (prob); vol=vol_ref 时稳定性减 k_vol
    double k_vol{0.5};            // 稳定性惩罚强度 (Vol 越大缩越多)
    double div_ref{0.01};         // 参考发散率 (prob/sec); conv_rate=div_ref 时 regime 减 k_div
    double k_div{0.5};            // 发散谨慎惩罚强度 (市场背离 sharp 越快缩越多)
    double floor{0.3};            // 乘子下限 (绝不把合法 edge 砍到 0)
    std::int32_t min_samples{3};  // 窗口样本 < 此 → fail-open (m=1, 不改基线)
};

// 返回 ∈ [cfg.floor, 1.0]。样本不足 / NaN → 1.0 (fail-open, 等于现行为)。
[[nodiscard]] inline double ComputeLifecycleMultiplier(const LifecycleInput& in,
                                                       const LifecycleConfig& cfg) noexcept {
    if (!cfg.enabled) return 1.0;
    if (in.sample_count < cfg.min_samples) return 1.0;
    if (!std::isfinite(in.sharp_vol) || !std::isfinite(in.sharp_conv_rate)) return 1.0;
    const double vr = cfg.vol_ref > 0.0 ? cfg.vol_ref : 1.0;
    const double dr = cfg.div_ref > 0.0 ? cfg.div_ref : 1.0;
    auto clampf = [&](double x) { return x < cfg.floor ? cfg.floor : (x > 1.0 ? 1.0 : x); };
    // 1. 稳定性因子 (header 钦定 Vol 用途): sharp 抖动大 = 噪声多于真移动 → 缩量。
    const double m_stab = clampf(1.0 - cfg.k_vol * (in.sharp_vol / vr));
    // 2. 发散谨慎因子: ConvergenceRate>0 = 市场背离 sharp (我们 sharp 有 ~2.3s 延迟, 别盲目按瞬时大 gap
    //    加满)→ 谨慎缩。收敛(<0)/震荡(≤0) 被确认或持平 → 不缩 (=1)。绝不因发散反号 (只缩量级)。
    const double m_regime = in.sharp_conv_rate > 0.0 ? clampf(1.0 - cfg.k_div * (in.sharp_conv_rate / dr)) : 1.0;
    return clampf(m_stab * m_regime);
}

}  // namespace stcpp::control
