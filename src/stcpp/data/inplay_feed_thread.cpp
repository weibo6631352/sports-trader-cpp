// stcpp/data/inplay_feed_thread.cpp — Goalserve inplay 采集线程实现
//
// Owner: 小段 (goalserve-specialist, #37)
// Date:  2026-05-29
// Task:  W5 真实 HTTP 接入 — POSIX socket + zlib gzip
//
// 实现说明:
//   HTTP 层: POSIX socket (无第三方库, 匹配跨平台 C++20 标准库方法)
//   代理支持: HTTP CONNECT 隧道 (读 http_proxy / HTTP_PROXY 环境变量)
//   gzip 解压: zlib inflate (系统库, macOS + Linux 均有)
//   线程: 每 sport 独立 std::thread (防单 sport 阻塞)
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
#include <unordered_map>

// POSIX
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <netdb.h>
#include <unistd.h>

// zlib
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

// ---- zlib gzip 解压 ----
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
        if (out_json.size() > 50 * 1024 * 1024) {  // 超 50MB 拒绝
            inflateEnd(&zs);
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
struct FetchResult {
    int status = 0;
    std::string body;
    std::int64_t ingestion_ns = 0;
};

[[nodiscard]] FetchResult HttpGetGz(const std::string& target_host, std::uint16_t target_port,
                                    const std::string& path, const std::string& proxy_str,
                                    std::uint32_t timeout_ms) noexcept {
    FetchResult result;

    const ProxySpec proxy = ParseProxySpec(proxy_str);

    // 连接目标或代理
    const std::string& connect_host = proxy.valid ? proxy.host : target_host;
    const std::uint16_t connect_port = proxy.valid ? proxy.port : target_port;

    const int fd = TcpConnect(connect_host, connect_port, timeout_ms);
    if (fd < 0) {
        std::fprintf(stderr, "[inplay_feed] connect failed: %s:%u\n", connect_host.c_str(), connect_port);
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
            std::fprintf(stderr, "[inplay_feed] CONNECT failed: %d\n", ParseStatusCode(proxy_resp));
            ::close(fd);
            return result;
        }
    }

    // HTTP/1.1 GET
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
        std::fprintf(stderr, "[inplay_feed] HTTP %d for %s%s\n", result.status, target_host.c_str(),
                     path.c_str());
        ::close(fd);
        return result;
    }

    // 读 body
    const std::int64_t content_length = ParseContentLength(headers);
    bool ok = false;
    if (content_length >= 0) {
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
                if (result.body.size() > 20 * 1024 * 1024)
                    break;
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
    std::fprintf(stderr, "[inplay_feed] Started %zu sport thread(s). proxy=%s\n", cfg_.sports.size(),
                 cfg_.http_proxy.empty() ? "(direct)" : cfg_.http_proxy.c_str());
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

    std::fprintf(stderr, "[inplay_feed] sport=%s thread started, endpoint=http://%s%s\n", sport_slug.c_str(),
                 cfg_.inplay_host.c_str(), path.c_str());

    while (!stop_.load(std::memory_order_acquire)) {
        // 拉取 gz
        std::string gz_body;
        std::int64_t ingestion_ns = 0;
        const bool fetch_ok = FetchGz(sport, gz_body, ingestion_ns);

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

        // 解析 GameScoreRecord
        const auto parse_result = inplay::InplayScoreParser::Parse(json_body, sport, ingestion_ns);

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
            for (const auto& old_key : old_keys) {
                merged_map_.erase(old_key);
            }
            old_keys.clear();
            // 写入新 events
            for (const auto& rec : parse_result.scores) {
                debug_api::EventScore es = ToEventScore(rec, sport);
                const std::string& key = rec.match_id.inplay_match_id;
                if (!key.empty()) {
                    merged_map_[key] = std::move(es);
                    old_keys.insert(key);
                }
            }
            // Publish merged map (拷贝一份 shared_ptr)
            store_.Publish(std::make_shared<ScoreMap>(merged_map_));
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

        SleepMs(cfg_.poll_interval_ms);
    }

    std::fprintf(stderr, "[inplay_feed] sport=%s thread stopped.\n", sport_slug.c_str());
}

// ---- FetchGz: HTTP GET → gzip body ----
bool InplayFeedThread::FetchGz(goalserve::GoalserveSport sport, std::string& gz_body,
                               std::int64_t& ingestion_ns) noexcept {
    const std::string path = "/inplay-" + std::string(goalserve::SportInplaySlug(sport)) + ".gz";

    const auto result =
        HttpGetGz(cfg_.inplay_host, cfg_.inplay_port, path, cfg_.http_proxy, cfg_.http_timeout_ms);

    if (result.status != 200 || result.body.empty()) {
        return false;
    }

    gz_body = result.body;
    ingestion_ns = result.ingestion_ns;
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
    es.source = "goalserve";

    // sport 字符串
    es.sport = std::string(goalserve::SportInplaySlug(sport));

    // period
    es.period = rec.period.value_or("");

    // clock_sec: elapsed_min × 60 + elapsed_sec (soccer/basketball 场内时钟)
    es.clock_sec = 0;
    if (rec.elapsed_min.has_value()) {
        es.clock_sec = static_cast<std::int64_t>(*rec.elapsed_min) * 60;
        if (rec.elapsed_sec.has_value()) {
            es.clock_sec += static_cast<std::int64_t>(*rec.elapsed_sec);
        }
    }

    // status: inplay feed 事件均为 InPlay; MapStatus 处理 Half Time
    es.status = inplay::InplayScoreParser::MapStatus(rec.status, es.period);

    // 4ts 透传 (R-20)
    es.ts.event_ts_ns = rec.ts.event_ts_ns;
    es.ts.data_source_ts_ns = rec.ts.data_source_ts_ns;
    es.ts.ingestion_ts_ns = rec.ts.ingestion_ts_ns;
    es.ts.as_of_ts_ns = rec.ts.as_of_ts_ns;

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

}  // namespace stcpp::data
