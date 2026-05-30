// tests/unit/test_market_discovery.cpp — market_discovery 单测草案
//
// Owner: 小宋 (test + replay, E 产品业务保障部)
// last_review: 2026-05-30
//
// 覆盖: MD-01..47 (discovery_detail 纯函数 + ParseSports* 两个 Parse 层函数)
//   §2.1 ExtractClobTokenIds    MD-01..08
//   §2.2 ExtractNextObject      MD-10..16
//   §2.3 NormalizeSportsMarketType  MD-20..31
//   §2.4 ParseSportsEvents      MD-40..47 (fixture JSON 喂, 无网络)
//   额外: ExtractJsonStr 基础用例 + ExtractMarketsArray 覆盖
//
// 链接库: stcpp_paper_app (包含 market_discovery.cpp)
// 注册: tests/unit/CMakeLists.txt §6 paper-only 块
//   EXTRA_LIBS_test_market_discovery stcpp_paper_app
//   LABELS_test_market_discovery "unit;paper;market-discovery;gamma;parse;xiaosong"

#include "stcpp/app/market_discovery.hpp"

#include <gtest/gtest.h>
#include <string>

using namespace stcpp::app;
using namespace stcpp::app::discovery_detail;

// ============================================================================
// §2.1  ExtractClobTokenIds — 双编码 (MD-01..08)
// ============================================================================

// MD-01: 原生 array, 两 token 正常
TEST(ExtractClobTokenIds, MD01_NativeArrayTwoTokens) {
    const std::string obj = R"({"clobTokenIds":["tok-aaa","tok-bbb"]})";
    std::string tok0, tok1;
    EXPECT_TRUE(ExtractClobTokenIds(obj, tok0, tok1));
    EXPECT_EQ(tok0, "tok-aaa");
    EXPECT_EQ(tok1, "tok-bbb");
}

// MD-02: JSON-encoded string (gamma /events 真实编码)
TEST(ExtractClobTokenIds, MD02_JsonEncodedString) {
    // 构造: "clobTokenIds":"[\"tok-aaa\",\"tok-bbb\"]"
    const std::string obj = R"({"clobTokenIds":"[\"tok-aaa\",\"tok-bbb\"]"})";
    std::string tok0, tok1;
    EXPECT_TRUE(ExtractClobTokenIds(obj, tok0, tok1));
    EXPECT_EQ(tok0, "tok-aaa");
    EXPECT_EQ(tok1, "tok-bbb");
}

// MD-03: 仅 1 token, return false
TEST(ExtractClobTokenIds, MD03_OnlyOneToken) {
    const std::string obj = R"({"clobTokenIds":["only-one"]})";
    std::string tok0, tok1;
    EXPECT_FALSE(ExtractClobTokenIds(obj, tok0, tok1));
}

// MD-04: 空原生数组, return false
TEST(ExtractClobTokenIds, MD04_EmptyNativeArray) {
    const std::string obj = R"({"clobTokenIds":[]})";
    std::string tok0, tok1;
    EXPECT_FALSE(ExtractClobTokenIds(obj, tok0, tok1));
}

// MD-05: JSON-encoded 空数组 "[]"
TEST(ExtractClobTokenIds, MD05_JsonEncodedEmptyArray) {
    const std::string obj = R"({"clobTokenIds":"[]"})";
    std::string tok0, tok1;
    EXPECT_FALSE(ExtractClobTokenIds(obj, tok0, tok1));
}

// MD-06: JSON-encoded string, token 含反斜杠字符 (合法 escape 序列 \\)
// outer JSON: "clobTokenIds":"[\"tok-with\\\\slash\",\"tok-b\"]"
// unescape 后 content = ["tok-with\\slash","tok-b"]
// 内层简单 " 扫描: tok0="tok-with\\slash", tok1="tok-b"
// 注意: 若 token 内部含 \" (引号转义), 内层简单扫描会截断 — 已知实现限制;
//       本 case 避开该限制, 验证合法的双反斜杠转义路径.
TEST(ExtractClobTokenIds, MD06_EscapedBackslashInEncodedString) {
    // 构造: {"clobTokenIds":"[\"tok-with\\\\slash\",\"tok-b\"]"}
    // 目标 outer string value = [\"tok-with\\slash\",\"tok-b\"]
    // unescape: [\"→", \\→\] → content = ["tok-with\slash","tok-b"]
    // 内层扫描: tok0="tok-with\slash", tok1="tok-b"
    const std::string obj =
        "{\"clobTokenIds\":\"[\\\"tok-with\\\\\\\\slash\\\",\\\"tok-b\\\"]\"}";
    std::string tok0, tok1;
    EXPECT_TRUE(ExtractClobTokenIds(obj, tok0, tok1));
    EXPECT_EQ(tok1, "tok-b");
    // tok0 含反斜杠 (unescape 后的 \slash)
    EXPECT_FALSE(tok0.empty());
}

