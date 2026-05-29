// stcpp/data/goalserve_record.hpp — Goalserve parsed record (POD)
//
// Owner: 小段 (goalserve-specialist)  +  4-ts canon @老唐 (audit schema v1.1)
// Sprint-2 W4 Wave 19 — record schema 闭合, W5 接 parser 时填 4 ts.
//
// 落:
//   docs/ADR/2026-05-28-gm-redline-data-source-timestamping.md  (R-20 4 ts)
//   docs/GOALSERVER/inplay-feed-new.txt                          (11 time_status)
//   小段 v3 §5 (game vs odds 分离, score 多字段)
//
// 红线:
//   R-20: 4 ts 不等式  event ≤ data_source ≤ ingestion ≤ as_of  (≤ now)
//   R-20: UPSTREAM_PAYLOAD 优先 — data_source_ts 来自 scores@ts (ms epoch),
//         禁 client 用本地 now() 顶替.
//   R-12: record 是 POD, 内存可在 vCPU3 worker → vCPU2 hot 队列零拷贝传递.
//
// 不耻下问:
//   - 4 ts 字段位置 / audit_id binding → @老唐
//   - WAL 落盘 schema 对齐 → @老王 (wal_record_header.hpp 已 4 ts 同 layout)
//
// ============================================================================

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "stcpp/data/goalserve_client.hpp"

namespace stcpp::data::goalserve {

// ---------------------------------------------------------------------------
// 1. FourTs — 4 时间戳契约 POD (R-20)
//
// 严格不等式 (PIT chain, 与 stcpp::infra::wal::pit::AssertChain 一致):
//   event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts ≤ now()
//
// source 标识 (UPSTREAM_PAYLOAD 优先, 用于 audit + 告警):
//   PayloadScoresTs     : <scores ts="..."> 直采, ms epoch (主)
//   PayloadLastUpdate   : <match last_update="dd.MM.yyyy HH:mm"> parse
//   IngestionFallback   : payload 无 ts, 本地 now (告警, 算违反 R-20 但允许带 flag)
// ---------------------------------------------------------------------------
enum class DataSourceTsOrigin : std::uint8_t {
    PayloadScoresTs = 0,    // 优先, R-20 合规
    PayloadLastUpdate = 1,  // 次优, R-20 合规
    IngestionFallback = 2,  // R-20 违规 (告警), 仅当 payload 完全无 ts
};

struct FourTs {
    std::int64_t event_ts_ns = 0;
    std::int64_t data_source_ts_ns = 0;
    std::int64_t ingestion_ts_ns = 0;
    std::int64_t as_of_ts_ns = 0;
    DataSourceTsOrigin ds_origin = DataSourceTsOrigin::PayloadScoresTs;

    // PIT chain (与 wal::pit::AssertChain 等价, 内联无分支)
    [[nodiscard]] constexpr bool IsMonotonic() const noexcept {
        return (event_ts_ns > 0) && (data_source_ts_ns >= event_ts_ns) &&
               (ingestion_ts_ns >= data_source_ts_ns) && (as_of_ts_ns >= ingestion_ts_ns);
    }
};

// ---------------------------------------------------------------------------
// 2. ScorePair — 分节得分 (足球/篮球/棒球分节, 网球分盘, 排球分盘)
//
// 容量 12 槽: 篮球 4 节 + OT3 = 7; 棒球 9 局 + 加局; 网球 5 盘 (BO5); 排球 5 盘.
// 实际节数 used_periods, 已结束 last_completed_period.
// ---------------------------------------------------------------------------
struct ScorePair {
    std::array<std::int32_t, 12> home_periods{{}};
    std::array<std::int32_t, 12> away_periods{{}};
    std::int32_t home_total = 0;
    std::int32_t away_total = 0;
    std::uint8_t used_periods = 0;           // 已写入的节数
    std::uint8_t last_completed_period = 0;  // 已结束节 (用于分节盘口结算)
};

// ---------------------------------------------------------------------------
// 3. GameRecord — 一局比赛快照 (livescore + inplay 通用)
//
// W5 解析时: parse → 填 FourTs (data_source_ts 优先 scores@ts) → 传 strategy 层.
// ---------------------------------------------------------------------------
struct GameRecord {
    FourTs ts{};  // R-20 4 ts
    GoalserveSport sport = GoalserveSport::Soccer;
    std::string match_id;   // Goalserve match id (string, 跨 sport)
    std::string league_id;  // gid / static_id
    std::string home_team;
    std::string away_team;
    ScorePair score{};
    TimeStatus status = TimeStatus::NotStarted;

    // 时刻属性 (inplay 才有)
    std::optional<std::uint8_t> period;           // 当前节 (1-based)
    std::optional<std::int32_t> elapsed_sec;      // 当前节已用秒
    std::optional<std::int64_t> scheduled_ts_ns;  // 比赛排定开始 (来自 fixture)

    // R-20 origin diagnostic
    [[nodiscard]] bool RespectsR20() const noexcept {
        return ts.IsMonotonic() && ts.ds_origin != DataSourceTsOrigin::IngestionFallback;
    }

    // 终态判定 (与 IsTerminal(TimeStatus) 等价, 便利包装)
    [[nodiscard]] bool IsTerminal() const noexcept { return ::stcpp::data::goalserve::IsTerminal(status); }
};

// ---------------------------------------------------------------------------
// 4. OddsRecord — 一笔盘口报价 (一个 bookmaker 一个 outcome)
//
// 用于 inplay-{sport}.gz / getodds/soccer?cat=*_10 解析后落 strategy / WAL.
// ---------------------------------------------------------------------------
struct OddsRecord {
    FourTs ts{};  // R-20 4 ts (与 GameRecord 同一 ts 源)
    GoalserveSport sport = GoalserveSport::Soccer;
    std::string match_id;
    std::int32_t bookmaker_id = 0;
    std::string market_id;           // 1x2 / OU_2.5 / AH_-0.5 / etc.
    std::string outcome;             // home / draw / away / over / under / line
    double value = 0.0;              // 十进制赔率 (decimal odds)
    std::optional<double> handicap;  // 让分 / 大小盘的盘口数值
    bool active = true;              // bm 标记 closed/suspended 时 false

    [[nodiscard]] bool RespectsR20() const noexcept {
        return ts.IsMonotonic() && ts.ds_origin != DataSourceTsOrigin::IngestionFallback;
    }
};

// ---------------------------------------------------------------------------
// 5. ParseBatch — 一次 fetch 解析后的批量 record
// ---------------------------------------------------------------------------
struct ParseBatch {
    std::int64_t scores_ts_ms = 0;  // <scores ts="..."> 直采
    std::int64_t next_ts_ms = 0;    // 下一次增量传入
    std::vector<GameRecord> games;
    std::vector<OddsRecord> odds;
};

// ---------------------------------------------------------------------------
// 6. compile-time invariants (与老唐 audit schema 对齐)
// ---------------------------------------------------------------------------
static_assert(sizeof(FourTs) <= 64, "FourTs 应紧凑 (8*4 + 1 + pad), 避免 record 膨胀");
static_assert(static_cast<std::uint8_t>(TimeStatus::Removed) == 99,
              "TimeStatus::Removed 必须 = 99 (docs/GOALSERVER/inplay-feed-new.txt L26)");

}  // namespace stcpp::data::goalserve
