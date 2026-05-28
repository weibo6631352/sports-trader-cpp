// stcpp/data/data_contract.hpp — Goalserve DataContract SSOT v0.1 (W6 Wave 29)
//
// Owner: 小段 (goalserve-specialist, #37)
// Sprint-2 W6 Wave 29 — D-W5-05 data-contract 落 src/stcpp/data/
//
// 红线:
//   R-20: validate_payload 必须校验 data_source_ts 非零且来自 UPSTREAM_PAYLOAD
//   R-7:  paper / live 共享此 DataContract (ADR-011, 数据契约一致)
//   bookmaker ABI 锁: kBookmakerIds 一旦 code-in, 必须 ADR 变更才能增删
//
// 不耻下问: bookmaker 第 9 家 → @老彭 W6 EOW; PM_WSS 字段位置 → @小冯; audit → @老唐

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "stcpp/data/goalserve_schema.hpp"
// DataSourceTsOrigin (PayloadScoresTs / PayloadLastUpdate / IngestionFallback)
#include "stcpp/data/goalserve_record.hpp"

namespace stcpp::data::goalserve {

// ---------------------------------------------------------------------------
// 1. Bookmaker 列表 ABI 锁 (ADR-008 + 小段 v3 §7 ETL-12)
//
// 顺序不可改变 (小卢 GoalserveDevigSignal + 老彭 CSV 列索引硬绑).
// 变更: ADR + @老彭 + @小卢 ack 后 bump kBookmakerAbiVersion.
// ---------------------------------------------------------------------------
inline constexpr std::array<std::int32_t, 8> kBookmakerIds{
    14,   // 10Bet
    15,   // WilliamHill
    16,   // bet365
    17,   // Marathon
    18,   // Unibet
    65,   // BetVictor
    105,  // 1xBet
    144,  // Betano
};

inline constexpr std::array<std::string_view, 8> kBookmakerNames{
    "10Bet", "WilliamHill", "bet365", "Marathon",
    "Unibet", "BetVictor", "1xBet", "Betano",
};

inline constexpr std::size_t    kNumBookmakers        = kBookmakerIds.size();
inline constexpr std::string_view kBookmakerAbiVersion = "bm-abi-v1.0-8bm";
inline constexpr std::size_t    kMinValidBookmakers   = 3U;

// ---------------------------------------------------------------------------
// 2. 4 ts 字段位置 per payload type (R-20 SSOT)
//
// 委托 goalserve_schema.hpp::TsFieldSpec / GetTsFieldSpec (同 SSOT).
// 此处重新导出供 D 单元统一 include.
// ---------------------------------------------------------------------------
// TsFieldSpec / GetTsFieldSpec 已在 goalserve_schema.hpp 定义, 此处无需重复.

// ---------------------------------------------------------------------------
// 3. Sport name alias (双 slug 陷阱 SSOT)
//
// 委托 goalserve_client.hpp SportInplaySlug / SportPregameSlug / SportOddsCat,
// 此处提供结构化封装便于 ETL 直接查表.
// ---------------------------------------------------------------------------
struct SportAlias {
    std::string_view inplay_slug;
    std::string_view pregame_slug;
    std::string_view odds_cat;
};

[[nodiscard]] constexpr SportAlias GetSportAlias(GoalserveSport s) noexcept {
    return {SportInplaySlug(s), SportPregameSlug(s), SportOddsCat(s)};
}

// ---------------------------------------------------------------------------
// 4. DataContract — 聚合常量 struct (SSOT)
// ---------------------------------------------------------------------------
struct DataContract {
    std::string_view schema_version       = kSchemaVersion;
    std::string_view bookmaker_abi_version = kBookmakerAbiVersion;
    std::size_t      num_bookmakers        = kNumBookmakers;
    std::size_t      num_sports            = kNumSports;
    std::size_t      num_hosts             = kNumHosts;
    std::size_t      num_time_status       = kAllTimeStatus.size();
    std::size_t      num_payload_types     = kNumPayloadTypes;
};

inline constexpr DataContract kDefaultDataContract{};

// ---------------------------------------------------------------------------
// 5. PayloadValidationResult + validate_payload (R-20 入口)
// ---------------------------------------------------------------------------
struct PayloadValidationResult {
    bool             valid                  = false;
    bool             data_source_ts_present = false;
    bool             event_ts_present       = false;
    bool             is_upstream_payload    = false;
    std::string_view error_msg              = "";
};

[[nodiscard]] inline PayloadValidationResult validate_payload(
    std::int64_t       event_ts_ns,
    std::int64_t       data_source_ts_ns,
    std::int64_t       ingestion_ts_ns,
    std::int64_t       as_of_ts_ns,
    DataSourceTsOrigin ds_origin,
    PayloadType        pt,
    const DataContract& = kDefaultDataContract) noexcept {
    (void)pt;
    PayloadValidationResult r{};
    if (event_ts_ns <= 0)        { r.error_msg = "event_ts_ns must be > 0";   return r; }
    if (data_source_ts_ns <= 0)  { r.error_msg = "data_source_ts_ns must be > 0 (UPSTREAM_PAYLOAD)"; return r; }
    if (ingestion_ts_ns <= 0)    { r.error_msg = "ingestion_ts_ns must be > 0"; return r; }
    if (as_of_ts_ns <= 0)        { r.error_msg = "as_of_ts_ns must be > 0";   return r; }
    if (data_source_ts_ns < event_ts_ns)      { r.error_msg = "data_source_ts < event_ts (PIT)";    return r; }
    if (ingestion_ts_ns < data_source_ts_ns)  { r.error_msg = "ingestion_ts < data_source_ts (PIT)"; return r; }
    if (as_of_ts_ns < ingestion_ts_ns)        { r.error_msg = "as_of_ts < ingestion_ts (PIT)";      return r; }
    r.data_source_ts_present = true;
    r.event_ts_present       = true;
    r.is_upstream_payload    = (ds_origin != DataSourceTsOrigin::IngestionFallback);
    r.valid                  = true;
    return r;
}

// ---------------------------------------------------------------------------
// 6. Compile-time ABI 锁 (ADR-008 + p0_01_goalserve_devig.hpp 对齐)
// ---------------------------------------------------------------------------
static_assert(kBookmakerIds.size()   == 8U);
static_assert(kBookmakerNames.size() == kBookmakerIds.size());
static_assert(kBookmakerIds[0] ==  14, "ABI: [0]=10Bet/14");
static_assert(kBookmakerIds[1] ==  15, "ABI: [1]=WilliamHill/15");
static_assert(kBookmakerIds[2] ==  16, "ABI: [2]=bet365/16");
static_assert(kBookmakerIds[3] ==  17, "ABI: [3]=Marathon/17");
static_assert(kBookmakerIds[4] ==  18, "ABI: [4]=Unibet/18");
static_assert(kBookmakerIds[5] ==  65, "ABI: [5]=BetVictor/65");
static_assert(kBookmakerIds[6] == 105, "ABI: [6]=1xBet/105");
static_assert(kBookmakerIds[7] == 144, "ABI: [7]=Betano/144");
static_assert(kMinValidBookmakers == 3U, "ADR-008 MIN_BOOKMAKERS=3");

}  // namespace stcpp::data::goalserve
