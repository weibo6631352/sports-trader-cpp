// stcpp/infra/wal/position_ledger.hpp — PositionLedger v0.1 主类
//
// 落:
//   老韩 W5 Smell-3 P0 升级 — 崩溃重启 circuit breaker 状态归零 + M4.5 gate 数据不完整
//   GM Wave 27 拍板 §P0: W6-A-position-wal owner 老王 + 老蒋
//   laozhou-architecture-v0.6-e2e.md §1 链路: VirtualFill→PL→WAL, §2.1 vCPU3
//   laowang-wal-framework-v0.2.md §3.3 (path prefix 硬校验), §4 (PIT)
//   laohan-riskmanager-design-v0.3.1.md (RM evaluate() 依赖 circuit_breaker_state)
//
// 红线:
//   R-1   PositionLedger.circuit_breaker_state() 是 RM evaluate() 依赖路径 — 不绕过 RM
//   R-7   paper/live/backtest 物理隔离: path 由 ExecutionMode 参数化 (build-time 决定)
//         paper  WAL → /var/lib/stcpp/paper/position*.wal
//         live   WAL → /var/lib/stcpp/live/position*.wal
//         实现: PositionLedger 构造参数 path_prefix, CMake 注入正确路径
//   R-11  paper position.wal 严禁出现在 live binary 路径; 由路径前缀硬校验 + CMake guard 保障
//   R-20  4 ts 全链路透传 (VirtualFill 携带, apply_fill 不替换任何 ts, 不本地 now())
//   老韩#3 启动期 restore_from_wal: replay 全量 WAL → 恢复 PositionState (circuit breaker 不归零)
//
// 设计约束:
//   - SPSC 写入路径 (apply_fill 是热路径, vCPU3 单线程写, 与老王 v0.2 group commit 兼容)
//   - 运行时不读 DB (老王铁律: DB 仅审计)
//   - apply_fill 不抛异常 (noexcept), 失败通过 ApplyResult 返回
//   - restore_from_wal 是启动期冷路径, 允许 throw
//
// 接入方:
//   - 小蒋 PaperSigner/VirtualMatcher → apply_fill(VirtualFill)
//   - 老韩 RM evaluate() → circuit_breaker_state()
//   - 小邓 ML hook → 旁路读 PositionRecord (最近一笔)
//   - 小董 M4.5 gate → restore_from_wal 历史
//
// 不耻下问:
//   VirtualFill 字段名 @小蒋 (见 virtual_matcher.hpp)
//   RM evaluate 回调字段 @老韩
//   WAL path CMake 注入 @老高

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include "stcpp/execution/execution_mode.hpp"
#include "stcpp/execution/virtual_matcher.hpp"
#include "stcpp/infra/wal/position_record.hpp"
#include "stcpp/infra/wal/wal_error.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/infra/wal/wal_writer.hpp"

namespace stcpp::infra::wal {

// ---------------------------------------------------------------------------
// PositionState — 单 market 运行时内存状态 (非持久化, replay 后重建)
//
// 不落磁盘; WAL replay 后由 PositionRecord 序列重建.
// ---------------------------------------------------------------------------
struct PositionState {
    std::array<char, 32> market_id{};
    std::uint8_t outcome{0};  // 0=YES 1=NO

    std::int64_t position_total{0};  // USDC * 1e6
    std::int64_t realized_pnl{0};
    std::int64_t unrealized_pnl{0};
    std::int64_t entry_avg_price_micro{0};  // * 1e6
    std::int64_t bankroll_total{0};

    std::int32_t consec_loss_count{0};
    std::int32_t exposure_pct{0};  // basis points

    // 最近一笔 record 的 4 ts (用于 ML hook + M4.5 读取)
    std::int64_t last_event_ts_ns{0};
    std::int64_t last_as_of_ts_ns{0};

