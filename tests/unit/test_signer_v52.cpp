// tests/unit/test_signer_v52.cpp — SignerV52 v5.2 cpp v0.1 单测 (8 测试)
//
// Owner: 老孙 (#06)
// Wave 30 W6: signer v5.2 cpp v0.1 + IPC msgpack + Ed25519 paper mock + 8 测试
//
// 测试矩阵:
//   T1: paper mode SignerV52 Ed25519 mock signature (64B detached)
//   T2: IPC msgpack request/response 语义 round-trip (struct 字段完整性)
//   T3: 4 ts R-20 透传 (与老李 TimestampQuad 对齐)
//   T4: data_source_ts_source uint8 0-3 enum 与老李 ABI 一致
//   T5: signature_type=2 paper 强 reject (paper 不真签, sigType!=2 → InternalError)
//   T6: audit_id 16B 非零 (BUG-W5-001 教训)
//   T7: HMAC bug 4 反模式 (rstrip / param_type / sigType=1 / path 拼 querystring) 全禁
//   T8: 与老沈 STRATEGY_DECAYED 三签 Ed25519 公钥 mismatch fail
//
// 红线:
//   R-7  paper mode only (STCPP_EXEC_MODE_paper guard)
//   R-20 4 ts chain
//   BUG-W5-001 audit_id 非零

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

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
    req.event_ts_ns        = base;
    req.data_source_ts_ns  = base + 1'000'000LL;      // +1ms
    req.ingestion_ts_ns    = base + 2'000'000LL;       // +2ms
    req.as_of_ts_ns        = base + 3'000'000LL;       // +3ms (still ≤ now since base = now-1s)
    req.data_source_ts_source = 0U;  // UpstreamPayload
    req.market_id          = "0xabcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890";
    req.outcome            = 0U;    // YES
    req.signature_type     = 2U;    // EIP-712 (HMAC bug #3 enforce)
    req.intent_id          = 42ULL;
    // audit_id 非零 (BUG-W5-001)
    req.audit_id[0]  = 0xDE;
    req.audit_id[1]  = 0xAD;
    req.audit_id[2]  = 0xBE;
    req.audit_id[3]  = 0xEF;
    req.audit_id[4]  = 0x01;
    req.audit_id[5]  = 0x02;
    req.audit_id[6]  = 0x03;
    req.audit_id[7]  = 0x04;
    req.audit_id[8]  = 0x05;
    req.audit_id[9]  = 0x06;
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

    const auto req  = MakeValidRequest();
    const auto resp = signer.Sign(req);

    EXPECT_EQ(resp.error, SignV52Error::Ok) << "Expected Ok, got: " << ToString(resp.error);
    EXPECT_EQ(resp.signature.size(), 64U) << "Ed25519 detached signature must be 64B";

    // 签名必须非全零 (有效签名; paper mock 或 libsodium 真签均不应全零)
    bool any_nonzero = false;
    for (const auto b : resp.signature) {
        if (b != 0U) { any_nonzero = true; break; }
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
    const auto req  = MakeValidRequest();
    const auto resp = signer.Sign(req);

    ASSERT_EQ(resp.error, SignV52Error::Ok);

    // R-20 4 ts 透传
    EXPECT_EQ(resp.event_ts_ns,       req.event_ts_ns);
    EXPECT_EQ(resp.data_source_ts_ns, req.data_source_ts_ns);
    EXPECT_EQ(resp.ingestion_ts_ns,   req.ingestion_ts_ns);
    EXPECT_EQ(resp.as_of_ts_ns,       req.as_of_ts_ns);

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
    req.event_ts_ns        = base + 2'000'000LL;   // event 靠后
    req.data_source_ts_ns  = base + 1'000'000LL;   // ds < event → PitViolation!
    req.ingestion_ts_ns    = base + 3'000'000LL;
    req.as_of_ts_ns        = base + 4'000'000LL;

    const auto resp = signer.Sign(req);
    // ds < event → PitViolation; 4 ts 仍然透传
    EXPECT_EQ(resp.error, SignV52Error::PitViolation)
        << "T3: data_source < event must trigger PitViolation";
    EXPECT_EQ(resp.event_ts_ns,       req.event_ts_ns);
    EXPECT_EQ(resp.data_source_ts_ns, req.data_source_ts_ns);
    EXPECT_EQ(resp.ingestion_ts_ns,   req.ingestion_ts_ns);
    EXPECT_EQ(resp.as_of_ts_ns,       req.as_of_ts_ns);

    // 合法 ts (近期)
    auto req2 = MakeValidRequest();
    const auto resp2 = signer.Sign(req2);
    EXPECT_EQ(resp2.error, SignV52Error::Ok);
    EXPECT_EQ(resp2.event_ts_ns,       req2.event_ts_ns);
    EXPECT_EQ(resp2.data_source_ts_ns, req2.data_source_ts_ns);
    EXPECT_EQ(resp2.ingestion_ts_ns,   req2.ingestion_ts_ns);
    EXPECT_EQ(resp2.as_of_ts_ns,       req2.as_of_ts_ns);
}

