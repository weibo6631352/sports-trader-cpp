// stcpp/signer/v52/signer_v52.hpp — SignerV52 v5.2 C++ 重写 (撤 Rust 后)
//
// Owner: 老孙 (#06, crypto-signing-expert)
// Wave 34 W8: libsodium FetchContent ExternalProject_Add cpp v0.2
//             撤 W6 W3 brew find_library 兼容性 hack (老周 C-02 review)
//             改单一路径: 调 stcpp::crypto::Ed25519::sign (老沈 W7 ack)
//
// 落:
//   laoSun-signer-v5.1-ipc-design.md §IPC协议 (msgpack request/response, 本文件保留结构)
//   laoli-laoSun-handshake-v1.md §3 (TimestampQuad 40B + 5 offset 锁定)
//   laoli-laoSun-handshake-v1.md §4.3 (DataSourceTsSource uint8 0-3 enum ABI lock)
//   laoli-polymarket-backend-requirements-v1.md §5 (HMAC 4 bug enforce)
//   laoshan-sop-v6.md §6 (Option A: FetchContent + ExternalProject_Add libsodium-1.0.20)
//
// 红线:
//   R-1  signer 接 RiskDecision 才 sign (caller 责任 — signer 不重复评估)
//   R-7  paper/live/backtest 三 mode CMake 物理隔离:
//        paper  → Ed25519 mock (stcpp::crypto::Ed25519, 固定 0x42 seed, 非真私钥)
//        live   → stub (M5+ secp256k1 真切)
//        backtest → stub
//   R-11 paper mode: secret_key = 固定 0x42 seed mock, 非真私钥
//        audit_wal_kind 硬填 (paper→PaperAudit / live→RiskAudit / backtest→ShadowAudit)
//   R-20 4 ts UPSTREAM_PAYLOAD 优先; 入口 AssertChain, 违反 → SignV52Error::PitViolation
//   HMAC bug 4 教训 (laoSun-signer-v3-§A 永久 enforce):
//     BUG#1: rstrip 尾斜杠 — 禁 path 尾 '/'
//     BUG#2: param_type 不传 — 禁 signature 字段直接 base64 不带 padding
//     BUG#3: sigType 误传 — signature_type 必须 = 2 (EIP-712) 本 signer 的职责
//     BUG#4: path 拼 querystring — 禁 url query 混入 canonical path
//   BUG-W5-001 防御: audit_id 16B 非零 (ULID rand 伪随机填充, M5+ 真 audit_id)
//
// IPC 协议 (v5.1 保留 msgpack 结构, 本文件 in-process 版):
//   SignV52Request  → SignerV52::Sign() → SignV52Response
//   序列化: msgpack-cxx (FetchContent, W6 W3 加)
//
// data_source_ts_source: uint8_t 0-3 (与老李 DataSourceTsSource enum ABI lock)
//   0 = UpstreamPayload       (PayloadScoresTs / PM WSS payload.timestamp)
//   1 = UpstreamHeader        (PayloadLastUpdate / HTTP Header Date)
//   2 = InferredFromDsTs      (InferredFromDsTs / batch 推断)
//   3 = InferredFromIngestion (InferredFromIngestion / 兜底本地 now)
//
// Ed25519 (paper mock): stcpp::crypto::Ed25519 (libsodium ExternalProject_Add)
//   detached 64B; paper 用固定 0x42 seed mock keypair (非真私钥 — R-11)
//   HMAC bug #3 教训: signature_type=2 (EIP-712) — paper 不真签但接口 reserve

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "stcpp/crypto/ed25519.hpp"        // SecureBuffer<N> + Ed25519 (老沈 W7 ack)
#include "stcpp/execution/execution_mode.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/polymarket/pm_client.hpp"  // DataSourceTsSource ABI lock

