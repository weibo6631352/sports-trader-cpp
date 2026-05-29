// stcpp/execution/virtual_matcher.hpp — VirtualMatcher (paper mode 虚拟撮合)
//
// 落:
//   xiaojiang-paper-engine-skeleton-v1.md §4 (Mode A++ Bernoulli sampler)
//   xiaoxiao-slippage-model-lib-v1.md §1 (SlippageModel.compute)
//   xiaoyuan-microstructure-v1   (KAPPA_DEPTH_GAMEDAY, fill_rate floor/cap)
//
// 红线:
//   R-7  Paper binary 专用; live binary 不 link.
//   R-11 VirtualFill 走 paper_audit.wal (caller 落), 严禁触碰 position / pnl_ledger.
//   R-20 VirtualFill 携带 4 ts (从 SignResponse 透传 + fill_ts_ns).
//
// Mode A++ Bernoulli:
//   p_fill = clamp(SlippageModel.expected_fill_rate, FLOOR=0.50, CAP=0.65)
//   draw   ~ Bernoulli(p_fill)
//   draw=1 → VirtualFill { price = expected_fill_price, size = req.size_usdc * fill_size_ratio }
//   draw=0 → VirtualFill { fill_size = 0, reject = LowFillRate }

#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <string_view>

#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/numerical/slippage_model.hpp"

namespace stcpp::execution {

// Mode A++ floor / cap (小袁 microstructure v1)
inline constexpr double kFillRateFloor = 0.50;
inline constexpr double kFillRateCap = 0.65;

enum class MatchReject : std::uint8_t {
    Ok = 0,
    SlippageModelReject = 1,  // SlippageModel 已拒 (INVALID_INTENT / ExceedBookDepth / FillRateBelowFloor)
    BernoulliMissed = 2,      // Bernoulli draw=0
    InvalidConfig = 3,
};

struct VirtualOrder {
    std::array<std::uint8_t, 16> audit_id{};
    std::uint64_t intent_id{0};
    std::string_view market_id{};
    std::string_view outcome{};
    double size_usdc{0.0};

    // 来自 RM gateway 通过后的 book snapshot (SlippageModel 输入)
    double quote_price{0.0};
    double book_depth_l1_usdc{0.0};
    double tick_size{0.01};

    // R-20 4 ts (从 RM/Signer 透传)
    std::int64_t event_ts_ns{0};
    std::int64_t data_source_ts_ns{0};
    std::int64_t ingestion_ts_ns{0};
    std::int64_t as_of_ts_ns{0};

    // SlippageModel 用; caller 注 wall_now_ns
    std::int64_t wall_now_ns{0};
};

struct VirtualFill {
    MatchReject reject{MatchReject::Ok};

    double fill_price{0.0};      // VWAP, SlippageModel 出
    double fill_size_usdc{0.0};  // = order.size_usdc * effective_fill_rate (or 0)
    double expected_fill_rate{0.0};
    double p_fill_clamped{0.0};  // floor/cap 后的 Bernoulli 参数
    std::int32_t slippage_bps{0};

    // Bernoulli draw (单测可 reproduce)
    bool bernoulli_draw{false};

    // R-11 审计目标 (硬填 PaperAudit)
    infra::wal::WalKind audit_wal_kind{infra::wal::WalKind::PaperAudit};

    // market_id / outcome — 从 VirtualOrder 透传 (W6 @小蒋)
    // market_id: 32B null-padded, 与 PolymarketClient::Position.market_id / PositionRecord 对齐
    // outcome: 0=YES, 1=NO (与 OrderStatus 协议 + PositionRecord.outcome 对齐)
    std::array<char, 32> market_id{};
    std::uint8_t outcome{0};

    // R-20 4 ts (透传 + fill_ts_ns 出口)
    std::int64_t event_ts_ns{0};
    std::int64_t data_source_ts_ns{0};
    std::int64_t ingestion_ts_ns{0};
    std::int64_t as_of_ts_ns{0};
    std::int64_t fill_ts_ns{0};
};

// VirtualFill ABI 校验 (paper engine 内部 struct, 不跨 binary, 但 sizeof 要显式锁定防意外 padding)
// 实测布局 (g++ -std=c++20 x86-64):
//   offset  0: reject(1)  →  pad7 → fill_price@8 .. p_fill_clamped@32(+8)
//   offset 40: slippage_bps(4), bernoulli_draw(1), audit_wal_kind(1) → pad 0 (packed by compiler)
//   offset 46: market_id[32] → outcome(1) → pad1 → event_ts_ns@80 .. fill_ts_ns@112(+8) = 120B
static_assert(sizeof(VirtualFill) == 120,
              "VirtualFill sizeof 改变 — 确认后更新此断言 (paper engine 内部 struct, R-2 not affected)");

class VirtualMatcher {
public:
    // seed=0 → time-based; >0 → deterministic (单测必传)
    explicit VirtualMatcher(std::uint64_t seed = 0xBE'EFCAFEULL) noexcept : rng_(seed) {}

    // 单次撮合. 内部:
    //   1) SlippageModel.compute → expected_fill_rate / expected_fill_price
    //   2) p_fill = clamp(rate, FLOOR, CAP)
    //   3) Bernoulli(p_fill) draw
    //   4) draw=1 → fill (price + size); draw=0 → reject(BernoulliMissed)
    [[nodiscard]] VirtualFill Match(const VirtualOrder& order) noexcept;

    // 测试钩子: 直接注入 [0,1] 抽样源 (确定性)
    void SetUniformOverrideForTesting(double u) noexcept {
        uniform_override_ = u;
        has_override_ = true;
    }
    void ClearOverrideForTesting() noexcept { has_override_ = false; }

private:
    std::mt19937_64 rng_;
    double uniform_override_{0.0};
    bool has_override_{false};
};

}  // namespace stcpp::execution