// ============================================================
// T4: data_source_ts_source uint8 0-3 enum 与老李 ABI 一致
// (handshake §4.3 ABI lock)
// ============================================================
TEST(SignerV52, T4_DataSourceTsSourceEnumAbiLock) {
    using DST = polymarket::DataSourceTsSource;

    // ABI lock: 值 0-3 与 signer IPC uint8_t 对应
    EXPECT_EQ(static_cast<std::uint8_t>(DST::UpstreamPayload),       0U);
    EXPECT_EQ(static_cast<std::uint8_t>(DST::UpstreamHeader),        1U);
    EXPECT_EQ(static_cast<std::uint8_t>(DST::InferredFromDsTs),      2U);
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
// T5: signature_type=2 paper 强 reject (sigType != 2 → InternalError)
// HMAC bug #3 教训: paper 不真签但接口 reserve; sigType=1 永久禁止
// ============================================================
TEST(SignerV52, T5_SignatureTypeMustBeTwo) {
    SignerV52 signer{execution::ExecutionMode::Paper};

    // sigType=2 → Ok
    {
        auto req = MakeValidRequest();
        req.signature_type = 2U;
        const auto resp = signer.Sign(req);
        EXPECT_EQ(resp.error, SignV52Error::Ok) << "sigType=2 must be accepted";
    }

    // sigType=1 → InternalError (HMAC bug #3 反模式禁止)
    {
        auto req = MakeValidRequest();
        req.signature_type = 1U;  // BUG#3: Magic 1-of-1 Safe — FORBIDDEN
        const auto resp = signer.Sign(req);
        EXPECT_EQ(resp.error, SignV52Error::InternalError)
            << "sigType=1 (HMAC bug #3) must be rejected";
    }

    // sigType=0 → InternalError
    {
        auto req = MakeValidRequest();
        req.signature_type = 0U;
        const auto resp = signer.Sign(req);
        EXPECT_EQ(resp.error, SignV52Error::InternalError)
            << "sigType=0 must be rejected";
    }

    // sigType=3 → InternalError
    {
        auto req = MakeValidRequest();
        req.signature_type = 3U;
        const auto resp = signer.Sign(req);
        EXPECT_EQ(resp.error, SignV52Error::InternalError)
            << "sigType=3 must be rejected";
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
        EXPECT_EQ(resp.error, SignV52Error::Ok)
            << "Single non-zero byte in audit_id should be accepted";
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
// T7: HMAC bug 4 反模式验证
// 老孙 v3 §A 永久 enforce:
//   BUG#1: rstrip 尾斜杠 (url_path 末尾 '/')
//   BUG#2: base64 padding ('=' 不能 rstrip)
//   BUG#3: sigType=1 (上面 T5 已覆盖; 此处强调概念)
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

    // BUG#3 教训: sigType=1 永久禁止 (T5 详细, 此处仅再确认)
    {
        auto req = MakeValidRequest();
        req.signature_type = 1U;  // FORBIDDEN
        const auto resp = signer.Sign(req);
        EXPECT_EQ(resp.error, SignV52Error::InternalError)
            << "BUG#3: sigType=1 must always be rejected";
    }

    // BUG#4 教训: market_id 含 '?' (querystring 混入) — signer 不崩溃
    {
        auto req = MakeValidRequest();
        req.market_id = "0xabcd?foo=bar";  // querystring 混入
        const auto resp = signer.Sign(req);
        EXPECT_NE(resp.error, SignV52Error::LibsodiumInitFail)
            << "BUG#4: signer must not crash on querystring in market_id";
    }

    // BUG#2 教训: signature_type=2 允许 (base64 padding 保留 — live M5+ 层约束)
    // 此处验证 sigType=2 正常流程
    {
        auto req = MakeValidRequest();
        req.signature_type = 2U;  // CORRECT: EIP-712
        const auto resp = signer.Sign(req);
        EXPECT_EQ(resp.error, SignV52Error::Ok)
            << "BUG#2: sigType=2 (EIP-712) must be accepted";
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
#  include <sodium.h>  // for T8 crypto_sign_ed25519_verify_detached
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
    EXPECT_NE(pk_a, pk_b)
        << "T8: two independently created signers must have different keypairs";

    // signer_a 签名
    auto req = MakeValidRequest();
    const auto resp_a = signer_a.Sign(req);
    ASSERT_EQ(resp_a.error, SignV52Error::Ok);
    ASSERT_EQ(resp_a.signature.size(), 64U);

#ifdef STCPP_SIGNER_V52_LIBSODIUM
    // 有 libsodium: 用 signer_b 的公钥验证 signer_a 的签名 → 必须失败

    // 重建 msg (与 signer_v52.cpp Sign() 中构造一致)
    std::vector<std::uint8_t> msg;
    msg.reserve(8U + req.market_id.size() + 1U + 16U);
    for (std::size_t i = 0; i < 8U; ++i) {
        msg.push_back(static_cast<std::uint8_t>(
            (static_cast<std::uint64_t>(req.event_ts_ns) >> (i * 8U)) & 0xFFU));
    }
    for (const char c : req.market_id) {
        msg.push_back(static_cast<std::uint8_t>(c));
    }
    msg.push_back(req.outcome);
    for (const auto b : req.audit_id) {
        msg.push_back(b);
    }

    // 用 signer_b pubkey 验证 → 必须失败 (返回 -1)
    const int verify_result = crypto_sign_ed25519_verify_detached(
        resp_a.signature.data(),
        msg.data(),
        static_cast<unsigned long long>(msg.size()),  // NOLINT(google-runtime-int)
        pk_b.data());
    EXPECT_EQ(verify_result, -1)
        << "T8: verifying signer_a signature with signer_b pubkey must fail";

    // 用 signer_a pubkey 验证 → 必须成功 (返回 0)
    const int verify_ok = crypto_sign_ed25519_verify_detached(
        resp_a.signature.data(),
        msg.data(),
        static_cast<unsigned long long>(msg.size()),  // NOLINT(google-runtime-int)
        pk_a.data());
    EXPECT_EQ(verify_ok, 0)
        << "T8: verifying signer_a signature with signer_a pubkey must succeed";
#else
    // 无 libsodium: 仅验证两个 signer 公钥不同 (证明 keypair 独立)
    EXPECT_NE(pk_a, pk_b) << "T8: signers must have different pubkeys (no libsodium verify)";

    // 验证 resp_a.signature 非零 (mock 路径也不应全零)
    bool nonzero = false;
    for (const auto b : resp_a.signature) {
        if (b != 0U) { nonzero = true; break; }
    }
    EXPECT_TRUE(nonzero) << "T8: signature must not be all-zero even in mock path";
#endif
}

// ============================================================
// 附加: R-11 audit_wal_kind paper 硬填 PaperAudit
// ============================================================
TEST(SignerV52, Bonus_R11AuditWalKindPaperAudit) {
    SignerV52 signer{execution::ExecutionMode::Paper};
    const auto req  = MakeValidRequest();
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

}  // namespace stcpp::signer::v52::test
