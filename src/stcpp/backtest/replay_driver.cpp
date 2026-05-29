// src/stcpp/backtest/replay_driver.cpp — ReplayDriver v0.1 实现
//
// Owner: 小肖 (numerical-algorithms, A 系统工程部)
// last_review: 2026-05-29
//
// 红线:
//   R-12: hub_.Publish() 本身 noexcept + 原子 swap; 驱动线程不阻塞 vCPU0
//   R-20: data_source_ts_ns 来自合成事件自带 ts; 禁止用 now() 替代
//
// 数值稳定性注意:
//   - 合成 price: 用 std::sin 周期函数产生 ∈ (0,1); clamp 保证不越界
//   - imbalance: (bid_q - ask_q)/(bid_q + ask_q) 数值稳定, 无 catastrophic cancellation
//                (bid_q + ask_q 始终 > 0)
//   - microprice: (bid_q*ask_p + ask_q*bid_p)/(bid_q + ask_q) 同样分母恒正

#include "stcpp/backtest/replay_driver.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <numbers>
#include <thread>

namespace stcpp::backtest {

// ---------------------------------------------------------------------------
// 内部帮助函数
// ---------------------------------------------------------------------------

namespace {

// clamp price ∈ [lo, hi]
[[nodiscard]] inline double clamp_price(double p, double lo, double hi) noexcept {
    if (p < lo)
        return lo;
    if (p > hi)
        return hi;
    return p;
}

// 计算 microprice (数值稳定: 分母恒正)
[[nodiscard]] inline double compute_microprice(double bid_p, double bid_q, double ask_p,
                                               double ask_q) noexcept {
    const double sum = bid_q + ask_q;
    if (sum <= 0.0) {
        return 0.5 * (bid_p + ask_p);  // fallback: mid
    }
    return (bid_q * ask_p + ask_q * bid_p) / sum;
}

// 计算 imbalance ∈ [-1, 1] (数值稳定: 分母恒正)
[[nodiscard]] inline double compute_imbalance(double bid_q, double ask_q) noexcept {
    const double sum = bid_q + ask_q;
    if (sum <= 0.0)
        return 0.0;
    return (bid_q - ask_q) / sum;
}

}  // namespace

// ---------------------------------------------------------------------------
// MakeSyntheticFrame
//
// 合成逻辑:
//   phi = 2π * tick_idx / phase_period_ticks   (周期角)
//   mid = clamp(mid_center + amplitude * sin(phi), tick, 1-tick)
//   bid = clamp(mid - spread/2, tick, 1-tick)
//   ask = clamp(mid + spread/2, tick, 1-tick)
//
// 5 档深度 (对称三角形):
//   L0: base_size
//   L1: base_size * 0.6
//   L2: base_size * 0.35
//   L3: base_size * 0.15
//   L4: NaN (无深度)
//
// bid_q / ask_q 交替增减以产生可观测 imbalance 波动:
//   bid_size_factor = 1.0 + 0.4 * sin(phi + π/4)  (略超前于 mid 周期)
//   ask_size_factor = 1.0 - 0.4 * sin(phi + π/4)
//
// R-20:
//   event_ts_ns       = base_event_ts_ns + tick_idx * tick_interval_ns
//   data_source_ts_ns = event_ts_ns + 1'000'000  (模拟 1ms 网络延迟; 严格 >= event_ts)
//   ingestion_ts_ns   = clock_fn()               (本地 now; 允许作 ingestion)
//   as_of_ts_ns       = ingestion_ts_ns
// ---------------------------------------------------------------------------
OrderBookFeatures ReplayDriver::MakeSyntheticFrame(std::int64_t tick_idx) const noexcept {
    using stcpp::microstructure::kBookDepthLevels;

    const double kNaN = std::numeric_limits<double>::quiet_NaN();
    const double tick = syn_cfg_.tick;
    const double lo = tick;
    const double hi = 1.0 - tick;

    // 周期角
    const double phi = (2.0 * std::numbers::pi * static_cast<double>(tick_idx)) /
                       static_cast<double>(syn_cfg_.phase_period_ticks);

    // mid price (正弦振荡)
    const double mid = clamp_price(syn_cfg_.mid_center + syn_cfg_.amplitude * std::sin(phi), lo, hi);

    // bid / ask (symmetric spread)
    const double half_spread = syn_cfg_.spread * 0.5;
    const double bid0 = clamp_price(mid - half_spread, lo, hi);
    const double ask0 = clamp_price(mid + half_spread, lo, hi);

    // 深度调制 (略超前 π/4, 产生预测性 imbalance)
    const double size_phi = phi + std::numbers::pi / 4.0;
    const double bid_factor = 1.0 + 0.4 * std::sin(size_phi);
    const double ask_factor = 1.0 - 0.4 * std::sin(size_phi);

    const double bid0_q = syn_cfg_.base_size * bid_factor;
    const double ask0_q = syn_cfg_.base_size * ask_factor;

    // 深度衰减系数 (三角分布)
    static constexpr std::array<double, kBookDepthLevels> kDecay = {1.0, 0.6, 0.35, 0.15, 0.0};

    OrderBookFeatures f{};
    f.valid = true;

    for (std::size_t i = 0; i < kBookDepthLevels; ++i) {
        if (kDecay[i] <= 0.0) {
            // L4: 无深度
            f.bids[i] = {kNaN, 0.0};
            f.asks[i] = {kNaN, 0.0};
        } else {
            // price: 从 L0 向外偏移 i 个 tick
            const double bid_p = clamp_price(bid0 - static_cast<double>(i) * tick, lo, hi);
            const double ask_p = clamp_price(ask0 + static_cast<double>(i) * tick, lo, hi);
            f.bids[i] = {bid_p, bid0_q * kDecay[i]};
            f.asks[i] = {ask_p, ask0_q * kDecay[i]};
        }
    }

    // 微观结构派生
    f.mid = mid;
    f.microprice = compute_microprice(bid0, bid0_q, ask0, ask0_q);
    f.spread = ask0 - bid0;
    f.imbalance = compute_imbalance(bid0_q, ask0_q);

    // 序列号: tick_idx 自然递增 → 观测层可验证 seq 单调性
    f.sequence_no = tick_idx;
    f.gap_count = 0;
    f.wss_state = WssConnState::kConnected;

    // R-20: 4 ts (合成事件自带; 禁 now() 替代 data_source_ts)
    f.event_ts_ns = syn_cfg_.base_event_ts_ns + tick_idx * syn_cfg_.tick_interval_ns;
    f.data_source_ts_ns = f.event_ts_ns + 1'000'000LL;  // +1ms 网络延迟模拟
    f.ingestion_ts_ns = clock_fn_();                    // 本地采时 (ingestion 允许)
    // ingestion 必须 >= data_source (正常情况下 steady_clock >> 2023 epoch)
    if (f.ingestion_ts_ns < f.data_source_ts_ns) {
        f.ingestion_ts_ns = f.data_source_ts_ns;
    }
    f.as_of_ts_ns = f.ingestion_ts_ns;

    return f;
}

// ---------------------------------------------------------------------------
// PublishHistoricalFrame
//
// 历史模式: 保留 frame 原始 4 ts, 仅更新 ingestion_ts_ns (重播入库时刻).
// R-20: data_source_ts_ns 来自 frame (不覆盖).
// ---------------------------------------------------------------------------
void ReplayDriver::PublishHistoricalFrame(const TickFrame& frame) noexcept {
    OrderBookFeatures f{};
    f.valid = true;

    f.bids = frame.bids;
    f.asks = frame.asks;

    f.microprice = frame.microprice;
    f.mid = frame.mid;
    f.spread = frame.spread;
    f.imbalance = frame.imbalance;
    f.sequence_no = frame.sequence_no;
    f.gap_count = frame.gap_count;
    f.wss_state = WssConnState::kConnected;

    // R-20: 保留原始 4 ts; ingestion 更新为当前重播时刻
    f.event_ts_ns = frame.event_ts_ns;
    f.data_source_ts_ns = frame.data_source_ts_ns;
    f.ingestion_ts_ns = clock_fn_();
    // clamp: ingestion >= data_source
    if (f.ingestion_ts_ns < f.data_source_ts_ns) {
        f.ingestion_ts_ns = f.data_source_ts_ns;
    }
    f.as_of_ts_ns = f.ingestion_ts_ns;

    hub_.Publish(frame.token_id, f);
    published_count_.fetch_add(1, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// DriveLoop — 核心驱动循环
// ---------------------------------------------------------------------------
std::int64_t ReplayDriver::DriveLoop(std::int64_t n_ticks, std::int64_t tick_sleep_ns) {
    std::int64_t driven = 0;

    if (mode_ == DriverMode::kSynthetic) {
        for (std::int64_t i = 0; i < n_ticks; ++i) {
            if (stop_.load(std::memory_order_acquire))
                break;

            const auto feat = MakeSyntheticFrame(i);
            hub_.Publish(token_id_, feat);
            published_count_.fetch_add(1, std::memory_order_relaxed);
            ++driven;

            if (tick_sleep_ns > 0) {
                std::this_thread::sleep_for(std::chrono::nanoseconds(tick_sleep_ns));
            }
        }
    } else {
        // kHistorical
        assert(frames_ != nullptr && "Historical mode requires frames pointer");
        const std::int64_t frame_count = static_cast<std::int64_t>(frames_->size());
        const std::int64_t limit = std::min(n_ticks, frame_count);
        for (std::int64_t i = 0; i < limit; ++i) {
            if (stop_.load(std::memory_order_acquire))
                break;
            PublishHistoricalFrame((*frames_)[static_cast<std::size_t>(i)]);
            ++driven;
            if (tick_sleep_ns > 0) {
                std::this_thread::sleep_for(std::chrono::nanoseconds(tick_sleep_ns));
            }
        }
    }

    return driven;
}

// ---------------------------------------------------------------------------
// RunSync
// ---------------------------------------------------------------------------
std::int64_t ReplayDriver::RunSync(std::int64_t n_ticks, std::int64_t tick_sleep_ns) {
    stop_.store(false, std::memory_order_release);
    return DriveLoop(n_ticks, tick_sleep_ns);
}

// ---------------------------------------------------------------------------
// RunAsync
// ---------------------------------------------------------------------------
void ReplayDriver::RunAsync(std::int64_t n_ticks, std::int64_t tick_sleep_ns) {
    stop_.store(false, std::memory_order_release);
    worker_ = std::thread([this, n_ticks, tick_sleep_ns] { DriveLoop(n_ticks, tick_sleep_ns); });
}

// ---------------------------------------------------------------------------
// WaitDone
// ---------------------------------------------------------------------------
void ReplayDriver::WaitDone() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

}  // namespace stcpp::backtest
