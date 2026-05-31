// include/stcpp/paper/binary_market_snapshot.hpp — 二元市场双边盘口决策入参 (老周架构 v1)
//
// Owner: 老周 (系统工程部主管 + 架构主权) — 实施 docs/RESEARCH/laozhou-binary-dual-side-arch-v1.md
// last_review: 2026-05-31
//
// 老板设计原则 (逐字, §8.1 红线治理纪律 #1 粘原文):
//   「一个二元市场应该既持有 yes 的快照，也持有 no 的快照，触发 WSS 订阅时，只刷新单边的，
//    这个没问题。但是进入决策时，我们一定要带入这个盘口的信息，而不仅仅是单单一边的信息。」
//
// 三条工程约束: C1 双边存储 (hub 已满足) / C2 单边刷新 (WSS 既定, 不改) / C3 决策带整盘口 (本文件)。
//
// 架构裁定 (老周 §8): 存储层 hub 不动 (per-token double-buffer 已正确); 新增**决策侧值聚合 view**,
//   决策线程 (loop_thread_) 每 tick 栈上由两次 hub_.Read() 组装, 零锁零存储 (R-12 不触碰)。
//
// 边界: 老周定结构 (本文件); 小袁定微观结构选边 (SelectSide 微观); 小梁定策略选边 + sizing 方向。
//   M1: SelectSide 桩恒返 {Yes, Buy} (与现状逐位等价); M2: 真双边选边 + sell-to-open 空头。
#pragma once

#include <cstdint>
#include <string>

#include "stcpp/polymarket/clob_wss/orderbook_snapshot_hub.hpp"
#include "stcpp/strategy/signal_iface.hpp"  // strategy::Side

namespace stcpp::paper {

// 一侧 (YES 或 NO token) 的快照 + 可用性。决策入参用, 非存储。
struct SideView {
    bool present{false};                             // hub.Read 命中 + valid
    polymarket::clob_wss::OrderBookFeatures book{};  // 该 token 完整 5 档 + 微观 + 独立 4ts

    // 便捷访问 (转发 book; 决策侧少写 .book.)
    [[nodiscard]] double best_bid() const noexcept { return book.best_bid(); }
    [[nodiscard]] double best_ask() const noexcept { return book.best_ask(); }
    [[nodiscard]] double microprice() const noexcept { return book.microprice; }
    [[nodiscard]] double mid() const noexcept { return book.mid; }
    [[nodiscard]] std::int64_t data_source_ts_ns() const noexcept { return book.data_source_ts_ns; }
};

// 一个 binary condition 的整盘口 (YES book + NO book)。决策线程栈上组装, 零锁零 malloc。
struct BinaryMarketSnapshot {
    std::string condition_id;
    std::string yes_token_id;
    std::string no_token_id;
    // 树上行引用 (盘口→event 父节点; 2026-05-31 统一数据树)。兄弟盘口经 event_id 在目录里导航 (不内嵌变长)。
    //   event_id: gamma Event (同场比赛多盘口共享父); neg_risk_market_id: negRisk 互斥组父合约 (可空)。
    std::string event_id;
    std::string neg_risk_market_id;
    SideView yes;  // present=false 表示该侧 hub 无快照 / invalid
    SideView no;   // 单边可用即可决策 (退化), 双边可用走完整双边逻辑

    // 至少一侧可决策 (fail-closed: 两边都缺 → 跳过该 condition)
    [[nodiscard]] bool any_side_present() const noexcept { return yes.present || no.present; }
    [[nodiscard]] bool both_sides_present() const noexcept { return yes.present && no.present; }
};

// 选边输出 (SelectSide 返回)。M1 桩恒 {Yes, Buy}; M2 开放 No / Sell。
enum class TradedSide : std::uint8_t { None = 0, Yes = 1, No = 2 };

struct DecisionSide {
    TradedSide outcome{TradedSide::None};      // None = 不交易 (两边都不值得)
    strategy::Side side{strategy::Side::Buy};  // M1 恒 Buy; M2 开放 Sell (sell-to-open 空头)
    double conviction{0.0};                    // 选边置信 (小袁/小梁 M2 填; audit/quote 展示)
};

}  // namespace stcpp::paper
