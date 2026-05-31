// tests/unit/test_fill_event.cpp — PositionLedger 中性 FillEvent 重载 (老郭 R-4 审计放行)
//
// Owner: GM (老雷) 2026-05-31。
// 验证: FillEvent 重载 R-11 fail-closed 方向不变 (mode_tag!=0 拒) + size==0 拒 + 正常记账。
#include "stcpp/risk/position_ledger.hpp"

#include <gtest/gtest.h>

namespace {

using stcpp::risk::FillEvent;
using stcpp::risk::Outcome;
using stcpp::risk::PositionLedger;

FillEvent paper_fill(std::int64_t size_micro, double price) {
    FillEvent ev;
    ev.filled_size_micro = size_micro;
    ev.fill_price = price;
    ev.mode_tag = 0;  // paper
    ev.as_of_ts_ns = 1'000'000;
    return ev;
}

}  // namespace

// 正常 paper FillEvent → 记账。
TEST(FillEventLedger, PaperFillRecorded) {
    PositionLedger led;
    led.apply_fill("0xcond", "tok1", Outcome::Yes, paper_fill(1'000'000, 0.5));
    const auto pos = led.get_position("tok1");
    ASSERT_TRUE(pos.has_value());
    EXPECT_EQ(pos->size_usdc, 1'000'000);
    EXPECT_DOUBLE_EQ(pos->avg_entry_price, 0.5);
}

// R-11 fail-closed 方向不变: mode_tag!=0 (live) → 拒, 不污染 paper 账本。
TEST(FillEventLedger, NonPaperModeTagRejected) {
    PositionLedger led;
    FillEvent ev = paper_fill(1'000'000, 0.5);
    ev.mode_tag = 1;  // live
    led.apply_fill("0xcond", "tok1", Outcome::Yes, ev);
    EXPECT_FALSE(led.get_position("tok1").has_value()) << "R-11 违反: live fill 污染 paper 账本";
}

// size==0 → 不记账。
TEST(FillEventLedger, ZeroSizeRejected) {
    PositionLedger led;
    led.apply_fill("0xcond", "tok1", Outcome::Yes, paper_fill(0, 0.5));
    EXPECT_FALSE(led.get_position("tok1").has_value());
}
