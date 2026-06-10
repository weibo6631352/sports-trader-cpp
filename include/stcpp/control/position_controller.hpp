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
    double fair{0.5};                 // 被选边 fair prob (p_fair_selected); taker 退出护栏锚 (2026-06-10)
                                      //   bid_not_degenerate 锚 fair 而非 reservation_buy (后者含 fee+vig 偷放宽容差)
    double best_ask{0.0};             // 被选边市场 best ask
    double best_bid{0.0};             // 被选边市场 best bid
    double min_rebalance_pusd{1.0};   // 防抖死区 (绝对 pUSD; 小梁 = max(1, 0.1·|target|))
    double min_order_pusd{0.0};       // 最小买单门 (老板 2026-06-09「体育 min 5 单」; 0=关): 买单 < 它跳过 (真盘下不进+砍dust churn)
    double per_order_cap_pusd{0.0};   // 单笔上限 (clamp; RM per_order_cap 同源)
    bool allow_short{false};          // v1=false (空头 clamp 0); M2 开
    bool force_cross{false};          // 强制穿越 (小梁 Q-梁-2: |Δfair|>0.02 → 绕死区; 比分大跳不堵)
    // 止损强平 (2026-06-09 老姜微观结构裁决): 仅 rel_stop 触发 → 卖侧 taker 在 best_bid 退出。与 force_cross
    //   解耦 —— force_cross(fair 噪声跳)绝不触发 taker 退出(否则把赢家在 coin-flip 点 churn 出去, 实测 71%→50%)。
    bool force_stop{false};
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
    // exec_margin (持仓管理 Stage 2 §4.1 执行层, 老板 2026-06-05): 逆选/波动保护边际, 调用方算好传入。
    //   = k_tox·|OFI|/depth (毒性: 簿薄/单流猛 → 易被逆选) + k_vol·σ²·τ (波动×剩余期限)。只用【幅度】
    //   (|OFI|, 不碰方向 — 方向归 sharp)。与抽样噪声 margin 【正交】(逆选≠估计噪声) → 叠加而非取大,
    //   且 noise_free 下仍生效 (sharp 信号在毒簿里同样要逆选保护)。≥0; 默认 0 = 无 (向后兼容, CR01-05 不变)。
    double exec_margin{0.0};
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
    const double base_margin = in.noise_free ? floor : std::max(floor, in.z * sigma);
    // 执行层动态 margin (§4.1): 逆选(毒性)+ 波动保护, 与抽样噪声正交 → 加性 (noise_free 下仍叠加)。
    const double exec_m = (std::isfinite(in.exec_margin) && in.exec_margin > 0.0) ? in.exec_margin : 0.0;
    out.required_margin = base_margin + exec_m;
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

// ---------------------------------------------------------------------------
// rebalance 死区 (持仓管理 Stage 2 §4.1 执行层, 老板 2026-06-05「死区随 p(1−p) 缩放防费磨损」)。
//   死区 = max(绝对 floor, pct×|target|, fee_churn_guard)。BR-1 纯函数 (回测=实盘共用)。
//   fee_churn_guard = fee_k × 往返费率 × |target|; 往返费率 = 2·fee_coef·p(1−p)。
//   p(1−p) 在 p≈0.5 处最大 (费最贵) → guard 自动放宽死区 → 抑制被费吃掉的小额 churn;
//   p 极端处 (p≈0.9) 费小, guard→0 不挡 → 该处可更细 rebalance。纯加性 (max 第三项, 只放宽不收窄)
//   → 默认 fee_k=0 时逐位等于原 max(floor, pct×|target|), 向后兼容。
// ---------------------------------------------------------------------------
struct DeadbandConfig {
    double floor_pusd{1.0};  // 绝对下限 (cfg_.min_rebalance_floor_pusd 同源)
    double pct{0.10};        // |target| 比例项 (现行 0.10)
    double fee_k{0.0};       // 往返费率倍数 (默认 0 = 关 = 现行为; 开 e.g. 2.0 = 死区 ≥ 2×往返费)
};

[[nodiscard]] inline double ComputeRebalanceDeadband(double target_mag, double p_fair, double fee_coef,
                                                     const DeadbandConfig& cfg) noexcept {
    const double abs_t = std::abs(target_mag);
    double dz = std::max(cfg.floor_pusd > 0.0 ? cfg.floor_pusd : 0.0, cfg.pct * abs_t);
    if (cfg.fee_k > 0.0 && std::isfinite(p_fair) && p_fair > 0.0 && p_fair < 1.0 &&
        std::isfinite(fee_coef) && fee_coef > 0.0) {
        const double pp = p_fair * (1.0 - p_fair);        // ∈ (0, 0.25]
        const double rt_fee_frac = 2.0 * fee_coef * pp;   // 往返 (买+卖) 费率
        dz = std::max(dz, cfg.fee_k * rt_fee_frac * abs_t);
    }
    return dz;
}

