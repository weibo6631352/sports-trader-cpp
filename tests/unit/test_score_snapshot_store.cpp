// tests/unit/test_score_snapshot_store.cpp — 小段 ScoreSnapshotStore + InplayScoreParser 单测
//
// Owner: 小段 (goalserve-specialist, #37)
// Date:  2026-05-29
//
// 测试覆盖:
//   T1  parser: soccer inplay 样本 → GameScoreRecord (比分/期/状态/4ts)
//   T2  parser: basketball inplay 样本 → GameScoreRecord
//   T3  parser: tennis inplay 样本 → GameScoreRecord
//   T4  parser: 4ts 单调不等式 (R-20) — event_ts ≤ data_source_ts ≤ ingestion_ts
//   T5  parser: ts_chain_ok() 自检 — IngestionFallback 触发 (missing updated_ts)
//   T6  parser: per-event 隔离 (两 event, 互不影响)
//   T7  parser: score 格式 ParseScore "0:1" / "6.3:2.1" (网球)
//   T8  parser: ParseTimeStatus 所有 11 值
//   T9  parser: MapStatus — inplay/halftime/pregame/final 投影
//   T10 store:  Publish → Get → found=true (基本读写)
//   T11 store:  Get miss → nullopt (未 Publish 的 event_id)
//   T12 store:  Publish 覆盖 → Get 返回新值 (snapshot swap)
//   T13 store:  并发读安全 — 多线程 Get 同一 store (数据一致)
//   T14 store:  EventScore 4ts 透传 (data_source_ts_ns 来自 Goalserve payload)
//   T15 store:  空 store Size() = 0; Publish 后 Size() 正确
//   T16 store:  GetSnapshot 返回正确 map 引用
//
// 样本数据: 录制/合成, 不依赖真实 Goalserve HTTP (本环境可能无网络)
// 真实 HTTP 接线: 见 tests/integration/test_goalserve_inplay_live.cpp (W5 TODO)

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "stcpp/data/inplay_score_parser.hpp"
#include "stcpp/data/score_snapshot_store.hpp"

// 使用完整命名空间别名
namespace gsi = stcpp::data::goalserve;
namespace inps = stcpp::data::inplay;
namespace dbgapi = stcpp::debug_api;

using stcpp::data::ScoreMap;
using stcpp::data::ScoreSnapshotStore;

// ============================================================================
// 合成 inplay JSON 样本 (录制自 2026-05-28 实测, SSOT §3.2)
// 注: raw string 使用 R"JSON(...)JSON" 分隔符, 避免 )" 歧义
// ============================================================================

// Soccer: Houston Dynamo vs Colorado Rapids, 86:31, score 1:0
static const std::string kSoccerInplaySample = R"JSON({
  "bm": "bet365",
  "updated_ts": 1716988800123,
  "events": {
    "134181543": {
      "info": {
        "id": "134181543",
        "mid": "9544365",
        "bet365id": "195235332",
        "league_id": "6374",
        "period": "2nd Half",
        "score": "1:0",
        "state": "11007",
        "minute": "86:31",
        "seconds": "31",
        "time_status": "1"
      },
      "odds": {
        "1": {
          "name": "1X2 Full Time",
          "participants": {
            "100": { "name": "Home", "value_eu": "1.15", "suspend": "0" }
          }
        }
      }
    }
  }
})JSON";

// Basketball: sample mid-game Q3
static const std::string kBasketballInplaySample = R"JSON({
  "bm": "bet365",
  "updated_ts": 1716988900000,
  "events": {
    "134200001": {
      "info": {
        "id": "134200001",
        "league_id": "1234",
        "period": "3rd Quarter",
        "score": "67:72",
        "minute": "5",
        "seconds": "30",
        "time_status": "1"
      },
      "odds": {}
    }
  }
})JSON";

