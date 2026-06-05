// include/stcpp/eval/rolling_clv.hpp — 实时 CLV (PIT-safe) 滚动均值环 (持仓管理 Stage 2)
//
// Owner: 老雷 (GM) · last_review: 2026-06-05
//
// 与 clv_tracker.hpp 的区别 (关键, 别混):
//   - clv_tracker.hpp = 结算/收盘口径 CLV (用【未来】参考价 settle/close) → 含前视, **只能离线评估,
//     绝禁进决策** (其 header 红线)。
//   - 本组件 = 实时 CLV (PIT-safe): CLV = 成交时刻【决策 fair (sharp 锚)】 − 成交价。fair 是成交刻
//     已观测量, 无未来参考 → 可驱动 sizing。
//
// 用途 (老板 2026-06-05 裁决, 推翻架构"仅离线"): CLV 好就实时放大 target。GM 护栏:
//   用【滚动均值】(~50 笔统计显著的稳健量) 而非单笔瞬时 CLV (那个才带 sharp ~2.3s 滞后噪声);
//   配 control::ComputeClvMultiplier (clamp / Kelly 天花板 / maxDD 地板) 使用。
//
// 含义: CLV>0 = 我们入场价优于决策 fair (买在 fair 下方) = 入场质量好 = edge 真; <0 = 总在追市。
//   研究 (体育博彩组): CLV≈EV 近 1:1, ~50 笔即统计显著 (胜负 PnL 要几千笔) → 低方差领先指标。
//
// BR-1: 纯逻辑环 (无 IO/无锁/无 now); 回测按成交序 replay → Mean 逐位一致。loop_thread_ 单 writer。
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace stcpp::eval {

// 全局实时 CLV 滚动环 (系统级"近期入场质量"信号; 跨盘口聚合)。
class RollingClv {
 public:
    static constexpr std::size_t kCapacity = 50;  // 研究: ~50 笔 CLV 统计显著

    // 记一笔成交的实时 CLV (= 决策 fair − 成交价, 买被低估边 long 视角; 正=入场优于 fair)。
    //   非有限值跳过 (不污染均值)。
    void Record(double clv) noexcept {
        if (!std::isfinite(clv)) return;
        buf_[head_] = clv;
        head_ = (head_ + 1) % kCapacity;
        if (count_ < kCapacity) ++count_;
    }

    [[nodiscard]] std::size_t Count() const noexcept { return count_; }

    // 滚动均值 (空 → NaN)。每次重算 (count_ ≤ 50, 廉价; 避免 running-sum 浮点漂移)。
    [[nodiscard]] double Mean() const noexcept {
        if (count_ == 0) return std::numeric_limits<double>::quiet_NaN();
        double s = 0.0;
        for (std::size_t i = 0; i < count_; ++i) s += buf_[i];  // 未满时填在 [0,count_); 满后全用
        return s / static_cast<double>(count_);
    }

    void Reset() noexcept {
        head_ = 0;
        count_ = 0;
        buf_.fill(0.0);
    }

 private:
    std::array<double, kCapacity> buf_{};
    std::size_t head_{0};
    std::size_t count_{0};
};

}  // namespace stcpp::eval
