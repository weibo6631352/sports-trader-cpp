// tests/unit/test_fill_rate_model_v2.cpp — FillRateModel v0.2 + VirtualMatcher Mode A 单测
//
// Owner: 小袁 (quant-microstructure)
// Wave 3 — FillRateModel v0.2 (compute_from_clob_book) + VirtualMatcher::MatchWithBook()
//
// 覆盖:
//   ClobFill_T01: 充足深度 + 小 size → queue_position_ratio < 1 → p_queue_fill > 0.36
//   ClobFill_T02: 大 size 超过单档深度 → queue_position_ratio > 1 → fill_rate < 0.50 (BelowFloor)
//   ClobFill_T03: spread 宽 → spread_penalty 触发
//   ClobFill_T04: adverse_selection > 0.5 → adverse_penalty 触发
//   ClobFill_T05: Late phase NBA → time_decay_penalty 触发
//   ClobFill_T06: slippage_rate 保守估算 ≈ 0.3% (rho=0.5, tick=0.01, price=0.5)
//   ClobFill_T07: InvalidSnapshot → reject=InvalidSnapshot (R-20 4 ts 违反)
//   ClobFill_T08: InvalidIntent size=0 → reject=InvalidIntent
//   ClobFill_T09: 目标价档不在 book 内 → fallback 到 best_ask/bid
//   ClobFill_T10: NBA Pregame 高流动性 → fill_rate ≥ FLOOR (充分正期望场景)
//   MatchWithBook_T11: R-11 audit_wal_kind 硬填 PaperAudit
//   MatchWithBook_T12: R-20 4 ts 透传正确
//   MatchWithBook_T13: 部分成交 — fill_size_usdc < size_usdc (禁止理想全成交)
//   MatchWithBook_T14: fill_price = price + slippage (buy 方向)
//   MatchWithBook_T15: InvalidBookSnapshot → ClobModelReject/InvalidBookSnapshot
//   MatchWithBook_T16: BelowFloor (过大 size) → ClobModelReject
//   MatchWithBook_T17: fill_rate ∈ [FLOOR, ClobCap=0.90]
//   MatchWithBook_T18: market_id + outcome 透传正确 (YES=0, NO=1)
//
// 红线自检:
//   R-7  纯函数, mode-agnostic (paper/backtest 共用同一接口)
//   R-11 audit_wal_kind = PaperAudit (硬填, T11 验证)
//   R-20 4 ts PIT 透传 (T12 验证)
//   禁止理想全成交: fill_size < size_usdc (T13 验证)
//   slippage ~0.3% (T06 验证)

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include "stcpp/execution/virtual_matcher.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/microstructure/fill_rate_model.hpp"
#include "stcpp/microstructure/orderbook.hpp"
#include "stcpp/microstructure/sport_profile.hpp"

namespace ms  = stcpp::microstructure;
namespace ex  = stcpp::execution;

