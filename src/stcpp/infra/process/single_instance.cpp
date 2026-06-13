// stcpp/infra/process/single_instance.cpp — SingleInstanceLock 实现
//
// 落:
//   laozhou-single-instance-spec-v1.md §2.2 (启动流程)
//   laozhou-single-instance-spec-v1.md §2.3 (PID file 格式)
//   laozhou-single-instance-spec-v1.md §2.4 (异常路径矩阵)
//   老高 P1-03: 修 g_lock_fd_for_handler 死代码 (W6 Wave 30)
//   老何 footgun v1.1: FdGuard RAII 接管裸 fd (W6 Wave 30)
//
// 依赖: 仅 POSIX (sys/file.h flock, unistd.h, fcntl.h, signal.h)
// 平台: macOS + Linux (flock 语义一致, 不用 fcntl F_SETLK)
// C++20, -Werror 全过

#include "stcpp/infra/process/single_instance.hpp"

#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

// POSIX headers
#include <fcntl.h>
#include <signal.h>    // NOLINT(modernize-deprecated-headers) — POSIX signals
#include <sys/file.h>  // flock
#include <sys/stat.h>
#include <unistd.h>

// build-time git commit hash injection:
//   CMake: target_compile_definitions(... PRIVATE STCPP_BUILD_COMMIT="abc1234")
// fallback to "unknown" if not set.
#ifndef STCPP_BUILD_COMMIT
#  define STCPP_BUILD_COMMIT "unknown"
#endif

namespace stcpp::infra::process {

namespace {

// ---------------------------------------------------------------------------
// MkdirP — 等同于 mkdir -p, 单级已存在 = ok
// ---------------------------------------------------------------------------
bool MkdirP(const char* path) noexcept {
    // 逐段创建; PID dir 只有 2 层 (/tmp/stcpp 或 /var/run/stcpp)
    // 简单实现: 先试一次; 已存在直接 ok
    if (::mkdir(path, 0755) == 0) return true;
    if (errno == EEXIST)          return true;
    // 尝试先创建父级 (处理 /var/run/stcpp 场景)
    std::string parent(path);
    const auto slash = parent.rfind('/');
    if (slash == std::string::npos || slash == 0) return false;
    parent.resize(slash);
    if (::mkdir(parent.c_str(), 0755) != 0 && errno != EEXIST) return false;
    if (::mkdir(path, 0755) == 0) return true;
    return errno == EEXIST;
}

// ---------------------------------------------------------------------------
// NowRealtimeNs — CLOCK_REALTIME (同 infra::wal::pit::NowRealtimeNs, 独立复制
// 避免 stcpp_process_lock 依赖 stcpp_infra_wal)
// ---------------------------------------------------------------------------
[[nodiscard]] std::int64_t NowRealtimeNs() noexcept {
    timespec ts{};
    ::clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<std::int64_t>(ts.tv_sec) * 1'000'000'000LL +
           static_cast<std::int64_t>(ts.tv_nsec);
}

// ---------------------------------------------------------------------------
// WritePidFile — 写 PID file 内容:
//   <PID>\n<start_ts_ns>\n<exec_mode>\n<build_commit_hash>\n
// fd 已 open, 此函数先 ftruncate + write + fsync.
// ---------------------------------------------------------------------------
bool WritePidFile(int fd,
                  std::int64_t pid,
                  std::int64_t start_ts_ns,
                  std::string_view exec_mode,
                  std::string_view commit) noexcept {
    if (::ftruncate(fd, 0) != 0) return false;
    if (::lseek(fd, 0, SEEK_SET) == static_cast<off_t>(-1)) return false;

    // 格式化到栈 buffer (≤ 256 字节)
    char buf[256];
    int len = ::snprintf(buf, sizeof(buf),
                         "%lld\n%lld\n%.*s\n%.*s\n",
                         static_cast<long long>(pid),
                         static_cast<long long>(start_ts_ns),
                         static_cast<int>(exec_mode.size()), exec_mode.data(),
                         static_cast<int>(commit.size()), commit.data());
    if (len <= 0 || static_cast<std::size_t>(len) >= sizeof(buf)) return false;

    const char* p = buf;
    int remaining = len;
    while (remaining > 0) {
        const ssize_t n = ::write(fd, p, static_cast<std::size_t>(remaining));
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        p         += n;
        remaining -= static_cast<int>(n);
    }
    ::fsync(fd);
    return true;
}

// ---------------------------------------------------------------------------
// ReadPidFile — 读 PID file, 解析 4 行内容
// 失败时各字段取 0 / empty (不影响 flock 语义, 仅诊断用)
// ---------------------------------------------------------------------------
struct PidFileContent {
    std::int64_t pid{0};
    std::int64_t start_ts_ns{0};
    std::string  exec_mode;
    std::string  commit;
};

PidFileContent ReadPidFile(int fd) noexcept {
    PidFileContent c;
    if (::lseek(fd, 0, SEEK_SET) == static_cast<off_t>(-1)) return c;

    char buf[256]{};
    ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) return c;
    buf[n] = '\0';

