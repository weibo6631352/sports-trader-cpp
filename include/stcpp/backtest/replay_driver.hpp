// include/stcpp/backtest/replay_driver.hpp — ReplayDriver v0.1
//
// Owner: 小肖 (numerical-algorithms, A 系统工程部)
// last_review: 2026-05-29
//
// 架构说明:
//   ReplayDriver 是 OrderBookSnapshotHub 的合成/历史事件源.
//   它按可配置的 tick 间隔产生 OrderBookFeatures 并调用 hub_.Publish(),
//   令观测看板通过 hub_.Read() 看到流动变化的 book 数据.
//
//   两种驱动模式 (Mode 枚举, 构造时注入):
//     kSynthetic  — 按参数化函数合成 price_change 序列 (无外部数据依赖)
//     kHistorical — 从预加载的 TickFrame 向量回放历史数据 (M2 切真实 WSS 后降级为测试路径)
//
//   可注入性设计:
//     - M1: 主循环用 ReplayDriver 驱动 hub (本模块)
//     - M2: 主循环换成 PolymarketCLOBSubscriber 驱动 hub (hub 接口不变)
//           ReplayDriver 降为集成测试/回放测试专用路径
//
// R-12 合规:
//   - 驱动循环在独立线程 (非 vCPU0 WSS event loop)
//   - hub_.Publish() 本身 noexcept + 原子 swap, 满足 R-12
//   - 驱动线程与观测线程的通信仅通过 hub 双缓冲 (SWMR, 无锁)
//
// R-20 时间戳:
//   - kSynthetic: event_ts_ns / data_source_ts_ns 来自合成事件自带 ts (构造时传入 base_ts_ns)
//                 ingestion_ts_ns = 驱动线程 push 时刻 (由 clock_fn 注入, 可 mock)
//                 as_of_ts_ns     = ingestion_ts_ns (观测消费方读时可覆盖)
//                 禁止用 now() 替代 data_source_ts (R-20 红线)
//   - kHistorical: 保留 TickFrame 中预存的全部 4 ts; 驱动层不覆盖
//
// 依赖:
//   stcpp/polymarket/clob_wss/orderbook_snapshot_hub.hpp  (目标发布器)
//   stcpp/microstructure/orderbook.hpp                    (OrderBookLevel)
//
// 不依赖:
//   RM / signer / exec (零反向依赖, R-12)

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include "stcpp/microstructure/orderbook.hpp"
#include "stcpp/polymarket/clob_wss/orderbook_snapshot_hub.hpp"

namespace stcpp::backtest {

using stcpp::microstructure::kBookDepthLevels;
using stcpp::microstructure::OrderBookLevel;
using stcpp::polymarket::clob_wss::OrderBookFeatures;
using stcpp::polymarket::clob_wss::OrderBookSnapshotHub;
using stcpp::polymarket::clob_wss::WssConnState;

// ---------------------------------------------------------------------------
// TickFrame — 单个回放事件帧 (历史模式 SSOT)
//
// 对应 M1 设计文档 §6.2: 历史 Parquet 行的 C++ 内存形态.
// 全字段 4-ts 来自数据源; kHistorical 模式下驱动层直接透传, 不覆盖.
// ---------------------------------------------------------------------------
struct TickFrame {
    // R-20: 4 时间戳 (数据源原始 ts, 单调不递减)
    std::int64_t event_ts_ns{0};        // 原始市场事件时间
    std::int64_t data_source_ts_ns{0};  // Polymarket WSS payload 时间
    std::int64_t ingestion_ts_ns{0};    // 当初入库时间
    std::int64_t as_of_ts_ns{0};        // 当初决策点时间

    // 市场标识
    std::string token_id;

    // 5 档 bid/ask (index 0 = best; NaN = 无深度)
    std::array<OrderBookLevel, kBookDepthLevels> bids{};
    std::array<OrderBookLevel, kBookDepthLevels> asks{};

    // 派生微观结构 (已预计算)
    double microprice{std::numeric_limits<double>::quiet_NaN()};
    double mid{std::numeric_limits<double>::quiet_NaN()};
    double spread{std::numeric_limits<double>::quiet_NaN()};
    double imbalance{std::numeric_limits<double>::quiet_NaN()};