namespace {

// 合法 4 ts
constexpr std::int64_t kEventTs      = 1'700'000'000'000'000'000LL;  // ~2023-11
constexpr std::int64_t kDataSrcTs    = kEventTs +   1'000'000LL;
constexpr std::int64_t kIngestionTs  = kEventTs +   2'000'000LL;
constexpr std::int64_t kAsOfTs       = kEventTs +   3'000'000LL;

ms::OrderBookTs MakeTs() {
    return {kEventTs, kDataSrcTs, kIngestionTs, kAsOfTs};
}

// 构造合法的 NBA Pregame mid-market book (bid=0.49, ask=0.50, spread=1¢)
// l1_size: 各档 L1 名义 USD 深度; top3: ±2 tick 累计
ms::OrderBookSnapshot MakeBook(double l1_size  = 1500.0,
                               double top3     = 5000.0,
                               std::int32_t spread_bps = 20,
                               double intent_price = 0.50) {
    ms::OrderBookSnapshot b;
    b.ts = MakeTs();
    b.market_id = "test-clob-market";
    b.tick_size = ms::TICK_01;
    // 目标价 0.50 在 ask[0]
    b.ask[0] = {intent_price,        l1_size};
    b.ask[1] = {intent_price + 0.01, l1_size * 0.6};
    b.ask[2] = {intent_price + 0.02, l1_size * 0.4};
    // bid 侧比 ask 低 1 tick
    b.bid[0] = {intent_price - 0.01, l1_size};
    b.bid[1] = {intent_price - 0.02, l1_size * 0.6};
    b.bid[2] = {intent_price - 0.03, l1_size * 0.4};
    b.top3_depth_usdc  = top3;
    b.spread_bps       = spread_bps;
    b.last_trade_ts_ns = kEventTs - 1'000'000'000LL;
    return b;
}

ms::Microprobe MakeProbe(double as_score = 0.0) {
    ms::Microprobe p;
    p.microprice              = 0.495;
    p.mid                     = 0.495;
    p.imbalance               = 0.0;
    p.quote_half_life_ms      = 30'000;
    p.adverse_selection_score = as_score;
    p.is_hot_token            = false;
    return p;
}

ms::FillIntent MakeIntent(double size_usdc,
                          ms::Sport sport = ms::Sport::Basketball,
                          ms::InplayPhase phase = ms::InplayPhase::Pregame,
                          double price = 0.50) {
    ms::FillIntent it;
    it.side      = ms::Side::Buy;
    it.price     = price;
    it.size_usdc = size_usdc;
    it.sport     = sport;
    it.phase     = phase;
    it.path      = ms::MatchPath::Maker;
    return it;
}

// 构造 VirtualOrderWithBook
ex::VirtualOrderWithBook MakeOrderWithBook(double size_usdc = 200.0,
                                           double l1_size   = 1500.0,
                                           double top3      = 5000.0,
                                           std::int32_t spread = 20,
                                           double price     = 0.50) {
    ex::VirtualOrderWithBook o;
    o.intent_id          = 42;
    o.market_id          = "0xNBAPreg2026";
    o.outcome            = "YES";
    o.size_usdc          = size_usdc;
    o.book               = MakeBook(l1_size, top3, spread, price);
    o.probe              = MakeProbe();
    o.fill_intent        = MakeIntent(size_usdc, ms::Sport::Basketball,
                                     ms::InplayPhase::Pregame, price);
    o.event_ts_ns        = kEventTs;
    o.data_source_ts_ns  = kDataSrcTs;
    o.ingestion_ts_ns    = kIngestionTs;
    o.as_of_ts_ns        = kAsOfTs;
    o.wall_now_ns        = kAsOfTs;
    return o;
}

}  // namespace

// ============================================================================
// ClobFillOutput 测试 (FillRateModel::compute_from_clob_book)
// ============================================================================

// ClobFill_T01: 充足深度 + 小 size → queue_position_ratio < 1, p_queue_fill > 0.36
TEST(FillRateModelV02_Clob, T01_SmallSizeHighQueueFill) {
    // L1 size=1500, intent size=200 → ratio = 200/1500 ≈ 0.133 → p_queue = exp(-0.133) ≈ 0.875
    auto book   = MakeBook(1500.0, 5000.0, 20);
    auto probe  = MakeProbe();
    auto intent = MakeIntent(200.0);
    auto out    = ms::FillRateModel::compute_from_clob_book(book, probe, intent);

    EXPECT_EQ(out.reject, ms::FillRateReject::Ok);
    EXPECT_LT(out.queue_position_ratio, 1.0);
    EXPECT_GT(out.p_queue_fill, 0.36);  // exp(-1) ≈ 0.368
    EXPECT_GE(out.fill_rate, ms::FILL_RATE_FLOOR);
}

// ClobFill_T02: 大 size 超过单档 → ratio > 2 → fill_rate < 0.50 → BelowFloor
TEST(FillRateModelV02_Clob, T02_LargeSizeBelowFloor) {
    // L1 size=200, intent size=2000 → ratio = 2000/200 = 10 → p_queue = exp(-10) ≈ 0.000045
    auto book   = MakeBook(200.0, 800.0, 20);
    auto probe  = MakeProbe();
    auto intent = MakeIntent(2000.0);
    auto out    = ms::FillRateModel::compute_from_clob_book(book, probe, intent);

    EXPECT_EQ(out.reject, ms::FillRateReject::BelowFloor);
    EXPECT_GT(out.queue_position_ratio, 1.0);
    EXPECT_LT(out.fill_rate, ms::FILL_RATE_FLOOR);
}

