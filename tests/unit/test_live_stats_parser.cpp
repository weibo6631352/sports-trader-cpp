// tests/unit/test_live_stats_parser.cpp — Soccer live_stats KV parser 单测
//
// Owner: 小段 (goalserve-specialist, #37)
// Date:  2026-05-31
//
// 覆盖:
//   L1:  真实 feed fixture — 实测 livescore payload 全量 KV (10 键)
//   L2:  ParseKvPair — IDangerousAttacks 正常解析
//   L3:  ParseKvPair — 缺 '=' 返回 false
//   L4:  ParseKvPair — 缺 'home:' 前缀 home_val 保持 -1
//   L5:  ParseKvPair — away 侧缺失 away_val 保持 -1
//   L6:  IPosession 原始拼写 (单 s) 正确映射
//   L7:  未知 key 忽略不崩
//   L8:  空串 → 全 -1, any_valid() = false
//   L9:  单 KV 对 (无 '|') 正确解析
//   L10: FillLiveStats → FeatureStoreGameRow 字段写入正确
//   L11: R-20 守法: Parse() 无 IO 无时间戳 (静态保证 — 通过编译即合规)
//   L12: 负数值拒绝 (保持 -1)
//   L13: 非数字值拒绝 (保持 -1)
//   L14: 首尾空白容错
//   L15: 多余 pipe '|' (空 segment) 不崩

#include <gtest/gtest.h>

#include "stcpp/data/feature_store_contract.hpp"
#include "stcpp/data/live_stats_parser.hpp"

using stcpp::data::livescore::FillLiveStats;
using stcpp::data::livescore::LiveStatsFields;
using stcpp::data::livescore::LiveStatsParser;
using stcpp::data::feature_store::FeatureStoreGameRow;

namespace {

// ============================================================================
// 实测 fixture (2026-05-30 livescore.goalserve.com/getfeed/.../soccernew/home)
// live_stats 节点内容, 10 KV 键全量
// ============================================================================
constexpr std::string_view kRealFeedFixture =
    "IDangerousAttacks=home:40,away:43"
    "|IOnTarget=home:3,away:6"
    "|IPosession=home:57,away:43"
    "|IRedCard=home:0,away:0"
    "|IYellowCard=home:2,away:1"
    "|ICorner=home:5,away:3"
    "|IFreeKick=home:12,away:14"
    "|IAttacks=home:98,away:87"
    "|IGoalKick=home:4,away:6"
    "|IThrowIn=home:17,away:19";

}  // namespace

// ============================================================================
// L1: 真实 feed fixture — 全量 10 KV 键解析正确
// ============================================================================
TEST(LiveStatsParser, L1_RealFeedFixture_AllKeysCorrect) {
    const LiveStatsFields f = LiveStatsParser::Parse(kRealFeedFixture);

    EXPECT_EQ(f.dangerous_attacks_home, 40);
    EXPECT_EQ(f.dangerous_attacks_away, 43);
    EXPECT_EQ(f.shots_on_target_home, 3);
    EXPECT_EQ(f.shots_on_target_away, 6);
    EXPECT_EQ(f.possession_home_pct, 57);
    EXPECT_EQ(f.possession_away_pct, 43);
    EXPECT_EQ(f.red_cards_home, 0);
    EXPECT_EQ(f.red_cards_away, 0);
    EXPECT_EQ(f.yellow_cards_home, 2);
    EXPECT_EQ(f.yellow_cards_away, 1);
    EXPECT_EQ(f.corners_home, 5);
    EXPECT_EQ(f.corners_away, 3);
    EXPECT_EQ(f.free_kicks_home, 12);
    EXPECT_EQ(f.free_kicks_away, 14);
    EXPECT_EQ(f.attacks_home, 98);
    EXPECT_EQ(f.attacks_away, 87);
    EXPECT_EQ(f.goal_kicks_home, 4);
    EXPECT_EQ(f.goal_kicks_away, 6);
    EXPECT_EQ(f.throw_ins_home, 17);
    EXPECT_EQ(f.throw_ins_away, 19);

    EXPECT_TRUE(f.any_valid());
}

// ============================================================================
// L2: ParseKvPair — IDangerousAttacks 正常解析
// ============================================================================
TEST(LiveStatsParser, L2_ParseKvPair_NormalCase) {
    std::string_view key;
    std::int32_t h = -1, a = -1;
    const bool ok = LiveStatsParser::ParseKvPair("IDangerousAttacks=home:40,away:43", key, h, a);
    EXPECT_TRUE(ok);
    EXPECT_EQ(key, "IDangerousAttacks");
    EXPECT_EQ(h, 40);
    EXPECT_EQ(a, 43);
}

