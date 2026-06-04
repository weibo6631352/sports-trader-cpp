// stcpp/data/inplay_feed_thread.cpp — Goalserve inplay 采集线程实现
//
// Owner: 小段 (goalserve-specialist, #37)
// Date:  2026-05-29
// Last-updated: 2026-05-30 (security harden: gzip-bomb/max-body/cred-masking/token-bucket, 小白审计)
//
// 实现说明:
//   HTTP 层: POSIX socket (无第三方库, 匹配跨平台 C++20 标准库方法)
//   代理支持: HTTP CONNECT 隧道 (读 http_proxy / HTTP_PROXY 环境变量)
//   gzip 解压: zlib inflate (系统库, macOS + Linux 均有)
//   线程: 每 sport 独立 std::thread (防单 sport 阻塞)
//
// 安全加固 (小白审计 2026-05-30):
//   gzip 炸弹防护: DecompressGzImpl max_output_bytes=8MB, 超限中止+告警
//   消息体上限: HTTP body kMaxBodyBytes=16MB (gzip 压缩前), 超限 drop
//   凭证不入日志: inplay.goalserve.com URL 含 key (明文 http), 日志仅记 host+endpoint 类型,
//                 完整 URL/路径 严禁 log; proxy addr 掩码为 "(proxy)"
//   token-bucket: 每 sport 线程内嵌令牌桶 (cfg.poll_interval_ms + min_interval_ms 双重保护)
//   insecure_fetch 告警: 直连时记 stderr [inplay_feed] WARN insecure_fetch host=...
//
// R-20:
//   data_source_ts_ns = updated_ts (ms) × 1e6 (Goalserve payload)
//   event_ts_ns       = start_ts (Unix sec) × 1e9 (赛事排定时间)
//   ingestion_ts_ns   = recv 完成时刻 (clock_gettime CLOCK_REALTIME ns)
//   禁 now() 替代 data_source_ts
//
// R-12:
//   HTTP I/O 仅在采集线程, 绝不在 WSS event loop 调用
//   ScoreSnapshotStore::Publish 原子 swap (~5ns 持锁)

#include "stcpp/data/inplay_feed_thread.hpp"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

// POSIX
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <netdb.h>
#include <unistd.h>

// zlib
#include "stcpp/data/inplay_odds_parser.hpp"  // ParseResultMarketIdsFromDict (字典驱动选盘)
#include "stcpp/data/inplay_score_parser.hpp"

#include <zlib.h>