// MD-07: 字段缺失 (无 "clobTokenIds": 键)
TEST(ExtractClobTokenIds, MD07_FieldMissing) {
    const std::string obj = R"({"conditionId":"cond-123","question":"Who wins?"})";
    std::string tok0, tok1;
    EXPECT_FALSE(ExtractClobTokenIds(obj, tok0, tok1));
}

// MD-08: 超过 2 token, 取前两个
TEST(ExtractClobTokenIds, MD08_MoreThanTwoTokens) {
    const std::string obj = R"({"clobTokenIds":["tok-a","tok-b","tok-c"]})";
    std::string tok0, tok1;
    EXPECT_TRUE(ExtractClobTokenIds(obj, tok0, tok1));
    EXPECT_EQ(tok0, "tok-a");
    EXPECT_EQ(tok1, "tok-b");
}

// 额外: 带空格的原生 array (格式松散)
TEST(ExtractClobTokenIds, Extra_NativeArrayWithSpaces) {
    const std::string obj = R"({"clobTokenIds": ["tok-x", "tok-y"]})";
    std::string tok0, tok1;
    EXPECT_TRUE(ExtractClobTokenIds(obj, tok0, tok1));
    EXPECT_EQ(tok0, "tok-x");
    EXPECT_EQ(tok1, "tok-y");
}

// ============================================================================
// §2.2  ExtractNextObject — 平衡括号 (MD-10..16)
// ============================================================================

// MD-10: 最简单对象
TEST(ExtractNextObject, MD10_SimpleObject) {
    const std::string s = R"({"a":1})";
    // s = { " a " : 1 }
    //     0 1 2 3 4 5 6
    auto [start, end] = ExtractNextObject(s, 0);
    EXPECT_EQ(start, 0u);
    EXPECT_EQ(end, 6u);
    EXPECT_EQ(s.substr(start, end - start + 1), s);
}

// MD-11: 嵌套对象
TEST(ExtractNextObject, MD11_NestedObject) {
    const std::string s = R"({"a":{"b":2}})";
    // 共 13 字符, 0-12
    auto [start, end] = ExtractNextObject(s, 0);
    EXPECT_EQ(start, 0u);
    EXPECT_EQ(end, static_cast<std::size_t>(s.size() - 1));
    EXPECT_EQ(s[end], '}');
}

// MD-12: 字符串内的 } 不计
TEST(ExtractNextObject, MD12_ClosingBraceInString) {
    const std::string s = R"({"a":"has }"})";
    auto [start, end] = ExtractNextObject(s, 0);
    EXPECT_EQ(start, 0u);
    EXPECT_EQ(end, static_cast<std::size_t>(s.size() - 1));
    EXPECT_EQ(s[end], '}');
}

// MD-13: 字符串内转义引号 (不影响括号深度计数)
TEST(ExtractNextObject, MD13_EscapedQuoteInString) {
    // {"a":"\""}  — a 的值是 " (一个双引号)
    const std::string s = "{\"a\":\"\\\"\"}";
    auto [start, end] = ExtractNextObject(s, 0);
    EXPECT_EQ(start, 0u);
    EXPECT_EQ(end, static_cast<std::size_t>(s.size() - 1));
    EXPECT_EQ(s[end], '}');
}

// MD-14: 以 ']' 开头, 立即返回 {npos, npos}
TEST(ExtractNextObject, MD14_StartsWithClosingBracket) {
    const std::string s = "]";
    auto [start, end] = ExtractNextObject(s, 0);
    EXPECT_EQ(start, std::string::npos);
    EXPECT_EQ(end, std::string::npos);
}

