// tests/unit/test_onchain_positions.cpp
//
// ParseOpenPositions — data-api positions JSON 数组 → 各 open 仓 (live 启动对账, 2026-06-13 真钱事故 #2)。
// 验证: ① 只取 open (redeemable=false), 跳已结算 ② 字段解析 (asset/conditionId/size/avgPrice/outcomeIndex/
//        negativeRisk) ③ 脏数据过滤 (股≤0/价越界/token 空) ④ 空数组/非数组 → 空 ⑤ 对象切分 string-aware
//        (title 含花括号/逗号不破坏切分)。
#include <gtest/gtest.h>

#include <string>

#include "stcpp/polymarket/onchain_positions.hpp"

using stcpp::polymarket::OnchainPosition;
using stcpp::polymarket::ParseOpenPositions;

namespace {

// 真实 data-api 字段形态 (2026-06-13 实测裁剪)。
constexpr const char* kSample = R"JSON([
  {"proxyWallet":"0x78de","asset":"659875030161613507164421091466","conditionId":"0xe478b487c0","size":27.3343,
   "avgPrice":0.7737,"currentValue":23.37,"redeemable":false,"title":"Nottingham Open: Klugman vs Zakharova",
   "outcome":"Hannah Klugman","outcomeIndex":0,"negativeRisk":false},
  {"proxyWallet":"0x78de","asset":"111004315157609337693010886067","conditionId":"0xabc123","size":12.5,
   "avgPrice":0.40,"redeemable":false,"outcome":"No","outcomeIndex":1,"negativeRisk":true},
  {"proxyWallet":"0x78de","asset":"222000","conditionId":"0xdead","size":11232.681,"avgPrice":0.0,
   "redeemable":true,"outcome":"O 8.5","outcomeIndex":0,"negativeRisk":false}
])JSON";

}  // namespace

TEST(OnchainPositions, ParsesOpenSkipsRedeemable) {
    const auto out = ParseOpenPositions(kSample);
    ASSERT_EQ(out.size(), 2u) << "3 仓中 1 个 redeemable=true 应跳, 余 2 open";

    EXPECT_EQ(out[0].token_id, "659875030161613507164421091466");
    EXPECT_EQ(out[0].condition_id, "0xe478b487c0");
    EXPECT_NEAR(out[0].shares, 27.3343, 1e-9);
    EXPECT_NEAR(out[0].avg_price, 0.7737, 1e-9);
    EXPECT_EQ(out[0].outcome_index, 0);
    EXPECT_FALSE(out[0].neg_risk);

    EXPECT_EQ(out[1].token_id, "111004315157609337693010886067");
    EXPECT_EQ(out[1].outcome_index, 1);
    EXPECT_NEAR(out[1].avg_price, 0.40, 1e-9);
    EXPECT_TRUE(out[1].neg_risk) << "negativeRisk=true 应解出";
}

TEST(OnchainPositions, SkipsDirty) {
    // 股=0 / 价>1 / 价=0 / token 空 → 全跳。
    constexpr const char* dirty = R"JSON([
      {"asset":"1","conditionId":"0xa","size":0,"avgPrice":0.5,"redeemable":false,"outcomeIndex":0},
      {"asset":"2","conditionId":"0xb","size":5,"avgPrice":1.5,"redeemable":false,"outcomeIndex":0},
      {"asset":"3","conditionId":"0xc","size":5,"avgPrice":0,"redeemable":false,"outcomeIndex":0},
      {"asset":"","conditionId":"0xd","size":5,"avgPrice":0.5,"redeemable":false,"outcomeIndex":0}
    ])JSON";
    EXPECT_TRUE(ParseOpenPositions(dirty).empty()) << "脏数据全跳, 不进真钱账本";
}

TEST(OnchainPositions, EmptyArrayAndNonArray) {
    EXPECT_TRUE(ParseOpenPositions("[]").empty()) << "空数组 → 空";
    EXPECT_TRUE(ParseOpenPositions("{\"error\":\"forbidden\"}").empty()) << "非数组对象 → 空 (无 '[' 前缀)";
    EXPECT_TRUE(ParseOpenPositions("").empty()) << "空串 → 空";
}

TEST(OnchainPositions, StringAwareObjectSplit) {
    // title 含 '}' '{' ',' → 不得破坏对象切分 (brace-depth + string-aware)。
    constexpr const char* tricky = R"JSON([
      {"asset":"1","conditionId":"0xa","title":"weird }{ , title","size":5,"avgPrice":0.6,"redeemable":false,"outcomeIndex":0},
      {"asset":"2","conditionId":"0xb","title":"another, one","size":7,"avgPrice":0.3,"redeemable":false,"outcomeIndex":1}
    ])JSON";
    const auto out = ParseOpenPositions(tricky);
    ASSERT_EQ(out.size(), 2u) << "字符串内的花括号/逗号不破坏切分";
    EXPECT_EQ(out[0].token_id, "1");
    EXPECT_EQ(out[1].token_id, "2");
    EXPECT_NEAR(out[1].shares, 7.0, 1e-9);
}
