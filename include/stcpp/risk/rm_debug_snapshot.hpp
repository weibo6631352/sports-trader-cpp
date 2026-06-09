// include/stcpp/risk/rm_debug_snapshot.hpp — RM 拒单只读 seq-counter ring
//
// Owner: 老沈 (B 风控合规部, RM 实施)
// 派单: 老韩 D1 + 老郭 R-12 裁定
// last_review: 2026-05-29
//
// 设计概述 (老郭 R-12 裁定):
//   单 writer (RM 线程) × 多 reader (debug_api 线程)
//   固定容量 lock-free ring + per-slot seq-counter
//
//   Writer 路径 (push_reject):
//     1. 原子递增 write_idx_ (fetch_add) → 得到写槽 idx = raw % cap
//     2. slots_[idx].seq.store(WRITING_SENTINEL)  → 标记写中
//     3. 写入 slots_[idx].row
//     4. slots_[idx].seq.store(raw + 1)           → 标记完成 (= 生成序号)
//     无堆分配, 无互斥锁, p99 目标 < 1us
//
//   Reader 路径 (snapshot):
//     遍历 slots_[0..cap-1], 按 seq 判断有效性:
//       seq == 0 → 尚未写入, 跳过
//       seq == WRITING_SENTINEL → 正在写, 跳过
//       seq > 0  → 有效, 拷贝 row
//     ring 满时: 保留所有 cap 行 (按写序排序)
//     无持锁 > 100us (R-12)
//
//   Consistency: 每个 slot 独立 seq-counter; 读到 WRITING_SENTINEL 时跳过
//     (观测可短暂缺该行; 严格一致由 audit WAL 保证)
//
// 黑名单字段 (小白 allowlist, 编译期物理不存在于 RejectRow):
//   × 私钥字节       × 签名字节 (sig[])      × nonce 原值
//   × timestamp_ms 原值   × order_id (CLOB)   × token_id 全值
//   条件: condition_id 为公开协议字段, token_id 不直接出
//
// 安全不变量 (compile-time static_assert):
//   sizeof(RejectRow) <= 256
//   RejectRow is_trivially_copyable (无堆数据)
//   字段白名单: reason_code / market_id / intent_ref / side /
//               size_usdc / price / rejected_ts_ns
//
// 红线:
//   R-12: snapshot() 无持锁 > 100us; 无反向调 RM/signer
//   R-1:  push_reject 在 evaluate() REJECTED 确认后调用, 不影响拒单逻辑
//   p99 增量: push_reject() < 1us
//   fail-open: ring 满覆盖最旧; 不反压热路径
//
// ABI 影响: 本文件纯新增头文件
//   risk_gateway.hpp 仅新增 attach_debug_snapshot() 方法 (非虚, RiskGateway sizeof 不变)
//   OrderIntent/RiskDecision/AuditRecord 字段集不变
//   需老韩+老孙确认: pImpl 内新增 atomic<RmDebugSnapshot*> (sizeof(State_) 变化 ok)
//
// 依赖方向 (R-12):
//   risk 侧 → rm_debug_snapshot.hpp (header-only)
//   debug_api 侧 → 持 const RmDebugSnapshot* 读 snapshot (单向消费)
//
// cite: docs/ADR/2026-05-29-observability-debug-api.md §2 §5
//       docs/RESEARCH/xiaobai-observability-api-security-v1.md §1 §3

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "stcpp/risk/reject_enum.hpp"

namespace stcpp::risk {

// ============================================================================
// §1  RejectRow — RM 拒单只读投影 POD
//
// 字段集 = debug_api RiskRejectRow allowlist (小白 §1)
// 黑名单: 私钥/签名/nonce/timestamp_ms/order_id/token_id 全值 物理不在此 struct
// ============================================================================
struct RejectRow {
    // reason_code: RejectCode 字符串 (e.g. "EXCEED_PER_ORDER_CAP")
    // 不含 sub_reason (细节保留在 audit WAL)
    char reason_code[48];  // 最长 RejectCode ~32 char; 留余量

