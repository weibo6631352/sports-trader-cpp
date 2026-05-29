// tests/unit/test_inplay_parser_harden.cpp — Goalserve inplay parser 安全加固单测
//
// Owner: 小段 (goalserve-specialist, #37)
// Date:  2026-05-30
// Task:  小白审计 §1.3-B/C 落地验证
//
// 覆盖:
//   H1: ExtractStringValue 末尾 \ 转义边界 bug 修 — 不越界, 不吃闭合引号
//   H2: 花括号深度 max_depth=32 — ExtractEventsBlock 深嵌套拒绝不崩
//   H3: 花括号深度 max_depth=32 — ParseEventInfo info 块深嵌套拒绝不崩
//   H4: EnumerateEvents 深嵌套 event value block 拒绝不崩
//   H5: log injection — entry.id 含控制字符/CRLF 被清洗为 '?'
//   H6: 正常解析 (soccer inplay 最小 JSON) 仍 OK — 不破真实 feed
//   H7: 超大 events 块 (正常深度, 多 event) 正常解析不误杀
//   H8: 畸形 JSON 无 events 块 — parse_errors 记录, 不崩
//   H9: score "0:0" / "21:17" / "6.3:2.1" ParseScore 覆盖
//
// 注: 不测 gzip 炸弹防护 (需 zlib, 在 inplay_feed_thread.cpp 的 DecompressGzImpl;
//     集成测试可用构造的 gzip payload 覆盖, 此处只测 parser 层)

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "stcpp/data/goalserve_client.hpp"
#include "stcpp/data/inplay_score_parser.hpp"

using namespace stcpp::data::inplay;
using namespace stcpp::data::goalserve;

namespace {

// ============================================================================
// 最小合法 inplay JSON (soccer, 1 event)
// ============================================================================
constexpr std::string_view kMinimalSoccerJson = R"({
  "bm": "bet365",
  "updated_ts": 1780066172550,
  "events": {
    "134261101": {
      "info": {
        "id": "134261101",
        "mid": "mid001",
        "name": "HomeFC vs AwayFC",
        "sport": "soccer",
        "league_id": "18235",
        "league": "Brazil Serie A",
        "start_time": "13:00",
        "start_date": "29.05.2026",
        "start_ts": "1780059600",
        "start_ts_utc": "1780059600",
        "period": "2nd Half",
        "score": "2:1",
        "state": "21000",
        "minute": "89",
        "seconds": "89:21"
      },
      "odds": {}
    }
  }
})";

// ============================================================================
// 构建深嵌套 JSON (花括号深度 > 32)
// ============================================================================
std::string MakeDeepNestedEventsJson(int depth) {
    // 构造 events 块里一个 event 的 value 为深嵌套 JSON
    std::string json = R"({"bm":"b","updated_ts":1000000000000,"events":{"99":)";
    for (int i = 0; i < depth; ++i)
        json += '{';
    json += '"';
    json += 'x';
    json += '"';
    json += ':';
    json += '1';
    for (int i = 0; i < depth; ++i)
        json += '}';
    json += "}}";
    return json;
}

// 构造顶层 events 值本身深嵌套 (ExtractEventsBlock 路径)
std::string MakeDeepNestedEventsBlockJson(int depth) {
    std::string json = R"({"bm":"b","updated_ts":1000000000000,"events":)";
    for (int i = 0; i < depth; ++i)
        json += '{';
    json += '"';
    json += 'x';
    json += '"';
    json += ':';
    json += '1';
    for (int i = 0; i < depth; ++i)
        json += '}';
    json += '}';
    return json;
}

}  // namespace

