// stcpp/infra/wal/ingest_raw_writer.hpp — IngestRaw WAL writer (2-tier, W6 Wave 29)
//
// Owner: 小冯 (api-watch-general, #34)
// Spec:  小余 DS-01 (2026-06-01-vote-xiaoyu-arch-challenge.md §2)
// last_review: 2026-05-28
//
// 2-Tier 设计 (R-12 WSS event loop 不阻塞):
//
//   Tier 1 — full payload WAL
//     SPSC ring (WALQueue) → bg fsync thread
//     容量: kTier1RingCapacity (= 16384, 2^n)
//     满时: fallback 到 Tier 2, 不阻塞调用方 (R-12)
//
//   Tier 2 — minimal counter WAL (不丢 metadata)
//     原子计数器 + 独立 bg ring (容量更大: kTier2RingCapacity = 65536)
//     每条记录 ~72B (无 payload_blob), outcome = DroppedRingFull / DroppedParseFail / DroppedR20Pit
//     Tier 2 ring 本身满时: 只递增 tier2_overflow_count_ (极端情况, 不能进一步 fallback)
//
// 接入点:
//   PM WSS subscriber (OnTransportFrame):
//     ingest_raw_writer.record_frame(IngestSourceKind::PM_WSS, payload, event_ts, ds_ts)
//   Goalserve client (ParseBatch / Fetch):
//     ingest_raw_writer.record_frame(IngestSourceKind::GS_*, payload, event_ts, ds_ts)
//
// 红线:
//   R-12: record_frame() 必须非阻塞 (不等 fsync, Tier 2 fallback 也不阻塞)
//   R-11: paper/live 路径物理隔离 (STCPP_INGEST_RAW_WAL_PREFIX build-time 宏)
//   R-20: event_ts / data_source_ts 来自调用方 (上游 payload), 本类只填 ingestion_ts (now())
//
// 不耻下问:
//   - WAL ring/fsync 框架 → 老王 (WalWriter<R>)
//   - Tier 2 bg core 分配 → 老周 (vCPU pin §15.6, Tier 2 走 core 5 or OS scheduler)
//   - ML 训练读取 schema → 小邓 (IngestRawRecord layout + payload decode)

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <functional>
#include <span>
#include <string_view>

#include "stcpp/infra/wal/ingest_raw_record.hpp"
#include "stcpp/infra/wal/wal_error.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"

namespace stcpp::infra::wal {

// ---------------------------------------------------------------------------
// IngestRawWriterConfig
// ---------------------------------------------------------------------------
struct IngestRawWriterConfig {
    // 2026-06-12 运行时 mode: 路径前缀按 ExecutionContext::Mode() 运行时选 (R-11 paper/live 分目录)。
    //   默认 paper 安全侧; 单测可覆盖。
    const char* tier1_path_prefix = "/var/lib/stcpp/ingest/paper";
    const char* tier2_path_prefix = "/var/lib/stcpp/ingest/paper/counter";

    // 按运行时 mode 取默认前缀 (调用方装配时用; R-11: live 路径必含 "live")。
    [[nodiscard]] static const char* DefaultTier1ForMode(bool is_live) noexcept {
        return is_live ? "/var/lib/stcpp/ingest/live" : "/var/lib/stcpp/ingest/paper";
    }

    // Tier 1 SPSC ring 容量 (2^n, 16 KB payload × 16384 = 256 MB 峰值; 实际不全占)
    std::size_t tier1_ring_capacity = 16384;

    // Tier 2 minimal ring 容量 (2^n, ~72B × 65536 = ~4.7 MB)
    std::size_t tier2_ring_capacity = 65536;

    // Tier 1 bg fsync cpu core (老周 §15.6 core 5 IngestRaw)
    int tier1_bg_cpu_core = 5;
    // Tier 2 bg cpu core (OS scheduler 可接受, 设 -1 = no pin)
    int tier2_bg_cpu_core = -1;

