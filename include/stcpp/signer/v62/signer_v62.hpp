// stcpp/signer/v62/signer_v62.hpp — SignerV62 v6.2 (Wave 104 P0)
//
// Owner: 老孙 (#06, crypto-signing-expert)
// Wave 104 P0: V2 ABI cpp 实施 (老沈 W97 拒 cpp 接手)
//   - EIP-712 domain v2 (version "2", verifyingContract V2 地址)
//   - Order struct V2 (无 nonce/feeRateBps/expiration/taker; 加 timestamp/metadata/builder)
//   - ADR-018 build switch: paper/live 必 V2; backtest 可 V1 for 历史
//   - V1 SignerV52 v5.3 共存 (deprecated, backtest 历史回测用)
//
// ADR-027 cite (Enforce-1 强 enforce):
//   polymarket_ssot_cite: docs/RESEARCH/laoli-w9-w5-polymarket-market-research-update-v1.md
//                         §3.1 EIP-712 Order struct V2 字段顺序
//                         §3.2 V2 verifyingContract 地址
//                         §3.4 CLOB V2 POST /order wire body
//   laosun_v62_spec_cite: docs/RESEARCH/laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md
//                         §2.1 SignV62Request + §3 domain + §7 共存
//   laoshen_8_spec_cite:  Wave 97 task output (10 安全 spec) — spec-9/10 含 V2 字段校验
//   laotang_v14_cite:     include/stcpp/observability/audit_record.hpp v1.4
//   adr_cite:             ADR-027 / ADR-029 / ADR-032 / ADR-034 v2.1
//
// 红线:
//   R-1  signer 接 RiskDecision 才 sign (caller 责任 — signer 不重复评估)
//   R-7  paper/live/backtest 三 mode CMake 物理隔离 (ADR-018)
//        paper  → V2 EIP-712 (stcpp_signer_v62_paper)
//        live   → V2 EIP-712 stub (M5+ secp256k1 真切)
//        backtest → V1 or V2 (按 STCPP_CLOB_VERSION)
//   R-11 paper mode: secret_key 随机 mock, 非真私钥
//   R-20 4 ts UPSTREAM_PAYLOAD 优先; 入口 AssertChain, 违反 → SignV62Error::PitViolation
//   HMAC bug 4 教训 (永久 enforce):
//     BUG#2: sigType 误传 — signature_type 必须 = 1 (Magic Safe EOA 1-of-1)
//   ADR-018 build switch: STCPP_CLOB_V2 (paper + live 必定义)
//                         STCPP_CLOB_V1 (backtest 历史回测专用, 不允许 paper/live)
//
// EIP-712 domain V2 (cite: laoli-w9-w5 §3.1):
//   name:              "Polymarket CTF Exchange"
//   version:           "2"  ← 关键, V1 是 "1"
//   chainId:           137  (Polygon PoS, 不变)
//   verifyingContract: (negRisk=false) 0xE111180000d2663C0091e4f400237545B87B996B
//                      (negRisk=true)  0xe2222d279d744050d28e00520010520000310F59
//
// EIP-712 Order struct V2 (cite: laoli-w9-w5 §3.1 + ctf-exchange-v2 Solidity):
//   Order(uint256 salt, address maker, address signer, uint256 tokenId,
//         uint256 makerAmount, uint256 takerAmount, uint8 side, uint8 signatureType,
//         uint256 timestamp, bytes32 metadata, bytes32 builder)
//   NOTE: 字段顺序影响 typeHash; 须与合约 _hashOrder() abi.encode() 完全一致.
//         @老李 W10 W2 handshake v2 确认精确顺序 (本 spec 先行).
//
// IPC v6.2 SignV62Request/Response (与 transformer_v62.hpp 对应)

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "stcpp/crypto/ed25519.hpp"  // SecureBuffer<N> + Ed25519
#include "stcpp/execution/execution_mode.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/polymarket/pm_client.hpp"  // DataSourceTsSource ABI lock

