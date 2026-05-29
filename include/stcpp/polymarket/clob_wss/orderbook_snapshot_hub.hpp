// include/stcpp/polymarket/clob_wss/orderbook_snapshot_hub.hpp
//
// v0.1 — per-token double-buffer OrderBookFeatures 只读快照发布器
//
// Owner: 小冯 (#34)
// last_review: 2026-05-29
//
// 架构说明 — double-buffer 原子发布 (R-12 零反向依赖):
//   vCPU0 (WSS event loop) 通过 Publish() 写 back-buffer, 完成后 atomic swap
//   →  观测线程 (debug_api / 监控) 通过 Read(token_id) 读 front-buffer 的原子快照.
//
//   内部结构: per-token 双槽 (slot[0] / slot[1]).
//     writer_slot_[token_idx] ∈ {0,1}  — 当前 back-slot (vCPU0 写)
//     atomic<slot_idx_t>[token_idx]     — 读者可见的 front-slot (原子 load)
//
//   flip 序列:
//     1. vCPU0 取 back = 1 - front_atomic.load(relaxed)
//     2. 写入 buf_[token_idx][back]
//     3. front_atomic.store(back, release)    // 读者看到一致快照
//     读者:
//     1. idx = front_atomic.load(acquire)
//     2. 复制 buf_[token_idx][idx]            // 纯读, 无锁, 无阻塞
//
//   R-12 保证:
//     - 写端: noexcept, 无 malloc (token_idx 由 vCPU0 在 cold path 预分配)
//     - 读端: 只做 atomic::load(acquire) + memcpy, < 1us
//     - 无锁: 无 mutex/spinlock; 依靠 C++20 atomic release/acquire
//     - 无 RM/signer/exec 依赖 (零反向依赖)
//
//   R-20 时间戳:
//     OrderBookFeatures 包含完整 4-ts (event / data_source / ingestion / as_of)
//     所有 ts 来自上游 WssEvent payload, 不本地 now() 替代 data_source_ts
//
// 使用方式:
//   vCPU0 (hot path):
//     hub_.Publish(token_id, features);   // double-buffer swap
//
//   观测线程 (debug_api state_provider impl):
//     auto snap = hub_.Read(token_id);    // 无锁原子读 → optional<OrderBookFeatures>
//     if (snap) { ... }
//
//   不要在本文件里 #include RM / signer / exec.
//
// 与 debug_api 的集成方式 (集成步骤, 后续独立做):
//   1. main 构造 OrderBookSnapshotHub hub_;
//   2. WSS event loop vCPU0 在 OrderBookAdapter::OnEvent 之后调用
//      hub_.Publish(token_id, MakeFeatures(out_snap, out_book_row, ...))
//   3. 实现 RealBookStateProvider : public StateProvider {
//          BookSnapshot book(const std::string& token_id) const override {
//              auto feat = hub_.Read(token_id);
//              if (!feat) return {};
//              return ToBookSnapshot(*feat);
//          }
//      }
//      其中 ToBookSnapshot() 完成 OrderBookFeatures → debug_api::BookSnapshot 映射
//      (字段说明见本文件末的"映射说明"注释)
//
// ============================================================================

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "stcpp/microstructure/orderbook.hpp"  // OrderBookLevel, kBookDepthLevels

namespace stcpp::polymarket::clob_wss {

using stcpp::microstructure::kBookDepthLevels;
using stcpp::microstructure::OrderBookLevel;

// ---------------------------------------------------------------------------
// WssConnState — per-token WSS 连接状态 (供 BookSnapshot.wss_state 映射)
// ---------------------------------------------------------------------------
enum class WssConnState : std::uint8_t {
    kUnknown = 0,
    kConnected = 1,
    kReconnecting = 2,
    kDisconnected = 3,
};

[[nodiscard]] inline const char* WssConnStateName(WssConnState s) noexcept {
    switch (s) {
        case WssConnState::kConnected:
            return "CONNECTED";
        case WssConnState::kReconnecting:
            return "RECONNECTING";
        case WssConnState::kDisconnected:
            return "DISCONNECTED";
        default:
            return "UNKNOWN";
    }
}

// ---------------------------------------------------------------------------
// OrderBookFeatures — per-token 快照 POD (double-buffer 单元)
//
// 设计原则:
//   - 纯 POD (trivially copyable) → 观测侧可安全 memcpy 或 value copy
//   - 不含 std::string (热路径 token_id 用外部 key 管理)
//   - 4-ts 字段全部来自上游 WssEvent (R-20)
//   - 5档 bid/ask (kBookDepthLevels = 5); v0.1 仅 L1 有效, L2..L5 = NaN
//   - sequence_no / gap_count: per-token WSS 序列控制
// ---------------------------------------------------------------------------
struct OrderBookFeatures {
    // R-20: 4 时间戳 (上游 WSS payload 填充, 禁本地 now() 替代 data_source_ts)
    std::int64_t event_ts_ns{0};
    std::int64_t data_source_ts_ns{0};
    std::int64_t ingestion_ts_ns{0};
    std::int64_t as_of_ts_ns{0};  // 由观测消费方在读取时填写 (R-20 allowed)

