// stcpp/signer/v52/signer_v52.hpp — SignerV52 v5.3 (ADR-027 Enforce-1, W9 P0)
//
// Owner: 老孙 (#06, crypto-signing-expert)
// Wave 34 W8: libsodium FetchContent ExternalProject_Add cpp v0.2
//             撤 W6 W3 brew find_library 兼容性 hack (老周 C-02 review)
//             改单一路径: 调 stcpp::crypto::Ed25519::sign (老沈 W7 ack)
// Wave 58 W9: v5.3 — 3 字段新增 (token_id/side/outcome) + EIP-712 tokenId binding
//             + sigType enforce 修 (1 = Magic Safe EOA, 非 2) + side 0/1 校验
//
// ADR-027 cite (Enforce-1 强 enforce):
//   polymarket_ssot_cite: docs/RESEARCH/laoli-w8-polymarket-data-structure-ssot-v1.md
//                         §3.5 SignedOrder 字段表 (11 字段 + verifyingContract 选择规则)
//   handshake_cite:       docs/RESEARCH/laoli-laoSun-handshake-v1.md
//                         §84 SignedOrder ABI Hash 9c156025c5d86914 (11 字段顺序锁定)
//   goalserve_ssot_cite:  N/A (signer 不消费 Goalserve 数据)
//   adr_cite:             docs/ADR/2026-06-W4-adr-027-core-data-structure-ssot-enforce.md
//                         Enforce-1 (SSOT cite 强 enforce) + Enforce-2 (FOM 4 人 approve)
//
// 落:
//   laoSun-signer-v5.1-ipc-design.md §IPC协议 (msgpack request/response, 本文件保留结构)
//   laoli-laoSun-handshake-v1.md §3 (TimestampQuad 40B + 5 offset 锁定)
//   laoli-laoSun-handshake-v1.md §4.3 (DataSourceTsSource uint8 0-3 enum ABI lock)
//   laoli-laoSun-handshake-v1.md §84 (SignedOrder ABI 11 字段 + tokenId/side/signatureType)
//   laoli-polymarket-backend-requirements-v1.md §5 (HMAC 4 bug enforce)
//   laoshan-sop-v6.md §6 (Option A: FetchContent + ExternalProject_Add libsodium-1.0.20)
//   laosun-w9-signer-v53-abi-align-spec-v1.md §2/§3 (v5.3 字段 + EIP-712 binding)
//
// 红线:
//   R-1  signer 接 RiskDecision 才 sign (caller 责任 — signer 不重复评估)
//   R-7  paper/live/backtest 三 mode CMake 物理隔离:
//        paper  → Ed25519 mock (stcpp::crypto::Ed25519, 随机 keypair, 非真私钥)
//        live   → stub (M5+ secp256k1 真切)
//        backtest → stub
//   R-11 paper mode: secret_key 随机 mock, 非真私钥
//        audit_wal_kind 硬填 (paper→PaperAudit / live→RiskAudit / backtest→ShadowAudit)
//   R-20 4 ts UPSTREAM_PAYLOAD 优先; 入口 AssertChain, 违反 → SignV52Error::PitViolation
//   HMAC bug 4 教训 (laoSun-signer-v3-§A 永久 enforce):
//     BUG#1: rstrip 尾斜杠 — 禁 path 尾 '/'
//     BUG#2: sigType 误传 — signature_type 必须 = 1 (Magic Safe EOA 1-of-1)
//             v5.1 曾错填 2; v5.3 修正为 1 (SSOT §5 T-10, handshake §84)
//     BUG#3: base64 padding — base64 保留 '=' padding (live M5+ 层约束)
//     BUG#4: path 拼 querystring — 禁 url query 混入 canonical path
//   BUG-W5-001 防御: audit_id 16B 非零 (ULID rand 伪随机填充, M5+ 真 audit_id)
//
// IPC 协议 (v5.3 msgpack schema v1.3, 与老唐 audit v1.3 同步):
//   SignV52Request  → SignerV52::Sign() → SignV52Response
//   序列化: msgpack-cxx (FetchContent, W6 W3 加)
//   v1.3 新增字段: token_id (string) + side (uint8) + outcome (uint8, audit only)
//
// data_source_ts_source: uint8_t 0-3 (与老李 DataSourceTsSource enum ABI lock)
//   0 = UpstreamPayload       (PayloadScoresTs / PM WSS payload.timestamp)
//   1 = UpstreamHeader        (PayloadLastUpdate / HTTP Header Date)
//   2 = InferredFromDsTs      (InferredFromDsTs / batch 推断)
//   3 = InferredFromIngestion (InferredFromIngestion / 兜底本地 now)
//
// Ed25519 (paper mock): stcpp::crypto::Ed25519 (libsodium ExternalProject_Add)
//   detached 64B; paper 用随机 keypair (非真私钥 — R-11)
//   signature_type 必须 = 1 (Magic Safe EOA, HMAC bug #2 修正)

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "stcpp/crypto/ed25519.hpp"  // SecureBuffer<N> + Ed25519 (老沈 W7 ack)
#include "stcpp/execution/execution_mode.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/polymarket/pm_client.hpp"  // DataSourceTsSource ABI lock

