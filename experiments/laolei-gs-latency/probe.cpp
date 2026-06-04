// experiments/laolei-gs-latency/probe.cpp — Goalserve inplay 数据新鲜度探针
//
// Owner: 老雷 (GM) — 2026-06-04 老板「goalserve 和 bet365 谁快, 用 c++ 测试程序测」
//
// 背景: 我们的 bet365 inplay 赔率经 Goalserve 中继 (http://inplay.goalserve.com/inplay-<sport>.gz)。
//   bet365 官网 (z1.bet365.com) 本地/服务器均不可直采 (本地中国 geo-block 403; 服务器数据中心 IP
//   被 Cloudflare bot 拦)。但 inplay feed 自带 bet365 自己的赔率变更时间戳:
//     - 顶层 "updated_ts"  = Goalserve 生成本快照的时刻 (ms epoch)
//     - 每事件 core."updated_ts" = bet365 上次改该事件赔率的时刻 (ms epoch)  ← bet365 真实变更刻
//   服务器 NTP 偏移实测 ~3µs (chronyc RMS 0.000003s) → 本机墙钟 vs 它们的 epoch 可直接比, 误差忽略。
//
//   于是端到端延迟链可完整分解 (单位 ms):
//     goalserve_internal = feed.updated_ts − core.updated_ts   (bet365 变更 → Goalserve 进快照)
//     transport          = t_recv          − feed.updated_ts   (Goalserve 快照 → 我们收到)
//     total (我们落后 bet365) = t_recv      − core.updated_ts   (= 上两者之和)
//   total 即「我们比 bet365 慢多少」—— bet365 官网约在 core.updated_ts 时刻显示该变更。
//
// 复用: HTTP GET + gzip 解压逐字摘自生产 src/stcpp/data/inplay_feed_thread.cpp (HttpGetGz/DecompressGzImpl,
//   小白审计加固版), 直连版 (服务器直连, 去代理)。JSON 用轻量 string-scan (与生产 inplay_score_parser 同法,
//   零外部依赖)。
//
// 编译 (服务器 gcc 11.5): g++ -O2 -std=c++20 probe.cpp -lz -lpthread -o probe
// 运行: ./probe [sport=tennis] [duration_sec=90] [poll_ms=300]

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>

