// stcpp/infra/wal/wal_record_header.hpp — WAL header v2 (64B POD)
//
// 落:
//   laowang-wal-framework-v0.2.md §2.1 (MAGIC "WAL2", 64B layout)
//   laowang-wal-framework-cpp-interface-v1.md §3
//   laotang-audit-schema-v1.1.md §2.x (4 ts offset 16/24/32/40, audit_id 48..64)
//
// 红线:
//   R-20 4 ts 全程携带 (event / data_source / ingestion / as_of)
//   R-11 framework 不算 BLAKE3, 仅 CRC32C (老唐 v1.1 §2.x); prev_hash 在 payload (老唐 ownership).
//
// 不耻下问: BLAKE3 @老孙, 4 ts canon @老唐

#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <type_traits>

namespace stcpp::infra::wal {

inline constexpr std::array<char, 4> kMagicV2{{'W', 'A', 'L', '2'}};
inline constexpr std::uint8_t kHeaderVersionV2 = 2;
inline constexpr std::size_t kHeaderSizeV2     = 64;
inline constexpr std::size_t kMaxPayloadBytes  = 65535;  // u16 LEN

#pragma pack(push, 1)
struct WalRecordHeader {
    std::array<char, 4>          magic;              //  0..4   "WAL2"
    std::uint8_t                 ver;                //  4..5   = 2
    std::uint8_t                 wal_kind;           //  5..6   WalKind (0..3)
    std::uint16_t                len_payload;        //  6..8   ≤ 64 KiB
    std::uint64_t                seq;                //  8..16  framework 管, monotonic per-WAL
    std::int64_t                 event_ts_ns;        // 16..24  R-20
    std::int64_t                 data_source_ts_ns;  // 24..32  R-20
    std::int64_t                 ingestion_ts_ns;    // 32..40  R-20 (MONOTONIC_RAW)
    std::int64_t                 as_of_ts_ns;        // 40..48  R-20 (REALTIME)
    std::array<std::uint8_t, 16> audit_id;           // 48..64  ULID (caller 填)
    // 64+N PAYLOAD (老唐 schema, 含 BLAKE3 prev_hash / payload_hash / current_hash, framework 不解析)
    // 64+N+4 CRC32C (over MAGIC..PAYLOAD, framework 算)
};
#pragma pack(pop)

static_assert(sizeof(WalRecordHeader) == kHeaderSizeV2,
              "WalRecordHeader 必须 64B (老王 v0.2 §2.1 + 老唐 v1.1 §2.x)");
static_assert(std::is_trivially_copyable_v<WalRecordHeader>,
              "WalRecordHeader 必须 trivially copyable (POD 落盘 + memcpy)");
static_assert(std::is_standard_layout_v<WalRecordHeader>);

// 字段偏移硬校验 (与老唐 v1.1 §2.x 表对齐)
static_assert(offsetof(WalRecordHeader, magic)             ==  0);
static_assert(offsetof(WalRecordHeader, ver)               ==  4);
static_assert(offsetof(WalRecordHeader, wal_kind)          ==  5);
static_assert(offsetof(WalRecordHeader, len_payload)       ==  6);
static_assert(offsetof(WalRecordHeader, seq)               ==  8);
static_assert(offsetof(WalRecordHeader, event_ts_ns)       == 16);
static_assert(offsetof(WalRecordHeader, data_source_ts_ns) == 24);
static_assert(offsetof(WalRecordHeader, ingestion_ts_ns)   == 32);
static_assert(offsetof(WalRecordHeader, as_of_ts_ns)       == 40);
static_assert(offsetof(WalRecordHeader, audit_id)          == 48);

[[nodiscard]] inline bool HasValidMagic(const WalRecordHeader& h) noexcept {
    return h.magic == kMagicV2 && h.ver == kHeaderVersionV2;
}

}  // namespace stcpp::infra::wal