    // 最近 audit_id
    std::array<std::uint8_t, 16> last_audit_id{};
};

// ---------------------------------------------------------------------------
// CircuitBreakerState — RM evaluate() 依赖 (R-1)
//
// circuit_breaker_state() 返回此结构, RM 判断是否触发 circuit breaker.
// ---------------------------------------------------------------------------
struct CircuitBreakerState {
    std::int64_t bankroll_total{0};     // USDC * 1e6
    std::int32_t consec_loss_count{0};  // 连续亏损笔数
    std::int32_t exposure_pct{0};       // basis points (当前敞口占比)
};

// ---------------------------------------------------------------------------
// ApplyResult — apply_fill 返回值 (noexcept 路径)
// ---------------------------------------------------------------------------
enum class ApplyStatus : std::uint8_t {
    Ok = 0,
    PitViolation = 1,     // 4 ts 不等式违反 (R-20)
    WalBackpressure = 2,  // WAL ring 满 (老韩 #18)
    WalFailed = 3,        // WAL writer 已失败 (fsync error)
    InvalidFill = 4,      // fill 字段非法 (size=0 且非 reject)
};

struct ApplyResult {
    ApplyStatus status{ApplyStatus::Ok};
    PositionRecord record{};   // 已写入 WAL 的 record (status=Ok 时有效)
    std::uint64_t wal_seq{0};  // WAL sequence number
};

// ---------------------------------------------------------------------------
// IWalWriterForPosition — 测试用接口 (让单测可注入 mock WAL writer)
//
// 生产代码走 WalWriter<PositionRecord>; 单测注入 InMemoryPositionWal.
// ---------------------------------------------------------------------------
class IWalWriterForPosition {
public:
    virtual ~IWalWriterForPosition() = default;

    [[nodiscard]] virtual WalResult<std::uint64_t> Append(const PositionRecord& rec) noexcept = 0;

    [[nodiscard]] virtual std::uint64_t HighWatermark() const noexcept = 0;
    [[nodiscard]] virtual bool IsFailed() const noexcept = 0;
};

// ---------------------------------------------------------------------------
// PositionLedger — 主类
//
// 线程模型: apply_fill 只能从 vCPU3 (单生产者) 调用; query_position /
//   circuit_breaker_state 可从其他线程读 (读锁保护).
//   restore_from_wal 只在启动期 (main thread) 调用, 之后不再调.
// ---------------------------------------------------------------------------
class PositionLedger {
public:
    // -- 构造 (生产路径) -------------------------------------------------------
    //
    // path_prefix: WAL 文件路径前缀, 必须以 WalKind::Position 的 PathRootOf 开头
    //   (Open() 内 WalWriter::Open 会做 R-11 硬校验, 不命中 → std::abort).
    //   paper  例: "/var/lib/stcpp/paper/position"
    //   live   例: "/var/lib/stcpp/live/position"
    //
    // init_bankroll: 启动时初始总资金 (USDC * 1e6); restore_from_wal 后会被 WAL 最新值覆盖.
    //
    // 注: 当前 WalKind::Position PathRootOf = "/var/lib/stcpp/exec/"
    //   R-7 paper/live 隔离通过 path_prefix 参数 + CMake 注入实现.
    //   CMake 会根据 STCPP_EXEC_MODE 选择正确的 path_prefix (详见 CMakeLists.txt guard).
    explicit PositionLedger(std::string_view path_prefix, std::int64_t init_bankroll);

    // -- 测试专用构造 (注入 mock WAL writer) -----------------------------------
    // 不做路径校验, 用于单测 T1-T6.
    explicit PositionLedger(std::unique_ptr<IWalWriterForPosition> mock_writer, std::int64_t init_bankroll);

    ~PositionLedger() = default;

    PositionLedger(const PositionLedger&) = delete;
    PositionLedger& operator=(const PositionLedger&) = delete;
    PositionLedger(PositionLedger&&) = delete;
    PositionLedger& operator=(PositionLedger&&) = delete;

    // -- 热路径 write (vCPU3 SPSC 单写) ---------------------------------------

    // apply_fill: 消费 VirtualFill, 更新内存 PositionState, 写 WAL.
    //
    // 4 ts 透传规则 (R-20): 直接从 VirtualFill 取 ts, 不本地 now().
    //   event_ts_ns       = fill.fill_ts_ns (fill 发生时间, 最接近"事件")
    //   data_source_ts_ns = fill.data_source_ts_ns
    //   ingestion_ts_ns   = fill.ingestion_ts_ns
    //   as_of_ts_ns       = fill.as_of_ts_ns
    //
    // noexcept: WAL 失败通过 ApplyResult.status 返回, 不抛.
    // SPSC 保证: 只有 vCPU3 调用此方法 (apply_fill + WAL 在同一线程).
    [[nodiscard]] ApplyResult apply_fill(const stcpp::execution::VirtualFill& fill) noexcept;

