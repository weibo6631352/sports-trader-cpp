// test_hot_swap_model.cpp — 模型热加载持有器单测 (原子换 + 旧模型回收 + 非占有注入)。
#include <gtest/gtest.h>

#include <memory>

#include "stcpp/ml/hot_swap_model.hpp"
#include "stcpp/ml/seq_arb_model.hpp"

using stcpp::ml::HotSwapHolder;
using stcpp::ml::SeqArbModel;
using stcpp::ml::StubSeqArbModel;

TEST(HotSwap, StoreLoadSwap) {
    HotSwapHolder<SeqArbModel> h;
    EXPECT_EQ(h.Load(), nullptr) << "初始空";
    auto a = std::make_shared<StubSeqArbModel>(110, "model-A");
    h.Store(a);
    auto la = h.Load();
    ASSERT_NE(la, nullptr);
    EXPECT_EQ(la->model_id(), "model-A");
    // 热换 B
    auto b = std::make_shared<StubSeqArbModel>(110, "model-B");
    h.Store(b);
    EXPECT_EQ(h.Load()->model_id(), "model-B") << "原子换上新模型";
}

TEST(HotSwap, OldModelReclaimedAfterSwap) {
    HotSwapHolder<SeqArbModel> h;
    std::weak_ptr<const SeqArbModel> weak_a;
    {
        auto a = std::make_shared<StubSeqArbModel>(110, "A");
        weak_a = a;
        h.Store(std::move(a));  // holder 持唯一强引用
    }
    EXPECT_FALSE(weak_a.expired()) << "holder 持有时不回收";
    // 换 B (无人持 A 的 Load copy) → A 回收
    h.Store(std::make_shared<StubSeqArbModel>(110, "B"));
    EXPECT_TRUE(weak_a.expired()) << "换模型后旧模型自动回收 (无泄漏)";
}

TEST(HotSwap, LoadCopyKeepsModelAliveDuringSwap) {
    HotSwapHolder<SeqArbModel> h;
    std::weak_ptr<const SeqArbModel> weak_a;
    auto a = std::make_shared<StubSeqArbModel>(110, "A");
    weak_a = a;
    h.Store(std::move(a));
    auto inflight = h.Load();  // 推理线程持有的 copy
    h.Store(std::make_shared<StubSeqArbModel>(110, "B"));  // 同时热换
    EXPECT_FALSE(weak_a.expired()) << "推理期内持 Load copy → 旧模型不被删 (推理不崩)";
    EXPECT_EQ(inflight->model_id(), "A") << "in-flight 推理仍用旧模型 (一致性)";
    inflight.reset();
    EXPECT_TRUE(weak_a.expired()) << "推理释放后旧模型回收";
}

TEST(HotSwap, NonOwningInjection) {
    HotSwapHolder<SeqArbModel> h;
    StubSeqArbModel stack_model(110, "stack");  // 栈对象, 调用方管生命周期
    h.StoreNonOwning(&stack_model);
    auto m = h.Load();
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->model_id(), "stack");
    m.reset();  // 非占有 → 不删 stack_model (无 double-free; 栈对象正常析构)
}
