// include/stcpp/risk/ledger_snapshot_hub.hpp
//
// v0.1 — per-market double-buffer LedgerFeatures 只读快照发布器
//
// Owner: 小石 (#41, data-structures-expert, G-LEDGER-OWNER)
// last_review: 2026-05-29
//
// 架构说明 — double-buffer 原子发布 (同构 orderbook_snapshot_hub, R-12 零反向依赖):
//   热路径写端 (RM / VirtualMatcher 线程) 通过 Publish() 写 back-buffer, 完成后 atomic swap.
//   观测线程 (debug_api / 监控) 通过 Read(market_key) 读 front-buffer 的原子快照 (值拷贝).
//
//   内部结构: per-key 双槽 (slot[0] / slot[1]).
//     writer_slot_[key_idx] ∈ {0,1}  — 由 1 - front_atomic.load(relaxed) 决定
//     atomic<uint8_t>[key_idx]       — 读者可见的 front-slot
//
//   flip 序列:
//     写端:
//       1. back = 1 - front_atomic.load(relaxed)
//       2. 写入 buf_[key_idx][back]
//       3. front_atomic.store(back, release)    // 读者看到一致快照
//     读端:
//       1. idx = front_atomic.load(acquire)
//       2. 复制 buf_[key_idx][idx]              // 纯读, 无锁, 无阻塞
//
// R-12 保证:
//   - 写端: noexcept, 无 malloc (key 由写端在 cold path 预分配)
//   - 读端: 只做 atomic::load(acquire) + value copy, < 1us
//   - 无锁: 无 mutex/spinlock; 依靠 C++20 atomic release/acquire
//   - 零反向依赖: 不 #include debug_api / RM / signer / exec 热路径头
//
// R-11 保证:
//   - LedgerFeatures 携带 ExecMode 字段 (paper/live/backtest), 写端在 Publish 时填入
//   - debug_api 消费方可据此在 response 顶层携带 mode 字段
//
// R-20 时间戳:
//   LedgerFeatures 包含完整 4-ts (event / data_source / ingestion / as_of)
//   所有 ts 来自上游 VirtualFill.as_of_ts_ns 等链路, 禁本地 now() 替代
//
// 使用方式:
//   热路径写端 (RM 线程 / VirtualMatcher 线程):
//     hub_.Publish(market_key, features);   // double-buffer swap
//
//   观测线程 (debug_api state_provider impl):
//     auto snap = hub_.Read(market_key);    // 无锁原子读 → optional<LedgerFeatures>
//     if (snap) { /* 映射到 HoldingView / PnlAttribution */ }
//
//   不要在本文件里 #include RM / signer / exec / debug_api.
//
// debug_api 映射说明 (见文件末尾注释块):
//   LedgerFeatures → debug_api::HoldingView (per positions endpoint)
//   LedgerFeatures → debug_api::PnlAttribution.per_market (per pnl endpoint)
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

namespace stcpp::risk {

// ---------------------------------------------------------------------------
// ExecutionModeTag — R-11: paper 模式标记 (不引入 execution_mode.hpp 保持零反向依赖)
// ---------------------------------------------------------------------------
enum class ExecutionModeTag : std::uint8_t {
    kPaper = 0,
    kLive = 1,
    kBacktest = 2,
};

// ---------------------------------------------------------------------------
// LedgerFeatures — per-market/per-token 持仓+PnL 快照 POD (double-buffer 单元)
//
// 设计原则:
//   - 纯 POD (trivially copyable) → 观测侧可安全 memcpy 或 value copy
//   - 不含 std::string (key 由外部 market_key 管理; 字符字段用固定 char 数组)
//   - 4-ts 字段全部来自上游链路 (R-20)
//   - R-11: mode 字段区分 paper/live/backtest
// ---------------------------------------------------------------------------
struct LedgerFeatures {
    // ---- R-20: 4 时间戳 (严格透传上游; 禁本地 now() 替代 data_source_ts) ----
    std::int64_t event_ts_ns{0};
    std::int64_t data_source_ts_ns{0};
    std::int64_t ingestion_ts_ns{0};
    std::int64_t as_of_ts_ns{0};  // 来自 VirtualFill.as_of_ts_ns (R-20 链路末端)

    // ---- 仓位字段 (per-token/per-market; 对应 HoldingView) ----
    // net_qty: 净持仓 (signed, > 0 多仓 / < 0 空仓 / 0 无仓)
    double net_qty{0.0};
    // avg_entry_price: 加权平均入场价 ∈ (0, 1)
    double avg_entry_price{0.0};
    // mark_price: 当前市价 (来自 orderbook microprice; 写端填入)
    double mark_price{0.0};

