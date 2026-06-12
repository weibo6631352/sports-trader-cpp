// tests/unit/test_background_writer.cpp — 异步缓冲落盘器 (2026-06-13)
//   验: 入队→析构 drain 落盘 (顺序保持) / 多文件路由 / 轻负载不丢。
#include "stcpp/infra/background_writer.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

namespace {
std::string ReadFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
}  // namespace

TEST(BackgroundWriter, AppendLinesDrainOnDestruct) {
    const std::string path = ::testing::TempDir() + "bw_drain.jsonl";
    std::remove(path.c_str());
    {
        stcpp::infra::BackgroundWriter w;
        w.AppendLine(path, "a\n");
        w.AppendLine(path, "b\n");
        w.AppendLine(path, "c\n");
    }  // 析构: drain + flush + close + join → 全部落盘且顺序保持
    EXPECT_EQ(ReadFile(path), "a\nb\nc\n");
    std::remove(path.c_str());
}

TEST(BackgroundWriter, MultiFileRouting) {
    const std::string p1 = ::testing::TempDir() + "bw_f1.jsonl";
    const std::string p2 = ::testing::TempDir() + "bw_f2.jsonl";
    std::remove(p1.c_str());
    std::remove(p2.c_str());
    {
        stcpp::infra::BackgroundWriter w;
        w.AppendLine(p1, "x1\n");
        w.AppendLine(p2, "y1\n");
        w.AppendLine(p1, "x2\n");
    }
    EXPECT_EQ(ReadFile(p1), "x1\nx2\n");  // per-file 缓冲不串
    EXPECT_EQ(ReadFile(p2), "y1\n");
    std::remove(p1.c_str());
    std::remove(p2.c_str());
}

TEST(BackgroundWriter, LightLoadNoDrop) {
    const std::string path = ::testing::TempDir() + "bw_drop.jsonl";
    std::remove(path.c_str());
    stcpp::infra::BackgroundWriter w;
    for (int i = 0; i < 200; ++i) w.AppendLine(path, "z\n");
    EXPECT_EQ(w.dropped(), 0u) << "轻负载 (200 << 队列 8192) 不应丢";
    std::remove(path.c_str());
}

TEST(BackgroundWriter, BadPathDoesNotCrash) {
    // 目录不存在 → fopen 失败 → 丢缓冲, 不崩 (与原 JournalFill 'jf==nullptr return' 同语义)。
    stcpp::infra::BackgroundWriter w;
    w.AppendLine("/nonexistent_dir_xyz/bw.jsonl", "ignored\n");
    SUCCEED();  // 析构正常 join, 无崩溃
}