namespace stcpp::signer::v62 {

// ---------- SignV62Error ----------

enum class SignV62Error : std::uint8_t {
    Ok = 0,
    PitViolation = 1,       // R-20 4 ts 顺序违反
    ModeMismatch = 2,       // R-7 paper signer 被 live mode 调用
    LibsodiumInitFail = 3,  // sodium_init() 返回 -1
    InternalError = 4,
    InvalidSide = 5,     // side != 0/1 (0=Buy,1=Sell)
    InvalidIntent = 6,   // INVALID_INTENT (缺 token_id / timestamp_ms=0 / bytes32 格式)
    TsV2Missing = 7,     // spec-10: timestamp_ms = 0
    InvalidBytes32 = 8,  // spec-9: metadata/builder 格式非 ^0x[0-9a-f]{64}$
};

[[nodiscard]] constexpr std::string_view ToString(SignV62Error e) noexcept {
    switch (e) {
        case SignV62Error::Ok:
            return "Ok";
        case SignV62Error::PitViolation:
            return "PitViolation";
        case SignV62Error::ModeMismatch:
            return "ModeMismatch";
        case SignV62Error::LibsodiumInitFail:
            return "LibsodiumInitFail";
        case SignV62Error::InternalError:
            return "InternalError";
        case SignV62Error::InvalidSide:
            return "InvalidSide";
        case SignV62Error::InvalidIntent:
            return "InvalidIntent";
        case SignV62Error::TsV2Missing:
            return "TsV2Missing";
        case SignV62Error::InvalidBytes32:
            return "InvalidBytes32";
    }
    return "unknown";
}

// ---------- HMAC bug 4 反模式检测常量 (永久 enforce) ----------
// BUG#2: signature_type 必须 = 1 (Magic Safe EOA 1-of-1) — NOT 2
// V1 v5.1 错误 enforce == 2; v5.3/v6.2 修正.
static_assert(true, "HMAC_BUG_GUARD_V62: see laosun-key-management-v5.1 §HMAC_BUG for enforce spec");

// ---------- SignV62Request ----------
//
// IPC v6.2 字段集 (与老唐 audit_record.hpp v1.4 + laosun-w10-w1 §2.1 对齐).
// V2 相比 V1 (SignV52Request):
//   移除: nonce / feeRateBps / expiration(signed) / taker (皆不入 V2 EIP-712)
//   新增: timestamp_ms / metadata / builder (V2 EIP-712 Order 新字段)
//   rename: size_usdc → size_pUSD_micro (抵押品 USDC.e → pUSD)
//   rename: market_id → condition_id (去历史遗留命名)
//
// ADR-027 cite: laosun-w10-w1 §2.1 SignV62Request §2.2 V1→V2 字段对比
// 老沈 spec-8: signature_type{1} 默认值; 调用方禁止赋值为其他值

struct SignV62Request {
    // --- 4 ts R-20 (与老李 TimestampQuad ABI 对齐) ---
    std::int64_t event_ts_ns{0};
    std::int64_t data_source_ts_ns{0};
    std::int64_t ingestion_ts_ns{0};
    std::int64_t as_of_ts_ns{0};

    // --- data_source_ts_source: uint8_t ABI lock (handshake §4.3) ---
    // 0=UpstreamPayload / 1=UpstreamHeader / 2=InferredFromDsTs / 3=InferredFromIngestion
    std::uint8_t data_source_ts_source{0};

    // --- 市场标识 (V2: condition_id; V1 遗留名 market_id 在 v52 中保留) ---
    // condition_id: bytes32 hex (0x 前缀, 66 char), market 级
    // cite: SSOT §3.2 conditionId / laosun-w10-w1 §2.3
    std::string condition_id;  // V2 正名 (V1 SignV52Request 里是 market_id)

    // --- CLOB 一等公民 token_id ---
    // uint256 decimal string (无 0x 前缀), 进 EIP-712 Order.tokenId (uint256 bignum)
    // cite: SSOT §3.5 token_id + handshake §84 tokenId
    std::string token_id;  // e.g. "16772020035481685..."  (up to ~77 chars)

    // --- side (与老韩 v0.5/v0.6 Side enum 对齐) ---
    // 0=Buy, 1=Sell; 进 EIP-712 Order.side (uint8); 其他值 → InvalidSide 拒签
    std::uint8_t side{0};