    // market_id: condition_id 映射 (公开协议字段; token_id 全值不直接出)
    char market_id[72];  // bytes32 hex = 0x+64char=66char; 留余量

    // intent_ref: audit_id 前 8 字节 hex (内部引用, 非签名/order_id 派生)
    char intent_ref[20];  // 8 byte × 2 hex char = 16 char; 留余量

    // side: "BUY" / "SELL"
    char side[8];

    // size_usdc: pUSD micro → USDC double
    double size_usdc{0.0};

    // price: 被拒订单报价 ∈ (0, 1)
    double price{0.0};

    // rejected_ts_ns: 拒单时刻 epoch ns (R-20: 来自 RiskDecision.decision_ts_ns)
    std::int64_t rejected_ts_ns{0};

    // sub_reason_code: INVALID_INTENT 细分码 (InvalidIntentSubReason 的 uint8 值; 0=NONE).
    //   仅 reason_code==INVALID_INTENT 时有意义 (老韩 invariant: 其余 code ⟹ NONE).
    //   2026-06-10 观测缺口修复: 让 /api/v1/risk/rejects 能自诊断 INVALID_INTENT 根因.
    std::uint8_t sub_reason_code{0};
};

// 编译期安全守护
static_assert(sizeof(RejectRow) <= 256,
              "RejectRow must be compact (<= 256 bytes); "
              "verify no secret field was accidentally added");
static_assert(std::is_trivially_copyable_v<RejectRow>,
              "RejectRow must be trivially copyable (no heap members)");

// ============================================================================
// §2  reject_code_to_str — RejectCode → C 字符串 (inline)
// ============================================================================
[[nodiscard]] inline const char* reject_code_to_str(RejectCode code) noexcept {
    switch (code) {
        case RejectCode::STATE_HALTED:
            return "STATE_HALTED";
        case RejectCode::STATE_DRAIN:
            return "STATE_DRAIN";
        case RejectCode::STATE_SAFE_MODE:
            return "STATE_SAFE_MODE";
        case RejectCode::DUPLICATE_INTENT:
            return "DUPLICATE_INTENT";
        case RejectCode::STALE_DATA:
            return "STALE_DATA";
        case RejectCode::INVALID_INTENT:
            return "INVALID_INTENT";
        case RejectCode::EXCEED_PER_ORDER_CAP:
            return "EXCEED_PER_ORDER_CAP";
        case RejectCode::EXCEED_CONDITION_EXPOSURE:
            return "EXCEED_CONDITION_EXPOSURE";
        case RejectCode::DAILY_LOSS_HALT:
            return "DAILY_LOSS_HALT";
        case RejectCode::CONSEC_LOSS_HALT:
            return "CONSEC_LOSS_HALT";
        case RejectCode::INSUFFICIENT_BANKROLL:
            return "INSUFFICIENT_BANKROLL";
        case RejectCode::EDGE_CI_NEGATIVE:
            return "EDGE_CI_NEGATIVE";
        case RejectCode::EDGE_NEGATED_BY_SLIPPAGE:
            return "EDGE_NEGATED_BY_SLIPPAGE";
        case RejectCode::MARKET_TYPE_NOT_ENABLED:
            return "MARKET_TYPE_NOT_ENABLED";
        case RejectCode::MARKET_NOT_ACTIVE:
            return "MARKET_NOT_ACTIVE";
        case RejectCode::LOW_FILL_RATE:
            return "LOW_FILL_RATE";
        case RejectCode::EXCESSIVE_SLIPPAGE:
            return "EXCESSIVE_SLIPPAGE";
        case RejectCode::EXCEED_BOOK_DEPTH:
            return "EXCEED_BOOK_DEPTH";
        case RejectCode::AUDIT_WAL_BACKPRESSURE:
            return "AUDIT_WAL_BACKPRESSURE";
        case RejectCode::STRATEGY_DECAYED:
            return "STRATEGY_DECAYED";
        case RejectCode::INTERNAL_ERROR:
            return "INTERNAL_ERROR";
        case RejectCode::EXCEED_PER_OUTCOME_CAP:
            return "EXCEED_PER_OUTCOME_CAP";
        case RejectCode::EXCEED_EVENT_EXPOSURE:
            return "EXCEED_EVENT_EXPOSURE";
        default:
            return "UNKNOWN";
    }
}

// ============================================================================
// §3  detail namespace — 安全字符串工具
// ============================================================================
namespace detail {

// 固定长度安全拷贝 (dst 末尾保证 NUL)
inline void safe_copy_cstr(char* dst, std::size_t dst_size, const char* src) noexcept {
    if (dst_size == 0 || src == nullptr) {
        return;
    }
    std::size_t i = 0;
    while (i + 1 < dst_size && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

inline void safe_copy_str(char* dst, std::size_t dst_size, const std::string& src) noexcept {
    safe_copy_cstr(dst, dst_size, src.c_str());
}

// audit_id 前 8 字节 → 16 char hex (intent_ref)
inline void audit_id_to_hex(char* dst, std::size_t dst_size,
                            std::array<std::uint8_t, 16> const& id) noexcept {
    static constexpr char kHex[] = "0123456789abcdef";
    std::size_t out_len = 0;
    for (std::size_t i = 0; i < 8u && out_len + 2u < dst_size; ++i) {
        dst[out_len++] = kHex[(id[i] >> 4u) & 0xFu];
        dst[out_len++] = kHex[id[i] & 0xFu];
    }
    if (out_len < dst_size) {
        dst[out_len] = '\0';
    }
}

}  // namespace detail

// ============================================================================
// §4  build_reject_row — 投影 RiskDecision + OrderIntent → RejectRow
//
// 模板参数避免直接 #include risk_gateway.hpp (防循环依赖)
// 安全: 只读 allowlist 字段 (condition_id/audit_id/side/size/price/decision_ts_ns)
//       不读: token_id / timestamp_ms / metadata / builder / nonce
// ============================================================================
template <typename OI, typename RD>
[[nodiscard]] inline RejectRow build_reject_row(RD const& decision, OI const& intent) noexcept {
    RejectRow row{};

    detail::safe_copy_cstr(row.reason_code, sizeof(row.reason_code), reject_code_to_str(decision.reject));

    // market_id = condition_id (公开协议字段; 非 token_id)
    detail::safe_copy_str(row.market_id, sizeof(row.market_id), intent.condition_id);

    // intent_ref = audit_id 前 8 字节 hex
    detail::audit_id_to_hex(row.intent_ref, sizeof(row.intent_ref), decision.audit_id);

    // side: Buy=0, Sell=1 (ABI lock v1.7)
    if (static_cast<std::uint8_t>(intent.side) == 0u) {
        detail::safe_copy_cstr(row.side, sizeof(row.side), "BUY");
    } else {
        detail::safe_copy_cstr(row.side, sizeof(row.side), "SELL");
    }

    // size_usdc: micro pUSD → USDC double
    row.size_usdc = static_cast<double>(intent.size_pUSD_micro) / 1'000'000.0;

    // price
    row.price = intent.price;

    // rejected_ts_ns (R-20: 来自 decision.decision_ts_ns)
    row.rejected_ts_ns = decision.decision_ts_ns;

    // sub_reason_code: INVALID_INTENT 细分码投影 (2026-06-10 观测缺口修复).
    row.sub_reason_code = static_cast<std::uint8_t>(decision.sub_reason);

    return row;
}

// ============================================================================
// §5  RmDebugSnapshot — lock-free ring with per-slot seq-counter
//
// 并发模型: 单 writer / 多 reader; 无锁
//
// Slot 设计:
//   struct Slot { atomic<uint64_t> seq; RejectRow row; }
//   seq == 0          → 未初始化
//   seq == WRITING    → writer 写中 (reader 跳过)
//   seq == write_gen  → 完成, write_gen = raw_write_idx + 1 (> 0, != WRITING)
//
// Writer (push_reject):
//   raw = write_idx_.fetch_add(1) [relaxed]
//   idx = raw % cap
//   slots_[idx].seq.store(WRITING, release)
//   slots_[idx].row = row  (trivially copyable assignment)
//   slots_[idx].seq.store(raw + 1, release)  [完成; raw+1 >= 1]
//   注: WRITING = UINT64_MAX (不与任何有效 raw+1 冲突: raw+1 <= UINT64_MAX-1)
//
// Reader (snapshot):
//   无锁遍历 slots_[0..cap-1]:
//     seq = slot.seq.load(acquire)
//     if seq == 0 || seq == WRITING → 跳过
//     else → 拷贝 row, 再次读 seq 确认不变 (ABA 防护)
//   ring 满时: 按 seq 排序, 保留最新 cap 行
//
// 容量: kRmSnapshotCapacity = 256 (编译期常量)
// 性能预算: push_reject p99 < 1us (2× atomic store + trivial copy)
// ============================================================================

inline constexpr std::size_t kRmSnapshotCapacity = 256;
inline constexpr std::uint64_t kSlotWriting = static_cast<std::uint64_t>(-1);  // UINT64_MAX

class RmDebugSnapshot {
public:
    RmDebugSnapshot() noexcept {
        for (auto& slot : slots_) {
            slot.seq.store(0u, std::memory_order_relaxed);
            slot.row = RejectRow{};  // 值初始化 (RejectRow 非平凡; gcc -Wclass-memaccess 禁 memset)
        }
        write_idx_.store(0u, std::memory_order_relaxed);
    }

    RmDebugSnapshot(RmDebugSnapshot const&) = delete;
    RmDebugSnapshot& operator=(RmDebugSnapshot const&) = delete;
    RmDebugSnapshot(RmDebugSnapshot&&) = delete;
    RmDebugSnapshot& operator=(RmDebugSnapshot&&) = delete;
    ~RmDebugSnapshot() = default;

    // ---- Writer API (RM 线程专用; 单 writer) --------------------------------

    // push_reject: p99 < 1us; 无锁; 无堆分配; ring 满覆盖最旧
    void push_reject(RejectRow const& row) noexcept {
        // 原子分配写槽 (raw = 0, 1, 2, ...; 单调递增)
        std::uint64_t const raw = write_idx_.fetch_add(1u, std::memory_order_relaxed);
        std::size_t const idx = static_cast<std::size_t>(raw % kRmSnapshotCapacity);

        Slot& slot = slots_[idx];

        // Step 1: 标记写中
        slot.seq.store(kSlotWriting, std::memory_order_release);

        // Step 2: 写入 row (trivially copyable)
        slot.row = row;

        // Step 3: 标记完成 (seq = raw + 1; reader 据此判断新旧)
        slot.seq.store(raw + 1u, std::memory_order_release);
    }

    // ---- Reader API (debug_api; const; 多 reader 安全) ----------------------

    // snapshot(): 返回当前所有有效行 (按写序从旧到新)
    //   无锁; 跳过 WRITING 槽 (观测容忍短暂缺行)
    //   ring 满时: 返回 cap 行 (去掉已被覆盖的老行)
    [[nodiscard]] std::vector<RejectRow> snapshot() const noexcept {
        struct Entry {
            std::uint64_t gen;  // slot.seq at time of read (用于排序 + 新旧判断)
            RejectRow row;
        };

        std::vector<Entry> valid;
        valid.reserve(kRmSnapshotCapacity);

        std::uint64_t const current_raw = write_idx_.load(std::memory_order_acquire);

        for (std::size_t i = 0; i < kRmSnapshotCapacity; ++i) {
            Slot const& slot = slots_[i];

            // 第一次读 seq
            std::uint64_t seq0 = slot.seq.load(std::memory_order_acquire);

            // 跳过: 未初始化 或 写中
            if (seq0 == 0u || seq0 == kSlotWriting) {
                continue;
            }

            // 拷贝 row
            RejectRow row = slot.row;

            // 第二次读 seq (ABA 防护: 若 seq 已变说明 slot 被覆盖)
            std::uint64_t seq1 = slot.seq.load(std::memory_order_acquire);
            if (seq1 != seq0 || seq1 == kSlotWriting) {
                // slot 在读取中途被覆盖, 跳过 (观测容忍)
                continue;
            }

            // gen = seq0 = raw_write_idx + 1 → raw = seq0 - 1
            // 当 ring 满时 (current_raw > cap): 若 raw < current_raw - cap → 已被覆盖
            if (current_raw > kRmSnapshotCapacity) {
                std::uint64_t const raw_of_slot = seq0 - 1u;
                if (raw_of_slot + kRmSnapshotCapacity <= current_raw - 1u) {
                    // 该 slot 对应的写轮已被覆盖 (下一次写会再次覆盖此槽)
                    continue;
                }
            }

            valid.push_back({seq0, row});
        }

        // 按 gen 从小到大排序 (写序: 越小越旧)
        std::sort(valid.begin(), valid.end(), [](Entry const& a, Entry const& b) { return a.gen < b.gen; });

        std::vector<RejectRow> out;
        out.reserve(valid.size());
        for (auto const& e : valid) {
            out.push_back(e.row);
        }
        return out;
    }

    // count(): 总写入次数 (单测用)
    [[nodiscard]] std::uint64_t count() const noexcept { return write_idx_.load(std::memory_order_acquire); }

private:
    // Slot: 对齐 64 字节 (cache-line) 减少 false sharing
    struct alignas(64) Slot {
        std::atomic<std::uint64_t> seq{0};
        RejectRow row{};
        // padding: 确保 Slot 至少 64 字节 (sizeof(atomic<uint64>) + sizeof(RejectRow))
        // sizeof(RejectRow) = 48+72+20+8+8+8+8 = 172 bytes → Slot > 64B → alignas 足够
    };

    alignas(64) std::array<Slot, kRmSnapshotCapacity> slots_{};
    alignas(64) std::atomic<std::uint64_t> write_idx_{0};
};

// 静态验证 RmDebugSnapshot 可在 pImpl 内使用
static_assert(sizeof(RmDebugSnapshot) > 0, "RmDebugSnapshot must be complete type");

// ============================================================================
// §6  全局 attach/detach — 进程级单例 hook (risk_gateway.cpp 实现)
//
// 设计: risk_gateway.hpp 不修改 (避免 ABI 锁定触发)
//   attach_rm_debug_snapshot(snap): 进程内全局注入 RmDebugSnapshot*
//     RiskGateway::evaluate() 内部 load 此全局指针
//     线程安全: atomic store (release) / atomic load (acquire)
//     调用约定: start-up 阶段注入 (单次; snap 生命周期须长于 RiskGateway)
//
//   detach_rm_debug_snapshot(): 注销 (nullptr → 禁用快照)
//
// 限制: 进程内最多一个 RmDebugSnapshot (单例语义)
//   多 RiskGateway 实例共用同一快照 ring
//   生产场景: 每进程一个 RiskGateway + 一个 snapshot (满足)
//
// 安全: attach/detach 只允许在初始化阶段调用, 不在热路径调用
//   (atomic ptr 写操作对 evaluate() 的 acquire-load 可见)
// ============================================================================

// 声明: 实现在 risk_gateway.cpp (链接 stcpp_risk 即可)
// (不用 extern "C"; 纯 C++ namespace)
void attach_rm_debug_snapshot(RmDebugSnapshot* snap) noexcept;
void detach_rm_debug_snapshot() noexcept;

// 读当前全局 hook 指针 (observability / 测试用; acquire-load).
// 用途: 断言 attach/detach 生命周期 (老韩 R-11 INV-1: Shutdown 后须 == nullptr).
[[nodiscard]] const RmDebugSnapshot* current_rm_debug_snapshot() noexcept;

}  // namespace stcpp::risk
