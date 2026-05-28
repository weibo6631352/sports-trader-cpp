// stcpp/data/goalserve_schema.hpp — Goalserve schema SSOT v0.1 (W6 Wave 29)
//
// Owner: 小段 (goalserve-specialist, #37)  Sprint-2 W6 D-W5-03
//
// 聚合层: 重导出 goalserve_client.hpp 枚举 + URL builder,
// 新增 PayloadType / TsFieldSpec (4 ts 字段位置 SSOT) + BuildUrlFromSchema.
//
// 红线: R-20 UPSTREAM_PAYLOAD 优先; R-33 5 host 闭合 enforce (compile-time assert)

#pragma once

#include "stcpp/data/goalserve_client.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace stcpp::data::goalserve {

// schema 版本标识 (与 ADR-009 v2 对应)
inline constexpr std::string_view kSchemaVersion = "goalserve-schema-v0.1-w6";

// ---------------------------------------------------------------------------
// 1. PayloadType — 4 种 payload 来源 (R-20 ts 字段位置分类)
// ---------------------------------------------------------------------------
enum class PayloadType : std::uint8_t {
    PM_WSS       = 0,  // Polymarket sports WSS 帧
    GS_OddsFeed  = 1,  // Goalserve oddsfeed
    GS_LiveScore = 2,  // Goalserve livescore REST
    GS_Inplay    = 3,  // Goalserve inplay-{sport}.gz
};

inline constexpr std::size_t kNumPayloadTypes = 4;

// ---------------------------------------------------------------------------
// 2. TsFieldSpec — 4 ts UPSTREAM_PAYLOAD 字段位置说明 (R-20, 按 PayloadType)
// ---------------------------------------------------------------------------
struct TsFieldSpec {
    std::string_view primary_field;    // 首选 data_source_ts 字段名
    std::string_view secondary_field;  // 次选 (首选缺失时)
    std::string_view tertiary_field;   // fallback (应告警, IngestionFallback)
};

[[nodiscard]] constexpr TsFieldSpec GetTsFieldSpec(PayloadType pt) noexcept {
    switch (pt) {
        case PayloadType::PM_WSS:
            return {"ts",               "timestamp",         "ingestion_local_now"};
        case PayloadType::GS_OddsFeed:
            return {"scores@ts",        "match@last_update", "ingestion_local_now"};
        case PayloadType::GS_LiveScore:
            return {"match@last_update","http_date_header",  "ingestion_local_now"};
        case PayloadType::GS_Inplay:
            return {"scores@ts",        "match@last_update", "ingestion_local_now"};
    }
    return {"", "", ""};
}

// ---------------------------------------------------------------------------
// 3. HostBaseUrl — 委托 HostBaseStatic (SSOT, prefer_https=true)
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr std::string_view HostBaseUrl(GoalserveHost h) noexcept {
    return HostBaseStatic(h, true);
}

// ---------------------------------------------------------------------------
// 4. BuildUrlFromSchema — 按 (host, sport, endpoint) 构造 URL
//    仅用于测试 / offline tooling; 生产路径用 GoalserveClient::BuildUrl.
// ---------------------------------------------------------------------------
[[nodiscard]] inline std::string BuildUrlFromSchema(GoalserveHost     host,
                                                    GoalserveSport    sport,
                                                    GoalserveEndpoint endpoint,
                                                    std::string_view  key = "") {
    GoalserveClient::Config cfg;
    cfg.key          = std::string(key.empty() ? "KEY_PLACEHOLDER" : key);
    cfg.prefer_https = true;
    cfg.gzip         = false;
    GoalserveClient client(cfg);
    UrlSpec spec{};
    spec.host     = host;
    spec.sport    = sport;
    spec.endpoint = endpoint;
    spec.json     = false;
    return client.BuildUrl(spec);
}

// ---------------------------------------------------------------------------
// 5. Compile-time 闭合校验 (ADR-009 v2)
// ---------------------------------------------------------------------------
static_assert(kNumSports == 8,         "GoalserveSport must have 8 values");
static_assert(kNumHosts  == 5,         "GoalserveHost must have 5 values");
static_assert(kAllTimeStatus.size() == 11U, "kAllTimeStatus must have 11 values (0-9 + 99)");
static_assert(static_cast<std::uint8_t>(TimeStatus::Removed) == 99U,
              "TimeStatus::Removed must == 99");
static_assert(kNumPayloadTypes == 4U,  "PayloadType must have 4 values");

}  // namespace stcpp::data::goalserve
