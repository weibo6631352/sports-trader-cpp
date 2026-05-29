// stcpp/signer/signer_iface.hpp — Signer / Nonce / Gas / Confirm 抽象接口
//
// 落:
//   xiaojiang-paper-engine-skeleton-v1.md §3 (3 mock 接口 + SignRequest/Response)
//   laozhou-trade-orchestrator-cpp-v0.5.md §18 (ExecutionMode.Paper 4 接口)
//   laohan-riskmanager-design-v0.3.1.md (RiskGateway::evaluate 上游, signer 在 gate 后)
//
// Wave 72 P0: to_sign_request() transformer 落在 transformer.hpp (同目录)
//   理由: signer_iface.hpp 被 stcpp_signer_paper 链 (不链 libsodium), 不能引 signer_v52.hpp.
//   用法: #include "stcpp/signer/transformer.hpp" (包含 to_sign_request inline noexcept)
//
// Wave 97 ABI V2 (老沈, L2 三方签: 老李 + 老孙 + GM 老雷):
//   SignRequest 字段升级至 OrderIntent v0.6 语义:
//     market_id → condition_id (bytes32 hex)
//     outcome: string_view (YES/NO) → token_id: string_view (uint256 decimal, 无 0x 前缀)
//     size_usdc: double → size_pUSD_micro: int64_t (pUSD micro 单位)
//     side: uint8_t 新增 (0=Buy/1=Sell, EIP-712 Order.side)
//     timestamp_ms: int64_t 新增 (EIP-712 Order.timestamp, spec-10: 必须 != 0)
//     metadata: string_view 新增 (bytes32 hex ^0x[0-9a-f]{64}$, EIP-712 Order.metadata)
//     builder: string_view 新增 (bytes32 hex ^0x[0-9a-f]{64}$, EIP-712 Order.builder, optional)
//   ABI ref: docs/RESEARCH/laoli-laoSun-handshake-v1.md F-02 L2
//   三方签: 老李 + 老孙 + GM 老雷 已会签
//
// 红线:
//   R-7  Live / Paper / Backtest 三 binary 物理隔离; 本文件只放 abstract 接口,
//        实现走 src/stcpp/signer/{paper,live,backtest}/ 独立 CMake target.
//        Live / Backtest stub 不出现在 Paper binary (CMake link 时挑 target).
//   R-11 paper_* 数据严禁污染 position / pnl_ledger / nonce_ledger; SignResponse.audit_wal_kind
//        强制标 PaperAudit (paper signer 内嵌 invariant, live signer 标 RiskAudit/Position).
//   R-12 signer 不在 WSS event loop 线程; 接口契约由 caller (orchestrator) 保证.
//   R-20 SignRequest / SignResponse 全程携带 4 ts (event / data_source / ingestion / as_of),
//        signer 入口 assert chain (event ≤ ds ≤ ingest ≤ as_of), violation = 立即拒.
//   R-R20-01 timestamp_ms > 0 强制 (= 0 → RM check_invalid_intent_ 拒 TS_V2_MISSING);
//        caller (Orchestrator) 负责填入; signer 入口 > 0 前置校验.

#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "stcpp/execution/execution_mode.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"

namespace stcpp::signer {

enum class SignerError : std::uint8_t {
    Ok = 0,
    PitViolation = 1,      // R-20 4 ts 顺序违反 → caller REJECT(INVALID_INTENT.sub=TS_*)
    NonceUnavailable = 2,  // nonce provider 空 (理论上 paper 不会触发)
    GasEstimateFailed = 3,
    ConfirmTimeout = 4,
    ModeMismatch = 5,  // 用 paper signer 跑 live mode (R-7 防御)
    InternalError = 6,
};

[[nodiscard]] constexpr std::string_view ToString(SignerError e) noexcept {
    switch (e) {
        case SignerError::Ok:
            return "Ok";
        case SignerError::PitViolation:
            return "PitViolation";
        case SignerError::NonceUnavailable:
            return "NonceUnavailable";
        case SignerError::GasEstimateFailed:
            return "GasEstimateFailed";
        case SignerError::ConfirmTimeout:
            return "ConfirmTimeout";
        case SignerError::ModeMismatch:
            return "ModeMismatch";
        case SignerError::InternalError:
            return "InternalError";
    }
    return "unknown";
}

// ---------- SignRequest / SignResponse ----------
//
// 4 ts (R-20) 由 caller 注入; signer 入口 AssertChain. 单测用 NowRealtimeNs() 灌 as_of.

struct SignRequest {
    // intent 标识 (caller fill, signer 不算)
    std::array<std::uint8_t, 16> audit_id{};
    std::uint64_t intent_id{0};  // RM 出的 dedup key

    // 订单参数 (V2: OrderIntent v0.6 对齐, ABI V2 Wave 97)
    // condition_id: bytes32 hex (0x 前缀, 66 char), market 级
    // cite: SSOT §3.2 conditionId; V1 名 market_id 已废弃
    std::string_view condition_id{};