namespace stcpp::data {

namespace {

// ---- 当前单调时间 ns (ingestion_ts R-20) ----
[[nodiscard]] std::int64_t NowNs() noexcept {
    struct timespec ts{};
    // CLOCK_REALTIME: 与 Goalserve updated_ts (Unix epoch) 同域, 满足 R-20 不等式
    ::clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<std::int64_t>(ts.tv_sec) * 1'000'000'000LL + static_cast<std::int64_t>(ts.tv_nsec);
}

// ---- sleep ms (不依赖 this_thread::sleep_for 以便测试替换) ----
void SleepMs(std::uint32_t ms) noexcept {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// ---- TCP connect (IPv4/IPv6, blocking, timeout via SO_RCVTIMEO) ----
// 返回 fd >= 0 = 成功; -1 = 失败
[[nodiscard]] int TcpConnect(const std::string& host, std::uint16_t port, std::uint32_t timeout_ms) noexcept {
    struct addrinfo hints{};
    struct addrinfo* res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    const std::string port_str = std::to_string(port);
    if (::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0 || res == nullptr) {
        return -1;
    }

    int fd = -1;
    for (struct addrinfo* rp = res; rp != nullptr; rp = rp->ai_next) {
        fd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0)
            continue;

        // SO_RCVTIMEO / SO_SNDTIMEO: blocking socket timeout
        struct timeval tv{};
        tv.tv_sec = static_cast<long>(timeout_ms / 1000);
        tv.tv_usec = static_cast<suseconds_t>((timeout_ms % 1000) * 1000);
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (::connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;  // 成功
        }
        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(res);
    return fd;
}

// ---- 发送全部 buf ----
[[nodiscard]] bool SendAll(int fd, const char* buf, std::size_t len) noexcept {
    std::size_t sent = 0;
    while (sent < len) {
        const ssize_t n = ::send(fd, buf + sent, len - sent, 0);
        if (n <= 0)
            return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

// ---- 读取 HTTP 响应头直到 \r\n\r\n ----
// 返回 true = 找到头结束; headers 填充完整头字符串
[[nodiscard]] bool ReadHttpHeaders(int fd, std::string& headers) noexcept {
    headers.clear();
    headers.reserve(2048);
    char buf[1];
    while (true) {
        ssize_t n = ::recv(fd, buf, 1, 0);
        if (n <= 0)
            return false;
        headers.push_back(buf[0]);
        if (headers.size() >= 4 && headers[headers.size() - 4] == '\r' &&
            headers[headers.size() - 3] == '\n' && headers[headers.size() - 2] == '\r' &&
            headers[headers.size() - 1] == '\n') {
            return true;
        }
        if (headers.size() > 65536)
            return false;  // 头太大
    }
}

// ---- 从 HTTP 头提取 Content-Length ----
[[nodiscard]] std::int64_t ParseContentLength(const std::string& headers) noexcept {
    const auto pos = headers.find("Content-Length:");
    if (pos == std::string::npos) {
        // 也可能是 chunked; 简化: 返回 -1 表示未知 (按 chunked 处理)
        return -1;
    }
    const auto num_start = pos + 15;  // "Content-Length:" length
    std::int64_t val = 0;
    bool found = false;
    for (auto i = num_start; i < headers.size() && i < num_start + 20; ++i) {
        if (headers[i] >= '0' && headers[i] <= '9') {
            val = val * 10 + (headers[i] - '0');
            found = true;
        } else if (found) {
            break;
        }
    }
    return found ? val : -1;
}

// ---- 解析 HTTP 状态码 ----
[[nodiscard]] int ParseStatusCode(const std::string& headers) noexcept {
    // "HTTP/1.1 200 OK\r\n..."
    if (headers.size() < 12)
        return 0;
    int code = 0;
    bool in_code = false;
    for (std::size_t i = 9; i < std::min(headers.size(), std::size_t{13}); ++i) {
        if (headers[i] >= '0' && headers[i] <= '9') {
            code = code * 10 + (headers[i] - '0');
            in_code = true;
        } else if (in_code) {
            break;
        }
    }
    return code;
}

// ---- 读取固定字节数 ----
[[nodiscard]] bool RecvExact(int fd, char* buf, std::size_t len) noexcept {
    std::size_t got = 0;
    while (got < len) {
        const ssize_t n = ::recv(fd, buf + got, len - got, 0);
        if (n <= 0)
            return false;
        got += static_cast<std::size_t>(n);
    }
    return true;
}

// ---- 读取 chunked Transfer-Encoding body ----
[[nodiscard]] bool ReadChunkedBody(int fd, std::string& body) noexcept {
    // chunk-size\r\n<data>\r\n  ... 0\r\n\r\n
    body.clear();
    char size_buf[32];
    while (true) {
        // 读 chunk size line (到 \r\n)
        std::string size_line;
        char c;
        while (true) {
            if (::recv(fd, &c, 1, 0) != 1)
                return false;
            if (c == '\n')
                break;
            if (c != '\r')
                size_line.push_back(c);
        }
        // 去掉 chunk extension (;...)
        const auto semi = size_line.find(';');
        const std::string hex_str = (semi != std::string::npos) ? size_line.substr(0, semi) : size_line;
        // hex_str → size
        (void)size_buf;
        unsigned long chunk_size = 0;
        try {
            chunk_size = std::stoul(hex_str, nullptr, 16);
        } catch (...) {
            return false;
        }
        if (chunk_size == 0) {
            // 结尾 \r\n
            char tail[2];
            (void)::recv(fd, tail, 2, 0);
            return true;
        }
        // 读 chunk 数据
        std::size_t old_size = body.size();
        body.resize(old_size + chunk_size);
        if (!RecvExact(fd, body.data() + old_size, chunk_size))
            return false;
        // 读尾部 \r\n
        char crlf[2];
        if (!RecvExact(fd, crlf, 2))
            return false;
        if (body.size() > 20 * 1024 * 1024)
            return false;  // 超 20MB 拒绝
    }
}

// ---- zlib gzip 解压 (gzip 炸弹防护) ----
//
// 安全加固 (小白审计 §1.3-B, 2026-05-30):
//   max_output_bytes = 8MB. 实证 Goalserve inplay 最大约 2-3MB, 留 buffer.
//   超限: inflateEnd + return false + stderr 告警.
//   防止: 1KB → 1GB 解压炸弹拖垮采集线程.
static constexpr std::size_t kMaxGzipOutputBytes = 8UL * 1024 * 1024;  // 8MB

[[nodiscard]] bool DecompressGzImpl(const std::string& gz_body, std::string& out_json) noexcept {
    out_json.clear();
    if (gz_body.empty())
        return false;

    z_stream zs{};
    // inflateInit2 with 16+MAX_WBITS = gzip 模式
    if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK)
        return false;

    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(gz_body.data()));
    zs.avail_in = static_cast<uInt>(gz_body.size());

    constexpr std::size_t kChunk = 65536;
    char chunk_buf[kChunk];

    int ret = Z_OK;
    do {
        zs.next_out = reinterpret_cast<Bytef*>(chunk_buf);
        zs.avail_out = kChunk;
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
            inflateEnd(&zs);
            return false;
        }
        const std::size_t have = kChunk - zs.avail_out;
        out_json.append(chunk_buf, have);
        if (out_json.size() > kMaxGzipOutputBytes) {
            // gzip 炸弹防护: 超 8MB 中止, 告警 (小白审计 §1.3-B)
            std::fprintf(stderr,
                         "[inplay_feed] WARN gzip_bomb_abort: output exceeded %zu bytes "
                         "(gz_input_sz=%zu), dropping frame\n",
                         kMaxGzipOutputBytes, gz_body.size());
            inflateEnd(&zs);
            out_json.clear();
            return false;
        }
    } while (ret != Z_STREAM_END);

    inflateEnd(&zs);
    return ret == Z_STREAM_END;
}

// ---- 解析代理环境变量 "http://host:port" 或 "host:port" → {host, port} ----
struct ProxySpec {
    std::string host;
    std::uint16_t port = 0;
    bool valid = false;
};

[[nodiscard]] ProxySpec ParseProxySpec(const std::string& proxy_str) noexcept {
    ProxySpec spec;
    if (proxy_str.empty())
        return spec;

    std::string s = proxy_str;
    // 去掉 "http://" 或 "https://"
    if (s.rfind("http://", 0) == 0)
        s = s.substr(7);
    else if (s.rfind("https://", 0) == 0)
        s = s.substr(8);

    // 去掉末尾 /
    while (!s.empty() && s.back() == '/')
        s.pop_back();

    // host:port
    const auto colon = s.rfind(':');
    if (colon == std::string::npos) {
        spec.host = s;
        spec.port = 3128;  // 默认代理端口
    } else {
        spec.host = s.substr(0, colon);
        try {
            spec.port = static_cast<std::uint16_t>(std::stoul(s.substr(colon + 1)));
        } catch (...) {
            return spec;
        }
    }
    spec.valid = !spec.host.empty() && spec.port > 0;
    return spec;
}

// ---- HTTP GET via 直连或 CONNECT 代理 ----
// 返回: {http_status, gz_body, ingestion_ns}; gz_body 空 = 失败
//
// 安全加固 (小白审计 §1.3-B, 2026-05-30):
//   kMaxBodyBytes = 16MB: HTTP body (gzip 压缩前) 上限, 超限 drop.
//   实证 Goalserve inplay.gz 压缩后约 30-100KB, 16MB 足够大又防资源耗尽.
//   凭证不入日志: 日志仅记 host + endpoint 类型, 完整 path (含 key) 严禁打印.
static constexpr std::size_t kMaxBodyBytes = 16UL * 1024 * 1024;  // 16MB

struct FetchResult {
    int status = 0;
    std::string body;
    std::int64_t ingestion_ns = 0;
    bool via_proxy = false;  // 是否走代理 (凭证保护状态)
};

[[nodiscard]] FetchResult HttpGetGz(const std::string& target_host, std::uint16_t target_port,
                                    const std::string& path, const std::string& proxy_str,
                                    std::uint32_t timeout_ms) noexcept {
    FetchResult result;

    const ProxySpec proxy = ParseProxySpec(proxy_str);
    result.via_proxy = proxy.valid;

    // 连接目标或代理
    const std::string& connect_host = proxy.valid ? proxy.host : target_host;
    const std::uint16_t connect_port = proxy.valid ? proxy.port : target_port;

    const int fd = TcpConnect(connect_host, connect_port, timeout_ms);
    if (fd < 0) {
        // 日志: 代理地址可以打, 但不含凭证; 直连 host 也不含路径
        std::fprintf(stderr, "[inplay_feed] connect failed: host=%s via=%s\n", target_host.c_str(),
                     proxy.valid ? "(proxy)" : "(direct)");
        return result;
    }

    // CONNECT 隧道 (代理时)
    if (proxy.valid) {
        const std::string connect_req = "CONNECT " + target_host + ":" + std::to_string(target_port) +
                                        " HTTP/1.1\r\nHost: " + target_host + "\r\n\r\n";
        if (!SendAll(fd, connect_req.data(), connect_req.size())) {
            ::close(fd);
            return result;
        }
        std::string proxy_resp;
        if (!ReadHttpHeaders(fd, proxy_resp)) {
            ::close(fd);
            return result;
        }
        if (ParseStatusCode(proxy_resp) != 200) {
            std::fprintf(stderr, "[inplay_feed] CONNECT tunnel failed: status=%d host=%s\n",
                         ParseStatusCode(proxy_resp), target_host.c_str());
            ::close(fd);
            return result;
        }
    }

    // HTTP/1.1 GET
    // 安全: path 含 API key, 严禁进日志 (小白审计 §3.2 + §5.3)
    const std::string req = "GET " + path + " HTTP/1.1\r\n" + "Host: " + target_host + "\r\n" +
                            "Accept-Encoding: gzip\r\n" + "Connection: close\r\n\r\n";
    if (!SendAll(fd, req.data(), req.size())) {
        ::close(fd);
        return result;
    }

    // 读响应头
    std::string headers;
    if (!ReadHttpHeaders(fd, headers)) {
        ::close(fd);
        return result;
    }
    result.status = ParseStatusCode(headers);

    if (result.status != 200) {
        // 日志: 不打 path (含 key), 只记 host + status
        std::fprintf(stderr, "[inplay_feed] HTTP status=%d host=%s (path redacted)\n", result.status,
                     target_host.c_str());
        ::close(fd);
        return result;
    }

    // 读 body
    const std::int64_t content_length = ParseContentLength(headers);
    bool ok = false;
    if (content_length >= 0) {
        // content-length 路径: 加上限检查 (小白审计 §1.3-B)
        if (static_cast<std::size_t>(content_length) > kMaxBodyBytes) {
            std::fprintf(stderr, "[inplay_feed] WARN max_body_bytes: content_length=%lld > %zu, dropping\n",
                         static_cast<long long>(content_length), kMaxBodyBytes);
            ::close(fd);
            return result;
        }
        result.body.resize(static_cast<std::size_t>(content_length));
        ok = RecvExact(fd, result.body.data(), static_cast<std::size_t>(content_length));
    } else {
        // chunked 或 content-length 未知
        const auto te_pos = headers.find("Transfer-Encoding: chunked");
        if (te_pos != std::string::npos) {
            ok = ReadChunkedBody(fd, result.body);
        } else {
            // 读到连接关闭
            char buf[8192];
            ssize_t n;
            while ((n = ::recv(fd, buf, sizeof(buf), 0)) > 0) {
                result.body.append(buf, static_cast<std::size_t>(n));
                if (result.body.size() > kMaxBodyBytes) {
                    std::fprintf(stderr,
                                 "[inplay_feed] WARN max_body_bytes: body exceeded %zu bytes, dropping\n",
                                 kMaxBodyBytes);
                    result.body.clear();
                    break;
                }
            }
            ok = !result.body.empty();
        }
    }

    // ingestion_ts: body 收完时刻 (R-20)
    result.ingestion_ns = NowNs();

    ::close(fd);

    if (!ok || result.body.empty()) {
        result.status = 0;
        result.body.clear();
    }

    return result;
}

}  // anonymous namespace

// ============================================================================
// InplayFeedThread 实现
// ============================================================================

InplayFeedThread::InplayFeedThread(ScoreSnapshotStore& store, InplayFeedConfig cfg) noexcept
    : store_(store), cfg_(std::move(cfg)) {
    // 如果 http_proxy 未配置, 从环境变量读取
    if (cfg_.http_proxy.empty()) {
        cfg_.http_proxy = ReadProxyFromEnv();
    }
    // 初始化诊断计数器
    for (auto& v : last_updated_ts_ms_)
        v.store(0, std::memory_order_relaxed);
    for (auto& v : last_event_count_)
        v.store(0, std::memory_order_relaxed);
}

InplayFeedThread::~InplayFeedThread() {
    Stop();
}

void InplayFeedThread::Start() {
    if (running_.exchange(true, std::memory_order_acq_rel)) {
        return;  // 已在运行
    }
    stop_.store(false, std::memory_order_release);
    threads_.reserve(cfg_.sports.size());
    for (const auto sport : cfg_.sports) {
        threads_.emplace_back([this, sport]() { RunSportLoop(sport); });
    }
    // 凭证不入日志: proxy 地址可能含认证信息 (user:pass@host), 只显示连接方式
    // inplay.goalserve.com 走 http (明文), 需 proxy 保护 key (小白审计 §3.2/§4.2)
    std::fprintf(stderr, "[inplay_feed] Started %zu sport thread(s). route=%s\n", cfg_.sports.size(),
                 cfg_.http_proxy.empty() ? "direct(WARN:insecure)" : "via-proxy");
}

void InplayFeedThread::Stop() {
    stop_.store(true, std::memory_order_release);
    for (auto& t : threads_) {
        if (t.joinable())
            t.join();
    }
    threads_.clear();
    running_.store(false, std::memory_order_release);
}

std::int64_t InplayFeedThread::last_updated_ts_ms(goalserve::GoalserveSport sport) const noexcept {
    const auto idx = static_cast<std::size_t>(sport);
    if (idx >= kNumSports)
        return 0;
    return last_updated_ts_ms_[idx].load(std::memory_order_relaxed);
}

std::int64_t InplayFeedThread::last_event_count(goalserve::GoalserveSport sport) const noexcept {
    const auto idx = static_cast<std::size_t>(sport);
    if (idx >= kNumSports)
        return 0;
    return last_event_count_[idx].load(std::memory_order_relaxed);
}

// ---- 单 sport 采集循环 ----
void InplayFeedThread::RunSportLoop(goalserve::GoalserveSport sport) noexcept {
    const std::string sport_slug(goalserve::SportInplaySlug(sport));
    const std::string path = "/inplay-" + sport_slug + ".gz";
    const auto sport_idx = static_cast<std::size_t>(sport);

    std::uint32_t backoff_ms = 0;
    std::uint32_t fail_count = 0;

    // 凭证不入日志: path 可能含 API key, 仅记 sport + host (小白审计 §3.2/§5.3)
    std::fprintf(stderr, "[inplay_feed] sport=%s thread started, host=%s (path redacted)\n",
                 sport_slug.c_str(), cfg_.inplay_host.c_str());

    // token-bucket: 记录上次 fetch 完成时刻 (单调时钟 ms), 强制 min_fetch_interval_ms 间隔。
    //   【每 sport 线程独立】—— Goalserve 限速是 per-sport (1 req/s/sport, laochen SSOT §rate),
    //   不是 per-IP 全局, 故每 sport 各自限速正是对的 (别改全局, 会把 4 sport 挤到共享 1/s 浪费配额)。
    std::uint64_t last_fetch_mono_ms = 0;

    // 简单单调 ms 时钟 (采集线程内部用, 不替代 R-20 ts)
    auto mono_ms_now = []() -> std::uint64_t {
        struct timespec ts{};
        ::clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<std::uint64_t>(ts.tv_sec) * 1000ULL +
               static_cast<std::uint64_t>(ts.tv_nsec) / 1'000'000ULL;
    };
    // CLOCK_REALTIME ms — 与 Goalserve updated_ts 同域 (相位对齐锚 feed 真实更新时刻, 见 R-20 注释 §68)。
    auto rt_ms_now = []() -> std::int64_t {
        struct timespec ts{};
        ::clock_gettime(CLOCK_REALTIME, &ts);
        return static_cast<std::int64_t>(ts.tv_sec) * 1000LL +
               static_cast<std::int64_t>(ts.tv_nsec) / 1'000'000LL;
    };

    // 相位对齐状态 (2026-06-05 老板「保持频率, 只做相位对齐」): 锚 feed 真实 updated_ts (非我方抓到时刻,
    //   无相位反馈漂移); 间隔 EMA 自适应。详见 Config 注释 + docs/RESEARCH/laolei-inplay-clock-alignment-v1.md。
    std::int64_t phase_prev_updated_ts = 0;     // 上版 feed updated_ts (检测新版本 + 算真实间隔)
    std::int64_t phase_interval_ema_ms = 2000;  // feed 版本间隔 EMA (ms; 锚 updated_ts 差, init 2s; 自适应)
    std::int64_t phase_corr_ms = 0;             // 自适应相位校正 (闭环: 实测抓取延迟 → 自动调 aim-point, 每 sport 自收敛)

    // 赛果盘 market_id 集合 (本 sport, thread-local 无锁): Goalserve 字典解析所得 (2026-06-04 老板
    //   「用字典匹配, 别只靠白名单」)。启动拉一次 + 每 kDictRefreshMs 刷新 (市场名/id 极少变)。空 →
    //   Parse 回退 IsResultMarketName 启发式 (按 feed market name 选)。
    std::unordered_set<std::string> result_ids;
    std::uint64_t last_dict_fetch_ms = 0;
    constexpr std::uint64_t kDictRefreshMs = 6ULL * 3600ULL * 1000ULL;  // 6h

    while (!stop_.load(std::memory_order_acquire)) {
        // 周期性拉字典 → 解析赛果盘 id-set (启动即拉; 之后 6h 刷新)。失败保留旧集 (不清空)。
        {
            const std::uint64_t now_ms = mono_ms_now();
            if (last_dict_fetch_ms == 0 || now_ms >= last_dict_fetch_ms + kDictRefreshMs) {
                std::string dict_json;
                if (FetchDict(sport, dict_json)) {
                    auto ids = ParseResultMarketIdsFromDict(dict_json);
                    if (!ids.empty()) {
                        result_ids = std::move(ids);
                        std::fprintf(stderr, "[inplay_dict] sport=%s 赛果盘 id-set=%zu (字典驱动选盘)\n",
                                     sport_slug.c_str(), result_ids.size());
                    } else {
                        std::fprintf(stderr,
                                     "[inplay_dict] sport=%s 字典无赛果盘命中 → 回退启发式选盘\n",
                                     sport_slug.c_str());
                    }
                    last_dict_fetch_ms = now_ms;  // 仅成功才记时, 失败下轮重试
                } else {
                    std::fprintf(stderr, "[inplay_dict] sport=%s 字典拉取失败 → 回退启发式 (下轮重试)\n",
                                 sport_slug.c_str());
                    // 失败不更新 last_dict_fetch_ms? 会每轮重试打爆。设一个短重试节流: 记当前时刻但
                    //   减去大部分间隔, 使 ~60s 后重试 (而非 6h 后)。
                    last_dict_fetch_ms = now_ms - (kDictRefreshMs - 60ULL * 1000ULL);
                }
            }
        }
        // token-bucket 速率控制 (per-sport): 距上次 fetch 不足 min_fetch_interval_ms 则等待。
        {
            const std::uint64_t now_ms = mono_ms_now();
            if (last_fetch_mono_ms > 0 && now_ms < last_fetch_mono_ms + cfg_.min_fetch_interval_ms) {
                const std::uint32_t wait_ms =
                    static_cast<std::uint32_t>(last_fetch_mono_ms + cfg_.min_fetch_interval_ms - now_ms);
                SleepMs(wait_ms);
            }
        }

        // 拉取 gz
        std::string gz_body;
        std::int64_t ingestion_ns = 0;
        const bool fetch_ok = FetchGz(sport, gz_body, ingestion_ns);
        last_fetch_mono_ms = mono_ms_now();  // 记录本次 fetch 完成时刻

        if (!fetch_ok || gz_body.empty()) {
            ++fail_count;
            backoff_ms =
                std::min(cfg_.max_backoff_ms, fail_count <= 5 ? fail_count * 2000u : cfg_.max_backoff_ms);
            std::fprintf(stderr, "[inplay_feed] sport=%s fetch failed (fail#%u), backoff=%ums\n",
                         sport_slug.c_str(), fail_count, backoff_ms);
            SleepMs(backoff_ms);
            continue;
        }
        fail_count = 0;
        backoff_ms = 0;

        // gzip 解压
        std::string json_body;
        if (!DecompressGz(gz_body, json_body)) {
            std::fprintf(stderr, "[inplay_feed] sport=%s gzip decompress failed (body_sz=%zu)\n",
                         sport_slug.c_str(), gz_body.size());
            SleepMs(cfg_.poll_interval_ms);
            continue;
        }

        // 解析 GameScoreRecord (传字典赛果盘 id-set → SelectResultMarketId 优先按 id 匹配)
        const auto parse_result =
            inplay::InplayScoreParser::Parse(json_body, sport, ingestion_ns, &result_ids);

        // 记录 parse_errors (非致命, 继续)
        if (!parse_result.parse_errors.empty()) {
            std::fprintf(stderr, "[inplay_feed] sport=%s parse_errors=%zu (first: %s)\n", sport_slug.c_str(),
                         parse_result.parse_errors.size(), parse_result.parse_errors[0].c_str());
        }

        // 构建本 sport 的 EventScore 条目 (R-20: key = inplay_match_id)
        // 注: 多 sport 线程共用同一 ScoreSnapshotStore, 需要 merge 才能共存.
        // 方案: 持 merged_mu_ → 更新 merged_map_ 中本 sport 的 key → Publish merged map.
        // merge 临界区持锁时间约 O(events) 哈希操作, 非 IO, 远 < 100us (R-12 合规).
        {
            std::lock_guard<std::mutex> lk(merged_mu_);
            // 删除本 sport 的旧 key (避免已结束比赛残留)
            // 用 sport_slug 前缀作为简单标记? 不行 (key 是 inplay_match_id, 无 sport 前缀)
            // 正确: 维护 per-sport key set, 先删旧, 再写新
            auto& old_keys = sport_keys_[sport_idx];
            // E2 事件检测: 删旧 key【前】快照上一帧 (score+state), 供下面帧间 diff。
            //   (本 sport 的旧 key 会被先 erase 再重写, erase 后 merged_map_ 没了上一帧 → 必须先快照。)
            std::unordered_map<std::string, std::tuple<int, int, std::string>> prev_snap;
            for (const auto& k : old_keys) {
                if (auto it = merged_map_.find(k); it != merged_map_.end())
                    prev_snap.emplace(k, std::make_tuple(it->second.home_score, it->second.away_score,
                                                         it->second.gs_state_code));
            }
            for (const auto& old_key : old_keys) {
                merged_map_.erase(old_key);
            }
            old_keys.clear();
            // 写入新 events
            for (std::size_t i = 0; i < parse_result.scores.size(); ++i) {
                const auto& rec = parse_result.scores[i];
                debug_api::EventScore es = ToEventScore(rec, sport);
                // inplay bet365 de-vig 三边 fair (home/away/draw 视角, 双边完整; 与 scores 1:1; -1=无 odds)。
                if (i < parse_result.inplay_home_fairs.size())
                    es.inplay_bet365_home_fair = parse_result.inplay_home_fairs[i];
                if (i < parse_result.inplay_away_fairs.size())
                    es.inplay_bet365_away_fair = parse_result.inplay_away_fairs[i];
                if (i < parse_result.inplay_draw_fairs.size())
                    es.inplay_bet365_draw_fair = parse_result.inplay_draw_fairs[i];
                // A-step-2 分局盘当前段 fair (与 scores 1:1; -1=当前段无赔率)。
                if (i < parse_result.inplay_seg_home_fairs.size())
                    es.inplay_seg_home_fair = parse_result.inplay_seg_home_fairs[i];
                if (i < parse_result.inplay_seg_away_fairs.size())
                    es.inplay_seg_away_fair = parse_result.inplay_seg_away_fairs[i];
                if (i < parse_result.inplay_seg_index.size())
                    es.inplay_seg_index = parse_result.inplay_seg_index[i];
                const std::string& key = rec.match_id.inplay_match_id;
                if (!key.empty()) {
                    // E2 事件检测器 (v3 事件套利; 只计数+log, 不交易; 双架构评审: 检测内联采集线程,
                    //   帧间纯内存 diff <<R-12 100μs, 不碰 WSS loop)。diff 新 es vs 上一帧 merged_map_[key]:
                    //   比分变化(进球/得分/跑垒) + state 码跳变(事件转移)。单 writer 线程, plain static 计数。
                    if (auto pit = prev_snap.find(key); pit != prev_snap.end()) {
                        const auto& [ph, pa, pstate] = pit->second;
                        const bool score_chg = (es.home_score != ph) || (es.away_score != pa);
                        const bool state_chg = !es.gs_state_code.empty() && es.gs_state_code != pstate;
                        if (score_chg || state_chg) {
                            static long long ev_n = 0, ev_score = 0, ev_state = 0;
                            ++ev_n;
                            if (score_chg) ++ev_score;
                            if (state_chg) ++ev_state;
                            static int dbg = 0;
                            if (dbg++ < 300)
                                std::fprintf(stderr,
                                             "[event-detect] %s %.16s %s sc=%d:%d->%d:%d state=%s->%s "
                                             "(n=%lld score=%lld state=%lld)\n",
                                             es.sport.c_str(), key.c_str(), score_chg ? "SCORE" : "state",
                                             ph, pa, es.home_score, es.away_score, pstate.c_str(),
                                             es.gs_state_code.c_str(), ev_n, ev_score, ev_state);
                        }
                    }
                    merged_map_[key] = std::move(es);
                    old_keys.insert(key);
                }
            }
            // Publish merged map (拷贝一份 shared_ptr)
            store_.Publish(std::make_shared<ScoreMap>(merged_map_));
        }

        // 相位对齐: 检测新版本 → 用 feed 真实 updated_ts 间隔更新 EMA (锚真实更新节奏, 无相位反馈漂移)。
        if (cfg_.phase_align_enabled && parse_result.updated_ts_ms > phase_prev_updated_ts) {
            // 自适应相位对齐 (2026-06-05 老板「自适应」): 用实测抓取延迟 L 闭环校正 aim-point, 每 sport 自动收敛,
            //   无需人读日志。L = 收到时刻(realtime) − feed 版本 updated_ts。
            const std::int64_t L = rt_ms_now() - parse_result.updated_ts_ms;
            if (phase_prev_updated_ts > 0) {
                const std::int64_t gap = parse_result.updated_ts_ms - phase_prev_updated_ts;
                if (gap >= 500 && gap <= 8000)  // 滤异常 → 间隔 EMA 自适应 (α=0.3)
                    phase_interval_ema_ms = (phase_interval_ema_ms * 7 + gap * 3) / 10;
            }
            // 闭环【双向】校正 (2026-06-05 老板「左右偏移都要算, 不能只减; 落到1.99就+0.05顶到2.04」):
            //   L 偏高(抓晚)→ phase_corr↑ → 瞄更早; L 偏低(抓太早/快撞到更新前)→ phase_corr↓(转负) → 瞄更晚(+offset)。
            //   伺服到目标 ~250ms。clamp [−max_nudge, margin−safety]: 负=往后顶(老板的+0.05), 正=往前。
            //   safety=30ms (老板「压太狠了, 留30ms余量让他稳定」): aim-offset 下限 30ms, 永远瞄在更新后 ≥30ms,
            //   留余量吸收 interval 预测误差 → 不会抢在更新前漏版(消除 max 尖峰), 用 30ms 换稳定。
            if (L >= 0 && L < phase_interval_ema_ms * 3 / 2) {
                constexpr std::int64_t kPhaseSafetyMs = 30;  // 留 30ms 余量防漏版 (老板)
                phase_corr_ms += (L - 250) * 3 / 10;  // 比例增益 0.3 (双向: err 正往早, err 负往晚)
                const std::int64_t lo = -static_cast<std::int64_t>(cfg_.phase_max_nudge_ms);  // 允许往后顶
                const std::int64_t hi = static_cast<std::int64_t>(cfg_.phase_margin_ms) - kPhaseSafetyMs;  // aim≥update+30ms
                if (phase_corr_ms < lo) phase_corr_ms = lo;
                if (phase_corr_ms > hi) phase_corr_ms = hi;
            }
            phase_prev_updated_ts = parse_result.updated_ts_ms;
        }

        // 诊断计数器更新
        if (sport_idx < kNumSports) {
            last_updated_ts_ms_[sport_idx].store(parse_result.updated_ts_ms, std::memory_order_relaxed);
            last_event_count_[sport_idx].store(static_cast<std::int64_t>(parse_result.scores.size()),
                                               std::memory_order_relaxed);
        }

        std::fprintf(stderr,
                     "[inplay_feed] sport=%-12s events=%2zu updated_ts_ms=%lld gz_sz=%zu json_sz=%zu\n",
                     sport_slug.c_str(), parse_result.scores.size(),
                     static_cast<long long>(parse_result.updated_ts_ms), gz_body.size(), json_body.size());

        // 相位对齐 sleep (2026-06-05 老板「不提频~1s, 对齐相位, 每次拿到最新版本不漏版」): 瞄准【下一次 feed
        //   更新刚发生后】抓 (updated_ts + n×EMA间隔 + margin − phase_corr; O(1) ceil 算出未来那一版, 非 while)。
        //   每收新版本重锚 updated_ts → 消累积漂移。sleep 钳 [floor(限速), 不漏版上限(=间隔−fetch余量)]:
        //   保证下次轮询落在下一版之前 → 每版必抓到(无 3-4s 尖峰)。SleepMs 让出 CPU, 不 busy-wait。
        std::uint32_t sleep_ms = cfg_.poll_interval_ms;
        if (cfg_.phase_align_enabled && phase_prev_updated_ts > 0 && phase_interval_ema_ms >= 500) {
            const std::int64_t now_rt = rt_ms_now();
            const std::int64_t floor = static_cast<std::int64_t>(cfg_.poll_interval_ms);
            // 算出 ≥ now+floor 的那一版 catch (O(1) 算术, 不用 while 循环 — 老板「别独占cpu写死while循环」;
            //   原 while 若 interval_ema=0 会死循环, 现直接 ceil 除法一步到位)。base = 相位基准(自适应瞄点)。
            const std::int64_t base = phase_prev_updated_ts +
                                      static_cast<std::int64_t>(cfg_.phase_margin_ms) - phase_corr_ms;
            const std::int64_t need = now_rt + floor - base;
            const std::int64_t k = (need <= phase_interval_ema_ms)
                                       ? 1
                                       : (need + phase_interval_ema_ms - 1) / phase_interval_ema_ms;  // ceil≥1
            const std::int64_t target = base + k * phase_interval_ema_ms;
            std::int64_t w = target - now_rt;
            // 【不漏版上限】(2026-06-05 老板「每次都能拿到最新的版本」核心): sleep + fetch 必须 < 版本间隔,
            //   否则两次轮询跨过一整版 → 漏版 → 年龄尖峰到 3-4s。cap = interval_ema − kFetchSafetyMs(400, fetch往返+余量),
            //   保证下次轮询一定落在下一版【之前】, 每版必被抓到。也不超 nudge 上限; 极快 feed 退化到 floor(限速兜底)。
            constexpr std::int64_t kFetchSafetyMs = 400;
            std::int64_t cap = phase_interval_ema_ms - kFetchSafetyMs;
            const std::int64_t nudge_cap = floor + static_cast<std::int64_t>(cfg_.phase_max_nudge_ms);
            if (cap > nudge_cap) cap = nudge_cap;
            if (cap < floor) cap = floor;
            if (w < floor) w = floor;     // 不破限速
            if (w > cap) w = cap;         // 不漏版 (gap < 版本间隔)
            sleep_ms = static_cast<std::uint32_t>(w);  // 总是瞄相位, 但钳在 [floor, 不漏版上限]
        }
        SleepMs(sleep_ms);
    }

    std::fprintf(stderr, "[inplay_feed] sport=%s thread stopped.\n", sport_slug.c_str());
}

// ---- FetchGz: HTTP GET → gzip body ----
//
// 安全加固 (小白审计 §2.2/§4.2, 2026-05-30):
//   1. insecure_fetch 告警: inplay.goalserve.com 走 http (明文), 若无代理则记 WARN.
//      key 在 URL query, 明文跨洋 → 走 GOALSERVE_PROXY 才能保护 key.
//   2. token-bucket: min_interval_ms (cfg.min_fetch_interval_ms, 默认 800ms) 强制间隔.
//      防止 backoff=0 时连续暴击 (尊重 Goalserve ToS rate limit).
bool InplayFeedThread::FetchGz(goalserve::GoalserveSport sport, std::string& gz_body,
                               std::int64_t& ingestion_ns) noexcept {
    const std::string path = "/inplay-" + std::string(goalserve::SportInplaySlug(sport)) + ".gz";

    // insecure_fetch 告警: inplay 强制 http, 无代理时 key 明文跨洋 (小白审计 §4.2)
    if (cfg_.http_proxy.empty()) {
        std::fprintf(stderr,
                     "[inplay_feed] WARN insecure_fetch host=%s: no proxy configured, "
                     "API key travels in plaintext over HTTP. Set GOALSERVE_PROXY.\n",
                     cfg_.inplay_host.c_str());
    }

    const auto result =
        HttpGetGz(cfg_.inplay_host, cfg_.inplay_port, path, cfg_.http_proxy, cfg_.http_timeout_ms);

    if (result.status != 200 || result.body.empty()) {
        return false;
    }

    gz_body = result.body;
    ingestion_ns = result.ingestion_ns;
    return true;
}

// ---- FetchDict: HTTP GET dictionaries/odds-markets/<sport> → 明文 JSON ----
//   2026-06-04 老板「用 goalserve 字典匹配功能」: 字典端点公开 (无 key) + 明文 (非 .gz)。
//   复用 HttpGetGz (通用 GET; gunzip 另在 DecompressGz, 此处明文不解压)。极少数情况服务器仍可能
//   gzip → magic byte (1f 8b) 检测兜底解压。
bool InplayFeedThread::FetchDict(goalserve::GoalserveSport sport, std::string& json_body) noexcept {
    const std::string path =
        "/dictionaries/odds-markets/" + std::string(goalserve::SportInplaySlug(sport)) + "?json=1";
    const auto result =
        HttpGetGz(cfg_.inplay_host, cfg_.inplay_port, path, cfg_.http_proxy, cfg_.http_timeout_ms);
    if (result.status != 200 || result.body.empty()) return false;
    // gzip magic 兜底 (字典通常明文, 但若服务器压缩则解之)。
    if (result.body.size() >= 2 && static_cast<unsigned char>(result.body[0]) == 0x1f &&
        static_cast<unsigned char>(result.body[1]) == 0x8b) {
        std::string out;
        if (!DecompressGz(result.body, out)) return false;
        json_body = std::move(out);
    } else {
        json_body = result.body;
    }
    return true;
}

// ---- DecompressGz ----
bool InplayFeedThread::DecompressGz(const std::string& gz_body, std::string& out_json) noexcept {
    return DecompressGzImpl(gz_body, out_json);
}

// ---- ToEventScore: GameScoreRecord → EventScore (for ScoreSnapshotStore) ----
debug_api::EventScore InplayFeedThread::ToEventScore(const data::adapter::GameScoreRecord& rec,
                                                     goalserve::GoalserveSport sport) noexcept {
    debug_api::EventScore es;
    es.found = true;
    es.event_id = rec.match_id.inplay_match_id;
    es.home = rec.home_team;
    es.away = rec.away_team;
    es.home_score = rec.home_score_total;
    es.away_score = rec.away_score_total;
    es.games_home = rec.home_games_total;  // 网球: 全场总局数 (totals/spreads); 非网球 0
    es.games_away = rec.away_games_total;
    // 实时比分 (老板 2026-06-03「显示实时比分」, inplay 主源): 逐盘比分 "2-6 6-4" (home-away/盘)。
    //   从 GameScoreRecord.home_periods/away_periods (inplay info.score "2:6,6:4" 解析); 非网球 periods_used=0。
    if (rec.periods_used > 0) {
        std::string ss;
        for (std::uint8_t i = 0; i < rec.periods_used && i < 12; ++i) {
            if (!ss.empty()) ss += ' ';
            ss += std::to_string(rec.home_periods[i]);
            ss += '-';
            ss += std::to_string(rec.away_periods[i]);
        }
        es.set_summary = std::move(ss);
    }
    es.source = "goalserve";
    // v3 事件套利地基 (E1): 透传 Goalserve info.state 5位瞬时事件码 (已抓进 rec 但此前未传到
    //   EventScore → 下游零消费)。纯事实字段透传, 不产信号语义 (语义/触发归下游)。空=无。
    es.gs_state_code = rec.gs_state_code.value_or("");

    // sport 字符串
    es.sport = std::string(goalserve::SportInplaySlug(sport));

    // period
    es.period = rec.period.value_or("");

    // clock_sec: 归一为【全场累计已用秒】(喂 time_frac = elapsed / total_game_seconds)。
    //   足球: minute 本就是全场累计 (81' in 2nd half), 直接用 — 正确, 不动。
    //   倒计时制运动 (篮球/冰球/橄榄球): minute 是【节内倒计时剩余】(实测篮球第3节 minute=8=还剩8分),
    //     此前当已用算 → time_frac 方向反 → 错误定价。修: 转全场累计 elapsed = (节号-1)×节长 +
    //     (节长 - 节内剩余)。节长: 篮球 NBA/CBA 12min (FIBA 10min 近似), 冰球 NHL 20min×3,
    //     橄榄球 NFL 15min×4。(2026-06-02 特征审计: 篮球实测 / 冰球·橄榄球后台验证强推断同构 —
    //     同 Goalserve bet365 接口同字段, 休赛无 live 直验, 待开赛复核。)
    //   无时钟运动 (网球/棒球/排球 total_game_seconds=0): time_frac 恒 0, clock_sec 不影响定价。
    es.clock_sec = 0;
    if (rec.elapsed_min.has_value()) {
        const std::int64_t raw = static_cast<std::int64_t>(*rec.elapsed_min) * 60 +
                                 static_cast<std::int64_t>(rec.elapsed_sec.value_or(0));
        std::int64_t period_len_sec = 0;  // >0 = 倒计时制, 需转累计
        int reg_periods = 0;
        switch (sport) {
            case goalserve::GoalserveSport::Basketball:
                period_len_sec = 12 * 60;
                reg_periods = 4;
                break;
            case goalserve::GoalserveSport::Hockey:
                period_len_sec = 20 * 60;
                reg_periods = 3;
                break;
            case goalserve::GoalserveSport::AmericanFootball:
                period_len_sec = 15 * 60;
                reg_periods = 4;
                break;
            default:
                break;  // soccer (累计正计时) / 无时钟运动 → 直用 raw
        }
        if (period_len_sec > 0) {
            int pnum = 0;  // es.period 首个数字: "3rd Quarter"/"2nd Period"→3/2; 无数字 (OT) → 0
            for (char c : es.period) {
                if (c >= '1' && c <= '9') {
                    pnum = c - '0';
                    break;
                }
            }
            if (pnum >= 1) {
                std::int64_t within = period_len_sec - raw;  // raw=节内剩余 → 节内已用
                if (within < 0)
                    within = 0;
                es.clock_sec = static_cast<std::int64_t>(pnum - 1) * period_len_sec + within;
            } else {
                es.clock_sec = static_cast<std::int64_t>(reg_periods) * period_len_sec;  // OT → regulation 末
            }
        } else {
            es.clock_sec = raw;  // soccer 累计 (正确) + 无时钟运动
        }
    }

    // status: inplay feed 事件均为 InPlay; MapStatus 处理 Half Time
    es.status = inplay::InplayScoreParser::MapStatus(rec.status, es.period);

    // 4ts 透传 (R-20)
    es.ts.event_ts_ns = rec.ts.event_ts_ns;
    es.ts.data_source_ts_ns = rec.ts.data_source_ts_ns;
    es.ts.ingestion_ts_ns = rec.ts.ingestion_ts_ns;
    es.ts.as_of_ts_ns = rec.ts.as_of_ts_ns;

    // A0 映射桥: league_id + kickoff_ts_sec (EventMatcher 锚定用)
    es.league_id = rec.match_id.league_id;
    // kickoff = 真实排定开赛 (scheduled_kickoff_ts_sec; 0=未知)。**不再用 event_ts_ns** —— 后者在
    //   start_ts 空时回落 data_source_ts(now), 会让 esports 等无 start_ts 的源 kickoff=now, 与 PM
    //   排定 gameStartTime 差 >窗口 → EventMatcher 误拒 (实测电竞 0 匹配根因 2026-06-01)。
    //   0=未知 → 匹配器跳过时间窗 (只按队名匹配); 有真 start_ts(网球等) → 时间窗正常生效。
    es.kickoff_ts_sec = rec.scheduled_kickoff_ts_sec;

    return es;
}

// ---- ReadProxyFromEnv ----
std::string InplayFeedThread::ReadProxyFromEnv() noexcept {
    // 按优先级: http_proxy → HTTP_PROXY → (空)
    const char* p = std::getenv("http_proxy");
    if (p && p[0] != '\0')
        return p;
    p = std::getenv("HTTP_PROXY");
    if (p && p[0] != '\0')
        return p;
    return {};
}

// ---- InjectSupplementalScores: 并入补充比分源 (tennis_scores / cricket / esports livescore) ----
//   覆盖率杠杆 (2026-06-03): inplay-*.gz (bet365 联动) 缺这些 (tennis ITF/Challenger, cricket 全部
//   [inplay-cricket 404], esports 大半) → 补充源补候选池。merge 进 merged_map_ 并 republish, 与
//   RunSportLoop 共用 merged_mu_ (单一发布者口径)。
//   多源隔离: source_id ("tennis_scores"/"cricket"/"esports") 各自 key 集, 刷新只删本源旧 key。
//   sport-aware 去重: 同 sport 同对阵已被现有条目占 (inplay 带 odds 或其他源) → 跳过, 不盖 sharp fair。
std::size_t InplayFeedThread::InjectSupplementalScores(const std::string& source_id,
                                                       std::vector<debug_api::EventScore> recs) noexcept {
    // sport-aware 对阵键: tennis 用末段姓 (容忍 "M. Malige"/"Mae Malige" 格式差异);
    //   队制 (cricket/esports) 用归一化全名 (lowercase alnum token 排序 join, 容忍大小写/空白)。
    auto surname = [](const std::string& name) -> std::string {
        std::size_t e = name.size();
        while (e > 0 && (name[e - 1] == ' ' || name[e - 1] == '\t'))
            --e;
        std::size_t b = e;
        while (b > 0 && name[b - 1] != ' ' && name[b - 1] != '/')
            --b;
        std::string s = name.substr(b, e - b);
        for (auto& c : s)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c + 32);
        return s;
    };
    auto team_norm = [](const std::string& name) -> std::string {
        std::string out;
        out.reserve(name.size());
        for (char c : name) {
            const unsigned char uc = static_cast<unsigned char>(c);
            if ((uc >= '0' && uc <= '9') || (uc >= 'a' && uc <= 'z'))
                out.push_back(static_cast<char>(uc));
            else if (uc >= 'A' && uc <= 'Z')
                out.push_back(static_cast<char>(uc + 32));
            // 其余 (空白/标点) 丢弃 → "Natus Vincere"→"natusvincere", "G2 Esports"→"g2esports"
        }
        return out;
    };
    auto entity_key = [&surname, &team_norm](const std::string& sport, const std::string& name) -> std::string {
        return (sport == "tennis") ? surname(name) : team_norm(name);
    };
    auto pair_key = [&entity_key](const std::string& sport, const std::string& h,
                                  const std::string& a) -> std::string {
        std::string kh = entity_key(sport, h), ka = entity_key(sport, a);
        if (kh > ka)
            std::swap(kh, ka);
        return sport + ":" + kh + "|" + ka;  // sport 前缀 → 跨 sport 绝不误去重
    };

    std::lock_guard<std::mutex> lk(merged_mu_);
    // 删【本源】上轮 key (避免已结束/已切换残留); 不动其他源/inplay。
    auto& my_keys = supplemental_keys_by_source_[source_id];
    for (const auto& k : my_keys)
        merged_map_.erase(k);
    my_keys.clear();
    // 去重锚: 现有 merged_map_ (inplay + 其他补充源) 全部对阵键 (sport-aware)。
    std::set<std::string> seen;
    for (const auto& [k, es] : merged_map_)
        seen.insert(pair_key(es.sport, es.home, es.away));
    std::size_t injected = 0;
    for (auto& es : recs) {
        if (es.event_id.empty() || es.home.empty() || es.away.empty())
            continue;
        const std::string pk = pair_key(es.sport, es.home, es.away);
        if (seen.count(pk))
            continue;  // 已有此场 (inplay 带 odds, 或其他源) → 跳过, 不盖
        seen.insert(pk);
        const std::string key = es.event_id;
        merged_map_[key] = std::move(es);
        my_keys.insert(key);
        ++injected;
    }
    store_.Publish(std::make_shared<ScoreMap>(merged_map_));
    return injected;  // post-dedup 净注入数 (调用方日志诊断)
}

}  // namespace stcpp::data
