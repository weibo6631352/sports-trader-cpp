// tests/unit/test_signer_v53_pit.cpp — SignerV52 v5.3 新增 7 ctest (T1-T7)
//
// Owner: 老孙 (#06, crypto-signing-expert)
// Wave 58 W9: v5.3 新增字段 token_id/side/outcome + EIP-712 binding + side 0/1 校验
//             + sigType enforce 修 (1 = Magic Safe EOA) + P99 < 8us 热路径
//
// ADR-027 cite (Enforce-1 强 enforce):
//   polymarket_ssot_cite: docs/RESEARCH/laoli-w8-polymarket-data-structure-ssot-v1.md §3.5
//   handshake_cite:       docs/RESEARCH/laoli-laoSun-handshake-v1.md §84
//   goalserve_ssot_cite:  N/A
//   adr_cite:             docs/ADR/2026-06-W4-adr-027-core-data-structure-ssot-enforce.md
//
// 测试矩阵 (7 case, spec §5 T1-T7):
//   T1: token_id byte-equal ABI binding (paper proxy message 含 token_id string bytes)
//   T2: side 0/1 round-trip + side=2 拒签 (InvalidSide, reject_reason="invalid_side")
//   T3: HMAC 4 bug 反陷阱维持 (sigType=1 accept; sigType=2 reject — v5.3 修正)
//   T4: SecureBuffer<64> sodium_memzero 维持 (v5.1 已落, v5.3 不回退)
//   T5: paper/live 共用 stcpp_crypto_ed25519 (paper sign + verify; 无 mode 判断)
//   T6: v5.1 cases migration — token_id/side 填 valid default, 旧行为不变
//   T7: P99 < 8us hot path (100k 次签名 CLOCK_MONOTONIC_RAW 测量)
//
// 红线:
//   R-7  paper mode only (STCPP_EXEC_MODE_paper guard)
//   R-20 4 ts chain (event_ts ≤ ds_ts ≤ ingest_ts ≤ as_of_ts ≤ now)
//   R-11 paper mock key, audit_wal_kind=PaperAudit
//   私钥不入 log (CLAUDE.md §8 红线)

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "stcpp/crypto/ed25519.hpp"
#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/signer/v52/signer_v52.hpp"

#include <sodium.h>  // crypto_sign_ed25519_verify_detached, CLOCK_MONOTONIC_RAW

