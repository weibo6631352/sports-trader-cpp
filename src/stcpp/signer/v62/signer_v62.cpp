// stcpp/signer/v62/signer_v62.cpp — SignerV62 v6.2 实现 (C++20, libsodium)
//
// Owner: 老孙 (#06, crypto-signing-expert)
// Wave 104 P0: V2 ABI cpp 实施 (V1 SignerV52 v5.3 共存, paper/live 必 V2)
//
// ADR-027 cite (Enforce-1 强 enforce):
//   polymarket_ssot_cite: docs/RESEARCH/laoli-w9-w5-polymarket-market-research-update-v1.md
//                         §3.1 EIP-712 Order struct V2
//   laosun_v62_spec_cite: docs/RESEARCH/laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md
//   laoshen_8_spec_cite:  Wave 97 task output (10 安全 spec)
//   laotang_v14_cite:     include/stcpp/observability/audit_record.hpp v1.4
//   adr_cite:             ADR-027 / ADR-029 / ADR-032 / ADR-034 v2.1
//
// 红线执行:
//   R-1  signer 不做 RM 评估 — 由 caller 保证已通过 RiskGateway
//   R-7  STCPP_EXEC_MODE_paper / live / backtest 三 mode CMake 物理隔离
//   R-11 paper mode: secret_key 随机 keypair (非真私钥); audit_wal_kind 硬填
//   R-20 4 ts 入口 AssertChain; 违反 → SignV62Error::PitViolation
//   ADR-018 build switch: STCPP_CLOB_V2 (paper + live 必定义)
//   HMAC bug 4 教训:
//     BUG#2: signature_type 必须 = 1; != 1 → InternalError
//   spec-8:  signature_type 不触碰 (transformer 层保证默认值 1)
//   spec-9:  metadata/builder bytes32 格式校验 ^0x[0-9a-f]{64}$ (66 chars)
//   spec-10: timestamp_ms 非零校验 (= 0 → TsV2Missing)
//
// paper mode 签名流程:
//   1. 构造: 随机 Ed25519 keypair (crypto::Ed25519::generate_keypair)
//      sk_ → SecureBuffer<64> (析构时 sodium_memzero 清零)
//   2. Sign(req): 校验 (R-20 PIT / sigType / side / token_id / spec-9/10)
//      构建 V2 EIP-712 proxy message (含 timestamp_ms / metadata / builder bytes)
//      调 crypto::Ed25519::sign(sk_, message) → 64B detached sig
//   3. 析构: SecureBuffer 自动 sodium_memzero 清零 sk_
//
// V2 proxy message 格式 (paper 模拟 EIP-712 V2 binding):
//   event_ts(8B LE) || condition_id(var) || token_id(var) || side(1B) || outcome(1B)
//   || timestamp_ms(8B LE) || metadata(var) || builder(var) || audit_id(16B)
//
// ADR-018 build switch:
//   paper + live: STCPP_CLOB_V2 必定义 (CI abi_lock grep 拦截 V1 link)
//   backtest: 可 STCPP_CLOB_V1 (历史回测 V1) 或 STCPP_CLOB_V2

#include "stcpp/signer/v62/signer_v62.hpp"

#include <algorithm>
#include <cctype>

#include "stcpp/crypto/ed25519.hpp"
#include "stcpp/infra/wal/pit.hpp"