// Tennis: French Open, Set 2, score "1:0" (sets)
static const std::string kTennisInplaySample = R"JSON({
  "bm": "bet365",
  "updated_ts": 1716989000000,
  "events": {
    "134300001": {
      "info": {
        "id": "134300001",
        "league_id": "7890",
        "period": "Set 2",
        "score": "1:0",
        "minute": "",
        "seconds": "",
        "time_status": "1"
      },
      "odds": {}
    }
  }
})JSON";

// Missing updated_ts — triggers IngestionFallback
static const std::string kMissingTsSample = R"JSON({
  "bm": "bet365",
  "events": {
    "134181999": {
      "info": {
        "id": "134181999",
        "score": "0:0",
        "time_status": "0"
      },
      "odds": {}
    }
  }
})JSON";

// Two events
static const std::string kTwoEventsSample = R"JSON({
  "bm": "bet365",
  "updated_ts": 1716988800500,
  "events": {
    "134181000": {
      "info": {
        "id": "134181000",
        "league_id": "100",
        "score": "2:0",
        "time_status": "1",
        "period": "2nd Half",
        "minute": "70"
      },
      "odds": {}
    },
    "134181001": {
      "info": {
        "id": "134181001",
        "league_id": "101",
        "score": "1:3",
        "time_status": "1",
        "period": "1st Half",
        "minute": "30"
      },
      "odds": {}
    }
  }
})JSON";

// Helper constants
constexpr std::int64_t kSoccerUpdatedTsMs = 1716988800123LL;
constexpr std::int64_t kSoccerIngestionNs = kSoccerUpdatedTsMs * 1'000'000LL + 50'000'000LL;

// ============================================================================
// §T1-T3: Parser 基础解析
// ============================================================================

TEST(InplayScoreParserTest, T1_SoccerInplay_ParsesCorrectly) {
    auto result =
        inps::InplayScoreParser::Parse(kSoccerInplaySample, gsi::GoalserveSport::Soccer, kSoccerIngestionNs);

    EXPECT_TRUE(result.parse_errors.empty())
        << "Unexpected parse errors: " << (result.parse_errors.empty() ? "" : result.parse_errors[0]);
    ASSERT_EQ(result.scores.size(), 1u);

    const auto& rec = result.scores[0];
    EXPECT_EQ(rec.match_id.inplay_match_id, "134181543");
    EXPECT_EQ(rec.match_id.league_id, "6374");
    EXPECT_EQ(rec.home_score_total, 1);
    EXPECT_EQ(rec.away_score_total, 0);
    ASSERT_TRUE(rec.period.has_value());
    EXPECT_EQ(*rec.period, "2nd Half");
    EXPECT_EQ(rec.status, gsi::TimeStatus::InPlay);

    // minute "86:31" → elapsed_min=86, elapsed_sec=31
    ASSERT_TRUE(rec.elapsed_min.has_value());
    EXPECT_EQ(*rec.elapsed_min, 86);
    ASSERT_TRUE(rec.elapsed_sec.has_value());
    EXPECT_EQ(*rec.elapsed_sec, 31);

    EXPECT_EQ(result.updated_ts_ms, kSoccerUpdatedTsMs);
    EXPECT_EQ(result.sport, gsi::GoalserveSport::Soccer);
}

TEST(InplayScoreParserTest, T2_BasketballInplay_ParsesCorrectly) {
    constexpr std::int64_t ingestion_ns = 1716988900000LL * 1'000'000LL + 50'000'000LL;
    auto result = inps::InplayScoreParser::Parse(kBasketballInplaySample, gsi::GoalserveSport::Basketball,
                                                 ingestion_ns);

    ASSERT_EQ(result.scores.size(), 1u);
    const auto& rec = result.scores[0];
    EXPECT_EQ(rec.match_id.inplay_match_id, "134200001");
    EXPECT_EQ(rec.home_score_total, 67);
    EXPECT_EQ(rec.away_score_total, 72);
    ASSERT_TRUE(rec.period.has_value());
    EXPECT_EQ(*rec.period, "3rd Quarter");
    EXPECT_EQ(rec.status, gsi::TimeStatus::InPlay);
}