    // 5 档 bid / ask (index 0 = best; NaN = level 无深度)
    std::array<OrderBookLevel, kBookDepthLevels> bids{};
    std::array<OrderBookLevel, kBookDepthLevels> asks{};

    // 微观结构派生 (由 compute_l1_probe 计算)
    double microprice{std::numeric_limits<double>::quiet_NaN()};
    double mid{std::numeric_limits<double>::quiet_NaN()};
    double spread{std::numeric_limits<double>::quiet_NaN()};     // ask[0] - bid[0] (absolute)
    double imbalance{std::numeric_limits<double>::quiet_NaN()};  // ∈ [-1, 1]

    // 序列控制 (per-token)
    std::int64_t sequence_no{0};
    std::int64_t gap_count{0};

    // WSS 连接状态
    WssConnState wss_state{WssConnState::kUnknown};

    // 有效性标记 (首次 snapshot 后为 true)
    bool valid{false};

    // ---- 快速访问 ----
    [[nodiscard]] double best_bid() const noexcept { return bids[0].price; }
    [[nodiscard]] double best_ask() const noexcept { return asks[0].price; }
    [[nodiscard]] double best_bid_size() const noexcept { return bids[0].size_usdc; }
    [[nodiscard]] double best_ask_size() const noexcept { return asks[0].size_usdc; }

    // R-20: 4-ts 单调链验证
    [[nodiscard]] bool ts_chain_ok() const noexcept {
        return event_ts_ns > 0 && data_source_ts_ns >= event_ts_ns && ingestion_ts_ns >= data_source_ts_ns &&
               as_of_ts_ns >= ingestion_ts_ns;
    }
};

static_assert(std::is_trivially_copyable_v<OrderBookFeatures>,
              "OrderBookFeatures must be trivially copyable for lock-free double-buffer");

// ---------------------------------------------------------------------------
// OrderBookSnapshotHub — per-token double-buffer 快照发布/读取
//
// 线程安全: 单写多读 (SWMR)
//   - 写端 (vCPU0 only): Publish()
//   - 读端 (任意线程): Read() — 原子 acquire + value copy
//
// 容量: max_tokens 在构造时预分配, hot path 不 grow
// ---------------------------------------------------------------------------
class OrderBookSnapshotHub {
public:
    static constexpr std::size_t kDefaultMaxTokens = 1024;

    explicit OrderBookSnapshotHub(std::size_t max_tokens = kDefaultMaxTokens);

    // Delete copy/move (状态机, 不允许拷贝)
    OrderBookSnapshotHub(const OrderBookSnapshotHub&) = delete;
    OrderBookSnapshotHub& operator=(const OrderBookSnapshotHub&) = delete;
    OrderBookSnapshotHub(OrderBookSnapshotHub&&) = delete;
    OrderBookSnapshotHub& operator=(OrderBookSnapshotHub&&) = delete;

    ~OrderBookSnapshotHub() = default;

    // -----------------------------------------------------------------------
    // Publish — vCPU0 hot path 写入快照 (无锁, noexcept)
    //
    //   1. 找/分配 token 的内部 index (首次见到 token_id 时 cold-path 分配)
    //   2. 写入 back-buffer slot
    //   3. atomic store(release) 令读端可见
    //
    //   token_id: caller 保证生命周期 >= 本次调用 (string_view)
    //   features: 值拷贝进 back-buffer (trivially copyable POD)
    //
    // R-12: 如 token_idx 已分配 → O(1) unordered_map lookup + atomic swap
    //        首次分配 (cold path) 可 malloc; hot path 后续无 malloc
    // -----------------------------------------------------------------------
    void Publish(std::string_view token_id, const OrderBookFeatures& features) noexcept;

    // -----------------------------------------------------------------------
    // Read — 观测线程只读快照 (无锁, 线程安全)
    //
    //   返回 std::optional<OrderBookFeatures>:
    //     - nullopt: token_id 未曾 Publish 过 (或 max_tokens 超限)
    //     - value:   最新原子快照 (可能含 valid=false 的初始状态 — 调用方检查)
    //
    //   流程: front_atomic.load(acquire) → value copy 一份
    //   时延: atomic load + trivially copyable copy ≈ < 500ns (远低于 100us R-12 限制)
    // -----------------------------------------------------------------------
    [[nodiscard]] std::optional<OrderBookFeatures> Read(std::string_view token_id) const noexcept;

    // -----------------------------------------------------------------------
    // ReadRaw — 直接返回指针 (zero-copy, 调用方必须在 Read 返回的 idx 有效期内使用)
    //   仅供高频内部使用; 一般消费方用 Read() 值语义
    // -----------------------------------------------------------------------
    [[nodiscard]] const OrderBookFeatures* ReadRaw(std::string_view token_id) const noexcept;

    // -----------------------------------------------------------------------
    // ResetToken — 清除 token 状态 (reconnect 时由 vCPU0 调用)
    // -----------------------------------------------------------------------
    void ResetToken(std::string_view token_id) noexcept;