namespace stcpp::signer::v62 {

namespace {

// ---------- R-20 PIT AssertChain (V2 维持, 4 ts 不变) ----------

[[nodiscard]] bool AssertChainTs(std::int64_t event_ts, std::int64_t ds_ts, std::int64_t ingest_ts,
                                 std::int64_t as_of_ts) noexcept {
    return event_ts > 0 && ds_ts >= event_ts && ingest_ts >= ds_ts && as_of_ts >= ingest_ts &&
           as_of_ts <= infra::wal::pit::NowRealtimeNs();
}

// ---------- BUG-W5-001: audit_id 非零检测 ----------

[[nodiscard]] bool AuditIdNonZero(const std::array<std::uint8_t, 16>& id) noexcept {
    for (const auto b : id) {
        if (b != 0U)
            return true;
    }
    return false;
}

// ---------- HMAC bug #2 enforce: signature_type 必须 = 1 ----------
// spec-8: transformer_v62 保证默认值 1; 此处二次校验防御

[[nodiscard]] bool ValidateSignatureType(std::uint8_t sig_type) noexcept {
    return sig_type == 1U;
}

// ---------- side 校验: 0=Buy, 1=Sell ----------

[[nodiscard]] bool ValidateSide(std::uint8_t side) noexcept {
    return side == 0U || side == 1U;
}

// ---------- token_id 非空校验 ----------

[[nodiscard]] bool ValidateTokenId(const std::string& token_id) noexcept {
    return !token_id.empty();
}

// ---------- spec-10: timestamp_ms 非零 ----------

[[nodiscard]] bool ValidateTimestampMs(std::int64_t ts_ms) noexcept {
    return ts_ms != 0LL;
}

// ---------- spec-9: bytes32 hex 格式校验 ^0x[0-9a-f]{64}$ ----------

[[nodiscard]] bool ValidateBytes32Hex(const std::string& s) noexcept {
    if (s.size() != 66U)
        return false;
    if (s[0] != '0' || s[1] != 'x')
        return false;
    for (std::size_t i = 2U; i < 66U; ++i) {
        const char c = s[i];
        const bool is_digit = (c >= '0' && c <= '9');
        const bool is_lower = (c >= 'a' && c <= 'f');
        if (!is_digit && !is_lower)
            return false;
    }
    return true;
}

// ---------- WalKind 按 mode 硬填 (R-11) ----------

[[nodiscard]] infra::wal::WalKind WalKindForMode(execution::ExecutionMode m) noexcept {
    switch (m) {
        case execution::ExecutionMode::Paper:
            return infra::wal::WalKind::PaperAudit;
        case execution::ExecutionMode::Live:
            return infra::wal::WalKind::RiskAudit;
        case execution::ExecutionMode::Backtest:
            return infra::wal::WalKind::ShadowAudit;
    }
    return infra::wal::WalKind::PaperAudit;
}

// ---------- paper mode keypair 生成 (R-11) ----------

[[nodiscard]] bool GeneratePaperKeypair(
    std::array<std::uint8_t, crypto::kEd25519PublicKeyBytes>& pk_out,
    crypto::SecureBuffer<crypto::kEd25519SecretKeyBytes>& sk_out) noexcept {
    return crypto::Ed25519::generate_keypair(pk_out, sk_out);
}

}  // namespace

// ---------- SignerV62 constructor ----------

SignerV62::SignerV62(execution::ExecutionMode mode) : mode_(mode), sk_{}, pk_{}, keypair_valid_(false) {
    if (mode_ != execution::ExecutionMode::Paper) {
        // live / backtest: stub
        return;
    }

    if (sodium_init() < 0) {
        return;  // 极罕见; Sign() 返回 LibsodiumInitFail
    }

    if (!GeneratePaperKeypair(pk_, sk_)) {
        return;
    }
    keypair_valid_ = true;
}

// ---------- SignerV62 destructor ----------

SignerV62::~SignerV62() {
    keypair_valid_ = false;
    // sk_ (SecureBuffer<64>) 析构链自动 sodium_memzero
}

// ---------- SignerV62 move constructor ----------

SignerV62::SignerV62(SignerV62&& other) noexcept
    : mode_(other.mode_), sk_(std::move(other.sk_)), pk_(other.pk_), keypair_valid_(other.keypair_valid_) {
    other.keypair_valid_ = false;
    other.pk_.fill(0U);
}

// ---------- SignerV62::Sign ----------

SignV62Response SignerV62::Sign(const SignV62Request& req) noexcept {
    SignV62Response resp;

    // R-20 4 ts 透传 (原样 copy, 无论 sign 成功与否)
    resp.event_ts_ns = req.event_ts_ns;
    resp.data_source_ts_ns = req.data_source_ts_ns;
    resp.ingestion_ts_ns = req.ingestion_ts_ns;
    resp.as_of_ts_ns = req.as_of_ts_ns;

    // R-11: audit_wal_kind 按 mode 硬填
    resp.audit_wal_kind = WalKindForMode(mode_);

    // BUG-W5-001: audit_id echo-back
    resp.audit_id = req.audit_id;

    // R-7: live/backtest → stub
    if (mode_ == execution::ExecutionMode::Live || mode_ == execution::ExecutionMode::Backtest) {
        resp.error = SignV62Error::InternalError;
        resp.reject_reason = "mode_not_paper";
        return resp;
    }

    if (!keypair_valid_) {
        resp.error = SignV62Error::LibsodiumInitFail;
        return resp;
    }

    // R-20: 入口 PIT AssertChain
    if (!AssertChainTs(req.event_ts_ns, req.data_source_ts_ns, req.ingestion_ts_ns, req.as_of_ts_ns)) {
        resp.error = SignV62Error::PitViolation;
        resp.reject_reason = "pit_violation";
        return resp;
    }

    // HMAC bug #2 enforce: signature_type 必须 = 1 (spec-8)
    if (!ValidateSignatureType(req.signature_type)) {
        resp.error = SignV62Error::InternalError;
        resp.reject_reason = "invalid_signature_type";
        return resp;
    }

    // BUG-W5-001: audit_id 非零
    if (!AuditIdNonZero(req.audit_id)) {
        resp.error = SignV62Error::InternalError;
        resp.reject_reason = "invalid_audit_id";
        return resp;
    }

    // side 0/1 校验 (spec-2 维持)
    if (!ValidateSide(req.side)) {
        resp.error = SignV62Error::InvalidSide;
        resp.reject_reason = "invalid_side";
        return resp;
    }

    // token_id 非空 (spec-1 维持)
    if (!ValidateTokenId(req.token_id)) {
        resp.error = SignV62Error::InvalidIntent;
        resp.reject_reason = "invalid_token_id";
        return resp;
    }

    // spec-10: timestamp_ms 非零 (V2 CLOB 唯一性保证)
    // cite: laosun-w10-w1 spec-10
    if (!ValidateTimestampMs(req.timestamp_ms)) {
        resp.error = SignV62Error::TsV2Missing;
        resp.reject_reason = "ts_v2_missing";
        return resp;
    }

    // spec-9: metadata bytes32 格式校验 ^0x[0-9a-f]{64}$ (66 chars)
    // cite: laosun-w10-w1 spec-9 + Wave 97 安全 spec
    if (!ValidateBytes32Hex(req.metadata)) {
        resp.error = SignV62Error::InvalidBytes32;
        resp.reject_reason = "invalid_bytes32_metadata";
        return resp;
    }

    // spec-9: builder bytes32 格式校验
    if (!ValidateBytes32Hex(req.builder)) {
        resp.error = SignV62Error::InvalidBytes32;
        resp.reject_reason = "invalid_bytes32_builder";
        return resp;
    }

    // ---------- 构建 V2 EIP-712 proxy message ----------
    //
    // V2 proxy message: event_ts(8B LE) || condition_id(var) || token_id(var)
    //   || side(1B) || outcome(1B) || timestamp_ms(8B LE)
    //   || metadata(var) || builder(var) || audit_id(16B)
    //
    // timestamp_ms, metadata, builder — V2 关键新增字段 (V1 无这三个)
    // outcome: audit only (不进 EIP-712 Order struct), 但 paper proxy 消息含
    // cite: laosun-w10-w1 §2.1 §3.3

    const std::size_t reserve_size = 8U                                                    // event_ts (8B LE)
                                     + req.condition_id.size() + req.token_id.size() + 1U  // side
                                     + 1U                                                  // outcome
                                     + 8U  // timestamp_ms (8B LE)
                                     + req.metadata.size() + req.builder.size() + 16U;  // audit_id

    std::vector<std::uint8_t> msg;
    msg.reserve(reserve_size);

    // event_ts_ns (8B little-endian)
    for (std::size_t i = 0U; i < 8U; ++i) {
        msg.push_back(
            static_cast<std::uint8_t>((static_cast<std::uint64_t>(req.event_ts_ns) >> (i * 8U)) & 0xFFU));
    }
    // condition_id (V2 正名; 不叫 market_id)
    for (const char c : req.condition_id) {
        msg.push_back(static_cast<std::uint8_t>(c));
    }
    // token_id (spec-1: pass-through, uint256 decimal string)
    for (const char c : req.token_id) {
        msg.push_back(static_cast<std::uint8_t>(c));
    }
    // side (spec-2: static_cast 纯 cast, 0=Buy/1=Sell)
    msg.push_back(req.side);
    // outcome (spec-5: audit only)
    msg.push_back(req.outcome);
    // timestamp_ms (8B little-endian) — V2 新增 (spec-3: 原样, 禁 now())
    for (std::size_t i = 0U; i < 8U; ++i) {
        msg.push_back(
            static_cast<std::uint8_t>((static_cast<std::uint64_t>(req.timestamp_ms) >> (i * 8U)) & 0xFFU));
    }
    // metadata (spec-9: bytes32 hex string, 66 chars, 已校验)
    for (const char c : req.metadata) {
        msg.push_back(static_cast<std::uint8_t>(c));
    }
    // builder (spec-9: bytes32 hex string, 66 chars, 已校验)
    for (const char c : req.builder) {
        msg.push_back(static_cast<std::uint8_t>(c));
    }
    // audit_id (16B, BUG-W5-001)
    for (const auto b : req.audit_id) {
        msg.push_back(b);
    }

    // ---------- Ed25519 detached sign ----------

    auto sig_arr = crypto::Ed25519::sign(sk_, std::span<const std::uint8_t>{msg.data(), msg.size()});

    bool sig_is_zero = true;
    for (const auto b : sig_arr) {
        if (b != 0U) {
            sig_is_zero = false;
            break;
        }
    }
    if (sig_is_zero) {
        resp.error = SignV62Error::InternalError;
        resp.reject_reason = "sign_failed";
        return resp;
    }

    resp.signature.assign(sig_arr.begin(), sig_arr.end());
    resp.error = SignV62Error::Ok;
    return resp;
}

// ---------- SignerV62::PublicKeyBytes ----------

std::vector<std::uint8_t> SignerV62::PublicKeyBytes() const noexcept {
    if (!keypair_valid_ || mode_ != execution::ExecutionMode::Paper) {
        return {};
    }
    return std::vector<std::uint8_t>(pk_.begin(), pk_.end());
}

}  // namespace stcpp::signer::v62
