// include/stcpp/polymarket/live_order_gate.hpp — 实盘下单闸 (ARM 总闸 + 翻译 + sink)
//
// Owner: GM (老雷) 2026-05-31 Phase 4; 2026-06-13 简化为 paper 同构 (老板「仿照虚拟盘,
//   不要多加不必要的限制」)。
//
// §8 红线 (任何下单必经 RiskManager) 的 enforcement 点在【决策环】: trading_loop.cpp 两个
//   下单路径在 sign + executor_->Execute 前 rm_.evaluate,
//   拒单到不了 executor 缝 — 与 paper 链路同一道闸同一处。
//
// gate 曾内置第二道 RM re-evaluate + 灰度日单数 RATE_CAP, 2026-06-13 删 (实盘首日全军覆没复盘):
//   ① 二次 RM 重复 — 上游决策环已过 RM, paper 链路也只有一道 (仿 paper);
//   ② 结构性必拒 — adapter 重建 intent 丢 book_snapshot_ts/signal_id/bytes32 → INVALID_INTENT/
//      BOOK_TS_ZERO 拦死 100% live 单; 即便补字段, 同 signal_id 也必撞 RM DUPLICATE_INTENT 去重集;
//   ③ RATE_CAP 20/日 是灰度遗留, paper 无此闸, 且生产无人调 ResetDailyCount = 实际终身 20 单。
//
// 唯一保留的闸: ARM (真金白银总闸, fail-closed, 默认 disarmed)。
//   策略只持有 LiveOrderGate, 拿不到裸 LiveOrderSubmitter (仍是 CLOB 唯一通路)。
//   开闸授权 = 老板一句话 → LIVE_ARMED=1 (2026-06-13 会签废除)。
#pragma once

#include "stcpp/polymarket/live_order_types.hpp"
#include "stcpp/risk/risk_gateway.hpp"

#include <functional>

namespace stcpp::polymarket {

// 下单出口 (生产 = LiveOrderSubmitter::Submit; 测试 = stub)。
using OrderSinkFn = std::function<LiveOrderResult(const LiveOrderRequest&)>;

// gate 拦截原因。
enum class GateBlock : std::uint8_t {
    NONE = 0,        // 未被 gate 拦
    DISARMED = 1,    // 未开闸
};

struct GateResult {
    bool submitted{false};   // 是否真触达 CLOB
    GateBlock gate_block{GateBlock::NONE};  // gate 层拦截 (DISARMED)
    LiveOrderResult order;   // 仅 submitted 时有效
};

class LiveOrderGate {
public:
    explicit LiveOrderGate(OrderSinkFn sink) noexcept : sink_(std::move(sink)) {}

    // ARM 过 → 翻译下单。入参须为决策环 RM-approved 的 intent (§8 在决策环 enforce,
    //   gate 不重决策不改 size/price — 老周 C-1)。neg_risk 来自市场目录 (gate 不臆造)。
    [[nodiscard]] GateResult Submit(const stcpp::risk::OrderIntent& intent, bool neg_risk) noexcept;

    // ---- ARM 开关 (fail-closed, 默认 disarmed) ----
    void Arm() noexcept { armed_ = true; }
    void Disarm() noexcept { armed_ = false; }  // kill: 立即停发
    [[nodiscard]] bool Armed() const noexcept { return armed_; }

private:
    OrderSinkFn sink_;
    bool armed_{false};       // 默认未开闸
};

// intent → LiveOrderRequest 翻译 (size_pUSD_micro + price → maker/taker micro)。
//   BUY : maker=size_pUSD_micro(USDC), taker=round(size/price)(shares)
//   SELL: maker=round(size/price)(shares), taker=size_pUSD_micro(USDC)
[[nodiscard]] LiveOrderRequest TranslateIntent(const stcpp::risk::OrderIntent& intent, bool neg_risk) noexcept;

}  // namespace stcpp::polymarket
