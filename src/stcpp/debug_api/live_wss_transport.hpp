// src/stcpp/debug_api/live_wss_transport.hpp — 真实 WSS transport (POSIX + OpenSSL)
//
// Owner: 小冯 (#34)  -- live WSS 接入 debug_server --live 模式
// last_review: 2026-05-29
//
// 用途:
//   --live 模式专用 IWssTransport 实现.
//   通过 POSIX socket + OpenSSL TLS 1.3 + HTTP CONNECT proxy 连接
//   wss://ws-subscriptions-clob.polymarket.com/ws/market.
//   代理自动读取环境变量 HTTPS_PROXY / HTTP_PROXY / ALL_PROXY.
//
// 线程模型:
//   AsyncConnect() 启动后台 io_thread_ 持续 recv → 回调 on_text_frame_.
//   AsyncSendText() 将帧放入 send_queue_ (mutex + cond_var) 由 send_thread_ 发送.
//   Close() 通知 stop_ flag → 两线程退出 → join.
//   R-12: callback 路径不阻塞 (on_text_frame_ 由 io_thread_ 调用, 调用方严禁 block).
//
// WebSocket framing:
//   RFC 6455. 客户端必须 masking (mask bit = 1, 4-byte random mask key).
//   只实现 text frame (opcode=0x1) 收发.
//   收: 自动处理分片 (continuation frame), ping/pong.
//
// 代理支持:
//   HTTP CONNECT tunnel → TLS wrap → WS upgrade.
//   环境变量: HTTPS_PROXY / HTTP_PROXY / ALL_PROXY (优先级按此顺序).
//   格式: http://host:port 或 host:port.
//
// 红线:
//   R-12: on_text_frame_ callback 在 io_thread_ 中同步调用; 调用方不得 block > 100us.
//   P-09: user channel auth 严禁落日志 (本类不处理 user channel).

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// POSIX
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

// OpenSSL
#include <openssl/err.h>
#include <openssl/ssl.h>

#include "stcpp/polymarket/wss/pm_wss_subscriber.hpp"  // IWssTransport

namespace stcpp::debug_api {

// ---------------------------------------------------------------------------
// ProxySpec — HTTP CONNECT 代理配置
// ---------------------------------------------------------------------------
struct ProxySpec {
    std::string host;
    std::uint16_t port{0};
    bool enabled{false};
};

// 从环境变量解析代理 (HTTPS_PROXY > HTTP_PROXY > ALL_PROXY)
inline ProxySpec ProxyFromEnv() {
    ProxySpec ps;
    const char* envs[] = {"HTTPS_PROXY", "HTTP_PROXY", "ALL_PROXY", nullptr};
    for (int i = 0; envs[i] != nullptr; ++i) {
        const char* v = ::getenv(envs[i]);
        if (!v || v[0] == '\0') continue;
        // strip http:// or https://
        std::string s = v;
        for (const auto& prefix : {"https://", "http://"}) {
            if (s.substr(0, std::strlen(prefix)) == prefix) {
                s = s.substr(std::strlen(prefix));
                break;
            }
        }
        // strip trailing /
        while (!s.empty() && s.back() == '/') s.pop_back();
        // split host:port
        auto colon = s.rfind(':');
        if (colon == std::string::npos) {
            ps.host = s;
            ps.port = 8080;
        } else {
            ps.host = s.substr(0, colon);
            ps.port = static_cast<std::uint16_t>(std::stoi(s.substr(colon + 1)));
        }
        if (!ps.host.empty() && ps.port > 0) {
            ps.enabled = true;
            break;
        }
    }
    return ps;
}

// ---------------------------------------------------------------------------
// LiveWssTransport — 真实 WSS (market channel only, 无 user auth)
//
// 实现 IWssTransport (pm_wss_subscriber.hpp):
//   AsyncConnect(url) → 后台线程建立 TCP+TLS+WS
//   AsyncSendText(payload) → send queue → send thread
//   Close() → stop flag → join threads
//   SetOn* → 回调注册
// ---------------------------------------------------------------------------
class LiveWssTransport final : public polymarket::wss::IWssTransport {
public:
    explicit LiveWssTransport(bool verbose = false) : verbose_(verbose) {}