    // payload 超 16 KB 时行为: true = truncate to 16384, false = reject (DroppedParseFail)
    bool truncate_oversized_payload = true;
};

// ---------------------------------------------------------------------------
// ApplyResult — record_frame() 返回值
// ---------------------------------------------------------------------------
enum class ApplyResult : std::uint8_t {
    Tier1Accepted    = 0,  // Tier 1 SPSC push 成功, 完整 payload 落盘路径
    Tier2Fallback    = 1,  // Tier 1 ring 满, fallback Tier 2 metadata-only
    Tier2Overflow    = 2,  // Tier 1 + Tier 2 都满 (极端), 仅递增 tier2_overflow_count_
    PitRejected      = 3,  // R-20 PIT 违反, 写 Tier 2 with DroppedR20Pit
    ParseFailed      = 4,  // parse fail (payload=null/0), 写 Tier 2 with DroppedParseFail
};

[[nodiscard]] constexpr std::string_view ToString(ApplyResult r) noexcept {
    switch (r) {
        case ApplyResult::Tier1Accepted: return "Tier1Accepted";
        case ApplyResult::Tier2Fallback: return "Tier2Fallback";
        case ApplyResult::Tier2Overflow: return "Tier2Overflow";
        case ApplyResult::PitRejected:   return "PitRejected";
        case ApplyResult::ParseFailed:   return "ParseFailed";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// IngestRawWriter
//
// 主类. 非虚, 最终类 (final). 禁 copy/move.
// 生命周期: 构造时 Open() Tier 1 + Tier 2 WAL writer; 析构时 stop bg threads.
// ---------------------------------------------------------------------------
class IngestRawWriter final {
public:
    // 可注入 now_ns 函数 (单测用); 默认走 CLOCK_REALTIME
    using NowFn = std::function<std::int64_t()>;

    // 构造: Open 两层 WAL (Tier 1 + Tier 2). 如果路径不合法 → std::abort (R-11).
    explicit IngestRawWriter(IngestRawWriterConfig cfg = {});

    IngestRawWriter(const IngestRawWriter&)            = delete;
    IngestRawWriter& operator=(const IngestRawWriter&) = delete;
    IngestRawWriter(IngestRawWriter&&)                 = delete;
    IngestRawWriter& operator=(IngestRawWriter&&)      = delete;

    ~IngestRawWriter();

    // -----------------------------------------------------------------------
    // record_frame — 主接入点 (R-12: 非阻塞, 严禁等 fsync)
    //
    // 参数:
    //   source_kind:    数据来源 (PM_WSS / GS_*)
    //   payload:        原始字节 (PM JSON text / GS XML/JSON 原始 body)
    //                   span 为空 = ParseFailed 路径
    //   event_ts_ns:    上游 frame 产生时刻 (R-20 UPSTREAM_PAYLOAD)
    //   data_source_ts_ns: PM/GS payload 携带的服务端 ts (R-20 UPSTREAM_PAYLOAD)
    //                   若上游无 ts, caller 传 0 → 本类 fallback 到 ingestion_ts
    //                   (将发 metric wss_r20_fallback_total++)
    //
    // 返回: ApplyResult (调用方可 emit metric, 不需要处理错误)
    // -----------------------------------------------------------------------
    [[nodiscard]] ApplyResult record_frame(
        IngestSourceKind          source_kind,
        std::span<const std::byte> payload,
        std::int64_t              event_ts_ns,
        std::int64_t              data_source_ts_ns) noexcept;

    // 便捷重载 — string_view payload (PM WSS text frame 常用)
    [[nodiscard]] ApplyResult record_frame(
        IngestSourceKind source_kind,
        std::string_view payload_text,
        std::int64_t     event_ts_ns,
        std::int64_t     data_source_ts_ns) noexcept;

    // -----------------------------------------------------------------------
    // Observability
    // -----------------------------------------------------------------------
    [[nodiscard]] std::uint64_t tier1_accepted_total()   const noexcept {
        return tier1_accepted_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t tier2_fallback_total()   const noexcept {
        return tier2_fallback_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t tier2_overflow_total()   const noexcept {
        return tier2_overflow_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t pit_rejected_total()     const noexcept {
        return pit_rejected_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t parse_failed_total()     const noexcept {
        return parse_failed_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t r20_fallback_total()     const noexcept {
        return r20_fallback_.load(std::memory_order_relaxed);
    }

    // 单测注入 now_ns (覆盖 CLOCK_REALTIME)
    void set_now_fn_for_test(NowFn fn) noexcept { now_fn_ = std::move(fn); }

private:
    [[nodiscard]] std::int64_t now_realtime_ns() const noexcept;

    // 向 Tier 1 ring 推送完整 record (非阻塞)
    [[nodiscard]] bool push_tier1(const IngestRawRecord& rec) noexcept;

    // 向 Tier 2 ring 推送 metadata-only record (非阻塞)
    [[nodiscard]] bool push_tier2(const IngestRawRecord& rec) noexcept;

    // R-20 PIT check (inline 快路径)
    [[nodiscard]] bool check_pit(
        std::int64_t event_ts, std::int64_t ds_ts,
        std::int64_t ingest_ts, std::int64_t as_of_ts) const noexcept;

    IngestRawWriterConfig cfg_;
    NowFn                 now_fn_;

    // metrics (cache-line 对齐防伪共享)
    alignas(64) std::atomic<std::uint64_t> tier1_accepted_{0};
    alignas(64) std::atomic<std::uint64_t> tier2_fallback_{0};
    alignas(64) std::atomic<std::uint64_t> tier2_overflow_{0};
    alignas(64) std::atomic<std::uint64_t> pit_rejected_{0};
    alignas(64) std::atomic<std::uint64_t> parse_failed_{0};
    alignas(64) std::atomic<std::uint64_t> r20_fallback_{0};  // data_source_ts 缺失 fallback

    // Tier 1 / Tier 2 内部 ring buffer (使用 IngestRawRecord SPSC; W6 stub: 原子计数模拟)
    // W6 stub: ring 用 bool flag 模拟 full 状态 (单测可通过 fill_tier1_ring() 触发 full)
    // W7: 接 rigtorp::SPSCQueue<IngestRawRecord, kTier1RingCapacity> (老王 ack)
    std::atomic<bool> tier1_ring_full_{false};   // 单测 hook: set true → 触发 Tier 2 fallback
    std::atomic<bool> tier2_ring_full_{false};   // 单测 hook: set true → 触发 overflow

public:
    // 单测专用: 模拟 ring 满
    void simulate_tier1_full(bool v) noexcept { tier1_ring_full_.store(v, std::memory_order_relaxed); }
    void simulate_tier2_full(bool v) noexcept { tier2_ring_full_.store(v, std::memory_order_relaxed); }
};

}  // namespace stcpp::infra::wal