// MD-15: 空串
TEST(ExtractNextObject, MD15_EmptyString) {
    const std::string s;
    auto [start, end] = ExtractNextObject(s, 0);
    EXPECT_EQ(start, std::string::npos);
    EXPECT_EQ(end, std::string::npos);
}

// MD-16: 未闭合括号
TEST(ExtractNextObject, MD16_UnclosedBrace) {
    const std::string s = R"({"unclosed")";
    auto [start, end] = ExtractNextObject(s, 0);
    EXPECT_EQ(start, std::string::npos);
    EXPECT_EQ(end, std::string::npos);
}

// 额外: 连续两个对象, pos=0 取第一个, pos=推进后取第二个
TEST(ExtractNextObject, Extra_TwoConsecutiveObjects) {
    const std::string s = R"({"x":1},{"y":2})";
    auto [s1, e1] = ExtractNextObject(s, 0);
    EXPECT_EQ(s1, 0u);
    EXPECT_EQ(s[e1], '}');
    // 第二个对象
    auto [s2, e2] = ExtractNextObject(s, e1 + 1);
    EXPECT_NE(s2, std::string::npos);
    EXPECT_EQ(s[e2], '}');
    EXPECT_EQ(s.substr(s2, e2 - s2 + 1), R"({"y":2})");
}

// 额外: 数组内的对象, pos=1 (跳过 '[')
TEST(ExtractNextObject, Extra_ObjectInsideArray) {
    const std::string s = R"([{"k":"v"}])";
    auto [start, end] = ExtractNextObject(s, 1);
    EXPECT_NE(start, std::string::npos);
    EXPECT_EQ(s.substr(start, end - start + 1), R"({"k":"v"})");
}

// ============================================================================
// §2.3  NormalizeSportsMarketType (MD-20..31)
// ============================================================================

TEST(NormalizeSportsMarketType, MD20_MoneylineCapitalized) {
    EXPECT_EQ(NormalizeSportsMarketType("Moneyline"), "moneyline");
}

TEST(NormalizeSportsMarketType, MD21_MoneylineAllCaps) {
    EXPECT_EQ(NormalizeSportsMarketType("MONEYLINE"), "moneyline");
}

TEST(NormalizeSportsMarketType, MD22_MoneylineLowercase) {
    EXPECT_EQ(NormalizeSportsMarketType("moneyline"), "moneyline");
}

TEST(NormalizeSportsMarketType, MD23_Spread) {
    EXPECT_EQ(NormalizeSportsMarketType("Spread"), "spread");
}

TEST(NormalizeSportsMarketType, MD24_TotalPoints) {
    EXPECT_EQ(NormalizeSportsMarketType("Total Points"), "totals");
}

TEST(NormalizeSportsMarketType, MD25_OverUnder) {
    EXPECT_EQ(NormalizeSportsMarketType("Over/Under"), "totals");
}

TEST(NormalizeSportsMarketType, MD26_Outright) {
    EXPECT_EQ(NormalizeSportsMarketType("Outright"), "outright");
}

TEST(NormalizeSportsMarketType, MD27_Futures) {
    EXPECT_EQ(NormalizeSportsMarketType("Futures"), "outright");
}

TEST(NormalizeSportsMarketType, MD28_PropBet) {
    EXPECT_EQ(NormalizeSportsMarketType("Prop Bet"), "prop");
}

TEST(NormalizeSportsMarketType, MD29_Series) {
    EXPECT_EQ(NormalizeSportsMarketType("Series"), "series");
}

TEST(NormalizeSportsMarketType, MD30_EmptyString) {
    EXPECT_EQ(NormalizeSportsMarketType(""), "unknown");
}

// MD-31: 无匹配 → 原样返回
TEST(NormalizeSportsMarketType, MD31_UnknownPassthrough) {
    EXPECT_EQ(NormalizeSportsMarketType("xyzunsupported"), "xyzunsupported");
}

// 额外: "money" 子串匹配 (非全词)
TEST(NormalizeSportsMarketType, Extra_MoneySubstring) {
    EXPECT_EQ(NormalizeSportsMarketType("money line"), "moneyline");
}

// 额外: spread 混合大小写
TEST(NormalizeSportsMarketType, Extra_SpreadMixedCase) {
    EXPECT_EQ(NormalizeSportsMarketType("Point Spread"), "spread");
}

