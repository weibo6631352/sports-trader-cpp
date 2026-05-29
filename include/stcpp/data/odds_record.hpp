// stcpp/data/odds_record.hpp — MultiBookOddsRecord v0.1 (W6 Wave 29)
//
// Owner: 小段 (goalserve-specialist, #37)  Sprint-2 W6 D-W5-05
//
// 8-9 家 bookmaker 聚合快照, 满足 WalRecord concept.
// 接 P0-01 GoalserveDevigSignal (小卢 W6) 输入; 落 IngestRaw WAL (小冯 W6).
//
// 注意: goalserve_record.hpp::OddsRecord = per-bm per-outcome 单笔 (ETL 原始行).
//       MultiBookOddsRecord = 多家聚合, signal input + WAL 落盘.
//
// 红线:
//   R-20: ts.ds_origin 必须标 PayloadScoresTs/PayloadLastUpdate (禁 IngestionFallback 静默)
//   WalRecord: serialize_into + max_serialized_size + 4 ts getter 必须齐全
//
// 不耻下问: bookmaker 第 9 家 → @老彭 W6 EOW; WAL schema → @小冯; audit → @老唐

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <type_traits>

#include "stcpp/data/data_contract.hpp"     // kNumBookmakers + kBookmakerIds
#include "stcpp/data/goalserve_record.hpp"  // FourTs + DataSourceTsOrigin

namespace stcpp::data::goalserve {

// ---------------------------------------------------------------------------
// 1. BookmakerOddsSlot — 单家 yes/no 十进制赔率 POD
// ---------------------------------------------------------------------------
struct BookmakerOddsSlot {
    double odds_yes = 0.0;  // decimal odds YES (home/over)
    double odds_no = 0.0;   // decimal odds NO  (away/under)
    bool valid = false;     // true = odds 就位且 > 1.0
};

static_assert(std::is_trivially_copyable_v<BookmakerOddsSlot>);

// ---------------------------------------------------------------------------
// 2. MultiBookOddsRecord — 满足 WalRecord concept
//
// slots[i] <-> kBookmakerIds[i] (ABI 锁, 8 家).
//
// WAL serialize (小冯配套, 不含 string 字段):
//   32B (4ts) + 4B (meta) + 8×24B (slots) + 4B (crc) + 16B (aid) = 248B
// ---------------------------------------------------------------------------
struct MultiBookOddsRecord {
    FourTs ts{};  // R-20 4 ts (UPSTREAM_PAYLOAD 优先)
    GoalserveSport sport = GoalserveSport::Soccer;
    std::string match_id;   // Goalserve match id
    std::string market_id;  // 1x2 / OU_2.5 / AH_-0.5 etc.
    std::array<BookmakerOddsSlot, kNumBookmakers> slots{};
    std::uint32_t crc32c = 0;
    std::array<std::uint8_t, 16> audit_id = {};

    // WalRecord concept 接口
    [[nodiscard]] std::int64_t event_ts_ns() const noexcept { return ts.event_ts_ns; }
    [[nodiscard]] std::int64_t data_source_ts_ns() const noexcept { return ts.data_source_ts_ns; }
    [[nodiscard]] std::int64_t ingestion_ts_ns() const noexcept { return ts.ingestion_ts_ns; }
    [[nodiscard]] std::int64_t as_of_ts_ns() const noexcept { return ts.as_of_ts_ns; }
    // GM 错 #11 hotfix: 成员变量名与方法名冲突, 改方法名 get_audit_id() (字段保持 audit_id)
    [[nodiscard]] std::array<std::uint8_t, 16> get_audit_id() const noexcept { return audit_id; }

    [[nodiscard]] std::size_t serialize_into(std::span<std::byte> out) const noexcept {
        if (out.size() < max_serialized_size())
            return 0;
        std::size_t off = 0;

        auto wi64 = [&](std::int64_t v) noexcept {
            std::uint64_t u;
            std::memcpy(&u, &v, 8);
            for (int b = 0; b < 8; ++b)
                out[off++] = static_cast<std::byte>((u >> (8 * b)) & 0xFFu);
        };
        auto wu32 = [&](std::uint32_t v) noexcept {
            for (int b = 0; b < 4; ++b)
                out[off++] = static_cast<std::byte>((v >> (8 * b)) & 0xFFu);
        };
        auto wf64 = [&](double v) noexcept {
            std::uint64_t u;
            std::memcpy(&u, &v, 8);
            for (int b = 0; b < 8; ++b)
                out[off++] = static_cast<std::byte>((u >> (8 * b)) & 0xFFu);
        };
        auto wu8 = [&](std::uint8_t v) noexcept {
            out[off++] = static_cast<std::byte>(v);
        };

        wi64(ts.event_ts_ns);
        wi64(ts.data_source_ts_ns);
        wi64(ts.ingestion_ts_ns);
        wi64(ts.as_of_ts_ns);
        wu8(static_cast<std::uint8_t>(ts.ds_origin));
        wu8(static_cast<std::uint8_t>(sport));
        wu8(0);
        wu8(0);  // pad

        for (const auto& sl : slots) {
            wf64(sl.odds_yes);
            wf64(sl.odds_no);
            wu8(sl.valid ? 1u : 0u);
            for (int p = 0; p < 7; ++p)
                wu8(0u);
        }
        wu32(crc32c);
        std::memcpy(out.data() + off, audit_id.data(), 16);
        off += 16;
        return off;
    }

    [[nodiscard]] static constexpr std::size_t max_serialized_size() noexcept {
        return 32U + 4U + (kNumBookmakers * 24U) + 4U + 16U;  // 248B
    }

    [[nodiscard]] bool IsFourTsMonotonic() const noexcept { return ts.IsMonotonic(); }
    [[nodiscard]] bool RespectsR20() const noexcept {
        return ts.IsMonotonic() && (ts.ds_origin != DataSourceTsOrigin::IngestionFallback);
    }
    [[nodiscard]] std::size_t ValidBookmakerCount() const noexcept {
        std::size_t n = 0;
        for (const auto& sl : slots)
            if (sl.valid && sl.odds_yes > 1.0 && sl.odds_no > 1.0)
                ++n;
        return n;
    }
};

// MultiBookOddsRecord 含 std::string, 非 trivially_copyable (WalRecord 不要求).
static_assert(!std::is_trivially_copyable_v<MultiBookOddsRecord>);
static_assert(MultiBookOddsRecord::max_serialized_size() == 248U);

}  // namespace stcpp::data::goalserve
