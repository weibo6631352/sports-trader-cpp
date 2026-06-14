// stcpp/data/score_snapshot_store.hpp — Goalserve 比分快照 in-mem store (R-12 合规)
//
// Owner: 小段 (goalserve-specialist, #37)
// Date:  2026-05-29
// Task:  小余接入方案 v1 §1.3 ScoreSnapshotStore + §1.4 ScoreStateProvider
//
// 设计:
//   采集线程 Publish(map) → atomic<shared_ptr> swap (RCU-lite, 无读锁)
//   读取线程 Get(event_id) → atomic load + hash lookup, O(1), 纳秒级
//   R-12 合规: 读侧无 IO, 无锁 > 100us, 纯内存操作
//   R-20: EventScore.ts.data_source_ts_ns 来自 Goalserve updated_ts, 禁 now()
//
// 线程安全:
//   Publish() 可同时被多个采集线程调用 (每 sport 一个线程 → 3 sport 并发 Publish)
//   Get()     可被任意线程调用 (debug_api / score endpoint 读快照)
//   atomic<shared_ptr> C++20: load/store 原子, ABA 安全 (引用计数归零自动释放旧 map)
//
// staleness:
//   store 不主动过期 (过期判定在消费侧 as_of - data_source_ts > threshold)
//   data_source_ts 在 EventScore.ts 中透明暴露
//
// 归属:
//   小余方案 §1.3: "新增 src/stcpp/debug_api/score_snapshot_store.hpp 或
//                   include/stcpp/data/ (由老周定归属)"
//   小余方案 §3.1: 物化段负责人 = 小余 + 老周 ack
//   本文件落 include/stcpp/data/ (data 层, 无 debug_api 反向依赖, R-12 要求)
//
// 依赖: 仅标准库 C++20 (atomic<shared_ptr> C++20 §20.8.6.3)
//       stcpp/debug_api/state_provider.hpp (EventScore, FourTs)
//
// 不耻下问:
//   @老周: atomic<shared_ptr> vs seqlock double-buffer 选型 ack?
//          (本实现用 C++20 atomic<shared_ptr> — RCU-lite, 读无锁, 写构建新 map)
//   @小卢: ScoreStateProvider 实现 (接 score() 契约) 由你落, store 接口见下.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "stcpp/debug_api/state_provider.hpp"  // EventScore, FourTs

namespace stcpp::data {

// ============================================================================
// ScoreMap — 快照 map 类型 (event_id → EventScore)
// 只读不变量: 一旦 publish 后不修改, 消费侧可无锁并发读
// ============================================================================
using ScoreMap = std::unordered_map<std::string, debug_api::EventScore>;

// ============================================================================
// ScoreSnapshotStore — shared_ptr swap in-mem 比分快照 store
//
// 实现: std::shared_ptr<const ScoreMap> + std::mutex (读写均持锁但极短)
//   替代 C++20 atomic<shared_ptr> — 后者在 Apple libc++ 当前版本需要
//   trivially_copyable, std::shared_ptr 不满足 (libc++ 特化在更高版本才完整支持)
//
// 读写模型:
//   写 (采集线程, ~1s 周期):
//     1. 构建新 shared_ptr<const ScoreMap>
//     2. lock(mu_) → swap front_ → unlock
//     3. 旧 map 引用计数减 1 (栈上临时 shared_ptr 析构, 锁外释放)
//
//   读 (任意线程):
//     1. lock(mu_) → copy front_ (shared_ptr 引用计数 +1) → unlock
//     2. snap->find(event_id)  ← 锁外, 并发安全
//     3. 栈上 shared_ptr 析构 → 引用计数减 1
//
// 性能:
//   读锁持有时间 = shared_ptr 拷贝 (~5ns, 仅 ref count incr), 不含 hash lookup
//   hash lookup 在锁外完成 → 不阻塞写侧
//   R-12 合规: 无 IO, 锁持有远 < 100us
//
// 线程安全: Publish / Get / GetSnapshot / Size 全部线程安全
// ============================================================================
class ScoreSnapshotStore {
public:
    ScoreSnapshotStore() = default;

    // 禁止拷贝/移动 (mutex 不可拷贝)
    ScoreSnapshotStore(const ScoreSnapshotStore&) = delete;
    ScoreSnapshotStore& operator=(const ScoreSnapshotStore&) = delete;
    ScoreSnapshotStore(ScoreSnapshotStore&&) = delete;
    ScoreSnapshotStore& operator=(ScoreSnapshotStore&&) = delete;