namespace stcpp::signer::v52 {

// ---------- SignV52Error ----------

enum class SignV52Error : std::uint8_t {
    Ok = 0,
    PitViolation = 1,       // R-20 4 ts 顺序违反
    ModeMismatch = 2,       // R-7 paper signer 被 live mode 调用
    LibsodiumInitFail = 3,  // sodium_init() 返回 -1 (不应发生)
    InternalError = 4,
    InvalidSide = 5,    // v5.3 新增: side != 0/1 (0=Buy,1=Sell; 拒签)
    InvalidIntent = 6,  // v5.3 新增: INVALID_INTENT (缺 token_id / schema v1.3 不兼容)
};

[[nodiscard]] constexpr std::string_view ToString(SignV52Error e) noexcept {
    switch (e) {
        case SignV52Error::Ok:
            return "Ok";
        case SignV52Error::PitViolation:
            return "PitViolation";
        case SignV52Error::ModeMismatch:
            return "ModeMismatch";
        case SignV52Error::LibsodiumInitFail:
            return "LibsodiumInitFail";
        case SignV52Error::InternalError:
            return "InternalError";
        case SignV52Error::InvalidSide:
            return "InvalidSide";
        case SignV52Error::InvalidIntent:
            return "InvalidIntent";
    }
    return "unknown";
}

// ---------- HMAC bug 4 反模式 检测常量 (老孙 v3 §A 永久 enforce) ----------
// 这里不做 runtime 检查 — 由 caller 保证; signer 层记录教训供代码审查引用.
// BUG#1: url_path 不含尾斜杠 (rstrip 约定)
// BUG#2: signature_type 必须 = 1 (Magic Safe EOA 1-of-1) — NOT 2
//         v5.1 错误地 enforce == 2; v5.3 修正 (SSOT §5 T-10, handshake §84)
//         "sigType=1 forbidden" 是历史误读; Polymarket CTF EOA funder = sigType 1
// BUG#3: signature 必须 base64 保留 padding (= 号不 rstrip, live M5+ 层约束)
// BUG#4: canonical_path 禁混入 query string (?=&) — 路径与参数分离
static_assert(true, "HMAC_BUG_GUARD: see laoSun-signer-v3-sectionA + v53-spec §4.2 for enforce spec");

// ---------- SignV52Request ----------
//
// IPC v5.3 字段集 (msgpack schema v1.3, 与老唐 audit v1.3 同步).
// v5.3 新增字段: token_id / side / outcome (老韩 OrderIntent v0.5 直传).
// data_source_ts_source: uint8_t; 与老李 DataSourceTsSource ABI lock (0-3).
// signature_type: 1 = Magic Safe EOA (HMAC bug #2 修正; v5.1 曾错填 2).
// market_id: 32B hex string (CTF condition_id, 与小蒋 VirtualFill 对齐).
//
// ADR-027 cite:
//   token_id: SSOT §3.5 token_id + handshake §84 tokenId (uint256 decimal string)
//   side:     SSOT §3.5 side + handshake §84 side (0=BUY,1=SELL, 老韩 v0.5 对齐)
//   outcome:  SSOT §2.4 outcomeIndex — audit only, 不进 EIP-712

struct SignV52Request {
    // --- 4 ts R-20 (与老李 TimestampQuad ABI 对齐, 老孙 v5.1 设计) ---
    std::int64_t event_ts_ns{0};
    std::int64_t data_source_ts_ns{0};
    std::int64_t ingestion_ts_ns{0};
    std::int64_t as_of_ts_ns{0};