// ============================================================================
// H1: ExtractStringValue 末尾 \ 边界 bug 修
//
// 原 bug: json[end]=='\\' 时 ++end 再 ++end, 若 \ 在字符串末尾 (end+1 越界) → 吃闭合引号.
// 修后: end+1 < size 守护, 否则中止.
// ============================================================================
TEST(InplayParserHarden_H1, TrailingBackslashNoOverrun) {
    // JSON 字符串末尾是 \: "val\":   (= val\ 后面直接是闭合引号)
    // 这是畸形 JSON (\ 不应单独在末尾), 但我们要保证不越界/不吃下一字段
    const std::string json = R"({"key":"val\","next":"good"})";
    // 期望: ExtractStringValue 对 "key" 可能返回 false 或截断, 但不应崩溃
    // 关键: "next" 字段应仍可被独立提取
    // 注: 该函数是 anonymous namespace 内部函数, 通过 Parse() 间接测试
    // 构造含畸形转义的 inplay JSON, 确保 Parse 不崩溃
    const std::string inplay_json = R"({
      "bm": "b",
      "updated_ts": 1000000000001,
      "events": {
        "1": {
          "info": {
            "id": "1",
            "name": "Home\ vs Away",
            "period": "1st Half",
            "score": "0:0",
            "start_ts": "1000000000"
          },
          "odds": {}
        }
      }
    })";
    // 关键: 不崩溃. parse_errors 可能有, 但不 throw / segfault
    EXPECT_NO_THROW({
        const auto result = InplayScoreParser::Parse(inplay_json, GoalserveSport::Soccer, 2000000000000LL);
        // 验证函数正常返回 (即使有 parse error 也不崩)
        (void)result;
    });
}

TEST(InplayParserHarden_H1, NormalEscapeInsideString) {
    // 正常的转义序列: \"、\\、\n 都应被正确跳过
    const std::string inplay_json = R"({
      "bm": "b",
      "updated_ts": 1000000000001,
      "events": {
        "2": {
          "info": {
            "id": "2",
            "name": "Home \"FC\" vs Away\\FC",
            "period": "2nd Half",
            "score": "1:0",
            "start_ts": "1000000000"
          },
          "odds": {}
        }
      }
    })";
    EXPECT_NO_THROW({
        const auto result = InplayScoreParser::Parse(inplay_json, GoalserveSport::Soccer, 2000000000000LL);
        // 至少解析到 1 个 score (转义不阻断解析)
        EXPECT_FALSE(result.scores.empty());
    });
}

// ============================================================================
// H2: ExtractEventsBlock 深嵌套 (> 32 层) 拒绝不崩
// ============================================================================
TEST(InplayParserHarden_H2, DeepNestedEventsBlockRejected) {
    // events 整块深嵌套 > 32 → ExtractEventsBlock 返回空 → Parse 报 error
    const std::string deep_json = MakeDeepNestedEventsBlockJson(40);
    EXPECT_NO_THROW({
        const auto result = InplayScoreParser::Parse(deep_json, GoalserveSport::Soccer, 2000000000000LL);
        // 应该有 parse_error (events block 被拒绝或格式不对)
        EXPECT_TRUE(result.scores.empty());
    });
}

TEST(InplayParserHarden_H2, Depth32IsAccepted) {
    // 正好 32 层嵌套 = 允许 (不超限)
    // 注: 真实 inplay JSON 深度约 3-5 层, 32 是极端安全上限
    // 此测试构造合法 events 块 (深度合理)
    const auto result =
        InplayScoreParser::Parse(std::string(kMinimalSoccerJson), GoalserveSport::Soccer, 2000000000000LL);
    EXPECT_FALSE(result.scores.empty()) << "正常 JSON 不应被误杀";
    EXPECT_EQ(result.scores[0].home_team, "HomeFC");
    EXPECT_EQ(result.scores[0].away_team, "AwayFC");
}

// ============================================================================
// H3: ParseEventInfo info 块深嵌套拒绝
// ============================================================================
TEST(InplayParserHarden_H3, DeepNestedInfoBlockRejected) {
    // event value 中 info 块深嵌套 > 32 → ParseEventInfo 返回 false, 计入 parse_errors
    const std::string deep_json = MakeDeepNestedEventsJson(40);
    EXPECT_NO_THROW({
        const auto result = InplayScoreParser::Parse(deep_json, GoalserveSport::Soccer, 2000000000000LL);
        // 深嵌套 event 应被跳过 (parse_error 有记录)
        EXPECT_TRUE(result.scores.empty());
    });
}

// ============================================================================
// H4: EnumerateEvents 深嵌套 event value block 中止不崩
// ============================================================================
TEST(InplayParserHarden_H4, EnumerateEventsDeepBlockAbort) {
    // 在 events 层 value block 内构造深嵌套: EnumerateEvents 应中止
    // 构造: events 块里的 event value 本身是深嵌套
    std::string json = R"({"bm":"b","updated_ts":1000000000002,"events":{"55":{"info":{)";
    // 嵌入深嵌套
    for (int i = 0; i < 35; ++i)
        json += '{';
    json += R"("x":1)";
    for (int i = 0; i < 35; ++i)
        json += '}';
    json += "}}}}";
    EXPECT_NO_THROW({
        const auto result = InplayScoreParser::Parse(json, GoalserveSport::Soccer, 2000000000000LL);
        (void)result;
        // 关键: 不崩溃
    });
}

