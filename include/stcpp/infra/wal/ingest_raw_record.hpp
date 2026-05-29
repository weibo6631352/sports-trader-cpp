// stcpp/infra/wal/ingest_raw_record.hpp — IngestRaw WAL record POD (W6 Wave 29)
//
// Owner: 小冯 (api-watch-general, #34)
// Spec:  小余 DS-01 (2026-06-01-vote-xiaoyu-arch-challenge.md §2)
// last_review: 2026-05-28
//
// 目的: Goalserve / PM WSS 原始帧落盘, 解决 ring 满时原始数据永久丢失问题.
//       ML 训练 (小邓) / 复盘 / 事故追溯可从此 WAL replay 原始字节序列.
//
// 红线:
//   R-20: 4 ts 契约 event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts
//   R-11: paper/live 物理隔离 (CMake STCPP_INGEST_RAW_WAL_PREFIX)
//   R-7:  paper 路径含 "paper", live 路径含 "live"
//
// WalRecord concept (wal_writer.hpp) 要求:
//   event_ts_ns() / data_source_ts_ns() / ingestion_ts_ns() / as_of_ts_ns()
//   audit_id() / serialize_into() / max_serialized_size()
//
// 命名约定: 存储字段以 m_ 前缀, accessor 方法与 concept 名一致 (无前缀).
// 为测试赋值方便, 提供别名宏不行 — 改为提供 setter 或直接暴露公开 setter 函数.
// 最终选择: 字段公开但带 m_ 前缀; 测试通过 set_* 赋值或 make_record helper.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>