    // --- data_source_ts_source: uint8_t ABI lock (handshake §4.3) ---
    // 0=UpstreamPayload / 1=UpstreamHeader / 2=InferredFromDsTs / 3=InferredFromIngestion
    std::uint8_t data_source_ts_source{0};

    // --- 订单标识 (与小蒋 VirtualFill 对齐) ---
    std::string market_id;  // CTF condition_id (32B hex, ~66 chars with 0x)

    // --- v5.3 新增: CLOB 一等公民 token_id (ADR-027 Enforce-1 gap 修复) ---
    // uint256 decimal string (无 0x 前缀), 进 EIP-712 Order.tokenId (uint256 bignum)
    // cite: SSOT §3.5 token_id + handshake §84 tokenId
    // 老韩 v0.5 OrderIntent.token_id 直传, 不做任何 string 变换
    std::string token_id;  // e.g. "16772020035481685..."  (up to ~77 chars)

    // --- v5.3 新增: side (与老韩 v0.5 Side enum 对齐) ---
    // 0=Buy, 1=Sell; 进 EIP-712 Order.side (uint8); 其他值 → InvalidSide 拒签
    // cite: SSOT §3.5 side + handshake §84 side
    std::uint8_t side{0};

    // --- v5.3 新增: outcome (audit only, 不进 EIP-712) ---
    // 0=Yes,1=No,2=Over,3=Under,255=Unknown; 与老韩 v0.5 Outcome enum 一致
    // cite: SSOT §2.4 outcomeIndex; AET_ORDER_PLACED payload 携带
    std::uint8_t outcome{0};

    // --- EIP-712 标识 (HMAC bug #2 修正: 必须 = 1, Magic Safe EOA) ---
    // v5.1 错误地 enforce == 2; v5.3 修正 (SSOT §5 T-10, handshake §84)
    std::uint8_t signature_type{1};  // 1 = Magic Safe EOA; 2 = FORBIDDEN (HMAC bug #2)

    // --- audit 链路 ---
    std::array<std::uint8_t, 16> audit_id{};  // 16B non-zero (BUG-W5-001 防御)
    std::uint64_t intent_id{0};
};

// ABI 断言: data_source_ts_source 值域 0-3 与老李 DataSourceTsSource 一致
static_assert(static_cast<std::uint8_t>(polymarket::DataSourceTsSource::UpstreamPayload) == 0U,
              "v52 ABI lock: DataSourceTsSource::UpstreamPayload must be 0");
static_assert(static_cast<std::uint8_t>(polymarket::DataSourceTsSource::UpstreamHeader) == 1U,
              "v52 ABI lock: DataSourceTsSource::UpstreamHeader must be 1");
static_assert(static_cast<std::uint8_t>(polymarket::DataSourceTsSource::InferredFromDsTs) == 2U,
              "v52 ABI lock: DataSourceTsSource::InferredFromDsTs must be 2");
static_assert(static_cast<std::uint8_t>(polymarket::DataSourceTsSource::InferredFromIngestion) == 3U,
              "v52 ABI lock: DataSourceTsSource::InferredFromIngestion must be 3");

// ---------- SignV52Response ----------
//
// signature: Ed25519 detached 64B (paper mock); M5+ secp256k1 真切.
// audit_id: 16B non-zero (BUG-W5-001 防御, M5+ 真 ULID).
// audit_wal_kind: R-11 硬填 (paper→PaperAudit / live→RiskAudit / backtest→ShadowAudit).
// reject_reason: 拒签原因 string (与老沈 v0.5 OrderIntent 联动; 空串表示 Ok).
//   已知值: "invalid_side" / "invalid_signature_type" / "pit_violation"
//   cite: handshake §84 OrderAck.reject_reason

struct SignV52Response {
    SignV52Error error{SignV52Error::Ok};