TEST(InplayScoreParserTest, T3_TennisInplay_ParsesCorrectly) {
    constexpr std::int64_t ingestion_ns = 1716989000000LL * 1'000'000LL + 50'000'000LL;
    auto result =
        inps::InplayScoreParser::Parse(kTennisInplaySample, gsi::GoalserveSport::Tennis, ingestion_ns);

    ASSERT_EQ(result.scores.size(), 1u);
    const auto& rec = result.scores[0];
    EXPECT_EQ(rec.match_id.inplay_match_id, "134300001");
    EXPECT_EQ(rec.home_score_total, 1);
    EXPECT_EQ(rec.away_score_total, 0);
    ASSERT_TRUE(rec.period.has_value());
    EXPECT_EQ(*rec.period, "Set 2");
}

// ============================================================================
// §T4: 4ts 单调不等式 (R-20)
// ============================================================================

TEST(InplayScoreParserTest, T4_FourTs_Monotonic_R20) {
    auto result =
        inps::InplayScoreParser::Parse(kSoccerInplaySample, gsi::GoalserveSport::Soccer, kSoccerIngestionNs);

    ASSERT_EQ(result.scores.size(), 1u);
    const auto& ts = result.scores[0].ts;

    // data_source_ts = updated_ts_ms × 1e6
    const std::int64_t expected_ds = kSoccerUpdatedTsMs * 1'000'000LL;
    EXPECT_EQ(ts.data_source_ts_ns, expected_ds);

    // event_ts = data_source_ts (inplay 无独立事件时钟)
    EXPECT_EQ(ts.event_ts_ns, ts.data_source_ts_ns);

    // ingestion_ts = 传入值
    EXPECT_EQ(ts.ingestion_ts_ns, kSoccerIngestionNs);

    // 4ts 单调
    EXPECT_TRUE(ts.IsMonotonic()) << "event=" << ts.event_ts_ns << " ds=" << ts.data_source_ts_ns
                                  << " ing=" << ts.ingestion_ts_ns << " as_of=" << ts.as_of_ts_ns;

    // ds_origin = PayloadScoresTs (R-20 合规)
    EXPECT_EQ(ts.ds_origin, gsi::DataSourceTsOrigin::PayloadScoresTs);

    // TsChainOk 通过
    EXPECT_TRUE(inps::InplayScoreParser::TsChainOk(ts));
}

// ============================================================================
// §T5: IngestionFallback (missing updated_ts)
// ============================================================================

TEST(InplayScoreParserTest, T5_MissingUpdatedTs_IngestionFallback) {
    constexpr std::int64_t ingestion_ns = 1716990000000LL * 1'000'000LL;
    auto result = inps::InplayScoreParser::Parse(kMissingTsSample, gsi::GoalserveSport::Soccer, ingestion_ns);

    // parse_errors 应有 R-20 IngestionFallback 警告
    bool found_r20_warn = false;
    for (const auto& e : result.parse_errors) {
        if (e.find("R-20") != std::string::npos || e.find("missing updated_ts") != std::string::npos) {
            found_r20_warn = true;
            break;
        }
    }
    EXPECT_TRUE(found_r20_warn) << "Expected R-20 IngestionFallback warning";

    // 仍能解析到 event
    EXPECT_EQ(result.scores.size(), 1u);
}

// ============================================================================
// §T6: per-event 隔离 (两 event 互不影响)
// ============================================================================

