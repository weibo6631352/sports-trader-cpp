// stcpp/infra/process/single_instance.cpp — SingleInstanceLock 实现
//
// 落:
//   laozhou-single-instance-spec-v1.md §2.2 (启动流程)
//   laozhou-single-instance-spec-v1.md §2.3 (PID file 格式)
//   laozhou-single-instance-spec-v1.md §2.4 (异常路径矩阵)
//
// 依赖: 仅 POSIX (sys/file.h flock, unistd.h, fcntl.h, signal.h)
// 平台: macOS + Linux (flock 语义一致, 不用 fcntl F_SETLK)
// C++20, -Werror 全过

#include "stcpp/infra/process/single_instance.hpp"

#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstdlib>
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
// SIGTERM / SIGINT handler state — g_pid_path_for_handler は static storage,
// signal handler は async-signal-safe のみ使用 (write / unlink / _exit)
// ---------------------------------------------------------------------------
constexpr std::size_t kMaxPidPathLen = 256;
static char g_pid_path_for_handler[kMaxPidPathLen];
static int  g_lock_fd_for_handler = -1;

extern "C" void SigtermHandlerImpl(int /*sig*/) {
    // async-signal-safe ops only
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
std::string SingleInstanceLock::path_for(stcpp::execution::ExecutionMode mode) {
    constexpr std::string_view base = STCPP_PID_DIR;
    std::string path;
    path.reserve(base.size() + 16);
    path.append(base);
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

// ---------------------------------------------------------------------------
// SingleInstanceLock constructor — acquire
// ---------------------------------------------------------------------------
SingleInstanceLock::SingleInstanceLock(stcpp::execution::ExecutionMode mode) {
    // 1. 解析 path
    pid_path_ = path_for(mode);

    // 2. mkdir -p <BASE>
    if (!MkdirP(STCPP_PID_DIR)) {
        throw SingleInstanceLockFailure(
            std::string("[single-instance] FATAL: cannot create pid dir: ") +
                STCPP_PID_DIR + " errno=" + std::to_string(errno),
            0, 0, std::string(stcpp::execution::ToString(mode)),
            STCPP_BUILD_COMMIT);
    }

    // 3. open — O_CLOEXEC 防 fork+exec fd 泄漏 (老沈 review 点)
    fd_ = ::open(pid_path_.c_str(),
                 O_CREAT | O_RDWR | O_CLOEXEC,   // NOLINT(hicpp-signed-bitwise)
                 0644);
    if (fd_ < 0) {
        throw SingleInstanceLockFailure(
            std::string("[single-instance] FATAL: open(") + pid_path_ +
                ") failed errno=" + std::to_string(errno),
            0, 0, std::string(stcpp::execution::ToString(mode)),
            STCPP_BUILD_COMMIT);
    }

    // 4. flock — LOCK_NB: 立即失败不阻塞
    if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {    // NOLINT(hicpp-signed-bitwise)
        if (errno == EWOULDBLOCK) {
            // 读旧 PID file 提供诊断 (失败不影响 flock 语义)
            const auto c = ReadPidFile(fd_);
            ::close(fd_);
            fd_ = -1;
            throw SingleInstanceLockFailure(
                std::string("[single-instance] FATAL: ") +
                    std::string(stcpp::execution::ToString(mode)) +
                    " already running pid=" + std::to_string(c.pid) +
                    " since=" + std::to_string(c.start_ts_ns) +
                    " commit=" + c.commit,
                c.pid, c.start_ts_ns, c.exec_mode, c.commit);
        }
        // 其他 errno (ENOLCK 等)
        const int saved = errno;
        ::close(fd_);
        fd_ = -1;
        throw SingleInstanceLockFailure(
            std::string("[single-instance] FATAL: flock failed errno=") +
                std::to_string(saved),
            0, 0, std::string(stcpp::execution::ToString(mode)),
            STCPP_BUILD_COMMIT);
    }

    // 5. 写 PID file 内容
    const std::int64_t start_ts = NowRealtimeNs();
    WritePidFile(fd_,
                 static_cast<std::int64_t>(::getpid()),
                 start_ts,
                 stcpp::execution::ToString(mode),
                 STCPP_BUILD_COMMIT);
}

// ---------------------------------------------------------------------------
// SingleInstanceLock destructor — release
// ---------------------------------------------------------------------------
SingleInstanceLock::~SingleInstanceLock() {
    if (fd_ >= 0) {
        // close fd → POSIX flock 自动释放 (绑 open file description)
        ::close(fd_);
        fd_ = -1;
    }
    // 注: SIGTERM handler 负责 unlink; 析构不 unlink (避免竞态: 新进程已写入覆盖)
}

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
