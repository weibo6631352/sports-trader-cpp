// include/stcpp/risk/arb_risk.hpp — 短时套利风控: RJ-ARB 拒单门 + open-leg 强平台账 (Phase 3; 主计划 §4)
//
// Owner: 老雷 (GM) — 短时套利引擎 模块3/5
// last_review: 2026-06-01
//
// 安全红线 (老板 2026-06-01 哲学: 策略阈值软化喂模型, 但【灾难兜底】保留 — bug 会掏空账户的不交给模型):
//   - RJ-ARB-1..5: taker 方向性套利专用拒单 (与结算 RM 21 active 码分开, 独立 enum; 集成进 RiskGateway = 模块4)。
//   - OpenLegLedger: 进场即背"horizon 内必平"义务; 到 deadline 未平 → 强平 (裸方向暴露违背套利初衷)。
//   - RJ-ARB-5 STALE_PREDICTION = 延迟可行性的 RM enforce 点 (老板点名: 跨洋/成交确认延迟吃掉窗口 → 拒)。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace stcpp::risk {

enum class ArbRejectCode : std::uint8_t {
    None = 0,
    PredCiCrossesZero = 1,        // RJ-ARB-1: CI 跨 entry → 方向不确定 (点估计禁直接发)
    ExitDepthInsufficient = 2,    // RJ-ARB-2: L1-L5 累计深度 < 预期平仓量 (进得去出不来=套利第一杀手)
    RoundTripBudgetExceeded = 3,  // RJ-ARB-3: in-flight 未平腿超并发上限
    HorizonBudgetExceeded = 4,    // RJ-ARB-4: 窗口内累计 taker 敞口超 cap
    StalePrediction = 5,          // RJ-ARB-5: 预测年龄+预估RTT > horizon×0.5 → 延迟吃掉窗口
};

[[nodiscard]] constexpr const char* to_string(ArbRejectCode c) noexcept {
    switch (c) {
        case ArbRejectCode::None: return "NONE";
        case ArbRejectCode::PredCiCrossesZero: return "RJ_ARB_PRED_CI_CROSSES_ZERO";
        case ArbRejectCode::ExitDepthInsufficient: return "RJ_ARB_EXIT_DEPTH_INSUFFICIENT";
        case ArbRejectCode::RoundTripBudgetExceeded: return "RJ_ARB_ROUND_TRIP_BUDGET_EXCEEDED";
        case ArbRejectCode::HorizonBudgetExceeded: return "RJ_ARB_HORIZON_BUDGET_EXCEEDED";
        case ArbRejectCode::StalePrediction: return "RJ_ARB_STALE_PREDICTION";
    }
    return "UNKNOWN";
}

struct ArbGateInput {
    double pred_dmid{0.0};         // 预测 mid 移动 (方向 = sign)
    double ci_low{0.0};            // 预测 CI 下界
    double ci_high{0.0};           // 预测 CI 上界
    double exit_depth_usdc{0.0};   // L1-L5 可平仓累计深度
    double target_notional_usdc{0.0};  // 想下的量
    std::int64_t pred_gen_ts_ns{0};    // 预测生成时刻
    std::int64_t now_ns{0};            // 当前 (下单决策时刻)
    std::int64_t horizon_ns{0};        // 该信号 horizon
    std::int64_t est_rtt_ns{0};        // 预估端到端 RTT (含成交确认)
    int open_leg_count{0};             // 当前未平腿数
    int max_open_legs{0};              // 并发上限 (0=不限)
    double horizon_exposure_usdc{0.0}; // 窗口内已累计 taker 敞口
    double horizon_cap_usdc{0.0};      // 窗口敞口上限 (0=不限)
};

// CheckArbGate — taker 套利专用门 (顺序: 最致命的延迟先判)。None = 放行。
[[nodiscard]] inline ArbRejectCode CheckArbGate(const ArbGateInput& in) noexcept {
    // RJ-ARB-5 延迟红线 (最先): 预测年龄 + 预估 RTT 超过 horizon 一半 → 窗口已被延迟吃掉过半。
    if (in.horizon_ns > 0) {
        const std::int64_t pred_age = (in.now_ns > in.pred_gen_ts_ns) ? in.now_ns - in.pred_gen_ts_ns : 0;
        if (pred_age + in.est_rtt_ns > in.horizon_ns / 2) return ArbRejectCode::StalePrediction;
    }
    // RJ-ARB-1 方向确定性: 多头需 ci_low>0; 空头需 ci_high<0; 否则 CI 跨 0 = 方向不确定。
    if (in.pred_dmid > 0.0) {
        if (!(in.ci_low > 0.0)) return ArbRejectCode::PredCiCrossesZero;
    } else if (in.pred_dmid < 0.0) {
        if (!(in.ci_high < 0.0)) return ArbRejectCode::PredCiCrossesZero;
    } else {
        return ArbRejectCode::PredCiCrossesZero;  // pred=0 无方向
    }
    // RJ-ARB-2 退出深度: 平仓累计深度 < 想下的量 → 出不来。
    if (in.exit_depth_usdc < in.target_notional_usdc) return ArbRejectCode::ExitDepthInsufficient;
    // RJ-ARB-3 并发腿上限。
    if (in.max_open_legs > 0 && in.open_leg_count >= in.max_open_legs)
        return ArbRejectCode::RoundTripBudgetExceeded;
    // RJ-ARB-4 窗口敞口上限。
    if (in.horizon_cap_usdc > 0.0 &&
        in.horizon_exposure_usdc + in.target_notional_usdc > in.horizon_cap_usdc)
        return ArbRejectCode::HorizonBudgetExceeded;
    return ArbRejectCode::None;
}

// ---------------------------------------------------------------------------
// OpenLegLedger — 未平套利腿台账 + 强平 fail-safe。
//   进场即登记 deadline (entry + horizon); SweepExpired 返回到期未平的腿 (调用方市价强平)。
//   单写线程 (决策线程); 非热路径 sweeper 周期调用。
// ---------------------------------------------------------------------------
struct OpenArbLeg {
    std::string token_id;
    std::string condition_id;
    double entry_px{0.0};
    double qty{0.0};
    double target_exit_px{0.0};
    std::int64_t entry_ts_ns{0};
    std::int64_t deadline_ts_ns{0};  // entry_ts + horizon → 必平 deadline
};

class OpenLegLedger {
public:
    void Add(OpenArbLeg leg) { legs_.push_back(std::move(leg)); }

    // 平仓 (按 token_id 移除第一个匹配腿)。返回是否找到。
    bool Close(const std::string& token_id) noexcept {
        for (auto it = legs_.begin(); it != legs_.end(); ++it) {
            if (it->token_id == token_id) {
                legs_.erase(it);
                return true;
            }
        }
        return false;
    }

    // SweepExpired — 返回 deadline ≤ now 的腿 (需强平)。不移除 (调用方平成后 Close)。
    [[nodiscard]] std::vector<OpenArbLeg> SweepExpired(std::int64_t now_ns) const {
        std::vector<OpenArbLeg> expired;
        for (const auto& l : legs_) {
            if (l.deadline_ts_ns > 0 && l.deadline_ts_ns <= now_ns) expired.push_back(l);
        }
        return expired;
    }

    // 预测源失效 / book 断流 → 所有未平腿立即强平 (裸方向暴露违背套利初衷)。
    [[nodiscard]] std::vector<OpenArbLeg> AllOpen() const { return legs_; }

    [[nodiscard]] std::size_t open_count() const noexcept { return legs_.size(); }

private:
    std::vector<OpenArbLeg> legs_;
};

}  // namespace stcpp::risk