    ~LiveWssTransport() override { Close(); }

    // 禁止拷贝/移动
    LiveWssTransport(const LiveWssTransport&) = delete;
    LiveWssTransport& operator=(const LiveWssTransport&) = delete;
    LiveWssTransport(LiveWssTransport&&) = delete;
    LiveWssTransport& operator=(LiveWssTransport&&) = delete;

    // -----------------------------------------------------------------------
    // AsyncConnect — 解析 URL, 启动 io_thread_
    // -----------------------------------------------------------------------
    bool AsyncConnect(std::string_view url) override {
        if (stop_.load(std::memory_order_acquire)) {
            return false;
        }
        // Join any previous thread
        if (io_thread_.joinable()) {
            stop_.store(true, std::memory_order_release);
            io_thread_.join();
            stop_.store(false, std::memory_order_release);
        }

        // Parse wss://host/path
        url_ = std::string(url);
        if (!ParseUrl(url_, wss_host_, wss_path_)) {
            std::fprintf(stderr, "[live_wss] ERROR: cannot parse URL: %s\n", url_.c_str());
            return false;
        }

        proxy_ = ProxyFromEnv();
        if (verbose_) {
            if (proxy_.enabled) {
                std::fprintf(stderr, "[live_wss] proxy: %s:%u\n", proxy_.host.c_str(), proxy_.port);
            } else {
                std::fprintf(stderr, "[live_wss] direct connect (no proxy)\n");
            }
        }

        stop_.store(false, std::memory_order_release);
        io_thread_ = std::thread([this] { IoLoop(); });
        return true;
    }

    // -----------------------------------------------------------------------
    // AsyncSendText — enqueue WS text frame for send_thread
    // -----------------------------------------------------------------------
    bool AsyncSendText(std::string_view payload) override {
        {
            std::lock_guard<std::mutex> lk(send_mu_);
            send_queue_.push(std::string(payload));
        }
        send_cv_.notify_one();
        return true;
    }

    // -----------------------------------------------------------------------
    // Close — signal stop, join threads, cleanup SSL
    // -----------------------------------------------------------------------
    void Close() override {
        stop_.store(true, std::memory_order_release);
        send_cv_.notify_all();
        if (io_thread_.joinable()) {
            io_thread_.join();
        }
        CleanupSsl();
    }

    void SetOnTextFrame(OnTextFrame cb) override { on_text_frame_ = std::move(cb); }
    void SetOnConnected(OnConnected cb) override { on_connected_ = std::move(cb); }
    void SetOnDisconnected(OnDisconnected cb) override { on_disconnected_ = std::move(cb); }

    [[nodiscard]] bool IsConnected() const noexcept override {
        return connected_.load(std::memory_order_acquire);
    }

private:
    // -----------------------------------------------------------------------
    // ParseUrl — wss://host/path → host, path
    // -----------------------------------------------------------------------
    static bool ParseUrl(const std::string& url, std::string& host, std::string& path) {
        // expects "wss://host/path"
        const std::string prefix = "wss://";
        if (url.substr(0, prefix.size()) != prefix) {
            return false;
        }
        std::string rest = url.substr(prefix.size());
        auto slash = rest.find('/');
        if (slash == std::string::npos) {
            host = rest;
            path = "/";
        } else {
            host = rest.substr(0, slash);
            path = rest.substr(slash);
        }
        return !host.empty();
    }

