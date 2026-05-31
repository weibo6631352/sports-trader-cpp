// include/stcpp/polymarket/live_order_gate.hpp — 实盘下单强制风控门 (mode-agnostic)
//
// Owner: GM (老雷) 2026-05-31 — Phase 4 D-A (评审会全员共识) + 开闸前安全控制。
//
// 红线 §8 第一条落地: 任何下单必经 RiskManager。LiveOrderGate 是策略到 CLOB 的**唯一通路**:
//   策略只持有 LiveOrderGate, 拿不到裸 LiveOrderSubmitter。
//
// 下单前三道门 (fail-closed, 任一不过绝不触达 sink):
//   1. ARM 开关 — 默认未开闸 (disarmed)。运行期人工 Arm() 才放行 (老韩 arm 层2)。
//   2. OrderRateCap — 日单数上限 (老韩新红线: 机器连发频控, RM 本身无此维度)。
//   3. RiskManager — rm_.evaluate() 必须 APPROVED (老周 C-1: gate 不重决策不改 size/price)。
//
// 下单 sink 注入 (OrderSinkFn): 生产绑 LiveOrderSubmitter::Submit; 测试注入 stub → 离线可测。
#pragma once

#include "stcpp/polymarket/live_order_types.hpp"
#include "stcpp/risk/risk_gateway.hpp"

#include <functional>

namespace stcpp::polymarket {

// 下单出口 (生产 = LiveOrderSubmitter::Submit; 测试 = stub)。
using OrderSinkFn = std::function<LiveOrderResult(const LiveOrderRequest&)>;

// gate 拦截原因 (非 RM 拒单)。
enum class GateBlock : std::uint8_t {
    NONE = 0,        // 未被 gate 拦 (通过到 RM)
    DISARMED = 1,    // 未开闸
    RATE_CAP = 2,    // 日单数超限
};

// gate 灰度配置 (老韩 Phase4 灰度值; 与 RM 的 LiveRiskConfig 分工: 这里管开闸+频控)。
struct LiveGateConfig {
    int max_orders_per_day{20};  // OrderRateCap 日单数上限 (老韩灰度初值)
};

struct GateResult {
    bool submitted{false};   // 是否真触达 CLOB
    bool rm_approved{false};
    GateBlock gate_block{GateBlock::NONE};  // gate 层拦截 (DISARMED/RATE_CAP)
    // 仅 gate_block==NONE && !rm_approved 时有意义 (默认值同 RiskDecision)。
    stcpp::risk::RejectCode reject_code{stcpp::risk::RejectCode::INTERNAL_ERROR};
    LiveOrderResult order;   // 仅 submitted 时有效
};

class LiveOrderGate {
public:
    LiveOrderGate(stcpp::risk::RiskGateway& rm, OrderSinkFn sink, LiveGateConfig cfg = {}) noexcept
        : rm_(rm), sink_(std::move(sink)), cfg_(cfg) {}

    // 三道门 → 全过才下单。neg_risk 来自市场目录 (gate 不臆造)。
    [[nodiscard]] GateResult Submit(const stcpp::risk::OrderIntent& intent, bool neg_risk) noexcept;

    // ---- ARM 开关 (fail-closed, 默认 disarmed) ----
    void Arm() noexcept { armed_ = true; }
    void Disarm() noexcept { armed_ = false; }  // kill: 立即停发
    [[nodiscard]] bool Armed() const noexcept { return armed_; }

    // ---- OrderRateCap ----
    [[nodiscard]] int OrdersToday() const noexcept { return orders_today_; }
    void ResetDailyCount() noexcept { orders_today_ = 0; }  // 运营日切

private:
    stcpp::risk::RiskGateway& rm_;
    OrderSinkFn sink_;
    LiveGateConfig cfg_;
    bool armed_{false};       // 默认未开闸
    int orders_today_{0};     // 已触达 CLOB 单数 (RATE_CAP 用)
};

// intent → LiveOrderRequest 翻译 (size_pUSD_micro + price → maker/taker micro)。
//   BUY : maker=size_pUSD_micro(USDC), taker=round(size/price)(shares)
//   SELL: maker=round(size/price)(shares), taker=size_pUSD_micro(USDC)
[[nodiscard]] LiveOrderRequest TranslateIntent(const stcpp::risk::OrderIntent& intent, bool neg_risk) noexcept;

// 灰度上线 RiskConfig 工厂 (老韩灰度值: per-order $1 / per-cond+outcome $2 / bankroll $25 /
//   日亏 halt $5 / 连亏 3)。**独立, 绝不复用 paper 100k 默认。** live RM 装配时用。
[[nodiscard]] stcpp::risk::RiskConfig MakeGrayLaunchRiskConfig() noexcept;

}  // namespace stcpp::polymarket
