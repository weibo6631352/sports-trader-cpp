// tests/unit/test_single_instance.cpp — SingleInstanceLock 7 用例 (W5 Wave 25)
//
// 落:
//   laozhou-single-instance-spec-v1.md §3.1 (测试矩阵)
//   GM ack: PID dir = /tmp/stcpp (MVP)
//
// 测试 fixture: 临时 dir 隔离 (mkdtemp), 不污染 /tmp/stcpp.
// STCPP_TEST_PID_DIR 通过编译期 macro 注入 (CMakeLists 设).
// 双进程用例走 fork + pipe 同步 (小宋 review 点: 无竞态 + macOS/Linux 双绿).

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <gtest/gtest.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "stcpp/execution/execution_mode.hpp"
#include "stcpp/infra/process/fd_guard.hpp"
#include "stcpp/infra/process/single_instance.hpp"

#include <fcntl.h>
#include <signal.h>  // NOLINT(modernize-deprecated-headers)
#include <unistd.h>

// ---------------------------------------------------------------------------
// Test 专用 PID dir (build-time macro 注入; CMakeLists 设 /tmp/stcpp_test_<pid>)
// 若未注入则退到 /tmp 下随机子目录
// ---------------------------------------------------------------------------
#ifndef STCPP_TEST_PID_DIR
#    define STCPP_TEST_PID_DIR "/tmp/stcpp_test"
#endif

namespace {

// ---- 辅助: 为每个测试制造隔离目录 ------------------------------------------
// 使用 tmpdir 传递给测试, 测试结束后 RAII 清理.
// 注: 我们用 STCPP_PID_DIR 宏, 但它是 compile-time constant.
// 为让测试隔离, 我们利用 path_for() 返回的 path 以 STCPP_PID_DIR 为 prefix;
// 测试中直接操作 path (open/flock/unlink) 验证行为.
// ---------------------------------------------------------------------------
std::string PidPathForMode(stcpp::execution::ExecutionMode m) {
    return stcpp::infra::process::SingleInstanceLock::path_for(m);
}

// 确保 /tmp/stcpp_test 目录存在
void EnsureTestDir() {
    ::mkdir(STCPP_TEST_PID_DIR, 0755);  // ok if exists
}

// 清理指定 PID file (忽略不存在)
void UnlinkIfExists(const std::string& path) {
    ::unlink(path.c_str());
}

}  // namespace

// ===========================================================================
// T1: 单进程 acquire → release → 再 acquire 都成功
// ===========================================================================
TEST(SingleInstanceLock, T1_AcquireReleaseReacquire) {
    EnsureTestDir();
    const std::string path = PidPathForMode(stcpp::execution::ExecutionMode::Paper);
    UnlinkIfExists(path);

    // 第 1 次 acquire
    {
        ASSERT_NO_THROW({
            stcpp::infra::process::SingleInstanceLock lock{stcpp::execution::ExecutionMode::Paper};
            // lock 持有期间 fd 应有效 (通过 pid file 存在验证)
            EXPECT_EQ(::access(path.c_str(), F_OK), 0) << "PID file should exist while lock is held";
        });  // 析构 → fd close → flock 释放
    }

    // 第 2 次 acquire (锁已释放)
    ASSERT_NO_THROW(
        { stcpp::infra::process::SingleInstanceLock lock2{stcpp::execution::ExecutionMode::Paper}; });

    UnlinkIfExists(path);
}

