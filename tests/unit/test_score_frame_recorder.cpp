// tests/unit/test_score_frame_recorder.cpp — 回测等价性 P0 比分帧落盘验证
//
// Owner: 老雷 (GM) — 回测等价性 spec P0
// 验证 ScoreFrameRecorder: ① 落盘比分帧 ② 字段透传(4ts/比分/inplay 赔率) ③ JSON 字符串转义(队名含")
//   ④ 前进帧去重(as_of 未推进不重复落帧)。是小蒋 P2 ReplayImpl 喂帧的数据契约回归。

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "stcpp/data/score_frame_recorder.hpp"
#include "stcpp/data/score_snapshot_store.hpp"

namespace {

using stcpp::data::ScoreFrameRecorder;
using stcpp::data::ScoreMap;
using stcpp::data::ScoreSnapshotStore;
using stcpp::debug_api::EventScore;

std::string TempPath(const char* stem) {
    std::ostringstream os;
    os << (std::filesystem::temp_directory_path() / stem).string()
       << "." << ::testing::UnitTest::GetInstance()->random_seed() << ".jsonl";
    return os.str();
}

EventScore MakeEs(std::int64_t as_of, int hs, int as) {
    EventScore es;
    es.found = true;
    es.event_id = "evt-42";
    es.sport = "soccer";
    es.status = "inplay";
    es.period = "2H";
    es.clock_sec = 3600;
    es.home = R"(Team "A")";  // 含双引号 → 测 JSON 转义
    es.away = "Team B";
    es.home_score = hs;
    es.away_score = as;
    es.ts.event_ts_ns = as_of - 300;
    es.ts.data_source_ts_ns = as_of - 200;
    es.ts.ingestion_ts_ns = as_of - 100;
    es.ts.as_of_ts_ns = as_of;
    es.league_id = "lg-7";
    es.kickoff_ts_sec = 1700000000;
    es.inplay_bet365_home_fair = 0.55;
    es.inplay_bet365_away_fair = 0.30;
    es.inplay_bet365_draw_fair = 0.15;
    return es;
}

// 等到 recorder 落 >=n 帧 (轮询 frames_written, 上限 ~3s; 无固定 sleep 减少 flaky)
bool WaitFrames(const ScoreFrameRecorder& r, std::uint64_t n) {
    for (int i = 0; i < 60; ++i) {
        if (r.frames_written() >= n) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

std::string ReadAll(const std::string& path) {
    std::ifstream in(path);
    std::ostringstream os;
    os << in.rdbuf();
    return os.str();
}

TEST(ScoreFrameRecorder, WritesFrameWithEscapedFieldsAndTimestamps) {
    const std::string path = TempPath("stcpp_score_frame");
    std::remove(path.c_str());

    ScoreSnapshotStore store;
    auto m = std::make_shared<ScoreMap>();
    (*m)["match-1"] = MakeEs(1'000'000'000, 2, 1);
    store.Publish(m);

    ScoreFrameRecorder::Config cfg;
    cfg.output_path = path;
    cfg.poll_interval_sec = 1;
    ScoreFrameRecorder rec(store, cfg);
    rec.Start();
    ASSERT_TRUE(WaitFrames(rec, 1)) << "recorder 未在超时内落帧";
    rec.Stop();

    const std::string content = ReadAll(path);
    // 帧结构
    EXPECT_NE(content.find("\"frame_ts_ns\":1000000000"), std::string::npos);
    EXPECT_NE(content.find("\"key\":\"match-1\""), std::string::npos);
    // 4ts 透传
    EXPECT_NE(content.find("\"event_ts_ns\":999999700"), std::string::npos);
    EXPECT_NE(content.find("\"as_of_ts_ns\":1000000000"), std::string::npos);
    // 比分 + inplay sharp 赔率
    EXPECT_NE(content.find("\"home_score\":2"), std::string::npos);
    EXPECT_NE(content.find("\"ip_home\":0.55"), std::string::npos);
    // JSON 字符串转义: 队名 Team "A" → Team \"A\"
    EXPECT_NE(content.find(R"(Team \"A\")"), std::string::npos);
    std::remove(path.c_str());
}

TEST(ScoreFrameRecorder, DedupesWhenAsOfNotAdvanced) {
    const std::string path = TempPath("stcpp_score_dedup");
    std::remove(path.c_str());

    ScoreSnapshotStore store;
    auto m = std::make_shared<ScoreMap>();
    (*m)["match-1"] = MakeEs(2'000'000'000, 0, 0);
    store.Publish(m);

    ScoreFrameRecorder::Config cfg;
    cfg.output_path = path;
    cfg.poll_interval_sec = 1;
    ScoreFrameRecorder rec(store, cfg);
    rec.Start();
    ASSERT_TRUE(WaitFrames(rec, 1));
    // 同一 as_of 再 republish (无前进) → 不应增加帧
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const std::uint64_t after_idle = rec.frames_written();
    // 推进 as_of → 应落新帧
    auto m2 = std::make_shared<ScoreMap>();
    (*m2)["match-1"] = MakeEs(2'000'000'500, 1, 0);
    store.Publish(m2);
    ASSERT_TRUE(WaitFrames(rec, after_idle + 1)) << "as_of 推进后未落新帧";
    rec.Stop();

    EXPECT_EQ(after_idle, 1u) << "as_of 未推进却重复落帧 (去重失效)";
    std::remove(path.c_str());
}

}  // namespace
