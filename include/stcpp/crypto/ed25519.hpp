// stcpp/crypto/ed25519.hpp — Ed25519 sign/verify wrapper (C++20)
//
// Owner: 老孙 (#06, crypto-signing-expert)
// Wave 34 W8: libsodium FetchContent ExternalProject_Add cpp v0.2
//             老沈 W7 ack Option A — 抽 stcpp_crypto_ed25519 INTERFACE target
//
// 落:
//   laoshan-sop-v6.md §6 (Option A: FetchContent + ExternalProject_Add libsodium-1.0.20)
//   laoSun-signer-v5.2-cpp-design.md §3 (Ed25519 wrapper INTERFACE)
//   老沈 W7 ack: signer v52 + STRATEGY_DECAYED CLI 共用 stcpp_crypto_ed25519
//
// 职责:
//   - 包装 libsodium crypto_sign_ed25519_detached (sign)
//   - 包装 libsodium crypto_sign_ed25519_verify_detached (verify)
//   - SecureBuffer<N>: 私钥内存包装 + 析构 sodium_memzero (老沈 W6 §6)
//
// 红线:
//   R-7  build-time mode (paper/live/backtest 共享 stcpp_crypto_ed25519 INTERFACE)
//   R-11 paper mode 不真签 — secret_key mock (调用方责任; 本 wrapper 纯实现)
//   R-12 不在 hot path (启动期签名, 不在 WSS event loop)
//   HMAC bug 4 (老孙 W3 v3 §A) — 本 wrapper 不做 HMAC, 但 ABI 保留注释
//   BUG-W5-001 防御 (shift safety, 不复用)
//
// ABI lock:
//   Ed25519::sign(secret_key 64B, message) -> array<uint8_t, 64>
//   Ed25519::verify(public_key 32B, message, signature 64B) -> bool
//   SecureBuffer<N> — 析构清零 (sodium_memzero)
//
// 线程安全:
//   Ed25519 是 stateless 静态方法 — 多线程调用安全
//   SecureBuffer 不线程安全 (同 std::array 语义)
//
// 依赖:
//   libsodium (ExternalProject_Add, libsodium-1.0.20-RELEASE, 静态库)
//   链接目标: stcpp_crypto_ed25519 (INTERFACE + libsodium_external IMPORTED STATIC)

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <span>

// libsodium (FetchContent ExternalProject_Add, 头文件由 libsodium_external 注入)
#include <sodium.h>

namespace stcpp::crypto {

// ---------- 常量 (ABI lock) ----------

/// Ed25519 签名字节数 (固定 64B)
inline constexpr std::size_t kEd25519SignatureBytes = 64U;
/// Ed25519 公钥字节数 (固定 32B)
inline constexpr std::size_t kEd25519PublicKeyBytes = 32U;
/// Ed25519 私钥字节数 (libsodium seed||pk 拼接, 64B)
inline constexpr std::size_t kEd25519SecretKeyBytes = 64U;

// ABI 断言: libsodium 常量必须与本 wrapper 一致
static_assert(crypto_sign_ed25519_BYTES      == kEd25519SignatureBytes,
    "ABI: crypto_sign_ed25519_BYTES != kEd25519SignatureBytes");
static_assert(crypto_sign_ed25519_PUBLICKEYBYTES == kEd25519PublicKeyBytes,
    "ABI: crypto_sign_ed25519_PUBLICKEYBYTES != kEd25519PublicKeyBytes");
static_assert(crypto_sign_ed25519_SECRETKEYBYTES == kEd25519SecretKeyBytes,
    "ABI: crypto_sign_ed25519_SECRETKEYBYTES != kEd25519SecretKeyBytes");

// ---------- SecureBuffer<N> — 私钥内存包装 (老沈 W6 §6) ----------
//
// 析构时调用 sodium_memzero (safe against compiler optimization).
// 禁止拷贝 (私钥不可 copy). 允许 move (所有权转移).
//
// 典型用法:
//   SecureBuffer<kEd25519SecretKeyBytes> sk;
//   // ... fill sk.data() ...
//   // 离开作用域自动清零

template <std::size_t N>
class SecureBuffer {
 public:
    SecureBuffer() noexcept { buf_.fill(0U); }

    // 禁止拷贝 (安全: 私钥不可 copy)
    SecureBuffer(const SecureBuffer&)            = delete;
    SecureBuffer& operator=(const SecureBuffer&) = delete;