    // ---- PnL 字段 (对应 HoldingView.pnl_* 及 PnlAttribution) ----
    // pnl_realized: 已实现 PnL (USDC, 来自历史成交; R-11 paper 账本)
    double pnl_realized{0.0};
    // pnl_unrealized: 未实现 PnL = (mark_price - avg_entry_price) × net_qty
    double pnl_unrealized{0.0};
    // pnl_fee: 已付手续费 (正数; taker fee)
    double pnl_fee{0.0};
    // pnl_gross: 毛 PnL = pnl_realized + pnl_unrealized
    double pnl_gross{0.0};

    // ---- 状态标记 ----
    // valid: 首次 Publish 后为 true; 初始状态 false
    bool valid{false};
    // R-11: 运行模式 (paper=0 / live=1 / backtest=2)
    ExecutionModeTag mode{ExecutionModeTag::kPaper};

    // ---- 快速访问 ----
    [[nodiscard]] double pnl_net() const noexcept { return pnl_gross - pnl_fee; }
    [[nodiscard]] bool is_flat() const noexcept { return net_qty == 0.0; }

    // R-20: 4-ts 单调链验证 (event ≤ data_source ≤ ingestion ≤ as_of; 全 > 0)
    [[nodiscard]] bool ts_chain_ok() const noexcept {
        return event_ts_ns > 0 && data_source_ts_ns >= event_ts_ns && ingestion_ts_ns >= data_source_ts_ns &&
               as_of_ts_ns >= ingestion_ts_ns;
    }
};

static_assert(std::is_trivially_copyable_v<LedgerFeatures>,
              "LedgerFeatures must be trivially copyable for lock-free double-buffer");

// ---------------------------------------------------------------------------
// LedgerSnapshotHub — per-market double-buffer 快照发布/读取
//
// 线程安全: 单写多读 (SWMR)
//   - 写端 (RM / VirtualMatcher 线程, single writer): Publish()
//   - 读端 (任意线程, debug_api 等观测线程): Read() — 原子 acquire + value copy
//
// 容量: max_keys 在构造时预分配, hot path 不 grow
// key 语义: 由调用方决定 (condition_id / token_id / market_key 均可)
// ---------------------------------------------------------------------------
class LedgerSnapshotHub {
public:
    static constexpr std::size_t kDefaultMaxKeys = 512;

    explicit LedgerSnapshotHub(std::size_t max_keys = kDefaultMaxKeys);

    // Delete copy/move (状态机, 不允许拷贝)
    LedgerSnapshotHub(const LedgerSnapshotHub&) = delete;
    LedgerSnapshotHub& operator=(const LedgerSnapshotHub&) = delete;
    LedgerSnapshotHub(LedgerSnapshotHub&&) = delete;
    LedgerSnapshotHub& operator=(LedgerSnapshotHub&&) = delete;

    ~LedgerSnapshotHub() = default;

    // -----------------------------------------------------------------------
    // Publish — 写端 hot path (无锁, noexcept)
    //
    //   1. 找/分配 key 的内部 index (首次见到 market_key 时 cold-path 分配)
    //   2. 写入 back-buffer slot
    //   3. atomic store(release) 令读端可见
    //
    //   market_key: caller 保证生命周期 >= 本次调用 (string_view)
    //   features:   值拷贝进 back-buffer (trivially copyable POD)
    //
    // R-12: key_idx 已分配 → O(1) unordered_map lookup + atomic swap
    //        首次分配 (cold path) 可 malloc; hot path 后续无 malloc
    // -----------------------------------------------------------------------
    void Publish(std::string_view market_key, const LedgerFeatures& features) noexcept;

    // -----------------------------------------------------------------------
    // Read — 观测线程只读快照 (无锁, 线程安全)
    //
    //   返回 std::optional<LedgerFeatures>:
    //     - nullopt: market_key 未曾 Publish 过 (或 max_keys 超限)
    //     - value:   最新原子快照 (可能含 valid=false 的初始状态 — 调用方检查)
    //
    //   时延: atomic load + trivially copyable copy ≈ < 500ns (远低于 100us R-12 限制)
    // -----------------------------------------------------------------------
    [[nodiscard]] std::optional<LedgerFeatures> Read(std::string_view market_key) const noexcept;

