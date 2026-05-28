// stcpp/signer/v52/signer_v52.cpp — SignerV52 实现 (C++20, libsodium Ed25519)
//
// Owner: 老孙 (#06)
// Wave 30 W6: signer v5.2 cpp v0.1 IPC + Ed25519 paper mock
//
// 红线执行:
//   R-1  signer 不做 RM 评估 — 由 caller 保证已通过 RiskGateway (assert 层只校 ts)
//   R-7  STCPP_EXEC_MODE_paper / live / backtest 三 mode CMake 物理隔离
//   R-11 audit_wal_kind 硬填 (paper→PaperAudit / live→RiskAudit / backtest→ShadowAudit)
//   R-20 4 ts UPSTREAM_PAYLOAD 优先; 入口 AssertChain
//   HMAC bug 4 教训 (永久 enforce; 本实现不做 HMAC, 但 API 约束见 §A 注释)
//   BUG-W5-001: audit_id 16B 非零 (request 层 caller 责任; response echo-back 验非零)
//
// libsodium: FetchContent (老沈 W6 Wave 29 SOP §6, vcpkg unofficial-sodium 已撤)
//   crypto_sign_ed25519_keypair() → paper keypair (构造时)
//   crypto_sign_ed25519_detached() → 64B detached signature
//   sodium_memzero() → 析构私钥清零
//
// HMAC bug 4 反模式 (老孙 v3 §A — 本文件不含 HMAC, 但注释 enforce 逻辑):
//   BUG#1 rstrip: CallerSide — url_path 最后 '/' 必须去掉再传入
//   BUG#2 param_type: base64 必须保留 padding '='
//   BUG#3 sigType: signature_type 必须 = 2 (EIP-712); req.signature_type != 2 → REJECT
//   BUG#4 path+query: canonical_path 与 query_string 分离; 禁混入 path
//
// Ed25519 vs secp256k1 路线图:
//   paper (W6): Ed25519 mock — libsodium, 不上链, 仅审计 trace 验证
//   live  (M5+): secp256k1 EIP-712 — 独立 signer 进程 + 内存清零 (老孙 v5.2.live.stub)

#include "stcpp/signer/v52/signer_v52.hpp"

#include <algorithm>
#include <cstring>

// libsodium (FetchContent, 条件编译)
#ifdef STCPP_SIGNER_V52_LIBSODIUM
#  include <sodium.h>
#endif

#include "stcpp/infra/wal/pit.hpp"

