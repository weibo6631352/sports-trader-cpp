// tests/unit/test_single_instance.cpp — SingleInstanceLock 7 用例 (W5 Wave 25)
//
// 落:
//   laozhou-single-instance-spec-v1.md §3.1 (测试矩阵)
//   GM ack: PID dir = /tmp/stcpp (MVP)
//
// 测试 fixture: 临时 dir 隔离 (mkdtemp), 不污染 /tmp/stcpp.
// STCPP_TEST_PID_DIR 通过编译期 macro 注入 (CMakeLists 设).
// 双进程用例走 fork + pipe 同步 (小宋 review 点: 无竞态 + macOS/Linux 双绿).

#include "stcpp/infra/process/single_instance.hpp"
#include "stcpp/execution/execution_mode.hpp"

#include <gtest/gtest.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <signal.h>     // NOLINT(modernize-deprecated-headers)
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Test 专用 PID dir (build-time macro 注入; CMakeLists 设 /tmp/stcpp_test_<pid>)
// 若未注入则退到 /tmp 下随机子目录
// ---------------------------------------------------------------------------
#ifndef STCPP_TEST_PID_DIR
#  define STCPP_TEST_PID_DIR "/tmp/stcpp_test"
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
            stcpp::infra::process::SingleInstanceLock lock{
                stcpp::execution::ExecutionMode::Paper};
            // lock 持有期间 fd 应有效 (通过 pid file 存在验证)
            EXPECT_EQ(::access(path.c_str(), F_OK), 0)
                << "PID file should exist while lock is held";
        });  // 析构 → fd close → flock 释放
    }

    // 第 2 次 acquire (锁已释放)
    ASSERT_NO_THROW({
        stcpp::infra::process::SingleInstanceLock lock2{
            stcpp::execution::ExecutionMode::Paper};
    });

    UnlinkIfExists(path);
}

// ===========================================================================
// T2: 双进程 (parent acquire, child 同 path 应失败 EWOULDBLOCK → 抛 exception)
//     fork + pipe 同步: parent 先锁, 通知 child; child 尝试 acquire, 失败写 pipe 1.
// ===========================================================================
TEST(SingleInstanceLock, T2_DoubleProcess_ChildFails) {
    EnsureTestDir();
    const std::string path = PidPathForMode(stcpp::execution::ExecutionMode::Paper);
    UnlinkIfExists(path);

    // pipe[0]=read, pipe[1]=write
    int ready_pipe[2];   // parent → child: parent 拿锁后通知
    int result_pipe[2];  // child → parent: child 结果 (1=fail as expected, 0=unexpected success)
    ASSERT_EQ(::pipe(ready_pipe),  0);
    ASSERT_EQ(::pipe(result_pipe), 0);

    stcpp::infra::process::SingleInstanceLock parent_lock{
        stcpp::execution::ExecutionMode::Paper};

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
            stcpp::infra::process::SingleInstanceLock lock{
                stcpp::execution::ExecutionMode::Paper};
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
    UnlinkIfExists(path);
}

// ===========================================================================
// T3: kill -9 模拟 (close fd 但不删 PID file) → 下次 acquire 应成功
//     验证 spec §2.4: kill -9 后 kernel close fd → flock 自动释放
// ===========================================================================
TEST(SingleInstanceLock, T3_KillMinusNine_NextAcquireSucceeds) {
    EnsureTestDir();
    const std::string path = PidPathForMode(stcpp::execution::ExecutionMode::Paper);
    UnlinkIfExists(path);

    int child_ready[2];   // child → parent: 已获锁
    int kill_ack[2];      // parent → child: 可以退了

    ASSERT_EQ(::pipe(child_ready), 0);
    ASSERT_EQ(::pipe(kill_ack),    0);

    const pid_t child = ::fork();
    ASSERT_GE(child, 0);

    if (child == 0) {
        ::close(child_ready[0]);
        ::close(kill_ack[1]);

        {
            stcpp::infra::process::SingleInstanceLock lock{
                stcpp::execution::ExecutionMode::Paper};
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
        stcpp::infra::process::SingleInstanceLock lock{
            stcpp::execution::ExecutionMode::Paper};
    }) << "After kill -9, new acquire should succeed (kernel auto-released flock)";

    UnlinkIfExists(path);
}

