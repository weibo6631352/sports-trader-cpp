// src/stcpp/bin/cli/three_signature.cpp — ThreeSignatureVerifier impl (老沈, W6 Wave 29)
//
// libsodium: crypto_sign_ed25519_verify_detached
// 依赖: vcpkg libsodium (pin 见 vcpkg.json)
//
// 私钥操作规范:
//   - 私钥由各签字人离线生成 (libsodium crypto_sign_keypair 或 openssl genpkey)
//   - 公钥通过 .env RM_UNLOCK_PUBKEY_* 注入 (不入 git)
//   - 签名在各人本机离线计算后以 base64 传给 CLI
//   - CLI 本身不接触私钥, 只做验签

#include "stcpp/cli/three_signature.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <sstream>
#include <string>

// libsodium (vcpkg: sodium)
#include <sodium.h>

namespace stcpp::cli {

// ---------------------------------------------------------------------------
// internal helpers
// ---------------------------------------------------------------------------

namespace {

// 标准 Base64 alphabet (RFC 4648)
constexpr char kB64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

bool base64_decode(std::string_view input, std::uint8_t* out, std::size_t expected_len) noexcept {
    if (input.empty())
        return false;
    // libsodium sodium_base642bin is the most robust option
    std::size_t decoded_len = 0;
    int rc = sodium_base642bin(out, expected_len, input.data(), input.size(),
                               nullptr,  // ignore whitespace: none
                               &decoded_len,
                               nullptr,  // end ptr
                               sodium_base64_VARIANT_ORIGINAL);
    return (rc == 0 && decoded_len == expected_len);
}

// all-zero pubkey → MISSING_KEY
bool pubkey_is_zero(std::array<std::uint8_t, kEd25519PubKeyBytes> const& k) noexcept {
    for (auto b : k) {
        if (b != 0)
            return false;
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// ThreeSignatureVerifier::verify_one
// ---------------------------------------------------------------------------

SigVerifyResult ThreeSignatureVerifier::verify_one(SignerInput const& signer, std::string_view payload,
                                                   std::int64_t now_ns) const noexcept {
    // C1: 公钥已配置
    if (pubkey_is_zero(signer.pubkey)) {
        return SigVerifyResult::MISSING_KEY;
    }

    // C2: timestamp drift <= 1h
    std::int64_t drift = signer.sig_timestamp_ns - now_ns;
    if (drift < 0)
        drift = -drift;
    if (drift > kMaxDriftNs) {
        return SigVerifyResult::EXPIRED;
    }

    // C3: payload 非空
    if (payload.empty()) {
        return SigVerifyResult::BAD_PAYLOAD;
    }

    // C4: Ed25519 验签 (libsodium)
    int rc = crypto_sign_ed25519_verify_detached(
        signer.signature.data(), reinterpret_cast<const unsigned char*>(payload.data()),
        static_cast<unsigned long long>(payload.size()), signer.pubkey.data());

    if (rc != 0) {
        return SigVerifyResult::BAD_SIGNATURE;
    }
    return SigVerifyResult::OK;
}

// ---------------------------------------------------------------------------
// ThreeSignatureVerifier::verify
// ---------------------------------------------------------------------------

ThreeSigResult ThreeSignatureVerifier::verify(ThreeSignatureInput const& in,
                                              std::int64_t now_ns) const noexcept {
    if (in.emergency_override) {
        // 紧急 override: 只验 laolei (GM), SOP §7
        auto r = verify_one(in.laolei, in.payload, now_ns);
        if (r != SigVerifyResult::OK)
            return ThreeSigResult::LAOLEI_FAILED;
        return ThreeSigResult::OK;
    }

    // 正常三签: 顺序不可跳 (老韩 → 老唐 → 老雷), 但 CLI 端全部独立验证
    auto r1 = verify_one(in.laohan, in.payload, now_ns);
    if (r1 != SigVerifyResult::OK)
        return ThreeSigResult::LAOHAN_FAILED;

    auto r2 = verify_one(in.laotang, in.payload, now_ns);
    if (r2 != SigVerifyResult::OK)
        return ThreeSigResult::LAOTANG_FAILED;

    auto r3 = verify_one(in.laolei, in.payload, now_ns);
    if (r3 != SigVerifyResult::OK)
        return ThreeSigResult::LAOLEI_FAILED;

    return ThreeSigResult::OK;
}

// ---------------------------------------------------------------------------
// Payload builder
// ---------------------------------------------------------------------------

std::string ThreeSignatureVerifier::build_payload(std::string_view strategy_id,
                                                  std::string_view trigger_audit_id_hex,
                                                  std::int64_t timestamp_ns) noexcept {
    std::ostringstream ss;
    ss << strategy_id << "|STRATEGY_DECAYED_UNLOCK|" << trigger_audit_id_hex << "|" << timestamp_ns;
    return ss.str();
}

// ---------------------------------------------------------------------------
// Base64 decode helpers
// ---------------------------------------------------------------------------

bool ThreeSignatureVerifier::decode_base64_sig(std::string_view b64,
                                               std::array<std::uint8_t, kEd25519SigBytes>& out) noexcept {
    return base64_decode(b64, out.data(), kEd25519SigBytes);
}

bool ThreeSignatureVerifier::decode_base64_pubkey(
    std::string_view b64, std::array<std::uint8_t, kEd25519PubKeyBytes>& out) noexcept {
    return base64_decode(b64, out.data(), kEd25519PubKeyBytes);
}

}  // namespace stcpp::cli
