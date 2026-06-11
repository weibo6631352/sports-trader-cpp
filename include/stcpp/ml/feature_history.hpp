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
//     调用方 (trading_loop live / backtest replay) 保证 push-then-read 的事件序; 组件本身无时间观念。
//   R-12: 定长 ring (kCapacity 上限), 无堆分配 / 无 unbounded 扫描; loop_thread_ 单 writer 无锁。
//   单调: ts 须单调递增 (事件序); ts ≤ last 的乱序/重复样本跳过 (PIT 保护 + 防停滞 book 灌重复)。
//
// 缺失语义: 样本不足 → NaN (与 ml::FeatureVector / MlFeature NaN 约定一致)。
//
// 范围:
//   slice-1 (老板「先 2 个最稳」): microprice 变化率 (per sec) + realized vol (相邻微价变化 RMS)。
//   slice-2 「卖不出」(老板「希望被量化模型包含」, 非硬门): 无 bid 占比 + 退出深度均值。
//     observe-always: 样本含无 bid/无价的 tick (卖不出正是要观测的事件); 价 derive 跳过无效价,
//     流动性 derive 用 bid 字段。两特征喂模型, 退出流动性如何影响规模由模型/小梁学, 不在此设硬 gate。
//   样本存 (ts, microprice, best_bid, best_bid_size)。后续 (结算临近度) 再扩。
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
        std::int64_t ts_ns{0};      // 上游 data_source_ts_ns (PIT 锚; 禁 now())
        double microprice{0.0};     // 被观测微价 (可 NaN/越界: 单边/无价时; 价 derive 自动跳过)
        double best_bid{0.0};       // 退出价 (卖出触价; ≤0 = 无 bid)
        double best_bid_size{0.0};  // 退出深度 (bid L1 size pUSD; ≤0 = 无 bid = 卖不出)
        double best_ask{0.0};       // 买入触价 (≤0 = 无 ask); OFI/dislocation 用
        double best_ask_size{0.0};  // ask L1 size pUSD; OFI (Δbid−Δask) 用
    };

    FeatureHistory() = default;

    // Push — 推入一个已观测样本 (loop_thread_ / replay 单 writer)。**observe-always**: 即便
    //   价/bid 无效也记录 —— 「卖不出」(bid 没了) 正是要观测的事件, 提前丢弃 = 特征被审查偏置。
    //   PIT 保护: ts ≤ last_ts_ (乱序/重复) → 跳过 (停滞 book 重复读不污染; 事件序违规不入)。
    //   价 derive (变化率/vol) 内部跳过 microprice ∉(0,1) 样本; 流动性 derive 用 bid 字段 (含无 bid)。
    void Push(std::int64_t ts_ns, double microprice, double best_bid, double best_bid_size,
              double best_ask = 0.0, double best_ask_size = 0.0) noexcept {
        if (ts_ns <= last_ts_) return;  // 单调 + 去重 (PIT)
        buf_[head_] = Sample{ts_ns, microprice, best_bid, best_bid_size, best_ask, best_ask_size};
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

    // 变化率 (prob/sec): (mp_last − mp_first_in_window) / Δsec。窗口内**有效价** < 2 → NaN。
    //   有效价 = microprice ∈ (0,1) (单边/无价样本跳过)。as_of = 最新样本 ts (无前视)。
    [[nodiscard]] double RateOfChangePerSec(std::int64_t window_ns) const noexcept {
        if (count_ < 2 || window_ns <= 0) return kNaN();
        const std::int64_t cutoff = last_ts_ - window_ns;
        const Sample* first = nullptr;  // 窗口内最旧有效价
        const Sample* last = nullptr;   // 窗口内最新有效价
        for (std::size_t i = 0; i < count_; ++i) {
            const Sample& s = at_(i);
            if (s.ts_ns < cutoff || !price_valid_(s.microprice)) continue;
            if (first == nullptr) first = &s;
            last = &s;
        }
        if (first == nullptr || last == nullptr || first->ts_ns >= last->ts_ns) return kNaN();
        const double dt_sec = static_cast<double>(last->ts_ns - first->ts_ns) / 1e9;
        if (dt_sec <= 0.0) return kNaN();
        return (last->microprice - first->microprice) / dt_sec;
    }

    // realized vol (prob 单位): 窗口内相邻**有效价**变化的 RMS = sqrt(mean(Δp_i^2))。
    //   零均值假设 (金融 realized vol 惯例); 样本数无关 (per-update 口径, 非 sum)。
    //   prob 价用绝对变化 (非 return), 规避 p→0/1 的 return 爆炸。窗口内 < 2 有效价 → NaN。
    [[nodiscard]] double RealizedVol(std::int64_t window_ns) const noexcept {
        if (count_ < 2 || window_ns <= 0) return kNaN();
        const std::int64_t cutoff = last_ts_ - window_ns;
        double sum_sq = 0.0;
        std::size_t n_diff = 0;
        bool have_prev = false;
        double prev = 0.0;
        for (std::size_t i = 0; i < count_; ++i) {
            const Sample& s = at_(i);
            if (s.ts_ns < cutoff || !price_valid_(s.microprice)) continue;  // 窗口外/无效价跳过 (PIT)
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

    // ---- slice-2 「卖不出」流动性留存 (老板: 量化模型包含, 非硬门) ----
    //   两特征喂模型, 让模型学退出流动性对定价/规模的影响 (不在此设硬 gate; sizing 用不用由模型/小梁定)。

    // 无 bid 占比 ∈ [0,1]: 窗口内无可执行 bid (best_bid≤0 或 size≤0) 的样本占比。
    //   = 1.0 → 整窗都卖不出 (单边倒挂); = 0 → 一直有退出流动性。窗口内 0 样本 → NaN。
    [[nodiscard]] double BidAbsenceFrac(std::int64_t window_ns) const noexcept {
        if (count_ == 0 || window_ns <= 0) return kNaN();
        const std::int64_t cutoff = last_ts_ - window_ns;
        std::size_t total = 0, absent = 0;
        for (std::size_t i = 0; i < count_; ++i) {
            const Sample& s = at_(i);
            if (s.ts_ns < cutoff) continue;
            ++total;
            if (!(s.best_bid > 0.0) || !(s.best_bid_size > 0.0)) ++absent;
        }
        if (total == 0) return kNaN();
        return static_cast<double>(absent) / static_cast<double>(total);
    }

    // 退出深度均值 (pUSD): 窗口内 best_bid_size 均值 (无 bid 计 0)。低 = 难卖出 (退出流动性薄)。
    //   窗口内 0 样本 → NaN。模型/sizing 可据此对「进得去出不来」的仓位定更小目标 (由模型学, 不硬钳)。
    [[nodiscard]] double ExitDepthMean(std::int64_t window_ns) const noexcept {
        if (count_ == 0 || window_ns <= 0) return kNaN();
        const std::int64_t cutoff = last_ts_ - window_ns;
        double sum = 0.0;
        std::size_t total = 0;
        for (std::size_t i = 0; i < count_; ++i) {
            const Sample& s = at_(i);
            if (s.ts_ns < cutoff) continue;
            ++total;
            sum += (s.best_bid_size > 0.0 && std::isfinite(s.best_bid_size)) ? s.best_bid_size : 0.0;
        }
        if (total == 0) return kNaN();
        return sum / static_cast<double>(total);
    }

    // ---- 批1 微结构派生 (老郭 b_ 血缘; 小袁/小肖公式; 全部现有 ring 可派生) ----

    // Amihud 非流动性近似 (小肖 P1): mean(|Δmicroprice| / bid_size)。每单位退出深度的价格冲击,
    //   高 = 流动性薄 (小深度驱动大价动)。窗口内有效 (价∈(0,1) + bid>0) 相邻对 < 1 → NaN。
    [[nodiscard]] double AmihudApprox(std::int64_t window_ns) const noexcept {
        if (count_ < 2 || window_ns <= 0) return kNaN();
        const std::int64_t cutoff = last_ts_ - window_ns;
        double sum = 0.0;
        std::size_t n = 0;
        bool have_prev = false;
        double prev_mp = 0.0;
        for (std::size_t i = 0; i < count_; ++i) {
            const Sample& s = at_(i);
            if (s.ts_ns < cutoff || !price_valid_(s.microprice)) continue;
            if (have_prev && s.best_bid_size > 0.0) {
                sum += std::abs(s.microprice - prev_mp) / s.best_bid_size;
                ++n;
            }
            prev_mp = s.microprice;
            have_prev = true;
        }
        return (n == 0) ? kNaN() : sum / static_cast<double>(n);
    }

    // 退出深度波动 (小袁): 相邻 bid_size 变化的 RMS。高 = 流动性不稳定 (间歇幌子盘, 不可依赖)。
    [[nodiscard]] double BidDepthVol(std::int64_t window_ns) const noexcept {
        if (count_ < 2 || window_ns <= 0) return kNaN();
        const std::int64_t cutoff = last_ts_ - window_ns;
        double sum_sq = 0.0;
        std::size_t n = 0;
        bool have_prev = false;
        double prev = 0.0;
        for (std::size_t i = 0; i < count_; ++i) {
            const Sample& s = at_(i);
            if (s.ts_ns < cutoff) continue;
            if (have_prev) {
                const double d = s.best_bid_size - prev;
                sum_sq += d * d;
                ++n;
            }
            prev = s.best_bid_size;
            have_prev = true;
        }
        return (n == 0) ? kNaN() : std::sqrt(sum_sq / static_cast<double>(n));
    }

    // Order Flow Imbalance (Cont-Kukanov-Stoikov 2014, L1; 小袁/小程 P0 微结构最强信号):
    //   ΔW_bid = (bid≥bid_prev)·bid_size − (bid≤bid_prev)·bid_size_prev
    //   ΔW_ask = (ask≤ask_prev)·ask_size − (ask≥ask_prev)·ask_size_prev
    //   OFI_t = ΔW_bid − ΔW_ask; 窗口累加 (正=买压)。需双边有效 (bid/ask>0)。
    [[nodiscard]] double OFI(std::int64_t window_ns) const noexcept {
        if (count_ < 2 || window_ns <= 0) return kNaN();
        const std::int64_t cutoff = last_ts_ - window_ns;
        double ofi = 0.0;
        std::size_t n = 0;
        bool have_prev = false;
        double pb = 0.0, pbs = 0.0, pa = 0.0, pas = 0.0;
        for (std::size_t i = 0; i < count_; ++i) {
            const Sample& s = at_(i);
            if (s.ts_ns < cutoff) continue;
            const bool ok = s.best_bid > 0.0 && s.best_ask > 0.0;
            if (have_prev && ok) {
                const double dW_bid =
                    (s.best_bid >= pb ? s.best_bid_size : 0.0) - (s.best_bid <= pb ? pbs : 0.0);
                const double dW_ask =
                    (s.best_ask <= pa ? s.best_ask_size : 0.0) - (s.best_ask >= pa ? pas : 0.0);
                ofi += dW_bid - dW_ask;
                ++n;
            }
            if (ok) {
                pb = s.best_bid;
                pbs = s.best_bid_size;
                pa = s.best_ask;
                pas = s.best_ask_size;
                have_prev = true;
            }
        }
        return (n == 0) ? kNaN() : ofi;
    }

    void Reset() noexcept {
        head_ = 0;
        count_ = 0;
        last_ts_ = 0;
    }

private:
    static constexpr double kNaN() noexcept { return std::numeric_limits<double>::quiet_NaN(); }

    // 有效价: microprice ∈ (0,1) 且有限 (单边/无价样本 → false, 价 derive 跳过, 仍计流动性)。
    [[nodiscard]] static bool price_valid_(double mp) noexcept {
        return std::isfinite(mp) && mp > 0.0 && mp < 1.0;
    }

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
