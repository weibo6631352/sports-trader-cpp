// stcpp/infra/wal/wal_writer.cpp — WalWriter<R> 模板实例化 + 路径校验 + Append stub
//
// 落:
//   laowang-wal-framework-v0.2.md §3.3 (P9 Open path prefix 硬校验, fail = std::abort)
//   laowang-wal-framework-v0.2.md §4   (PIT assert 同步必调)
//   laowang-wal-framework-cpp-interface-v1.md §8
//
// W3 skeleton:
//   - Open() 真做 path prefix 硬校验 (R-11 闭环, 单测可验)
//   - Append() 真做 PIT (R-20 闭环, 单测可验)
//   - ring / bg fsync / fd  TODO W4 (rigtorp SPSC @小石, fsync @老练)
//
// 4 record 类型实例化在文件末尾.

#include "stcpp/infra/wal/wal_writer.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string_view>

namespace stcpp::infra::wal {

namespace {

// R-11 path prefix 硬校验. 不抛 — 防 caller try-catch 绕过.
// 调用点: WalWriter<R>::Open() 第一步, fail 立即 std::abort.
[[noreturn, maybe_unused]] inline void AbortOnPathMismatch(
    WalKind kind, std::string_view actual) noexcept {
    // 留 stderr 痕迹 (W4 接 syslog + chaos drill). 这里只 abort, 不 throw.
    static_cast<void>(kind);
    static_cast<void>(actual);
    std::abort();
}

[[nodiscard, maybe_unused]] inline bool PathPrefixOk(
    WalKind kind, std::string_view path) noexcept {
    const std::string_view root = PathRootOf(kind);
    return path.size() >= root.size() && path.substr(0, root.size()) == root;
}

}  // namespace

template <WalRecord R>
WalResult<std::unique_ptr<WalWriter<R>>> WalWriter<R>::Open(const WalConfig& cfg) {
    // P9 / R-11: 路径前缀硬校验. 不命中 = 配置/部署 bug → 立即 abort.
    if (!PathPrefixOk(cfg.kind, cfg.path_prefix)) {
        AbortOnPathMismatch(cfg.kind, cfg.path_prefix);   // [[noreturn]]
    }

    // ring_capacity 2 的幂 (CI lint 也拦, 这里兜底)
    if (cfg.ring_capacity == 0 || (cfg.ring_capacity & (cfg.ring_capacity - 1)) != 0) {
        return WalError::Io;
    }

    // TODO W4: 开 segment fd / 启动 bg jthread / pin cfg.bg_cpu_core / 启 SPSC ring.
    // [已转 Sprint-3 WAL-B01, 见 docs/SPRINTS/sprint-03-backlog.md]
    // 当前 skeleton: 构造空对象, API 闭环, 让 R-11 / R-20 / API 表面可测.
    auto w  = std::unique_ptr<WalWriter<R>>(new WalWriter<R>());
    w->cfg_ = cfg;
    return WalResult<std::unique_ptr<WalWriter<R>>>{std::move(w)};
}

template <WalRecord R>
WalResult<std::uint64_t> WalWriter<R>::Append(const R& record) noexcept {
    if (failed_.load(std::memory_order_acquire)) {
        return WalError::FsyncFailed;
    }

    // R-20: PIT 必调. 先填 header (4 ts + ULID), 再 assert.
    WalRecordHeader h{};
    FillHeaderFromRecord<R>(record, cfg_.kind, h);
    if (!pit::AssertChain(h)) {
        // 慢路径不在这里 emit RECON_DRIFT (那是 RM 的事), framework 只回错.
        return WalError::PitViolation;
    }

    // seq 单调 (framework 管).
    const std::uint64_t seq = next_seq_.fetch_add(1, std::memory_order_acq_rel) + 1;
    h.seq         = seq;
    h.len_payload = static_cast<std::uint16_t>(0);   // W4: 真实 serialize_into 后填

    // TODO W4: build_frame (header + payload + CRC32C) → ring.try_push.
    //  ring 满 → return WalError::Backpressure (老韩 v0.3 #18 AUDIT_WAL_BACKPRESSURE).
    //  PerRecord (position) 等 bg fsync 完成才返回.
    // [已转 Sprint-3 WAL-B02, 见 docs/SPRINTS/sprint-03-backlog.md]
    // 当前 skeleton: 视作 ring 永有空, 直接进 group commit watermark.
    high_watermark_.store(seq, std::memory_order_release);
    return WalResult<std::uint64_t>{seq};
}

template <WalRecord R>
WalResult<void> WalWriter<R>::FlushUntil(
    std::uint64_t seq, std::chrono::milliseconds /*timeout*/) noexcept {
    // W4: 真 fdatasync + condvar. 当前 skeleton: HighWatermark ≥ seq 即视作 flush 完成.
    if (high_watermark_.load(std::memory_order_acquire) >= seq) return WalResult<void>{};
    return WalError::Io;
}

template <WalRecord R>
WalWriter<R>::~WalWriter() = default;
// TODO W4: drain ring + 最终 fsync + close fd, 此处 stub.
// [已转 Sprint-3 WAL-B03, 见 docs/SPRINTS/sprint-03-backlog.md]

// -------- 4 类 record 实例化 (派单 §6) --------------------------------------
//
// 真 struct 由各 owner 仓位定义 + 满足 WalRecord concept. 这里 skeleton 用空 stub
// 让模板可独立编译 + 单测自定 MockRecord 校验 API 表面.
//
// W4 真上线时, 各 owner 在自己 module 显式实例化:
//
//   namespace stcpp::infra::wal {
//   template class WalWriter<RiskAuditRecord>;   // 老韩 + 老唐
//   template class WalWriter<PositionRecord>;    // 老周 + 老孙
//   template class WalWriter<PaperAuditRecord>;  // 小蒋
//   template class WalWriter<ShadowAuditRecord>; // 小蒋 / 小邓
//   }
//
// 当前 skeleton 没有 4 个 record struct 定义, 不在此 .cpp 实例化 (会编译错).
// 单测在 tests/unit/test_wal_writer.cpp 用 MockRecord 显式实例化.

}  // namespace stcpp::infra::wal
