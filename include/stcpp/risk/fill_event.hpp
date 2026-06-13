// include/stcpp/risk/fill_event.hpp — 中性成交事件 (paper/live 共用; FillEvent 中性化)
//
// Owner: GM (老雷) 2026-05-31 — Phase 4 接线 (老郭 R-4 审计放行)。
//
// PositionLedger::apply_fill 的中性入参, 解耦 execution::VirtualFill。
//   VirtualFill (paper) 与 ExecReport (live) 都映射到本结构, 账本只消费一种中性类型。
//   纯 risk:: 命名空间 POD, 无 execution/polymarket 反向依赖 (守"零反向依赖"纪律)。
//
// 红线: mode_tag 是 R-11 fail-closed 载体 (apply_fill 仍 mode_tag!=0 → 拒, 方向不变);
//   4ts 严格透传上游 (R-20, 禁 now())。
#pragma once

#include <cstdint>
#include <string>

namespace stcpp::risk {

struct FillEvent {
    // 仓位增量 (VWAP 计算所需)。signed micro pUSD (= VirtualFill.fill_shares_micro 同单位, 直拷无 cast)。
    std::int64_t filled_size_micro{0};
    double fill_price{0.0};  // ∈ (0, 1)

    // R-11 fail-closed 载体 (0=paper 1=live 2=backtest)。apply_fill 仅 0 记账 (paper 专用账本)。
    std::uint8_t mode_tag{0};

    // live 溯源 (paper 恒空; 账本不消费, 留痕给审计/WAL)。
    std::string tx_hash;
    std::string order_id;

    // R-20 4ts (严格透传上游, 禁 now())。
    std::int64_t event_ts_ns{0};
    std::int64_t data_source_ts_ns{0};
    std::int64_t ingestion_ts_ns{0};
    std::int64_t as_of_ts_ns{0};
};

}  // namespace stcpp::risk