TEST(InplayScoreParserTest, T6_TwoEvents_IsolatedCorrectly) {
    constexpr std::int64_t ingestion_ns = 1716988800500LL * 1'000'000LL + 50'000'000LL;
    auto result = inps::InplayScoreParser::Parse(kTwoEventsSample, gsi::GoalserveSport::Soccer, ingestion_ns);

    ASSERT_EQ(result.scores.size(), 2u);

    const stcpp::data::adapter::GameScoreRecord* ev0 = nullptr;
    const stcpp::data::adapter::GameScoreRecord* ev1 = nullptr;
    for (const auto& rec : result.scores) {
        if (rec.match_id.inplay_match_id == "134181000")
            ev0 = &rec;
        if (rec.match_id.inplay_match_id == "134181001")
            ev1 = &rec;
    }

    ASSERT_NE(ev0, nullptr) << "event 134181000 not found";
    ASSERT_NE(ev1, nullptr) << "event 134181001 not found";

    EXPECT_EQ(ev0->home_score_total, 2);
    EXPECT_EQ(ev0->away_score_total, 0);
    EXPECT_EQ(ev0->match_id.league_id, "100");

    EXPECT_EQ(ev1->home_score_total, 1);
    EXPECT_EQ(ev1->away_score_total, 3);
    EXPECT_EQ(ev1->match_id.league_id, "101");

    // 两个 event 的 data_source_ts 相同 (同一 updated_ts)
    EXPECT_EQ(ev0->ts.data_source_ts_ns, ev1->ts.data_source_ts_ns);
}

// ============================================================================
// §T7: ParseScore
// ============================================================================

TEST(InplayScoreParserTest, T7_ParseScore_Formats) {
    std::int32_t home = 0, away = 0;

    EXPECT_TRUE(inps::InplayScoreParser::ParseScore("0:1", home, away));
    EXPECT_EQ(home, 0);
    EXPECT_EQ(away, 1);

    EXPECT_TRUE(inps::InplayScoreParser::ParseScore("21:17", home, away));
    EXPECT_EQ(home, 21);
    EXPECT_EQ(away, 17);

    // 网球格式 "6.3:2.1" → 取整数部分
    EXPECT_TRUE(inps::InplayScoreParser::ParseScore("6.3:2.1", home, away));
    EXPECT_EQ(home, 6);
    EXPECT_EQ(away, 2);

    EXPECT_TRUE(inps::InplayScoreParser::ParseScore("0:0", home, away));
    EXPECT_EQ(home, 0);
    EXPECT_EQ(away, 0);

    EXPECT_FALSE(inps::InplayScoreParser::ParseScore("invalid", home, away));
    EXPECT_FALSE(inps::InplayScoreParser::ParseScore("", home, away));
}

// ============================================================================
// §T8: ParseTimeStatus 11 值
// ============================================================================

TEST(InplayScoreParserTest, T8_ParseTimeStatus_AllValues) {
    EXPECT_EQ(inps::InplayScoreParser::ParseTimeStatus("0"), gsi::TimeStatus::NotStarted);
    EXPECT_EQ(inps::InplayScoreParser::ParseTimeStatus("1"), gsi::TimeStatus::InPlay);
    EXPECT_EQ(inps::InplayScoreParser::ParseTimeStatus("2"), gsi::TimeStatus::ToBeFixed);
    EXPECT_EQ(inps::InplayScoreParser::ParseTimeStatus("3"), gsi::TimeStatus::Ended);
    EXPECT_EQ(inps::InplayScoreParser::ParseTimeStatus("4"), gsi::TimeStatus::Postponed);
    EXPECT_EQ(inps::InplayScoreParser::ParseTimeStatus("5"), gsi::TimeStatus::Cancelled);
    EXPECT_EQ(inps::InplayScoreParser::ParseTimeStatus("6"), gsi::TimeStatus::Walkover);
    EXPECT_EQ(inps::InplayScoreParser::ParseTimeStatus("7"), gsi::TimeStatus::Interrupted);
    EXPECT_EQ(inps::InplayScoreParser::ParseTimeStatus("8"), gsi::TimeStatus::Abandoned);
    EXPECT_EQ(inps::InplayScoreParser::ParseTimeStatus("9"), gsi::TimeStatus::Retired);
    EXPECT_EQ(inps::InplayScoreParser::ParseTimeStatus("99"), gsi::TimeStatus::Removed);
    EXPECT_EQ(inps::InplayScoreParser::ParseTimeStatus("42"), gsi::TimeStatus::ToBeFixed);
    EXPECT_EQ(inps::InplayScoreParser::ParseTimeStatus(""), gsi::TimeStatus::ToBeFixed);
}