namespace {

constexpr std::size_t kMaxGzipOutputBytes = 8UL * 1024 * 1024;
constexpr std::size_t kMaxBodyBytes = 16UL * 1024 * 1024;

[[nodiscard]] std::int64_t NowMs() noexcept {
    struct timespec ts{};
    ::clock_gettime(CLOCK_REALTIME, &ts);  // 与 Goalserve updated_ts (Unix epoch ms) 同域
    return static_cast<std::int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1'000'000;
}

// ---- TCP connect (直连版) ----
[[nodiscard]] int TcpConnect(const std::string& host, std::uint16_t port, std::uint32_t timeout_ms) noexcept {
    struct addrinfo hints{};
    struct addrinfo* res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    const std::string port_str = std::to_string(port);
    if (::getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0 || res == nullptr) return -1;
    int fd = -1;
    for (struct addrinfo* rp = res; rp != nullptr; rp = rp->ai_next) {
        fd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        struct timeval tv{};
        tv.tv_sec = static_cast<long>(timeout_ms / 1000);
        tv.tv_usec = static_cast<suseconds_t>((timeout_ms % 1000) * 1000);
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        if (::connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(res);
    return fd;
}

[[nodiscard]] bool SendAll(int fd, const char* buf, std::size_t len) noexcept {
    std::size_t sent = 0;
    while (sent < len) {
        const ssize_t n = ::send(fd, buf + sent, len - sent, 0);
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

[[nodiscard]] bool ReadHttpHeaders(int fd, std::string& headers) noexcept {
    headers.clear();
    headers.reserve(2048);
    char buf[1];
    while (true) {
        const ssize_t n = ::recv(fd, buf, 1, 0);
        if (n <= 0) return false;
        headers.push_back(buf[0]);
        const std::size_t s = headers.size();
        if (s >= 4 && headers[s - 4] == '\r' && headers[s - 3] == '\n' && headers[s - 2] == '\r' &&
            headers[s - 1] == '\n')
            return true;
        if (s > 65536) return false;
    }
}

[[nodiscard]] std::int64_t ParseContentLength(const std::string& h) noexcept {
    const auto pos = h.find("Content-Length:");
    if (pos == std::string::npos) return -1;
    auto i = pos + 15;
    std::int64_t val = 0;
    bool found = false;
    for (; i < h.size() && i < pos + 35; ++i) {
        if (h[i] >= '0' && h[i] <= '9') {
            val = val * 10 + (h[i] - '0');
            found = true;
        } else if (found)
            break;
    }
    return found ? val : -1;
}

[[nodiscard]] int ParseStatusCode(const std::string& h) noexcept {
    if (h.size() < 12) return 0;
    int code = 0;
    bool in = false;
    for (std::size_t i = 9; i < std::min(h.size(), std::size_t{13}); ++i) {
        if (h[i] >= '0' && h[i] <= '9') {
            code = code * 10 + (h[i] - '0');
            in = true;
        } else if (in)
            break;
    }
    return code;
}

[[nodiscard]] bool RecvExact(int fd, char* buf, std::size_t len) noexcept {
    std::size_t got = 0;
    while (got < len) {
        const ssize_t n = ::recv(fd, buf + got, len - got, 0);
        if (n <= 0) return false;
        got += static_cast<std::size_t>(n);
    }
    return true;
}

[[nodiscard]] bool ReadChunkedBody(int fd, std::string& body) noexcept {
    body.clear();
    while (true) {
        std::string size_line;
        char c;
        while (true) {
            if (::recv(fd, &c, 1, 0) != 1) return false;
            if (c == '\n') break;
            if (c != '\r') size_line.push_back(c);
        }
        const auto semi = size_line.find(';');
        const std::string hex = (semi != std::string::npos) ? size_line.substr(0, semi) : size_line;
        long sz = 0;
        try {
            sz = std::stol(hex, nullptr, 16);
        } catch (...) {
            return false;
        }
        if (sz == 0) {
            char tail[2];
            (void)::recv(fd, tail, 2, 0);
            return true;
        }
        const std::size_t old = body.size();
        body.resize(old + static_cast<std::size_t>(sz));
        if (!RecvExact(fd, body.data() + old, static_cast<std::size_t>(sz))) return false;
        char crlf[2];
        if (!RecvExact(fd, crlf, 2)) return false;
        if (body.size() > kMaxBodyBytes) return false;
    }
}

[[nodiscard]] bool DecompressGz(const std::string& gz, std::string& out) noexcept {
    out.clear();
    if (gz.empty()) return false;
    z_stream zs{};
    if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK) return false;
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(gz.data()));
    zs.avail_in = static_cast<uInt>(gz.size());
    constexpr std::size_t kChunk = 65536;
    char buf[kChunk];
    int ret = Z_OK;
    do {
        zs.next_out = reinterpret_cast<Bytef*>(buf);
        zs.avail_out = kChunk;
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
            inflateEnd(&zs);
            return false;
        }
        out.append(buf, kChunk - zs.avail_out);
        if (out.size() > kMaxGzipOutputBytes) {
            inflateEnd(&zs);
            return false;
        }
    } while (ret != Z_STREAM_END);
    inflateEnd(&zs);
    return ret == Z_STREAM_END;
}

struct FetchResult {
    int status = 0;
    std::string json;  // 解压后
    std::int64_t t_recv_ms = 0;
};

// HTTP/1.1 GET + gzip (直连, Connection: close — 与生产 inplay 同)
[[nodiscard]] FetchResult Fetch(const std::string& host, std::uint16_t port, const std::string& path,
                                std::uint32_t timeout_ms) noexcept {
    FetchResult r;
    const int fd = TcpConnect(host, port, timeout_ms);
    if (fd < 0) return r;
    const std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + host +
                            "\r\nAccept-Encoding: gzip\r\nConnection: close\r\n\r\n";
    if (!SendAll(fd, req.data(), req.size())) {
        ::close(fd);
        return r;
    }
    std::string headers;
    if (!ReadHttpHeaders(fd, headers)) {
        ::close(fd);
        return r;
    }
    r.status = ParseStatusCode(headers);
    if (r.status != 200) {
        ::close(fd);
        return r;
    }
    std::string gz;
    bool ok = false;
    const std::int64_t cl = ParseContentLength(headers);
    if (cl >= 0) {
        if (static_cast<std::size_t>(cl) > kMaxBodyBytes) {
            ::close(fd);
            return r;
        }
        gz.resize(static_cast<std::size_t>(cl));
        ok = RecvExact(fd, gz.data(), static_cast<std::size_t>(cl));
    } else if (headers.find("Transfer-Encoding: chunked") != std::string::npos) {
        ok = ReadChunkedBody(fd, gz);
    } else {
        char buf[8192];
        ssize_t n;
        while ((n = ::recv(fd, buf, sizeof(buf), 0)) > 0) {
            gz.append(buf, static_cast<std::size_t>(n));
            if (gz.size() > kMaxBodyBytes) break;
        }
        ok = !gz.empty();
    }
    r.t_recv_ms = NowMs();  // body 收完 = 我们「能用上」的时刻
    ::close(fd);
    if (!ok || gz.empty()) {
        r.status = 0;
        return r;
    }
    if (!DecompressGz(gz, r.json)) r.status = 0;
    return r;
}

// ---- 轻量 JSON string-scan (零依赖, 与生产 inplay_score_parser 同法) ----
constexpr std::size_t npos = std::string::npos;

[[nodiscard]] std::size_t MatchBrace(std::string_view s, std::size_t open) noexcept {
    int depth = 0;
    bool instr = false;
    for (std::size_t i = open; i < s.size(); ++i) {
        const char c = s[i];
        if (instr) {
            if (c == '"' && (i == 0 || s[i - 1] != '\\')) instr = false;
            continue;
        }
        if (c == '"')
            instr = true;
        else if (c == '{')
            ++depth;
        else if (c == '}') {
            if (--depth == 0) return i;
        }
    }
    return npos;
}

// 取 "key" 标量值 (字符串去引号 / 数字原样); 在 block 内首个 key
[[nodiscard]] std::string FieldRaw(std::string_view s, std::string_view key) noexcept {
    const std::string pat = "\"" + std::string(key) + "\"";
    std::size_t p = s.find(pat);
    if (p == npos) return "";
    p += pat.size();
    while (p < s.size() && (s[p] == ' ' || s[p] == ':' || s[p] == '\t')) ++p;
    if (p >= s.size()) return "";
    if (s[p] == '"') {
        const std::size_t e = s.find('"', p + 1);
        return (e == npos) ? "" : std::string(s.substr(p + 1, e - p - 1));
    }
    std::size_t e = p;
    while (e < s.size() && s[e] != ',' && s[e] != '}' && s[e] != '\n' && s[e] != '\r' && s[e] != ' ') ++e;
    return std::string(s.substr(p, e - p));
}

// 取 "key": { ... } 对象子串 (含外括号)
[[nodiscard]] std::string FieldObject(std::string_view s, std::string_view key) noexcept {
    const std::string pat = "\"" + std::string(key) + "\"";
    std::size_t p = s.find(pat);
    if (p == npos) return "";
    p = s.find('{', p);
    if (p == npos) return "";
    const std::size_t e = MatchBrace(s, p);
    return (e == npos) ? "" : std::string(s.substr(p, e - p + 1));
}

[[nodiscard]] std::int64_t ToI64(const std::string& v) noexcept {
    if (v.empty()) return 0;
    try {
        return std::stoll(v);
    } catch (...) {
        return 0;
    }
}

// ---- 百分位 ----
struct Stats {
    std::vector<double> v;
    void add(double x) { v.push_back(x); }
    double pct(double p) {
        if (v.empty()) return 0;
        std::vector<double> t = v;
        std::sort(t.begin(), t.end());
        const std::size_t i = std::min(t.size() - 1, static_cast<std::size_t>(p / 100.0 * t.size()));
        return t[i];
    }
    double min() {
        if (v.empty()) return 0;
        return *std::min_element(v.begin(), v.end());
    }
    double max() {
        if (v.empty()) return 0;
        return *std::max_element(v.begin(), v.end());
    }
    std::size_t n() const { return v.size(); }
};

}  // namespace

int main(int argc, char** argv) {
    const std::string sport = (argc > 1) ? argv[1] : "tennis";
    const int duration_sec = (argc > 2) ? std::atoi(argv[2]) : 90;
    const int poll_ms = (argc > 3) ? std::atoi(argv[3]) : 300;

    const std::string host = "inplay.goalserve.com";
    const std::uint16_t port = 80;
    const std::string path = "/inplay-" + sport + ".gz";

    std::printf("=== Goalserve inplay 新鲜度探针 ===\n");
    std::printf("sport=%s  endpoint=http://%s%s  duration=%ds  poll=%dms\n", sport.c_str(), host.c_str(),
                path.c_str(), duration_sec, poll_ms);
    std::printf("分解: total(我们落后bet365) = goalserve_internal(变更→快照) + transport(快照→我们)\n\n");

    Stats http_rtt, feed_age, total_lag, gs_internal, transport;
    std::unordered_map<std::string, std::int64_t> last_core_ts;  // event_id → 上次见 core.updated_ts
    std::int64_t last_feed_ts = 0;
    Stats feed_cadence;
    int polls = 0, errors = 0, fresh_updates = 0, active_events_last = 0;

    const std::int64_t t_end = NowMs() + static_cast<std::int64_t>(duration_sec) * 1000;
    while (NowMs() < t_end) {
        const std::int64_t t_send = NowMs();
        FetchResult fr = Fetch(host, port, path, 8000);
        if (fr.status != 200 || fr.json.empty()) {
            ++errors;
            std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
            continue;
        }
        ++polls;
        http_rtt.add(static_cast<double>(fr.t_recv_ms - t_send));

        // 顶层 feed updated_ts (在 events 之前; FieldRaw 取首个即对)
        const std::int64_t feed_ts = ToI64(FieldRaw(fr.json, "updated_ts"));
        if (feed_ts > 0) {
            feed_age.add(static_cast<double>(fr.t_recv_ms - feed_ts));
            if (last_feed_ts > 0 && feed_ts > last_feed_ts)
                feed_cadence.add(static_cast<double>(feed_ts - last_feed_ts));
            if (feed_ts != last_feed_ts) last_feed_ts = feed_ts;
        }

        // 遍历 events
        const std::string events = FieldObject(fr.json, "events");
        int active = 0;
        std::size_t i = 1;
        while (i < events.size()) {
            const std::size_t q = events.find('"', i);
            if (q == npos) break;
            const std::size_t q2 = events.find('"', q + 1);
            if (q2 == npos) break;
            const std::string id = events.substr(q + 1, q2 - q - 1);
            const std::size_t br = events.find('{', q2);
            if (br == npos) break;
            const std::size_t bre = MatchBrace(events, br);
            if (bre == npos) break;
            const std::string ev = events.substr(br, bre - br + 1);
            i = bre + 1;

            const std::string core = FieldObject(ev, "core");
            if (core.empty()) continue;
            const std::string stopped = FieldRaw(core, "stopped");
            const std::string finished = FieldRaw(core, "finished");
            const std::int64_t core_ts = ToI64(FieldRaw(core, "updated_ts"));
            const bool is_active = (stopped != "1" && finished != "1");
            if (is_active) ++active;
            if (core_ts <= 0) continue;

            // 检测「bet365 该事件赔率发生新变更」: core.updated_ts 较上次推进
            const auto it = last_core_ts.find(id);
            if (it != last_core_ts.end() && core_ts > it->second) {
                const double total = static_cast<double>(fr.t_recv_ms - core_ts);
                if (total >= 0 && total < 120000) {  // 过滤异常 (时钟/解析)
                    ++fresh_updates;
                    total_lag.add(total);
                    if (feed_ts > 0) {
                        gs_internal.add(static_cast<double>(feed_ts - core_ts));
                        transport.add(static_cast<double>(fr.t_recv_ms - feed_ts));
                    }
                    if (fresh_updates <= 12) {
                        const std::string info = FieldObject(ev, "info");
                        std::printf("  [fresh] %-34s set=%-6s score=%-5s  total=%5.0fms (gs_int=%4.0f + transport=%4.0f)\n",
                                    FieldRaw(info, "name").c_str(), FieldRaw(info, "period").c_str(),
                                    FieldRaw(info, "score").c_str(), total,
                                    feed_ts > 0 ? static_cast<double>(feed_ts - core_ts) : 0.0,
                                    feed_ts > 0 ? static_cast<double>(fr.t_recv_ms - feed_ts) : 0.0);
                    }
                }
            }
            last_core_ts[id] = core_ts;
        }
        active_events_last = active;

        const std::int64_t spent = NowMs() - t_send;
        const std::int64_t sleep = poll_ms - spent;
        if (sleep > 0) std::this_thread::sleep_for(std::chrono::milliseconds(sleep));
    }

    std::printf("\n=== 汇总 (sport=%s) ===\n", sport.c_str());
    std::printf("polls=%d  errors=%d  当前活跃事件=%d  检测到 bet365 新变更=%d 次\n", polls, errors,
                active_events_last, fresh_updates);
    std::printf("\nHTTP 往返 (connect+GET+body, Connection:close):  P50=%.0f  P95=%.0f  max=%.0f ms\n",
                http_rtt.pct(50), http_rtt.pct(95), http_rtt.max());
    std::printf("feed 快照新鲜度 (t_recv − feed.updated_ts):       P50=%.0f  P95=%.0f  max=%.0f ms (n=%zu)\n",
                feed_age.pct(50), feed_age.pct(95), feed_age.max(), feed_age.n());
    std::printf("feed 刷新间隔 (Goalserve 多久出一版):             P50=%.0f  P95=%.0f ms (n=%zu)\n",
                feed_cadence.pct(50), feed_cadence.pct(95), feed_cadence.n());
    if (fresh_updates > 0) {
        std::printf("\n--- 我们落后 bet365 多少 (新变更 n=%zu) ---\n", total_lag.n());
        std::printf("total   (t_recv − bet365变更刻):   min=%.0f  P50=%.0f  P95=%.0f  P99=%.0f  max=%.0f ms\n",
                    total_lag.min(), total_lag.pct(50), total_lag.pct(95), total_lag.pct(99), total_lag.max());
        std::printf("  ├ goalserve_internal (变更→快照): P50=%.0f  P95=%.0f ms\n", gs_internal.pct(50),
                    gs_internal.pct(95));
        std::printf("  └ transport          (快照→我们): P50=%.0f  P95=%.0f ms\n", transport.pct(50),
                    transport.pct(95));
    } else {
        std::printf("\n(未检测到活跃赔率变更 — 可能当前无 in-play 进行中赛事; 换 sport 或延长 duration)\n");
    }
    return 0;
}