    // -- 启动期 (冷路径) replay -----------------------------------------------

    // restore_from_wal: 读 WAL 文件目录, 顺序 replay 所有 PositionRecord,
    //   重建内存 PositionState (circuit breaker 状态完全恢复, 老韩 #3 P0 核心).
    //
    // 参数 wal_dir: WAL 文件目录 (遍历 position*.wal 文件, 按 segment 序号排序).
    // 返回: 恢复的 market 数量.
    // 失败: 抛 std::runtime_error (启动期, 允许 throw + 向上传播).
    //
    // replay 后 PositionState 完全等价于崩溃前最后一次 apply_fill 的结果.
    std::size_t restore_from_wal(const std::filesystem::path& wal_dir);

    // -- 读路径 (多线程安全, 共享锁) ------------------------------------------

    // query_position: 按 market_id 查当前 PositionState.
    // 返回: 找不到则返回空 PositionState (market_id 全零).
    [[nodiscard]] PositionState query_position(std::string_view market_id) const;

    // circuit_breaker_state: RM evaluate() 调用入口 (R-1).
    // 返回全局聚合 circuit breaker 状态 (汇总所有 market 的 consec_loss + exposure).
    [[nodiscard]] CircuitBreakerState circuit_breaker_state() const noexcept;

    // -- 辅助读 ---------------------------------------------------------------

    // last_record: 最近写入的 PositionRecord (ML hook / M4.5 旁路读).
    // 返回: apply_fill 成功后的最新 record; 若未 apply 过则返回 zero record.
    [[nodiscard]] PositionRecord last_record() const noexcept;

    // record_count: 已 apply 的总笔数 (含 replay 恢复的).
    [[nodiscard]] std::uint64_t record_count() const noexcept {
        return record_count_.load(std::memory_order_acquire);
    }

private:
    // -- 内部辅助 -------------------------------------------------------------

    // _update_state: 根据 PositionRecord 更新内存 PositionState.
    // 由 apply_fill (写路径) 和 restore_from_wal (replay 路径) 共用.
    // 调用方已持有 state_mutex_ (write) 或确保单线程.
    void _update_state(const PositionRecord& rec);

    // _build_record: 从 VirtualFill 构建 PositionRecord.
    // 调用方已持有 state_mutex_ (write).
    PositionRecord _build_record(const stcpp::execution::VirtualFill& fill) noexcept;

    // -- 内存状态 (读写锁) ----------------------------------------------------
    mutable std::mutex state_mutex_;
    std::unordered_map<std::string, PositionState> states_;  // key=market_id string
    PositionRecord last_record_{};

    // -- 全局聚合 circuit breaker 状态 (原子读, RM 热路径) --------------------
    // consec_loss_count: 取所有 market 中最大值 (最保守)
    // exposure_pct: 所有 market 敞口之和 (basis points)
    std::atomic<std::int64_t> global_bankroll_{0};
    std::atomic<std::int32_t> global_consec_loss_{0};
    std::atomic<std::int32_t> global_exposure_pct_{0};

    // -- WAL writer (持有权) --------------------------------------------------
    std::unique_ptr<IWalWriterForPosition> writer_;

    // -- 计数 -----------------------------------------------------------------
    std::atomic<std::uint64_t> record_count_{0};
};

// ---------------------------------------------------------------------------
// RealWalWriter — 生产路径 IWalWriterForPosition 适配器
//
// 封装 WalWriter<PositionRecord>, 实现 IWalWriterForPosition 接口.
// 单测中被 InMemoryPositionWal 替换.
// ---------------------------------------------------------------------------
class RealWalWriter final : public IWalWriterForPosition {
public:
    explicit RealWalWriter(std::unique_ptr<WalWriter<PositionRecord>> w) : writer_(std::move(w)) {}

    [[nodiscard]] WalResult<std::uint64_t> Append(const PositionRecord& rec) noexcept override {
        return writer_->Append(rec);
    }

    [[nodiscard]] std::uint64_t HighWatermark() const noexcept override { return writer_->HighWatermark(); }
    [[nodiscard]] bool IsFailed() const noexcept override { return writer_->IsFailed(); }

private:
    std::unique_ptr<WalWriter<PositionRecord>> writer_;
};

}  // namespace stcpp::infra::wal
