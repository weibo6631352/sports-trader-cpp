// stcpp/infra/process/fd_guard.hpp — POSIX fd RAII wrapper (W6 Wave 30)
//
// 落:
//   老高 P1-03 review: single_instance.cpp 裸 int fd + 手写 close 是 footgun
//   老何 modern C++ footgun v1.1: RAII 缺失导致泄漏风险
//
// 用法:
//   FdGuard g{::open(path, O_RDWR)};
//   if (!g.valid()) { /* open failed */ }
//   int raw = g.get();          // 借用, 不释放
//   int owned = g.release();    // 转移所有权, RAII 不再 close
//   g.close_now();              // 主动提前关闭
//   // 析构自动 close (若未 release / close_now)
//
// 平台: macOS + Linux (POSIX unistd.h ::close)
// C++20, -Werror 全过

#pragma once

#ifndef __linux__
#  ifndef __APPLE__
#    error "FdGuard only supports Linux and macOS (POSIX unistd)"
#  endif
#endif

#include <unistd.h>

namespace stcpp::infra::process {

// ---------------------------------------------------------------------------
// FdGuard — POSIX file descriptor RAII wrapper
//
// 不变量:
//   fd_ == -1  ⟺  不持有有效 fd (initial / released / closed)
//   fd_ >= 0   ⟺  持有 fd, 析构时调 ::close(fd_)
// ---------------------------------------------------------------------------
class FdGuard {
 public:
    // 默认构造: 不持有 fd
    FdGuard() noexcept = default;

    // 接管 fd 所有权. fd < 0 等同于空 guard (不持有)
    explicit FdGuard(int fd) noexcept : fd_(fd) {}

    // 析构: 自动 close (若仍持有)
    ~FdGuard() noexcept { do_close(); }

    // 禁止拷贝 (fd 是唯一资源, 不可共享)
    FdGuard(const FdGuard&)            = delete;
    FdGuard& operator=(const FdGuard&) = delete;

    // 移动构造: 转移所有权
    FdGuard(FdGuard&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }

    // 移动赋值: 先 close 当前持有的 fd, 再转移
    FdGuard& operator=(FdGuard&& other) noexcept {
        if (this != &other) {
            do_close();
            fd_       = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    // -----------------------------------------------------------------------
    // 观察

    // 是否持有有效 fd
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }

    // 借用 fd (不释放所有权). 返回 -1 若不持有.
    [[nodiscard]] int get() const noexcept { return fd_; }

    // -----------------------------------------------------------------------
    // 所有权操作

    // 释放所有权: 返回 fd, RAII 不再 close.
    // 调用后 valid() == false.
    [[nodiscard]] int release() noexcept {
        const int fd = fd_;
        fd_          = -1;
        return fd;
    }

    // 主动提前关闭 fd.
    // 返回 true 若 close 成功或原本不持有 fd.
    // 调用后 valid() == false (即使 close 失败).
    bool close_now() noexcept {
        if (fd_ < 0) return true;
        const int ret = ::close(fd_);
        fd_           = -1;
        return ret == 0;
    }

    // -----------------------------------------------------------------------
    // reset: 先关闭当前持有的 fd, 再持有新 fd (< 0 = 不持有)
    void reset(int new_fd = -1) noexcept {
        do_close();
        fd_ = new_fd;
    }

 private:
    void do_close() noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    int fd_{-1};
};

}  // namespace stcpp::infra::process