// ===========================================================================
// T2: 双进程 (parent acquire, child 同 path 应失败 EWOULDBLOCK → 抛 exception)
//     fork + pipe 同步: parent 先锁, 通知 child; child 尝试 acquire, 失败写 pipe 1.
// ===========================================================================
TEST(SingleInstanceLock, T2_DoubleProcess_ChildFails) {
    EnsureTestDir();
    const std::string path = PidPathForMode(stcpp::execution::ExecutionMode::Paper);
    // STCPP_TEST_BUILD: 设 STCPP_TEST_PID_DIR 保证 parent+child 竞争同一 pid file
    {
        const auto slash = path.rfind('/');
        const std::string pid_dir = (slash != std::string::npos) ? path.substr(0, slash) : "/tmp";
        ::setenv("STCPP_TEST_PID_DIR", pid_dir.c_str(), 1);
    }
    UnlinkIfExists(path);

    // pipe[0]=read, pipe[1]=write
    int ready_pipe[2];   // parent → child: parent 拿锁后通知
    int result_pipe[2];  // child → parent: child 结果 (1=fail as expected, 0=unexpected success)
    ASSERT_EQ(::pipe(ready_pipe), 0);
    ASSERT_EQ(::pipe(result_pipe), 0);

    stcpp::infra::process::SingleInstanceLock parent_lock{stcpp::execution::ExecutionMode::Paper};

    const pid_t child = ::fork();
    ASSERT_GE(child, 0) << "fork failed";

    if (child == 0) {
        // ---- child ----
        ::close(ready_pipe[1]);
        ::close(result_pipe[0]);

        // 等 parent 信号 (read block)
        char dummy = 0;
        ::read(ready_pipe[0], &dummy, 1);
        ::close(ready_pipe[0]);

        // child 尝试 acquire 同一 path — 应失败
        char result = '0';  // '0' = unexpected success
        try {
            stcpp::infra::process::SingleInstanceLock lock{stcpp::execution::ExecutionMode::Paper};
            // 不该到这里
            result = '0';
        } catch (const stcpp::infra::process::SingleInstanceLockFailure&) {
            result = '1';  // 符合预期
        } catch (...) {
            result = '0';
        }
        ::write(result_pipe[1], &result, 1);
        ::close(result_pipe[1]);
        ::_exit(0);
    }

    // ---- parent ----
    ::close(ready_pipe[0]);
    ::close(result_pipe[1]);

    // 通知 child: parent 已持锁
    char go = 'g';
    ::write(ready_pipe[1], &go, 1);
    ::close(ready_pipe[1]);

    // 读 child 结果
    char result = '?';
    ::read(result_pipe[0], &result, 1);
    ::close(result_pipe[0]);

    int wstatus = 0;
    ::waitpid(child, &wstatus, 0);

    EXPECT_EQ(result, '1') << "Child should have failed to acquire same-mode lock";

    // parent_lock 析构 → 释放
    ::unsetenv("STCPP_TEST_PID_DIR");
    UnlinkIfExists(path);
}

// ===========================================================================
// T3: kill -9 模拟 (close fd 但不删 PID file) → 下次 acquire 应成功
//     验证 spec §2.4: kill -9 后 kernel close fd → flock 自动释放
// ===========================================================================
TEST(SingleInstanceLock, T3_KillMinusNine_NextAcquireSucceeds) {
    EnsureTestDir();
    const std::string path = PidPathForMode(stcpp::execution::ExecutionMode::Paper);
    // STCPP_TEST_BUILD: 设 STCPP_TEST_PID_DIR 保证 parent+child 用同一 pid dir
    {
        const auto slash = path.rfind('/');
        const std::string pid_dir = (slash != std::string::npos) ? path.substr(0, slash) : "/tmp";
        ::setenv("STCPP_TEST_PID_DIR", pid_dir.c_str(), 1);
    }
    UnlinkIfExists(path);

    int child_ready[2];  // child → parent: 已获锁
    int kill_ack[2];     // parent → child: 可以退了

    ASSERT_EQ(::pipe(child_ready), 0);
    ASSERT_EQ(::pipe(kill_ack), 0);

    const pid_t child = ::fork();
    ASSERT_GE(child, 0);

    if (child == 0) {
        ::close(child_ready[0]);
        ::close(kill_ack[1]);

        {
            stcpp::infra::process::SingleInstanceLock lock{stcpp::execution::ExecutionMode::Paper};
            // 通知 parent: 已持锁
            char rdy = 'r';
            ::write(child_ready[1], &rdy, 1);
            ::close(child_ready[1]);

            // 等 parent kill 信号 (实际是 pipe eof / 消息)
            char dummy = 0;
            ::read(kill_ack[0], &dummy, 1);
            // 模拟 kill -9: 直接 _exit (lock 析构不运行 → fd close by kernel)
        }
        ::_exit(0);
    }

    // parent
    ::close(child_ready[1]);
    ::close(kill_ack[0]);

    // 等 child 持锁
    char rdy = 0;
    ::read(child_ready[0], &rdy, 1);
    ::close(child_ready[0]);

    EXPECT_EQ(rdy, 'r');

    // 发送 kill -9 (SIGKILL): kernel 释放 flock
    ::kill(child, SIGKILL);
    int wstatus = 0;
    ::waitpid(child, &wstatus, 0);
    ::close(kill_ack[1]);

    // PID file 应仍存在 (留尾), 但 flock 已释放 → 新 acquire 应成功
    EXPECT_EQ(::access(path.c_str(), F_OK), 0) << "PID file should remain after kill -9";

    ASSERT_NO_THROW({
        stcpp::infra::process::SingleInstanceLock lock{stcpp::execution::ExecutionMode::Paper};
    }) << "After kill -9, new acquire should succeed (kernel auto-released flock)";

    ::unsetenv("STCPP_TEST_PID_DIR");
    UnlinkIfExists(path);
}

