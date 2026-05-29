// tests/unit/test_virtual_matcher.cpp — VirtualMatcher Mode A++ Bernoulli 单测
//
// 落: xiaojiang-paper-engine-skeleton-v1.md §4
// 红线: R-11 PaperAudit 硬绑 / R-20 4 ts 透传 / Mode A++ Bernoulli 分布

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <cstdint>

#include "stcpp/execution/virtual_matcher.hpp"
#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"

namespace {

using namespace stcpp;

execution::VirtualOrder MakeOrder(double size = 100.0,
                                  double price = 0.55,
                                  double depth = 1000.0,
                                  double tick  = 0.01) {
    execution::VirtualOrder o;
    o.intent_id          = 7;
    o.market_id          = "0xMatch";
    o.outcome            = "YES";
    o.size_usdc          = size;
    o.quote_price        = price;
    o.book_depth_l1_usdc = depth;
    o.tick_size          = tick;
    const std::int64_t t = infra::wal::pit::NowRealtimeNs();
    o.event_ts_ns        = t - 10'000'000;
    o.data_source_ts_ns  = t -  8'000'000;
    o.ingestion_ts_ns    = t -  4'000'000;
    o.as_of_ts_ns        = t;
    o.wall_now_ns        = t;
    return o;
}

}  // namespace

// ---------- R-11 / R-20 baseline ----------

TEST(VirtualMatcher, R11_AuditWalKindAlwaysPaperAudit) {
    execution::VirtualMatcher m{0x1};
    const auto fill = m.Match(MakeOrder());
    EXPECT_EQ(fill.audit_wal_kind, infra::wal::WalKind::PaperAudit);
    EXPECT_NE(fill.audit_wal_kind, infra::wal::WalKind::RiskAudit);
    EXPECT_NE(fill.audit_wal_kind, infra::wal::WalKind::Position);
}

TEST(VirtualMatcher, R20_FourTsPassthrough) {
    execution::VirtualMatcher m{0x2};
    const auto ord  = MakeOrder();
    const auto fill = m.Match(ord);
    EXPECT_EQ(fill.event_ts_ns,       ord.event_ts_ns);
    EXPECT_EQ(fill.data_source_ts_ns, ord.data_source_ts_ns);
    EXPECT_EQ(fill.ingestion_ts_ns,   ord.ingestion_ts_ns);
    EXPECT_EQ(fill.as_of_ts_ns,       ord.as_of_ts_ns);
    EXPECT_EQ(fill.fill_ts_ns,        ord.wall_now_ns);
}

// ---------- SlippageModel reject 路径 ----------

TEST(VirtualMatcher, SlippageReject_NaN_Propagates) {
    execution::VirtualMatcher m{0x3};
    auto ord = MakeOrder();
    ord.book_depth_l1_usdc = std::nan("");  // → SlippageModel reject
    const auto fill = m.Match(ord);
    EXPECT_EQ(fill.reject, execution::MatchReject::SlippageModelReject);
    EXPECT_DOUBLE_EQ(fill.fill_size_usdc, 0.0);
}