    // Ed25519 detached signature (64B). paper: mock (libsodium real keygen + sign).
    // live: stub (M5+ secp256k1 真切). backtest: stub.
    std::vector<std::uint8_t> signature;  // 64B when Ok

    // R-20 4 ts 透传 (raw copy from request)
    std::int64_t event_ts_ns{0};
    std::int64_t data_source_ts_ns{0};
    std::int64_t ingestion_ts_ns{0};
    std::int64_t as_of_ts_ns{0};

    // R-11 audit WAL 路由
    infra::wal::WalKind audit_wal_kind{infra::wal::WalKind::PaperAudit};

    // audit 追踪
    std::array<std::uint8_t, 16> audit_id{};  // echo back from request (BUG-W5-001)

    // v5.3 新增: 拒签原因 (success=false 时填写, 与老沈 v0.5 联动)
    // cite: handshake §84 OrderAck.reject_reason
    std::string reject_reason;  // "invalid_side" / "invalid_signature_type" / ""
};

// ---------- SignerV52 ----------
//
// 同步 Sign(req) → SignV52Response.
// paper mode: Ed25519 mock (stcpp::crypto::Ed25519; 每次构造生成固定 0x42 seed keypair).
// live mode:  stub → SignV52Error::InternalError (M5+ secp256k1 真切).
// backtest:   stub → SignV52Error::InternalError.
//
// 线程安全: 构造后 Sign() 可多线程调用 (无 mutable state 除 const keypair bytes).
// 私钥: 仅存活于构造到析构; 析构时 SecureBuffer 自动 sodium_memzero 清零.

class SignerV52 {
public:
    // 构造: mode 决定 paper/live/backtest 行为 (R-7).
    // paper: 随机生成 Ed25519 keypair (mock, 非真私钥, 不从 .env 读 — R-11).
    // live/backtest: 不生成 keypair (stub).
    explicit SignerV52(execution::ExecutionMode mode);

    // 析构: sk_ (SecureBuffer) 析构链自动 sodium_memzero 清零
    ~SignerV52();

    // 禁止拷贝 (私钥是唯一资源, 不可 copy)
    SignerV52(const SignerV52&) = delete;
    SignerV52& operator=(const SignerV52&) = delete;
    // 允许 move (私钥所有权转移; SecureBuffer move 自动清零 source)
    SignerV52(SignerV52&&) noexcept;
    SignerV52& operator=(SignerV52&&) = delete;

    [[nodiscard]] execution::ExecutionMode Mode() const noexcept { return mode_; }

    // Sign: 入口 R-20 AssertChain; paper → Ed25519 mock; live/backtest → stub.
    // noexcept: 所有失败通过 SignV52Response.error 返回.
    [[nodiscard]] SignV52Response Sign(const SignV52Request& req) noexcept;

    // 取 Ed25519 公钥 (paper mode only; 用于 T8 公钥 mismatch 测试).
    // 返回 32B pubkey vector; live/backtest 返回空.
    [[nodiscard]] std::vector<std::uint8_t> PublicKeyBytes() const noexcept;

private:
    execution::ExecutionMode mode_;

    // Ed25519 keypair (paper mode only):
    //   sk_: SecureBuffer<64> — 析构时 sodium_memzero 自动清零 (老沈 W6 §6)
    //        W8 变更: 从 std::array<uint8_t,64> 改为 crypto::SecureBuffer<64>
    //   pk_: std::array<uint8_t,32> — 公钥, 不需要清零
    crypto::SecureBuffer<crypto::kEd25519SecretKeyBytes> sk_{};
    std::array<std::uint8_t, crypto::kEd25519PublicKeyBytes> pk_{};
    bool keypair_valid_{false};
};

}  // namespace stcpp::signer::v52