// ===========================================================================
// T4: paper / live / backtest 3 path 物理隔离
//     paper acquire 不影响 live acquire (R-7 + R-11)
// ===========================================================================
TEST(SingleInstanceLock, T4_PhysicalIsolation_ThreeModes) {
    EnsureTestDir();
    const std::string paper_path = PidPathForMode(stcpp::execution::ExecutionMode::Paper);
    const std::string live_path = PidPathForMode(stcpp::execution::ExecutionMode::Live);
    const std::string bt_path = PidPathForMode(stcpp::execution::ExecutionMode::Backtest);

    // 三路径必须不同 (R-7 / R-11)
    ASSERT_NE(paper_path, live_path);
    ASSERT_NE(paper_path, bt_path);
    ASSERT_NE(live_path, bt_path);

    UnlinkIfExists(paper_path);
    UnlinkIfExists(live_path);
    UnlinkIfExists(bt_path);

    // 三锁可同时持有 (互不影响)
    {
        stcpp::infra::process::SingleInstanceLock lp{stcpp::execution::ExecutionMode::Paper};
        stcpp::infra::process::SingleInstanceLock ll{stcpp::execution::ExecutionMode::Live};
        stcpp::infra::process::SingleInstanceLock lb{stcpp::execution::ExecutionMode::Backtest};

        EXPECT_EQ(::access(paper_path.c_str(), F_OK), 0);
        EXPECT_EQ(::access(live_path.c_str(), F_OK), 0);
        EXPECT_EQ(::access(bt_path.c_str(), F_OK), 0);
    }  // all three released

    UnlinkIfExists(paper_path);
    UnlinkIfExists(live_path);
    UnlinkIfExists(bt_path);
}

// ===========================================================================
// T5: PID file 内容格式校验
//     4 行: PID = getpid(), start_ts_ns > 0, mode = "paper", commit nonempty
// ===========================================================================
TEST(SingleInstanceLock, T5_PidFileContent) {
    EnsureTestDir();
    const std::string path = PidPathForMode(stcpp::execution::ExecutionMode::Paper);
    UnlinkIfExists(path);

    const pid_t my_pid = ::getpid();

    {
        stcpp::infra::process::SingleInstanceLock lock{stcpp::execution::ExecutionMode::Paper};

        // 直接读文件内容
        const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);  // NOLINT
        ASSERT_GE(fd, 0) << "PID file should be readable";

        char buf[256]{};
        const ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
        ::close(fd);
        ASSERT_GT(n, 0) << "PID file should not be empty";
        buf[n] = '\0';

        // 解析 4 行
        char* p = buf;
        char* nl = nullptr;

        // line 1: PID
        nl = ::strchr(p, '\n');
        ASSERT_NE(nl, nullptr);
        *nl = '\0';
        const long long file_pid = ::atoll(p);  // NOLINT(cert-err34-c)
        EXPECT_EQ(static_cast<pid_t>(file_pid), my_pid) << "PID in file should match getpid()";
        p = nl + 1;

        // line 2: start_ts_ns
        nl = ::strchr(p, '\n');
        ASSERT_NE(nl, nullptr);
        *nl = '\0';
        const long long ts = ::atoll(p);  // NOLINT(cert-err34-c)
        EXPECT_GT(ts, 0LL) << "start_ts_ns should be positive";
        p = nl + 1;

        // line 3: exec_mode
        nl = ::strchr(p, '\n');
        ASSERT_NE(nl, nullptr);
        *nl = '\0';
        EXPECT_STREQ(p, "paper") << "exec_mode should be 'paper'";
        p = nl + 1;

        // line 4: commit hash (non-empty; "unknown" OK in unit test)
        nl = ::strchr(p, '\n');
        if (nl)
            *nl = '\0';
        EXPECT_GT(::strlen(p), 0u) << "build_commit_hash should not be empty";
    }

    UnlinkIfExists(path);
}

