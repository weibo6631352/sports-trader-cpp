// stcpp/execution/virtual_matcher.hpp — VirtualMatcher (paper mode 虚拟撮合)
//
// 落:
//   xiaojiang-paper-engine-skeleton-v1.md §4 (Mode A++ Bernoulli sampler)
//   xiaoxiao-slippage-model-lib-v1.md §1 (SlippageModel.compute)
//   xiaoyuan-microstructure-v1   (KAPPA_DEPTH_GAMEDAY, fill_rate floor/cap)
//   xiaocheng-p0-02-alpha-v2-backtest-spec-v0.2.md §2.1 (slippage ~0.3%, fee 3%)
//
// 红线:
//   R-7  Paper binary 专用; live binary 不 link.
//   R-11 VirtualFill 走 paper_audit.wal (caller 落), 严禁触碰 position / pnl_ledger.
//        VirtualFill.mode_tag 强制 "paper", 不污染真账本.
//   R-20 VirtualFill 携带 4 ts (从 SignResponse 透传 + fill_ts_ns).
//
// Mode A++ Bernoulli (原有 Match 接口, 不变):
//   p_fill = clamp(SlippageModel.expected_fill_rate, FLOOR=0.50, CAP=0.65)
//   draw   ~ Bernoulli(p_fill)
//   draw=1 → VirtualFill { price = expected_fill_price, size = req.size_usdc * fill_size_ratio }
//   draw=0 → VirtualFill { fill_size = 0, reject = LowFillRate }
//
// Mode A MatchWithBook (Wave 3 新增, 真实 CLOB depth 撮合):
//   p_fill = clamp(FillRateModel::compute_from_clob_book().fill_rate, FLOOR=0.50, CAP=0.90)
//   slippage 从 ClobFillOutput.slippage_rate 直接取 (~0.3%)
//   fill_price = quote_price + slippage (buy) 或 quote_price - slippage (sell)
//   部分成交: 若 p_fill < 1.0, fill_size = intent_size × p_fill (基于 queue depth 折扣)
//   R-11: 产出 VirtualFill.audit_wal_kind = PaperAudit + mode_tag = "paper"

#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <string_view>

#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/microstructure/fill_rate_model.hpp"
#include "stcpp/microstructure/orderbook.hpp"
#include "stcpp/numerical/slippage_model.hpp"

namespace stcpp::execution {

// Mode A++ floor / cap (小袁 microstructure v1, SlippageModel 路径)
inline constexpr double kFillRateFloor = 0.50;
inline constexpr double kFillRateCap = 0.65;

// Mode A (Wave 3 CLOB book 路径) cap — 真实 depth 建模更精确, 上限放宽到 0.90
// (允许高流动性市场 NBA Pregame $15K depth 的高 fill rate)
inline constexpr double kFillRateClobCap = 0.90;

enum class MatchReject : std::uint8_t {
    Ok = 0,
    SlippageModelReject = 1,  // SlippageModel 已拒 (INVALID_INTENT / ExceedBookDepth / FillRateBelowFloor)
    BernoulliMissed = 2,      // Bernoulli draw=0
    InvalidConfig = 3,
    ClobModelReject = 4,      // FillRateModel::compute_from_clob_book 拒 (InvalidSnapshot / BelowFloor)
    InvalidBookSnapshot = 5,  // MatchWithBook 入参 book 校验失败
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

    // live 路由所需 (2026-06-12 实盘准备; 加性 POD, VirtualMatcher 忽略):
    std::string_view token_id{};  // decimal ERC1155 outcome token (LiveOrderRequest 用)
    bool is_buy{true};            // BUY/SELL (live maker/taker 金额方向)
    bool neg_risk{false};         // negRisk 市场 (0xC5d5… exchange 选择)
};

// VirtualOrderWithBook — Wave 3 Mode A 入参, 携带完整 OrderBookSnapshot
// 比 VirtualOrder 额外持有真实 CLOB 多档 book + Microprobe (来自 CLOBSubscriber)
//
// 设计: MatchWithBook() 接受此结构, 不修改 VirtualOrder (保留向后兼容).
// sport/phase 由 caller 从 Goalserve LiveSection + sport 映射注入.
struct VirtualOrderWithBook {
    std::array<std::uint8_t, 16> audit_id{};
    std::uint64_t intent_id{0};
    std::string_view market_id{};
    std::string_view outcome{};
    double size_usdc{0.0};

    // FillRateModel::compute_from_clob_book 输入
    microstructure::OrderBookSnapshot book{};  // 实时 CLOB depth
    microstructure::Microprobe probe{};        // QHL + AS (可传默认值)
    microstructure::FillIntent fill_intent{};  // side / price / sport / phase