// ============================================================================
// §2.4  ParseSportsEvents — fixture JSON (MD-40..47)
// ============================================================================

// 辅助函数: 构造一个最小合法的 market JSON 对象 (字符串)
// conditionId, question, clobTokenIds (native array)
static std::string MakeMarket(const std::string& condition_id,
                               const std::string& question,
                               const std::string& tok0,
                               const std::string& tok1,
                               const std::string& smt = "Moneyline") {
    return "{\"conditionId\":\"" + condition_id + "\","
           "\"question\":\"" + question + "\","
           "\"sportsMarketType\":\"" + smt + "\","
           "\"clobTokenIds\":[\"" + tok0 + "\",\"" + tok1 + "\"]}";
}

// 辅助函数: 构造一个最小合法的 event JSON 对象
// sport 字段非空即可通过 sports filter
static std::string MakeEvent(const std::string& event_id,
                              const std::string& title,
                              const std::string& sport,
                              const std::string& markets_content) {
    return "{\"id\":\"" + event_id + "\","
           "\"slug\":\"" + event_id + "-slug\","
           "\"title\":\"" + title + "\","
           "\"sport\":\"" + sport + "\","
           "\"markets\":[" + markets_content + "]}";
}

// MD-40: 2 个 event, 每个 2 markets, native array clobTokenIds
TEST(ParseSportsEvents, MD40_TwoEventsTwoMarketsEach) {
    const std::string m1 = MakeMarket("cond-1a", "Q1", "tok-1a-y", "tok-1a-n");
    const std::string m2 = MakeMarket("cond-1b", "Q2", "tok-1b-y", "tok-1b-n");
    const std::string m3 = MakeMarket("cond-2a", "Q3", "tok-2a-y", "tok-2a-n");
    const std::string m4 = MakeMarket("cond-2b", "Q4", "tok-2b-y", "tok-2b-n");
    const std::string ev1 = MakeEvent("ev-1", "NBA Finals", "basketball", m1 + "," + m2);
    const std::string ev2 = MakeEvent("ev-2", "NFL Sunday", "football",   m3 + "," + m4);
    const std::string json = "[" + ev1 + "," + ev2 + "]";

    auto result = ParseSportsEvents(json, 30);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].event_id, "ev-1");
    EXPECT_EQ(result[0].markets.size(), 2u);
    EXPECT_EQ(result[1].event_id, "ev-2");
    EXPECT_EQ(result[1].markets.size(), 2u);

    // token_map 等价: 4 条 market
    std::size_t total_markets = result[0].markets.size() + result[1].markets.size();
    EXPECT_EQ(total_markets, 4u);

    EXPECT_EQ(result[0].markets[0].condition_id, "cond-1a");
    EXPECT_EQ(result[0].markets[0].token0_id, "tok-1a-y");
    EXPECT_EQ(result[0].markets[0].token1_id, "tok-1a-n");
}

// MD-41: conditionId 缺失, market 被跳过 → event 无 markets → event 也跳过
TEST(ParseSportsEvents, MD41_MissingConditionId) {
    // market 无 conditionId 字段
    const std::string m = "{\"question\":\"Who wins?\","
                          "\"clobTokenIds\":[\"tok-y\",\"tok-n\"]}";
    const std::string ev = MakeEvent("ev-41", "NBA Game", "basketball", m);
    const std::string json = "[" + ev + "]";

    auto result = ParseSportsEvents(json, 30);
    EXPECT_TRUE(result.empty());
}

// MD-42: markets:[] 空数组 → event 被跳过, result 为空
TEST(ParseSportsEvents, MD42_EmptyMarketsArray) {
    const std::string ev = "{\"id\":\"ev-42\",\"slug\":\"ev-42\","
                           "\"title\":\"NBA Game\",\"sport\":\"basketball\","
                           "\"markets\":[]}";
    const std::string json = "[" + ev + "]";

    auto result = ParseSportsEvents(json, 30);
    EXPECT_TRUE(result.empty());
}

// MD-43: sport 为 "null" 字符串, title 含 "NBA" → 关键词匹配, event 纳入
TEST(ParseSportsEvents, MD43_SportNullTitleNba) {
    const std::string m = MakeMarket("cond-43", "NBA Winner", "tok-43-y", "tok-43-n");
    const std::string ev = "{\"id\":\"ev-43\",\"slug\":\"ev-43-slug\","
                           "\"title\":\"NBA Playoffs\","
                           "\"sport\":\"null\","
                           "\"markets\":[" + m + "]}";
    const std::string json = "[" + ev + "]";

    auto result = ParseSportsEvents(json, 30);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].event_id, "ev-43");
    EXPECT_EQ(result[0].sport, "null");
}