    // token_id: uint256 decimal string (无 0x 前缀), outcome 级主键
    // 进 EIP-712 Order.tokenId; spec-1 零变换 (禁 trim/normalize/pad)
    // cite: SSOT §3.5 token_id + handshake §84 tokenId
    // 注: V1 字段 outcome (string "YES"/"NO") 已替换为 token_id
    std::string_view token_id{};

    // price: 价格 [0,1] (limit_price_bps 由 signer_v62 层推算)
    double price{0.0};

    // size_pUSD_micro: pUSD 数量 micro (1e-6 单位), e.g. 10_000_000 = 10 pUSD
    // V1 字段 size_usdc: double 已替换; 使用 int64_t 保持精度
    // cite: laosun-w10-w1 §2.1 §5.1 (USDC.e → pUSD rename)
    std::int64_t size_pUSD_micro{0};

    // side: 0=Buy / 1=Sell; 进 EIP-712 Order.side
    // cite: SSOT §3.5 side + handshake §84 side
    std::uint8_t side{0};

    // timestamp_ms: EIP-712 Order.timestamp (毫秒), 替代 nonce
    // R-R20-01: 必须 > 0 (= 0 → RM 拒 TS_V2_MISSING); caller 填入, signer 前置校验
    // cite: laoli-w9-w5 §3.1 + laosun-w10-w1 §2.1 §8.4
    std::int64_t timestamp_ms{0};

    // metadata: bytes32 hex (^0x[0-9a-f]{64}$, 66 char), EIP-712 Order.metadata
    // 不使用时填 "0x0000...0000"; 格式错 → signer 拒 (spec-9)
    // cite: laoli-w9-w5 §3.1 + laosun-w10-w1 §2.1
    std::string_view metadata{};

    // builder: bytes32 hex (^0x[0-9a-f]{64}$, 66 char), EIP-712 Order.builder (optional)
    // 不使用时填 "0x0000...0000"; 格式错 → signer 拒 (spec-9)
    // cite: laoli-w9-w5 §3.1 §2.2 + laosun-w10-w1 §2.1
    std::string_view builder{};

    // R-20 PIT 4 ts (原样, 禁 now() 替代)
    std::int64_t event_ts_ns{0};
    std::int64_t data_source_ts_ns{0};
    std::int64_t ingestion_ts_ns{0};
    std::int64_t as_of_ts_ns{0};
};

struct SignResponse {
    SignerError error{SignerError::Ok};
    std::array<std::uint8_t, 32> signature{};  // mock sig in paper mode (deterministic)
    std::uint64_t nonce{0};
    std::uint64_t gas_estimate{0};

    // confirm phase
    std::int64_t confirm_ts_ns{0};  // virtual block ts (paper: as_of + ~2s + jitter)
    std::uint64_t block_number{0};  // virtual

    // R-11: 落审计走哪条 WAL (paper signer 必填 PaperAudit)
    infra::wal::WalKind audit_wal_kind{infra::wal::WalKind::PaperAudit};

    // R-20 PIT 4 ts (raw copy from request + 出口 ts)
    std::int64_t event_ts_ns{0};
    std::int64_t data_source_ts_ns{0};
    std::int64_t ingestion_ts_ns{0};
    std::int64_t as_of_ts_ns{0};
};

// ---------- 抽象基类 (mode-specific 实现在 paper/live/backtest 子目录) ----------

class INonceProvider {
public:
    virtual ~INonceProvider() = default;
    [[nodiscard]] virtual std::uint64_t Next() noexcept = 0;
};

class IGasEstimator {
public:
    virtual ~IGasEstimator() = default;
    [[nodiscard]] virtual std::uint64_t Estimate(const SignRequest& req) noexcept = 0;
};

class IConfirmWatcher {
public:
    virtual ~IConfirmWatcher() = default;
    // 同步等待 (paper 模拟 ~2s polygon block); 返回 virtual block ts (ns) + block_number.
    struct ConfirmResult {
        SignerError error{SignerError::Ok};
        std::int64_t confirm_ts_ns{0};
        std::uint64_t block_number{0};
    };
    [[nodiscard]] virtual ConfirmResult Wait(std::uint64_t nonce, std::int64_t submit_ts_ns) noexcept = 0;
};

// Signer 基类 (Live / Paper / Backtest 同形 API, 三 binary 各选一)
class ISigner {
public:
    virtual ~ISigner() = default;

    [[nodiscard]] virtual execution::ExecutionMode Mode() const noexcept = 0;

    // 同步 sign + (paper) virtual confirm. Returns SignResponse 携带 4 ts + WalKind 标签.
    // 入口 R-20 AssertChain; 失败 → SignerError::PitViolation, caller 走 RM REJECT.
    [[nodiscard]] virtual SignResponse Sign(const SignRequest& req) noexcept = 0;
};

// 三 mode 接口标签 (caller 静态选; CMake link 时只选一个 impl)
class IPaperSigner : public ISigner {};
class ILiveSigner : public ISigner {};
class IBacktestSigner : public ISigner {};

}  // namespace stcpp::signer
