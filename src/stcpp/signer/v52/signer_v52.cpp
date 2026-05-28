// stcpp/signer/v52/signer_v52.cpp — SignerV52 v5.3 实现 (C++20, libsodium ExternalProject_Add)
//
// Owner: 老孙 (#06)
// Wave 34 W8: libsodium FetchContent ExternalProject_Add cpp v0.2
//             撤 W6 W3 STCPP_SIGNER_V52_LIBSODIUM=0/1 双路径 (mock + brew)
//             改单一路径: 调 stcpp::crypto::Ed25519::sign(secret_key, message)
// Wave 58 W9: v5.3 — token_id/side/outcome 字段; side 0/1 校验; sigType enforce 修 (1);
//             EIP-712 message 含 token_id bytes; reject_reason 填写
//
// ADR-027 cite (Enforce-1 强 enforce):
//   polymarket_ssot_cite: docs/RESEARCH/laoli-w8-polymarket-data-structure-ssot-v1.md §3.5
//   handshake_cite:       docs/RESEARCH/laoli-laoSun-handshake-v1.md §84
//   goalserve_ssot_cite:  N/A
//   adr_cite:             docs/ADR/2026-06-W4-adr-027-core-data-structure-ssot-enforce.md
//
// 红线执行:
//   R-1  signer 不做 RM 评估 — 由 caller 保证已通过 RiskGateway
//   R-7  STCPP_EXEC_MODE_paper / live / backtest 三 mode CMake 物理隔离
//   R-11 paper mode: secret_key 随机 keypair (非真私钥); audit_wal_kind 硬填
//   R-20 4 ts UPSTREAM_PAYLOAD 优先; 入口 AssertChain
//   HMAC bug 4 教训 (永久 enforce; 本实现不做 HMAC, 但 API 约束见注释):
//     BUG#1 rstrip: url_path 最后 '/' 必须去掉
//     BUG#2 sigType: signature_type 必须 = 1 (Magic Safe EOA); != 1 → REJECT
//             v5.1 曾错填 2; v5.3 修正 (SSOT §5 T-10, handshake §84)
//     BUG#3 base64 padding: '=' 保留 (live M5+ 层约束)
//     BUG#4 path+query: canonical_path 与 query_string 分离
//   BUG-W5-001 防御: 不自写 shift/xor; 仅调 stcpp::crypto::Ed25519 API
//
// paper mode 签名流程 (R-11):
//   1. 构造时生成随机 Ed25519 keypair (crypto::Ed25519::generate_keypair)
//      secret_key 存入 SecureBuffer<64> (析构时 sodium_memzero 清零)
//   2. Sign(req): 校验 side 0/1; sigType == 1; 4 ts chain;
//      构建 EIP-712 proxy message (含 token_id bytes);
//      调 crypto::Ed25519::sign(sk_, message) → 64B detached sig
//   3. 析构: SecureBuffer 析构自动清零 sk_
//
// live mode (M5+):
//   secret_key 从 .env 加密读 (Sygnum / Taurus, deferred)
//   本 stub 返回 SignV52Error::InternalError

#include "stcpp/signer/v52/signer_v52.hpp"
#include "stcpp/crypto/ed25519.hpp"

#include <algorithm>
#include <cstring>

#include "stcpp/infra/wal/pit.hpp"