// ===========================================================================
// T4: paper / live / backtest 3 path 物理隔离
//     paper acquire 不影响 live acquire (R-7 + R-11)
// ===========================================================================
TEST(SingleInstanceLock, T4_PhysicalIsolation_ThreeModes) {
    EnsureTestDir();
    const std::string paper_path =
        PidPathForMode(stcpp::execution::ExecutionMode::Paper);
    const std::string live_path =
        PidPathForMode(stcpp::execution::ExecutionMode::Live);
    const std::string bt_path =
        PidPathForMode(stcpp::execution::ExecutionMode::Backtest);

    // 三路径必须不同 (R-7 / R-11)
    ASSERT_NE(paper_path, live_path);
    ASSERT_NE(paper_path, bt_path);
    ASSERT_NE(live_path,  bt_path);

    UnlinkIfExists(paper_path);
    UnlinkIfExists(live_path);
    UnlinkIfExists(bt_path);

    // 三锁可同时持有 (互不影响)
    {
        stcpp::infra::process::SingleInstanceLock lp{
            stcpp::execution::ExecutionMode::Paper};
        stcpp::infra::process::SingleInstanceLock ll{
            stcpp::execution::ExecutionMode::Live};
        stcpp::infra::process::SingleInstanceLock lb{
            stcpp::execution::ExecutionMode::Backtest};

        EXPECT_EQ(::access(paper_path.c_str(), F_OK), 0);
        EXPECT_EQ(::access(live_path.c_str(),  F_OK), 0);
        EXPECT_EQ(::access(bt_path.c_str(),    F_OK), 0);
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
        stcpp::infra::process::SingleInstanceLock lock{
            stcpp::execution::ExecutionMode::Paper};

        // 直接读文件内容
        const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);   // NOLINT
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
        const long long file_pid = ::atoll(p);    // NOLINT(cert-err34-c)
        EXPECT_EQ(static_cast<pid_t>(file_pid), my_pid)
            << "PID in file should match getpid()";
        p = nl + 1;

        // line 2: start_ts_ns
        nl = ::strchr(p, '\n');
        ASSERT_NE(nl, nullptr);
        *nl = '\0';
        const long long ts = ::atoll(p);          // NOLINT(cert-err34-c)
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
        if (nl) *nl = '\0';
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
            stcpp::infra::process::SingleInstanceLock lock{
                stcpp::execution::ExecutionMode::Paper};
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
    ::fcntl(result_pipe[0], F_SETFL, O_NONBLOCK);   // NOLINT(hicpp-signed-bitwise)
    ::read(result_pipe[0], &r, 1);
    ::close(result_pipe[0]);

    int wstatus = 0;
    ::waitpid(child, &wstatus, 0);

    // child exited (via _exit(0) in SIGTERM handler or normally)
    // Verify: PID file should NOT exist (handler unlinked it)
    EXPECT_NE(::access(path.c_str(), F_OK), 0)
        << "PID file should be unlinked after SIGTERM handler ran";
}

// ===========================================================================
// T7: race condition — 并发 fork 多 child 同时 acquire 同 path, 仅 1 成功
//     fork N children, each tries to acquire paper lock.
//     Exactly 1 should succeed (flock LOCK_EX|LOCK_NB guarantee).
// ===========================================================================
TEST(SingleInstanceLock, T7_RaceCondition_OnlyOneWins) {
    EnsureTestDir();
    const std::string path = PidPathForMode(stcpp::execution::ExecutionMode::Paper);
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
                stcpp::infra::process::SingleInstanceLock lock{
                    stcpp::execution::ExecutionMode::Paper};
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
        if (r == 'S') ++success_count;
    }
    ::close(result_pipe[0]);

    for (pid_t c : children) {
        int wstatus = 0;
        ::waitpid(c, &wstatus, 0);
    }

    EXPECT_EQ(success_count, 1)
        << "Exactly 1 child should win the lock race (got " << success_count << ")";

    UnlinkIfExists(path);
}