    // 4 行解析
    char* p = buf;
    char* nl = nullptr;

    // line 1: PID
    nl = ::strchr(p, '\n');
    if (!nl) return c;
    *nl = '\0';
    {
        long long tmp = 0;
        auto [ptr, ec] = std::from_chars(p, nl, tmp);
        if (ec == std::errc{}) c.pid = static_cast<std::int64_t>(tmp);
    }
    p = nl + 1;

    // line 2: start_ts_ns
    nl = ::strchr(p, '\n');
    if (!nl) return c;
    *nl = '\0';
    {
        long long tmp = 0;
        auto [ptr, ec] = std::from_chars(p, nl, tmp);
        if (ec == std::errc{}) c.start_ts_ns = static_cast<std::int64_t>(tmp);
    }
    p = nl + 1;

    // line 3: exec_mode
    nl = ::strchr(p, '\n');
    if (!nl) return c;
    *nl = '\0';
    c.exec_mode = p;
    p = nl + 1;

    // line 4: commit
    nl = ::strchr(p, '\n');
    if (nl) *nl = '\0';
    c.commit = p;

    return c;
}

// ---------------------------------------------------------------------------
// SIGTERM / SIGINT handler state — static storage, async-signal-safe only
//   g_pid_path_for_handler: written by InstallSigtermHandler before sigaction
//   g_lock_fd_for_handler:  written by inject_lock_fd_for_handler (called from
//                           SingleInstanceLock constructor after open+flock)
// ---------------------------------------------------------------------------
constexpr std::size_t kMaxPidPathLen = 256;
static char g_pid_path_for_handler[kMaxPidPathLen];
static int  g_lock_fd_for_handler = -1;

// Inject the live fd so handler can close it (fixes dead-code bug P1-03).
// Must be called before sigaction registration in InstallSigtermHandler.
// This is NOT async-signal-safe — call only from normal (non-signal) context.
void inject_lock_fd_for_handler(int fd) noexcept {
    g_lock_fd_for_handler = fd;
}

extern "C" void SigtermHandlerImpl(int /*sig*/) {
    // async-signal-safe ops only (write / unlink / close / _exit)
    if (g_pid_path_for_handler[0] != '\0') {
        ::unlink(g_pid_path_for_handler);
    }
    if (g_lock_fd_for_handler >= 0) {
        ::close(g_lock_fd_for_handler);
        g_lock_fd_for_handler = -1;
    }
    ::_exit(0);
}

}  // namespace

