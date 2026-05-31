// include/stcpp/execution/order_executor.hpp — 订单执行器抽象 (老郭 R-4 审计放行)
//
// Owner: GM (老雷) 2026-05-31 — Phase 4 接线 Step B (G-1 executor 注入)。
//
// 把 "订单怎么执行" 从决策循环解耦: paper 走 VirtualExecutor (虚拟撮合), live 将来走
//   LiveExecutorAdapter (gate→submitter, 需 4ts 补全 + 老韩开闸, 本次不接)。
//
// VirtualExecutor 是 matcher_.Match 的透明转发 → paper 行为 bit-identical (同 matcher 实例
//   同 RNG 流; 老郭审计硬约束, 1119 ctest 守住)。返回 VirtualFill (保 paper 下游零改)。
#pragma once

#include "stcpp/execution/virtual_matcher.hpp"

namespace stcpp::execution {

class IOrderExecutor {
public:
    virtual ~IOrderExecutor() = default;
    // RM-approved 的 VirtualOrder → VirtualFill (reject/missed 由 fill.reject 表达, 调用方过滤)。
    [[nodiscard]] virtual VirtualFill Execute(const VirtualOrder& order) noexcept = 0;
};

// 虚拟撮合执行器 (paper): 透明转发 matcher_.Match, 行为逐位不变。
class VirtualExecutor final : public IOrderExecutor {
public:
    explicit VirtualExecutor(VirtualMatcher& matcher) noexcept : matcher_(matcher) {}
    [[nodiscard]] VirtualFill Execute(const VirtualOrder& order) noexcept override {
        return matcher_.Match(order);
    }

private:
    VirtualMatcher& matcher_;
};

}  // namespace stcpp::execution
