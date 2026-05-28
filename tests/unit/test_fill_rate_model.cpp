// tests/unit/test_fill_rate_model.cpp — FillRateModel v0.1 单测 (20 case)
//
// Owner: 小袁 (quant-microstructure)
// Sprint-2 W4 Wave 20
//
// 覆盖派单 §4 (≤ 300 行):
//   T01 depth 充足 + 正常 spread → ~0.80+
//   T02 depth 不足 → < 0.50 (触发 BelowFloor)
//   T03 QHL 短 → -0.15 penalty
//   T04 spread 宽 → -0.10 penalty
//   T05 adverse selection → -0.20
//   T06 inplay late → -time_decay
//   T07 复合 penalty 叠加
//   T08 Soccer vs Esports Pregame sport_bias 差异 (Soccer 高)
//   T09 边界 fill_rate=0
//   T10 边界 fill_rate=1
//   T11 R-20 4 ts OrderBookSnapshot 校验 (ts_order 违反 → InvalidSnapshot)
//   T12 R-20 ts_all_positive 违反 → InvalidSnapshot
//   T13 crossed book ask<=bid → InvalidSnapshot
//   T14 NaN size → InvalidIntent
//   T15 size <= 0 → InvalidIntent
//   T16 Taker 路径 size <= L1 → ~0.95
//   T17 Taker 路径 size > L1 → ~0.85
//   T18 Microprice + L1Probe round-trip + |micro-mid| cap 2-tick
//   T19 Sport enum 数值与 GoalserveSport 一致 (static_assert)
//   T20 NBA Late phase late_decay_penalty 触发 (实测 §1.3 Q4 hot)

#include <gtest/gtest.h>

#include <cstdint>

#include "stcpp/microstructure/fill_rate_model.hpp"
#include "stcpp/microstructure/orderbook.hpp"
#include "stcpp/microstructure/sport_profile.hpp"
#include "stcpp/data/goalserve_client.hpp"

namespace ms = stcpp::microstructure;

namespace {

// 4 ts 合法 (严格不递减, 全正)
constexpr std::int64_t kEventTs       = 1'000'000'000'000LL;  // 1000s
constexpr std::int64_t kDataSourceTs  = 1'000'001'000'000LL;
constexpr std::int64_t kIngestionTs   = 1'000'002'000'000LL;
constexpr std::int64_t kAsOfTs        = 1'000'003'000'000LL;

ms::OrderBookTs MakeTs() {
    return {kEventTs, kDataSourceTs, kIngestionTs, kAsOfTs};
}

// 构造一个合法的 gameday "正常" book (NBA $5K ±2tick 深度, spread 1¢)
ms::OrderBookSnapshot MakeBook(double l1_size = 1500.0,
                               double top3 = 5000.0,
                               std::int32_t spread_bps = 20) {
    ms::OrderBookSnapshot b;
    b.ts = MakeTs();
    b.market_id = "test-market";
    b.tick_size = ms::TICK_01;
    // best_bid 0.49 / best_ask 0.50 → spread 1¢
    b.bid[0] = {0.49, l1_size};
    b.bid[1] = {0.48, l1_size * 0.6};
    b.bid[2] = {0.47, l1_size * 0.4};
    b.ask[0] = {0.50, l1_size};
    b.ask[1] = {0.51, l1_size * 0.6};
    b.ask[2] = {0.52, l1_size * 0.4};
    b.top3_depth_usdc  = top3;
    b.spread_bps       = spread_bps;
    b.last_trade_ts_ns = kEventTs - 1'000'000'000LL;  // 1s ago
    return b;
}

ms::Microprobe MakeProbe(std::int64_t qhl_ms = 30'000,
                         double as_score = 0.0) {
    ms::Microprobe p;
    p.microprice  = 0.495;
    p.mid         = 0.495;
    p.imbalance   = 0.0;
    p.quote_half_life_ms      = qhl_ms;
    p.adverse_selection_score = as_score;
    p.is_hot_token            = false;
    return p;
}

ms::FillIntent MakeIntent(double size_usdc,
                          ms::Sport sport = ms::Sport::Basketball,
                          ms::InplayPhase phase = ms::InplayPhase::Mid,
                          ms::MatchPath path = ms::MatchPath::Maker) {
    ms::FillIntent it;
    it.side = ms::Side::Buy;
    it.price = 0.50;
    it.size_usdc = size_usdc;
    it.sport = sport;
    it.phase = phase;
    it.path  = path;
    return it;
}

}  // namespace