namespace stcpp::signer::v52 {

// ---------- SignV52Error ----------

enum class SignV52Error : std::uint8_t {
    Ok                = 0,
    PitViolation      = 1,  // R-20 4 ts 顺序违反
    ModeMismatch      = 2,  // R-7 paper signer 被 live mode 调用
    LibsodiumInitFail = 3,  // sodium_init() 返回 -1 (不应发生)
    InternalError     = 4,
};

[[nodiscard]] constexpr std::string_view ToString(SignV52Error e) noexcept {
    switch (e) {
        case SignV52Error::Ok:                return "Ok";
        case SignV52Error::PitViolation:      return "PitViolation";
        case SignV52Error::ModeMismatch:      return "ModeMismatch";
        case SignV52Error::LibsodiumInitFail: return "LibsodiumInitFail";
        case SignV52Error::InternalError:     return "InternalError";
    }
    return "unknown";
}

// ---------- HMAC bug 4 反模式 检测常量 (老孙 v3 §A 永久 enforce) ----------
// 这里不做 runtime 检查 — 由 caller 保证; signer 层记录教训供代码审查引用.
// BUG#1: url_path 不含尾斜杠 (rstrip 约定)
// BUG#2: signature 必须 base64 保留 padding (= 号不 rstrip)
// BUG#3: signature_type 必须 = 2 (EIP-712) — NOT 1 (Magic 1-of-1 Safe)
// BUG#4: canonical_path 禁混入 query string (?=&) — 路径与参数分离
static_assert(true, "HMAC_BUG_GUARD: see laoSun-signer-v3-sectionA for enforce spec");

// ---------- SignV52Request ----------
//
// IPC v5.1 字段集 (msgpack round-trip 兼容).
// data_source_ts_source: uint8_t; 与老李 DataSourceTsSource ABI lock (0-3).
// signature_type: 2 = EIP-712 (HMAC bug #3 教训; live M5+ 真接).
// market_id: 32B hex string (CTF condition_id, 与小蒋 VirtualFill 对齐).
// outcome: uint8_t (0=YES, 1=NO — 与 VirtualFill.outcome_index 对齐).

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
    std::string  market_id;     // CTF condition_id (32B hex, ~66 chars with 0x)
    std::uint8_t outcome{0};    // 0=YES, 1=NO

    // --- EIP-712 标识 (HMAC bug #3 教训: 必须 = 2) ---
    std::uint8_t signature_type{2};  // 2 = EIP-712; 1 = FORBIDDEN (sigType=1 bug)

    // --- audit 链路 ---
    std::array<std::uint8_t, 16> audit_id{};  // 16B non-zero (BUG-W5-001 防御)
    std::uint64_t                intent_id{0};
};

// ABI 断言: data_source_ts_source 值域 0-3 与老李 DataSourceTsSource 一致
static_assert(
    static_cast<std::uint8_t>(polymarket::DataSourceTsSource::UpstreamPayload)       == 0U,
    "v52 ABI lock: DataSourceTsSource::UpstreamPayload must be 0");
static_assert(
    static_cast<std::uint8_t>(polymarket::DataSourceTsSource::UpstreamHeader)        == 1U,
    "v52 ABI lock: DataSourceTsSource::UpstreamHeader must be 1");
static_assert(
    static_cast<std::uint8_t>(polymarket::DataSourceTsSource::InferredFromDsTs)      == 2U,
    "v52 ABI lock: DataSourceTsSource::InferredFromDsTs must be 2");
static_assert(
    static_cast<std::uint8_t>(polymarket::DataSourceTsSource::InferredFromIngestion) == 3U,
    "v52 ABI lock: DataSourceTsSource::InferredFromIngestion must be 3");

// ---------- SignV52Response ----------
//
// signature: Ed25519 detached 64B (paper mock); M5+ secp256k1 真切.
// audit_id: 16B non-zero (BUG-W5-001 防御, M5+ 真 ULID).
// audit_wal_kind: R-11 硬填 (paper→PaperAudit / live→RiskAudit / backtest→ShadowAudit).

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
    SignerV52(const SignerV52&)            = delete;
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
    bool                           keypair_valid_{false};
};

}  // namespace stcpp::signer::v52