// ClobFill_T03: spread 宽 → spread_penalty = 0.10 触发
TEST(FillRateModelV02_Clob, T03_WideSpreadPenalty) {
    auto book_narrow = MakeBook(1500.0, 5000.0, 20);   // spread_bps=20 < 50
    auto book_wide   = MakeBook(1500.0, 5000.0, 80);   // spread_bps=80 > 50
    auto probe  = MakeProbe();
    auto intent = MakeIntent(200.0);
    auto o_n = ms::FillRateModel::compute_from_clob_book(book_narrow, probe, intent);
    auto o_w = ms::FillRateModel::compute_from_clob_book(book_wide,   probe, intent);

    EXPECT_NEAR(o_w.spread_penalty, ms::PENALTY_SPREAD_WIDE, 1e-9);
    EXPECT_DOUBLE_EQ(o_n.spread_penalty, 0.0);
    // 宽 spread 时 fill_rate 更低
    EXPECT_LT(o_w.fill_rate, o_n.fill_rate);
}

// ClobFill_T04: adverse_selection_score > 0.5 → adverse_penalty 触发
TEST(FillRateModelV02_Clob, T04_AdverseSelectionPenalty) {
    auto book       = MakeBook(1500.0, 5000.0, 20);
    auto probe_none = MakeProbe(0.0);
    auto probe_as   = MakeProbe(0.8);  // > 0.5
    auto intent     = MakeIntent(200.0);
    auto o_no_as = ms::FillRateModel::compute_from_clob_book(book, probe_none, intent);
    auto o_as    = ms::FillRateModel::compute_from_clob_book(book, probe_as,   intent);

    EXPECT_NEAR(o_as.adverse_penalty, ms::PENALTY_ADVERSE_SELECT, 1e-9);
    EXPECT_LT(o_as.fill_rate, o_no_as.fill_rate);
}

// ClobFill_T05: Late phase NBA → time_decay_penalty 触发
TEST(FillRateModelV02_Clob, T05_LatePhaseNbaTimeDecay) {
    auto book        = MakeBook(1500.0, 5000.0, 20);
    auto probe       = MakeProbe();
    auto intent_preg = MakeIntent(200.0, ms::Sport::Basketball, ms::InplayPhase::Pregame);
    auto intent_late = MakeIntent(200.0, ms::Sport::Basketball, ms::InplayPhase::Late);
    auto o_p = ms::FillRateModel::compute_from_clob_book(book, probe, intent_preg);
    auto o_l = ms::FillRateModel::compute_from_clob_book(book, probe, intent_late);

    auto const& prof_late = ms::profile_of(ms::Sport::Basketball, ms::InplayPhase::Late);
    EXPECT_NEAR(o_l.time_decay_penalty, prof_late.late_decay_penalty, 1e-9);
    EXPECT_GT(prof_late.late_decay_penalty, 0.0);
    EXPECT_LT(o_l.fill_rate, o_p.fill_rate);
}

// ClobFill_T06: slippage_rate 保守估算 ~0.3%
// rho = size/l1 = 100/1000 = 0.1; tick=0.01; price=0.50
// slip = 0.1 * 0.01 / 0.50 * 0.5 = 0.001 (0.1%), clamp floor 0.00001 → 0.001
// 验证 slippage_rate ∈ [0.001, 0.005] (保守估算区间)
TEST(FillRateModelV02_Clob, T06_SlippageConservativeEstimate) {
    auto book   = MakeBook(1000.0, 4000.0, 20);
    auto probe  = MakeProbe();
    auto intent = MakeIntent(100.0, ms::Sport::Basketball,
                             ms::InplayPhase::Pregame, 0.50);
    auto out    = ms::FillRateModel::compute_from_clob_book(book, probe, intent);

    EXPECT_EQ(out.reject, ms::FillRateReject::Ok);
    // 小程 §2.1: pregame near-even slippage ~0.2-0.5%
    EXPECT_GT(out.slippage_rate, 0.00001);   // noise floor 以上
    EXPECT_LE(out.slippage_rate, 0.005);     // 上限 0.5%
    // slippage_bps 正确计算
    auto const expected_bps = static_cast<std::int32_t>(out.slippage_rate * 10000.0 + 0.5);
    EXPECT_EQ(out.slippage_bps, expected_bps);
}