// MD-44: sport 为空, title 无体育关键词 → event 被过滤
TEST(ParseSportsEvents, MD44_NonSportEvent) {
    const std::string m = MakeMarket("cond-44", "Will X happen?", "tok-44-y", "tok-44-n");
    const std::string ev = "{\"id\":\"ev-44\",\"slug\":\"ev-44\","
                           "\"title\":\"Political Election 2026\","
                           "\"sport\":\"\","
                           "\"markets\":[" + m + "]}";
    const std::string json = "[" + ev + "]";

    auto result = ParseSportsEvents(json, 30);
    EXPECT_TRUE(result.empty());
}

// MD-45: 3 events, max_events=2 → result.size()==2 (截断)
TEST(ParseSportsEvents, MD45_MaxEventsTruncation) {
    const std::string m = MakeMarket("cond-x", "Q", "tok-y", "tok-n");
    const std::string ev1 = MakeEvent("ev-45a", "NBA Game 1", "basketball", m);
    // 第二个 market conditionId 不同
    const std::string m2 = MakeMarket("cond-x2", "Q2", "tok-y2", "tok-n2");
    const std::string ev2 = MakeEvent("ev-45b", "NBA Game 2", "basketball", m2);
    const std::string m3 = MakeMarket("cond-x3", "Q3", "tok-y3", "tok-n3");
    const std::string ev3 = MakeEvent("ev-45c", "NBA Game 3", "basketball", m3);
    const std::string json = "[" + ev1 + "," + ev2 + "," + ev3 + "]";

    auto result = ParseSportsEvents(json, 2);
    EXPECT_EQ(result.size(), 2u);
}

// MD-46: JSON-encoded clobTokenIds 格式 → token_map 正确填充
TEST(ParseSportsEvents, MD46_JsonEncodedClobTokenIds) {
    // market 使用 JSON-encoded string 格式
    const std::string m = "{\"conditionId\":\"cond-46\","
                          "\"question\":\"NBA winner?\","
                          "\"sportsMarketType\":\"Moneyline\","
                          "\"clobTokenIds\":\"[\\\"tok-46-y\\\",\\\"tok-46-n\\\"]\"}";
    const std::string ev = MakeEvent("ev-46", "NBA Finals", "basketball", m);
    const std::string json = "[" + ev + "]";

    auto result = ParseSportsEvents(json, 30);
    ASSERT_EQ(result.size(), 1u);
    ASSERT_EQ(result[0].markets.size(), 1u);
    EXPECT_EQ(result[0].markets[0].token0_id, "tok-46-y");
    EXPECT_EQ(result[0].markets[0].token1_id, "tok-46-n");
}

// MD-47: 嵌套引号 "question":"NBA \"Finals\" 2026" → ExtractJsonStr 不截断
// ExtractJsonStr 不做 unescape, 返回 raw bytes (含 \").
// 关键验证: 字符串不被 \" 提前截断 (esc 状态机保证跨越 \" 继续扫), 包含完整内容.
TEST(ParseSportsEvents, MD47_EscapedQuoteInQuestion) {
    // question 含转义引号
    const std::string m = "{\"conditionId\":\"cond-47\","
                          "\"question\":\"NBA \\\"Finals\\\" 2026\","
                          "\"sportsMarketType\":\"Moneyline\","
                          "\"clobTokenIds\":[\"tok-47-y\",\"tok-47-n\"]}";
    const std::string ev = MakeEvent("ev-47", "NBA game", "basketball", m);
    const std::string json = "[" + ev + "]";

    auto result = ParseSportsEvents(json, 30);
    ASSERT_EQ(result.size(), 1u);
    ASSERT_EQ(result[0].markets.size(), 1u);
    // ExtractJsonStr 不截断: question 包含 "Finals" 和 "2026" (不被首个 \" 截断)
    const std::string& q = result[0].markets[0].question;
    EXPECT_NE(q.find("Finals"), std::string::npos) << "question truncated at escaped quote: " << q;
    EXPECT_NE(q.find("2026"), std::string::npos)   << "question missing trailing content: " << q;
    // raw bytes 含反斜杠 (不做 unescape)
    EXPECT_NE(q.find('\\'), std::string::npos);
}

