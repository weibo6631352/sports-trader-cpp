// tests/unit/test_feature_vector_hub.cpp — Phase 2 项6 完整 75 列向量 hub + record
#include <gtest/gtest.h>

#include <vector>

#include "stcpp/ml/feature_vector_hub.hpp"

using stcpp::ml::FeatureVectorHub;
using stcpp::ml::FeatureVectorRecord;
using stcpp::ml::kMlFeatureCount;

namespace {
FeatureVectorRecord MakeRec(const std::string& cid, std::int64_t as_of, float base) {
    FeatureVectorRecord r;
    r.set_condition(cid);
    r.as_of_ts_ns = as_of;
    r.set_spec("ml-feature-spec-v0.4");
    std::vector<float> v(kMlFeatureCount);
    for (std::size_t i = 0; i < kMlFeatureCount; ++i) v[i] = base + static_cast<float>(i);
    r.set_values(v);
    r.valid = true;
    return r;
}
}  // namespace

// FVH01: record set_* — condition/spec/values 正确, count = 75
TEST(FeatureVectorHub, FVH01_RecordSetters) {
    const auto r = MakeRec("0xabc", 1000, 0.0f);
    EXPECT_STREQ(r.condition_id, "0xabc");
    EXPECT_STREQ(r.spec_version, "ml-feature-spec-v0.4");
    EXPECT_EQ(r.count, kMlFeatureCount);
    EXPECT_FLOAT_EQ(r.values[0], 0.0f);
    EXPECT_FLOAT_EQ(r.values[kMlFeatureCount - 1], static_cast<float>(kMlFeatureCount - 1));
}

// FVH02: set_values 列数超/欠 → clamp 到 kMlFeatureCount
TEST(FeatureVectorHub, FVH02_ValuesClamp) {
    FeatureVectorRecord r;
    r.set_values(std::vector<float>(kMlFeatureCount + 10, 1.0f));  // 超
    EXPECT_EQ(r.count, kMlFeatureCount);
    FeatureVectorRecord r2;
    r2.set_values(std::vector<float>(5, 2.0f));  // 欠
    EXPECT_EQ(r2.count, 5u);
    EXPECT_FLOAT_EQ(r2.values[5], 0.0f) << "欠补 0";
}

// FVH03: hub Publish/Read/SnapshotAll + 覆盖同 cid
TEST(FeatureVectorHub, FVH03_HubPubReadSnapshot) {
    FeatureVectorHub hub;
    hub.Publish(MakeRec("c1", 100, 0.0f));
    hub.Publish(MakeRec("c2", 200, 10.0f));
    EXPECT_EQ(hub.Size(), 2u);

    const auto r1 = hub.Read("c1");
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(r1->as_of_ts_ns, 100);
    EXPECT_EQ(r1->count, kMlFeatureCount);
    EXPECT_FALSE(hub.Read("nope").has_value());

    // 覆盖同 cid → 最新快照
    hub.Publish(MakeRec("c1", 150, 5.0f));
    EXPECT_EQ(hub.Size(), 2u) << "覆盖不增 key";
    EXPECT_EQ(hub.Read("c1")->as_of_ts_ns, 150);

    const auto all = hub.SnapshotAll();
    EXPECT_EQ(all.size(), 2u);
}