// ============================================================================
// §T9: MapStatus 投影
// ============================================================================

TEST(InplayScoreParserTest, T9_MapStatus_Projection) {
    EXPECT_EQ(inps::InplayScoreParser::MapStatus(gsi::TimeStatus::InPlay, "2nd Half"), "inplay");
    EXPECT_EQ(inps::InplayScoreParser::MapStatus(gsi::TimeStatus::InPlay, "3rd Quarter"), "inplay");
    EXPECT_EQ(inps::InplayScoreParser::MapStatus(gsi::TimeStatus::InPlay, "Set 2"), "inplay");
    EXPECT_EQ(inps::InplayScoreParser::MapStatus(gsi::TimeStatus::InPlay, "Half Time"), "halftime");
    EXPECT_EQ(inps::InplayScoreParser::MapStatus(gsi::TimeStatus::InPlay, "HT"), "halftime");
    EXPECT_EQ(inps::InplayScoreParser::MapStatus(gsi::TimeStatus::Ended, "Full Time"), "final");
    EXPECT_EQ(inps::InplayScoreParser::MapStatus(gsi::TimeStatus::NotStarted, ""), "pregame");
    EXPECT_EQ(inps::InplayScoreParser::MapStatus(gsi::TimeStatus::Postponed, ""), "pregame");
    EXPECT_EQ(inps::InplayScoreParser::MapStatus(gsi::TimeStatus::Abandoned, ""), "pregame");
}

// ============================================================================
// §T10: ScoreSnapshotStore Publish → Get → found
// ============================================================================

TEST(ScoreSnapshotStoreTest, T10_PublishThenGet_Found) {
    ScoreSnapshotStore store;

    dbgapi::EventScore ev;
    ev.found = true;
    ev.event_id = "nba-lal-bos-2026-05-29";
    ev.sport = "basketball";
    ev.status = "inplay";
    ev.period = "Q3";
    ev.home = "Los Angeles Lakers";
    ev.away = "Boston Celtics";
    ev.home_score = 67;
    ev.away_score = 72;
    ev.ts.data_source_ts_ns = 1716988900000LL * 1'000'000LL;
    ev.ts.ingestion_ts_ns = ev.ts.data_source_ts_ns + 50'000'000LL;
    ev.ts.event_ts_ns = ev.ts.data_source_ts_ns;
    ev.ts.as_of_ts_ns = ev.ts.ingestion_ts_ns;
    ev.source = "goalserve";

    auto map = std::make_shared<ScoreMap>();
    (*map)[ev.event_id] = ev;
    store.Publish(std::move(map));

    auto result = store.Get("nba-lal-bos-2026-05-29");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->found);
    EXPECT_EQ(result->event_id, "nba-lal-bos-2026-05-29");
    EXPECT_EQ(result->home_score, 67);
    EXPECT_EQ(result->away_score, 72);
    EXPECT_EQ(result->source, "goalserve");
}

// ============================================================================
// §T11: Get miss → nullopt
// ============================================================================

TEST(ScoreSnapshotStoreTest, T11_Get_Miss_Nullopt) {
    ScoreSnapshotStore store;

    EXPECT_FALSE(store.Get("nonexistent").has_value());

    auto map = std::make_shared<ScoreMap>();
    dbgapi::EventScore ev;
    ev.event_id = "existing-event";
    ev.found = true;
    (*map)["existing-event"] = ev;
    store.Publish(std::move(map));

    EXPECT_FALSE(store.Get("nonexistent").has_value());
    EXPECT_TRUE(store.Get("existing-event").has_value());
}