    // 序列号
    std::int64_t sequence_no{0};
    std::int64_t gap_count{0};
};

// ---------------------------------------------------------------------------
// SyntheticConfig — 合成模式参数
//
// 合成价格序列: bid/ask 在 [mid_center - amplitude, mid_center + amplitude]
// 之间按正弦波振荡, 流动性按对称三角分布铺 5 档.
//
// 数值稳定性注意:
//   - price ∈ (0, 1): 用 clamp 保证不溢出 tick lattice 边界
//   - size 用整数微单位避免浮点累积误差
//   - imbalance ∈ [-1, 1]: bid_size / ask_size 交替以产生可观测的 imbalance 变化
// ---------------------------------------------------------------------------
struct SyntheticConfig {
    double mid_center{0.60};   // 中间价基准 (∈ (0,1))
    double amplitude{0.05};    // 振幅 (mid ± amplitude, 不跨 (0,1) 边界)
    double spread{0.02};       // 固定 bid/ask spread (absolute)
    double base_size{1000.0};  // L1 USD 深度基准
    double tick{0.01};         // 最小价格单位

    // 时间基准 (event_ts_ns + i * tick_interval_ns 产生 data_source_ts_ns)
    std::int64_t base_event_ts_ns{1'000'000'000LL * 1'700'000'000LL};  // 2023-11-14 近似
    std::int64_t tick_interval_ns{100'000'000LL};                      // 100ms 间隔

    // 幅度调制: 每 phase_period_ticks 个 tick 完成一个正弦周期
    std::int64_t phase_period_ticks{20};
};

// ---------------------------------------------------------------------------
// DriverMode
// ---------------------------------------------------------------------------
enum class DriverMode : std::uint8_t {
    kSynthetic = 0,   // 合成序列 (无外部数据)
    kHistorical = 1,  // 历史 TickFrame 向量回放
};

// ---------------------------------------------------------------------------
// ReplayDriver — 主类
//
// 接口契约:
//   1. 构造:  ReplayDriver(hub, mode, [config/frames])
//   2. 启动:  RunSync(n_ticks)       — 同步驱动 n 个 tick (用于测试/demo)
//             RunAsync(n_ticks)      — 后台线程驱动 (用于长跑 paper runtime M1)
//   3. 等待:  WaitDone()             — 等待 RunAsync 完成
//   4. 停止:  Stop()                 — 发送停止信号 (RunAsync 提前退出)
//   5. 注入性: clock_fn 可 mock (测试注入确定性 now())
//
// M2 切换路径:
//   主循环替换为 PolymarketCLOBSubscriber → hub.Publish() 时,
//   ReplayDriver 退为:  仅在 STCPP_EXEC_MODE=paper 的集成测试中使用.
//   hub 接口完全不变, 观测层/debug_api 零感知.
// ---------------------------------------------------------------------------
class ReplayDriver {
public:
    // 时钟注入函数类型 (默认用 steady_clock; 测试可注入确定性 mock)
    using ClockFn = std::function<std::int64_t()>;