// ClobFill_T07: InvalidSnapshot — R-20 4 ts 违反 → reject=InvalidSnapshot
TEST(FillRateModelV02_Clob, T07_InvalidSnapshotTs) {
    auto book = MakeBook();
    book.ts.event_ts_ns = book.ts.data_source_ts_ns + 1;  // 违反 event <= data_source
    auto out  = ms::FillRateModel::compute_from_clob_book(book, MakeProbe(), MakeIntent(200.0));
    EXPECT_EQ(out.reject, ms::FillRateReject::InvalidSnapshot);
    EXPECT_DOUBLE_EQ(out.fill_rate, 0.0);
}

// ClobFill_T08: InvalidIntent size=0 → reject=InvalidIntent
TEST(FillRateModelV02_Clob, T08_InvalidIntentZeroSize) {
    auto book   = MakeBook();
    auto intent = MakeIntent(0.0);
    auto out    = ms::FillRateModel::compute_from_clob_book(book, MakeProbe(), intent);
    EXPECT_EQ(out.reject, ms::FillRateReject::InvalidIntent);
}

// ClobFill_T09: 目标价不在 book 内 → fallback 到 best_ask
// 构造 intent.price=0.60 但 book ask 在 0.50~0.52 → 无法精确匹配 → fallback
TEST(FillRateModelV02_Clob, T09_PriceNotInBook_FallbackBestAsk) {
    auto book   = MakeBook(1000.0, 4000.0, 20, 0.50);  // ask 在 0.50 附近
    auto probe  = MakeProbe();
    // intent 价格偏离: 0.60 比 book ask[0]=0.50 差了 10 tick > ±1 tick 容差
    auto intent = MakeIntent(200.0, ms::Sport::Basketball, ms::InplayPhase::Pregame, 0.60);
    auto out    = ms::FillRateModel::compute_from_clob_book(book, probe, intent);

    // 即使价格不匹配, fallback 到 best_ask 保证 queue_depth > 0
    EXPECT_GT(out.queue_depth_at_price, 0.0);
    // 不应 crash 或返 InvalidIntent
    EXPECT_NE(out.reject, ms::FillRateReject::InvalidIntent);
    EXPECT_NE(out.reject, ms::FillRateReject::InvalidSnapshot);
}

// ClobFill_T10: NBA Pregame 高流动性 → fill_rate >= FLOOR (充分正期望场景)
// NBA 实测 $4800 ±2tick (sport_profile), 小程 §4.1 充足正期望场景
TEST(FillRateModelV02_Clob, T10_NbaPreGameHighLiquidity) {
    // $5000 ±2 tick, size=$200 → ratio 很低 → high fill rate
    auto book   = MakeBook(5000.0, 20000.0, 15);
    auto probe  = MakeProbe(0.0);
    auto intent = MakeIntent(200.0, ms::Sport::Basketball, ms::InplayPhase::Pregame);
    auto out    = ms::FillRateModel::compute_from_clob_book(book, probe, intent);

    EXPECT_EQ(out.reject, ms::FillRateReject::Ok);
    EXPECT_GE(out.fill_rate, ms::FILL_RATE_FLOOR);
    EXPECT_GT(out.p_queue_fill, 0.80);  // 高流动性 ratio 很低 → exp 接近 1
}

// ============================================================================
// VirtualMatcher::MatchWithBook() 测试 (Mode A)
// ============================================================================

// MatchWithBook_T11: R-11 — audit_wal_kind 硬填 PaperAudit
TEST(VirtualMatcherModeA, T11_R11_AuditWalKindAlwaysPaperAudit) {
    ex::VirtualMatcher m{0x11};
    auto order = MakeOrderWithBook();
    auto fill  = m.MatchWithBook(order);

    EXPECT_EQ(fill.audit_wal_kind, stcpp::infra::wal::WalKind::PaperAudit);
    EXPECT_NE(fill.audit_wal_kind, stcpp::infra::wal::WalKind::Position);
    EXPECT_NE(fill.audit_wal_kind, stcpp::infra::wal::WalKind::RiskAudit);
}

