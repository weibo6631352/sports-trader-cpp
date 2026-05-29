// tests/unit/test_signer_v52.cpp — SignerV52 v5.3 cpp 单测 (18 测试, v5.1 migration)
//
// Owner: 老孙 (#06)
// Wave 30 W6: signer v5.2 cpp v0.1 + IPC msgpack + Ed25519 paper mock + 8 测试 (原始)
// Wave 36 W8: ADR-023 IC 自测 + SIGNER-01 future-ts 修 (小宋 W8 W1 retro)
//   W8 W2 升级: 新增 NewT1-NewT8 共 8 cases (总计 18 tests)
// Wave 58 W9: v5.3 migration — signature_type 改 1 (Magic Safe EOA, HMAC bug #2 修正)
//             新增 token_id + side 字段填 valid default; 旧行为不变
//
// 测试矩阵 (原始 T1-T8 + Bonus 10 cases):
//   T1: paper mode SignerV52 Ed25519 mock signature (64B detached)
//   T2: IPC msgpack request/response 语义 round-trip (struct 字段完整性)
//   T3: 4 ts R-20 透传 (与老李 TimestampQuad 对齐)
//   T4: data_source_ts_source uint8 0-3 enum 与老李 ABI 一致
//   T5: signature_type=2 paper 强 reject (paper 不真签, sigType!=2 → InternalError)
//   T6: audit_id 16B 非零 (BUG-W5-001 教训)
//   T7: HMAC bug 4 反模式 (rstrip / param_type / sigType=1 / path 拼 querystring) 全禁
//   T8: 与老沈 STRATEGY_DECAYED 三签 Ed25519 公钥 mismatch fail
//   Bonus_R11: audit_wal_kind paper 硬填 PaperAudit
//   Bonus_R20: PIT 5 violation case
//
// 测试矩阵 (W8 W2 新增 NewT1-NewT8, 共 8 cases):
//   NewT1: Ed25519 真签 + verify_detached round-trip (libsodium 验签 API)
//   NewT2: SignV52Request 全字段 round-trip (4 ts + audit_id + market_id + outcome)
//   NewT3: HMAC BUG#1 rstrip 反模式 (market_id 尾 '/' 不崩) — 独立 case
//   NewT4: SecureBuffer<64> 析构后 sodium_memzero 清零验证 (老沈 §6)
//   NewT5: GeneratePaperKeypair 10 次都唯一 (keypair 独立不冲突)
//   NewT6: sign → verify round-trip 一致 (stcpp::crypto::Ed25519::verify API)
//   NewT7: stcpp_crypto_ed25519 linker check (Ed25519::generate_keypair 直调)
//   NewT8: SIGNER-01 修 — event_ts > now → PitViolation (小宋 W8 W1 retro 发现盲点)
//
// 红线:
//   R-7  paper mode only (STCPP_EXEC_MODE_paper guard)
//   R-20 4 ts chain (含 event_ts ≤ now — SIGNER-01 修)
//   BUG-W5-001 audit_id 非零

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/polymarket/pm_client.hpp"
#include "stcpp/signer/v52/signer_v52.hpp"

