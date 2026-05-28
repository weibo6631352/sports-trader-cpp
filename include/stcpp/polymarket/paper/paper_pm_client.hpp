// stcpp/polymarket/paper/paper_pm_client.hpp — Paper mode PolymarketClient (mock, 不真上链)
//
// Owner: 老李 (#07) — Sprint-2 W5 Wave 24
//
// 落:
//   docs/RESEARCH/laoli-polymarket-backend-requirements-v1.md §1.3 (paper 走真 / 走 mock 决策表)
//                                                        §3.1 (14 接口 paper 实现列)
//                                                        §6   (OrderStatus 7 状态机)
//                                                        §7.1 (paper 不上链 mock 表)
//
// 红线:
//   R-7  仅进 paper binary (CMake STCPP_EXEC_MODE=paper 才 link)
//   R-11 OrderAck.audit_wal_kind 硬填 PaperAudit (基类默认值); 严禁污染 RiskAudit / Position
//   R-12 同步 API; caller 在 vCPU3 worker pool 调用 (signer 不可在 WSS event loop 直调)
//   R-20 入口 / 出口 全部携带 TimestampQuad, UPSTREAM_PAYLOAD 优先, 单测 grep enforce
//   HMAC 4 bug: paper 不签真签名, 但 SignedOrder.signature 字段 reserve 给 live W5+ 接入
//
// paper / live 共用 IPolymarketClient 抽象, paper 实现:
//   - F-01 GetOrderbook: 从内存 book mirror 拿 (paper WSS sports channel 真接 read-only, 见 §1.3)
//   - F-02 SubmitOrder:  生成 mock order_id "paper-<seq>", 走 VirtualMatcher (此 v0.1 mock 直接 Booked)
//   - F-03 CancelOrder:  内存 OrderTable lookup + 状态转 Canceled
//   - F-04 CancelAll:    所有 non-terminal 挂单 → Canceled, 返回 count
//   - F-05 GetMarketInfo: 静态 mock (W5 联调期由小段 + 小田 接入 gamma listing cache)
//   - F-06 GetUserPositions: PaperLedger 内 BTreeMap (此 v0.1 返空 / 单测注入)
//   - F-07 GetBalance:   infinite (paper 永远不爆)
//   - F-08 GetOrderStatus: lookup OrderTable
//   - F-09 GetMyOpenOrders: 过滤 non-terminal
//   - F-10 GetMyTrades:  VirtualMatcher 成交流 (此 v0.1 空, W5 接入)
//   - F-11/F-12 paper 不用 (返 BadRequest "paper mode not applicable")
//   - F-13 GetPricesHistory: 公开 endpoint paper 也可真接 (此 v0.1 返空, W5 联调真接)
//   - F-14 SubscribeSportsWss: paper 读真 WSS sports channel (此 v0.1 占位 callback, W5 接小冯)

#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "stcpp/polymarket/pm_client.hpp"

namespace stcpp::polymarket::paper {

// In-memory order table (paper, R-11 不污染真 ledger).
struct PaperOrderEntry {
    OrderAck      ack;
    SignedOrder   original;     // 留底, 撤单时 echo
};

class PaperPolymarketClient final : public IPolymarketClient {
 public:
    explicit PaperPolymarketClient(std::uint64_t order_id_seed = 0) noexcept
        : next_order_seq_(order_id_seed) {}

    [[nodiscard]] execution::ExecutionMode Mode() const noexcept override {
        return execution::ExecutionMode::Paper;
    }

    // --- F-01 ---
    [[nodiscard]] Result<OrderBookSnapshot> GetOrderbook(std::string_view condition_id) noexcept override;

    // --- F-02 ---
    [[nodiscard]] Result<OrderAck> SubmitOrder(const SignedOrder& order) noexcept override;

    // --- F-03 ---
    [[nodiscard]] Result<OrderAck> CancelOrder(std::string_view order_id) noexcept override;

    // --- F-04 ---
    [[nodiscard]] Result<std::uint32_t> CancelAll() noexcept override;

    // --- F-05 ---
    [[nodiscard]] Result<MarketInfo> GetMarketInfo(std::string_view condition_id) noexcept override;

    // --- F-06 ---
    [[nodiscard]] Result<std::vector<Position>> GetUserPositions(std::string_view funder_addr) noexcept override;

    // --- F-07 ---
    [[nodiscard]] Result<Balance> GetBalance() noexcept override;

    // --- F-08 ---
    [[nodiscard]] Result<OrderAck> GetOrderStatus(std::string_view order_id) noexcept override;

    // --- F-09 ---
    [[nodiscard]] Result<std::vector<OrderAck>> GetMyOpenOrders() noexcept override;

    // --- F-10 ---
    [[nodiscard]] Result<std::vector<Trade>> GetMyTrades(std::uint32_t limit) noexcept override;

    // --- F-11 / F-12 (paper 不用) ---
    [[nodiscard]] Result<std::string> DeriveApiKey() noexcept override;
    [[nodiscard]] Result<std::vector<std::string>> ListApiKeys() noexcept override;

    // --- F-13 ---
    [[nodiscard]] Result<std::vector<PriceHistoryPoint>> GetPricesHistory(
        std::string_view token_id,
        std::int64_t     start_unix_s,
        std::int64_t     end_unix_s) noexcept override;

    // --- F-14 ---
    [[nodiscard]] Result<std::uint32_t> SubscribeSportsWss(
        const std::vector<std::string>& condition_ids,
        OrderBookCallback                cb,
        void*                            user_data) noexcept override;

    // ---------- 单测辅助 (生产代码勿调) ----------
    //
    // 注入 orderbook (paper WSS 真接前的内存 mirror; W5 小冯 WSS subscriber 接入后调用此)
    void TestInjectOrderbook(const OrderBookSnapshot& snap) noexcept;
    // 注入 position (PaperLedger PoC 接入前的桩)
    void TestInjectPosition(const Position& p) noexcept;
    // 注入 market info
    void TestInjectMarketInfo(const MarketInfo& m) noexcept;
    // 推进 order 到指定状态 (单测 7 状态机用); 不合法转移返 false (不变状态)
    [[nodiscard]] bool TestTransitionOrder(std::string_view order_id, OrderStatus to) noexcept;
    // 注入 trade (单测 F-10 用)
    void TestInjectTrade(const Trade& t) noexcept;
    // 注入余额 (单测 F-07 用)
    void TestSetBalance(const Balance& b) noexcept;

 private:
    [[nodiscard]] std::string MakeOrderId() noexcept;

    mutable std::mutex                                       mu_;
    std::uint64_t                                            next_order_seq_;
    std::unordered_map<std::string, OrderBookSnapshot>       books_;        // condition_id → snap
    std::unordered_map<std::string, MarketInfo>              markets_;      // condition_id → info
    std::unordered_map<std::string, PaperOrderEntry>         orders_;       // order_id → entry
    std::vector<Position>                                    positions_;    // PaperLedger 桩
    std::vector<Trade>                                       trades_;       // VirtualMatcher 桩
    Balance                                                  balance_;      // F-07 mock infinite
    bool                                                     balance_set_{false};
};

}  // namespace stcpp::polymarket::paper
