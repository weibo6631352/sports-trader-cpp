// tests/unit/test_label_pipeline.cpp — Phase 2 标签管道 (condition→outcome join)
#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "stcpp/ml/label_pipeline.hpp"

namespace ml = stcpp::ml;
using stcpp::data::SettlementMap;
using stcpp::data::SettlementRecord;

namespace {
SettlementRecord MakeSettle(const std::string& cid, bool closed, std::int8_t val) {
    SettlementRecord r;
    r.condition_id = cid;
    r.closed = closed;
    r.settlement_value = val;
    r.valid = true;
    return r;
}
// FeatureRecorder 真实行首字段格式: {"condition_id":"<hex>",...}
std::string FeatLine(const std::string& cid, double fair) {
    return "{\"condition_id\":\"" + cid + "\",\"fair_value\":" + std::to_string(fair) + ",\"b_ofi\":1.5}";
}
}  // namespace

// LP01: DeriveOutcomeLabel — closed YES→y=1 / closed NO→y=0 / 未知/未结算→invalid
TEST(LabelPipeline, LP01_DeriveLabel) {
    EXPECT_TRUE(ml::DeriveOutcomeLabel(MakeSettle("c", true, 1)).valid);
    EXPECT_DOUBLE_EQ(ml::DeriveOutcomeLabel(MakeSettle("c", true, 1)).y, 1.0);   // YES 赢
    EXPECT_DOUBLE_EQ(ml::DeriveOutcomeLabel(MakeSettle("c", true, 0)).y, 0.0);   // NO 赢
    EXPECT_FALSE(ml::DeriveOutcomeLabel(MakeSettle("c", true, -1)).valid);       // 未知赢家
    EXPECT_FALSE(ml::DeriveOutcomeLabel(MakeSettle("c", false, 1)).valid);       // 未 closed
}

// LP02: BuildLabelStore — 只收已结算
TEST(LabelPipeline, LP02_BuildStore) {
    SettlementMap m;
    m["yes"] = MakeSettle("yes", true, 1);
    m["no"] = MakeSettle("no", true, 0);
    m["open"] = MakeSettle("open", false, -1);  // 未结算
    const auto store = ml::BuildLabelStore(m);
    EXPECT_EQ(store.size(), 2u) << "未结算的 open 不入监督集";
    EXPECT_DOUBLE_EQ(store.at("yes").y, 1.0);
    EXPECT_DOUBLE_EQ(store.at("no").y, 0.0);
    EXPECT_EQ(store.count("open"), 0u);
}

// LP03: ExtractConditionId — 从真实 recorder 行抽 cid
TEST(LabelPipeline, LP03_ExtractCid) {
    const auto cid = ml::ExtractConditionId(FeatLine("0xabc123", 0.6));
    ASSERT_TRUE(cid.has_value());
    EXPECT_EQ(*cid, "0xabc123");
    EXPECT_FALSE(ml::ExtractConditionId("not json").has_value());
}

// LP04: AppendLabel — 在 '}' 前插 label, X 原样保留
TEST(LabelPipeline, LP04_AppendLabel) {
    const std::string out = ml::AppendLabel(FeatLine("c", 0.6), ml::OutcomeLabel{1.0, true, 1});
    EXPECT_NE(out.find("\"fair_value\":"), std::string::npos) << "X 原样";
    EXPECT_NE(out.find("\"label\":1"), std::string::npos);
    EXPECT_NE(out.find("\"label_valid\":1"), std::string::npos);
    EXPECT_EQ(out.back(), '}');
}

// LP05: JoinLine — 已结算→带label / 未结算+drop→nullopt / 未结算+keep→valid=0
TEST(LabelPipeline, LP05_JoinLine) {
    SettlementMap m;
    m["won"] = MakeSettle("won", true, 1);
    const auto store = ml::BuildLabelStore(m);
    // 已结算
    const auto j1 = ml::JoinLine(FeatLine("won", 0.7), store, /*drop=*/true);
    ASSERT_TRUE(j1.has_value());
    EXPECT_NE(j1->find("\"label\":1"), std::string::npos);
    // 未结算 + drop → 排除
    EXPECT_FALSE(ml::JoinLine(FeatLine("open", 0.5), store, /*drop=*/true).has_value());
    // 未结算 + keep → label_valid=0
    const auto j3 = ml::JoinLine(FeatLine("open", 0.5), store, /*drop=*/false);
    ASSERT_TRUE(j3.has_value());
    EXPECT_NE(j3->find("\"label_valid\":0"), std::string::npos);
}

// LP06: JoinFile — 端到端文件 join (3 特征行, 2 已结算)
TEST(LabelPipeline, LP06_JoinFile) {
    const std::string in_path = "/tmp/stcpp_lp_feat.jsonl";
    const std::string out_path = "/tmp/stcpp_lp_train.jsonl";
    {
        std::ofstream f(in_path, std::ios::trunc);
        f << FeatLine("won", 0.7) << '\n';
        f << FeatLine("lost", 0.3) << '\n';
        f << FeatLine("open", 0.5) << '\n';  // 未结算
    }
    SettlementMap m;
    m["won"] = MakeSettle("won", true, 1);
    m["lost"] = MakeSettle("lost", true, 0);
    const auto store = ml::BuildLabelStore(m);

    const auto st = ml::JoinFile(in_path, store, out_path, /*drop_unlabeled=*/true);
    EXPECT_EQ(st.total, 3u);
    EXPECT_EQ(st.labeled, 2u);
    EXPECT_EQ(st.unlabeled, 1u) << "open 被 drop";

    // 输出文件应只有 2 行 (已结算), 含正确 label。
    std::ifstream out(out_path);
    std::string line;
    int rows = 0, y1 = 0, y0 = 0;
    while (std::getline(out, line)) {
        ++rows;
        if (line.find("\"label\":1") != std::string::npos) ++y1;
        if (line.find("\"label\":0") != std::string::npos) ++y0;
    }
    EXPECT_EQ(rows, 2);
    EXPECT_EQ(y1, 1);  // won
    EXPECT_EQ(y0, 1);  // lost
    std::remove(in_path.c_str());
    std::remove(out_path.c_str());
}

// LP07 (缺口E): 从 settlement.jsonl (SettlementRecorder 格式) 离线建 LabelStore
TEST(LabelPipeline, LP07_LoadFromSettlementJsonl) {
    const std::string path = "/tmp/stcpp_lp_settle.jsonl";
    {
        std::ofstream f(path, std::ios::trunc);
        f << R"({"condition_id":"won","closed":1,"settlement_value":1,"end_date_ts_ns":100})" << '\n';
        f << R"({"condition_id":"lost","closed":1,"settlement_value":0,"end_date_ts_ns":200})" << '\n';
        f << R"({"condition_id":"unknown","closed":1,"settlement_value":-1,"end_date_ts_ns":300})" << '\n';
    }
    const auto store = ml::LoadLabelStoreFromJsonl(path);
    EXPECT_EQ(store.size(), 2u) << "settlement_value=-1 (未知赢家) 不入";
    EXPECT_DOUBLE_EQ(store.at("won").y, 1.0);
    EXPECT_DOUBLE_EQ(store.at("lost").y, 0.0);
    EXPECT_EQ(store.count("unknown"), 0u);
    // 端到端: settlement.jsonl → LabelStore → join 特征行
    const auto j = ml::JoinLine(FeatLine("won", 0.8), store, /*drop=*/true);
    ASSERT_TRUE(j.has_value());
    EXPECT_NE(j->find("\"label\":1"), std::string::npos);
    std::remove(path.c_str());
}