namespace stcpp::infra::wal {

// ---------------------------------------------------------------------------
// 1. IngestSourceKind — 数据来源标识 (5 种)
// ---------------------------------------------------------------------------
enum class IngestSourceKind : std::uint8_t {
    PM_WSS = 0,        // Polymarket sports WSS frame
    GS_OddsFeed = 1,   // Goalserve oddsfeed dump
    GS_LiveScore = 2,  // Goalserve livescore REST
    GS_Inplay = 3,     // Goalserve inplay-{sport}.gz
    GS_Mapping = 4,    // Goalserve inplay-mapping
};

inline constexpr std::size_t kIngestSourceKindCount = 5;

[[nodiscard]] constexpr std::string_view ToString(IngestSourceKind k) noexcept {
    switch (k) {
        case IngestSourceKind::PM_WSS:
            return "PM_WSS";
        case IngestSourceKind::GS_OddsFeed:
            return "GS_OddsFeed";
        case IngestSourceKind::GS_LiveScore:
            return "GS_LiveScore";
        case IngestSourceKind::GS_Inplay:
            return "GS_Inplay";
        case IngestSourceKind::GS_Mapping:
            return "GS_Mapping";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// 2. ReceptionOutcome — 帧接收结果
// ---------------------------------------------------------------------------
enum class ReceptionOutcome : std::uint8_t {
    Accepted = 0,          // Tier 1 full payload 成功
    DroppedRingFull = 1,   // Tier 1 ring 满 → Tier 2 metadata-only
    DroppedParseFail = 2,  // parse 失败
    DroppedR20Pit = 3,     // R-20 PIT 违反
};

[[nodiscard]] constexpr std::string_view ToString(ReceptionOutcome o) noexcept {
    switch (o) {
        case ReceptionOutcome::Accepted:
            return "ACCEPTED";
        case ReceptionOutcome::DroppedRingFull:
            return "DROPPED_RING_FULL";
        case ReceptionOutcome::DroppedParseFail:
            return "DROPPED_PARSE_FAIL";
        case ReceptionOutcome::DroppedR20Pit:
            return "DROPPED_R20_PIT";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// 3. 常量
// ---------------------------------------------------------------------------

inline constexpr std::size_t kIngestRawMaxPayloadBytes = 16384;
inline constexpr std::size_t kIngestRawMinimalRecordBytes = 26;  // 1+1+4+0+4+16

// ---------------------------------------------------------------------------
// 4. IngestRawRecord — POD, 满足 WalRecord concept
//
// 字段前缀 m_ 避免与 accessor 方法同名 (C++ 禁止 data member 与 member function 同名).
// accessor 方法名与 WalRecord concept 要求完全一致.
// ---------------------------------------------------------------------------
struct IngestRawRecord {
    // --- 4 ts (R-20) — m_ 前缀字段 ---
    std::int64_t m_event_ts_ns = 0;
    std::int64_t m_data_source_ts_ns = 0;
    std::int64_t m_ingestion_ts_ns = 0;
    std::int64_t m_as_of_ts_ns = 0;

    // --- 来源 + 结果 ---
    IngestSourceKind m_source_kind = IngestSourceKind::PM_WSS;
    ReceptionOutcome m_outcome = ReceptionOutcome::Accepted;

    // --- payload ---
    std::uint32_t m_payload_size = 0;
    std::array<std::uint8_t, kIngestRawMaxPayloadBytes> m_payload_blob{};

    // --- integrity ---
    std::uint32_t m_crc32c = 0;
    std::array<std::uint8_t, 16> m_audit_id{};

    // -----------------------------------------------------------------------
    // Convenience setters (test + IngestRawWriter 用)
    // -----------------------------------------------------------------------
    void set_event_ts_ns(std::int64_t v) noexcept { m_event_ts_ns = v; }
    void set_data_source_ts_ns(std::int64_t v) noexcept { m_data_source_ts_ns = v; }
    void set_ingestion_ts_ns(std::int64_t v) noexcept { m_ingestion_ts_ns = v; }
    void set_as_of_ts_ns(std::int64_t v) noexcept { m_as_of_ts_ns = v; }
    void set_source_kind(IngestSourceKind k) noexcept { m_source_kind = k; }
    void set_outcome(ReceptionOutcome o) noexcept { m_outcome = o; }
    void set_payload_size(std::uint32_t sz) noexcept { m_payload_size = sz; }
    void set_crc32c(std::uint32_t c) noexcept { m_crc32c = c; }
    void set_audit_id(const std::array<std::uint8_t, 16>& id) noexcept { m_audit_id = id; }

    // -----------------------------------------------------------------------
    // WalRecord concept 接口 (accessor methods — no m_ prefix)
    // -----------------------------------------------------------------------
    [[nodiscard]] std::int64_t event_ts_ns() const noexcept { return m_event_ts_ns; }
    [[nodiscard]] std::int64_t data_source_ts_ns() const noexcept { return m_data_source_ts_ns; }
    [[nodiscard]] std::int64_t ingestion_ts_ns() const noexcept { return m_ingestion_ts_ns; }
    [[nodiscard]] std::int64_t as_of_ts_ns() const noexcept { return m_as_of_ts_ns; }
    [[nodiscard]] std::array<std::uint8_t, 16> audit_id() const noexcept { return m_audit_id; }

    // Mutable field references for direct assignment (IngestRawWriter internals)
    // Provided to keep IngestRawWriter.cpp simple without boilerplate setters.
    std::int64_t& event_ts_ns_ref() noexcept { return m_event_ts_ns; }
    std::int64_t& data_source_ts_ns_ref() noexcept { return m_data_source_ts_ns; }
    std::int64_t& ingestion_ts_ns_ref() noexcept { return m_ingestion_ts_ns; }
    std::int64_t& as_of_ts_ns_ref() noexcept { return m_as_of_ts_ns; }
    IngestSourceKind& source_kind_ref() noexcept { return m_source_kind; }
    ReceptionOutcome& outcome_ref() noexcept { return m_outcome; }
    std::uint32_t& payload_size_ref() noexcept { return m_payload_size; }
    std::uint32_t& crc32c_ref() noexcept { return m_crc32c; }
    std::array<std::uint8_t, 16>& audit_id_ref() noexcept { return m_audit_id; }

    // payload_blob ref (caller fills directly)
    std::array<std::uint8_t, kIngestRawMaxPayloadBytes>& payload_blob_ref() noexcept {
        return m_payload_blob;
    }
    const std::array<std::uint8_t, kIngestRawMaxPayloadBytes>& payload_blob_ref() const noexcept {
        return m_payload_blob;
    }

    // -----------------------------------------------------------------------
    // Getters for test convenience
    // -----------------------------------------------------------------------
    [[nodiscard]] IngestSourceKind source_kind() const noexcept { return m_source_kind; }
    [[nodiscard]] ReceptionOutcome outcome() const noexcept { return m_outcome; }
    [[nodiscard]] std::uint32_t payload_size() const noexcept { return m_payload_size; }
    [[nodiscard]] std::uint32_t crc32c() const noexcept { return m_crc32c; }

    // -----------------------------------------------------------------------
    // serialize_into (WalRecord concept)
    //   Tier 1: [1B source_kind][1B outcome][4B payload_size][N bytes][4B crc32c][16B audit_id]
    //   Tier 2: [1B source_kind][1B outcome][4B=0][4B crc32c][16B audit_id]
    // -----------------------------------------------------------------------
    [[nodiscard]] std::size_t serialize_into(std::span<std::byte> out) const noexcept {
        const bool has_payload = (m_payload_size > 0 && m_outcome == ReceptionOutcome::Accepted);
        const std::uint32_t psz = has_payload ? m_payload_size : 0u;
        const std::size_t needed = 1u + 1u + 4u + static_cast<std::size_t>(psz) + 4u + 16u;
        if (out.size() < needed)
            return 0;

        std::size_t off = 0;

        out[off++] = static_cast<std::byte>(static_cast<std::uint8_t>(m_source_kind));
        out[off++] = static_cast<std::byte>(static_cast<std::uint8_t>(m_outcome));

        // payload_size (LE)
        out[off++] = static_cast<std::byte>((psz) & 0xFFu);
        out[off++] = static_cast<std::byte>((psz >> 8) & 0xFFu);
        out[off++] = static_cast<std::byte>((psz >> 16) & 0xFFu);
        out[off++] = static_cast<std::byte>((psz >> 24) & 0xFFu);

        if (has_payload && psz > 0) {
            std::memcpy(out.data() + off, m_payload_blob.data(), psz);
            off += psz;
        }

        // crc32c (LE)
        out[off++] = static_cast<std::byte>((m_crc32c) & 0xFFu);
        out[off++] = static_cast<std::byte>((m_crc32c >> 8) & 0xFFu);
        out[off++] = static_cast<std::byte>((m_crc32c >> 16) & 0xFFu);
        out[off++] = static_cast<std::byte>((m_crc32c >> 24) & 0xFFu);

        std::memcpy(out.data() + off, m_audit_id.data(), 16);
        off += 16;
        return off;
    }

    [[nodiscard]] static constexpr std::size_t max_serialized_size() noexcept {
        return 1u + 1u + 4u + kIngestRawMaxPayloadBytes + 4u + 16u;  // = 16410
    }
};

static_assert(std::is_trivially_copyable_v<IngestRawRecord>,
              "IngestRawRecord must be trivially copyable for SPSC ring");

}  // namespace stcpp::infra::wal