namespace stcpp::signer::v52 {

namespace {

// ---------- R-20 PIT AssertChain ----------

[[nodiscard]] bool AssertChainTs(std::int64_t event_ts,
                                 std::int64_t ds_ts,
                                 std::int64_t ingest_ts,
                                 std::int64_t as_of_ts) noexcept {
    return event_ts  >  0
        && ds_ts     >= event_ts
        && ingest_ts >= ds_ts
        && as_of_ts  >= ingest_ts
        && as_of_ts  <= infra::wal::pit::NowRealtimeNs();
}

// ---------- BUG-W5-001 防御: audit_id 全零检测 ----------

[[nodiscard]] bool AuditIdNonZero(const std::array<std::uint8_t, 16>& id) noexcept {
    for (const auto b : id) {
        if (b != 0U) return true;
    }
    return false;
}

// ---------- HMAC bug #2 enforce: signature_type 必须 = 1 ----------
//
// v5.3 修正: signature_type 必须 = 1 (Magic Safe EOA 1-of-1, Polymarket CTF funder).
// v5.1 曾错误 enforce == 2; v5.3 修正为 1 (SSOT §5 T-10, handshake §84).
// sig_type != 1 → InternalError, reject_reason = "invalid_signature_type".
// 教训: HMAC bug #2 = "填了 2 而非 1"; 正确值是 1.

[[nodiscard]] bool ValidateSignatureType(std::uint8_t sig_type) noexcept {
    return sig_type == 1U;  // 1 = Magic Safe EOA; 2 = FORBIDDEN (HMAC bug #2 修正)
}

// ---------- v5.3 side 校验: 必须 0 (Buy) 或 1 (Sell) ----------
//
// side=2 或其他值 → InvalidSide 拒签, reject_reason = "invalid_side".
// 与老韩 v0.5 Side enum 一致: 0=Buy, 1=Sell.
// cite: SSOT §3.5 side + handshake §84 side

[[nodiscard]] bool ValidateSide(std::uint8_t side) noexcept {
    return side == 0U || side == 1U;
}

// ---------- v5.3 token_id 非空校验 ----------
//
// v5.3 要求 token_id 必须非空 (uint256 decimal string, 来自 OrderIntent).
// 空 token_id = INVALID_INTENT (msgpack v1.2 → v1.3 未更新, 缺字段).
// cite: laosun-w9-signer-v53-abi-align-spec-v1.md §4.3

[[nodiscard]] bool ValidateTokenId(const std::string& token_id) noexcept {
    return !token_id.empty();
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
    return infra::wal::WalKind::PaperAudit;  // 防御性 fallback
}

// ---------- paper mode keypair 生成 (R-11) ----------
//
// paper mode: 随机生成 Ed25519 keypair (crypto::Ed25519::generate_keypair).
// 此 keypair 不是真实私钥 (不从 .env 读 — R-11 paper 不真签), 仅用于 paper audit trace.
// 每次构造 SignerV52 生成独立随机 keypair (T8 验证: 两个 signer 公钥必须不同).
//
// live mode (M5+): secret_key 从 .env 加密读 (Sygnum / Taurus, deferred)
//
// 返回: 成功 true; sk_out (64B SecureBuffer) + pk_out (32B) 已填充.

[[nodiscard]] bool GeneratePaperKeypair(
    std::array<std::uint8_t, crypto::kEd25519PublicKeyBytes>& pk_out,
    crypto::SecureBuffer<crypto::kEd25519SecretKeyBytes>&     sk_out) noexcept {

    return crypto::Ed25519::generate_keypair(pk_out, sk_out);
}

}  // namespace

// ---------- SignerV52 constructor ----------

SignerV52::SignerV52(execution::ExecutionMode mode)
    : mode_(mode), sk_{}, pk_{}, keypair_valid_(false) {

    if (mode_ != execution::ExecutionMode::Paper) {
        // live / backtest: stub — M5+ secp256k1 真切
        return;
    }

    // paper mode: 初始化 libsodium (幂等, 多次调用安全)
    // sodium_init() 返回 -1 = 失败 (极罕见, kernel RNG 不可用)
    if (sodium_init() < 0) {
        // 不抛异常 (noexcept 合约) — Sign() 返回 LibsodiumInitFail
        return;
    }

    // 生成 paper keypair (R-11: 随机, 非真私钥 — 不从 .env 读)
    if (!GeneratePaperKeypair(pk_, sk_)) {
        return;  // 极罕见失败 — Sign() 返回 LibsodiumInitFail
    }
    keypair_valid_ = true;
}

// ---------- SignerV52 destructor ----------
// SecureBuffer<64> sk_ 析构时自动 sodium_memzero — 不需要手动清零

SignerV52::~SignerV52() {
    keypair_valid_ = false;
    // sk_ (SecureBuffer<64>) 析构链自动 sodium_memzero
    // pk_ (std::array<uint8_t,32>) 非私钥, 公钥不需要清零
}

// ---------- SignerV52 move constructor ----------

SignerV52::SignerV52(SignerV52&& other) noexcept
    : mode_(other.mode_)
    , sk_(std::move(other.sk_))
    , pk_(other.pk_)
    , keypair_valid_(other.keypair_valid_) {
    // 清空 other 防止析构时访问已移走资源
    other.keypair_valid_ = false;
    other.pk_.fill(0U);
    // other.sk_ 已 move (SecureBuffer move 构造自动清零 source)
}

// ---------- SignerV52::Sign ----------

SignV52Response SignerV52::Sign(const SignV52Request& req) noexcept {
    SignV52Response resp;

    // R-20 4 ts 透传 (原样 copy, 无论 sign 成功与否)
    resp.event_ts_ns       = req.event_ts_ns;
    resp.data_source_ts_ns = req.data_source_ts_ns;
    resp.ingestion_ts_ns   = req.ingestion_ts_ns;
    resp.as_of_ts_ns       = req.as_of_ts_ns;

    // R-11: audit_wal_kind 按 mode 硬填
    resp.audit_wal_kind = WalKindForMode(mode_);

    // BUG-W5-001: audit_id echo-back (caller 保证非零; response 携带原样)
    resp.audit_id = req.audit_id;

    // R-7 ModeMismatch: paper signer 不允许被 live/backtest mode 调 (防御)
    if (mode_ == execution::ExecutionMode::Live
        || mode_ == execution::ExecutionMode::Backtest) {
        resp.error = SignV52Error::InternalError;
        return resp;
    }

    // 私钥有效性检查
    if (!keypair_valid_) {
        resp.error = SignV52Error::LibsodiumInitFail;
        return resp;
    }

    // R-20: 入口 PIT AssertChain
    if (!AssertChainTs(req.event_ts_ns, req.data_source_ts_ns,
                       req.ingestion_ts_ns, req.as_of_ts_ns)) {
        resp.error = SignV52Error::PitViolation;
        return resp;
    }

    // HMAC bug #2 enforce: signature_type 必须 = 1 (Magic Safe EOA)
    // v5.1 错填 2; v5.3 修正 (SSOT §5 T-10, handshake §84)
    if (!ValidateSignatureType(req.signature_type)) {
        resp.error = SignV52Error::InternalError;
        resp.reject_reason = "invalid_signature_type";
        return resp;
    }

    // BUG-W5-001: audit_id 必须非零
    if (!AuditIdNonZero(req.audit_id)) {
        resp.error = SignV52Error::InternalError;
        resp.reject_reason = "invalid_audit_id";
        return resp;
    }

    // v5.3: side 必须 0 (Buy) 或 1 (Sell)
    // cite: SSOT §3.5 side + handshake §84 side + 老韩 v0.5 Side enum
    if (!ValidateSide(req.side)) {
        resp.error = SignV52Error::InvalidSide;
        resp.reject_reason = "invalid_side";
        return resp;
    }

    // v5.3: token_id 必须非空 (uint256 decimal string)
    // cite: SSOT §3.5 token_id + handshake §84 tokenId
    if (!ValidateTokenId(req.token_id)) {
        resp.error = SignV52Error::InvalidIntent;
        resp.reject_reason = "invalid_token_id";
        return resp;
    }

    // ---------- 构建签名消息 ----------
    //
    // paper 使用 EIP-712 proxy 消息格式 (含 token_id bytes, v5.3 新增)
    // 签名内容: event_ts(8B LE) || market_id(变长) || token_id(变长) || side(1B)
    //           || outcome(1B) || audit_id(16B)
    //
    // token_id 进 EIP-712 Order.tokenId (uint256 bignum encoding) — v5.3 关键变更
    // cite: laosun-w9-signer-v53-abi-align-spec-v1.md §3.2
    //
    // BUG-W5-001 shift safety: 仅用 static_cast 字节提取, 不自写位移宏
    std::vector<std::uint8_t> msg;
    msg.reserve(8U + req.market_id.size() + req.token_id.size() + 1U + 1U + 16U);

    // event_ts_ns (8B little-endian)
    for (std::size_t i = 0U; i < 8U; ++i) {
        msg.push_back(static_cast<std::uint8_t>(
            (static_cast<std::uint64_t>(req.event_ts_ns) >> (i * 8U)) & 0xFFU));
    }
    // market_id (UTF-8 bytes, condition_id)
    for (const char c : req.market_id) {
        msg.push_back(static_cast<std::uint8_t>(c));
    }
    // token_id (UTF-8 bytes, uint256 decimal string — v5.3 新增)
    // 进 EIP-712 Order.tokenId; paper proxy 消息直接附 string bytes
    // cite: SSOT §3.5 token_id + handshake §84 tokenId
    for (const char c : req.token_id) {
        msg.push_back(static_cast<std::uint8_t>(c));
    }
    // side (1B: 0=Buy, 1=Sell — v5.3 新增)
    // cite: SSOT §3.5 side + handshake §84 side
    msg.push_back(req.side);
    // outcome (1B, audit only — 不进 EIP-712 Order struct, 但 paper proxy 消息含)
    msg.push_back(req.outcome);
    // audit_id (16B)
    for (const auto b : req.audit_id) {
        msg.push_back(b);
    }

    // ---------- Ed25519 detached sign (stcpp::crypto::Ed25519) ----------
    //
    // 单一路径: stcpp::crypto::Ed25519::sign(sk_, message)
    // 撤 W6 W3 STCPP_SIGNER_V52_LIBSODIUM=0/1 条件编译双路径
    auto sig_arr = crypto::Ed25519::sign(
        sk_,
        std::span<const std::uint8_t>{msg.data(), msg.size()}
    );

    // Ed25519 失败: sign() 返回全零数组 (极罕见)
    bool sig_is_zero = true;
    for (const auto b : sig_arr) {
        if (b != 0U) { sig_is_zero = false; break; }
    }
    if (sig_is_zero) {
        resp.error = SignV52Error::InternalError;
        return resp;
    }

    resp.signature.assign(sig_arr.begin(), sig_arr.end());
    resp.error = SignV52Error::Ok;
    return resp;
}

// ---------- SignerV52::PublicKeyBytes ----------

std::vector<std::uint8_t> SignerV52::PublicKeyBytes() const noexcept {
    if (!keypair_valid_ || mode_ != execution::ExecutionMode::Paper) {
        return {};
    }
    return std::vector<std::uint8_t>(pk_.begin(), pk_.end());
}

}  // namespace stcpp::signer::v52