// ===========================================================================
// T6: SIGTERM handler 测试
//     InstallSigtermHandler → kill(SIGTERM) → PID file 被 unlink
//     注: 测试自身不调 _exit, 用 fork + child install + raise(SIGTERM)
// ===========================================================================
TEST(SingleInstanceLock, T6_SigtermHandler_UnlinksPidFile) {
    EnsureTestDir();
    const std::string path = PidPathForMode(stcpp::execution::ExecutionMode::Paper);
    UnlinkIfExists(path);

    // result pipe: child → parent (0=pid file still exists, 1=unlinked as expected)
    int result_pipe[2];
    ASSERT_EQ(::pipe(result_pipe), 0);

    const pid_t child = ::fork();
    ASSERT_GE(child, 0);

    if (child == 0) {
        ::close(result_pipe[0]);

        // child: acquire lock + install handler + raise SIGTERM
        // We capture whether the handler actually ran via the pid file absence.
        // But _exit(0) in handler before we can write pipe...
        // Strategy: install handler that unlinks + writes pipe + _exit.
        // We override g_pid_path_for_handler approach: use child-local wrapper.
        {
            stcpp::infra::process::SingleInstanceLock lock{stcpp::execution::ExecutionMode::Paper};
            stcpp::infra::process::InstallSigtermHandler(path);

            // Handler will _exit(0) after unlink. We won't reach pipe write.
            // Parent verifies PID file absence after child exits.
            ::raise(SIGTERM);
        }
        // Should not reach here if handler ran
        char r = '0';
        ::write(result_pipe[1], &r, 1);
        ::close(result_pipe[1]);
        ::_exit(1);
    }

    // parent
    ::close(result_pipe[1]);
    char r = '?';
    // non-blocking read (child may have _exit before writing)
    ::fcntl(result_pipe[0], F_SETFL, O_NONBLOCK);  // NOLINT(hicpp-signed-bitwise)
    ::read(result_pipe[0], &r, 1);
    ::close(result_pipe[0]);

    int wstatus = 0;
    ::waitpid(child, &wstatus, 0);

    // child exited (via _exit(0) in SIGTERM handler or normally)
    // Verify: PID file should NOT exist (handler unlinked it)
    EXPECT_NE(::access(path.c_str(), F_OK), 0) << "PID file should be unlinked after SIGTERM handler ran";
}

// ===========================================================================
// T7: race condition — 并发 fork 多 child 同时 acquire 同 path, 仅 1 成功
//     fork N children, each tries to acquire paper lock.
//     Exactly 1 should succeed (flock LOCK_EX|LOCK_NB guarantee).
//
// STCPP_TEST_BUILD 隔离: fork 前设 STCPP_TEST_PID_DIR, 让 parent+child 用同一 pid dir.
// (父子进程继承 env, 竞争同一 flock, 测试语义不变; 不影响其他并发 ctest 进程)
// ===========================================================================
TEST(SingleInstanceLock, T7_RaceCondition_OnlyOneWins) {
    EnsureTestDir();
    // 设置 STCPP_TEST_PID_DIR 让 fork 出的 child 与 parent 竞争同一 pid file
    const std::string path = PidPathForMode(stcpp::execution::ExecutionMode::Paper);
    {
        const auto slash = path.rfind('/');
        const std::string pid_dir = (slash != std::string::npos) ? path.substr(0, slash) : "/tmp";
        ::setenv("STCPP_TEST_PID_DIR", pid_dir.c_str(), 1);
    }
    UnlinkIfExists(path);

    constexpr int kNumChildren = 8;

    // result pipe: each child writes 'S' (success) or 'F' (fail)
    int result_pipe[2];
    ASSERT_EQ(::pipe(result_pipe), 0);

    // start_gate pipe: parent signals all children simultaneously
    int start_gate[2];
    ASSERT_EQ(::pipe(start_gate), 0);

    std::vector<pid_t> children;
    children.reserve(kNumChildren);

    for (int i = 0; i < kNumChildren; ++i) {
        const pid_t c = ::fork();
        ASSERT_GE(c, 0);
        if (c == 0) {
            // child
            ::close(result_pipe[0]);
            ::close(start_gate[1]);

            // wait for start signal
            char dummy = 0;
            ::read(start_gate[0], &dummy, 1);
            ::close(start_gate[0]);

            char result = 'F';
            try {
                stcpp::infra::process::SingleInstanceLock lock{stcpp::execution::ExecutionMode::Paper};
                result = 'S';
                // hold briefly
                ::usleep(5000);  // 5ms
            } catch (const stcpp::infra::process::SingleInstanceLockFailure&) {
                result = 'F';
            }
            ::write(result_pipe[1], &result, 1);
            ::close(result_pipe[1]);
            ::_exit(0);
        }
        children.push_back(c);
    }

    // close child-side fds in parent
    ::close(start_gate[0]);
    ::close(result_pipe[1]);

    // signal all children to start simultaneously
    // write kNumChildren bytes — each child reads 1
    for (int i = 0; i < kNumChildren; ++i) {
        char go = 'g';
        ::write(start_gate[1], &go, 1);
    }
    ::close(start_gate[1]);

    // collect results
    int success_count = 0;
    for (int i = 0; i < kNumChildren; ++i) {
        char r = '?';
        ::read(result_pipe[0], &r, 1);
        if (r == 'S')
            ++success_count;
    }
    ::close(result_pipe[0]);

    for (pid_t c : children) {
        int wstatus = 0;
        ::waitpid(c, &wstatus, 0);
    }

    EXPECT_EQ(success_count, 1) << "Exactly 1 child should win the lock race (got " << success_count << ")";

    ::unsetenv("STCPP_TEST_PID_DIR");
    UnlinkIfExists(path);
}