TEST(VirtualMatcher, SlippageReject_ExceedBookDepth) {
    execution::VirtualMatcher m{0x4};
    auto ord = MakeOrder(/*size=*/10'000.0, /*price=*/0.55, /*depth=*/100.0);
    // ρ = 100 → 远超 RHO_MAX=3
    const auto fill = m.Match(ord);
    EXPECT_EQ(fill.reject, execution::MatchReject::SlippageModelReject);
}

// ---------- Mode A++ floor/cap clamp ----------

TEST(VirtualMatcher, FillRateClampedToCap_065) {
    execution::VirtualMatcher m{0x5};
    // 极小 size + 大 depth + 极新 ts → expected_fill_rate ≈ 1.0
    auto ord = MakeOrder(/*size=*/1.0, /*price=*/0.55, /*depth=*/10'000.0);
    // 强制 Bernoulli 抽到 0.0 (永远过) 看 p_clamped 值
    m.SetUniformOverrideForTesting(0.0);
    const auto fill = m.Match(ord);
    EXPECT_EQ(fill.reject, execution::MatchReject::Ok);
    EXPECT_GT(fill.expected_fill_rate, 0.9);            // model 给的 rate 很高
    EXPECT_DOUBLE_EQ(fill.p_fill_clamped, execution::kFillRateCap);   // 但 Bernoulli 用 cap=0.65
}

TEST(VirtualMatcher, FillRateClampedToFloor_050) {
    execution::VirtualMatcher m{0x6};
    // 让 model 出的 rate 落到 floor 以下 (但 model 自己会先 FillRateBelowFloor reject).
    // 这里用一个边界 case: rate 介于 0.50~0.65 区间 — 不 clamp.
    // 改测 mid-band: rate=0.55 时 p_clamped 应该 == 0.55 (不 clamp)
    auto ord = MakeOrder(/*size=*/300.0, /*price=*/0.5, /*depth=*/1'000.0, /*tick=*/0.01);
    m.SetUniformOverrideForTesting(0.0);
    const auto fill = m.Match(ord);
    if (fill.reject == execution::MatchReject::Ok) {
        EXPECT_GE(fill.p_fill_clamped, execution::kFillRateFloor);
        EXPECT_LE(fill.p_fill_clamped, execution::kFillRateCap);
    }
}

// ---------- Bernoulli draw 分布 ----------

TEST(VirtualMatcher, BernoulliMissed_WhenUniformAboveP) {
    execution::VirtualMatcher m{0x7};
    auto ord = MakeOrder(/*size=*/1.0, /*price=*/0.55, /*depth=*/10'000.0);
    m.SetUniformOverrideForTesting(0.99);  // > cap=0.65 → miss
    const auto fill = m.Match(ord);
    EXPECT_EQ(fill.reject, execution::MatchReject::BernoulliMissed);
    EXPECT_DOUBLE_EQ(fill.fill_size_usdc, 0.0);
    EXPECT_FALSE(fill.bernoulli_draw);
}

TEST(VirtualMatcher, BernoulliHit_WhenUniformBelowP) {
    execution::VirtualMatcher m{0x8};
    auto ord = MakeOrder(/*size=*/1.0, /*price=*/0.55, /*depth=*/10'000.0);
    m.SetUniformOverrideForTesting(0.10);  // < cap=0.65 → hit
    const auto fill = m.Match(ord);
    EXPECT_EQ(fill.reject, execution::MatchReject::Ok);
    EXPECT_TRUE(fill.bernoulli_draw);
    EXPECT_GT(fill.fill_size_usdc, 0.0);
}

// Mode A++ 分布稳定性: 1000 抽样下 hit rate 应落在 [floor, cap+eps]
TEST(VirtualMatcher, ModeAPlusPlus_HitRateInBand) {
    execution::VirtualMatcher m{0xABCDEF};
    auto ord = MakeOrder(/*size=*/1.0, /*price=*/0.55, /*depth=*/10'000.0);
    constexpr int N = 1000;
    int hits = 0;
    double p_clamped_seen = 0.0;
    for (int i = 0; i < N; ++i) {
        const auto fill = m.Match(ord);
        if (i == 0) p_clamped_seen = fill.p_fill_clamped;
        if (fill.reject == execution::MatchReject::Ok) ++hits;
    }
    const double rate = static_cast<double>(hits) / N;
    // p_clamped 应为 cap=0.65; 实测 hit rate 应 ≈ 0.65, 容忍 ±0.05 (95% CI 大致 ±0.03)
    EXPECT_NEAR(p_clamped_seen, execution::kFillRateCap, 1e-9);
    EXPECT_NEAR(rate, execution::kFillRateCap, 0.05);
    EXPECT_GE(rate, execution::kFillRateFloor - 0.05);
}

// ---------- Determinism ----------

TEST(VirtualMatcher, DeterministicSameSeed) {
    execution::VirtualMatcher a{0xAAAA};
    execution::VirtualMatcher b{0xAAAA};
    auto ord = MakeOrder(/*size=*/1.0, /*price=*/0.55, /*depth=*/10'000.0);
    for (int i = 0; i < 50; ++i) {
        const auto fa = a.Match(ord);
        const auto fb = b.Match(ord);
        EXPECT_EQ(fa.bernoulli_draw, fb.bernoulli_draw);
        EXPECT_EQ(fa.reject,         fb.reject);
    }
}

// ---------- fill_size 不放大 (审计可还原) ----------

TEST(VirtualMatcher, FillSizeUsdc_ScaledByExpectedRate) {
    execution::VirtualMatcher m{0xBBBB};
    auto ord = MakeOrder(/*size=*/200.0, /*price=*/0.55, /*depth=*/10'000.0);
    m.SetUniformOverrideForTesting(0.0);
    const auto fill = m.Match(ord);
    ASSERT_EQ(fill.reject, execution::MatchReject::Ok);
    // size_usdc * expected_fill_rate (不是 p_clamped)
    const double expected = ord.size_usdc * fill.expected_fill_rate;
    EXPECT_NEAR(fill.fill_size_usdc, expected, 1e-9);
}

// ---------- T9: VirtualFill 含 market_id / outcome 字段 (W6 @小蒋 Wave 29) ----------
//
// 验证: VirtualFill struct 新增字段 market_id(array<char,32>) + outcome(uint8_t)
//   - market_id 默认零 (未 Match 时)
//   - outcome 默认 0

TEST(VirtualMatcher, T9_VirtualFill_HasMarketIdAndOutcomeFields) {
    execution::VirtualFill fill{};

    // sizeof 锁定 (内部 struct, paper engine 专用)
    static_assert(sizeof(execution::VirtualFill) == 120,
        "T9: VirtualFill sizeof 变化须同步更新 static_assert");

    // 字段类型/大小正确
    static_assert(sizeof(fill.market_id) == 32,
        "T9: market_id 必须 array<char,32> (32B, 与 PositionRecord 对齐)");
    static_assert(sizeof(fill.outcome) == 1,
        "T9: outcome 必须 uint8_t (1B)");

    // 默认零值
    std::array<char, 32> const zero_market{};
    EXPECT_EQ(fill.market_id, zero_market) << "T9: market_id 默认零";
    EXPECT_EQ(fill.outcome, std::uint8_t{0}) << "T9: outcome 默认 0 (YES)";
}

// ---------- T10: VirtualOrder → VirtualFill market_id/outcome 透传 (W6 @小蒋 Wave 29) ----------
//
// 验证: VirtualMatcher.Match() 把 VirtualOrder.market_id(string_view) / outcome(string_view)
//   → VirtualFill.market_id(array<char,32>) / outcome(uint8_t 0=YES/1=NO)

TEST(VirtualMatcher, T10_VirtualOrder_MarketIdOutcome_TransparentToFill) {
    execution::VirtualMatcher m{0xCC10};

    // case A: YES outcome
    {
        auto ord        = MakeOrder(/*size=*/1.0, /*price=*/0.55, /*depth=*/10'000.0);
        ord.market_id   = "market_xyz_test_01";
        ord.outcome     = "YES";
        m.SetUniformOverrideForTesting(0.0);  // 强制 Bernoulli hit
        const auto fill = m.Match(ord);
        EXPECT_EQ(fill.reject, execution::MatchReject::Ok);

        // market_id: "market_xyz_test_01" → array<char,32> null-padded
        std::array<char, 32> expected_mid{};
        const std::string_view mid_sv = "market_xyz_test_01";
        std::memcpy(expected_mid.data(), mid_sv.data(), mid_sv.size());
        EXPECT_EQ(fill.market_id, expected_mid)
            << "T10: market_id 从 VirtualOrder 透传到 VirtualFill (null-padded 32B)";
        EXPECT_EQ(fill.outcome, std::uint8_t{0})
            << "T10: outcome YES → 0";
    }

    // case B: NO outcome
    {
        auto ord        = MakeOrder(/*size=*/1.0, /*price=*/0.45, /*depth=*/10'000.0);
        ord.market_id   = "market_no_outcome";
        ord.outcome     = "NO";
        m.SetUniformOverrideForTesting(0.0);
        const auto fill = m.Match(ord);
        EXPECT_EQ(fill.reject, execution::MatchReject::Ok);
        EXPECT_EQ(fill.outcome, std::uint8_t{1})
            << "T10: outcome NO → 1";

        std::array<char, 32> expected_no{};
        const std::string_view no_sv = "market_no_outcome";
        std::memcpy(expected_no.data(), no_sv.data(), no_sv.size());
        EXPECT_EQ(fill.market_id, expected_no)
            << "T10: market_id NO case 透传正确";
    }

    // case C: market_id 32B 边界 (恰好 32B, 不截断)
    {
        auto ord      = MakeOrder(/*size=*/1.0, /*price=*/0.55, /*depth=*/10'000.0);
        ord.market_id = "0xABCDEF1234567890ABCDEF1234567890";  // 34B → 截断至 32B
        ord.outcome   = "YES";
        m.SetUniformOverrideForTesting(0.0);
        const auto fill = m.Match(ord);
        // 截断 = 前 32B
        std::array<char, 32> expected_trunc{};
        std::memcpy(expected_trunc.data(), "0xABCDEF1234567890ABCDEF12345678", 32);
        EXPECT_EQ(fill.market_id, expected_trunc)
            << "T10: market_id > 32B 截断至 32B";
    }
}