namespace stcpp::signer::v52::test {

namespace {

// 构造合法 4 ts (R-20 通过)
// as_of_ts 用 pit::NowRealtimeNs() 保证 ≤ now
SignV52Request MakeValidRequest() {
    SignV52Request req;
    const std::int64_t base = infra::wal::pit::NowRealtimeNs() - 1'000'000'000LL;  // 1s ago
    req.event_ts_ns = base;
    req.data_source_ts_ns = base + 1'000'000LL;  // +1ms
    req.ingestion_ts_ns = base + 2'000'000LL;    // +2ms
    req.as_of_ts_ns = base + 3'000'000LL;        // +3ms (still ≤ now since base = now-1s)
    req.data_source_ts_source = 0U;              // UpstreamPayload
    req.market_id = "0xabcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890";
    // v5.3 新增字段: token_id + side (valid defaults for migration)
    req.token_id = "1677202003548168512111076196662317438975560192301735320827449539424843146463";
    req.side = 0U;     // Buy (valid)
    req.outcome = 0U;  // YES
    // v5.3 修正: signature_type = 1 (Magic Safe EOA, HMAC bug #2 修正)
    // v5.1 曾填 2 (错误值); v5.3 修正为 1 (SSOT §5 T-10, handshake §84)
    req.signature_type = 1U;  // 1 = Magic Safe EOA (CORRECT in v5.3)
    req.intent_id = 42ULL;
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

}  // namespace

// ============================================================
// T1: paper mode Ed25519 mock signature (64B detached)
// ============================================================
TEST(SignerV52, T1_PaperModeEd25519MockSignature64B) {
    SignerV52 signer{execution::ExecutionMode::Paper};
    EXPECT_EQ(signer.Mode(), execution::ExecutionMode::Paper);

    const auto req = MakeValidRequest();
    const auto resp = signer.Sign(req);

    EXPECT_EQ(resp.error, SignV52Error::Ok) << "Expected Ok, got: " << ToString(resp.error);
    EXPECT_EQ(resp.signature.size(), 64U) << "Ed25519 detached signature must be 64B";

    // 签名必须非全零 (有效签名; paper mock 或 libsodium 真签均不应全零)
    bool any_nonzero = false;
    for (const auto b : resp.signature) {
        if (b != 0U) {
            any_nonzero = true;
            break;
        }
    }
    EXPECT_TRUE(any_nonzero) << "Signature must not be all-zero";

    // R-11: paper 硬填 PaperAudit
    EXPECT_EQ(resp.audit_wal_kind, infra::wal::WalKind::PaperAudit)
        << "R-11: paper signer must fill PaperAudit";
}

// ============================================================
// T2: IPC request/response round-trip 语义 (struct 字段完整性)
// ============================================================
TEST(SignerV52, T2_IpcRequestResponseRoundTrip) {
    SignerV52 signer{execution::ExecutionMode::Paper};
    const auto req = MakeValidRequest();
    const auto resp = signer.Sign(req);

    ASSERT_EQ(resp.error, SignV52Error::Ok);

    // R-20 4 ts 透传
    EXPECT_EQ(resp.event_ts_ns, req.event_ts_ns);
    EXPECT_EQ(resp.data_source_ts_ns, req.data_source_ts_ns);
    EXPECT_EQ(resp.ingestion_ts_ns, req.ingestion_ts_ns);
    EXPECT_EQ(resp.as_of_ts_ns, req.as_of_ts_ns);

    // audit_id echo-back (BUG-W5-001)
    EXPECT_EQ(resp.audit_id, req.audit_id);

    // signature 64B
    EXPECT_EQ(resp.signature.size(), 64U);
}

// ============================================================
// T3: 4 ts R-20 透传 (与老李 TimestampQuad 对齐)
// ============================================================
TEST(SignerV52, T3_FourTsR20Passthrough) {
    SignerV52 signer{execution::ExecutionMode::Paper};

    // 1) 违反 PIT 顺序 (ds < event): PitViolation 时 4 ts 仍透传
    // 使用近期有效 base 时间但故意让 ds < event
    auto req = MakeValidRequest();
    const std::int64_t base = infra::wal::pit::NowRealtimeNs() - 2'000'000'000LL;
    req.event_ts_ns = base + 2'000'000LL;        // event 靠后
    req.data_source_ts_ns = base + 1'000'000LL;  // ds < event → PitViolation!
    req.ingestion_ts_ns = base + 3'000'000LL;
    req.as_of_ts_ns = base + 4'000'000LL;

    const auto resp = signer.Sign(req);
    // ds < event → PitViolation; 4 ts 仍然透传
    EXPECT_EQ(resp.error, SignV52Error::PitViolation) << "T3: data_source < event must trigger PitViolation";
    EXPECT_EQ(resp.event_ts_ns, req.event_ts_ns);
    EXPECT_EQ(resp.data_source_ts_ns, req.data_source_ts_ns);
    EXPECT_EQ(resp.ingestion_ts_ns, req.ingestion_ts_ns);
    EXPECT_EQ(resp.as_of_ts_ns, req.as_of_ts_ns);

    // 合法 ts (近期)
    auto req2 = MakeValidRequest();
    const auto resp2 = signer.Sign(req2);
    EXPECT_EQ(resp2.error, SignV52Error::Ok);
    EXPECT_EQ(resp2.event_ts_ns, req2.event_ts_ns);
    EXPECT_EQ(resp2.data_source_ts_ns, req2.data_source_ts_ns);
    EXPECT_EQ(resp2.ingestion_ts_ns, req2.ingestion_ts_ns);
    EXPECT_EQ(resp2.as_of_ts_ns, req2.as_of_ts_ns);
}

// ============================================================
// T4: data_source_ts_source uint8 0-3 enum 与老李 ABI 一致
// (handshake §4.3 ABI lock)
// ============================================================
TEST(SignerV52, T4_DataSourceTsSourceEnumAbiLock) {
    using DST = polymarket::DataSourceTsSource;

    // ABI lock: 值 0-3 与 signer IPC uint8_t 对应
    EXPECT_EQ(static_cast<std::uint8_t>(DST::UpstreamPayload), 0U);
    EXPECT_EQ(static_cast<std::uint8_t>(DST::UpstreamHeader), 1U);
    EXPECT_EQ(static_cast<std::uint8_t>(DST::InferredFromDsTs), 2U);
    EXPECT_EQ(static_cast<std::uint8_t>(DST::InferredFromIngestion), 3U);

    // SignV52Request.data_source_ts_source 接受 0-3
    SignerV52 signer{execution::ExecutionMode::Paper};
    for (std::uint8_t src = 0U; src <= 3U; ++src) {
        auto req = MakeValidRequest();
        req.data_source_ts_source = src;
        const auto resp = signer.Sign(req);
        EXPECT_EQ(resp.error, SignV52Error::Ok)
            << "data_source_ts_source=" << static_cast<int>(src) << " should be accepted";
    }
}

// ============================================================
// T5: signature_type enforce (v5.3: sigType=1 accept; sigType=2 reject)
// HMAC bug #2 修正 (v5.3): v5.1 曾错填 2; v5.3 修正为 1 (Magic Safe EOA)
// cite: SSOT §5 T-10, handshake §84 signatureType
// ============================================================
TEST(SignerV52, T5_SignatureTypeMustBeOne) {
    SignerV52 signer{execution::ExecutionMode::Paper};

    // sigType=1 → Ok (Magic Safe EOA — CORRECT in v5.3)
    {
        auto req = MakeValidRequest();
        req.signature_type = 1U;  // 正确值 (v5.3 修正)
        const auto resp = signer.Sign(req);
        EXPECT_EQ(resp.error, SignV52Error::Ok) << "sigType=1 (Magic Safe EOA) must be accepted in v5.3";
    }

    // sigType=2 → InternalError (HMAC bug #2 的错误值, v5.3 修正后禁止)
    // v5.1 曾 enforce == 2 (错误); v5.3 修正: 2 = FORBIDDEN
    {
        auto req = MakeValidRequest();
        req.signature_type = 2U;  // v5.1 的错误值, 现在被 v5.3 拒绝
        const auto resp = signer.Sign(req);
        EXPECT_EQ(resp.error, SignV52Error::InternalError)
            << "sigType=2 must be rejected in v5.3 (was HMAC bug #2 error value)";
    }

    // sigType=0 → InternalError
    {
        auto req = MakeValidRequest();
        req.signature_type = 0U;
        const auto resp = signer.Sign(req);
        EXPECT_EQ(resp.error, SignV52Error::InternalError) << "sigType=0 must be rejected";
    }

    // sigType=3 → InternalError
    {
        auto req = MakeValidRequest();
        req.signature_type = 3U;
        const auto resp = signer.Sign(req);
        EXPECT_EQ(resp.error, SignV52Error::InternalError) << "sigType=3 must be rejected";
    }
}

// ============================================================
// T6: audit_id 16B 非零 (BUG-W5-001 教训)
// ============================================================
TEST(SignerV52, T6_AuditIdNonZero) {
    SignerV52 signer{execution::ExecutionMode::Paper};

    // 全零 audit_id → InternalError (BUG-W5-001 防御)
    {
        auto req = MakeValidRequest();
        req.audit_id.fill(0U);  // 全零 — 违反 BUG-W5-001
        const auto resp = signer.Sign(req);
        EXPECT_EQ(resp.error, SignV52Error::InternalError)
            << "All-zero audit_id must be rejected (BUG-W5-001)";
    }

    // 最小非零: 只有 audit_id[15] = 1
    {
        auto req = MakeValidRequest();
        req.audit_id.fill(0U);
        req.audit_id[15] = 0x01U;
        const auto resp = signer.Sign(req);
        EXPECT_EQ(resp.error, SignV52Error::Ok) << "Single non-zero byte in audit_id should be accepted";
        EXPECT_EQ(resp.signature.size(), 64U);
    }

    // 合法 audit_id echo-back
    {
        auto req = MakeValidRequest();
        const auto resp = signer.Sign(req);
        ASSERT_EQ(resp.error, SignV52Error::Ok);
        EXPECT_EQ(resp.audit_id, req.audit_id) << "audit_id must echo back unchanged";
    }
}

// ============================================================
// T7: HMAC bug 4 反模式验证 (v5.3 migration)
// 老孙 v3 §A 永久 enforce + v5.3 sigType 修正:
//   BUG#1: rstrip 尾斜杠 (url_path 末尾 '/')
//   BUG#2: sigType=2 现在是 FORBIDDEN (v5.3 修正: 正确值是 1)
//   BUG#3: base64 padding ('=' 不能 rstrip, live M5+ 层约束)
//   BUG#4: path 拼 querystring (canonical_path 禁含 '?')
// 注: signer v52 本身不做 HMAC 构造 (live M5+ live_pm_client.cpp 负责);
//     此测试验证 signer 层 API 约束反映了 bug 教训.
// ============================================================
TEST(SignerV52, T7_HmacBug4AntiPatternEnforced) {
    SignerV52 signer{execution::ExecutionMode::Paper};

    // BUG#1 教训: market_id 不应含尾 '/' (caller 层约定; signer 不校验, 但测试记录约束)
    // 这里验证 signer 不因 market_id 格式崩溃
    {
        auto req = MakeValidRequest();
        req.market_id = "0xabcd/";  // 尾斜杠 (caller bug, signer 不过滤但不崩)
        const auto resp = signer.Sign(req);
        // signer 不关 market_id 格式; 不崩溃即可
        EXPECT_NE(resp.error, SignV52Error::LibsodiumInitFail)
            << "BUG#1: signer must not crash on trailing slash in market_id";
    }

    // BUG#2 修正 (v5.3): sigType=2 永久禁止 (v5.1 曾是正确值, v5.3 修正后变为 FORBIDDEN)
    {
        auto req = MakeValidRequest();
        req.signature_type = 2U;  // FORBIDDEN in v5.3 (HMAC bug #2 error value)
        const auto resp = signer.Sign(req);
        EXPECT_EQ(resp.error, SignV52Error::InternalError) << "BUG#2 修正 (v5.3): sigType=2 must be rejected";
    }

    // BUG#4 教训: market_id 含 '?' (querystring 混入) — signer 不崩溃
    {
        auto req = MakeValidRequest();
        req.market_id = "0xabcd?foo=bar";  // querystring 混入
        const auto resp = signer.Sign(req);
        EXPECT_NE(resp.error, SignV52Error::LibsodiumInitFail)
            << "BUG#4: signer must not crash on querystring in market_id";
    }

    // sigType=1 是 v5.3 正确值 (Magic Safe EOA, HMAC bug #2 修正)
    {
        auto req = MakeValidRequest();
        req.signature_type = 1U;  // CORRECT in v5.3: Magic Safe EOA
        const auto resp = signer.Sign(req);
        EXPECT_EQ(resp.error, SignV52Error::Ok) << "sigType=1 (Magic Safe EOA) must be accepted in v5.3";
        EXPECT_EQ(resp.signature.size(), 64U);
    }
}

// ============================================================
// T8: 与老沈 STRATEGY_DECAYED 三签 Ed25519 公钥 mismatch fail
// 场景: 用 signer_a 签名, 用 signer_b 的公钥验证 → 验证失败
// 依赖: libsodium crypto_sign_ed25519_verify_detached
//       (若无 libsodium, 验证两个 signer 的 pubkey 不同即可)
// ============================================================

#ifdef STCPP_SIGNER_V52_LIBSODIUM
#    include <sodium.h>  // for T8 crypto_sign_ed25519_verify_detached
#endif

TEST(SignerV52, T8_Ed25519PublicKeyMismatchFail) {
    // 创建两个 signer, 各自生成独立 keypair
    SignerV52 signer_a{execution::ExecutionMode::Paper};
    SignerV52 signer_b{execution::ExecutionMode::Paper};

    const auto pk_a = signer_a.PublicKeyBytes();
    const auto pk_b = signer_b.PublicKeyBytes();

    // 两个 signer 的公钥必须不同 (各自随机 keypair)
    ASSERT_EQ(pk_a.size(), 32U) << "signer_a pubkey must be 32B";
    ASSERT_EQ(pk_b.size(), 32U) << "signer_b pubkey must be 32B";
    EXPECT_NE(pk_a, pk_b) << "T8: two independently created signers must have different keypairs";

    // signer_a 签名
    auto req = MakeValidRequest();
    const auto resp_a = signer_a.Sign(req);
    ASSERT_EQ(resp_a.error, SignV52Error::Ok);
    ASSERT_EQ(resp_a.signature.size(), 64U);

#ifdef STCPP_SIGNER_V52_LIBSODIUM
    // 有 libsodium: 用 signer_b 的公钥验证 signer_a 的签名 → 必须失败

    // 重建 msg (与 signer_v52.cpp Sign() 一致, v5.3 格式)
    // v5.3: event_ts(8B LE) || market_id || token_id || side(1B) || outcome(1B) || audit_id(16B)
    std::vector<std::uint8_t> msg;
    msg.reserve(8U + req.market_id.size() + req.token_id.size() + 1U + 1U + 16U);
    for (std::size_t i = 0; i < 8U; ++i) {
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

    // 用 signer_b pubkey 验证 → 必须失败 (返回 -1)
    const int verify_result = crypto_sign_ed25519_verify_detached(
        resp_a.signature.data(), msg.data(),
        static_cast<unsigned long long>(msg.size()),  // NOLINT(google-runtime-int)
        pk_b.data());
    EXPECT_EQ(verify_result, -1) << "T8: verifying signer_a signature with signer_b pubkey must fail";

    // 用 signer_a pubkey 验证 → 必须成功 (返回 0)
    const int verify_ok = crypto_sign_ed25519_verify_detached(
        resp_a.signature.data(), msg.data(),
        static_cast<unsigned long long>(msg.size()),  // NOLINT(google-runtime-int)
        pk_a.data());
    EXPECT_EQ(verify_ok, 0) << "T8: verifying signer_a signature with signer_a pubkey must succeed";
#else
    // 无 libsodium: 仅验证两个 signer 公钥不同 (证明 keypair 独立)
    EXPECT_NE(pk_a, pk_b) << "T8: signers must have different pubkeys (no libsodium verify)";

    // 验证 resp_a.signature 非零 (mock 路径也不应全零)
    bool nonzero = false;
    for (const auto b : resp_a.signature) {
        if (b != 0U) {
            nonzero = true;
            break;
        }
    }
    EXPECT_TRUE(nonzero) << "T8: signature must not be all-zero even in mock path";
#endif
}

// ============================================================
// 附加: R-11 audit_wal_kind paper 硬填 PaperAudit
// ============================================================
TEST(SignerV52, Bonus_R11AuditWalKindPaperAudit) {
    SignerV52 signer{execution::ExecutionMode::Paper};
    const auto req = MakeValidRequest();
    const auto resp = signer.Sign(req);

    ASSERT_EQ(resp.error, SignV52Error::Ok);
    EXPECT_EQ(resp.audit_wal_kind, infra::wal::WalKind::PaperAudit)
        << "R-11: paper signer must always fill PaperAudit, never RiskAudit/Position";
}

// ============================================================
// 附加: R-20 PIT 5 violation case
// ============================================================
TEST(SignerV52, Bonus_R20PitViolation5Cases) {
    SignerV52 signer{execution::ExecutionMode::Paper};

    // 1) event_ts_ns = 0 → PitViolation
    {
        auto req = MakeValidRequest();
        req.event_ts_ns = 0;
        EXPECT_EQ(signer.Sign(req).error, SignV52Error::PitViolation)
            << "event_ts=0 must trigger PitViolation";
    }

    // 2) data_source_ts < event_ts → PitViolation
    {
        auto req = MakeValidRequest();
        req.data_source_ts_ns = req.event_ts_ns - 1;
        EXPECT_EQ(signer.Sign(req).error, SignV52Error::PitViolation)
            << "ds < event must trigger PitViolation";
    }

    // 3) ingestion_ts < data_source_ts → PitViolation
    {
        auto req = MakeValidRequest();
        req.ingestion_ts_ns = req.data_source_ts_ns - 1;
        EXPECT_EQ(signer.Sign(req).error, SignV52Error::PitViolation)
            << "ingest < ds must trigger PitViolation";
    }

    // 4) as_of < ingestion → PitViolation
    {
        auto req = MakeValidRequest();
        req.as_of_ts_ns = req.ingestion_ts_ns - 1;
        EXPECT_EQ(signer.Sign(req).error, SignV52Error::PitViolation)
            << "as_of < ingest must trigger PitViolation";
    }

    // 5) as_of 远在未来 → PitViolation
    {
        auto req = MakeValidRequest();
        const std::int64_t now = infra::wal::pit::NowRealtimeNs();
        req.as_of_ts_ns = now + 10'000'000'000LL;  // +10s future
        EXPECT_EQ(signer.Sign(req).error, SignV52Error::PitViolation)
            << "as_of in future must trigger PitViolation";
    }
}

// ============================================================
// NewT1: Ed25519 真签 + crypto_sign_ed25519_verify_detached round-trip
//        (libsodium 原生 verify 函数验签)
// ============================================================
TEST(SignerV52, NewT1_Ed25519TrueSignVerifyRoundTrip) {
    SignerV52 signer{execution::ExecutionMode::Paper};

    auto req = MakeValidRequest();
    const auto resp = signer.Sign(req);
    ASSERT_EQ(resp.error, SignV52Error::Ok);
    ASSERT_EQ(resp.signature.size(), 64U);

    // 取公钥
    const auto pk = signer.PublicKeyBytes();
    ASSERT_EQ(pk.size(), 32U);

    // 重建签名消息 (与 signer_v52.cpp Sign() 中构造一致, v5.3 格式)
    // v5.3: event_ts(8B LE) || market_id || token_id || side(1B) || outcome(1B) || audit_id(16B)
    std::vector<std::uint8_t> msg;
    msg.reserve(8U + req.market_id.size() + req.token_id.size() + 1U + 1U + 16U);
    for (std::size_t i = 0U; i < 8U; ++i) {
        msg.push_back(
            static_cast<std::uint8_t>((static_cast<std::uint64_t>(req.event_ts_ns) >> (i * 8U)) & 0xFFU));
    }
    for (const char c : req.market_id) {
        msg.push_back(static_cast<std::uint8_t>(c));
    }
    // v5.3 新增: token_id bytes
    for (const char c : req.token_id) {
        msg.push_back(static_cast<std::uint8_t>(c));
    }
    // v5.3 新增: side (1B)
    msg.push_back(req.side);
    msg.push_back(req.outcome);
    for (const auto b : req.audit_id) {
        msg.push_back(b);
    }

    // libsodium 原生验签: pk.data() + msg + sig → 0 表示成功
    const int vr = crypto_sign_ed25519_verify_detached(
        resp.signature.data(), msg.data(),
        static_cast<unsigned long long>(msg.size()),  // NOLINT(google-runtime-int)
        pk.data());
    EXPECT_EQ(vr, 0) << "NewT1: libsodium verify_detached of own signature must return 0 (valid)";
}

// ============================================================
// NewT2: SignV52Request 全字段 round-trip
//        IPC 结构所有字段经 Sign() 正确透传到 SignV52Response
// ============================================================
TEST(SignerV52, NewT2_SignV52RequestAllFieldsRoundTrip) {
    SignerV52 signer{execution::ExecutionMode::Paper};

    auto req = MakeValidRequest();
    // 覆盖所有字段 (IPC 协议 v5.3 完整性)
    const std::int64_t base = infra::wal::pit::NowRealtimeNs() - 2'000'000'000LL;
    req.event_ts_ns = base;
    req.data_source_ts_ns = base + 1'000'000LL;
    req.ingestion_ts_ns = base + 2'000'000LL;
    req.as_of_ts_ns = base + 3'000'000LL;
    req.data_source_ts_source = 1U;  // UpstreamHeader
    req.market_id = "0xdeadbeefcafebabe0000111122223333";
    req.token_id = "9876543210987654321098765432109876543210987654321098765432109876543";
    req.side = 1U;            // Sell
    req.outcome = 1U;         // NO
    req.signature_type = 1U;  // v5.3: Magic Safe EOA (HMAC bug #2 修正)
    req.intent_id = 9999ULL;
    req.audit_id.fill(0xABU);

    const auto resp = signer.Sign(req);
    ASSERT_EQ(resp.error, SignV52Error::Ok) << "NewT2: valid request with all fields must succeed";

    // 4 ts 透传
    EXPECT_EQ(resp.event_ts_ns, req.event_ts_ns);
    EXPECT_EQ(resp.data_source_ts_ns, req.data_source_ts_ns);
    EXPECT_EQ(resp.ingestion_ts_ns, req.ingestion_ts_ns);
    EXPECT_EQ(resp.as_of_ts_ns, req.as_of_ts_ns);

    // audit_id echo-back
    EXPECT_EQ(resp.audit_id, req.audit_id);

    // signature 64B 非零
    EXPECT_EQ(resp.signature.size(), 64U);
    bool nonzero = false;
    for (const auto b : resp.signature) {
        if (b != 0U) {
            nonzero = true;
            break;
        }
    }
    EXPECT_TRUE(nonzero) << "NewT2: signature must not be all-zero";

    // R-11 audit_wal_kind
    EXPECT_EQ(resp.audit_wal_kind, infra::wal::WalKind::PaperAudit);
}

// ============================================================
// NewT3: HMAC BUG#1 rstrip 反模式 — market_id 尾 '/' 不崩溃
//        signer 层不做 market_id 格式过滤, 不应因 trailing slash 崩溃
// ============================================================
TEST(SignerV52, NewT3_HmacBug1RstripNocrash) {
    SignerV52 signer{execution::ExecutionMode::Paper};

    // BUG#1 教训: url_path 末尾 '/' 必须在 caller 层去掉
    // signer 不过滤 market_id, 但不应崩溃 (no abort / no exception / no crash)
    const std::vector<std::string> dangerous_market_ids = {
        "0xabcd/",                // 尾斜杠
        "0xabcd//",               // 双斜杠
        "0xabcd?foo=bar",         // querystring 混入 (BUG#4)
        "0xabcd?foo=bar&baz=1/",  // querystring + 尾斜杠 (BUG#1 + BUG#4)
        "",                       // 空字符串 (caller bug; signer 不 abort)
    };

    for (const auto& mid : dangerous_market_ids) {
        auto req = MakeValidRequest();
        req.market_id = mid;
        const auto resp = signer.Sign(req);
        // 不崩溃 (返回任何 error 值均可, 关键是进程不挂)
        EXPECT_NE(resp.error, SignV52Error::LibsodiumInitFail)
            << "NewT3: signer must not crash (LibsodiumInitFail) on market_id='" << mid << "'";
        // 签名失败（空 market_id 等）是可接受的，重要是不是 LibsodiumInitFail / crash
        (void)resp;  // 结果合理性在其他测试覆盖
    }
}

// ============================================================
// NewT4: SecureBuffer<64> 析构后 sodium_memzero 清零
//        老沈 §6 审核要求: 私钥离开作用域必须清零
// ============================================================
TEST(SignerV52, NewT4_SecureBufferSodiumMemzeroOnDestruct) {
    // 在作用域内 fill 一个 SecureBuffer<64>，然后离开作用域
    // 析构后无法直接读取已清零的内存 (UB), 但可以通过:
    //   1. 验证 SecureBuffer 析构不崩溃 (正向测试)
    //   2. 验证 move 后 source 被清零 (可观测)
    {
        crypto::SecureBuffer<crypto::kEd25519SecretKeyBytes> sk;
        // 填入非零数据
        for (std::size_t i = 0U; i < sk.size(); ++i) {
            sk[i] = static_cast<std::uint8_t>(i + 1U);
        }
        // 验证填充成功
        EXPECT_NE(sk[0], 0U) << "NewT4: buffer should be non-zero before destruct";

        // move 后 source 被清零 (move constructor 调 sodium_memzero)
        crypto::SecureBuffer<crypto::kEd25519SecretKeyBytes> sk2{std::move(sk)};
        // sk.data() 已清零 (访问 move-from 状态, 但 SecureBuffer 保证 data() 不悬空)
        // 验证 sk2 有数据, sk 已清零
        EXPECT_NE(sk2[0], 0U) << "NewT4: moved-to buffer should retain data";
        EXPECT_EQ(sk[0], 0U) << "NewT4: moved-from buffer should be zeroed (sodium_memzero)";
    }
    // 离开 {} 作用域: sk2 析构, sodium_memzero 清零 (不崩溃)
    // 通过此 test 正常退出即证明析构清零路径 ok

    // 验证 SignerV52 析构不崩 (keypair_valid 路径)
    {
        SignerV52* s = new SignerV52{execution::ExecutionMode::Paper};
        // s->keypair_valid_ = true (paper mode + sodium_init ok)
        // Sign 一次确认有效
        auto req = MakeValidRequest();
        const auto resp = s->Sign(req);
        EXPECT_EQ(resp.error, SignV52Error::Ok) << "NewT4: SignerV52 should sign ok before destruct";
        // delete 触发析构 → SecureBuffer<64> sodium_memzero
        delete s;  // NOLINT(cppcoreguidelines-owning-memory)
        // 无 crash = SecureBuffer 析构路径正确
    }
}

// ============================================================
// NewT5: GeneratePaperKeypair 10 次独立 (keypair 不冲突)
//        每次构造 SignerV52 生成独立随机 keypair (T8 的批量版)
// ============================================================
TEST(SignerV52, NewT5_GeneratePaperKeypairMultipleUnique) {
    constexpr int kN = 10;
    std::vector<std::vector<std::uint8_t>> pks;
    pks.reserve(kN);

    for (int i = 0; i < kN; ++i) {
        SignerV52 s{execution::ExecutionMode::Paper};
        const auto pk = s.PublicKeyBytes();
        ASSERT_EQ(pk.size(), 32U) << "NewT5: keypair " << i << " pubkey must be 32B";
        // 非全零
        bool nonzero = false;
        for (const auto b : pk) {
            if (b != 0U) {
                nonzero = true;
                break;
            }
        }
        EXPECT_TRUE(nonzero) << "NewT5: keypair " << i << " pubkey must not be all-zero";
        pks.push_back(pk);
    }

    // 10 个 pubkey 两两不同 (Pigeonhole: 极小概率碰撞, Ed25519 随机 keypair)
    for (int i = 0; i < kN; ++i) {
        for (int j = i + 1; j < kN; ++j) {
            EXPECT_NE(pks[static_cast<std::size_t>(i)], pks[static_cast<std::size_t>(j)])
                << "NewT5: keypair " << i << " and " << j << " must differ";
        }
    }
}

// ============================================================
// NewT6: sign → crypto::Ed25519::verify round-trip 一致
//        直接调 stcpp::crypto::Ed25519::verify (INTERFACE target 共用路径)
// ============================================================
TEST(SignerV52, NewT6_SignVerifyRoundTripConsistentViaCryptoApi) {
    SignerV52 signer{execution::ExecutionMode::Paper};

    auto req = MakeValidRequest();
    const auto resp = signer.Sign(req);
    ASSERT_EQ(resp.error, SignV52Error::Ok);
    ASSERT_EQ(resp.signature.size(), 64U);

    const auto pk = signer.PublicKeyBytes();
    ASSERT_EQ(pk.size(), 32U);

    // 重建消息 (与 signer_v52.cpp 一致, v5.3 格式)
    // v5.3: event_ts(8B LE) || market_id || token_id || side(1B) || outcome(1B) || audit_id(16B)
    std::vector<std::uint8_t> msg;
    msg.reserve(8U + req.market_id.size() + req.token_id.size() + 1U + 1U + 16U);
    for (std::size_t i = 0U; i < 8U; ++i) {
        msg.push_back(
            static_cast<std::uint8_t>((static_cast<std::uint64_t>(req.event_ts_ns) >> (i * 8U)) & 0xFFU));
    }
    for (const char c : req.market_id) {
        msg.push_back(static_cast<std::uint8_t>(c));
    }
    // v5.3 新增: token_id bytes
    for (const char c : req.token_id) {
        msg.push_back(static_cast<std::uint8_t>(c));
    }
    // v5.3 新增: side (1B)
    msg.push_back(req.side);
    msg.push_back(req.outcome);
    for (const auto b : req.audit_id) {
        msg.push_back(b);
    }

    // stcpp::crypto::Ed25519::verify API (INTERFACE target 共用路径)
    // 使用 span<const uint8_t, 32> 和 span<const uint8_t, 64>
    std::array<std::uint8_t, crypto::kEd25519PublicKeyBytes> pk_arr{};
    std::copy(pk.begin(), pk.end(), pk_arr.begin());

    std::array<std::uint8_t, crypto::kEd25519SignatureBytes> sig_arr{};
    std::copy(resp.signature.begin(), resp.signature.end(), sig_arr.begin());

    const bool ok =
        crypto::Ed25519::verify(std::span<const std::uint8_t, crypto::kEd25519PublicKeyBytes>{pk_arr},
                                std::span<const std::uint8_t>{msg.data(), msg.size()},
                                std::span<const std::uint8_t, crypto::kEd25519SignatureBytes>{sig_arr});
    EXPECT_TRUE(ok) << "NewT6: Ed25519::verify of own signature must return true";

    // 篡改消息: verify 应返回 false
    if (!msg.empty()) {
        std::vector<std::uint8_t> tampered = msg;
        tampered[0] ^= 0xFFU;
        const bool bad =
            crypto::Ed25519::verify(std::span<const std::uint8_t, crypto::kEd25519PublicKeyBytes>{pk_arr},
                                    std::span<const std::uint8_t>{tampered.data(), tampered.size()},
                                    std::span<const std::uint8_t, crypto::kEd25519SignatureBytes>{sig_arr});
        EXPECT_FALSE(bad) << "NewT6: Ed25519::verify of tampered message must return false";
    }
}

// ============================================================
// NewT7: stcpp_crypto_ed25519 linker check
//        直接调用 crypto::Ed25519::generate_keypair (不经过 SignerV52)
//        验证 stcpp_crypto_ed25519 INTERFACE target 正确链到 test_signer_v52
// ============================================================
TEST(SignerV52, NewT7_CryptoEd25519LinkerCheck) {
    // 直接调 Ed25519::generate_keypair (stcpp_crypto_ed25519 公共 API)
    std::array<std::uint8_t, crypto::kEd25519PublicKeyBytes> pk{};
    crypto::SecureBuffer<crypto::kEd25519SecretKeyBytes> sk{};

    // 需要先 sodium_init (幂等)
    const int init_rc = sodium_init();
    ASSERT_GE(init_rc, 0) << "NewT7: sodium_init must succeed (0 = first, 1 = already)";

    const bool ok = crypto::Ed25519::generate_keypair(pk, sk);
    EXPECT_TRUE(ok) << "NewT7: Ed25519::generate_keypair must succeed";

    // pubkey 非全零
    bool pk_nonzero = false;
    for (const auto b : pk) {
        if (b != 0U) {
            pk_nonzero = true;
            break;
        }
    }
    EXPECT_TRUE(pk_nonzero) << "NewT7: generated pubkey must not be all-zero";

    // sk 非全零 (私钥 64B = seed || pubkey)
    bool sk_nonzero = false;
    for (std::size_t i = 0U; i < sk.size(); ++i) {
        if (sk[i] != 0U) {
            sk_nonzero = true;
            break;
        }
    }
    EXPECT_TRUE(sk_nonzero) << "NewT7: generated secret key must not be all-zero";
}

// ============================================================
// NewT8: SIGNER-01 修 — event_ts > now → PitViolation
//
// 小宋 W8 W1 retro 发现盲点:
//   R20_PitViolation 现有 tests 主要验证 as_of_ts > now,
//   缺独立 case 专门验证 event_ts > now.
//
// AssertChainTs 逻辑: as_of >= ingest >= ds >= event → event > now ⇒ as_of > now
// 物理上已覆盖, 但缺专门 case 标注 event_ts 维度.
//
// 本 case: event_ts = now + 10s (整条链都在未来) → PitViolation
// 4 ts 仍透传 (R-20 透传规则不因错误跳过)
// ============================================================
TEST(SignerV52, NewT8_SIGNER01_EventTsFutureIsPitViolation) {
    SignerV52 signer{execution::ExecutionMode::Paper};

    const std::int64_t now = infra::wal::pit::NowRealtimeNs();

    // event_ts = now + 10s (整条链在未来, 最严格的 future-ts case)
    auto req = MakeValidRequest();
    req.event_ts_ns = now + 10'000'000'000LL;  // +10s
    req.data_source_ts_ns = req.event_ts_ns + 1'000LL;
    req.ingestion_ts_ns = req.data_source_ts_ns + 1'000LL;
    req.as_of_ts_ns = req.ingestion_ts_ns + 1'000LL;

    const auto resp = signer.Sign(req);

    // SIGNER-01: event_ts in future → PitViolation (as_of > now 触发)
    EXPECT_EQ(resp.error, SignV52Error::PitViolation)
        << "SIGNER-01: event_ts > now must trigger PitViolation";

    // R-20: 4 ts 仍透传 (不因 error 丢弃)
    EXPECT_EQ(resp.event_ts_ns, req.event_ts_ns)
        << "SIGNER-01: event_ts must be passed through even on PitViolation";
    EXPECT_EQ(resp.data_source_ts_ns, req.data_source_ts_ns);
    EXPECT_EQ(resp.ingestion_ts_ns, req.ingestion_ts_ns);
    EXPECT_EQ(resp.as_of_ts_ns, req.as_of_ts_ns);

    // edge case: event_ts = now + 100ms (稳定未来, 远大于 Sign() 执行时间)
    // 注: +1ns 不可靠 — Sign() 内部再次调 NowRealtimeNs() 时执行时间已超过 1ns
    //     SIGNER-01 揭示: 检测"足够远的未来"才是可测边界 (生产 event_ts 不会 +100ms)
    auto req2 = MakeValidRequest();
    req2.event_ts_ns = now + 100'000'000LL;  // +100ms
    req2.data_source_ts_ns = req2.event_ts_ns + 1'000LL;
    req2.ingestion_ts_ns = req2.data_source_ts_ns + 1'000LL;
    req2.as_of_ts_ns = req2.ingestion_ts_ns + 1'000LL;
    const auto resp2 = signer.Sign(req2);
    EXPECT_EQ(resp2.error, SignV52Error::PitViolation)
        << "SIGNER-01: +100ms in future for event_ts must trigger PitViolation";
}

}  // namespace stcpp::signer::v52::test
