// include/stcpp/ml/sharp_fair_track.hpp — sharp fair 时序环 (PIT-safe, line-movement 一等公民)
//
// Owner: 老雷 (GM) — 赔率源时序地基 (老板 2026-06-05「方向真值=赔率源 sharp; 盘口趋势/line movement
//   用一阶导, 不只用瞬时快照」)
// last_review: 2026-06-05
//
// 背景 / 为什么单独建环 (不复用 ml::FeatureHistory):
//   研究确认 (量化组独立印证): microprice 有 ts_history_ 环、比分有 game_history_ 环, 唯独最关键的
//   【赔率源 sharp fair】没有时序环 —— 它的轨迹 (velocity) 就是"真值的运动", 是最可靠的方向信号。
//   FeatureHistory 存单价 (microprice) + bid/ask, 派生 OFI/depth 等; 强行复用会污染其语义 (best_bid
//   被借去存 mid)。故建本专用环, 存 (ts, sharp, mid) 双序列, 派生 sharp 速度 + 市场价相对 sharp 的
//   收敛/发散率。零触碰 FeatureHistory (它服务关键 microprice 链路, 不冒险)。
//
// 红线 / 契约 (与 FeatureHistory 同纪律; 做歪一个 tick 的前视 → 回测金光实盘亏穿):
//   PIT: 只 push 已观测样本; ts = 上游 sharp 赔率版本时刻 (Goalserve inplay updated_ts = data_source_ts;
//     禁本地 now())。窗口 [as_of−W, as_of] 只含过去样本。as_of = 最新样本 ts。
//   BR-1: 纯逻辑 / 无 IO / 无锁 / 无 now()。回测按事件序 replay 喂同一组件 → 派生逐位一致。
//   R-12: 定长 ring (kCapacity), 无堆分配 / 无 unbounded 扫描; loop_thread_ 单 writer 无锁。
//   单调: ts 须单调递增; ts ≤ last 的乱序/重复样本跳过 (PIT 保护 + 防停滞 feed 灌重复版本)。
//
// 缺失语义: 样本不足 (<2 有效) → NaN (与 ml::FeatureHistory / MlFeature NaN 约定一致)。
//
// 派生 (全部观测先行; Stage 1 不驱动任何交易决策 — 接进加/减/止盈/止损是 Stage 2, 另行设计+回测):
//   Velocity(w)        sharp 速度 (prob/sec; 有符号; +升 −降) = sharp line movement。
//   ConvergenceRate(w) (|gap_last|−|gap_first|)/Δsec, gap=sharp−mid; <0=市场向 sharp 收敛(持仓变对),
//                      >0=发散(变错)。这是"持仓对错"的实时判据。
//   Vol(w)             窗口内相邻 sharp 变化 RMS (prob; sharp 抖动度) — Stage 2 sizing 稳定性门用。
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace stcpp::ml {

// SharpFairTrack — 单 instrument 的 sharp fair + PM mid 定长时序环 (PIT-safe, BR-1)。
class SharpFairTrack {
 public:
    // feed ~2s/版 → 64 样本覆盖 ~2min 窗口余量 (Velocity/Conv 默认窗 10s ≈ 5 样本)。
    static constexpr std::size_t kCapacity = 64;

    struct Sample {
        std::int64_t ts_ns{0};  // 上游 sharp 赔率版本时刻 (PIT 锚; 禁 now())
        double sharp{0.0};      // YES-canonical sharp fair ∈ (0,1) (de-vig 胜率)
        double mid{0.0};        // 同刻 PM 市场 mid (microprice; 收敛/发散用)
    };

    SharpFairTrack() = default;

    // Push — 推入一个已观测样本 (loop_thread_ / replay 单 writer)。
    //   sharp 必须有效概率 ∈(0,1) 才记 (无效 sharp 不污染轨迹)。mid 无效 → 退化存 = sharp (gap=0)。
    //   PIT 保护: ts ≤ last_ts_ (乱序/重复版本) → 跳过。
    void Push(std::int64_t ts_ns, double sharp, double mid) noexcept {
        if (ts_ns <= last_ts_) return;             // 单调 + 去重 (PIT)
        if (!(sharp > 0.0 && sharp < 1.0)) return;  // sharp 必须有效概率
        buf_[head_] = Sample{ts_ns, sharp, std::isfinite(mid) ? mid : sharp};
        head_ = (head_ + 1) % kCapacity;
        if (count_ < kCapacity) ++count_;
        last_ts_ = ts_ns;
    }

    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] bool empty() const noexcept { return count_ == 0; }
    [[nodiscard]] std::int64_t last_ts_ns() const noexcept { return last_ts_; }

    // 窗口内样本数 (观测质量代理; <2 则 Velocity/Conv 为 NaN)。
    [[nodiscard]] std::size_t WindowSampleCount(std::int64_t window_ns) const noexcept {
        if (count_ == 0 || window_ns <= 0) return 0;
        const std::int64_t cutoff = last_ts_ - window_ns;
        std::size_t n = 0;
        for (std::size_t i = 0; i < count_; ++i)
            if (at_(i).ts_ns >= cutoff) ++n;
        return n;
    }

    // sharp 速度 (prob/sec): (sharp_last − sharp_first_in_window) / Δsec。窗口内 <2 样本 → NaN。
    //   +升 −降 = sharp line movement 方向 (老板钦定最可靠方向信号)。
    [[nodiscard]] double Velocity(std::int64_t window_ns) const noexcept {
        const Sample* f = first_in_window_(window_ns);
        const Sample* l = newest_();
        if (f == nullptr || l == nullptr || f->ts_ns >= l->ts_ns) return kNaN();
        const double dt = static_cast<double>(l->ts_ns - f->ts_ns) / 1e9;
        return dt > 0.0 ? (l->sharp - f->sharp) / dt : kNaN();
    }

    // 收敛率 (prob/sec): (|gap_last| − |gap_first|)/Δsec; gap = sharp − mid。
    //   <0 = 市场向 sharp 收敛 (持仓正在变对); >0 = 发散 (变错); ≈0 = 震荡。窗口内 <2 → NaN。
    [[nodiscard]] double ConvergenceRate(std::int64_t window_ns) const noexcept {
        const Sample* f = first_in_window_(window_ns);
        const Sample* l = newest_();
        if (f == nullptr || l == nullptr || f->ts_ns >= l->ts_ns) return kNaN();
        const double dt = static_cast<double>(l->ts_ns - f->ts_ns) / 1e9;
        if (dt <= 0.0) return kNaN();
        return (std::abs(l->sharp - l->mid) - std::abs(f->sharp - f->mid)) / dt;
    }

    // sharp 波动 (prob): 窗口内相邻 sharp 变化 RMS (零均值惯例)。高 = sharp 抖动剧烈 (噪声多于真移动)。
    [[nodiscard]] double Vol(std::int64_t window_ns) const noexcept {
        if (count_ < 2 || window_ns <= 0) return kNaN();
        const std::int64_t cutoff = last_ts_ - window_ns;
        double sum_sq = 0.0;
        std::size_t n_diff = 0;
        bool have_prev = false;
        double prev = 0.0;
        for (std::size_t i = 0; i < count_; ++i) {
            const Sample& s = at_(i);
            if (s.ts_ns < cutoff) continue;
            if (have_prev) {
                const double d = s.sharp - prev;
                sum_sq += d * d;
                ++n_diff;
            }
            prev = s.sharp;
            have_prev = true;
        }
        return n_diff == 0 ? kNaN() : std::sqrt(sum_sq / static_cast<double>(n_diff));
    }

    // 最新 |gap| = |sharp − mid| (mispricing 幅度; 单点, 无窗口)。空 → NaN。
    [[nodiscard]] double LastGap() const noexcept {
        const Sample* l = newest_();
        return l ? std::abs(l->sharp - l->mid) : kNaN();
    }

    void Reset() noexcept {
        head_ = 0;
        count_ = 0;
        last_ts_ = 0;
    }

 private:
    static constexpr double kNaN() noexcept { return std::numeric_limits<double>::quiet_NaN(); }

    // 逻辑索引 i (0=最旧, count_-1=最新) → 物理 buf_ 下标。
    [[nodiscard]] const Sample& at_(std::size_t logical) const noexcept {
        const std::size_t oldest = (head_ + kCapacity - count_) % kCapacity;
        return buf_[(oldest + logical) % kCapacity];
    }
    [[nodiscard]] const Sample* newest_() const noexcept {
        return count_ != 0 ? &at_(count_ - 1) : nullptr;
    }
    // 窗口内最旧样本 (oldest s.ts ≥ last−window)。<2 总样本 → nullptr。
    [[nodiscard]] const Sample* first_in_window_(std::int64_t window_ns) const noexcept {
        if (count_ < 2 || window_ns <= 0) return nullptr;
        const std::int64_t cutoff = last_ts_ - window_ns;
        for (std::size_t i = 0; i < count_; ++i) {
            const Sample& s = at_(i);
            if (s.ts_ns >= cutoff) return &s;
        }
        return nullptr;
    }

    std::array<Sample, kCapacity> buf_{};
    std::size_t head_{0};
    std::size_t count_{0};
    std::int64_t last_ts_{0};
};

}  // namespace stcpp::ml