namespace stcpp::signer::v52 {

namespace {

// ---------- R-20 PIT AssertChain (inline, 与 paper_signer.cpp 模式一致) ----------

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

// ---------- HMAC bug #3 enforce: signature_type 必须 = 2 ----------
//
// paper mode 不真签, 但接口 reserve 给 live W5+.
// 若 req.signature_type != 2, 说明 caller 传错 sigType → InternalError.
// 教训: bug #3 sigType=1 (Magic 1-of-1 Safe) 是历史错误, EIP-712 必须 2.

[[nodiscard]] bool ValidateSignatureType(std::uint8_t sig_type) noexcept {
    return sig_type == 2U;  // 2 = EIP-712; 1 = FORBIDDEN (HMAC bug #3)
}

// ---------- HMAC bug #1 enforce: url_path 不含尾斜杠 ----------
// paper signer 不做 HTTP, 但辅助函数供上层 caller 使用; 此处仅留注释约束.
// 实际 HMAC 构造在 live_pm_client.cpp (M5+).

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

}  // namespace

// ---------- SignerV52 constructor ----------

SignerV52::SignerV52(execution::ExecutionMode mode)
    : mode_(mode), sk_{}, pk_{}, keypair_valid_(false) {

    if (mode_ != execution::ExecutionMode::Paper) {
        // live / backtest: stub — M5+ secp256k1 真切
        return;
    }

#ifdef STCPP_SIGNER_V52_LIBSODIUM
    // sodium_init() 幂等 (多次调用安全); -1 = 初始化失败 (极罕见, kernel RNG 不可用)
    if (sodium_init() < 0) {
        // 不抛异常 (noexcept 合约) — Sign() 返回 LibsodiumInitFail
        return;
    }
    // 生成临时 Ed25519 keypair (paper mode only; 析构时 sodium_memzero 清零)
    // crypto_sign_ed25519_keypair 写 pk_(32B) + sk_(64B = seed||pk)
    crypto_sign_ed25519_keypair(pk_.data(), sk_.data());
    keypair_valid_ = true;
#else
    // libsodium 未链接时, paper mode 填 deterministic mock bytes (for tests without sodium)
    // 用 "PAPER_V52_MOCK_PK" 固定前缀区分
    constexpr std::uint8_t kMockTag = 0xA5U;
    for (std::size_t i = 0; i < kPublicKeyBytes; ++i) {
        pk_[i] = static_cast<std::uint8_t>(kMockTag ^ static_cast<std::uint8_t>(i));
    }
    for (std::size_t i = 0; i < kSecretKeyBytes; ++i) {
        sk_[i] = static_cast<std::uint8_t>(kMockTag ^ static_cast<std::uint8_t>(i + kPublicKeyBytes));
    }
    keypair_valid_ = true;
#endif
}

// ---------- SignerV52 destructor ----------

SignerV52::~SignerV52() {
    if (keypair_valid_) {
#ifdef STCPP_SIGNER_V52_LIBSODIUM
        // R-7 / CLAUDE: 私钥清零 (libsodium sodium_memzero, safe against compiler optimization)
        sodium_memzero(sk_.data(), sk_.size());
#else
        // 无 libsodium: 手动 volatile memset 防止编译器优化掉清零
        volatile auto* p = static_cast<volatile std::uint8_t*>(sk_.data());
        for (std::size_t i = 0; i < sk_.size(); ++i) {
            p[i] = 0U;
        }
#endif
        keypair_valid_ = false;
    }
}

// ---------- SignerV52 move constructor ----------

SignerV52::SignerV52(SignerV52&& other) noexcept
    : mode_(other.mode_)
    , sk_(other.sk_)
    , pk_(other.pk_)
    , keypair_valid_(other.keypair_valid_) {
    // 清空 other 防止析构时双重清零
    other.keypair_valid_ = false;
    other.sk_.fill(0U);
    other.pk_.fill(0U);
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

    // R-7 ModeMismatch: paper signer 不允许被 live mode 逻辑路径调 (防御)
    // 注: mode_ 是 build-time 决定的, 此处 runtime check 为防御性
    if (mode_ == execution::ExecutionMode::Live
        || mode_ == execution::ExecutionMode::Backtest) {
        // live/backtest stub — M5+ secp256k1 真切
        resp.error = SignV52Error::InternalError;
        return resp;
    }

    // R-20: 入口 PIT AssertChain
    if (!AssertChainTs(req.event_ts_ns, req.data_source_ts_ns,
                       req.ingestion_ts_ns, req.as_of_ts_ns)) {
        resp.error = SignV52Error::PitViolation;
        return resp;
    }

    // HMAC bug #3 enforce: signature_type 必须 = 2 (EIP-712)
    // paper mode 虽然不真签, 接口 reserve — 传错 sigType 视为 caller bug, 返回 InternalError.
    if (!ValidateSignatureType(req.signature_type)) {
        resp.error = SignV52Error::InternalError;
        return resp;
    }

    // BUG-W5-001: audit_id 必须非零
    if (!AuditIdNonZero(req.audit_id)) {
        resp.error = SignV52Error::InternalError;
        return resp;
    }

    // paper mode: Ed25519 detached signature (64B)
    resp.signature.resize(64U, 0U);

#ifdef STCPP_SIGNER_V52_LIBSODIUM
    if (!keypair_valid_) {
        resp.error = SignV52Error::LibsodiumInitFail;
        return resp;
    }
    // 签名内容: event_ts || market_id || outcome || audit_id
    // (paper 不做真 EIP-712 domain hash — 留 marker bytes 便于审计 trace)
    std::vector<std::uint8_t> msg;
    msg.reserve(8U + req.market_id.size() + 1U + 16U);
    // event_ts_ns (8B LE)
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

    unsigned long long sig_len = 0ULL;  // NOLINT(google-runtime-int) libsodium API
    if (crypto_sign_ed25519_detached(
            resp.signature.data(), &sig_len,
            msg.data(), static_cast<unsigned long long>(msg.size()),  // NOLINT(google-runtime-int)
            sk_.data()) != 0) {
        resp.error = SignV52Error::InternalError;
        return resp;
    }
    // sig_len must be 64 (Ed25519 detached fixed size)
    if (sig_len != 64ULL) {
        resp.error = SignV52Error::InternalError;
        return resp;
    }
#else
    // 无 libsodium: deterministic mock (paper trace only)
    // 填充: intent_id(8B) || as_of_ts(8B) || audit_id(16B) || "V52P" tag || 0-pad
    for (std::size_t i = 0; i < 8U && i < resp.signature.size(); ++i) {
        resp.signature[i] = static_cast<std::uint8_t>(
            (req.intent_id >> (i * 8U)) & 0xFFU);
    }
    for (std::size_t i = 0; i < 8U && (8U + i) < resp.signature.size(); ++i) {
        resp.signature[8U + i] = static_cast<std::uint8_t>(
            (static_cast<std::uint64_t>(req.as_of_ts_ns) >> (i * 8U)) & 0xFFU);
    }
    for (std::size_t i = 0; i < 16U && (16U + i) < resp.signature.size(); ++i) {
        resp.signature[16U + i] = req.audit_id[i];
    }
    // "V52P" marker bytes at [32..35]
    if (resp.signature.size() >= 36U) {
        resp.signature[32U] = static_cast<std::uint8_t>('V');
        resp.signature[33U] = static_cast<std::uint8_t>('5');
        resp.signature[34U] = static_cast<std::uint8_t>('2');
        resp.signature[35U] = static_cast<std::uint8_t>('P');
    }
#endif

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