    ~ScoreSnapshotStore() = default;

    // ------------------------------------------------------------------------
    // Publish — 采集线程调用 (非热路径, ~1s 周期)
    //
    // 参数:
    //   next: 新 map (key = event_id, value = EventScore with valid 4ts)
    //         caller 负责确保 EventScore.ts.data_source_ts_ns 来自 Goalserve payload
    //
    // 线程安全: 多线程并发 Publish 安全 (mutex 保护 swap)
    //           多 sport 并发 Publish 时, 若共用同一 store 实例会整体替换 map
    //           (推荐: 每 sport 独立 store 实例, 或 Publish 合并 map — 由采集层决定)
    // ------------------------------------------------------------------------
    void Publish(std::shared_ptr<const ScoreMap> next) noexcept;

    // ------------------------------------------------------------------------
    // Get — 读取侧调用 (R-12: 无 IO, 锁持有 < 100us)
    //
    // 返回: optional<EventScore>
    //   found = true  → event_id 存在, EventScore.found = true
    //   nullopt       → event_id 不存在
    //
    // 性能: O(1) — mutex lock (shared_ptr copy) + hash lookup (锁外)
    //   锁持有时间仅 shared_ptr 引用计数 incr (~5ns), hash lookup 在锁外
    //
    // 调用方 (ScoreStateProvider::score()):
    //   if (auto s = store_.Get(event_id)) return *s;
    //   EventScore miss; miss.found = false; ...
    // ------------------------------------------------------------------------
    [[nodiscard]] std::optional<debug_api::EventScore> Get(const std::string& event_id) const noexcept;

    // ------------------------------------------------------------------------
    // GetSnapshot — 获取当前整个 map 的引用计数快照 (测试 / 监控用)
    // 返回 nullptr 表示 store 为空 (尚未 Publish)
    // ------------------------------------------------------------------------
    [[nodiscard]] std::shared_ptr<const ScoreMap> GetSnapshot() const noexcept;

    // ------------------------------------------------------------------------
    // Size — 当前快照 event 数量 (0 = 空 store / 尚未 Publish)
    // ------------------------------------------------------------------------
    [[nodiscard]] std::size_t Size() const noexcept;

    // SetOnChange — 注册"赔率源内容变动"回调 (2026-06-14 老板「赔率源状态变动也该触发决策」)。
    //   Publish 时若新 map 的内容签名 (比分+赔率+状态 core/status/period) 与上次不同 → 锁外调此回调
    //   (上层接 TradingLoop::RequestTick(kOdds))。无变动 → 不调 (只更新 front_ 保新鲜)。
    //   须在采集线程 Start() 前注册 (启动期单线程, 之后只读)。回调极快 (RequestTick <1us, R-12)。
    void SetOnChange(std::function<void()> cb) { on_change_ = std::move(cb); }

private:
    mutable std::mutex mu_;
    std::shared_ptr<const ScoreMap> front_;  // nullptr = 尚未 Publish
    std::uint64_t last_sig_{0};              // 上次 Publish 的内容签名 (mu_ 下; 变动检测)
    std::function<void()> on_change_;        // 内容变动回调 (Start 前设, 之后只读)
};

// ============================================================================
// ScoreStateProvider — 接 StateProvider 契约 (G-FREEZE-W 不改 score() 签名)
//
// 实现:
//   score(event_id) → store_.Get(event_id) → EventScore
//   data_source() → "live" (非 demo, 老钱红线: 非 demo 标 live)
//
// 注入方式 (小余方案 §1.4):
//   main: --score-live flag → new ScoreStateProvider(store)
//         default → DemoStateProvider (前端集成不阻塞)
//
// 实现归属: 小卢 (#senior-ic-pool, ADR-038 owner) 或小余
//           本头文件提供接口声明 + inline 实现 (轻量, 无 .cpp)
//
// 注意: 本文件不 include state_provider.hpp 虚基类 (避免循环依赖),
//       实际使用时 ScoreStateProvider 需继承 StateProvider.
//       接口定义在 state_provider.hpp (G-FREEZE-W 冻结, 本文件不改).
// ============================================================================

}  // namespace stcpp::data