// === T01: depth 充足 + 正常 spread → 高 fill_rate ===
TEST(FillRateModelV01, T01_DepthAmpleNormalSpread) {
    auto book   = MakeBook(/*l1=*/3000.0, /*top3=*/15000.0, /*spread=*/20);
    auto probe  = MakeProbe();
    auto intent = MakeIntent(/*size=*/2000.0);  // ratio = 15000 / 2000 = 7.5 → base=0.95 cap
    auto out = ms::FillRateModel::compute_maker(book, probe, intent);
    EXPECT_EQ(out.reject, ms::FillRateReject::Ok);
    EXPECT_GE(out.fill_rate, 0.80);
    EXPECT_NEAR(out.breakdown.base_rate, ms::BASE_MAX, 1e-9);
}

// === T02: depth 不足 → BelowFloor ===
TEST(FillRateModelV01, T02_DepthInsufficientBelowFloor) {
    auto book = MakeBook(/*l1=*/200.0, /*top3=*/500.0, /*spread=*/20);
    auto probe = MakeProbe();
    auto intent = MakeIntent(/*size=*/2000.0);  // ratio = 500/2000 = 0.25 → base=0.25
    auto out = ms::FillRateModel::compute_maker(book, probe, intent);
    EXPECT_LT(out.fill_rate, ms::FILL_RATE_FLOOR);
    EXPECT_EQ(out.reject, ms::FillRateReject::BelowFloor);
}

