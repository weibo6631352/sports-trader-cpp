// stcpp/crypto/ed25519.cpp — Ed25519 sign/verify impl (C++20, libsodium)
//
// Owner: 老孙 (#06, crypto-signing-expert)
// Wave 34 W8: libsodium FetchContent ExternalProject_Add cpp v0.2
//
// 落:
//   laoshan-sop-v6.md §6 (Option A: ExternalProject_Add libsodium-1.0.20)
//   老沈 W7 ack: 抽 stcpp_crypto_ed25519 INTERFACE target
//
// 红线:
//   R-11 paper mode 不真签 — 本 impl 无 mode 判断, 由调用方(signer_v52)传 mock key
//   R-12 不在 hot path (启动期签名)
//   HMAC bug 4 (老孙 W3 v3 §A) — 本文件不含 HMAC, enforce 注释见 signer_v52.hpp
//   BUG-W5-001 防御 — shift safety: 仅用 libsodium 原生 API, 不自写字节移位
//
// BUG-W5-001 shift safety (永久 enforce):
//   本文件不自写 shift / xor 对签名字节操作.
//   只调 libsodium crypto_sign_ed25519_detached / crypto_sign_ed25519_verify_detached.
//   libsodium 内部已处理 endian + alignment.

#include "stcpp/crypto/ed25519.hpp"

#include <cstring>

// libsodium (ExternalProject_Add IMPORTED STATIC; header 注入)
#include <sodium.h>

namespace stcpp::crypto {

// ---------- Ed25519::sign ----------

std::array<std::uint8_t, kEd25519SignatureBytes>
Ed25519::sign(const SecureBuffer<kEd25519SecretKeyBytes>& secret_key,
              std::span<const std::uint8_t> message) noexcept {
    std::array<std::uint8_t, kEd25519SignatureBytes> sig{};

    // libsodium detached sign
    // sig_len 为 out 参数; Ed25519 detached 固定 64B
    unsigned long long sig_len = 0ULL;  // NOLINT(google-runtime-int) — libsodium API
    int rc = crypto_sign_ed25519_detached(
        sig.data(),
        &sig_len,
        message.data(),
        static_cast<unsigned long long>(message.size()),  // NOLINT(google-runtime-int)
        secret_key.data()
    );

    if (rc != 0 || sig_len != kEd25519SignatureBytes) {
        // 极罕见: libsodium 内部错误; 返回全零数组 (caller 需检验非零)
        sig.fill(0U);
        return sig;
    }

    return sig;
}

// ---------- Ed25519::verify ----------

bool Ed25519::verify(
    std::span<const std::uint8_t, kEd25519PublicKeyBytes> public_key,
    std::span<const std::uint8_t> message,
    std::span<const std::uint8_t, kEd25519SignatureBytes> signature) noexcept {

    int rc = crypto_sign_ed25519_verify_detached(
        signature.data(),
        message.data(),
        static_cast<unsigned long long>(message.size()),  // NOLINT(google-runtime-int)
        public_key.data()
    );

    // libsodium: 0 = 有效; -1 = 无效 (含 message 被篡改 / key 不匹配)
    return rc == 0;
}

// ---------- Ed25519::generate_keypair ----------

bool Ed25519::generate_keypair(
    std::array<std::uint8_t, kEd25519PublicKeyBytes>& pk_out,
    SecureBuffer<kEd25519SecretKeyBytes>& sk_out) noexcept {

    // crypto_sign_ed25519_keypair: 写 pk (32B) + sk (64B = seed || pk)
    int rc = crypto_sign_ed25519_keypair(pk_out.data(), sk_out.data());
    return rc == 0;
}

}  // namespace stcpp::crypto