    // -----------------------------------------------------------------------
    // ReadRaw — 零拷贝指针 (zero-copy, caller 必须在 front 有效期内使用)
    //   仅供高频内部使用; 一般消费方用 Read() 值语义
    // -----------------------------------------------------------------------
    [[nodiscard]] const LedgerFeatures* ReadRaw(std::string_view market_key) const noexcept;

    // -----------------------------------------------------------------------
    // ResetKey — 清除单 key 状态 (reconnect / position-close 后写端调用)
    // -----------------------------------------------------------------------
    void ResetKey(std::string_view market_key) noexcept;

    // -----------------------------------------------------------------------
    // ResetAll — 清除全部 key 状态 (系统重启 / paper 模式重置)
    // -----------------------------------------------------------------------
    void ResetAll() noexcept;

    // -----------------------------------------------------------------------
    // key_count — 当前已注册 key 数量 (供监控)
    // -----------------------------------------------------------------------
    [[nodiscard]] std::size_t key_count() const noexcept;

    // -----------------------------------------------------------------------
    // publish_count — 累计 Publish 调用次数 (供监控)
    // -----------------------------------------------------------------------
    [[nodiscard]] std::uint64_t publish_count() const noexcept { return publish_count_; }

private:
    // -----------------------------------------------------------------------
    // KeySlot — per-key double-buffer 存储
    //
    // buf_[0] / buf_[1]: 两个 LedgerFeatures 槽
    // front_: 当前读者可见的槽 index (0 or 1), atomic<uint8_t>
    //   - store: release (writer)
    //   - load:  acquire (reader)
    // -----------------------------------------------------------------------
    struct KeySlot {
        std::array<LedgerFeatures, 2> buf{};
        std::atomic<std::uint8_t> front{0};  // 0 or 1

        KeySlot() noexcept = default;
        KeySlot(KeySlot&&) = delete;
        KeySlot& operator=(KeySlot&&) = delete;
        KeySlot(const KeySlot&) = delete;
        KeySlot& operator=(const KeySlot&) = delete;
    };

    // market_key (string) → KeySlot index into slots_
    std::unordered_map<std::string, std::size_t> index_map_;

    // slots_ 用 unique_ptr<KeySlot[]> 避免 KeySlot 不可拷贝带来的 vector 问题
    std::unique_ptr<KeySlot[]> slots_;
    std::size_t max_keys_;
    std::size_t slot_count_{0};  // 写端 only

    std::uint64_t publish_count_{0};  // 写端 only

    static constexpr std::size_t kInvalidIdx = std::numeric_limits<std::size_t>::max();
    [[nodiscard]] std::size_t GetOrAllocSlot(std::string_view market_key) noexcept;
    [[nodiscard]] std::size_t FindSlot(std::string_view market_key) const noexcept;
};

// ---------------------------------------------------------------------------
// 映射说明 (LedgerFeatures → debug_api 类型; 集成步骤后续独立做, 不改 debug_api)
//
// 集成时 RealLedgerStateProvider 需完成以下映射:
//
// → debug_api::HoldingView (per-token; /api/v1/positions):
//   features.valid                 → 决定是否纳入 positions 列表
//   market_key (外部 key)          → HoldingView::market_id (condition_id 语义)
//   (外部 outcome 映射)            → HoldingView::outcome ("Yes"/"No"/球队名)
//   features.net_qty               → HoldingView::net_qty
//   features.avg_entry_price       → HoldingView::avg_entry_price
//   features.mark_price            → HoldingView::mark_price
//   features.pnl_realized          → HoldingView::pnl_realized
//   features.pnl_unrealized        → HoldingView::pnl_unrealized
//   features.as_of_ts_ns           → HoldingView::as_of_ts_ns
//
// → debug_api::PnlAttribution.per_market (聚合; /api/v1/pnl/attribution):
//   market_key                     → PnlPerMarket::market_id
//   features.pnl_net()             → PnlPerMarket::net_pnl
//   (汇总所有 key): gross/fee/net  → PnlAttribution::gross/fee/net
//   features.as_of_ts_ns (max)     → PnlAttribution::as_of_ts_ns
//
// R-11 映射:
//   features.mode == kPaper        → ExecMode::Paper (response 顶层 mode 字段)
//   features.mode == kLive         → ExecMode::Live
//   features.mode == kBacktest     → ExecMode::Backtest
// ---------------------------------------------------------------------------

}  // namespace stcpp::risk