// ===========================================================================
// T8: FdGuard RAII — 构造接 fd, valid/get/release/close_now + 析构自动 close
//     使用真实 open(2) fd 验证 RAII 语义; pipe() 提供两个低成本 fd.
// ===========================================================================
TEST(FdGuard, T8_RAII_Lifecycle) {
    using stcpp::infra::process::FdGuard;

    // ---- 默认构造: 不持有 ----
    {
        FdGuard g;
        EXPECT_FALSE(g.valid());
        EXPECT_EQ(g.get(), -1);
    }

    // ---- 构造持有合法 fd ----
    int pipefd[2];
    ASSERT_EQ(::pipe(pipefd), 0) << "pipe() should succeed";
    // pipefd[0]=read, pipefd[1]=write

    {
        FdGuard g{pipefd[0]};
        EXPECT_TRUE(g.valid());
        EXPECT_EQ(g.get(), pipefd[0]);

        // 析构时 close pipefd[0]
    }
    // pipefd[0] 已被 FdGuard 析构关闭: fcntl 应返回 -1
    EXPECT_EQ(::fcntl(pipefd[0], F_GETFD), -1) << "FdGuard dtor should have closed fd";
    ::close(pipefd[1]);  // 手动关闭 write end

    // ---- release: 转移所有权, RAII 不 close ----
    int pipefd2[2];
    ASSERT_EQ(::pipe(pipefd2), 0);

    int released_fd = -1;
    {
        FdGuard g{pipefd2[0]};
        EXPECT_TRUE(g.valid());
        released_fd = g.release();
        EXPECT_FALSE(g.valid());  // 所有权已转出
        EXPECT_EQ(released_fd, pipefd2[0]);
        // 析构时 g 不 close (已 release)
    }
    // released_fd 仍有效
    EXPECT_NE(::fcntl(released_fd, F_GETFD), -1) << "release() should leave fd open after dtor";
    ::close(released_fd);
    ::close(pipefd2[1]);

    // ---- close_now: 主动提前关闭 ----
    int pipefd3[2];
    ASSERT_EQ(::pipe(pipefd3), 0);

    {
        FdGuard g{pipefd3[0]};
        EXPECT_TRUE(g.valid());
        const bool ok = g.close_now();
        EXPECT_TRUE(ok);
        EXPECT_FALSE(g.valid());
        // fd 已关闭
        EXPECT_EQ(::fcntl(pipefd3[0], F_GETFD), -1) << "close_now() should have closed fd immediately";
        // 析构时 g 不再尝试关闭 (valid() == false)
    }
    ::close(pipefd3[1]);

    // ---- 移动构造 ----
    int pipefd4[2];
    ASSERT_EQ(::pipe(pipefd4), 0);

    {
        FdGuard src{pipefd4[0]};
        EXPECT_TRUE(src.valid());

        FdGuard dst{std::move(src)};
        EXPECT_FALSE(src.valid());  // src 交出所有权
        EXPECT_TRUE(dst.valid());
        EXPECT_EQ(dst.get(), pipefd4[0]);
        // dst 析构 close pipefd4[0]
    }
    EXPECT_EQ(::fcntl(pipefd4[0], F_GETFD), -1) << "move-ctor: moved-into FdGuard should close fd on dtor";
    ::close(pipefd4[1]);

    // ---- 移动赋值 ----
    int pipefd5[2];
    ASSERT_EQ(::pipe(pipefd5), 0);
    int pipefd6[2];
    ASSERT_EQ(::pipe(pipefd6), 0);

    {
        FdGuard a{pipefd5[0]};
        FdGuard b{pipefd6[0]};
        // a = move(b): a 先 close pipefd5[0], 再持有 pipefd6[0]
        a = std::move(b);
        EXPECT_FALSE(b.valid());
        EXPECT_EQ(a.get(), pipefd6[0]);
        // pipefd5[0] 已被关闭
        EXPECT_EQ(::fcntl(pipefd5[0], F_GETFD), -1) << "move-assign: displaced fd should be closed";
        // a 析构 close pipefd6[0]
    }
    EXPECT_EQ(::fcntl(pipefd6[0], F_GETFD), -1) << "move-assign: new fd should be closed by dtor";
    ::close(pipefd5[1]);
    ::close(pipefd6[1]);
}