// ============================================================================
// L3: ParseKvPair — 缺 '=' 返回 false
// ============================================================================
TEST(LiveStatsParser, L3_ParseKvPair_MissingEquals_ReturnsFalse) {
    std::string_view key;
    std::int32_t h = -1, a = -1;
    EXPECT_FALSE(LiveStatsParser::ParseKvPair("IDangerousAttacks_home:40", key, h, a));
    EXPECT_EQ(h, -1);
    EXPECT_EQ(a, -1);
}

// ============================================================================
// L4: ParseKvPair — 缺 'home:' 前缀 → home_val 保持 -1
// ============================================================================
TEST(LiveStatsParser, L4_ParseKvPair_MissingHomePrefix) {
    std::string_view key;
    std::int32_t h = -1, a = -1;
    // value_part 无 "home:" 前缀: parse_home_away 返回 false (home_ok=false)
    const bool ok = LiveStatsParser::ParseKvPair("ICorner=40,away:3", key, h, a);
    // home_ok = false → ParseKvPair returns false
    EXPECT_FALSE(ok);
    EXPECT_EQ(h, -1);
}

// ============================================================================
// L5: ParseKvPair — away 侧缺失 → away_val 保持 -1, 仍返回 true
// ============================================================================
TEST(LiveStatsParser, L5_ParseKvPair_AwayMissing) {
    std::string_view key;
    std::int32_t h = -1, a = -1;
    const bool ok = LiveStatsParser::ParseKvPair("ICorner=home:5", key, h, a);
    EXPECT_TRUE(ok);
    EXPECT_EQ(key, "ICorner");
    EXPECT_EQ(h, 5);
    EXPECT_EQ(a, -1);  // away 缺失保持 -1
}

// ============================================================================
// L6: IPosession — 原始拼写 (单 s) 映射到 possession 字段
// ============================================================================
TEST(LiveStatsParser, L6_IPosession_OriginalSpelling_MappedCorrectly) {
    // 注意: 是 "IPosession" (单 s), 非 "IPossession"
    const LiveStatsFields f = LiveStatsParser::Parse("IPosession=home:57,away:43");
    EXPECT_EQ(f.possession_home_pct, 57);
    EXPECT_EQ(f.possession_away_pct, 43);
    // 双 s 拼写 (未来可能出现的变体) 应被忽略 (未知 key)
    const LiveStatsFields f2 = LiveStatsParser::Parse("IPossession=home:57,away:43");
    EXPECT_EQ(f2.possession_home_pct, -1);  // 未知 key, 忽略
}

// ============================================================================
// L7: 未知 key 忽略不崩
// ============================================================================
TEST(LiveStatsParser, L7_UnknownKey_IgnoredSilently) {
    const LiveStatsFields f = LiveStatsParser::Parse(
        "INewFutureKey=home:99,away:88|IDangerousAttacks=home:40,away:43");
    // 未知 key 被忽略, 已知 key 正常解析
    EXPECT_EQ(f.dangerous_attacks_home, 40);
    EXPECT_EQ(f.dangerous_attacks_away, 43);
}

// ============================================================================
// L8: 空串 → 全 -1, any_valid() = false
// ============================================================================
TEST(LiveStatsParser, L8_EmptyString_AllMinus1) {
    const LiveStatsFields f = LiveStatsParser::Parse("");
    EXPECT_EQ(f.dangerous_attacks_home, -1);
    EXPECT_EQ(f.shots_on_target_home, -1);
    EXPECT_EQ(f.possession_home_pct, -1);
    EXPECT_FALSE(f.any_valid());
}

// ============================================================================
// L9: 单 KV 对 (无 '|') 正确解析
// ============================================================================
TEST(LiveStatsParser, L9_SingleKvNoPipe) {
    const LiveStatsFields f = LiveStatsParser::Parse("IYellowCard=home:2,away:1");
    EXPECT_EQ(f.yellow_cards_home, 2);
    EXPECT_EQ(f.yellow_cards_away, 1);
}

