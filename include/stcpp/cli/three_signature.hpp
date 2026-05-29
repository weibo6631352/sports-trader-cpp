// stcpp/cli/three_signature.hpp — ThreeSignatureVerifier v1 (老沈, W6 Wave 29)
//
// 落: docs/RUNBOOKS/strategy-decayed-unlock-sop-v1.md §4 Step 4
//     laohan-riskmanager-design-v0.3.1.md Smell #4
//     ADR-009 v2 (Sonnet)
//
// 职责:
//   Ed25519 三签独立验证 + 集成 (libsodium crypto_sign_ed25519)
//   签名载体: "<strategy_id>|STRATEGY_DECAYED_UNLOCK|<trigger_audit_id_hex>|<timestamp_ns>"
//
// 红线:
//   R-1  解锁经 RM set_state(), 不绕过 evaluate()
//   R-7  paper / live exec-mode 由 CLI 传入, 不硬编码
//   R-11 paper / live audit WAL 物理隔离 (由 AuditEmitter 保证)
//   私钥不入日志 / 不入 git / .env 存公钥 RM_UNLOCK_PUBKEY_{LAOHAN,LAOTANG,LAOLEI}
//
// Ed25519:
//   libsodium crypto_sign_ed25519_verify_detached (非自写, FIPS/审计友好)
//   公钥 32B, 签名 64B, message = UTF-8 payload
//
// timestamp drift: |sig_ts - now_ts| <= kMaxDriftNs (1h) — 防重放

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace stcpp::cli {

// Ed25519 公钥 32B
inline constexpr std::size_t kEd25519PubKeyBytes = 32;
// Ed25519 签名 64B
inline constexpr std::size_t kEd25519SigBytes = 64;
// 最大时间漂移: 1h (防重放)
inline constexpr std::int64_t kMaxDriftNs = 3'600'000'000'000LL;

// 单签验证结果
enum class SigVerifyResult : std::uint8_t {
    OK = 0,
    BAD_SIGNATURE = 1,  // Ed25519 验签失败
    EXPIRED = 2,        // |sig_ts - now_ts| > kMaxDriftNs
    BAD_PAYLOAD = 3,    // payload 字段与预期不一致
    MISSING_KEY = 4,    // 公钥为全零 (未配置)
};

[[nodiscard]] constexpr std::string_view ToString(SigVerifyResult r) noexcept {
    switch (r) {
        case SigVerifyResult::OK:
            return "OK";
        case SigVerifyResult::BAD_SIGNATURE:
            return "BAD_SIGNATURE";
        case SigVerifyResult::EXPIRED:
            return "EXPIRED";
        case SigVerifyResult::BAD_PAYLOAD:
            return "BAD_PAYLOAD";
        case SigVerifyResult::MISSING_KEY:
            return "MISSING_KEY";
    }
    return "UNKNOWN";
}

// 三签集成结果
enum class ThreeSigResult : std::uint8_t {
    OK = 0,
    LAOHAN_FAILED = 1,
    LAOTANG_FAILED = 2,
    LAOLEI_FAILED = 3,
    INSUFFICIENT = 4,  // 非 3 签输入 (emergency_override=false 时强制要求 3 签)
};

[[nodiscard]] constexpr std::string_view ToString(ThreeSigResult r) noexcept {
    switch (r) {
        case ThreeSigResult::OK:
            return "OK";
        case ThreeSigResult::LAOHAN_FAILED:
            return "LAOHAN_FAILED";
        case ThreeSigResult::LAOTANG_FAILED:
            return "LAOTANG_FAILED";
        case ThreeSigResult::LAOLEI_FAILED:
            return "LAOLEI_FAILED";
        case ThreeSigResult::INSUFFICIENT:
            return "INSUFFICIENT";
    }
    return "UNKNOWN";
}

// 单签输入
struct SignerInput {
    // Ed25519 公钥 (32B, 从 .env RM_UNLOCK_PUBKEY_* 读取, base64 解码后填入)
    std::array<std::uint8_t, kEd25519PubKeyBytes> pubkey{};
    // Ed25519 签名 (64B, base64 解码后填入)
    std::array<std::uint8_t, kEd25519SigBytes> signature{};
    // 签名时的 Unix timestamp (ns) — 来自签名 payload 解析
    std::int64_t sig_timestamp_ns{0};
};

// 三签聚合输入
struct ThreeSignatureInput {
    SignerInput laohan;
    SignerInput laotang;
    SignerInput laolei;
    // 被签名的 payload (UTF-8 明文, 三签共用相同 payload)
    // 格式: "<strategy_id>|STRATEGY_DECAYED_UNLOCK|<trigger_audit_id_hex>|<timestamp_ns>"
    std::string payload;
    // 是否紧急 override (GM 单签模式, SOP §7)
    bool emergency_override{false};
};

// ThreeSignatureVerifier — 三签独立验证 + 集成
//
// 使用方:
//   ThreeSignatureVerifier verifier;
//   auto result = verifier.verify(input, now_ns);
//   if (result != ThreeSigResult::OK) { ... }
//
// libsodium 依赖: 调用方须在进程启动时调用 sodium_init() (CLI main 负责)
class ThreeSignatureVerifier {
public:
    ThreeSignatureVerifier() = default;
    ~ThreeSignatureVerifier() = default;
    ThreeSignatureVerifier(ThreeSignatureVerifier const&) = delete;
    ThreeSignatureVerifier& operator=(ThreeSignatureVerifier const&) = delete;

    // 主入口: 三签集成验证
    // now_ns: 当前单调时钟 ns (CLI main 传入, 不在本类调 now() — 遵 R-20 精神)
    // emergency_override=true 时只验 laolei 签, 允许单签
    [[nodiscard]] ThreeSigResult verify(ThreeSignatureInput const& in, std::int64_t now_ns) const noexcept;

    // 单签验证 (测试 + 内部调用)
    [[nodiscard]] SigVerifyResult verify_one(SignerInput const& signer, std::string_view payload,
                                             std::int64_t now_ns) const noexcept;

    // Payload 构造 helper (CLI 生成待签内容时使用)
    // 格式: "<strategy_id>|STRATEGY_DECAYED_UNLOCK|<trigger_audit_id_hex>|<timestamp_ns>"
    [[nodiscard]] static std::string build_payload(std::string_view strategy_id,
                                                   std::string_view trigger_audit_id_hex,
                                                   std::int64_t timestamp_ns) noexcept;

    // Base64 decode helper (CLI 读取 --*-sig= 参数用)
    // 返回 false 表示输入不是合法 base64 或长度不匹配
    [[nodiscard]] static bool decode_base64_sig(std::string_view b64,
                                                std::array<std::uint8_t, kEd25519SigBytes>& out) noexcept;

    [[nodiscard]] static bool decode_base64_pubkey(
        std::string_view b64, std::array<std::uint8_t, kEd25519PubKeyBytes>& out) noexcept;
};

}  // namespace stcpp::cli