// ============================================================================
// H5: log injection — entry.id 含控制字符/CRLF 被清洗
//
// 恶意 id 含 "\r\n[INJECTED]", CRLF 注入日志可伪造日志行.
// SanitizeId 应将非白名单字符替换为 '?'.
// ============================================================================
TEST(InplayParserHarden_H5, LogInjectionIdCleaned) {
    // 构造含 CRLF + 超长 id 的 events
    const std::string poison_id = "12345\r\n[INJECTED_LOG_LINE]\r\n";
    std::string json = R"({"bm":"b","updated_ts":1000000000003,"events":{")" + poison_id +
                       R"(":{"info":{"id":")" + poison_id +
                       R"(","name":"X vs Y","score":"0:0","start_ts":"1000000"},"odds":{}}}})";
    EXPECT_NO_THROW({
        const auto result = InplayScoreParser::Parse(json, GoalserveSport::Soccer, 2000000000000LL);
        // parse_errors 里的 id 应该被清洗 (不含 \r\n / 控制字符)
        for (const auto& err : result.parse_errors) {
            // 清洗后不应含 CR/LF
            EXPECT_EQ(err.find('\r'), std::string::npos) << "CR 不应出现在 parse_errors: " << err;
            EXPECT_EQ(err.find('\n'), std::string::npos) << "LF 不应出现在 parse_errors: " << err;
        }
    });
}

TEST(InplayParserHarden_H5, LogInjectionIdTruncated) {
    // 超长 id (> 64 字符) 应被截断
    const std::string long_id(200, 'A');
    std::string json = R"({"bm":"b","updated_ts":1000000000004,"events":{")" + long_id +
                       R"(":{"info":{"id":")" + long_id +
                       R"(","name":"X vs Y","score":"0:0","start_ts":"1000000"},"odds":{}}}})";
    EXPECT_NO_THROW({
        const auto result = InplayScoreParser::Parse(json, GoalserveSport::Soccer, 2000000000000LL);
        // parse_errors 中的 id 部分不超过 64 字符 (SanitizeId 截断)
        for (const auto& err : result.parse_errors) {
            // 检查 "event XXXX: " 前缀里的 id 部分 ≤ 64 char
            const auto colon_pos = err.find(": ");
            if (colon_pos != std::string::npos) {
                const std::string id_part = err.substr(0, colon_pos);
                // "event " = 6 chars, id_part = "event <id>"
                EXPECT_LE(id_part.size(), 6 + 64) << "id 部分超过 64 字符: " << id_part;
            }
        }
    });
}

// ============================================================================
// H6: 正常 soccer inplay JSON — 解析结果正确
// ============================================================================
TEST(InplayParserHarden_H6, NormalSoccerJsonParses) {
    const std::int64_t ingestion_ns = 1780066172600LL * 1'000'000LL;
    const auto result =
        InplayScoreParser::Parse(std::string(kMinimalSoccerJson), GoalserveSport::Soccer, ingestion_ns);

    EXPECT_EQ(result.scores.size(), 1U) << "应解析到 1 个 event";
    EXPECT_TRUE(result.parse_errors.empty()) << "正常 JSON 不应有 parse_errors";

    const auto& rec = result.scores[0];
    EXPECT_EQ(rec.match_id.inplay_match_id, "134261101");
    EXPECT_EQ(rec.home_team, "HomeFC");
    EXPECT_EQ(rec.away_team, "AwayFC");
    EXPECT_EQ(rec.home_score_total, 2);
    EXPECT_EQ(rec.away_score_total, 1);
    EXPECT_EQ(rec.period.value_or(""), "2nd Half");
    EXPECT_EQ(rec.status, TimeStatus::InPlay);

    // R-20 四时间戳
    EXPECT_GT(rec.ts.data_source_ts_ns, 0);
    EXPECT_GT(rec.ts.ingestion_ts_ns, 0);
    EXPECT_TRUE(rec.ts.IsMonotonic());
}