    // -----------------------------------------------------------------------
    // ResetAll — 清除全部 token 状态 (全量重连)
    // -----------------------------------------------------------------------
    void ResetAll() noexcept;

    // -----------------------------------------------------------------------
    // token_count — 当前已注册 token 数量 (供监控)
    // -----------------------------------------------------------------------
    [[nodiscard]] std::size_t token_count() const noexcept;

    // -----------------------------------------------------------------------
    // publish_count — 累计 Publish 调用次数 (供监控)
    // -----------------------------------------------------------------------
    [[nodiscard]] std::uint64_t publish_count() const noexcept { return publish_count_; }

    // -----------------------------------------------------------------------
    // OldestEventTsNs — 遍历所有已发布 token 的 front-buffer，返回最小 event_ts_ns
    //
    // 用途: P1-4 staleness 计算 — max_staleness_ms = (now - oldest_event_ts) / 1e6
    //   观测线程调用; 遍历 slot_count_ 个 slot, 每个 atomic acquire load + 字段读取.
    //   时延: O(N_tokens), N 通常 ≤ 200; 观测路径 (非热路径), R-12 compliant.
    //
    // 返回值:
    //   0            — 无已发布 token (hub 空, 无数据)
    //   > 0          — 最老 event_ts_ns (ns epoch)
    //
    // 线程安全: 与 Publish() SWMR 兼容 (每 slot atomic acquire 读 front index)。
    // -----------------------------------------------------------------------
    [[nodiscard]] std::int64_t OldestEventTsNs() const noexcept;

private:
    // -----------------------------------------------------------------------
    // TokenSlot — per-token double-buffer 存储
    //
    // buf_[0] / buf_[1]: 两个 OrderBookFeatures 槽
    // front_: 当前读者可见的槽 index (0 or 1), atomic<uint8_t>
    //   - store: release (writer)
    //   - load:  acquire (reader)
    // -----------------------------------------------------------------------
    struct TokenSlot {
        std::array<OrderBookFeatures, 2> buf{};
        std::atomic<std::uint8_t> front{0};  // 0 or 1

        TokenSlot() noexcept = default;
        // atomic 不可拷贝, 需手动 move
        TokenSlot(TokenSlot&&) = delete;
        TokenSlot& operator=(TokenSlot&&) = delete;
        TokenSlot(const TokenSlot&) = delete;
        TokenSlot& operator=(const TokenSlot&) = delete;
    };

    // token_id (string) → TokenSlot index into slots_
    // 注: unordered_map 在预分配后 hot path 不 grow → R-12 compliant
    std::unordered_map<std::string, std::size_t> index_map_;

    // slots_ 用 unique_ptr<TokenSlot[]> 避免 TokenSlot 不可拷贝带来的 vector 问题
    std::unique_ptr<TokenSlot[]> slots_;
    std::size_t max_tokens_;
    std::size_t slot_count_{0};  // 已分配 slot 数 (vCPU0 only 写)

    std::uint64_t publish_count_{0};  // vCPU0 only

    // -----------------------------------------------------------------------
    // GetOrAllocSlot — 找 index; 若 token_id 未见过则分配新 slot (可 malloc)
    //   返回 kInvalidIdx 表示容量超限
    // -----------------------------------------------------------------------
    static constexpr std::size_t kInvalidIdx = std::numeric_limits<std::size_t>::max();
    [[nodiscard]] std::size_t GetOrAllocSlot(std::string_view token_id) noexcept;

    // -----------------------------------------------------------------------
    // FindSlot — 只查, 不分配 (const, 供读端)
    // -----------------------------------------------------------------------
    [[nodiscard]] std::size_t FindSlot(std::string_view token_id) const noexcept;
};

// ---------------------------------------------------------------------------
// 映射说明 (OrderBookFeatures → debug_api::BookSnapshot)
//
// 集成时 RealBookStateProvider::book(token_id) 需完成以下映射:
//   features.valid              → BookSnapshot::found
//   token_id (外部 key)         → BookSnapshot::token_id
//   (外部 condition_id 映射)    → BookSnapshot::condition_id
//   (外部 outcome 映射)         → BookSnapshot::outcome
//   features.best_bid()         → BookSnapshot::best_bid
//   features.best_ask()         → BookSnapshot::best_ask
//   features.microprice         → BookSnapshot::microprice
//   features.spread             → BookSnapshot::spread
//   features.imbalance          → BookSnapshot::imbalance
//   features.sequence_no        → BookSnapshot::sequence_no
//   features.gap_count          → BookSnapshot::gap_count
//   WssConnStateName(wss_state) → BookSnapshot::wss_state
//   features.{event/data_source/ingestion/as_of}_ts_ns → BookSnapshot::ts.*
//   features.bids / asks (逐档) → BookSnapshot::bids / asks (BookLevel{price, size})
// ---------------------------------------------------------------------------

}  // namespace stcpp::polymarket::clob_wss