// ============================================================================
// ParseSportsMarketsFlat — 回退路径 (额外覆盖)
// ============================================================================

// 基础: 一个 basketball market → 包成 synthetic event
TEST(ParseSportsMarketsFlat, Basic_OneBasketballMarket) {
    const std::string m = MakeMarket("cond-f1", "NBA Finals winner?", "tok-f1-y", "tok-f1-n");
    const std::string json = "[" + m + "]";

    auto result = ParseSportsMarketsFlat(json, 10);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].event_id, "cond-f1");  // synthetic event_id = condition_id
    EXPECT_EQ(result[0].markets.size(), 1u);
    EXPECT_EQ(result[0].markets[0].condition_id, "cond-f1");
}

// 非体育 market 被过滤
TEST(ParseSportsMarketsFlat, Filter_NonSportMarket) {
    const std::string m = MakeMarket("cond-f2", "Will it rain tomorrow?", "tok-y", "tok-n");
    const std::string json = "[" + m + "]";

    auto result = ParseSportsMarketsFlat(json, 10);
    EXPECT_TRUE(result.empty());
}

// max_markets 截断
TEST(ParseSportsMarketsFlat, Truncation_MaxMarkets) {
    const std::string m1 = MakeMarket("cond-fa", "NBA game 1 winner?", "ty1", "tn1");
    const std::string m2 = MakeMarket("cond-fb", "NBA game 2 winner?", "ty2", "tn2");
    const std::string m3 = MakeMarket("cond-fc", "NBA game 3 winner?", "ty3", "tn3");
    const std::string json = "[" + m1 + "," + m2 + "," + m3 + "]";

    auto result = ParseSportsMarketsFlat(json, 2);
    EXPECT_EQ(result.size(), 2u);
}

// ============================================================================
// ExtractJsonStr — 基础覆盖 (辅助函数可测性验证)
// ============================================================================

TEST(ExtractJsonStr, Basic_SimpleKeyValue) {
    const std::string json = R"({"id":"ev-123","title":"NBA Finals"})";
    EXPECT_EQ(ExtractJsonStr(json, "id"), "ev-123");
    EXPECT_EQ(ExtractJsonStr(json, "title"), "NBA Finals");
}

TEST(ExtractJsonStr, MissingKey_ReturnsEmpty) {
    const std::string json = R"({"id":"ev-123"})";
    EXPECT_EQ(ExtractJsonStr(json, "nonexistent"), "");
}

TEST(ExtractJsonStr, EscapedQuote_NotTruncated) {
    // value 含转义引号: "NBA \"Finals\""
    // ExtractJsonStr 不做 unescape, 直接截取 raw bytes (含 \).
    // 实现保证不被 \" 提前截断 (esc 状态机跳过), 但不去掉 \.
    // 预期返回: NBA \"Finals\" (raw bytes, 含反斜杠)
    const std::string json = "{\"question\":\"NBA \\\"Finals\\\"\"}";
    const std::string val = ExtractJsonStr(json, "question");
    // 不被截断: 长度 > len("NBA ") = 4
    EXPECT_GT(val.size(), 4u);
    // 含 "NBA" 前缀
    EXPECT_EQ(val.substr(0, 3), "NBA");
    // 含 "Finals" 子串
    EXPECT_NE(val.find("Finals"), std::string::npos);
    // raw bytes 含 backslash (不做 unescape)
    EXPECT_NE(val.find('\\'), std::string::npos);
}

TEST(ExtractJsonStr, SpaceAfterColon_AlsoMatches) {
    const std::string json = R"({"id": "ev-space"})";
    EXPECT_EQ(ExtractJsonStr(json, "id"), "ev-space");
}

// ============================================================================
// ExtractMarketsArray — 基础覆盖
// ============================================================================

