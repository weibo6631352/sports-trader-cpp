// stcpp/infra/wal/ingest_raw_writer.cpp — IngestRawWriter 实现 (W6 Wave 29)
//
// Owner: 小冯 (api-watch-general, #34)
// last_review: 2026-05-28
//
// W6 stub 实现: ring 用原子 flag 模拟满状态.
// W7: 接 rigtorp::SPSCQueue<IngestRawRecord> (@小石) + bg fsync thread (@老王, vCPU pin).
//
// 红线 enforce:
//   R-12: record_frame 严禁阻塞 — 所有路径 O(1), 无 mutex, 无 wait
//   R-11: 构造期路径前缀含 "ingest" (runtime assert 兜底)
//   R-20: ingestion_ts = now(); event_ts / data_source_ts 由 caller 传入

#include "stcpp/infra/wal/ingest_raw_writer.hpp"

#include <cassert>
#include <cstring>
#include <ctime>
#include <string_view>

namespace stcpp::infra::wal {

namespace {

void MakeAuditId(std::array<std::uint8_t, 16>& out) noexcept {
    static std::atomic<std::uint64_t> seq{0};
    timespec t{};
    ::clock_gettime(CLOCK_REALTIME, &t);
    const std::uint64_t ts_ms =
        static_cast<std::uint64_t>(t.tv_sec) * 1000ULL
        + static_cast<std::uint64_t>(t.tv_nsec) / 1'000'000ULL;
    const std::uint64_t s = seq.fetch_add(1, std::memory_order_relaxed);
    std::memcpy(out.data(),     &ts_ms, 8);
    std::memcpy(out.data() + 8, &s,     8);
}

// 简单 CRC32C (Castagnoli). W7 换 hw-crc32c @老王.
std::uint32_t Crc32cSimple(const std::uint8_t* data, std::size_t len) noexcept {
    std::uint32_t crc = 0xFFFF'FFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            const std::uint32_t mask =
                static_cast<std::uint32_t>(-(static_cast<std::int32_t>(crc & 1u)));
            crc = (crc >> 1) ^ (0x82F6'3B78u & mask);
        }
    }
    return ~crc;
}

}  // namespace

// ---------------------------------------------------------------------------
// ctor / dtor
// ---------------------------------------------------------------------------

IngestRawWriter::IngestRawWriter(IngestRawWriterConfig cfg)
    : cfg_(cfg)
{
    // R-11 runtime assert 兜底 (CMakeLists 已 configure-time 校验)
    assert(std::string_view(cfg_.tier1_path_prefix).find("ingest") != std::string_view::npos
           && "R-11: IngestRaw tier1 path must contain 'ingest'");
    assert(std::string_view(cfg_.tier2_path_prefix).find("ingest") != std::string_view::npos
           && "R-11: IngestRaw tier2 path must contain 'ingest'");
    // W7: Open rigtorp SPSC ring + pin bg threads (老王 + 老周 ack)
}

IngestRawWriter::~IngestRawWriter() {
    // W7: stop bg fsync threads + flush
}

// ---------------------------------------------------------------------------
// record_frame — string_view 重载
// ---------------------------------------------------------------------------

ApplyResult IngestRawWriter::record_frame(
    IngestSourceKind source_kind,
    std::string_view payload_text,
    std::int64_t     event_ts_ns,
    std::int64_t     data_source_ts_ns) noexcept
{
    const auto* p = reinterpret_cast<const std::byte*>(payload_text.data());
    return record_frame(source_kind,
                        std::span<const std::byte>(p, payload_text.size()),
                        event_ts_ns,
                        data_source_ts_ns);
}

// ---------------------------------------------------------------------------
// record_frame — 核心路径 (R-12: 全 O(1), 无 mutex)
// ---------------------------------------------------------------------------