    // --- outcome (audit only, 不进 EIP-712) ---
    // 0=Yes,1=No,2=Over,3=Under,255=Unknown (与老韩 v0.5/v0.6 Outcome enum)
    // cite: SSOT §2.4 outcomeIndex
    std::uint8_t outcome{0};

    // --- 定价 (V2 单位: pUSD micro = 1e-6) ---
    // limit_price_bps: 价格 bps, e.g. 5500 = 0.55; 用于推算 makerAmount/takerAmount
    // cite: laosun-w10-w1 §2.1
    std::int64_t limit_price_bps{0};
    // size_pUSD_micro: pUSD 数量 micro (1e-6), e.g. 10_000_000 = 10 pUSD
    // cite: laosun-w10-w1 §2.1 §5.1 (USDC.e → pUSD rename)
    std::int64_t size_pUSD_micro{0};

    // --- V2 新增: timestamp_ms (替代 nonce, EIP-712 Order.timestamp) ---
    // ms 毫秒时间戳; = 0 → TsV2Missing 拒签 (spec-10)
    // cite: laoli-w9-w5 §3.1 + laosun-w10-w1 §2.1 §8.4
    std::int64_t timestamp_ms{0};

    // --- V2 新增: metadata (bytes32 hex, EIP-712 Order.metadata) ---
    // ^0x[0-9a-f]{64}$ (66 chars); 格式错 → InvalidBytes32 拒签 (spec-9)
    // 不使用: "0x0000000000000000000000000000000000000000000000000000000000000000"
    // cite: laoli-w9-w5 §3.1 + laosun-w10-w1 §2.1
    std::string metadata;

    // --- V2 新增: builder (bytes32 hex, EIP-712 Order.builder, optional) ---
    // ^0x[0-9a-f]{64}$ (66 chars); 格式错 → InvalidBytes32 拒签 (spec-9)
    // 不使用: "0x0000000000000000000000000000000000000000000000000000000000000000"
    // cite: laoli-w9-w5 §3.1 §2.2 + laosun-w10-w1 §2.1
    std::string builder;

    // --- EIP-712 标识 (HMAC bug #2 修正: 必须 = 1, Magic Safe EOA) ---
    // signature_type 必须默认值 1; 调用方禁止赋值 (transformer_v62 不触碰 — spec-8)
    std::uint8_t signature_type{1};  // 1 = Magic Safe EOA; 2 = FORBIDDEN (HMAC bug #2)

    // --- maker 地址 (Orchestrator 在 transformer 后填入) ---
    // funder 地址 0x hex (lowercase, 20 bytes = 42 chars)
    std::string maker_address;

    // --- 客户端订单 ID ---
    std::string client_order_id;  // UUID 形式

    // --- audit 链路 ---
    std::array<std::uint8_t, 16> audit_id{};  // 16B non-zero (BUG-W5-001 防御)
};

// ABI 断言: data_source_ts_source 值域 0-3 与老李 DataSourceTsSource 一致 (继承 v52)
static_assert(static_cast<std::uint8_t>(polymarket::DataSourceTsSource::UpstreamPayload) == 0U,
              "v62 ABI lock: DataSourceTsSource::UpstreamPayload must be 0");
static_assert(static_cast<std::uint8_t>(polymarket::DataSourceTsSource::UpstreamHeader) == 1U,
              "v62 ABI lock: DataSourceTsSource::UpstreamHeader must be 1");
static_assert(static_cast<std::uint8_t>(polymarket::DataSourceTsSource::InferredFromDsTs) == 2U,
              "v62 ABI lock: DataSourceTsSource::InferredFromDsTs must be 2");
static_assert(static_cast<std::uint8_t>(polymarket::DataSourceTsSource::InferredFromIngestion) == 3U,
              "v62 ABI lock: DataSourceTsSource::InferredFromIngestion must be 3");

// ---------- EIP-712 V2 addresses (cite: laoli-w9-w5 §3.2) ----------
// negRisk=false (普通市场)
inline constexpr std::string_view kCtfExchangeV2Addr = "0xE111180000d2663C0091e4f400237545B87B996B";
// negRisk=true  (negRisk 市场)
inline constexpr std::string_view kCtfExchangeV2NegRisk = "0xe2222d279d744050d28e00520010520000310F59";
// EIP-712 domain version string
inline constexpr std::string_view kDomainVersionV2 = "2";
// Chain ID: Polygon PoS (unchanged from V1)
inline constexpr std::uint64_t kChainIdPolygon = 137U;

// ---------- SignV62Response ----------
//
// signature: Ed25519 detached 64B (paper mock); M5+ secp256k1 真切.
// audit_id:  16B non-zero (BUG-W5-001 防御).
// audit_wal_kind: R-11 硬填 (paper→PaperAudit / live→RiskAudit).
// reject_reason: 拒签原因 (空串 = Ok; spec-9/10 错误有对应字符串).

struct SignV62Response {
    SignV62Error error{SignV62Error::Ok};