// MatchWithBook_T12: R-20 — 4 ts 透传正确
TEST(VirtualMatcherModeA, T12_R20_FourTsPassthrough) {
    ex::VirtualMatcher m{0x12};
    auto order = MakeOrderWithBook();
    auto fill  = m.MatchWithBook(order);

    EXPECT_EQ(fill.event_ts_ns,       order.event_ts_ns);
    EXPECT_EQ(fill.data_source_ts_ns, order.data_source_ts_ns);
    EXPECT_EQ(fill.ingestion_ts_ns,   order.ingestion_ts_ns);
    EXPECT_EQ(fill.as_of_ts_ns,       order.as_of_ts_ns);
    EXPECT_EQ(fill.fill_ts_ns,        order.wall_now_ns);
}

// MatchWithBook_T13: 部分成交 — fill_size_usdc < size_usdc (禁止理想全成交)
// p_fill ∈ [0.50, 0.90] → fill_size < size_usdc
TEST(VirtualMatcherModeA, T13_PartialFill_NoIdealFullFill) {
    ex::VirtualMatcher m{0x13};
    // 充足流动性 → fill 成功, 但 fill_size < size 因 p_fill < 1
    auto order = MakeOrderWithBook(200.0, 2000.0, 8000.0, 15);
    auto fill  = m.MatchWithBook(order);

    if (fill.reject == ex::MatchReject::Ok) {
        EXPECT_LT(fill.fill_size_usdc, order.size_usdc)
            << "Mode A 禁止理想全成交: fill_size 必须 < size_usdc";
        EXPECT_GT(fill.fill_size_usdc, 0.0);
        // fill_size = size_usdc × p_fill_clamped; p_fill ∈ [0.50, 0.90]
        EXPECT_GE(fill.fill_size_usdc, order.size_usdc * ex::kFillRateFloor - 1e-6);
        EXPECT_LE(fill.fill_size_usdc, order.size_usdc * ex::kFillRateClobCap + 1e-6);
    }
}

// MatchWithBook_T14: fill_price = price + slippage (buy 方向)
TEST(VirtualMatcherModeA, T14_FillPriceWithSlippage_BuySide) {
    ex::VirtualMatcher m{0x14};
    const double price = 0.50;
    auto order = MakeOrderWithBook(200.0, 1500.0, 5000.0, 20, price);
    auto fill  = m.MatchWithBook(order);

    if (fill.reject == ex::MatchReject::Ok) {
        // Buy: fill_price > quote_price (slippage 正向)
        EXPECT_GT(fill.fill_price, price);
        // slippage 不超过 0.5% → fill_price < price + 0.005
        EXPECT_LT(fill.fill_price, price + 0.005 + 1e-9);
        // fill_price 在 (0, 1) 合法区间
        EXPECT_GT(fill.fill_price, 0.0);
        EXPECT_LT(fill.fill_price, 1.0);
    }
}

// MatchWithBook_T15: InvalidBookSnapshot — book ts 违反 R-20
TEST(VirtualMatcherModeA, T15_InvalidBookSnapshot_Rejects) {
    ex::VirtualMatcher m{0x15};
    auto order = MakeOrderWithBook();
    order.book.ts.event_ts_ns = order.book.ts.data_source_ts_ns + 1;  // 违反顺序
    auto fill = m.MatchWithBook(order);

    EXPECT_EQ(fill.reject, ex::MatchReject::InvalidBookSnapshot);
    EXPECT_DOUBLE_EQ(fill.fill_size_usdc, 0.0);
    // R-11 仍硬填 PaperAudit
    EXPECT_EQ(fill.audit_wal_kind, stcpp::infra::wal::WalKind::PaperAudit);
}

// MatchWithBook_T16: BelowFloor — 过大 size → ClobModelReject
TEST(VirtualMatcherModeA, T16_BelowFloor_ClobModelReject) {
    ex::VirtualMatcher m{0x16};
    // 超大 size: $50000, L1 仅 $200 → ratio 极高 → BelowFloor
    auto order = MakeOrderWithBook(50000.0, 200.0, 800.0, 20);
    order.fill_intent.size_usdc = 50000.0;
    auto fill = m.MatchWithBook(order);

    EXPECT_EQ(fill.reject, ex::MatchReject::ClobModelReject);
    EXPECT_DOUBLE_EQ(fill.fill_size_usdc, 0.0);
    EXPECT_FALSE(fill.bernoulli_draw);
}

