// stcpp/net/persistent_https.hpp — 持久化 (keep-alive) HTTPS GET 客户端 (热链高频轮询)
//
// Owner: 老雷 (GM) — 2026-06-04 老板「主动实时查询订单簿压着官方限速, 全市场共享 149hz, 用热链」。
//   PM /book 单 token 限速 150 req/s; SeedTokensFromRest 的 popen curl 每请求冷连 (TLS 握手 ~30ms)
//   太慢, 跑不满 149hz。本客户端持久一条 TLS 连接复用, GET 之间不重握手 → 可压满限速。
//
// 设计:
//   - 单线程使用 (调用方在 ActiveBookPoller 线程内独占; 非线程安全, 不共享)。
//   - SSL_write/SSL_read 失败 → shut() 关连接, 下次 Get() 自动重连 (热链自愈)。
//   - 带 socket 读写超时 (防 PM 卡住整个轮询线程)。
//   - 仅 GET (轮询只读 /book); 不含凭证 (公开端点, URL 无 key — 红线: 不记 URL 到日志)。
//
// 红线: 不在 WSS event loop 调用 (本客户端在独立 ActiveBookPoller 线程, R-12 合规)。
#pragma once

#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>

#include <openssl/ssl.h>

namespace stcpp::net {

class PersistentHttps {
public:
    explicit PersistentHttps(std::string host, int timeout_ms = 4000) noexcept
        : host_(std::move(host)), timeout_ms_(timeout_ms) {}
    ~PersistentHttps() { Shut(); }
    PersistentHttps(const PersistentHttps&) = delete;
    PersistentHttps& operator=(const PersistentHttps&) = delete;

    // Get — 复用持久连接发 GET path; 返回 response body (chunked/content-length 解完整)。
    //   失败 (连接断/超时/非 2xx 不区分) → 返回空串 + 关连接 (下次 Get 重连)。
    [[nodiscard]] std::string Get(const std::string& path) noexcept {
        if (ssl_ == nullptr && !Open()) return {};
        std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + host_ +
                          "\r\nConnection: keep-alive\r\nAccept: application/json\r\n\r\n";
        if (SSL_write(ssl_, req.data(), static_cast<int>(req.size())) <= 0) {
            Shut();
            return {};
        }
        std::string buf;
        char c[8192];
        int n;
        std::size_t hp;
        while ((hp = buf.find("\r\n\r\n")) == std::string::npos) {
            n = SSL_read(ssl_, c, sizeof(c));
            if (n <= 0) {
                Shut();
                return {};
            }
            buf.append(c, static_cast<std::size_t>(n));
            if (buf.size() > kMaxResp) {
                Shut();
                return {};
            }
        }
        std::string head = buf.substr(0, hp);
        std::string body = buf.substr(hp + 4);
        std::string lh = head;
        std::transform(lh.begin(), lh.end(), lh.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        // chunked
        if (lh.find("transfer-encoding: chunked") != std::string::npos) {
            std::string de;
            while (true) {
                std::size_t e;
                while ((e = body.find("\r\n")) == std::string::npos) {
                    n = SSL_read(ssl_, c, sizeof(c));
                    if (n <= 0) {
                        Shut();
                        return de;
                    }
                    body.append(c, static_cast<std::size_t>(n));
                }
                long sz = std::strtol(body.substr(0, e).c_str(), nullptr, 16);
                body.erase(0, e + 2);
                if (sz <= 0) break;
                while (static_cast<long>(body.size()) < sz + 2) {
                    n = SSL_read(ssl_, c, sizeof(c));
                    if (n <= 0) {
                        Shut();
                        return de;
                    }
                    body.append(c, static_cast<std::size_t>(n));
                }
                de.append(body, 0, static_cast<std::size_t>(sz));
                body.erase(0, static_cast<std::size_t>(sz) + 2);
                if (de.size() > kMaxResp) {
                    Shut();
                    return de;
                }
            }
            return de;
        }
        // content-length
        auto cl = lh.find("content-length:");
        long len = (cl != std::string::npos) ? std::atol(lh.c_str() + cl + 15) : -1;
        if (len >= 0) {
            while (static_cast<long>(body.size()) < len) {
                n = SSL_read(ssl_, c, sizeof(c));
                if (n <= 0) {
                    Shut();
                    break;
                }
                body.append(c, static_cast<std::size_t>(n));
            }
            return body.substr(0, std::min(static_cast<long>(body.size()), len));
        }
        return body;
    }

    [[nodiscard]] bool connected() const noexcept { return ssl_ != nullptr; }

private:
    static constexpr std::size_t kMaxResp = 4 * 1024 * 1024;  // 4MB 上限 (防异常巨包)

    bool Open() noexcept {
        Shut();
        struct addrinfo hints{};
        struct addrinfo* res = nullptr;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host_.c_str(), "443", &hints, &res) != 0 || res == nullptr) return false;
        fd_ = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (fd_ < 0) {
            freeaddrinfo(res);
            return false;
        }
        // socket 读写超时 (防卡死轮询线程)
        struct timeval tv{};
        tv.tv_sec = timeout_ms_ / 1000;
        tv.tv_usec = (timeout_ms_ % 1000) * 1000;
        ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        ::setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        if (::connect(fd_, res->ai_addr, res->ai_addrlen) != 0) {
            freeaddrinfo(res);
            Shut();
            return false;
        }
        freeaddrinfo(res);
        ctx_ = SSL_CTX_new(TLS_client_method());
        if (ctx_ == nullptr) {
            Shut();
            return false;
        }
        ssl_ = SSL_new(ctx_);
        if (ssl_ == nullptr) {
            Shut();
            return false;
        }
        SSL_set_tlsext_host_name(ssl_, host_.c_str());
        SSL_set_fd(ssl_, fd_);
        if (SSL_connect(ssl_) != 1) {
            Shut();
            return false;
        }
        return true;
    }

    void Shut() noexcept {
        if (ssl_ != nullptr) {
            SSL_free(ssl_);
            ssl_ = nullptr;
        }
        if (ctx_ != nullptr) {
            SSL_CTX_free(ctx_);
            ctx_ = nullptr;
        }
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    std::string host_;
    int timeout_ms_;
    SSL_CTX* ctx_ = nullptr;
    SSL* ssl_ = nullptr;
    int fd_ = -1;
};

}  // namespace stcpp::net