// ---------------------------------------------------------------------------
// 毒性冻结加仓 (持仓管理 Stage 2 §4.1 执行层, 老板 2026-06-05「OFI/BidAbsence 超阈→暂停新单/冻结加仓」)。
//   毒簿 (|OFI|/depth 超阈 = 单边流冲击 / BidAbsence 超阈 = 簿一侧塌陷) → 暂停【新增加仓】, 减仓/平仓照常
//   (泄险优先)。这是 exec_margin (软, 渐进压价) 的【硬档】配套: 严重毒性直接冻结, 不只是压价。BR-1 纯函数。
//   只判「是否冻结加仓」(bool), 调用方据此把加仓侧 target clamp 到 current (不增不减); 减仓不受影响。
//   force_cross (进球/必赢事件) 由调用方绕过 (事件驱动合法穿越)。默认 enabled=false → 恒 false (现行为)。
// ---------------------------------------------------------------------------
struct ToxicityGateConfig {
    bool enabled{false};
    double ofi_depth_thr{0.0};    // |OFI|/depth ≥ 此 → 冻结 (0 = 该判据关, 由 bid_absence 单独判)
    double bid_absence_thr{1.0};  // BidAbsence frac ≥ 此 → 冻结 (1.0 = 该判据关; e.g. 0.5 = 半窗无 bid)
};