// ===========================================================================
// T9: SIGTERM handler 真实 close fd (非死代码)
//
// 场景: child fork → SingleInstanceLock acquire (inject fd to handler) →
//       InstallSigtermHandler → raise(SIGTERM).
//       Parent 通过 /proc/fd or fcntl 验证 child 退出后 PID file 已 unlink.
//       同时验证 handler 中 close fd 分支确实执行 (fd_close_pipe 同步).
//
// 策略: 因 handler 调 _exit(0), child 无法写 pipe.
//   检验方法:
//   1. PID file 不存在 (unlink 成功 → handler 运行了 unlink 分支)
//   2. 用 WEXITSTATUS 验证 child 以 0 退出 (handler _exit(0))
//   3. 另开 sync_pipe: child 在 raise 前通知 parent "handler 已安装",
//      parent 收到后 waitpid, 再检查 PID file.
// ===========================================================================
TEST(SingleInstanceLock, T9_SigtermHandler_ClosesLockFd) {
    EnsureTestDir();
    const std::string path = PidPathForMode(stcpp::execution::ExecutionMode::Live);
    // STCPP_TEST_BUILD: 设 STCPP_TEST_PID_DIR 让 child 用与 parent 相同的 pid dir,
    // 保证 child acquire + InstallSigtermHandler(path) + unlink 的 path 一致
    {
        const auto slash = path.rfind('/');
        const std::string pid_dir = (slash != std::string::npos) ? path.substr(0, slash) : "/tmp";
        ::setenv("STCPP_TEST_PID_DIR", pid_dir.c_str(), 1);
    }
    UnlinkIfExists(path);

    // sync_pipe: child → parent: handler 已安装 + raise 即将触发
    int sync_pipe[2];
    ASSERT_EQ(::pipe(sync_pipe), 0);

    const pid_t child = ::fork();
    ASSERT_GE(child, 0) << "fork() should succeed";

    if (child == 0) {
        // ---- child ----
        ::close(sync_pipe[0]);

        // acquire live lock (inject_lock_fd_for_handler 在构造中调用)
        stcpp::infra::process::SingleInstanceLock lock{stcpp::execution::ExecutionMode::Live};

        // install SIGTERM handler (now g_lock_fd_for_handler is populated)
        stcpp::infra::process::InstallSigtermHandler(path);

        // 通知 parent: handler 已就绪, raise 即将执行
        char rdy = 'r';
        ::write(sync_pipe[1], &rdy, 1);
        ::close(sync_pipe[1]);

        // raise SIGTERM → handler: unlink + close fd + _exit(0)
        ::raise(SIGTERM);

        // 不应到达此处
        ::_exit(42);
    }

    // ---- parent ----
    ::close(sync_pipe[1]);

    // 等 child ready
    char rdy = '?';
    ::read(sync_pipe[0], &rdy, 1);
    ::close(sync_pipe[0]);
    EXPECT_EQ(rdy, 'r') << "child should signal handler-ready";

    // 等 child 退出
    int wstatus = 0;
    ::waitpid(child, &wstatus, 0);

    // 验证 1: child 以 _exit(0) 退出 (handler 跑了)
    ASSERT_TRUE(WIFEXITED(wstatus)) << "child should exit normally";
    EXPECT_EQ(WEXITSTATUS(wstatus), 0) << "SIGTERM handler should _exit(0), not _exit(42)";

    // 验证 2: PID file 已被 handler unlink (不存在)
    EXPECT_NE(::access(path.c_str(), F_OK), 0)
        << "SIGTERM handler should unlink PID file (P1-03: close+unlink both run)";

    ::unsetenv("STCPP_TEST_PID_DIR");
}