ApplyResult IngestRawWriter::record_frame(
    IngestSourceKind           source_kind,
    std::span<const std::byte> payload,
    std::int64_t               event_ts_ns,
    std::int64_t               data_source_ts_ns) noexcept
{
    // (1) ingestion_ts = now (唯一合法的本地 now())
    const std::int64_t ingestion_ts = now_realtime_ns();
    const std::int64_t as_of_ts     = ingestion_ts;

    // (2) R-20: data_source_ts 缺失 → fallback 到 ingestion_ts
    std::int64_t eff_ds_ts = data_source_ts_ns;
    if (eff_ds_ts <= 0) {
        eff_ds_ts = ingestion_ts;
        r20_fallback_.fetch_add(1, std::memory_order_relaxed);
    }

    // (3) event_ts 缺失 → fallback 到 eff_ds_ts
    std::int64_t eff_event_ts = event_ts_ns;
    if (eff_event_ts <= 0) {
        eff_event_ts = eff_ds_ts;
        r20_fallback_.fetch_add(1, std::memory_order_relaxed);
    }

    // (4) parse fail: 空 payload
    if (payload.empty()) {
        parse_failed_.fetch_add(1, std::memory_order_relaxed);
        IngestRawRecord meta{};
        meta.set_event_ts_ns(eff_event_ts);
        meta.set_data_source_ts_ns(eff_ds_ts);
        meta.set_ingestion_ts_ns(ingestion_ts);
        meta.set_as_of_ts_ns(as_of_ts);
        meta.set_source_kind(source_kind);
        meta.set_outcome(ReceptionOutcome::DroppedParseFail);
        meta.set_payload_size(0);
        MakeAuditId(meta.audit_id_ref());
        (void)push_tier2(meta);  // GM 错 #11 hotfix: [[nodiscard]] dropping case 显式忽略
        return ApplyResult::ParseFailed;
    }

    // (5) R-20 PIT check
    if (!check_pit(eff_event_ts, eff_ds_ts, ingestion_ts, as_of_ts)) {
        pit_rejected_.fetch_add(1, std::memory_order_relaxed);
        IngestRawRecord meta{};
        meta.set_event_ts_ns(eff_event_ts);
        meta.set_data_source_ts_ns(eff_ds_ts);
        meta.set_ingestion_ts_ns(ingestion_ts);
        meta.set_as_of_ts_ns(as_of_ts);
        meta.set_source_kind(source_kind);
        meta.set_outcome(ReceptionOutcome::DroppedR20Pit);
        meta.set_payload_size(0);
        MakeAuditId(meta.audit_id_ref());
        (void)push_tier2(meta);  // GM 错 #11 hotfix: [[nodiscard]] dropping case 显式忽略
        return ApplyResult::PitRejected;
    }

    // (6) payload 大小处理
    std::uint32_t psz = static_cast<std::uint32_t>(payload.size());
    if (psz > static_cast<std::uint32_t>(kIngestRawMaxPayloadBytes)) {
        if (cfg_.truncate_oversized_payload) {
            psz = static_cast<std::uint32_t>(kIngestRawMaxPayloadBytes);
        } else {
            parse_failed_.fetch_add(1, std::memory_order_relaxed);
            IngestRawRecord meta{};
            meta.set_event_ts_ns(eff_event_ts);
            meta.set_data_source_ts_ns(eff_ds_ts);
            meta.set_ingestion_ts_ns(ingestion_ts);
            meta.set_as_of_ts_ns(as_of_ts);
            meta.set_source_kind(source_kind);
            meta.set_outcome(ReceptionOutcome::DroppedParseFail);
            meta.set_payload_size(0);
            MakeAuditId(meta.audit_id_ref());
            (void)push_tier2(meta);  // GM 错 #11 hotfix: [[nodiscard]] 显式忽略 (oversize 已计 parse_failed_)
            return ApplyResult::ParseFailed;
        }
    }

    // (7) 构造 Tier 1 record
    IngestRawRecord rec{};
    rec.set_event_ts_ns(eff_event_ts);
    rec.set_data_source_ts_ns(eff_ds_ts);
    rec.set_ingestion_ts_ns(ingestion_ts);
    rec.set_as_of_ts_ns(as_of_ts);
    rec.set_source_kind(source_kind);
    rec.set_outcome(ReceptionOutcome::Accepted);
    rec.set_payload_size(psz);
    std::memcpy(rec.payload_blob_ref().data(), payload.data(), psz);
    rec.set_crc32c(Crc32cSimple(rec.payload_blob_ref().data(), psz));
    MakeAuditId(rec.audit_id_ref());

    // (8) Tier 1 push (非阻塞)
    if (push_tier1(rec)) {
        tier1_accepted_.fetch_add(1, std::memory_order_relaxed);
        return ApplyResult::Tier1Accepted;
    }

    // (9) Tier 1 满 → Tier 2 fallback
    tier2_fallback_.fetch_add(1, std::memory_order_relaxed);
    rec.set_outcome(ReceptionOutcome::DroppedRingFull);
    rec.set_payload_size(0);
    rec.set_crc32c(0);

    if (!push_tier2(rec)) {
        tier2_overflow_.fetch_add(1, std::memory_order_relaxed);
        return ApplyResult::Tier2Overflow;
    }
    return ApplyResult::Tier2Fallback;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::int64_t IngestRawWriter::now_realtime_ns() const noexcept {
    if (now_fn_) return now_fn_();
    timespec ts{};
    ::clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<std::int64_t>(ts.tv_sec) * 1'000'000'000LL
         + static_cast<std::int64_t>(ts.tv_nsec);
}

bool IngestRawWriter::push_tier1(const IngestRawRecord& rec) noexcept {
    // W6 stub: atomic flag; W7: tier1_spsc_.try_push(rec)
    if (tier1_ring_full_.load(std::memory_order_relaxed)) return false;
    (void)rec;
    return true;
}

bool IngestRawWriter::push_tier2(const IngestRawRecord& rec) noexcept {
    // W6 stub: atomic flag; W7: tier2_spsc_.try_push(rec)
    if (tier2_ring_full_.load(std::memory_order_relaxed)) return false;
    (void)rec;
    return true;
}

bool IngestRawWriter::check_pit(
    std::int64_t event_ts, std::int64_t ds_ts,
    std::int64_t ingest_ts, std::int64_t as_of_ts) const noexcept
{
    return (event_ts  >  0)
        && (ds_ts     >= event_ts)
        && (ingest_ts >= ds_ts)
        && (as_of_ts  >= ingest_ts);
}

}  // namespace stcpp::infra::wal
