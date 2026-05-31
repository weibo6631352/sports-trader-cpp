// include/stcpp/ml/feature_history.hpp — 时序特征环形缓冲 (PIT-safe, BR-1 共用)
//
// Owner: 老雷 (GM) — 时序地基 (老板 2026-05-31: 单点切片看不到变化率/波动率, 接时序入模型)
// last_review: 2026-05-31
//
// 目的:
//   单点截面无法表达"动态" (变化率 / 波动率 / 流动性演化)。本组件给每个 instrument 维护
//   一个定长时序环形缓冲, 从中派生窗口特征 (变化率 / realized vol), 喂模型 + 训练数据。
//   这是所有时序特征 (变化率/波动率, 后续"卖不出"流动性留存, 结算临近度) 的共用地基。
//
// 红线 / 契约 (做歪一个 tick 的前视 → 回测金光实盘亏穿):
//   PIT (ML-R8): 只 push 已观测样本; ts = 上游 data_source_ts_ns (禁本地 now())。
//     窗口 [as_of − W, as_of] 只含过去样本 (ring 从不存未来)。as_of = 最新样本 ts。
//   BR-1: 纯逻辑 / 无 IO / 无锁 / 无 now()。回测按事件序 replay 喂同一组件 → 派生特征逐位一致。
//     调用方 (paper_loop live / backtest replay) 保证 push-then-read 的事件序; 组件本身无时间观念。
//   R-12: 定长 ring (kCapacity 上限), 无堆分配 / 无 unbounded 扫描; loop_thread_ 单 writer 无锁。
//   单调: ts 须单调递增 (事件序); ts ≤ last 的乱序/重复样本跳过 (PIT 保护 + 防停滞 book 灌重复)。
//
// 缺失语义: 样本不足 → NaN (与 ml::FeatureVector / MlFeature NaN 约定一致)。
//
// v0.1 范围 (老板「先 2 个最稳时序特征」): microprice 变化率 (per sec) + realized vol (窗口内
//   相邻微价变化 RMS)。样本只存 (ts, microprice)。后续"卖不出"流动性留存特征 → 扩 Sample 即可
//   (per-token ephemeral state, 无迁移)。
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace stcpp::ml {

// FeatureHistory — 单 instrument 定长时序环形缓冲 + 窗口派生 (PIT-safe, BR-1 共用)。
class FeatureHistory {
public:
    // 定长 ring (R-12 bounded)。book 更新 ~1-5s/次, 128 样本覆盖 ~2-10min 窗口余量。
    //   窗口跨度若超 ring 容量 (高频更新) → 派生只用 ring 内最近 kCapacity 样本 (容量内最长窗)。
    static constexpr std::size_t kCapacity = 128;

    struct Sample {
        std::int64_t ts_ns{0};   // 上游 data_source_ts_ns (PIT 锚; 禁 now())
        double microprice{0.0};  // 被观测微价 ∈ (0,1)
    };

    FeatureHistory() = default;

    // Push — 推入一个已观测样本 (loop_thread_ / replay 单 writer)。
    //   PIT 保护: ts ≤ last_ts_ (乱序/重复) → 跳过 (停滞 book 重复读不污染 vol; 事件序违规不入)。
    //   microprice 非有限 / 越界 (0,1) → 跳过 (脏样本不入缓冲)。
    void Push(std::int64_t ts_ns, double microprice) noexcept {
        if (ts_ns <= last_ts_) return;  // 单调 + 去重 (PIT)
        if (!std::isfinite(microprice) || microprice <= 0.0 || microprice >= 1.0) return;
        buf_[head_] = Sample{ts_ns, microprice};
        head_ = (head_ + 1) % kCapacity;
        if (count_ < kCapacity) ++count_;
        last_ts_ = ts_ns;
    }

    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] bool empty() const noexcept { return count_ == 0; }
    [[nodiscard]] std::int64_t last_ts_ns() const noexcept { return last_ts_; }

    // 窗口内样本数 (as_of = 最新样本; 回看 window_ns)。观测质量代理 (特征可靠性)。
    [[nodiscard]] std::size_t WindowSampleCount(std::int64_t window_ns) const noexcept {
        if (count_ == 0 || window_ns <= 0) return 0;
        const std::int64_t cutoff = last_ts_ - window_ns;
        std::size_t n = 0;
        for (std::size_t i = 0; i < count_; ++i) {
            if (at_(i).ts_ns >= cutoff) ++n;
        }
        return n;
    }

    // 变化率 (prob/sec): (mp_last − mp_first_in_window) / Δsec。
    //   窗口内 < 2 样本 或 Δt ≤ 0 → NaN。as_of = 最新样本 ts (无前视)。
    [[nodiscard]] double RateOfChangePerSec(std::int64_t window_ns) const noexcept {
        if (count_ < 2 || window_ns <= 0) return kNaN();
        const std::int64_t cutoff = last_ts_ - window_ns;
        // 最旧的"窗口内"样本 (oldest→newest 遍历, 第一个 ts≥cutoff)。
        const Sample* first = nullptr;
        for (std::size_t i = 0; i < count_; ++i) {
            const Sample& s = at_(i);
            if (s.ts_ns >= cutoff) {
                first = &s;
                break;
            }
        }
        const Sample& last = at_(count_ - 1);
        if (first == nullptr || first->ts_ns >= last.ts_ns) return kNaN();
        const double dt_sec = static_cast<double>(last.ts_ns - first->ts_ns) / 1e9;
        if (dt_sec <= 0.0) return kNaN();
        return (last.microprice - first->microprice) / dt_sec;
    }

    // realized vol (prob 单位): 窗口内相邻微价变化的 RMS = sqrt(mean(Δp_i^2))。
    //   零均值假设 (金融 realized vol 惯例); 样本数无关 (per-update 口径, 非 sum)。
    //   prob 价用绝对变化 (非 return), 规避 p→0/1 的 return 爆炸。窗口内 < 2 样本 → NaN。
    [[nodiscard]] double RealizedVol(std::int64_t window_ns) const noexcept {
        if (count_ < 2 || window_ns <= 0) return kNaN();
        const std::int64_t cutoff = last_ts_ - window_ns;
        double sum_sq = 0.0;
        std::size_t n_diff = 0;
        bool have_prev = false;
        double prev = 0.0;
        for (std::size_t i = 0; i < count_; ++i) {
            const Sample& s = at_(i);
            if (s.ts_ns < cutoff) continue;  // 窗口外不计 (PIT)
            if (have_prev) {
                const double d = s.microprice - prev;
                sum_sq += d * d;
                ++n_diff;
            }
            prev = s.microprice;
            have_prev = true;
        }
        if (n_diff == 0) return kNaN();
        return std::sqrt(sum_sq / static_cast<double>(n_diff));
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
        // 最旧物理位置: head_ - count_ (mod kCapacity)。
        const std::size_t oldest = (head_ + kCapacity - count_) % kCapacity;
        return buf_[(oldest + logical) % kCapacity];
    }

    std::array<Sample, kCapacity> buf_{};
    std::size_t head_{0};       // 下一个写入位置
    std::size_t count_{0};      // 当前样本数 (≤ kCapacity)
    std::int64_t last_ts_{0};   // 最新样本 ts (单调门)
};

}  // namespace stcpp::ml