// ============================================================================
// §T12: Publish 覆盖 → Get 返回新值 (snapshot swap)
// ============================================================================

TEST(ScoreSnapshotStoreTest, T12_Publish_Overwrites_OldSnapshot) {
    ScoreSnapshotStore store;

    {
        auto map = std::make_shared<ScoreMap>();
        dbgapi::EventScore ev;
        ev.event_id = "soccer-event-1";
        ev.found = true;
        ev.home_score = 1;
        ev.away_score = 0;
        ev.ts.data_source_ts_ns = 1000LL;
        ev.ts.ingestion_ts_ns = 1100LL;
        ev.ts.event_ts_ns = 1000LL;
        ev.ts.as_of_ts_ns = 1100LL;
        (*map)["soccer-event-1"] = ev;
        store.Publish(std::move(map));
    }

    auto r1 = store.Get("soccer-event-1");
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(r1->home_score, 1);

    {
        auto map = std::make_shared<ScoreMap>();
        dbgapi::EventScore ev;
        ev.event_id = "soccer-event-1";
        ev.found = true;
        ev.home_score = 2;
        ev.away_score = 0;
        ev.ts.data_source_ts_ns = 2000LL;
        ev.ts.ingestion_ts_ns = 2100LL;
        ev.ts.event_ts_ns = 2000LL;
        ev.ts.as_of_ts_ns = 2100LL;
        (*map)["soccer-event-1"] = ev;
        store.Publish(std::move(map));
    }

    auto r2 = store.Get("soccer-event-1");
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r2->home_score, 2) << "Expected new value after Publish overwrite";
    EXPECT_EQ(r1->home_score, 1);  // 旧值 optional copy 仍有效
}

// ============================================================================
// §T13: 并发读安全 (多线程 Get)
// ============================================================================