    // 默认时钟: steady_clock (仅用于 ingestion_ts_ns, 不替代 data_source_ts)
    static std::int64_t DefaultClockNs() noexcept {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    // ------------------------------------------------------------------
    // 合成模式构造
    // ------------------------------------------------------------------
    ReplayDriver(OrderBookSnapshotHub& hub, std::string token_id, SyntheticConfig cfg,
                 ClockFn clock_fn = DefaultClockNs)
        : hub_(hub),
          token_id_(std::move(token_id)),
          mode_(DriverMode::kSynthetic),
          syn_cfg_(std::move(cfg)),
          clock_fn_(std::move(clock_fn)) {}

    // ------------------------------------------------------------------
    // 历史模式构造 (M2 降级路径)
    //
    // frames: 调用方持有生命周期; ReplayDriver 仅存引用 (不拷贝大向量)
    // ------------------------------------------------------------------
    ReplayDriver(OrderBookSnapshotHub& hub, std::vector<TickFrame> const& frames,
                 ClockFn clock_fn = DefaultClockNs)
        : hub_(hub),
          token_id_(""),
          mode_(DriverMode::kHistorical),
          frames_(&frames),
          clock_fn_(std::move(clock_fn)) {}

    // 删除拷贝/移动 (含 atomic stop_flag)
    ReplayDriver(const ReplayDriver&) = delete;
    ReplayDriver& operator=(const ReplayDriver&) = delete;
    ReplayDriver(ReplayDriver&&) = delete;
    ReplayDriver& operator=(ReplayDriver&&) = delete;

    ~ReplayDriver() { Stop(); }

    // ------------------------------------------------------------------
    // RunSync — 同步驱动 n_ticks 个 tick, 当前线程阻塞直到完成
    //
    // tick_sleep_ns: tick 间隔 (ns). 0 = 无 sleep (最快回放, 适合测试)
    // 返回: 实际发布的 tick 数 (可能 < n_ticks 若历史帧不足)
    // ------------------------------------------------------------------
    std::int64_t RunSync(std::int64_t n_ticks, std::int64_t tick_sleep_ns = 0);

    // ------------------------------------------------------------------
    // RunAsync — 后台线程驱动
    // ------------------------------------------------------------------
    void RunAsync(std::int64_t n_ticks, std::int64_t tick_sleep_ns = 10'000'000LL /* 10ms */);

    // ------------------------------------------------------------------
    // WaitDone — 等待 RunAsync 完成 (blocking)
    // ------------------------------------------------------------------
    void WaitDone();

    // ------------------------------------------------------------------
    // Stop — 发送停止信号 (RunAsync 在当前 tick 完成后退出)
    // ------------------------------------------------------------------
    void Stop() noexcept {
        stop_.store(true, std::memory_order_release);
        WaitDone();
    }

    // ------------------------------------------------------------------
    // 统计
    // ------------------------------------------------------------------
    [[nodiscard]] std::int64_t published_count() const noexcept {
        return published_count_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] DriverMode mode() const noexcept { return mode_; }

private:
    // ------------------------------------------------------------------
    // MakeSyntheticFrame — 合成第 tick_idx 帧的 OrderBookFeatures
    //
    // 数值稳定性:
    //   使用 std::sin(2π * tick_idx / phase_period) 产生 ∈ [-1,1] 周期振荡.
    //   price 用 clamp 强制 ∈ [tick, 1-tick].
    //   imbalance 通过 bid_size / ask_size 比 (≠1) 体现, 不直接计算除法 mid.
    // R-20:
    //   event_ts_ns      = base_event_ts_ns + tick_idx * tick_interval_ns
    //   data_source_ts_ns = event_ts_ns + 1  (模拟网络延迟 1ns; ≥ event_ts)
    //   ingestion_ts_ns  = clock_fn() 调用结果 (本地 now, 允许作 ingestion_ts)
    //   as_of_ts_ns      = ingestion_ts_ns   (观测消费方可在读时覆盖)
    // ------------------------------------------------------------------
    [[nodiscard]] OrderBookFeatures MakeSyntheticFrame(std::int64_t tick_idx) const noexcept;

    // ------------------------------------------------------------------
    // PublishFrame — 从 TickFrame 转换为 OrderBookFeatures 并 Publish
    // R-20: 全部 4 ts 来自 frame (不覆盖, 不用 now())
    //       ingestion_ts_ns 使用 clock_fn() 更新 (允许: 这是重播入库时刻)
    // ------------------------------------------------------------------
    void PublishHistoricalFrame(const TickFrame& frame) noexcept;

    // core loop (called from both RunSync and thread)
    std::int64_t DriveLoop(std::int64_t n_ticks, std::int64_t tick_sleep_ns);

    OrderBookSnapshotHub& hub_;
    std::string token_id_;
    DriverMode mode_;
    SyntheticConfig syn_cfg_{};
    std::vector<TickFrame> const* frames_{nullptr};  // historical mode only
    ClockFn clock_fn_;

    std::atomic<bool> stop_{false};
    std::atomic<std::int64_t> published_count_{0};
    std::thread worker_;
};

}  // namespace stcpp::backtest