TEST(ExtractMarketsArray, Basic_SingleMarket) {
    const std::string m = MakeMarket("cond-ma1", "Q", "ty", "tn");
    const std::string event_obj = "{\"id\":\"ev\",\"markets\":[" + m + "]}";
    const std::string arr = ExtractMarketsArray(event_obj);
    EXPECT_FALSE(arr.empty());
    EXPECT_EQ(arr.front(), '[');
    EXPECT_EQ(arr.back(), ']');
    // 内容应包含 conditionId
    EXPECT_NE(arr.find("cond-ma1"), std::string::npos);
}

TEST(ExtractMarketsArray, Empty_NoMarketsKey) {
    const std::string event_obj = R"({"id":"ev","title":"No Markets"})";
    EXPECT_EQ(ExtractMarketsArray(event_obj), "");
}

TEST(ExtractMarketsArray, EmptyArray) {
    const std::string event_obj = R"({"id":"ev","markets":[]})";
    const std::string arr = ExtractMarketsArray(event_obj);
    // "[]" — 空数组也能提取
    EXPECT_EQ(arr, "[]");
}

// ============================================================================
// ParseSportsEvents — 额外边界
// ============================================================================

// 空 JSON 字符串 → 空结果
TEST(ParseSportsEvents, Empty_EmptyJson) {
    auto result = ParseSportsEvents("", 30);
    EXPECT_TRUE(result.empty());
}

// max_events=0 → 立即截断
TEST(ParseSportsEvents, MaxZero_EmptyResult) {
    const std::string m = MakeMarket("cond-z", "NBA?", "ty-z", "tn-z");
    const std::string ev = MakeEvent("ev-z", "NBA Game", "basketball", m);
    const std::string json = "[" + ev + "]";

    auto result = ParseSportsEvents(json, 0);
    EXPECT_TRUE(result.empty());
}

// event 无 id 字段 → 跳过
TEST(ParseSportsEvents, MissingEventId_Skipped) {
    const std::string m = MakeMarket("cond-noid", "NBA?", "ty-noid", "tn-noid");
    // 无 "id" 字段
    const std::string ev = "{\"slug\":\"no-id-slug\","
                           "\"title\":\"NBA Game\","
                           "\"sport\":\"basketball\","
                           "\"markets\":[" + m + "]}";
    const std::string json = "[" + ev + "]";

    auto result = ParseSportsEvents(json, 30);
    EXPECT_TRUE(result.empty());
}

// sports_market_type 归一化在 Parse 路径中正确传递
TEST(ParseSportsEvents, SportsMarketType_Normalized) {
    const std::string m = MakeMarket("cond-smt", "NBA spread?", "ty-smt", "tn-smt", "Point Spread");
    const std::string ev = MakeEvent("ev-smt", "NBA Game", "basketball", m);
    const std::string json = "[" + ev + "]";

    auto result = ParseSportsEvents(json, 30);
    ASSERT_EQ(result.size(), 1u);
    ASSERT_EQ(result[0].markets.size(), 1u);
    EXPECT_EQ(result[0].markets[0].sports_market_type, "spread");
}

// neg_risk_market_id 字段提取
TEST(ParseSportsEvents, NegRiskMarketId_Extracted) {
    const std::string m = MakeMarket("cond-nr", "Q", "ty-nr", "tn-nr");
    const std::string ev = "{\"id\":\"ev-nr\",\"slug\":\"ev-nr\","
                           "\"title\":\"NBA Game\",\"sport\":\"basketball\","
                           "\"negRiskMarketID\":\"nr-market-456\","
                           "\"markets\":[" + m + "]}";
    const std::string json = "[" + ev + "]";

    auto result = ParseSportsEvents(json, 30);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].neg_risk_market_id, "nr-market-456");
}

// 同一 event 内多个 markets, 其中一个无 conditionId → 只保留有效 market
TEST(ParseSportsEvents, PartialMarkets_OnlyValidKept) {
    const std::string m_valid = MakeMarket("cond-pv1", "Q1", "ty-pv1", "tn-pv1");
    // 无 conditionId
    const std::string m_invalid = "{\"question\":\"No cond\","
                                  "\"clobTokenIds\":[\"ty-bad\",\"tn-bad\"]}";
    const std::string ev = MakeEvent("ev-pv", "NBA Game", "basketball",
                                     m_valid + "," + m_invalid);
    const std::string json = "[" + ev + "]";

    auto result = ParseSportsEvents(json, 30);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].markets.size(), 1u);
    EXPECT_EQ(result[0].markets[0].condition_id, "cond-pv1");
}