    // -----------------------------------------------------------------------
    // IoLoop — runs in io_thread_
    //   1. TCP connect (to proxy or direct to host)
    //   2. HTTP CONNECT (if proxy)
    //   3. TLS wrap (OpenSSL)
    //   4. WebSocket upgrade
    //   5. Start send_thread_
    //   6. Recv loop: parse WS frames → on_text_frame_
    // -----------------------------------------------------------------------
    void IoLoop() {
        if (verbose_) {
            std::fprintf(stderr, "[live_wss] IoLoop: connecting to %s%s\n",
                         wss_host_.c_str(), wss_path_.c_str());
        }

        // 1. TCP connect
        const std::string tcp_host = proxy_.enabled ? proxy_.host : wss_host_;
        const std::uint16_t tcp_port = proxy_.enabled ? proxy_.port : 443;

        int sockfd = TcpConnect(tcp_host, tcp_port);
        if (sockfd < 0) {
            std::fprintf(stderr, "[live_wss] TCP connect failed: %s:%u\n", tcp_host.c_str(), tcp_port);
            if (on_disconnected_) on_disconnected_("tcp_connect_failed");
            return;
        }
        if (verbose_) {
            std::fprintf(stderr, "[live_wss] TCP connected to %s:%u\n", tcp_host.c_str(), tcp_port);
        }

        // 2. HTTP CONNECT (proxy)
        if (proxy_.enabled) {
            if (!HttpConnectTunnel(sockfd, wss_host_, 443)) {
                std::fprintf(stderr, "[live_wss] HTTP CONNECT tunnel failed\n");
                ::close(sockfd);
                if (on_disconnected_) on_disconnected_("proxy_connect_failed");
                return;
            }
            if (verbose_) {
                std::fprintf(stderr, "[live_wss] HTTP CONNECT tunnel established\n");
            }
        }

        // 3. TLS wrap
        SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx) {
            std::fprintf(stderr, "[live_wss] SSL_CTX_new failed\n");
            ::close(sockfd);
            if (on_disconnected_) on_disconnected_("ssl_ctx_failed");
            return;
        }
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
        SSL_CTX_set_default_verify_paths(ctx);

        SSL* ssl = SSL_new(ctx);
        if (!ssl) {
            SSL_CTX_free(ctx);
            ::close(sockfd);
            if (on_disconnected_) on_disconnected_("ssl_new_failed");
            return;
        }
        SSL_set_fd(ssl, sockfd);
        SSL_set_tlsext_host_name(ssl, wss_host_.c_str());

        if (SSL_connect(ssl) != 1) {
            char errbuf[256];
            ERR_error_string_n(ERR_get_error(), errbuf, sizeof(errbuf));
            std::fprintf(stderr, "[live_wss] SSL_connect failed: %s\n", errbuf);
            SSL_free(ssl);
            SSL_CTX_free(ctx);
            ::close(sockfd);
            if (on_disconnected_) on_disconnected_("ssl_connect_failed");
            return;
        }
        if (verbose_) {
            std::fprintf(stderr, "[live_wss] TLS established: %s\n", SSL_get_version(ssl));
        }

        ssl_ = ssl;
        ctx_ = ctx;
        sockfd_ = sockfd;

        // 4. WebSocket upgrade
        if (!WsHandshake(ssl, wss_host_, wss_path_)) {
            std::fprintf(stderr, "[live_wss] WebSocket handshake failed\n");
            CleanupSsl();
            if (on_disconnected_) on_disconnected_("ws_handshake_failed");
            return;
        }
        if (verbose_) {
            std::fprintf(stderr, "[live_wss] WebSocket connected\n");
        }

        connected_.store(true, std::memory_order_release);
        if (on_connected_) on_connected_();

        // 5. Start send thread
        if (send_thread_.joinable()) {
            send_thread_.join();
        }
        send_thread_ = std::thread([this] { SendLoop(); });

        // 6. Recv loop
        RecvLoop(ssl);

        connected_.store(false, std::memory_order_release);
        stop_.store(true, std::memory_order_release);
        send_cv_.notify_all();
        if (send_thread_.joinable()) {
            send_thread_.join();
        }
        CleanupSsl();
        if (on_disconnected_) on_disconnected_("recv_loop_ended");
    }

    // -----------------------------------------------------------------------
    // TcpConnect — blocking TCP connect with getaddrinfo
    // -----------------------------------------------------------------------
    static int TcpConnect(const std::string& host, std::uint16_t port) {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        char port_str[8];
        std::snprintf(port_str, sizeof(port_str), "%u", port);

        addrinfo* res = nullptr;
        int rc = ::getaddrinfo(host.c_str(), port_str, &hints, &res);
        if (rc != 0 || !res) {
            std::fprintf(stderr, "[live_wss] getaddrinfo(%s): %s\n", host.c_str(), ::gai_strerror(rc));
            return -1;
        }

        int fd = -1;
        for (addrinfo* p = res; p; p = p->ai_next) {
            fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (fd < 0) continue;
            if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
            ::close(fd);
            fd = -1;
        }
        ::freeaddrinfo(res);
        return fd;
    }