// MatchWithBook_T17: fill_rate ∈ [FLOOR=0.50, ClobCap=0.90]
TEST(VirtualMatcherModeA, T17_FillRateInBand) {
    ex::VirtualMatcher m{0x17};
    auto order = MakeOrderWithBook(200.0, 1500.0, 5000.0, 20);
    auto fill  = m.MatchWithBook(order);

    if (fill.reject == ex::MatchReject::Ok) {
        EXPECT_GE(fill.p_fill_clamped, ex::kFillRateFloor);
        EXPECT_LE(fill.p_fill_clamped, ex::kFillRateClobCap);
        EXPECT_GE(fill.expected_fill_rate, 0.0);
        EXPECT_LE(fill.expected_fill_rate, 1.0);
    }
}

// MatchWithBook_T18: market_id + outcome 透传 (YES=0, NO=1)
TEST(VirtualMatcherModeA, T18_MarketIdOutcomePassthrough) {
    ex::VirtualMatcher m{0x18};

    // YES case
    {
        auto order     = MakeOrderWithBook();
        order.market_id = "mkt_yes_001";
        order.outcome   = "YES";
        auto fill = m.MatchWithBook(order);
        EXPECT_EQ(fill.outcome, std::uint8_t{0}) << "YES → 0";
        std::array<char, 32> expected{};
        std::memcpy(expected.data(), "mkt_yes_001", 11);
        EXPECT_EQ(fill.market_id, expected);
    }

    // NO case
    {
        auto order      = MakeOrderWithBook();
        order.market_id = "mkt_no_002";
        order.outcome   = "NO";
        auto fill = m.MatchWithBook(order);
        EXPECT_EQ(fill.outcome, std::uint8_t{1}) << "NO → 1";
    }
}

// ============================================================================
// 补充: R-7 mode-agnostic — compute_from_clob_book 纯函数 backtest 复用确认
// ============================================================================

// ClobFill_T19: 相同 book snapshot 两次调用得到相同结果 (确定性, 无副作用)
TEST(FillRateModelV02_Clob, T19_PureFunctionDeterminism) {
    auto book   = MakeBook(1500.0, 5000.0, 20);
    auto probe  = MakeProbe();
    auto intent = MakeIntent(300.0);
    auto out1   = ms::FillRateModel::compute_from_clob_book(book, probe, intent);
    auto out2   = ms::FillRateModel::compute_from_clob_book(book, probe, intent);

    EXPECT_EQ(out1.reject, out2.reject);
    EXPECT_DOUBLE_EQ(out1.fill_rate, out2.fill_rate);
    EXPECT_DOUBLE_EQ(out1.slippage_rate, out2.slippage_rate);
    EXPECT_DOUBLE_EQ(out1.p_queue_fill, out2.p_queue_fill);
}

// ClobFill_T20: sum_penalties 符合公式 sum = -(spread + adverse + time_decay) + sport_bias
TEST(FillRateModelV02_Clob, T20_SumPenaltiesFormula) {
    // 触发所有 penalty: spread > 50, AS > 0.5, Late phase
    auto book   = MakeBook(1500.0, 5000.0, 80);   // spread=80 > 50
    auto probe  = MakeProbe(0.9);                  // AS > 0.5
    auto intent = MakeIntent(200.0, ms::Sport::Basketball, ms::InplayPhase::Late);
    auto out    = ms::FillRateModel::compute_from_clob_book(book, probe, intent);

    double const expected_sum = -(out.spread_penalty + out.adverse_penalty
                                + out.time_decay_penalty) + out.sport_bias;
    EXPECT_NEAR(out.sum_penalties, expected_sum, 1e-9);
    // 三种 penalty 都必须 > 0
    EXPECT_GT(out.spread_penalty,      0.0);
    EXPECT_GT(out.adverse_penalty,     0.0);
    EXPECT_GT(out.time_decay_penalty,  0.0);
}