// === T03: QHL 短 → -0.15 penalty ===
// 用 size 大 + 深度刚够的 case, base 在 0.4 左右, 不触顶 clamp.
// sport 选 Basketball Mid (base=0.72, bias=+0.07) → 合成 ~0.47, penalty 后 ~0.32, 线性可比.
TEST(FillRateModelV01, T03_QhlShortPenalty) {
    auto book = MakeBook(1000.0, 800.0, 20);  // top3=800
    auto probe_long  = MakeProbe(/*qhl=*/30'000);
    auto probe_short = MakeProbe(/*qhl=*/200);  // < 500ms
    auto intent = MakeIntent(2000.0);  // base = 800/2000 = 0.40
    auto o_long  = ms::FillRateModel::compute_maker(book, probe_long, intent);
    auto o_short = ms::FillRateModel::compute_maker(book, probe_short, intent);
    EXPECT_NEAR(o_short.fill_rate - o_long.fill_rate, -ms::PENALTY_QHL_SHORT, 1e-9);
    EXPECT_NEAR(o_short.breakdown.qhl_penalty, ms::PENALTY_QHL_SHORT, 1e-9);
}

// === T04: spread 宽 → -0.10 penalty ===
TEST(FillRateModelV01, T04_SpreadWidePenalty) {
    auto book_narrow = MakeBook(1000.0, 800.0, /*spread=*/20);
    auto book_wide   = MakeBook(1000.0, 800.0, /*spread=*/80);  // > 50
    auto probe = MakeProbe();
    auto intent = MakeIntent(2000.0);
    auto o_n = ms::FillRateModel::compute_maker(book_narrow, probe, intent);
    auto o_w = ms::FillRateModel::compute_maker(book_wide,   probe, intent);
    EXPECT_NEAR(o_w.fill_rate - o_n.fill_rate, -ms::PENALTY_SPREAD_WIDE, 1e-9);
}

// === T05: adverse selection → -0.20 ===
TEST(FillRateModelV01, T05_AdverseSelectionPenalty) {
    auto book = MakeBook(1000.0, 800.0, 20);
    auto probe_no  = MakeProbe(30'000, /*as=*/0.0);
    auto probe_yes = MakeProbe(30'000, /*as=*/0.8);  // > 0.5
    auto intent = MakeIntent(2000.0);
    auto o1 = ms::FillRateModel::compute_maker(book, probe_no,  intent);
    auto o2 = ms::FillRateModel::compute_maker(book, probe_yes, intent);
    EXPECT_NEAR(o2.fill_rate - o1.fill_rate, -ms::PENALTY_ADVERSE_SELECT, 1e-9);
}

// === T06: inplay late → -time_decay ===
// 注: Late phase 同时切 sport_bias 和 time_decay 双重. 这里只测两者合成 delta.
// 用 base 不触顶 (depth/size=0.40) 保线性可比.
TEST(FillRateModelV01, T06_LatePhaseTimeDecay) {
    auto book = MakeBook(1000.0, 800.0, 20);
    auto probe = MakeProbe();
    auto intent_mid  = MakeIntent(2000.0, ms::Sport::Basketball, ms::InplayPhase::Mid);
    auto intent_late = MakeIntent(2000.0, ms::Sport::Basketball, ms::InplayPhase::Late);
    auto o_m = ms::FillRateModel::compute_maker(book, probe, intent_mid);
    auto o_l = ms::FillRateModel::compute_maker(book, probe, intent_late);
    auto const& prof_late = ms::profile_of(ms::Sport::Basketball, ms::InplayPhase::Late);
    auto const& prof_mid  = ms::profile_of(ms::Sport::Basketball, ms::InplayPhase::Mid);
    double const delta_expected =
        -prof_late.late_decay_penalty
        + (prof_late.base_fill_rate - prof_mid.base_fill_rate);
    EXPECT_NEAR(o_l.fill_rate - o_m.fill_rate, delta_expected, 1e-9);
    EXPECT_GT(o_l.breakdown.time_decay_penalty, 0.0);
}

// === T07: 复合 penalty 叠加 (QHL + spread + AS + Late) ===
TEST(FillRateModelV01, T07_CompoundPenaltiesStack) {
    auto book   = MakeBook(3000.0, 12000.0, /*spread=*/80);
    auto probe  = MakeProbe(/*qhl=*/200, /*as=*/0.9);
    auto intent = MakeIntent(2000.0, ms::Sport::Basketball, ms::InplayPhase::Late);
    auto out = ms::FillRateModel::compute_maker(book, probe, intent);
    auto const& p = ms::profile_of(ms::Sport::Basketball, ms::InplayPhase::Late);
    double const expected_sum_penalties = -(ms::PENALTY_QHL_SHORT
                                          + ms::PENALTY_SPREAD_WIDE
                                          + ms::PENALTY_ADVERSE_SELECT
                                          + p.late_decay_penalty);
    EXPECT_NEAR(out.breakdown.sum_penalties, expected_sum_penalties, 1e-9);
}

// === T08: Soccer vs Esports Pregame sport_bias 差异 (Soccer base 高) ===
TEST(FillRateModelV01, T08_SoccerVsEsportsSportBias) {
    auto book   = MakeBook(3000.0, 8000.0, 20);
    auto probe  = MakeProbe();
    auto i_soc  = MakeIntent(2000.0, ms::Sport::Soccer,  ms::InplayPhase::Pregame);
    auto i_esp  = MakeIntent(2000.0, ms::Sport::Esports, ms::InplayPhase::Pregame);
    auto o_soc = ms::FillRateModel::compute_maker(book, probe, i_soc);
    auto o_esp = ms::FillRateModel::compute_maker(book, probe, i_esp);
    EXPECT_GT(o_soc.fill_rate, o_esp.fill_rate);
    EXPECT_GT(o_soc.breakdown.sport_bias, o_esp.breakdown.sport_bias);
}

// === T09: fill_rate clamp 下界 = 0 (极端复合 penalty) ===
TEST(FillRateModelV01, T09_ClampLowerZero) {
    auto book   = MakeBook(/*l1=*/50.0, /*top3=*/50.0, /*spread=*/200);
    auto probe  = MakeProbe(/*qhl=*/100, /*as=*/0.99);
    auto intent = MakeIntent(/*size=*/100000.0,  // depth/size = 0.0005 → BASE_MIN=0.10
                             ms::Sport::Esports, ms::InplayPhase::Late);
    auto out = ms::FillRateModel::compute_maker(book, probe, intent);
    EXPECT_GE(out.fill_rate, 0.0);
    EXPECT_LE(out.fill_rate, 1.0);
    EXPECT_EQ(out.reject, ms::FillRateReject::BelowFloor);
}

// === T10: fill_rate clamp 上界 = 1 (极厚深度 + Tennis Pregame bias 高) ===
TEST(FillRateModelV01, T10_ClampUpperOne) {
    auto book   = MakeBook(50000.0, 200000.0, 10);
    auto probe  = MakeProbe();
    auto intent = MakeIntent(100.0, ms::Sport::Tennis, ms::InplayPhase::Pregame);
    auto out = ms::FillRateModel::compute_maker(book, probe, intent);
    EXPECT_LE(out.fill_rate, 1.0);
    EXPECT_NEAR(out.fill_rate, 1.0, 0.05);  // base=0.95 + sport_bias 正 → 接近 1.0
}

// === T11: R-20 4 ts 顺序违反 ===
TEST(FillRateModelV01, T11_TsOrderViolationRejects) {
    auto book = MakeBook();
    book.ts.event_ts_ns = book.ts.data_source_ts_ns + 1;  // event > data_source
    auto probe = MakeProbe();
    auto intent = MakeIntent(2000.0);
    auto out = ms::FillRateModel::compute_maker(book, probe, intent);
    EXPECT_EQ(out.reject, ms::FillRateReject::InvalidSnapshot);
}

// === T12: R-20 ts_all_positive 违反 ===
TEST(FillRateModelV01, T12_TsZeroRejects) {
    auto book = MakeBook();
    book.ts.event_ts_ns = 0;
    auto out = ms::FillRateModel::compute_maker(book, MakeProbe(), MakeIntent(2000.0));
    EXPECT_EQ(out.reject, ms::FillRateReject::InvalidSnapshot);
}

// === T13: crossed book (ask <= bid) ===
TEST(FillRateModelV01, T13_CrossedBookRejects) {
    auto book = MakeBook();
    book.ask[0].price = 0.49;  // = bid[0]
    auto out = ms::FillRateModel::compute_maker(book, MakeProbe(), MakeIntent(2000.0));
    EXPECT_EQ(out.reject, ms::FillRateReject::InvalidSnapshot);
}

// === T14: NaN size → InvalidIntent ===
TEST(FillRateModelV01, T14_NanSizeRejects) {
    auto intent = MakeIntent(0.0);
    intent.size_usdc = std::numeric_limits<double>::quiet_NaN();
    auto out = ms::FillRateModel::compute_maker(MakeBook(), MakeProbe(), intent);
    EXPECT_EQ(out.reject, ms::FillRateReject::InvalidIntent);
}

// === T15: size <= 0 → InvalidIntent ===
TEST(FillRateModelV01, T15_NegativeSizeRejects) {
    auto intent = MakeIntent(-100.0);
    auto out = ms::FillRateModel::compute_maker(MakeBook(), MakeProbe(), intent);
    EXPECT_EQ(out.reject, ms::FillRateReject::InvalidIntent);
}

// === T16: Taker 路径 size <= L1 → ~0.95 ===
TEST(FillRateModelV01, T16_TakerSmallSizeNearOne) {
    auto book = MakeBook(/*l1=*/3000.0, 8000.0, 20);
    auto intent = MakeIntent(/*size=*/1000.0, ms::Sport::Basketball,
                             ms::InplayPhase::Mid, ms::MatchPath::Taker);
    auto out = ms::FillRateModel::compute_taker(book, intent);
    EXPECT_NEAR(out.fill_rate, 0.95, 1e-9);
    EXPECT_EQ(out.reject, ms::FillRateReject::Ok);
}

// === T17: Taker 路径 size > L1 → 0.85 ===
TEST(FillRateModelV01, T17_TakerLargeSizeMultiLevel) {
    auto book = MakeBook(/*l1=*/500.0, 8000.0, 20);
    auto intent = MakeIntent(2000.0, ms::Sport::Basketball,
                             ms::InplayPhase::Mid, ms::MatchPath::Taker);
    auto out = ms::FillRateModel::compute_taker(book, intent);
    EXPECT_NEAR(out.fill_rate, 0.85, 1e-9);
}

// === T18: L1Probe round-trip + microprice cap 2-tick ===
TEST(FillRateModelV01, T18_L1ProbeMicropriceCap) {
    // 极度不平衡: ask qty 是 bid qty 的 1000 倍 → micro_raw 偏向 bid 大幅, cap 在 2-tick
    ms::OrderBookLevel bid{0.50, 1.0};
    ms::OrderBookLevel ask{0.51, 1000.0};
    auto p = ms::compute_l1_probe(bid, ask, ms::TICK_01);
    EXPECT_TRUE(p.valid);
    EXPECT_NEAR(p.mid, 0.505, 1e-9);
    // |micro - mid| ≤ 2 * tick = 0.02
    EXPECT_LE(p.microprice, 0.505 + 2 * ms::TICK_01 + 1e-9);
    EXPECT_GE(p.microprice, 0.505 - 2 * ms::TICK_01 - 1e-9);
    EXPECT_GT(p.imbalance, -1.0);
    EXPECT_LT(p.imbalance,  0.0);  // bid 量更小 → imbalance 负
}

// === T19: Sport enum 数值与 GoalserveSport 一致 ===
TEST(FillRateModelV01, T19_SportEnumAlignsWithGoalserve) {
    using G = stcpp::data::goalserve::GoalserveSport;
    static_assert(static_cast<std::uint8_t>(ms::Sport::Soccer)
                  == static_cast<std::uint8_t>(G::Soccer));
    static_assert(static_cast<std::uint8_t>(ms::Sport::Basketball)
                  == static_cast<std::uint8_t>(G::Basketball));
    static_assert(static_cast<std::uint8_t>(ms::Sport::Tennis)
                  == static_cast<std::uint8_t>(G::Tennis));
    static_assert(static_cast<std::uint8_t>(ms::Sport::Esports)
                  == static_cast<std::uint8_t>(G::Esports));
    static_assert(static_cast<std::uint8_t>(ms::Sport::Baseball)
                  == static_cast<std::uint8_t>(G::Baseball));
    SUCCEED();
}

// === T20: NBA Late phase time_decay_penalty > 0 ===
TEST(FillRateModelV01, T20_BasketballLateDecayActive) {
    auto const& prof = ms::profile_of(ms::Sport::Basketball, ms::InplayPhase::Late);
    EXPECT_GT(prof.late_decay_penalty, 0.0);
    EXPECT_LT(prof.qhl_ms, ms::QHL_THRESHOLD_MS * 2);  // 300ms < 1000ms
}