[[nodiscard]] inline bool ToxicityFreezesAdds(double abs_ofi_over_depth, double bid_absence_frac,
                                              const ToxicityGateConfig& cfg) noexcept {
    if (!cfg.enabled) return false;
    if (cfg.ofi_depth_thr > 0.0 && std::isfinite(abs_ofi_over_depth) &&
        abs_ofi_over_depth >= cfg.ofi_depth_thr) {
        return true;
    }
    if (cfg.bid_absence_thr < 1.0 && std::isfinite(bid_absence_frac) &&
        bid_absence_frac >= cfg.bid_absence_thr) {
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// A-S 库存 skew — 已审定 SKIP (持仓管理 Stage 2 §4.1, 小袁 microstructure 设计裁决 2026-06-09)。
//   synthesis §4.1 列了「reservation 减 A-S 库存项」, 但微观结构裁决: 我们范式里它【双重计数 + 半边非法 +
//   数值可忽略】, 不实现:
//   ① 目标仓位控制器的 gap=target−current 【就是】A-S 库存机制, 且锚在 sharp target (比 A-S 锚 flat=0 更对);
//      锚 flat=0 会双重计数且与自身 alpha 打架, 锚 target 则 skew≡gap×正系数 (符号已由控制器定, 无新信息)。
//   ② 生产 predictive_unwind=true 令卖侧绕开 reservation_sell → A-S 仅作用买侧, 已被死区 + lifecycle 乘子 +
//      本文件动态 exec_margin 三重覆盖; 卖侧 A-S skew (库存大就主动压价卖) = 抢跑 sharp 反向, 违 2026-06-05 裁决。
//   ③ A-S 教科书量纲下该项 ~0.02¢ 可忽略; 调大需非物理 γ → 沦为又一个 ad-hoc taper (已有 lifecycle/clv/DD 三个)。
//   → 执行层「库存→更挑剔→泄回目标」语义由【target 控制器 + 动态 exec_margin + 死区】完整表达。不补 A-S。
// ---------------------------------------------------------------------------

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
        const double buy_sz = std::min(abs_gap, cap);
        // 最小买单门 (老板 2026-06-09「体育 min 5 单」): 买单 < min_order_pusd 跳过 —— 真盘 PM 体育 min 5
        //   下不进 + 砍 dust churn (实测 0.0u/0.1u 碎单污染流水)。卖侧(平仓)不设此门, 允许清掉零头。
        if (in.min_order_pusd > 0.0 && buy_sz < in.min_order_pusd) {
            a.reason = NoActReason::BelowThreshold;
            return a;
        }
        a.act = true;
        a.side = strategy::Side::Buy;
        a.size_pusd = buy_sz;
        a.limit_price = in.reservation_buy_px;
        a.is_close = false;
    } else {
        // 卖减/平仓: target<current = 预测说该减仓 (edge 收敛/翻转 → 仓位回 flat)。
        //   predictive_unwind (老板「双边预测的双边仓位管理」): best_bid 可成交即平 (随预测回 flat = 收敛兑现);
        //     原门 best_bid ≥ reservation_sell(fair+margin) 是做市「卖高」价, 市场收敛到 fair 永不触发 → 只能
        //     持到结算 (= alpha 退化成赌博)。买侧已保证 entry ≤ reservation_buy ≤ bid (减仓时市场≥fair) → 不锁亏。
        //   默认 (做市): 维持「卖高」语义。
        //   taker 退出 (2026-06-09 老姜微观结构裁决): 仅 rel_stop(force_stop) 或 predictive_unwind 触发 →
        //     taker 在 best_bid 成交退出 (崩盘 loser 割损 / 收敛兑现)。【不含 force_cross】—— force_cross 是
        //     fair 噪声跳(|Δfair|>0.02)绕死区, 若它触发 taker 退出会把赢家在 coin-flip 点卖掉(实测胜率 71%→50%
        //     + churn 放血)。解耦后: 赢家持到 reservation_sell(卖高/近收敛)或结算; 只有真崩盘(rel_stop)才 taker 割。
        const bool taker_exit = in.predictive_unwind || in.force_stop;
        // 执行护栏 (2026-06-09 老雷 + 老姜/小梁裁决, 配套实测 −5.80 灾难单): taker 退出绝不砸进【退化簿】——
        //   best_bid 远低于 fair(≈reservation_buy_px, =fair−fee−margin) = 簿塌陷(MM 撤盘/feed 陈旧), 砸卖 = 白送仓位
        //   (实测: fair 0.79 的 NO 被以 best_bid 0.0129 止损甩卖, realized −5.80)。bid 距 fair 超 kMaxTakerSlip → 不卖,
        //   持有等簿恢复/真决出。真崩盘 loser: fair 也低 → reservation_buy 也低 → bid 仍 ≥ reservation_buy−slip → 正常割损放行。
        // 2026-06-10 收紧 0.15→0.05 (老雷, 实盘 realized −$20 复盘): 0.15(15pt) 太松 —— 只拦灾难性砸卖(>15pt,
        //   如 0.0129)却放过【中等坏】砸卖。实测 0x33d2a04e fair 0.49 仓被 stop 砸卖在 bid 0.38 (距 reservation_buy
        //   ~8.75pt < 15pt → 放行), realized −0.219/share vs 持有到结算 fair-implied −0.109/share = 损失翻倍。
        //   收到 0.05: bid 距 fair 超 ~5pt 即不 taker 卖, 持有到结算 (fair 是真值, 不在 fair 之下贱卖)。真崩盘
        //   (fair 也塌) bid 仍在 fair−5pt 容差内 → 正常割损放行不受影响 (fair 自适应)。
        // 2026-06-10 锚点修正 (老韩交易历史复盘): 原锚 reservation_buy_px(=fair−fee−margin) 让 vig/fee 偷偷
        //   放宽容差 —— 真实容差 = kMaxTakerSlip + fee_buy + 半 vig, 正常体育 vig(0.045) 把 5pt 名义顶成 8pt
        //   (实测 sell NO @0.40 vs fair 0.48 = 8pt below 放行)。改锚 fair: 5pt 就是真 5pt, 与「不在 fair 之下
        //   贱卖超 5pt」意图一致, 且天然随 fair 浮动 (真崩盘 fair 低→正常割损照放行, 自适应性不变)。
        constexpr double kMaxTakerSlip = 0.05;
        const bool bid_not_degenerate = in.best_bid >= in.fair - kMaxTakerSlip;
        const bool marketable = taker_exit ? (in.best_bid > 0.0 && bid_not_degenerate)
                                           : (in.best_bid > 0.0 && in.best_bid >= in.reservation_sell_px);
        if (!marketable) {
            a.reason = NoActReason::NotMarketable;
            return a;
        }
        // v1 不开空: 卖出量不超过当前持仓 (current ≥0)。
        // 2026-06-10 force_stop 一次性清仓 (老姜+老韩交易历史复盘, 治碎卖 fee 头号成本): 止损/急转强平走
        //   taker 砸 bid, 分批无降冲击收益, 纯多付 N 次 fee (实测 0x1ef76b 12 笔碎卖)。force_stop 时绕
        //   per_order_cap 一次卖全仓 (current); 普通减仓/做市仍受 cap。减仓不增敞口, 绕 cap 安全 (仍经 RM)。
        double sell_sz = in.force_stop ? abs_gap : std::min(abs_gap, cap);
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
        a.limit_price = taker_exit ? in.best_bid : in.reservation_sell_px;  // taker 退出(止损/事件) / 做市卖高(持赢家)
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

// ============================================================================
// CLV sizing 乘子 (持仓管理 Stage 2, 老板 2026-06-05 裁决「CLV 好就实时放大」, 推翻架构"仅离线")。
//   用【滚动 CLV 均值】(eval::RollingClv, ~50 笔稳健量) 调 target 量级。
//   与生命周期乘子的关键不同: 本乘子【可 >1 放大】(老板明确授权), 但封顶 max_mult (护栏)。
//
// GM 护栏 (化解架构 2s 滞后噪声担忧):
//   · 用滚动均值非单笔瞬时 CLV (滚动平滑掉 sharp 2.3s 噪声; 研究 ~50 笔显著)。
//   · 放大封顶 max_mult (默认 1.5×); 配合 λ_base=0.35 → λ_eff ≤ 0.525 ≪ 全 Kelly f* (天花板未破)。
//   · 下游 RM caps (per_order/condition/event/maxDD) 仍硬夹 → 放大的真实上界在 RM, 不失控。
//   · 滚动样本 < min_samples → 1.0 (fail-open)。不碰符号 (方向归 sharp 低估边)。
// ============================================================================
struct ClvSizingConfig {
    bool enabled{true};
    double clv_ref{0.01};         // 参考 CLV (prob); |clv_mean|=clv_ref 时放大/收缩 k_amp/k_cut
    double k_amp{0.5};            // 正 CLV 放大强度
    double k_cut{1.0};            // 负 CLV 收缩强度 (更狠: 负 CLV=追市, 该缩)
    double max_mult{1.5};         // 放大上限 (老板护栏 clamp≤1.5×)
    double floor{0.3};            // 收缩下限
    std::int32_t min_samples{20};  // 滚动样本 < 此 → fail-open (研究 ~50 显著; 20 起步)
};

// 返回 ∈ [cfg.floor, cfg.max_mult]。正 CLV→放大(>1), 负 CLV→收缩(<1), 样本不足/NaN→1.0。
[[nodiscard]] inline double ComputeClvMultiplier(double clv_mean, std::int32_t n,
                                                 const ClvSizingConfig& cfg) noexcept {
    if (!cfg.enabled) return 1.0;
    if (n < cfg.min_samples) return 1.0;
    if (!std::isfinite(clv_mean)) return 1.0;
    const double cr = cfg.clv_ref > 0.0 ? cfg.clv_ref : 1.0;
    if (clv_mean >= 0.0) {
        const double m = 1.0 + cfg.k_amp * (clv_mean / cr);  // 放大
        return m > cfg.max_mult ? cfg.max_mult : (m < 1.0 ? 1.0 : m);
    }
    const double m = 1.0 + cfg.k_cut * (clv_mean / cr);  // clv_mean<0 → 收缩
    return m < cfg.floor ? cfg.floor : (m > 1.0 ? 1.0 : m);
}

// ============================================================================
// DD→target 乘子 (持仓管理 Stage 2, 老板 2026-06-05 裁决「回撤大只停加仓 + hysteresis, 不主动砍现仓」)。
//   按【当前回撤】分档给乘子 m∈[0,1]; 调用方据此【只限制加仓幅度, 绝不强制减仓】(见 paper_loop)。
//   这把「出场=target 缩小」在账户级机制化, 但低流动性 DD 区不被迫 taker 锤实浮亏 (老板裁决)。
//   保命门 (daily-loss 熔断 / maxDD 红线) 仍在, 本乘子是叠加收紧非替代。
// ============================================================================
struct DrawdownConfig {
    bool enabled{true};
    double dd_t1{0.05};            // 回撤 ≥ 此 → m_t1 (轻度去险)
    double dd_t2{0.10};            // 回撤 ≥ 此 → m_t2
    double dd_halt{0.15};          // 回撤 ≥ 此 (北极星红线) → m=0 (只持不加; 砍仓交给保命门)
    double m_t1{0.5};
    double m_t2{0.25};
    double hysteresis_band{0.02};  // 恢复需比降档阈值多回落此带 (防抖)
};

// 纯 tier 映射 (无 hysteresis): 当前回撤 dd → 乘子。调用方用 dd / dd+band 两路实现黏滞恢复。
[[nodiscard]] inline double DrawdownTierMultiplier(double dd, const DrawdownConfig& cfg) noexcept {
    if (!cfg.enabled) return 1.0;
    if (dd >= cfg.dd_halt) return 0.0;
    if (dd >= cfg.dd_t2) return cfg.m_t2;
    if (dd >= cfg.dd_t1) return cfg.m_t1;
    return 1.0;
}

// ============================================================================
// 相关性折扣乘子 (持仓管理 Stage 2 §4.1 规模层, 小梁 2026-06-09 设计裁决; 老郭 Round2: 按 gross 加权
//   非计数 N + ρ 静态分桶)。消同赛事 ρ 相关超注 (5 路调研一致挖出的 #1 结构洞)。BR-1 纯函数。
//
// 与 R6.2c RM 硬 cap 分工 (spec Q3, 非冗余非双重计数):
//   · 硬 cap = ρ=1 保命墙 (event_gross 满 → 拒单 EXCEED_EVENT_EXPOSURE);
//   · 本乘子 = ρ 加权【提前 taper】(占用率 ≥ taper_start 就柔性缩 target, 撞墙前先刹车)。
//   两者串联作用于信号流不同阶段 (乘子削意图 / gateway 拒成交), 乘子削小 → 到 RM 时 gross 更小 → 更不撞 cap。
//
// 防自激 (老郭顾虑 N-自激, spec Q2): existing_event_gross 【排除本 condition 自身】→ ∂m_self/∂target_self=0,
//   本盘削小不反馈回本盘乘子 (环物理切断); 连续线性无 tier 跳变; 死区 (ComputeRebalanceDeadband) 阻尼二阶收敛环。
//
// 架构界线: ∈[floor,1] 不碰符号 (方向归 sharp)、绝不 >1 放大; fail-open (NaN/退化/cap=0 → 1.0)。
// ============================================================================
struct CorrelationConfig {
    bool enabled{false};       // 默认关: 改交易行为 + ρ 表未校准; R6.2c 硬 cap 已 backstop 保命 (spec Q5)
    double taper_start{0.50};  // ρ 加权占用率 u ≥ 此才开始削 (硬 cap 满在 u_raw=1.0, 留半档减速带)
    double floor{0.30};        // 乘子下限 (对齐 lifecycle/clv floor)
    double rho_default{0.70};  // ρ 桶查不到 → 保守偏高先验 (宁可多削)
};

struct CorrelationInput {
    double prospective_notional{0.0};  // 本 condition 拟达 |target| (pUSD; 仅判 >0 决定是否参与, 不入公式量级)
    double existing_event_gross{0.0};  // 该 event 当前 Σ|condition敞口| 【扣除本 condition 自身】(pUSD)
    double event_cap{0.0};             // event_exposure_cap (pUSD; RM 同源); ≤0 → 乘子退场 fail-open
    double rho{std::numeric_limits<double>::quiet_NaN()};  // 静态表查得 ρ; NaN → cfg.rho_default
};

// 返回 ∈ [cfg.floor, 1.0]。u = ρ·existing_gross/cap; u≤taper_start→1; 线性退坡到 floor; 退化→1.0 fail-open。
[[nodiscard]] inline double ComputeCorrelationMultiplier(const CorrelationInput& in,
                                                         const CorrelationConfig& cfg) noexcept {
    if (!cfg.enabled) return 1.0;
    // fail-open: 本笔无新增 / 该 event 无其他敞口 / cap 禁用 / 非有限 → 不改基线。
    if (!std::isfinite(in.prospective_notional) || in.prospective_notional <= 0.0) return 1.0;
    if (!std::isfinite(in.existing_event_gross) || in.existing_event_gross <= 0.0) return 1.0;
    if (!std::isfinite(in.event_cap) || in.event_cap <= 0.0) return 1.0;
    const double rho = std::isfinite(in.rho) ? std::clamp(in.rho, 0.0, 1.0) : cfg.rho_default;
    // ρ 加权"有效事件占用率": 已有同赛事敞口按 ρ 折成相关等价占 cap 比例。
    const double u = rho * in.existing_event_gross / in.event_cap;
    const double ts = std::clamp(cfg.taper_start, 0.0, 0.999);
    if (u <= ts) return 1.0;
    // 线性段: u∈(ts,1] → m∈[floor,1); u≥1 (cap 满) → floor。
    const double t = std::clamp((u - ts) / (1.0 - ts), 0.0, 1.0);
    const double m = 1.0 - (1.0 - cfg.floor) * t;
    return std::clamp(m, cfg.floor, 1.0);
}

}  // namespace stcpp::control