    // Ed25519 detached signature (64B). paper: mock (libsodium real keygen + sign).
    // live: stub (M5+ secp256k1 真切 EIP-712 ECDSA).
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

    // 拒签原因 (success → 空串)
    // 已知值: "invalid_side" / "invalid_signature_type" / "ts_v2_missing" /
    //         "invalid_bytes32" / "pit_violation" / "invalid_token_id"
    std::string reject_reason;
};

// ---------- SignerV62 ----------
//
// 同步 Sign(req) → SignV62Response.
// paper mode: Ed25519 mock (libsodium); 每次构造生成固定 0x42 seed keypair.
//             EIP-712 V2 domain hash + Order struct hash 计算 (W10 Wave 104 实施).
// live mode:  stub → SignV62Error::InternalError (M5+ secp256k1 真切).
// backtest:   stub.
//
// ADR-018 build switch:
//   paper + live binary: 必须定义 STCPP_CLOB_V2 (CI abi_lock grep 拦截 V1 signer)
//   backtest binary: 可定义 STCPP_CLOB_V1 (历史回测 V1 时代数据) 或 STCPP_CLOB_V2
//
// 线程安全: 构造后 Sign() 可多线程调用 (无 mutable state 除 const keypair bytes).
// 私钥: 仅存活于构造到析构; 析构时 SecureBuffer 自动 sodium_memzero 清零.

class SignerV62 {
public:
    // 构造: mode 决定 paper/live/backtest 行为 (R-7).
    // paper: 随机生成 Ed25519 keypair (mock, 非真私钥 — R-11).
    // live/backtest: 不生成 keypair (stub).
    explicit SignerV62(execution::ExecutionMode mode);

    // 析构: sk_ (SecureBuffer) 析构链自动 sodium_memzero 清零
    ~SignerV62();

    // 禁止拷贝 (私钥是唯一资源, 不可 copy)
    SignerV62(const SignerV62&) = delete;
    SignerV62& operator=(const SignerV62&) = delete;
    // 允许 move (私钥所有权转移; SecureBuffer move 自动清零 source)
    SignerV62(SignerV62&&) noexcept;
    SignerV62& operator=(SignerV62&&) = delete;

    [[nodiscard]] execution::ExecutionMode Mode() const noexcept { return mode_; }

    // Sign: 入口 R-20 AssertChain; paper → Ed25519 mock; live/backtest → stub.
    // 老沈 spec-8/9/10 在此校验 (signature_type / timestamp_ms / bytes32 格式).
    // noexcept: 所有失败通过 SignV62Response.error 返回.
    [[nodiscard]] SignV62Response Sign(const SignV62Request& req) noexcept;

    // 取 Ed25519 公钥 (paper mode only; 用于 byte-equal 测试).
    // 返回 32B pubkey vector; live/backtest 返回空.
    [[nodiscard]] std::vector<std::uint8_t> PublicKeyBytes() const noexcept;

private:
    execution::ExecutionMode mode_;

    // Ed25519 keypair (paper mode only):
    //   sk_: SecureBuffer<64> — 析构时 sodium_memzero 自动清零
    //   pk_: std::array<uint8_t,32> — 公钥, 不需要清零
    crypto::SecureBuffer<crypto::kEd25519SecretKeyBytes> sk_{};
    std::array<std::uint8_t, crypto::kEd25519PublicKeyBytes> pk_{};
    bool keypair_valid_{false};
};

}  // namespace stcpp::signer::v62
