// stcpp/execution/execution_mode.hpp — ExecutionMode build-time switch + context singleton
//
// 落:
//   xiaojiang-paper-engine-skeleton-v1.md §2 (Mode A++ Bernoulli + 3 mock 接口)
//   laozhou-trade-orchestrator-cpp-v0.5.md §18 (ExecutionMode.Paper)
//   ADR gm-signoff-paper-trade  R-7 (build-time 锁, 三 mode 同进程不切换)
//
// 红线:
//   R-7  ExecutionMode build-time 锁; 顶层 CMake 选 live/paper/backtest 三选一,
//        编译期 macro STCPP_EXEC_MODE_{LIVE|PAPER|BACKTEST} 一次注入, runtime 不切换.
//        ExecutionContext 单例 Init/Get; Init 双调 → std::abort (防 caller 二次注入伪 mode).
//   R-11 Live/Paper signer CMake target 物理隔离; paper signer .o 不进 live binary.
//        本头不直接依赖 signer 实现, 仅暴露 mode 标识.
//
// 不耻下问: CMake target 隔离 @老高, build-time macro 注入 @老何

#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <string_view>

namespace stcpp::execution {

enum class ExecutionMode : std::uint8_t {
    Live     = 0,
    Paper    = 1,
    Backtest = 2,
};

[[nodiscard]] constexpr std::string_view ToString(ExecutionMode m) noexcept {
    switch (m) {
        case ExecutionMode::Live:     return "live";
        case ExecutionMode::Paper:    return "paper";
        case ExecutionMode::Backtest: return "backtest";
    }
    return "unknown";
}

// 编译期默认 mode (CMake -DSTCPP_EXEC_MODE_PAPER=1 注入).
// 三 macro 互斥, 顶层 CMakeLists.txt 只设一个.
#if defined(STCPP_EXEC_MODE_live) || defined(STCPP_EXEC_MODE_LIVE)
inline constexpr ExecutionMode kCompiledMode = ExecutionMode::Live;
#elif defined(STCPP_EXEC_MODE_paper) || defined(STCPP_EXEC_MODE_PAPER)
inline constexpr ExecutionMode kCompiledMode = ExecutionMode::Paper;
#elif defined(STCPP_EXEC_MODE_backtest) || defined(STCPP_EXEC_MODE_BACKTEST)
inline constexpr ExecutionMode kCompiledMode = ExecutionMode::Backtest;
#else
// 缺省 paper (跑单测时 CMake 已传 STCPP_EXEC_MODE_paper=1; 兜底防爆)
inline constexpr ExecutionMode kCompiledMode = ExecutionMode::Paper;
#endif

// ExecutionContext: 进程级 singleton, 只携带 mode 标识 + 起始 ns (audit chain).
// Init() 双调 → abort (R-7 防 runtime 切换).
class ExecutionContext {
 public:
    static void Init(ExecutionMode m) noexcept {
        bool expected = false;
        if (!initialized_.compare_exchange_strong(expected, true,
                                                  std::memory_order_acq_rel)) {
            std::abort();  // R-7: 双调 = bug, 立即 abort 不放行
        }
        mode_ = m;
    }

    [[nodiscard]] static ExecutionMode Mode() noexcept { return mode_; }
    [[nodiscard]] static bool          IsInitialized() noexcept {
        return initialized_.load(std::memory_order_acquire);
    }

    // 单测专用 reset (生产代码勿调; 用 _internal_ 命名 + only-test 释义)
    static void ResetForTesting() noexcept {
        initialized_.store(false, std::memory_order_release);
        mode_ = kCompiledMode;
    }

 private:
    static inline std::atomic<bool> initialized_{false};
    static inline ExecutionMode     mode_{kCompiledMode};
};

}  // namespace stcpp::execution
