// include/stcpp/ml/hot_swap_model.hpp — 运行时模型热加载持有器 (在线/准在线; 主计划 v1 §3 online)
//
// Owner: 老雷 (GM) — 短时套利引擎 模块6 (热加载)
// last_review: 2026-06-01
//
// "边跑边训" 生产侧基础件: 旁边离线 Python 训练进程持续吃稠密标签 → 产新 ONNX → 生产 C++ 不停盘换模型。
//   - 推理线程 Load() 拿 shared_ptr copy (引用计数+1), 即使同时被 Store() 换掉, 本次推理引用期内旧模型
//     不被删 → 推理永不读到半换状态 / 不崩。旧模型在最后引用释放时自动回收 (无泄漏, 无显式同步)。
//   - 同步: Apple libc++ 暂不支持 std::atomic<std::shared_ptr> → 用短锁守 (仅 shared_ptr copy/swap, ns 级,
//     远 <R-12 100us; 且在 paper 决策线程 (loop_thread_), 非 WSS event loop, R-12 不适用此处)。
//   - 红线: 训练 Python 独立离线进程 (不进生产); 生产只 C++ 推理 + 此处换模型。
//
// 用法: HotSwapHolder<SeqArbModel> h; h.Store(make_seq_arb_model(...)); auto m=h.Load(); if(m) m->predict(fv);
#pragma once

#include <memory>
#include <mutex>

namespace stcpp::ml {

template <class Model>
class HotSwapHolder {
public:
    HotSwapHolder() = default;
    explicit HotSwapHolder(std::shared_ptr<const Model> m) noexcept : ptr_(std::move(m)) {}

    // 换模型 (任意线程; 训练进程产新 ONNX 后调)。旧模型在最后引用释放时回收。
    void Store(std::shared_ptr<const Model> m) noexcept {
        std::lock_guard<std::mutex> g(mu_);
        ptr_ = std::move(m);
    }

    // 拿当前模型 (推理线程; 返回 shared_ptr copy 保本次推理期内不被回收)。
    [[nodiscard]] std::shared_ptr<const Model> Load() const noexcept {
        std::lock_guard<std::mutex> g(mu_);
        return ptr_;
    }

    // 非占有注入 (裸指针; 调用方管生命周期 — 启动期注入栈/成员对象, 兼容旧 SetXxxModel(const*) 接口)。
    void StoreNonOwning(const Model* m) noexcept {
        Store(std::shared_ptr<const Model>(std::shared_ptr<const void>{}, m));
    }

private:
    mutable std::mutex mu_;
    std::shared_ptr<const Model> ptr_;
};

}  // namespace stcpp::ml