    // -----------------------------------------------------------------------
    // HttpConnectTunnel — send HTTP CONNECT, read 200
    // -----------------------------------------------------------------------
    static bool HttpConnectTunnel(int sockfd, const std::string& target_host, std::uint16_t target_port) {
        char req[512];
        int n = std::snprintf(req, sizeof(req),
                              "CONNECT %s:%u HTTP/1.1\r\nHost: %s:%u\r\n\r\n",
                              target_host.c_str(), target_port,
                              target_host.c_str(), target_port);
        if (::send(sockfd, req, static_cast<std::size_t>(n), 0) < 0) return false;

        // Read until "\r\n\r\n"
        char resp[1024];
        int total = 0;
        while (total < static_cast<int>(sizeof(resp)) - 1) {
            int r = static_cast<int>(::recv(sockfd, resp + total, 1, 0));
            if (r <= 0) return false;
            total += r;
            resp[total] = '\0';
            if (total >= 4 && std::strstr(resp, "\r\n\r\n")) break;
        }
        return std::strstr(resp, "200") != nullptr;
    }

    // -----------------------------------------------------------------------
    // WsHandshake — RFC 6455 upgrade
    // -----------------------------------------------------------------------
    static bool WsHandshake(SSL* ssl, const std::string& host, const std::string& path) {
        // Random 16-byte key, base64-encoded
        unsigned char raw_key[16];
        for (std::size_t i = 0; i < sizeof(raw_key); ++i) {
            raw_key[i] = static_cast<unsigned char>(::rand() % 256);  // NOLINT
        }
        // Base64 encode
        char b64[32];
        static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        int b64len = 0;
        for (int i = 0; i < 16; i += 3) {
            std::uint32_t v = (static_cast<std::uint32_t>(raw_key[i]) << 16) |
                              (i + 1 < 16 ? static_cast<std::uint32_t>(raw_key[i + 1]) << 8 : 0u) |
                              (i + 2 < 16 ? static_cast<std::uint32_t>(raw_key[i + 2]) : 0u);
            b64[b64len++] = kB64[(v >> 18) & 0x3F];
            b64[b64len++] = kB64[(v >> 12) & 0x3F];
            b64[b64len++] = (i + 1 < 16) ? kB64[(v >> 6) & 0x3F] : '=';
            b64[b64len++] = (i + 2 < 16) ? kB64[v & 0x3F] : '=';
        }
        b64[b64len] = '\0';

        char req[1024];
        int n = std::snprintf(req, sizeof(req),
                              "GET %s HTTP/1.1\r\n"
                              "Host: %s\r\n"
                              "Upgrade: websocket\r\n"
                              "Connection: Upgrade\r\n"
                              "Sec-WebSocket-Key: %s\r\n"
                              "Sec-WebSocket-Version: 13\r\n"
                              "\r\n",
                              path.c_str(), host.c_str(), b64);
        if (SslWriteAll(ssl, req, static_cast<std::size_t>(n)) < 0) return false;

        // Read until "\r\n\r\n"
        char resp[2048];
        int total = 0;
        while (total < static_cast<int>(sizeof(resp)) - 1) {
            int r = SSL_read(ssl, resp + total, 1);
            if (r <= 0) return false;
            total += r;
            resp[total] = '\0';
            if (total >= 4 && std::strstr(resp, "\r\n\r\n")) break;
        }
        return std::strstr(resp, "101") != nullptr;
    }