    // R-20 4 ts (同 VirtualOrder)
    std::int64_t event_ts_ns{0};
    std::int64_t data_source_ts_ns{0};
    std::int64_t ingestion_ts_ns{0};
    std::int64_t as_of_ts_ns{0};
    std::int64_t wall_now_ns{0};
};

struct VirtualFill {
    MatchReject reject{MatchReject::Ok};

    double fill_price{0.0};  // VWAP, SlippageModel 出 (Mode A++) 或 clob 估算 (Mode A)
    // A1 (老郭钳-3): micro pUSD (1e-6), signed; = to_micro_pusd(order.size_usdc * fill_rate)。
    //   double→micro 转换唯一走 to_micro_pusd() (禁手写 ×1e6); ledger 直存无截断 (消 <1pUSD 丢仓)。
    std::int64_t fill_size_usdc{0};
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

    // R-11 mode_tag — 物理标记 "paper" (Wave 3 新增, R-11 paper_ledger_guard 语义一致)
    // 1B flag: 0 = paper (only valid value for VirtualFill), non-zero = 开发期侦错
    // 不污染真账本: PositionLedger 写入前必须检查 mode_tag == 0
    std::uint8_t mode_tag{0};  // 0 = paper (R-11 硬填)

    // R-20 4 ts (透传 + fill_ts_ns 出口)
    std::int64_t event_ts_ns{0};
    std::int64_t data_source_ts_ns{0};
    std::int64_t ingestion_ts_ns{0};
    std::int64_t as_of_ts_ns{0};
    std::int64_t fill_ts_ns{0};

    // CLOB order_id (live: LiveExecutorAdapter 从 ExecReport 填; paper 空)。Phase 2 对账兜底用:
    //   WSS user 频道成交回执的 taker_order_id 与此匹配 → 判定 sync 路径是否已记此单 (防漏记/双记)。
    //   CLOB order hash = 0x+64hex = 66 字符, 留 72 余量 + null。
    std::array<char, 72> order_id{};
};

// VirtualFill ABI 校验 (paper engine 内部 struct, 不跨 binary, 但 sizeof 要显式锁定防意外 padding)
// Wave 3: outcome(1) + mode_tag(1) → 两字节连续, 后接 6B pad 对齐 int64, 共 120B 不变.
// 实测布局 (clang++ -std=c++20 arm64/x86-64):
//   offset  0: reject(1)  →  pad7 → fill_price@8 .. p_fill_clamped@32(+8)
//   offset 40: slippage_bps(4), bernoulli_draw(1), audit_wal_kind(1) → pad 0
//   offset 46: market_id[32]@46 → outcome@78(1) → mode_tag@79(1) → pad6
//   event_ts_ns@86? — 实际依赖编译器; 更新 sizeof 以编译时实测为准
static_assert(sizeof(VirtualFill) == 192,
              "VirtualFill sizeof 改变 — 确认后更新此断言 (paper engine 内部 struct, R-2 not affected)。"
              "2026-06-13: +order_id[72] (Phase 2 对账兜底) 120→192。");

class VirtualMatcher {
public:
    // seed=0 → time-based; >0 → deterministic (单测必传)
    explicit VirtualMatcher(std::uint64_t seed = 0xBE'EFCAFEULL) noexcept : rng_(seed) {}

    // Mode A++ 单次撮合 (原有接口, 不变). 内部:
    //   1) SlippageModel.compute → expected_fill_rate / expected_fill_price
    //   2) p_fill = clamp(rate, FLOOR=0.50, CAP=0.65)
    //   3) Bernoulli(p_fill) draw
    //   4) draw=1 → fill (price + size); draw=0 → reject(BernoulliMissed)
    [[nodiscard]] VirtualFill Match(const VirtualOrder& order) noexcept;

    // Mode A 单次撮合 (Wave 3 新增, 真实 CLOB depth). 内部:
    //   1) FillRateModel::compute_from_clob_book → fill_rate / slippage_rate
    //   2) p_fill = clamp(fill_rate, FLOOR=0.50, CAP=0.90)
    //   3) fill_price = price +/- slippage (buy/sell)
    //   4) fill_size = size_usdc * p_fill  (部分成交, 禁止理想全成交)
    //   5) R-11: audit_wal_kind = PaperAudit (硬填)
    //   backtest 复用: 传入历史 book snapshot 即可重现撮合结果
    [[nodiscard]] VirtualFill MatchWithBook(const VirtualOrderWithBook& order) noexcept;

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