TEST(ScoreSnapshotStoreTest, T13_ConcurrentRead_Safe) {
    ScoreSnapshotStore store;

    auto map = std::make_shared<ScoreMap>();
    for (int i = 0; i < 100; ++i) {
        dbgapi::EventScore ev;
        ev.event_id = "event-" + std::to_string(i);
        ev.found = true;
        ev.home_score = i;
        ev.away_score = i * 2;
        ev.ts.data_source_ts_ns = 1000LL * static_cast<std::int64_t>(i + 1);
        ev.ts.ingestion_ts_ns = ev.ts.data_source_ts_ns + 100LL;
        ev.ts.event_ts_ns = ev.ts.data_source_ts_ns;
        ev.ts.as_of_ts_ns = ev.ts.ingestion_ts_ns;
        (*map)[ev.event_id] = ev;
    }
    store.Publish(std::move(map));

    constexpr int kNumThreads = 8;
    std::atomic<int> error_count{0};

    std::vector<std::thread> threads;
    threads.reserve(kNumThreads);

    for (int t = 0; t < kNumThreads; ++t) {
        threads.emplace_back([&store, &error_count]() {
            for (int i = 0; i < 100; ++i) {
                const std::string key = "event-" + std::to_string(i);
                auto result = store.Get(key);
                if (!result.has_value()) {
                    error_count.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                if (result->home_score != i || result->away_score != i * 2) {
                    error_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& th : threads)
        th.join();

    EXPECT_EQ(error_count.load(), 0) << "Concurrent read detected data inconsistency";
}

// ============================================================================
// §T14: EventScore 4ts 透传 (data_source_ts_ns 来自 Goalserve payload)
// ============================================================================

TEST(ScoreSnapshotStoreTest, T14_EventScore_4ts_Transparently_Passed) {
    constexpr std::int64_t ingestion_ns = kSoccerUpdatedTsMs * 1'000'000LL + 50'000'000LL;
    auto parse_result =
        inps::InplayScoreParser::Parse(kSoccerInplaySample, gsi::GoalserveSport::Soccer, ingestion_ns);

    ASSERT_FALSE(parse_result.scores.empty());
    const auto& gs_rec = parse_result.scores[0];

    // 构建 EventScore (模拟物化段投影 GameScoreRecord → EventScore)
    dbgapi::EventScore ev;
    ev.found = true;
    ev.event_id = "soccer-" + gs_rec.match_id.inplay_match_id;
    ev.sport = "soccer";
    ev.status = inps::InplayScoreParser::MapStatus(gs_rec.status, gs_rec.period.value_or(""));
    ev.period = gs_rec.period.value_or("");
    ev.home_score = gs_rec.home_score_total;
    ev.away_score = gs_rec.away_score_total;
    ev.ts.data_source_ts_ns = gs_rec.ts.data_source_ts_ns;  // 透传 Goalserve ts
    ev.ts.ingestion_ts_ns = gs_rec.ts.ingestion_ts_ns;
    ev.ts.event_ts_ns = gs_rec.ts.event_ts_ns;
    ev.ts.as_of_ts_ns = ingestion_ns;
    ev.source = "goalserve";

    ScoreSnapshotStore store;
    auto map = std::make_shared<ScoreMap>();
    (*map)[ev.event_id] = ev;
    store.Publish(std::move(map));

    auto result = store.Get(ev.event_id);
    ASSERT_TRUE(result.has_value());

    // 验证 data_source_ts_ns 来自 Goalserve payload (updated_ts_ms)
    const std::int64_t expected_ds = kSoccerUpdatedTsMs * 1'000'000LL;
    EXPECT_EQ(result->ts.data_source_ts_ns, expected_ds)
        << "data_source_ts_ns must come from Goalserve updated_ts (R-20)";

    // 验证 4ts 单调
    EXPECT_GE(result->ts.data_source_ts_ns, result->ts.event_ts_ns);
    EXPECT_GE(result->ts.ingestion_ts_ns, result->ts.data_source_ts_ns);
    EXPECT_GE(result->ts.as_of_ts_ns, result->ts.ingestion_ts_ns);
}

// ============================================================================
// §T15: Size() 正确
// ============================================================================

TEST(ScoreSnapshotStoreTest, T15_Size_Correct) {
    ScoreSnapshotStore store;

    EXPECT_EQ(store.Size(), 0u) << "Empty store should have size 0";

    auto map = std::make_shared<ScoreMap>();
    for (int i = 0; i < 5; ++i) {
        dbgapi::EventScore ev;
        ev.event_id = "ev-" + std::to_string(i);
        ev.found = true;
        (*map)[ev.event_id] = ev;
    }
    store.Publish(std::move(map));

    EXPECT_EQ(store.Size(), 5u);

    auto map2 = std::make_shared<ScoreMap>();
    map2->emplace("ev-0", dbgapi::EventScore{});
    store.Publish(std::move(map2));
    EXPECT_EQ(store.Size(), 1u);
}

// ============================================================================
// §T16: GetSnapshot 返回正确 map 引用
// ============================================================================

TEST(ScoreSnapshotStoreTest, T16_GetSnapshot_ReturnsCurrentMap) {
    ScoreSnapshotStore store;

    EXPECT_EQ(store.GetSnapshot(), nullptr) << "Empty store snapshot should be nullptr";

    auto map = std::make_shared<ScoreMap>();
    dbgapi::EventScore ev;
    ev.event_id = "test-ev";
    ev.found = true;
    ev.home_score = 99;
    (*map)["test-ev"] = ev;

    store.Publish(map);  // pass by value to keep local ref

    auto snap = store.GetSnapshot();
    ASSERT_NE(snap, nullptr);
    ASSERT_EQ(snap->count("test-ev"), 1u);
    EXPECT_EQ(snap->at("test-ev").home_score, 99);
}