// ---------------------------------------------------------------------------
// SingleInstanceLock::path_for
// ---------------------------------------------------------------------------
// 锁文件 base dir (R-7: 生产用 STCPP_PID_DIR; 测试用 per-process tmpdir 防 ctest 误争锁)。
static std::string lock_base_dir() {
#ifdef STCPP_TEST_BUILD
    const char* env_override = ::getenv("STCPP_TEST_PID_DIR");
    char pid_dir_buf[64];
    if (!env_override) {
        ::snprintf(pid_dir_buf, sizeof(pid_dir_buf),
                   "/tmp/stcpp_test_%d", static_cast<int>(::getpid()));
        env_override = pid_dir_buf;
    }
    return std::string(env_override);
#else
    return std::string(STCPP_PID_DIR);
#endif
}

std::string SingleInstanceLock::path_for(stcpp::execution::ExecutionMode mode) {
    std::string path = lock_base_dir();
    path += '/';
    switch (mode) {
        case stcpp::execution::ExecutionMode::Live:
            path += "live.pid";
            break;
        case stcpp::execution::ExecutionMode::Paper:
            path += "paper.pid";
            break;
        case stcpp::execution::ExecutionMode::Backtest:
            path += "backtest.pid";
            break;
    }
    return path;
}

// 全局引擎锁 path: <BASE>/engine.pid (任意 mode 共用 → 一台机只一个引擎)。
std::string SingleInstanceLock::global_engine_path() {
    return lock_base_dir() + "/engine.pid";
}

// ---------------------------------------------------------------------------
// SingleInstanceLock constructor — acquire
// ---------------------------------------------------------------------------
SingleInstanceLock::SingleInstanceLock(stcpp::execution::ExecutionMode mode) {
    acquire_(path_for(mode), std::string(stcpp::execution::ToString(mode)));
}

SingleInstanceLock::SingleInstanceLock(GlobalEngineTag) {
    // 全局引擎锁: engine.pid, 任意 mode 只许一个引擎 (GM 决议: 省资源, 不分模式)。
    acquire_(global_engine_path(), "engine");
}