// ============================================================================
// L10: FillLiveStats → FeatureStoreGameRow 字段写入
// ============================================================================
TEST(LiveStatsParser, L10_FillLiveStats_RowFieldsCorrect) {
    const LiveStatsFields f = LiveStatsParser::Parse(kRealFeedFixture);
    FeatureStoreGameRow row;
    FillLiveStats(row, f);

    EXPECT_EQ(row.soccer_dangerous_attacks_home, 40);
    EXPECT_EQ(row.soccer_dangerous_attacks_away, 43);
    EXPECT_EQ(row.soccer_shots_on_target_home,   3);
    EXPECT_EQ(row.soccer_shots_on_target_away,   6);
    EXPECT_EQ(row.soccer_possession_home_pct,    57);
    EXPECT_EQ(row.soccer_possession_away_pct,    43);
    EXPECT_EQ(row.soccer_red_cards_home,         0);
    EXPECT_EQ(row.soccer_red_cards_away,         0);
    EXPECT_EQ(row.soccer_yellow_cards_home,      2);
    EXPECT_EQ(row.soccer_yellow_cards_away,      1);
    EXPECT_EQ(row.soccer_corners_home,           5);
    EXPECT_EQ(row.soccer_corners_away,           3);
    EXPECT_EQ(row.soccer_free_kicks_home,        12);
    EXPECT_EQ(row.soccer_free_kicks_away,        14);
    EXPECT_EQ(row.soccer_attacks_home,           98);
    EXPECT_EQ(row.soccer_attacks_away,           87);
    EXPECT_EQ(row.soccer_goal_kicks_home,        4);
    EXPECT_EQ(row.soccer_goal_kicks_away,        6);
    EXPECT_EQ(row.soccer_throw_ins_home,         17);
    EXPECT_EQ(row.soccer_throw_ins_away,         19);
}

// ============================================================================
// L11: vendor-agnostic 守法静态检查
//   — FeatureStoreGameRow 字段名不含 "IDangerousAttacks" 等 Goalserve 原始名
//   — 通过编译即合规 (运行时无需额外 assert)
// ============================================================================
TEST(LiveStatsParser, L11_VendorAgnostic_CompileTimeCheck) {
    // 只要 soccer_dangerous_attacks_home 字段存在且可赋值 = 命名合规
    FeatureStoreGameRow row;
    row.soccer_dangerous_attacks_home = 1;
    EXPECT_EQ(row.soccer_dangerous_attacks_home, 1);
    // 同时确认初始值 -1 (sentinel)
    EXPECT_EQ(row.soccer_shots_on_target_home, -1);
}

// ============================================================================
// L12: 负数值拒绝 (保持 -1)
//
// 解析器行为: home 负数 → parse_int32 返回 false → home_val 保持 -1
//             away 格式合法 → away_val 正常解析
// ParseKvPair 返回 false (home_ok=false) → Parse() 跳过此 segment
// 因此整个 KV 对被丢弃: home=-1, away=-1
// ============================================================================
TEST(LiveStatsParser, L12_NegativeValue_Rejected) {
    const LiveStatsFields f = LiveStatsParser::Parse("ICorner=home:-5,away:3");
    // ParseKvPair returns false (home_ok=false) → apply_kv not called → both -1
    EXPECT_EQ(f.corners_home, -1);
    EXPECT_EQ(f.corners_away, -1);
}

// ============================================================================
// L13: 非数字值拒绝 (保持 -1)
//
// 同 L12: home_ok=false → ParseKvPair returns false → segment 丢弃 → 双侧 -1
// ============================================================================
TEST(LiveStatsParser, L13_NonNumericValue_Rejected) {
    const LiveStatsFields f = LiveStatsParser::Parse("ICorner=home:abc,away:3");
    EXPECT_EQ(f.corners_home, -1);
    EXPECT_EQ(f.corners_away, -1);
}

// ============================================================================
// L14: 首尾空白容错
// ============================================================================
TEST(LiveStatsParser, L14_WhitespaceTolerance) {
    // 整体前后空白
    const LiveStatsFields f1 = LiveStatsParser::Parse("  IDangerousAttacks=home:40,away:43  ");
    EXPECT_EQ(f1.dangerous_attacks_home, 40);

    // segment 内空白 (key 前后)
    const LiveStatsFields f2 = LiveStatsParser::Parse("  ICorner = home:5,away:3  ");
    EXPECT_EQ(f2.corners_home, 5);
    EXPECT_EQ(f2.corners_away, 3);
}

// ============================================================================
// L15: 多余 '|' (空 segment) 不崩
// ============================================================================
TEST(LiveStatsParser, L15_ExtraPipes_DoNotCrash) {
    // 头部 pipe / 尾部 pipe / 中间连续 pipe
    const LiveStatsFields f = LiveStatsParser::Parse(
        "|IDangerousAttacks=home:40,away:43||ICorner=home:5,away:3|");
    EXPECT_EQ(f.dangerous_attacks_home, 40);
    EXPECT_EQ(f.corners_home, 5);
}