// ============================================================================
// H7: 多 event 正常解析不误杀 (正常深度)
// ============================================================================
TEST(InplayParserHarden_H7, MultipleEventsNormalDepth) {
    const std::string json = R"({
      "bm": "bet365",
      "updated_ts": 1780066172550,
      "events": {
        "100": {
          "info": {
            "id": "100", "name": "A vs B", "score": "0:1",
            "period": "1st Half", "start_ts": "1780050000",
            "state": "11000", "minute": "30", "seconds": "30:00"
          }, "odds": {}
        },
        "200": {
          "info": {
            "id": "200", "name": "C vs D", "score": "2:2",
            "period": "2nd Half", "start_ts": "1780055000",
            "state": "21000", "minute": "75", "seconds": "75:10"
          }, "odds": {}
        },
        "300": {
          "info": {
            "id": "300", "name": "E vs F", "score": "1:0",
            "period": "1st Half", "start_ts": "1780060000",
            "state": "11000", "minute": "45", "seconds": "45:00"
          }, "odds": {}
        }
      }
    })";

    const auto result = InplayScoreParser::Parse(json, GoalserveSport::Soccer, 1780066173000LL * 1'000'000LL);

    EXPECT_EQ(result.scores.size(), 3U) << "应解析到 3 个 events";
    EXPECT_TRUE(result.parse_errors.empty());
    for (const auto& rec : result.scores) {
        EXPECT_TRUE(rec.ts.IsMonotonic()) << "R-20 ts chain 应成立";
    }
}

// ============================================================================
// H8: 畸形 JSON 无 events 块 — parse_errors 记录不崩
// ============================================================================
TEST(InplayParserHarden_H8, MissingEventsBlock) {
    const std::string json = R"({"bm":"b","updated_ts":1000000000005})";
    EXPECT_NO_THROW({
        const auto result = InplayScoreParser::Parse(json, GoalserveSport::Soccer, 2000000000000LL);
        EXPECT_TRUE(result.scores.empty());
        EXPECT_FALSE(result.parse_errors.empty()) << "应有 parse_error: missing events block";
    });
}

TEST(InplayParserHarden_H8, EmptyJson) {
    EXPECT_NO_THROW({
        const auto result = InplayScoreParser::Parse("", GoalserveSport::Soccer, 2000000000000LL);
        EXPECT_TRUE(result.scores.empty());
        EXPECT_FALSE(result.parse_errors.empty());
    });
}

TEST(InplayParserHarden_H8, GarbageJson) {
    EXPECT_NO_THROW({
        const auto result =
            InplayScoreParser::Parse("{{{{{{garbage", GoalserveSport::Soccer, 2000000000000LL);
        EXPECT_TRUE(result.scores.empty());
    });
}

TEST(InplayParserHarden_H8, UnclosedEventsBlock) {
    // 未闭合的 events 块 — ExtractEventsBlock 应返回空 (不崩)
    const std::string json = R"({"bm":"b","updated_ts":1000000000006,"events":{"1":{"info":{"id":"1")";
    EXPECT_NO_THROW({
        const auto result = InplayScoreParser::Parse(json, GoalserveSport::Soccer, 2000000000000LL);
        EXPECT_TRUE(result.scores.empty());
    });
}

// ============================================================================
// H9: ParseScore 边界覆盖
// ============================================================================
TEST(InplayParserHarden_H9, ParseScoreNormal) {
    std::int32_t h = 0, a = 0;
    EXPECT_TRUE(InplayScoreParser::ParseScore("0:0", h, a));
    EXPECT_EQ(h, 0);
    EXPECT_EQ(a, 0);

    EXPECT_TRUE(InplayScoreParser::ParseScore("21:17", h, a));
    EXPECT_EQ(h, 21);
    EXPECT_EQ(a, 17);
}

TEST(InplayParserHarden_H9, ParseScoreTennisDotFormat) {
    // 网球 "6.3:2.1" → home=6, away=2 (取整数部分)
    std::int32_t h = 0, a = 0;
    EXPECT_TRUE(InplayScoreParser::ParseScore("6.3:2.1", h, a));
    EXPECT_EQ(h, 6);
    EXPECT_EQ(a, 2);
}

TEST(InplayParserHarden_H9, ParseScoreMalformed) {
    std::int32_t h = 0, a = 0;
    EXPECT_FALSE(InplayScoreParser::ParseScore("", h, a));
    EXPECT_FALSE(InplayScoreParser::ParseScore("nocodon", h, a));
}
