// include/stcpp/ml/game_score_history.hpp — 比分时序环形缓冲 (game 侧, PIT-safe BR-1)
//
// Owner: 老雷 (GM) — 批1 体育动态特征 (体育市场主权: 进球后 30-120s = edge 最浓时段)
// last_review: 2026-05-31
//
// 跟踪每个 condition 的比分轨迹, 派生「进球新鲜度 / 比分动量 / 领先变化」。数据 = game_row.score
// (现有, 非想象)。goal_freshness 用 as_of_ts (上游观测刻, 禁 now()) → PIT-safe。
//   进球后 LP 30-120s 没调价 = in-play 延迟 edge 核心窗口; goal_freshness 标这个窗口。
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace stcpp::ml {

class GameScoreHistory {
public:
    static constexpr std::size_t kCapacity = 64;  // 比分变化稀疏, 64 足够

    struct Sample {
        std::int64_t ts_ns{0};
        std::int32_t score_diff{0};  // home − away
    };

    GameScoreHistory() = default;

    // Observe — 每 tick (有真实比分时) 观测一次。检测比分变化 → 记最近进球 ts。
    //   ts = as_of_ts (上游观测刻); 比分不变也调 (last_ts 更新), 但 last_change 仅变化时更新。
    void Observe(std::int64_t ts_ns, std::int32_t score_home, std::int32_t score_away) noexcept {
        if (ts_ns < last_ts_) return;  // 乱序保护
        const std::int32_t diff = score_home - score_away;
        if (count_ == 0 || score_home != last_home_ || score_away != last_away_) {
            last_change_ts_ = ts_ns;  // 进球/比分变化
        }
        last_home_ = score_home;
        last_away_ = score_away;
        last_ts_ = ts_ns;
        // ring (仅在 ts 推进时存, 防停滞重复; 比分动量用)
        if (count_ == 0 || ts_ns > buf_[(head_ + kCapacity - 1) % kCapacity].ts_ns) {
            buf_[head_] = Sample{ts_ns, diff};
            head_ = (head_ + 1) % kCapacity;
            if (count_ < kCapacity) ++count_;
        }
        have_ = true;
    }

    [[nodiscard]] bool valid() const noexcept { return have_; }

    // 进球新鲜度: exp(−(now − 最近变化)/half_life)。1=刚进球, →0=很久没进。half_life 默认 120s。
    //   now_ns = as_of_ts (PIT, 禁 now())。无观测 → NaN。
    [[nodiscard]] double GoalFreshness(std::int64_t now_ns, double half_life_sec = 120.0) const noexcept {
        if (!have_ || half_life_sec <= 0.0) return kNaN();
        const double dt_sec = static_cast<double>(now_ns - last_change_ts_) / 1e9;
        if (dt_sec < 0.0) return kNaN();
        return std::exp(-dt_sec / half_life_sec);
    }

    // 净动量: score_diff_now − score_diff_{≤now−window 最近}。窗口内 < 2 样本 → NaN。
    [[nodiscard]] double NetMomentum(std::int64_t window_ns) const noexcept {
        if (count_ < 1 || window_ns <= 0) return kNaN();
        const std::int64_t cutoff = last_ts_ - window_ns;
        const Sample* first = nullptr;
        for (std::size_t i = 0; i < count_; ++i) {
            const Sample& s = at_(i);
            if (s.ts_ns >= cutoff) {
                first = &s;
                break;
            }
        }
        if (first == nullptr) return kNaN();
        const Sample& last = at_(count_ - 1);
        return static_cast<double>(last.score_diff - first->score_diff);
    }

    void Reset() noexcept {
        head_ = 0;
        count_ = 0;
        last_ts_ = 0;
        last_change_ts_ = 0;
        last_home_ = 0;
        last_away_ = 0;
        have_ = false;
    }

private:
    static constexpr double kNaN() noexcept { return std::numeric_limits<double>::quiet_NaN(); }
    [[nodiscard]] const Sample& at_(std::size_t logical) const noexcept {
        const std::size_t oldest = (head_ + kCapacity - count_) % kCapacity;
        return buf_[(oldest + logical) % kCapacity];
    }

    std::array<Sample, kCapacity> buf_{};
    std::size_t head_{0};
    std::size_t count_{0};
    std::int64_t last_ts_{0};
    std::int64_t last_change_ts_{0};
    std::int32_t last_home_{0};
    std::int32_t last_away_{0};
    bool have_{false};
};

}  // namespace stcpp::ml
