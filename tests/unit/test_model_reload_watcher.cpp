// test_model_reload_watcher.cpp — 模型热加载触发器单测 (验证安全门 / id 变更 / manifest 解析)。
#include <gtest/gtest.h>

#include "stcpp/ml/model_reload_watcher.hpp"

using stcpp::ml::ParseReloadManifest;

TEST(ReloadManifest, ValidatedNewModelTriggers) {
    const std::string m =
        R"({"model_path":"/models/seq_arb_v2.onnx","model_id":"hashB","validated":true,"val_metric":0.62})";
    const auto d = ParseReloadManifest(m, "hashA");  // 当前 hashA → 新 hashB
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->model_id, "hashB");
    EXPECT_EQ(d->model_path, "/models/seq_arb_v2.onnx");
    EXPECT_NEAR(d->val_metric, 0.62, 1e-9);
}

TEST(ReloadManifest, NotValidatedNeverReloads) {
    // 安全门: 未验证的模型绝不热换 (防坏模型上实盘)。
    const std::string m =
        R"({"model_path":"/models/x.onnx","model_id":"hashB","validated":false,"val_metric":0.9})";
    EXPECT_FALSE(ParseReloadManifest(m, "hashA").has_value()) << "validated=false → 不换 (安全门)";
    const std::string m2 = R"({"model_path":"/models/x.onnx","model_id":"hashB"})";  // 无 validated 字段
    EXPECT_FALSE(ParseReloadManifest(m2, "hashA").has_value()) << "缺 validated → 不换";
}

TEST(ReloadManifest, SameIdNoReload) {
    const std::string m =
        R"({"model_path":"/models/x.onnx","model_id":"hashA","validated":true})";
    EXPECT_FALSE(ParseReloadManifest(m, "hashA").has_value()) << "id 未变 → 不换";
}

TEST(ReloadManifest, MissingFieldsNoReload) {
    EXPECT_FALSE(ParseReloadManifest(R"({"validated":true})", "x").has_value()) << "无 model_id/path";
    EXPECT_FALSE(ParseReloadManifest(R"({"validated":true,"model_id":"","model_path":"p"})", "x").has_value())
        << "空 id";
}

TEST(ReloadManifest, IntValidatedAlsoWorks) {
    const std::string m =
        R"({"model_path":"/m.onnx","model_id":"hashB","validated":1})";  // 1 = true
    EXPECT_TRUE(ParseReloadManifest(m, "hashA").has_value());
}