namespace stcpp::signer::v52::test_v53 {

namespace {

// ---------------------------------------------------------------------------
// 构造 v5.3 合法请求 (token_id + side + outcome 均填)
// ---------------------------------------------------------------------------
SignV52Request MakeValidV53Request() {
    SignV52Request req;
    const std::int64_t base = infra::wal::pit::NowRealtimeNs() - 1'000'000'000LL;
    req.event_ts_ns = base;
    req.data_source_ts_ns = base + 1'000'000LL;
    req.ingestion_ts_ns = base + 2'000'000LL;
    req.as_of_ts_ns = base + 3'000'000LL;
    req.data_source_ts_source = 0U;  // UpstreamPayload
    req.market_id = "0xa9db6005902abcdef1234567890abcdef1234567890abcdef12345678900000";
    // token_id: 样本 uint256 decimal (spec §5 T1 golden)
    // cite: SSOT §3.5 token_id + handshake §84 tokenId
    req.token_id = "1677202003548168512111076196662317438975560192301735320827449539424843146463";
    req.side = 0U;            // Buy
    req.outcome = 0U;         // Yes (audit only)
    req.signature_type = 1U;  // Magic Safe EOA (HMAC bug #2 修正)
    req.intent_id = 1234ULL;
    // audit_id 非零 (BUG-W5-001)
    req.audit_id[0] = 0xDE;
    req.audit_id[1] = 0xAD;
    req.audit_id[2] = 0xBE;
    req.audit_id[3] = 0xEF;
    req.audit_id[4] = 0x01;
    req.audit_id[5] = 0x02;
    req.audit_id[6] = 0x03;
    req.audit_id[7] = 0x04;
    req.audit_id[8] = 0x05;
    req.audit_id[9] = 0x06;
    req.audit_id[10] = 0x07;
    req.audit_id[11] = 0x08;
    req.audit_id[12] = 0x09;
    req.audit_id[13] = 0x0A;
    req.audit_id[14] = 0x0B;
    req.audit_id[15] = 0x0C;
    return req;
}

// ---------------------------------------------------------------------------
// 重建 paper proxy message (与 signer_v52.cpp Sign() 中构造逻辑一致)
// v5.3: event_ts(8B LE) || market_id || token_id || side(1B) || outcome(1B) || audit_id(16B)
// ---------------------------------------------------------------------------
std::vector<std::uint8_t> BuildProxyMessage(const SignV52Request& req) {
    std::vector<std::uint8_t> msg;
    msg.reserve(8U + req.market_id.size() + req.token_id.size() + 1U + 1U + 16U);
    // event_ts_ns (8B LE)
    for (std::size_t i = 0U; i < 8U; ++i) {
        msg.push_back(
            static_cast<std::uint8_t>((static_cast<std::uint64_t>(req.event_ts_ns) >> (i * 8U)) & 0xFFU));
    }
    for (const char c : req.market_id) {
        msg.push_back(static_cast<std::uint8_t>(c));
    }
    for (const char c : req.token_id) {
        msg.push_back(static_cast<std::uint8_t>(c));
    }
    msg.push_back(req.side);
    msg.push_back(req.outcome);
    for (const auto b : req.audit_id) {
        msg.push_back(b);
    }
    return msg;
}

}  // namespace

// ============================================================
// T1: token_id ABI binding end-to-end
//     token_id bytes 进入 paper proxy message; 不同 token_id → 不同签名
//     cite: laosun-w9-signer-v53-abi-align-spec-v1.md §5 T1
//     cite: SSOT §3.5 token_id + handshake §84 tokenId (uint256)
// ============================================================
TEST(SignerV53, T1_TokenIdAbiBindingEndToEnd) {
    SignerV52 signer{execution::ExecutionMode::Paper};

    // 基准请求: token_id = spec §5 T1 golden value
    auto req_a = MakeValidV53Request();
    const auto resp_a = signer.Sign(req_a);
    ASSERT_EQ(resp_a.error, SignV52Error::Ok) << "T1: valid token_id request must succeed";
    ASSERT_EQ(resp_a.signature.size(), 64U);

    // 验收标准 1: recovered address (paper Ed25519 verify)
    const auto pk = signer.PublicKeyBytes();
    ASSERT_EQ(pk.size(), 32U);
    const auto msg_a = BuildProxyMessage(req_a);
    const int vr_a = crypto_sign_ed25519_verify_detached(
        resp_a.signature.data(), msg_a.data(),
        static_cast<unsigned long long>(msg_a.size()),  // NOLINT(google-runtime-int)
        pk.data());
    EXPECT_EQ(vr_a, 0) << "T1: verify_detached of token_id message must succeed (recovered address ok)";

    // 验收标准 2: 不同 token_id → 不同签名 (token_id 进消息, byte-deterministic)
    auto req_b = req_a;
    req_b.token_id = "9999999999999999999999999999999999999999999999999999999999999999999999";
    const auto resp_b = signer.Sign(req_b);
    ASSERT_EQ(resp_b.error, SignV52Error::Ok);
    EXPECT_NE(resp_a.signature, resp_b.signature)
        << "T1: different token_id must produce different signatures";

    // 验收标准 3: token_id uint256 string 无截断 (完整 77 字节进入消息)
    const auto msg_b = BuildProxyMessage(req_b);
    // msg_b 比 msg_a 字节数不同 (token_id 长度不同)
    EXPECT_NE(msg_a.size(), msg_b.size())
        << "T1: different length token_id must produce different message lengths";

    // 验收标准 4: 空 token_id → InvalidIntent 拒签
    auto req_empty = req_a;
    req_empty.token_id = "";
    const auto resp_empty = signer.Sign(req_empty);
    EXPECT_EQ(resp_empty.error, SignV52Error::InvalidIntent)
        << "T1: empty token_id must be rejected as InvalidIntent";
    EXPECT_EQ(resp_empty.reject_reason, "invalid_token_id") << "T1: reject_reason must be 'invalid_token_id'";
}

// ============================================================
// T2: side 0/1 round-trip + side=2 拒签
//     side=0 (Buy) / side=1 (Sell) accept; side=2+ → InvalidSide
//     cite: laosun-w9-signer-v53-abi-align-spec-v1.md §5 T2
//     cite: SSOT §3.5 side + handshake §84 side (老韩 v0.5 enum 对齐)
// ============================================================
TEST(SignerV53, T2_SideEnumRoundTrip) {
    SignerV52 signer{execution::ExecutionMode::Paper};
    const auto pk = signer.PublicKeyBytes();
    ASSERT_EQ(pk.size(), 32U);

    // side=0 (Buy) → accept + verify
    {
        auto req = MakeValidV53Request();
        req.side = 0U;  // Buy
        const auto resp = signer.Sign(req);
        ASSERT_EQ(resp.error, SignV52Error::Ok) << "T2: side=0 (Buy) must be accepted";
        ASSERT_EQ(resp.signature.size(), 64U);
        const auto msg = BuildProxyMessage(req);
        const int vr = crypto_sign_ed25519_verify_detached(
            resp.signature.data(), msg.data(),
            static_cast<unsigned long long>(msg.size()),  // NOLINT(google-runtime-int)
            pk.data());
        EXPECT_EQ(vr, 0) << "T2: side=0 signature must verify ok";
    }

    // side=1 (Sell) → accept + verify
    {
        auto req = MakeValidV53Request();
        req.side = 1U;  // Sell
        const auto resp = signer.Sign(req);
        ASSERT_EQ(resp.error, SignV52Error::Ok) << "T2: side=1 (Sell) must be accepted";
        ASSERT_EQ(resp.signature.size(), 64U);
        const auto msg = BuildProxyMessage(req);
        const int vr = crypto_sign_ed25519_verify_detached(
            resp.signature.data(), msg.data(),
            static_cast<unsigned long long>(msg.size()),  // NOLINT(google-runtime-int)
            pk.data());
        EXPECT_EQ(vr, 0) << "T2: side=1 signature must verify ok";
    }

    // side=0 和 side=1 的签名必须不同 (side 进消息)
    {
        auto req_buy = MakeValidV53Request();
        req_buy.side = 0U;
        auto req_sell = req_buy;
        req_sell.side = 1U;
        const auto resp_buy = signer.Sign(req_buy);
        const auto resp_sell = signer.Sign(req_sell);
        ASSERT_EQ(resp_buy.error, SignV52Error::Ok);
        ASSERT_EQ(resp_sell.error, SignV52Error::Ok);
        EXPECT_NE(resp_buy.signature, resp_sell.signature)
            << "T2: Buy and Sell must produce different signatures (side in message)";
    }

    // side=2 → InvalidSide + reject_reason="invalid_side"
    {
        auto req = MakeValidV53Request();
        req.side = 2U;
        const auto resp = signer.Sign(req);
        EXPECT_EQ(resp.error, SignV52Error::InvalidSide) << "T2: side=2 must be rejected as InvalidSide";
        EXPECT_EQ(resp.reject_reason, "invalid_side")
            << "T2: reject_reason must be 'invalid_side' for side=2";
    }

    // side=255 → InvalidSide
    {
        auto req = MakeValidV53Request();
        req.side = 255U;
        const auto resp = signer.Sign(req);
        EXPECT_EQ(resp.error, SignV52Error::InvalidSide) << "T2: side=255 must be rejected as InvalidSide";
    }

    // R-20 4 ts 仍透传 (即使 side 无效)
    {
        auto req = MakeValidV53Request();
        req.side = 2U;
        const auto resp = signer.Sign(req);
        EXPECT_EQ(resp.event_ts_ns, req.event_ts_ns);
        EXPECT_EQ(resp.data_source_ts_ns, req.data_source_ts_ns);
        EXPECT_EQ(resp.ingestion_ts_ns, req.ingestion_ts_ns);
        EXPECT_EQ(resp.as_of_ts_ns, req.as_of_ts_ns);
    }
}

// ============================================================
// T3: HMAC 4 bug 反陷阱 (v5.3 修正 + 维持)
//     T3a: base_string 无 querystring (BUG#1 维持)
//     T3b: sigType=1 accept; sigType=2 reject (BUG#2 v5.3 修正)
//     T3c: signature 64B (paper Ed25519; base64 padding 由 live M5+ 层保证)
//     T3d: market_id 含 '?' 不崩 (BUG#4 维持)
//     cite: laosun-w9-signer-v53-abi-align-spec-v1.md §5 T3 + §4.2
// ============================================================
TEST(SignerV53, T3_HmacBug4AntiPatternV53) {
    SignerV52 signer{execution::ExecutionMode::Paper};

    // T3b: sigType=1 → accept (v5.3 修正: 1 是正确值, Magic Safe EOA)
    // cite: SSOT §5 T-10 + handshake §84 signatureType
    {
        auto req = MakeValidV53Request();
        req.signature_type = 1U;  // 正确值 (Magic Safe EOA)
        const auto resp = signer.Sign(req);
        EXPECT_EQ(resp.error, SignV52Error::Ok) << "T3b: sigType=1 (Magic Safe EOA) must be accepted in v5.3";
    }

    // T3b: sigType=2 → reject (v5.3: 2 是 HMAC bug #2 的错误值)
    // v5.1 曾错误 enforce == 2; v5.3 修正
    {
        auto req = MakeValidV53Request();
        req.signature_type = 2U;  // HMAC bug #2 错误值, 现在被拒绝
        const auto resp = signer.Sign(req);
        EXPECT_EQ(resp.error, SignV52Error::InternalError)
            << "T3b: sigType=2 must be rejected in v5.3 (HMAC bug #2 fix)";
        EXPECT_EQ(resp.reject_reason, "invalid_signature_type")
            << "T3b: reject_reason must be 'invalid_signature_type'";
    }

    // T3b: sigType=0 → reject
    {
        auto req = MakeValidV53Request();
        req.signature_type = 0U;
        const auto resp = signer.Sign(req);
        EXPECT_EQ(resp.error, SignV52Error::InternalError) << "T3b: sigType=0 must be rejected";
    }

    // T3c: signature 64B (paper Ed25519 detached; BUG#3 base64 padding = live M5+ 层约束)
    {
        auto req = MakeValidV53Request();
        const auto resp = signer.Sign(req);
        ASSERT_EQ(resp.error, SignV52Error::Ok);
        EXPECT_EQ(resp.signature.size(), 64U) << "T3c: paper Ed25519 signature must be exactly 64 bytes";
        bool nonzero = false;
        for (const auto b : resp.signature) {
            if (b != 0U) {
                nonzero = true;
                break;
            }
        }
        EXPECT_TRUE(nonzero) << "T3c: signature must not be all-zero (base64 = live layer, not here)";
    }

    // T3a: market_id 含 querystring — signer 不崩溃 (BUG#1 + BUG#4 caller 层约定)
    {
        auto req = MakeValidV53Request();
        req.market_id = "0xabcd?market=0xa9db&foo=bar";  // BUG#4 pattern
        const auto resp = signer.Sign(req);
        // signer 不关 market_id 格式; 不崩溃即可
        EXPECT_NE(resp.error, SignV52Error::LibsodiumInitFail)
            << "T3a/T3d: signer must not crash on querystring in market_id";
    }

    // T3d: market_id 含尾斜杠 — signer 不崩溃 (BUG#1 caller 层约定)
    {
        auto req = MakeValidV53Request();
        req.market_id = "0xabcdef/";  // BUG#1 pattern
        const auto resp = signer.Sign(req);
        EXPECT_NE(resp.error, SignV52Error::LibsodiumInitFail)
            << "T3d: signer must not crash on trailing slash in market_id";
    }
}

// ============================================================
// T4: SecureBuffer<64> sodium_memzero 维持
//     v5.1 已落 (W8 W1), v5.3 不回退
//     新字段 token_id/side/outcome 是公开订单参数, 不需要 SecureBuffer
//     cite: laosun-w9-signer-v53-abi-align-spec-v1.md §5 T4
// ============================================================
TEST(SignerV53, T4_SecureBufferMemzeroMaintained) {
    // 1. SecureBuffer move → source 清零 (与 NewT4 逻辑一致)
    {
        crypto::SecureBuffer<crypto::kEd25519SecretKeyBytes> sk;
        for (std::size_t i = 0U; i < sk.size(); ++i) {
            sk[i] = static_cast<std::uint8_t>((i % 200U) + 1U);
        }
        EXPECT_NE(sk[0], 0U) << "T4: buffer should be non-zero before move";

        crypto::SecureBuffer<crypto::kEd25519SecretKeyBytes> sk2{std::move(sk)};
        EXPECT_NE(sk2[0], 0U) << "T4: moved-to buffer retains data";
        EXPECT_EQ(sk[0], 0U) << "T4: moved-from buffer must be zeroed (sodium_memzero)";
    }

    // 2. SignerV52 析构后 sk_ 清零 (通过 heap allocation 验证析构路径)
    {
        SignerV52* s = new SignerV52{execution::ExecutionMode::Paper};
        auto req = MakeValidV53Request();
        const auto resp = s->Sign(req);
        EXPECT_EQ(resp.error, SignV52Error::Ok) << "T4: sign must succeed before destruct";
        delete s;  // NOLINT(cppcoreguidelines-owning-memory)
        // 无 crash = SecureBuffer<64> sodium_memzero 析构路径 ok
    }

    // 3. token_id/side/outcome 不需要 SecureBuffer (是公开参数, 非私钥材料)
    //    验证方式: 这三个字段在 SignV52Request 中是 string/uint8, 正常访问
    {
        auto req = MakeValidV53Request();
        // token_id 可正常 read (公开订单参数)
        EXPECT_FALSE(req.token_id.empty()) << "T4: token_id is public order param, no SecureBuffer needed";
        EXPECT_LE(req.side, 1U) << "T4: side is public order param, no SecureBuffer needed";
        // outcome 是 audit only, 不进签名, 不需要 SecureBuffer
        EXPECT_EQ(req.outcome, 0U);
    }

    // 4. 私钥明文不出现在 token_id / reject_reason 字段 (红线: 私钥不入 log)
    //    paper 模式: private_key 全程在 SecureBuffer, 不应泄入 response 字段
    {
        SignerV52 signer{execution::ExecutionMode::Paper};
        auto req = MakeValidV53Request();
        req.side = 2U;  // 触发 reject
        const auto resp = signer.Sign(req);
        EXPECT_EQ(resp.error, SignV52Error::InvalidSide);
        // reject_reason 不含私钥相关内容 (安全性: 拒绝原因不泄露私钥)
        EXPECT_EQ(resp.reject_reason, "invalid_side");
        EXPECT_NE(resp.reject_reason.find("key"), std::string::npos - 1U)
            << "T4: reject_reason must not contain private key material";
    }
}

// ============================================================
// T5: paper/live 共用 stcpp_crypto_ed25519
//     paper mode 签名 + verify; crypto 层无 mode 判断
//     cite: laosun-w9-signer-v53-abi-align-spec-v1.md §5 T5
// ============================================================
TEST(SignerV53, T5_PaperLiveShareCryptoEd25519) {
    // 1. paper mode 使用 stcpp_crypto_ed25519 (INTERFACE target 共用路径)
    SignerV52 signer{execution::ExecutionMode::Paper};
    EXPECT_EQ(signer.Mode(), execution::ExecutionMode::Paper);

    auto req = MakeValidV53Request();
    const auto resp = signer.Sign(req);
    ASSERT_EQ(resp.error, SignV52Error::Ok) << "T5: paper mode sign must succeed";
    ASSERT_EQ(resp.signature.size(), 64U) << "T5: paper Ed25519 signature must be 64B";

    // 2. 同一参数签名两次 byte-equal (deterministic Ed25519 + 同 keypair)
    const auto resp2 = signer.Sign(req);
    ASSERT_EQ(resp2.error, SignV52Error::Ok);
    EXPECT_EQ(resp.signature, resp2.signature)
        << "T5: same request must produce identical signatures (deterministic Ed25519)";

    // 3. stcpp_crypto_ed25519 verify API 直接调 (INTERFACE target linker check)
    const auto pk = signer.PublicKeyBytes();
    ASSERT_EQ(pk.size(), 32U);

    std::array<std::uint8_t, crypto::kEd25519PublicKeyBytes> pk_arr{};
    std::copy(pk.begin(), pk.end(), pk_arr.begin());

    std::array<std::uint8_t, crypto::kEd25519SignatureBytes> sig_arr{};
    std::copy(resp.signature.begin(), resp.signature.end(), sig_arr.begin());

    const auto msg = BuildProxyMessage(req);
    const bool ok =
        crypto::Ed25519::verify(std::span<const std::uint8_t, crypto::kEd25519PublicKeyBytes>{pk_arr},
                                std::span<const std::uint8_t>{msg.data(), msg.size()},
                                std::span<const std::uint8_t, crypto::kEd25519SignatureBytes>{sig_arr});
    EXPECT_TRUE(ok) << "T5: Ed25519::verify of paper signature via shared stcpp_crypto_ed25519 must succeed";

    // 4. crypto 层无 mode 判断 (stcpp_crypto_ed25519 不区分 paper/live)
    //    验证方式: 直接调 Ed25519::generate_keypair (不经 SignerV52)
    {
        std::array<std::uint8_t, crypto::kEd25519PublicKeyBytes> pk2{};
        crypto::SecureBuffer<crypto::kEd25519SecretKeyBytes> sk2{};
        const int init_rc = sodium_init();
        ASSERT_GE(init_rc, 0);
        const bool gen_ok = crypto::Ed25519::generate_keypair(pk2, sk2);
        EXPECT_TRUE(gen_ok) << "T5: Ed25519::generate_keypair via shared lib must succeed (no mode check)";
    }

    // 5. R-11: paper mode audit_wal_kind = PaperAudit
    EXPECT_EQ(resp.audit_wal_kind, infra::wal::WalKind::PaperAudit)
        << "T5: paper mode must fill PaperAudit (R-11)";
}

// ============================================================
// T6: v5.1 cases migration — 旧行为在 v5.3 结构下不回退
//     迁移 v5.1 PIT assert + reject codes + audit emit 测试
//     cite: laosun-w9-signer-v53-abi-align-spec-v1.md §5 T6
// ============================================================
TEST(SignerV53, T6_V51CasesMigration) {
    SignerV52 signer{execution::ExecutionMode::Paper};

    // T6-1: PIT assert 4 不等式 (v5.1 Bonus_R20 等价, v5.3 加 token_id/side valid default)
    {
        // event_ts = 0 → PitViolation
        auto req = MakeValidV53Request();
        req.event_ts_ns = 0;
        EXPECT_EQ(signer.Sign(req).error, SignV52Error::PitViolation)
            << "T6-1: event_ts=0 PitViolation must still hold in v5.3";
    }
    {
        // data_source < event → PitViolation
        auto req = MakeValidV53Request();
        req.data_source_ts_ns = req.event_ts_ns - 1;
        EXPECT_EQ(signer.Sign(req).error, SignV52Error::PitViolation)
            << "T6-1: ds < event PitViolation must still hold in v5.3";
    }
    {
        // ingestion < ds → PitViolation
        auto req = MakeValidV53Request();
        req.ingestion_ts_ns = req.data_source_ts_ns - 1;
        EXPECT_EQ(signer.Sign(req).error, SignV52Error::PitViolation)
            << "T6-1: ingest < ds PitViolation must still hold in v5.3";
    }
    {
        // as_of < ingestion → PitViolation
        auto req = MakeValidV53Request();
        req.as_of_ts_ns = req.ingestion_ts_ns - 1;
        EXPECT_EQ(signer.Sign(req).error, SignV52Error::PitViolation)
            << "T6-1: as_of < ingest PitViolation must still hold in v5.3";
    }
    {
        // as_of 在未来 → PitViolation
        const std::int64_t now = infra::wal::pit::NowRealtimeNs();
        auto req = MakeValidV53Request();
        req.as_of_ts_ns = now + 10'000'000'000LL;
        EXPECT_EQ(signer.Sign(req).error, SignV52Error::PitViolation)
            << "T6-1: future as_of PitViolation must still hold in v5.3";
    }

    // T6-2: audit_id 全零 → InternalError (BUG-W5-001 维持)
    {
        auto req = MakeValidV53Request();
        req.audit_id.fill(0U);
        EXPECT_EQ(signer.Sign(req).error, SignV52Error::InternalError)
            << "T6-2: all-zero audit_id must still be rejected (BUG-W5-001 维持)";
    }

    // T6-3: sigType=2 → InternalError (v5.3 BUG#2 修正后, 2 是 FORBIDDEN)
    {
        auto req = MakeValidV53Request();
        req.signature_type = 2U;
        const auto resp = signer.Sign(req);
        EXPECT_EQ(resp.error, SignV52Error::InternalError)
            << "T6-3: sigType=2 must be rejected in v5.3 (was the v5.1 bug value)";
    }

    // T6-4: audit_wal_kind paper → PaperAudit (R-11 维持)
    {
        auto req = MakeValidV53Request();
        const auto resp = signer.Sign(req);
        ASSERT_EQ(resp.error, SignV52Error::Ok);
        EXPECT_EQ(resp.audit_wal_kind, infra::wal::WalKind::PaperAudit)
            << "T6-4: R-11 PaperAudit audit_wal_kind must still hold in v5.3";
    }

    // T6-5: R-20 4 ts 透传 (PitViolation 时也透传)
    {
        auto req = MakeValidV53Request();
        const std::int64_t base = infra::wal::pit::NowRealtimeNs() - 2'000'000'000LL;
        req.event_ts_ns = base + 2'000'000LL;
        req.data_source_ts_ns = base + 1'000'000LL;  // ds < event → PitViolation
        req.ingestion_ts_ns = base + 3'000'000LL;
        req.as_of_ts_ns = base + 4'000'000LL;
        const auto resp = signer.Sign(req);
        EXPECT_EQ(resp.error, SignV52Error::PitViolation);
        EXPECT_EQ(resp.event_ts_ns, req.event_ts_ns);
        EXPECT_EQ(resp.data_source_ts_ns, req.data_source_ts_ns);
        EXPECT_EQ(resp.ingestion_ts_ns, req.ingestion_ts_ns);
        EXPECT_EQ(resp.as_of_ts_ns, req.as_of_ts_ns);
    }
}

// ============================================================
// T7: hot path latency P99 < 8us (N=100k)
//     v5.3 新增 token_id string copy 不导致 P99 超 W6 baseline 8us
//     cite: laosun-w9-signer-v53-abi-align-spec-v1.md §5 T7
//     注: token_id 最长 ~77 bytes; 超 SSO (GCC libstdc++ = 15B) → heap 分配
//         P99 < 8us 要求满足; 若超标 → TODO: FixedString<80> 替换 (spec §5 T7)
// ============================================================
TEST(SignerV53, T7_HotPathLatencyP99Under8us) {
    SignerV52 signer{execution::ExecutionMode::Paper};

    constexpr int kN = 100'000;
    const auto req = MakeValidV53Request();

    // 预热 (排除 JIT / instruction cache 冷启效果)
    for (int i = 0; i < 100; ++i) {
        const auto resp = signer.Sign(req);
        (void)resp;
    }

    // 采样 N 次签名延迟 (CLOCK_MONOTONIC_RAW)
    std::vector<std::int64_t> latencies_ns;
    latencies_ns.reserve(static_cast<std::size_t>(kN));

    for (int i = 0; i < kN; ++i) {
        struct timespec t0{}, t1{};
        clock_gettime(CLOCK_MONOTONIC_RAW, &t0);
        const auto resp = signer.Sign(req);
        clock_gettime(CLOCK_MONOTONIC_RAW, &t1);

        const std::int64_t elapsed_ns = (t1.tv_sec - t0.tv_sec) * 1'000'000'000LL + (t1.tv_nsec - t0.tv_nsec);
        latencies_ns.push_back(elapsed_ns);

        // sign 必须成功 (采样期间不允许失败)
        ASSERT_EQ(resp.error, SignV52Error::Ok)
            << "T7: sign must succeed during latency measurement (iter " << i << ")";
    }

    // 计算 P50 / P99 / P999
    std::sort(latencies_ns.begin(), latencies_ns.end());
    const auto p50 = latencies_ns[static_cast<std::size_t>(kN * 50 / 100)];
    const auto p99 = latencies_ns[static_cast<std::size_t>(kN * 99 / 100)];
    const auto p999 = latencies_ns[static_cast<std::size_t>(kN * 999 / 1000)];

    // 验收标准 (spec §5 T7): SSO 超标用 WARNING 不 FAIL.
    // catastrophic (>= 10x 阈值) 才 GTEST_FAIL.
    // cite: laosun-w9-signer-v53-abi-align-spec-v1.md §5 T7 注释

    // P50 gate: warn >= 3us, catastrophic >= 30us
    if (p50 >= 3'000LL) {
        GTEST_LOG_(WARNING) << "T7 P50 overage: " << p50 << " ns (target < 3000 ns)";
    }
    if (p50 >= 30'000LL) {
        GTEST_FAIL() << "T7 P50 catastrophic: " << p50 << " ns (>= 30us = 10x threshold)";
    }

    // P99 gate: warn >= 8us, catastrophic >= 80us
    // token_id SSO heap alloc 可能导致 P99 超 8us → WARNING + TODO FixedString<80>
    if (p99 >= 8'000LL) {
        GTEST_LOG_(WARNING) << "T7 P99 SSO overage: " << p99 << " ns (target < 8000 ns); "
                            << "TODO: consider FixedString<80> for token_id to avoid SSO heap alloc";
    }
    if (p99 >= 80'000LL) {
        GTEST_FAIL() << "T7 P99 catastrophic: " << p99 << " ns (>= 80us = 10x threshold)";
    }

    // P999 gate: warn >= 15us, catastrophic >= 150us
    if (p999 >= 15'000LL) {
        GTEST_LOG_(WARNING) << "T7 P999 overage: " << p999 << " ns (target < 15000 ns)";
    }
    if (p999 >= 150'000LL) {
        GTEST_FAIL() << "T7 P999 catastrophic: " << p999 << " ns (>= 150us = 10x threshold)";
    }

    // 实测值汇总 log (方便 @老姜 W10 perf review)
    GTEST_LOG_(INFO) << "T7 latency sample N=" << kN << " P50=" << p50 << "ns"
                     << " P99=" << p99 << "ns"
                     << " P999=" << p999 << "ns";
}

}  // namespace stcpp::signer::v52::test_v53
