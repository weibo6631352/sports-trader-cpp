// tests/unit/test_dotenv.cpp — 程序内置 .env 加载 (dotenv.hpp)
//
// Owner: 老雷 (GM) 2026-06-12 — 老板「程序直接读 .env」配套。
// 契约: ① 进程环境优先 (绝不覆盖); ② live 模式跳过 PAPER_MODE;
//       ③ export 前缀/引号/空行/注释/脏行容错; ④ 文件不存在 → 空 (非错误)。
#include "stcpp/app/dotenv.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace stcpp::app::test {

class DotEnvTest : public ::testing::Test {
protected:
    void SetUp() override {
        path_ = ::testing::TempDir() + "stcpp_dotenv_test.env";
        // 测试键全部前缀 STCPP_DOTENV_T_, 用前清场
        for (const char* k : {"STCPP_DOTENV_T_A", "STCPP_DOTENV_T_B", "STCPP_DOTENV_T_Q",
                              "STCPP_DOTENV_T_EXP", "PAPER_MODE_DOTENV_T"})
            ::unsetenv(k);
        ::unsetenv("PAPER_MODE");  // PAPER_MODE 跳过契约用真键名
    }
    void TearDown() override {
        std::remove(path_.c_str());
        ::unsetenv("PAPER_MODE");
    }
    void write(const std::string& content) {
        std::ofstream f(path_, std::ios::trunc);
        f << content;
    }
    std::string path_;
};

// 基础解析: KEY=VALUE / export 前缀 / 引号剥离 / 注释与空行跳过。
TEST_F(DotEnvTest, ParsesBasicFormats) {
    write("# comment\n"
          "STCPP_DOTENV_T_A=hello\n"
          "\n"
          "export STCPP_DOTENV_T_EXP=world\n"
          "STCPP_DOTENV_T_Q=\"quo ted\"\n"
          "===garbage line===\n");
    const auto keys = LoadDotEnv(path_, /*live_mode=*/false);
    EXPECT_EQ(keys.size(), 3u);
    EXPECT_STREQ(::getenv("STCPP_DOTENV_T_A"), "hello");
    EXPECT_STREQ(::getenv("STCPP_DOTENV_T_EXP"), "world");
    EXPECT_STREQ(::getenv("STCPP_DOTENV_T_Q"), "quo ted");
}

// 进程环境优先: 已存在的变量绝不被 .env 覆盖。
TEST_F(DotEnvTest, ProcessEnvWins) {
    ::setenv("STCPP_DOTENV_T_A", "from_process", 1);
    write("STCPP_DOTENV_T_A=from_file\n");
    const auto keys = LoadDotEnv(path_, false);
    EXPECT_TRUE(keys.empty());
    EXPECT_STREQ(::getenv("STCPP_DOTENV_T_A"), "from_process");
}

// live 模式跳过 PAPER_MODE (文件默认值不 veto 显式 --mode live, R-11 只拦真环境冲突)。
TEST_F(DotEnvTest, LiveModeSkipsPaperMode) {
    write("PAPER_MODE=1\nSTCPP_DOTENV_T_B=x\n");
    const auto keys = LoadDotEnv(path_, /*live_mode=*/true);
    EXPECT_EQ(::getenv("PAPER_MODE"), nullptr);
    EXPECT_STREQ(::getenv("STCPP_DOTENV_T_B"), "x");
    EXPECT_EQ(keys.size(), 1u);
}

// paper 模式正常加载 PAPER_MODE。
TEST_F(DotEnvTest, PaperModeLoadsPaperMode) {
    write("PAPER_MODE=1\n");
    const auto keys = LoadDotEnv(path_, /*live_mode=*/false);
    EXPECT_EQ(keys.size(), 1u);
    EXPECT_STREQ(::getenv("PAPER_MODE"), "1");
}

// 文件不存在 → 空列表 (非错误; 凭证 fail-fast 在下游把关)。
TEST_F(DotEnvTest, MissingFileIsEmpty) {
    const auto keys = LoadDotEnv(path_ + ".nope", false);
    EXPECT_TRUE(keys.empty());
}

}  // namespace stcpp::app::test