    // 允许 move (所有权转移; other 的内存在 move 后清零)
    SecureBuffer(SecureBuffer&& other) noexcept : buf_(other.buf_) {
        sodium_memzero(other.buf_.data(), N);
    }
    SecureBuffer& operator=(SecureBuffer&& other) noexcept {
        if (this != &other) {
            sodium_memzero(buf_.data(), N);
            buf_ = other.buf_;
            sodium_memzero(other.buf_.data(), N);
        }
        return *this;
    }

    // 析构: 清零 (防止私钥留在内存)
    ~SecureBuffer() noexcept {
        sodium_memzero(buf_.data(), N);
    }

    // 数据访问
    [[nodiscard]] std::uint8_t* data() noexcept { return buf_.data(); }
    [[nodiscard]] const std::uint8_t* data() const noexcept { return buf_.data(); }
    [[nodiscard]] static constexpr std::size_t size() noexcept { return N; }

    [[nodiscard]] std::uint8_t& operator[](std::size_t i) noexcept { return buf_[i]; }
    [[nodiscard]] const std::uint8_t& operator[](std::size_t i) const noexcept { return buf_[i]; }

    // span 兼容
    [[nodiscard]] std::span<std::uint8_t, N> span() noexcept {
        return std::span<std::uint8_t, N>{buf_.data(), N};
    }
    [[nodiscard]] std::span<const std::uint8_t, N> span() const noexcept {
        return std::span<const std::uint8_t, N>{buf_.data(), N};
    }

 private:
    std::array<std::uint8_t, N> buf_{};
};

// ---------- Ed25519 — stateless sign/verify wrapper ----------
//
// 所有方法静态 (stateless); 线程安全.
// 调用前需确保 sodium_init() 已调用 (返回 >= 0).
//
// sign: 使用 secret_key (64B) 对 message 产生 detached 签名 (64B).
//   返回 std::array<uint8_t, 64>; 失败 (内部错误极罕见) 返回全零数组.
//
// verify: 使用 public_key (32B) 验证 detached 签名 (64B).
//   返回 true = 签名有效; false = 无效或参数错误.
//
// generate_keypair: 生成随机 Ed25519 keypair (paper mock 用).
//   pk 32B (out), sk 64B (out); 成功 true.

class Ed25519 {
 public:
    // ---------- sign ----------
    //
    // secret_key: 64B libsodium Ed25519 secret key (seed || pubkey 拼接)
    // message:    任意字节序列 (std::span<const uint8_t>)
    // returns:    64B detached signature; 失败返回全零 (内部 ASAN-safe)
    //
    // 注: R-12 — 仅启动期调用 (paper: 每次 Sign() 请求; live M5+: 启动时导入密钥后缓存)
    // 注: paper mode 传 mock secret_key (32B 全 0x42 seed) — 调用方责任 (R-11)
    [[nodiscard]] static std::array<std::uint8_t, kEd25519SignatureBytes>
    sign(const SecureBuffer<kEd25519SecretKeyBytes>& secret_key,
         std::span<const std::uint8_t> message) noexcept;

    // ---------- verify ----------
    //
    // public_key: 32B Ed25519 public key
    // message:    原始字节序列 (与签名时相同)
    // signature:  64B detached signature (来自 sign())
    // returns:    true = 签名有效; false = 无效
    [[nodiscard]] static bool
    verify(std::span<const std::uint8_t, kEd25519PublicKeyBytes> public_key,
           std::span<const std::uint8_t> message,
           std::span<const std::uint8_t, kEd25519SignatureBytes> signature) noexcept;

    // ---------- generate_keypair ----------
    //
    // 生成随机 Ed25519 keypair (libsodium crypto_sign_ed25519_keypair).
    // pk_out: 32B 公钥 (out)
    // sk_out: 64B 私钥 SecureBuffer (out; 析构时自动清零)
    // returns: true = 成功
    //
    // paper mode 用于生成临时 keypair; live M5+ 不调此函数 (从 .env 导入)
    [[nodiscard]] static bool
    generate_keypair(std::array<std::uint8_t, kEd25519PublicKeyBytes>& pk_out,
                     SecureBuffer<kEd25519SecretKeyBytes>& sk_out) noexcept;

    // 禁止实例化 (纯静态工具类)
    Ed25519() = delete;
};

}  // namespace stcpp::crypto
