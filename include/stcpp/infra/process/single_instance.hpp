// stcpp/infra/process/single_instance.hpp — 防多开 PID+flock RAII (W5 Wave 25)
//
// 落:
//   laozhou-single-instance-spec-v1.md §2 (PID file + flock 协议)
//   ADR gm-signoff-paper-trade R-7  (ExecutionMode build-time 物理隔离)
//   ADR R-11 (paper.pid ≠ live.pid, 物理隔离背书)
//   ADR R-12 (单实例锁仅启动期, 不进 WSS event loop)
//
// 红线:
//   R-7  PID file path 由 build-time STCPP_PID_DIR 决定, 不允许 runtime 覆盖
//   R-11 paper / live / backtest 三 path 物理隔离
//   R-12 SingleInstanceLock 构造在 main() 第一行, 不在 hot path 重复调用
//
// 平台: macOS (BSD flock 真实现) + Linux — 均走 flock(2), 不用 fcntl(F_SETLK)
// Windows: #error 拦截 (公司不部署)
//
// 不耻下问: O_CLOEXEC fd 泄漏 @老沈, fork+pipe test fixture @小宋

#pragma once

#ifndef __linux__
#    ifndef __APPLE__
#        error "SingleInstanceLock only supports Linux and macOS (POSIX flock)"
#    endif
#endif

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include "stcpp/execution/execution_mode.hpp"
#include "stcpp/infra/process/fd_guard.hpp"

// build-time PID dir.  CMake 注入 -DSTCPP_PID_DIR=/tmp/stcpp (MVP default).
// M5+ 生产 Linux 切换 -DSTCPP_PID_DIR=/var/run/stcpp, 仅重编, 不改代码.
#ifndef STCPP_PID_DIR
#    define STCPP_PID_DIR "/tmp/stcpp"
#endif

namespace stcpp::infra::process {

// ---------------------------------------------------------------------------
// SingleInstanceLockFailure — acquire() 失败时抛出 (含已运行进程诊断信息)
// ---------------------------------------------------------------------------
struct SingleInstanceLockFailure : std::runtime_error {
    std::int64_t existing_pid{0};   // 已运行进程的 PID (读自 PID file)
    std::int64_t start_ts_ns{0};    // 已运行进程起始时间 (ns, CLOCK_REALTIME)
    std::string exec_mode_str;      // "paper" / "live" / "backtest"
    std::string build_commit_hash;  // git commit hash (从 PID file 读)

    explicit SingleInstanceLockFailure(std::string msg, std::int64_t pid, std::int64_t ts_ns,
                                       std::string mode_str, std::string commit)
        : std::runtime_error(std::move(msg)),
          existing_pid(pid),
          start_ts_ns(ts_ns),
          exec_mode_str(std::move(mode_str)),
          build_commit_hash(std::move(commit)) {}
};

// ---------------------------------------------------------------------------
// SingleInstanceLock — RAII, 构造 acquire, 析构 release
//
// 用法 (paper.cpp main() 第一行):
//   SingleInstanceLock lock{stcpp::execution::ExecutionMode::Paper};
//   // throws SingleInstanceLockFailure if another instance is running
// ---------------------------------------------------------------------------
class SingleInstanceLock {
public:
    // 构造即 acquire.  失败抛 SingleInstanceLockFailure.
    // mode 决定 PID file path (path_for(mode)) — R-7 物理隔离.
    explicit SingleInstanceLock(stcpp::execution::ExecutionMode mode);

    // 析构即 release: FdGuard 析构自动 close fd → POSIX flock 自动释放.
    // SIGTERM handler 另行 unlink PID file (优雅退出).
    ~SingleInstanceLock();

    // 禁止拷贝 / 移动 (锁是进程级唯一资源)
    SingleInstanceLock(const SingleInstanceLock&) = delete;
    SingleInstanceLock& operator=(const SingleInstanceLock&) = delete;
    SingleInstanceLock(SingleInstanceLock&&) = delete;
    SingleInstanceLock& operator=(SingleInstanceLock&&) = delete;

    // static helper: 按 ExecutionMode 返回 PID file 绝对路径
    // paper    → STCPP_PID_DIR/paper.pid
    // live     → STCPP_PID_DIR/live.pid
    // backtest → STCPP_PID_DIR/backtest.pid
    [[nodiscard]] static std::string path_for(stcpp::execution::ExecutionMode mode);

    // 已持锁的 PID file path (构造成功后有效)
    [[nodiscard]] std::string_view pid_path() const noexcept { return pid_path_; }

private:
    FdGuard fd_guard_;
    std::string pid_path_;
};

// ---------------------------------------------------------------------------
// InstallSigtermHandler — 注册 SIGTERM/SIGINT handler, 优雅退出时 unlink PID file
//
// 须在 SingleInstanceLock 构造成功后调用.
// kill -9 不走 handler, kernel close fd → flock 自动释放, PID file 留尾,
// 下次启动 flock 抢成功后覆盖, 不影响正确性 (spec §2.2 / §2.4).
// ---------------------------------------------------------------------------
void InstallSigtermHandler(std::string pid_path) noexcept;

}  // namespace stcpp::infra::process