    // -----------------------------------------------------------------------
    // RecvLoop — parse WebSocket frames, dispatch on_text_frame_
    // -----------------------------------------------------------------------
    void RecvLoop(SSL* ssl) {
        std::vector<std::uint8_t> fragment_buf;
        bool in_fragment = false;

        while (!stop_.load(std::memory_order_acquire)) {
            // Read 2-byte header
            std::uint8_t hdr[2];
            if (!SslReadExact(ssl, hdr, 2)) break;

            bool fin = (hdr[0] & 0x80) != 0;
            std::uint8_t opcode = hdr[0] & 0x0F;
            bool masked = (hdr[1] & 0x80) != 0;
            std::uint64_t payload_len = hdr[1] & 0x7F;

            if (payload_len == 126) {
                std::uint8_t ext[2];
                if (!SslReadExact(ssl, ext, 2)) break;
                payload_len = (static_cast<std::uint64_t>(ext[0]) << 8) | ext[1];
            } else if (payload_len == 127) {
                std::uint8_t ext[8];
                if (!SslReadExact(ssl, ext, 8)) break;
                payload_len = 0;
                for (int i = 0; i < 8; ++i) {
                    payload_len = (payload_len << 8) | ext[static_cast<std::size_t>(i)];
                }
            }

            // Masking (server → client: usually not masked, but handle anyway)
            std::uint8_t mask_key[4] = {0, 0, 0, 0};
            if (masked) {
                if (!SslReadExact(ssl, mask_key, 4)) break;
            }

            // Payload
            static constexpr std::size_t kMaxPayload = 4 * 1024 * 1024;  // 4 MB guard
            if (payload_len > kMaxPayload) {
                std::fprintf(stderr, "[live_wss] oversized frame: %llu bytes\n",
                             static_cast<unsigned long long>(payload_len));
                break;
            }

            std::vector<std::uint8_t> payload(payload_len);
            if (payload_len > 0 && !SslReadExact(ssl, payload.data(), payload_len)) break;

            if (masked) {
                for (std::size_t i = 0; i < payload_len; ++i) {
                    payload[i] ^= mask_key[i % 4];
                }
            }

            // Dispatch
            if (opcode == 0x8) {
                // Close frame
                if (verbose_) std::fprintf(stderr, "[live_wss] CLOSE frame received\n");
                break;
            } else if (opcode == 0x9) {
                // Ping → send Pong
                SendPong(ssl, payload);
            } else if (opcode == 0xA) {
                // Pong — ignore
            } else if (opcode == 0x1 || opcode == 0x2) {
                // Text or binary
                if (fin && !in_fragment) {
                    // Complete single frame
                    const auto recv_ts = RecvNowNs();
                    std::string text(reinterpret_cast<const char*>(payload.data()), payload.size());
                    if (on_text_frame_) on_text_frame_(text, recv_ts);
                } else {
                    // Start of fragmented message
                    fragment_buf.insert(fragment_buf.end(), payload.begin(), payload.end());
                    in_fragment = !fin;
                    if (fin) {
                        const auto recv_ts = RecvNowNs();
                        std::string text(reinterpret_cast<const char*>(fragment_buf.data()),
                                         fragment_buf.size());
                        fragment_buf.clear();
                        in_fragment = false;
                        if (on_text_frame_) on_text_frame_(text, recv_ts);
                    }
                }
            } else if (opcode == 0x0) {
                // Continuation
                fragment_buf.insert(fragment_buf.end(), payload.begin(), payload.end());
                if (fin) {
                    in_fragment = false;
                    const auto recv_ts = RecvNowNs();
                    std::string text(reinterpret_cast<const char*>(fragment_buf.data()),
                                     fragment_buf.size());
                    fragment_buf.clear();
                    if (on_text_frame_) on_text_frame_(text, recv_ts);
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // SendLoop — runs in send_thread_, dequeues and sends WS text frames
    // -----------------------------------------------------------------------
    void SendLoop() {
        while (!stop_.load(std::memory_order_acquire)) {
            std::string msg;
            {
                std::unique_lock<std::mutex> lk(send_mu_);
                send_cv_.wait(lk, [this] {
                    return stop_.load(std::memory_order_acquire) || !send_queue_.empty();
                });
                if (send_queue_.empty()) continue;
                msg = std::move(send_queue_.front());
                send_queue_.pop();
            }
            if (ssl_ && IsConnected()) {
                SendWsText(ssl_, msg);
            }
        }
    }

    // -----------------------------------------------------------------------
    // SendWsText — build masked WS text frame, SSL_write
    // -----------------------------------------------------------------------
    static bool SendWsText(SSL* ssl, const std::string& payload) {
        const std::size_t plen = payload.size();
        // Header: FIN=1, opcode=text(0x1), MASK=1
        std::vector<std::uint8_t> frame;
        frame.reserve(plen + 14);
        frame.push_back(0x81);  // FIN + text opcode
        if (plen <= 125) {
            frame.push_back(static_cast<std::uint8_t>(0x80 | plen));
        } else if (plen <= 65535) {
            frame.push_back(0xFE);  // 0x80 | 126
            frame.push_back(static_cast<std::uint8_t>((plen >> 8) & 0xFF));
            frame.push_back(static_cast<std::uint8_t>(plen & 0xFF));
        } else {
            frame.push_back(0xFF);  // 0x80 | 127
            for (int i = 7; i >= 0; --i) {
                frame.push_back(static_cast<std::uint8_t>((plen >> (8 * i)) & 0xFF));
            }
        }
        // 4-byte random mask key
        std::uint8_t mask[4];
        for (std::size_t i = 0; i < 4; ++i) {
            mask[i] = static_cast<std::uint8_t>(::rand() % 256);  // NOLINT
        }
        frame.insert(frame.end(), mask, mask + 4);
        // Masked payload
        for (std::size_t i = 0; i < plen; ++i) {
            frame.push_back(static_cast<std::uint8_t>(
                static_cast<std::uint8_t>(payload[i]) ^ mask[i % 4]));
        }
        return SslWriteAll(ssl, frame.data(), frame.size()) >= 0;
    }

    // -----------------------------------------------------------------------
    // SendPong — send WS pong (unmasked, server doesn't require mask on server-originated pings)
    // -----------------------------------------------------------------------
    static void SendPong(SSL* ssl, const std::vector<std::uint8_t>& ping_payload) {
        std::vector<std::uint8_t> pong;
        pong.push_back(0x8A);  // FIN + pong opcode
        pong.push_back(static_cast<std::uint8_t>(ping_payload.size() & 0x7F));
        pong.insert(pong.end(), ping_payload.begin(), ping_payload.end());
        SslWriteAll(ssl, pong.data(), pong.size());
    }

    // -----------------------------------------------------------------------
    // SSL helpers
    // -----------------------------------------------------------------------
    static bool SslReadExact(SSL* ssl, void* buf, std::size_t n) {
        std::size_t done = 0;
        auto* p = static_cast<std::uint8_t*>(buf);
        while (done < n) {
            int r = SSL_read(ssl, p + done, static_cast<int>(n - done));
            if (r <= 0) return false;
            done += static_cast<std::size_t>(r);
        }
        return true;
    }

    static int SslWriteAll(SSL* ssl, const void* buf, std::size_t n) {
        std::size_t done = 0;
        const auto* p = static_cast<const std::uint8_t*>(buf);
        while (done < n) {
            int r = SSL_write(ssl, p + done, static_cast<int>(n - done));
            if (r <= 0) return -1;
            done += static_cast<std::size_t>(r);
        }
        return static_cast<int>(done);
    }

    void CleanupSsl() noexcept {
        if (ssl_) {
            SSL_shutdown(ssl_);
            SSL_free(ssl_);
            ssl_ = nullptr;
        }
        if (ctx_) {
            SSL_CTX_free(ctx_);
            ctx_ = nullptr;
        }
        if (sockfd_ >= 0) {
            ::close(sockfd_);
            sockfd_ = -1;
        }
        connected_.store(false, std::memory_order_release);
    }

    static std::int64_t RecvNowNs() noexcept {
        using namespace std::chrono;
        return static_cast<std::int64_t>(
            duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count());
    }

    // -----------------------------------------------------------------------
    // Data members
    // -----------------------------------------------------------------------
    bool verbose_{false};

    std::string url_;
    std::string wss_host_;
    std::string wss_path_;
    ProxySpec proxy_;

    // SSL state (owned by io_thread_; set before send_thread_ starts)
    SSL* ssl_{nullptr};
    SSL_CTX* ctx_{nullptr};
    int sockfd_{-1};

    std::atomic<bool> stop_{false};
    std::atomic<bool> connected_{false};

    // io_thread_: recv loop
    std::thread io_thread_;

    // send_thread_: send loop
    std::thread send_thread_;
    std::mutex send_mu_;
    std::condition_variable send_cv_;
    std::queue<std::string> send_queue_;

    // Callbacks (set before AsyncConnect)
    OnTextFrame on_text_frame_;
    OnConnected on_connected_;
    OnDisconnected on_disconnected_;
};

}  // namespace stcpp::debug_api