// 共享 acquire: mkdir + open + flock(NB) + write pid。两 ctor 复用。失败抛 SingleInstanceLockFailure。
void SingleInstanceLock::acquire_(const std::string& path, const std::string& mode_str) {
    // 1. path (STCPP_TEST_BUILD: runtime env override 用于测试进程隔离)
    pid_path_ = path;

    // 2. mkdir -p <BASE>
    const auto last_slash = pid_path_.rfind('/');
    const std::string pid_dir = (last_slash != std::string::npos)
        ? pid_path_.substr(0, last_slash)
        : std::string(STCPP_PID_DIR);

    if (!MkdirP(pid_dir.c_str())) {
        throw SingleInstanceLockFailure(
            std::string("[single-instance] FATAL: cannot create pid dir: ") +
                pid_dir + " errno=" + std::to_string(errno),
            0, 0, mode_str, STCPP_BUILD_COMMIT);
    }

    // 3. open — O_CLOEXEC 防 fork+exec fd 泄漏 (老沈 review 点)
    fd_guard_.reset(::open(pid_path_.c_str(),
                           O_CREAT | O_RDWR | O_CLOEXEC,   // NOLINT(hicpp-signed-bitwise)
                           0644));
    if (!fd_guard_.valid()) {
        throw SingleInstanceLockFailure(
            std::string("[single-instance] FATAL: open(") + pid_path_ +
                ") failed errno=" + std::to_string(errno),
            0, 0, mode_str, STCPP_BUILD_COMMIT);
    }

    // 4. flock — LOCK_NB: 立即失败不阻塞
    if (::flock(fd_guard_.get(), LOCK_EX | LOCK_NB) != 0) {    // NOLINT(hicpp-signed-bitwise)
        if (errno == EWOULDBLOCK) {
            // 2026-06-13 死锁自动回收 (老板「PID 已死自动回收, 不要堆积」): flock 被持 = 持有者进程仍活
            //   (flock 绑 open fd, 进程死→kernel 释放, PID 文件只是诊断)。但 SIGKILL 后旧进程可能短暂未被
            //   回收 (D 态网络线程) → flock 未释 → 新实例 EWOULDBLOCK = 误判"已在跑"。重试 ~2s 给其释放,
            //   期间一旦 flock 成功即【自动回收】上一实例残留锁。仅启动期 (R-12 不进 hot path)。
            //   2s 后仍被持 = 确有另一实例真在跑 → 拒。
            const auto c0 = ReadPidFile(fd_guard_.get());
            const bool prev_dead =
                (c0.pid <= 0) || (::kill(static_cast<pid_t>(c0.pid), 0) != 0 && errno == ESRCH);
            constexpr int kRetries = 5;           // 5 × 200ms = 1s 上限 (reap 竞态够; 主防线是 start_*.sh wait-for-death)
            constexpr useconds_t kSleepUs = 200'000;
            bool acquired = false;
            for (int attempt = 0; attempt < kRetries; ++attempt) {
                ::usleep(kSleepUs);
                if (::flock(fd_guard_.get(), LOCK_EX | LOCK_NB) == 0) {  // NOLINT(hicpp-signed-bitwise)
                    acquired = true;
                    break;
                }
            }
            if (!acquired) {
                const auto c = ReadPidFile(fd_guard_.get());
                fd_guard_.close_now();  // RAII close: releases flock
                throw SingleInstanceLockFailure(
                    std::string("[single-instance] FATAL: ") + mode_str +
                        " already running pid=" + std::to_string(c.pid) +
                        " since=" + std::to_string(c.start_ts_ns) +
                        " commit=" + c.commit + " (2s retry 后仍被持, 确有实例在跑)",
                    c.pid, c.start_ts_ns, c.exec_mode, c.commit);
            }
            std::fprintf(stderr,
                         "[single-instance] ⚠ 自动回收上一实例残留锁 (prev pid=%lld %s) → 启动继续\n",
                         static_cast<long long>(c0.pid), prev_dead ? "已死" : "刚释放");
        } else {
            // 其他 errno (ENOLCK 等)
            const int saved = errno;
            fd_guard_.close_now();  // RAII close
            throw SingleInstanceLockFailure(
                std::string("[single-instance] FATAL: flock failed errno=") +
                    std::to_string(saved),
                0, 0, mode_str, STCPP_BUILD_COMMIT);
        }
    }

    // 5. 写 PID file 内容
    const std::int64_t start_ts = NowRealtimeNs();
    WritePidFile(fd_guard_.get(),
                 static_cast<std::int64_t>(::getpid()),
                 start_ts,
                 mode_str,
                 STCPP_BUILD_COMMIT);

    // 6. 注入 fd 到 SIGTERM handler state
    inject_lock_fd_for_handler(fd_guard_.get());
}

// ---------------------------------------------------------------------------
// SingleInstanceLock destructor — release
// FdGuard 析构自动 close fd → POSIX flock 自动释放 (绑 open file description)
// 注: SIGTERM handler 负责 unlink; 析构不 unlink (避免竞态: 新进程已写入覆盖)
// ---------------------------------------------------------------------------
SingleInstanceLock::~SingleInstanceLock() = default;

// ---------------------------------------------------------------------------
// InstallSigtermHandler
// ---------------------------------------------------------------------------
void InstallSigtermHandler(std::string pid_path) noexcept {
    // 写入 signal handler 状态 (async-signal-safe raw buffer)
    const std::size_t copy_len =
        pid_path.size() < kMaxPidPathLen - 1 ? pid_path.size() : kMaxPidPathLen - 1;
    ::memcpy(g_pid_path_for_handler, pid_path.c_str(), copy_len);
    g_pid_path_for_handler[copy_len] = '\0';

    struct sigaction sa{};
    sa.sa_handler = SigtermHandlerImpl;
    // sigemptyset は macOS で macro 展開されるため :: なし
    sigemptyset(&sa.sa_mask);   // NOLINT(hicpp-no-assembler) macOS macro
    sa.sa_flags = 0;
    ::sigaction(SIGTERM, &sa, nullptr);
    ::sigaction(SIGINT,  &sa, nullptr);
}

}  // namespace stcpp::infra::process
