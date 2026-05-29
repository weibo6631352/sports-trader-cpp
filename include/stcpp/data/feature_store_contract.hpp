// stcpp/data/feature_store_contract.hpp — Feature Store Consumer Contract v1.0
//
// Owner: 小田 (dwh-analyst, #24)
// last_review: 2026-05-29
//
// 目的:
//   定义 feature store 的归一化中间表示 (FeatureStoreGameRow + FeatureStoreBookRow),
//   供 Goalserve adapter (小段 #37) 和 orderbook adapter (小冯) 实现消费契约.
//   不实现 adapter 本身 — 只定义接口约束.
//
// 关联:
//   docs/RESEARCH/xiaotian-feature-store-schema-v1.md   (schema SSOT)
//   include/stcpp/data/goalserve_record.hpp             (GameRecord / OddsRecord)
//   include/stcpp/microstructure/orderbook.hpp          (OrderBookSnapshot)
//   include/stcpp/data/data_contract.hpp                (kBookmakerIds ABI)
//   include/stcpp/data/parquet_writer.hpp               (Parquet 写入 ABI)
//
// 红线:
//   R-20: 4 ts 不等式 event <= data_source <= ingestion <= as_of; 禁本地 now() 替代上游 ts
//   PIT:  FeatureStoreGameRow.as_of_ts_ns <= 消费决策时刻; 禁回填历史行
//   vendor-agnostic: 不硬编码 Goalserve / Polymarket 字段名; 通过 adapter 归一化
//   ABI 锁: kFeatureStoreSchemaVersion bump 需 ADR + @小段 + @小冯 + @小邓 ack
//
// 不耻下问:
//   - market_id -> match_id 映射 @小冯 + @小段 (FS-01)
//   - orderbook_snapshot sport/event_date 注入 @小冯 + @老李 (FS-02)
//   - PIT join 层 (DuckDB vs C++) @小蒋 + @小梁 (FS-03)
//   - bookmaker 第 9 家 schema bump @老彭 (FS-04)
//
// ============================================================================

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include "stcpp/data/goalserve_record.hpp"   // FourTs, GoalserveSport, TimeStatus, ScorePair
#include "stcpp/data/data_contract.hpp"       // kNumBookmakers, kBookmakerAbiVersion

namespace stcpp::data::feature_store {

// ---------------------------------------------------------------------------
// Schema 版本 ABI 锁
// 变更: ADR + @小段 + @小冯 + @小邓 ack → bump kFeatureStoreSchemaVersion
// ---------------------------------------------------------------------------
inline constexpr std::string_view kFeatureStoreSchemaVersion = "fs-schema-v1.0";

// 分区键数量 (sport + event_date + market_type)
inline constexpr std::size_t kNumPartitionKeys = 3;

// ---------------------------------------------------------------------------
// 1. BookmakerOddsOptional — 单家赔率 (NaN = 缺失, 不阻塞 game_snapshot 写入)
//
// vendor-agnostic: 列顺序与 kBookmakerIds ABI 锁一致 (data_contract.hpp).
// NaN 语义: 本次 poll 无该家 OddsRecord → odds_yes/odds_no = NaN, valid = false.
// ---------------------------------------------------------------------------
struct BookmakerOddsOptional {
    double odds_yes = std::numeric_limits<double>::quiet_NaN();
    double odds_no  = std::numeric_limits<double>::quiet_NaN();
    bool   valid    = false;

    [[nodiscard]] bool is_present() const noexcept {
        return valid && (odds_yes == odds_yes) && (odds_no == odds_no);  // NaN != NaN
    }
};

// ---------------------------------------------------------------------------
// 2. FeatureStoreGameRow — Goalserve adapter 归一化输出 (game_snapshot 行)
//
// 小段 adapter 实施要求 (§5.1 消费契约):
//   - 输入: GameRecord + 可选 MultiBookOddsRecord (同 match, 同 poll)
//   - 输出: FeatureStoreGameRow (1 行/match/poll)
//   - match_id: 去掉 vendor 前缀后的纯 ID
//   - sport: GoalserveSport enum string name (via SportInplaySlug / GetSportAlias)
//   - event_date_epoch_days: from scheduled_ts_ns (优先) or event_ts_ns / 86400e9
//   - as_of_ts_ns: ingestion_ts_ns 不得用未来 ts
//   - bm_slots: 无 OddsRecord 则 is_present()=false, 不阻塞写入
//
// PIT 约束 (红线):
//   as_of_ts_ns <= adapter 调用时刻 now(); 禁止回填历史行
// ---------------------------------------------------------------------------
struct FeatureStoreGameRow {
    // ---- R-20 4 ts (ns) ----
    std::int64_t event_ts_ns       = 0;  // 比赛事件 ts (scores@ts 或 last_update parse)
    std::int64_t data_source_ts_ns = 0;  // 数据源生成 ts (UPSTREAM_PAYLOAD 优先)
    std::int64_t ingestion_ts_ns   = 0;  // 本地收到 payload 时刻
    std::int64_t as_of_ts_ns       = 0;  // PIT 锚 = ingestion_ts_ns (决策前不可更新)
    goalserve::DataSourceTsOrigin ds_origin = goalserve::DataSourceTsOrigin::PayloadScoresTs;

    // ---- 分区键 ----
    // sport: GoalserveSport enum 对应的可读名称 (adapter 负责转换, 不硬编码字符串)
    std::string sport;          // "Soccer" / "Basketball" / "AmFootball" / ...
    std::int32_t event_date_epoch_days = 0;  // days since 1970-01-01 (DATE32)
    std::string market_type;    // "Moneyline" / "Totals" / "Spreads"

    // ---- 业务键 ----
    std::string match_id;       // Goalserve match id (去 vendor 前缀)
    std::string league_id;      // gid / static_id

    // ---- 团队 ----
    std::string home_team;
    std::string away_team;

    // ---- 比分 ----
    std::int32_t score_home_total = 0;
    std::int32_t score_away_total = 0;
    // 分节得分: 12 槽 (篮球4节+OT3, 棒球9局+加局, 网球5盘, 排球5盘)
    // -1 = 未开始/无效
    std::array<std::int32_t, 12> score_home_periods{{}};
    std::array<std::int32_t, 12> score_away_periods{{}};
    std::uint8_t used_periods          = 0;
    std::uint8_t last_completed_period = 0;

    // ---- 比赛状态 ----
    goalserve::TimeStatus time_status = goalserve::TimeStatus::NotStarted;
    std::uint8_t  period      = 0;           // 当前节 (1-based), 0=无
    std::int32_t  elapsed_sec = -1;          // 当前节已用秒, -1=无
    std::int64_t  scheduled_ts_ns = 0;       // 排定开赛 ts, 0=无

    // ---- Bookmaker 赔率 (可选, 来自同 match OddsRecord) ----
    // ABI 锁: slots[i] <-> kBookmakerIds[i] (data_contract.hpp)
    // 顺序: [10bet, williamhill, bet365, marathon, unibet, betvictor, 1xbet, betano]
    std::array<BookmakerOddsOptional, goalserve::kNumBookmakers> bm_slots{};
    std::string bm_market_id;  // 1x2 / OU_2.5 / AH_-0.5; 空串=无赔率

    // ---- ABI 版本 (写入 Parquet metadata 用) ----
    std::string_view bm_abi_version = goalserve::kBookmakerAbiVersion;
    std::string_view schema_version = kFeatureStoreSchemaVersion;

    // ---- R-20 4 ts 自检 ----
    [[nodiscard]] bool ts_chain_ok() const noexcept {
        return (event_ts_ns        >  0)
            && (data_source_ts_ns >= event_ts_ns)
            && (ingestion_ts_ns   >= data_source_ts_ns)
            && (as_of_ts_ns       >= ingestion_ts_ns);
    }

    // ---- PIT: as_of_ts 是否严格 <= now_ns (调用方传入) ----
    [[nodiscard]] bool pit_ok(std::int64_t now_ns) const noexcept {
        return (now_ns > 0) && (as_of_ts_ns <= now_ns);
    }

    // ---- valid_bm_count (for kMinValidBookmakers = 3 check) ----
    [[nodiscard]] std::size_t valid_bm_count() const noexcept {
        std::size_t n = 0;
        for (const auto& sl : bm_slots) if (sl.is_present()) ++n;
        return n;
    }

    // ---- 从 GameRecord + 可选 FourTs 填 4 ts (adapter 使用) ----
    static FeatureStoreGameRow from_game_record(
        const goalserve::GameRecord& gr,
        std::string_view             market_type_sv,
        std::int64_t                 as_of_ts_ns_override = 0) noexcept
    {
        FeatureStoreGameRow r;
        // 4 ts
        r.event_ts_ns       = gr.ts.event_ts_ns;
        r.data_source_ts_ns = gr.ts.data_source_ts_ns;
        r.ingestion_ts_ns   = gr.ts.ingestion_ts_ns;
        // as_of_ts: 若 override 传入则用 override, 否则用 GameRecord.as_of_ts
        r.as_of_ts_ns = (as_of_ts_ns_override > 0)
                       ? as_of_ts_ns_override
                       : gr.ts.as_of_ts_ns;
        r.ds_origin = gr.ts.ds_origin;

        // 分区键 sport 来自 enum (避免硬编码字符串)
        r.sport       = std::string(goalserve::SportInplaySlug(gr.sport));
        r.market_type = std::string(market_type_sv);

        // event_date: 优先 scheduled_ts_ns, fallback event_ts_ns
        const std::int64_t date_ts_ns =
            (gr.scheduled_ts_ns.has_value() && *gr.scheduled_ts_ns > 0)
            ? *gr.scheduled_ts_ns
            : gr.ts.event_ts_ns;
        // days since epoch = ns / (86400 * 1e9)
        r.event_date_epoch_days = static_cast<std::int32_t>(
            date_ts_ns / static_cast<std::int64_t>(86400LL * 1'000'000'000LL));

        // 业务键
        r.match_id  = gr.match_id;
        r.league_id = gr.league_id;
        r.home_team = gr.home_team;
        r.away_team = gr.away_team;

        // 比分
        r.score_home_total = gr.score.home_total;
        r.score_away_total = gr.score.away_total;
        r.score_home_periods = gr.score.home_periods;
        r.score_away_periods = gr.score.away_periods;
        r.used_periods          = gr.score.used_periods;
        r.last_completed_period = gr.score.last_completed_period;

        // 状态
        r.time_status = gr.status;
        if (gr.period.has_value())      r.period      = *gr.period;
        if (gr.elapsed_sec.has_value()) r.elapsed_sec = *gr.elapsed_sec;
        if (gr.scheduled_ts_ns.has_value()) r.scheduled_ts_ns = *gr.scheduled_ts_ns;

        // bm_slots: 默认全 NaN/false (adapter 后续填充 OddsRecord)
        return r;
    }
};

// ---------------------------------------------------------------------------
// 3. FeatureStoreBookRow — orderbook adapter 归一化输出 (orderbook_snapshot 行)
//
// 小冯 adapter 实施要求 (§5.2 消费契约):
//   - 输入: OrderBookSnapshot + L1Probe (compute_l1_probe 计算)
//   - 输出: FeatureStoreBookRow (1 行/market_id/WSS push, token_side="YES"/"NO")
//   - market_id: Polymarket condition_id / asset_id, 与 CLOB 一致
//   - sport/event_date/market_type: 从外部 market_metadata 注入 (小冯不自行推断)
//   - data_source_ts_ns: WSS payload @ts (ms * 1e6 → ns), 禁本地 now()
//   - bid_p0..ask_s4: NaN = 该 level 无深度 (非 0)
//
// PIT 约束:
//   as_of_ts_ns: strategy 层填写的 evaluate 时刻 (非 ingestion 时刻)
// ---------------------------------------------------------------------------
inline constexpr std::size_t kOrderBookLevels = 5;  // 与 orderbook.hpp kBookDepthLevels 对齐

struct FeatureStoreBookRow {
    // ---- R-20 4 ts (ns) ----
    std::int64_t event_ts_ns       = 0;
    std::int64_t data_source_ts_ns = 0;  // WSS @ts (ms → ns), UPSTREAM_PAYLOAD 优先
    std::int64_t ingestion_ts_ns   = 0;  // 本地收字节流时刻
    std::int64_t as_of_ts_ns       = 0;  // strategy evaluate 时刻 (strategy 层填写)

    // ---- 分区键 (由 market_metadata 注入, 小冯不自行推断) ----
    std::string sport;
    std::int32_t event_date_epoch_days = 0;
    std::string market_type;

    // ---- 业务键 ----
    std::string market_id;    // Polymarket condition_id / asset_id
    std::string token_side;   // "YES" or "NO" (大写)

    // ---- 5 档 bid (0 = best), NaN = level 无深度 ----
    std::array<double, kOrderBookLevels> bid_price{};
    std::array<double, kOrderBookLevels> bid_size_usdc{};

    // ---- 5 档 ask ----
    std::array<double, kOrderBookLevels> ask_price{};
    std::array<double, kOrderBookLevels> ask_size_usdc{};

    // ---- 微观结构派生 (由 compute_l1_probe 填充) ----
    double mid            = std::numeric_limits<double>::quiet_NaN();
    double spread_bps_f   = std::numeric_limits<double>::quiet_NaN();  // (ask0-bid0)/mid*10000
    double top3_depth_usdc = 0.0;
    double tick_size      = 0.01;
    double microprice     = std::numeric_limits<double>::quiet_NaN();
    double imbalance      = std::numeric_limits<double>::quiet_NaN();
    std::int64_t last_trade_ts_ns = 0;

    // ---- ABI ----
    std::string_view schema_version = kFeatureStoreSchemaVersion;

    // ---- R-20 自检 ----
    [[nodiscard]] bool ts_chain_ok() const noexcept {
        return (event_ts_ns        >  0)
            && (data_source_ts_ns >= event_ts_ns)
            && (ingestion_ts_ns   >= data_source_ts_ns)
            && (as_of_ts_ns       >= ingestion_ts_ns);
    }

    // ---- PIT ----
    [[nodiscard]] bool pit_ok(std::int64_t now_ns) const noexcept {
        return (now_ns > 0) && (as_of_ts_ns <= now_ns);
    }

    // ---- best bid/ask (L1) ----
    [[nodiscard]] double best_bid() const noexcept { return bid_price[0]; }
    [[nodiscard]] double best_ask() const noexcept { return ask_price[0]; }

    // ---- level valid check (NaN = 空 level) ----
    [[nodiscard]] bool bid_level_valid(std::size_t i) const noexcept {
        if (i >= kOrderBookLevels) return false;
        const double p = bid_price[i], s = bid_size_usdc[i];
        return (p == p) && (s == s) && (p > 0.0) && (s > 0.0);
    }
    [[nodiscard]] bool ask_level_valid(std::size_t i) const noexcept {
        if (i >= kOrderBookLevels) return false;
        const double p = ask_price[i], s = ask_size_usdc[i];
        return (p == p) && (s == s) && (p > 0.0) && (s > 0.0);
    }
};

// ---------------------------------------------------------------------------
// 4. FeatureStoreValidationResult — adapter 调用 validate() 后的诊断
// ---------------------------------------------------------------------------
struct FeatureStoreValidationResult {
    bool    valid                  = false;
    bool    ts_chain_ok            = false;
    bool    pit_ok                 = false;
    bool    ds_origin_upstream     = false;  // true = PayloadScoresTs / PayloadLastUpdate
    std::string_view error_msg     = "";
};

[[nodiscard]] inline FeatureStoreValidationResult validate_game_row(
    const FeatureStoreGameRow& row,
    std::int64_t               now_ns) noexcept
{
    FeatureStoreValidationResult r{};
    if (!row.ts_chain_ok()) {
        r.error_msg = "4 ts chain violation (R-20)";
        return r;
    }
    r.ts_chain_ok = true;
    if (!row.pit_ok(now_ns)) {
        r.error_msg = "PIT violation: as_of_ts > now_ns";
        return r;
    }
    r.pit_ok = true;
    if (row.match_id.empty()) {
        r.error_msg = "match_id must not be empty";
        return r;
    }
    if (row.sport.empty()) {
        r.error_msg = "sport must not be empty";
        return r;
    }
    r.ds_origin_upstream =
        (row.ds_origin != goalserve::DataSourceTsOrigin::IngestionFallback);
    r.valid = true;
    return r;
}

[[nodiscard]] inline FeatureStoreValidationResult validate_book_row(
    const FeatureStoreBookRow& row,
    std::int64_t               now_ns) noexcept
{
    FeatureStoreValidationResult r{};
    if (!row.ts_chain_ok()) {
        r.error_msg = "4 ts chain violation (R-20)";
        return r;
    }
    r.ts_chain_ok = true;
    if (!row.pit_ok(now_ns)) {
        r.error_msg = "PIT violation: as_of_ts > now_ns";
        return r;
    }
    r.pit_ok = true;
    if (row.market_id.empty()) {
        r.error_msg = "market_id must not be empty";
        return r;
    }
    if (row.token_side != "YES" && row.token_side != "NO") {
        r.error_msg = "token_side must be YES or NO";
        return r;
    }
    // data_source_ts 来自 WSS @ts; IngestionFallback 不允许 (R-20)
    // 无 ds_origin 字段在 book row — 依赖外部填写约束, 此处仅检查 ts
    r.ds_origin_upstream = true;  // book row 默认来自 WSS UPSTREAM
    r.valid = true;
    return r;
}

// ---------------------------------------------------------------------------
// 5. Compile-time ABI 锁
// ---------------------------------------------------------------------------
static_assert(kFeatureStoreSchemaVersion == "fs-schema-v1.0",
              "feature store schema version ABI must be fs-schema-v1.0");
static_assert(kOrderBookLevels == 5,
              "orderbook levels must match orderbook.hpp kBookDepthLevels = 5");
static_assert(goalserve::kNumBookmakers == 8,
              "kNumBookmakers must be 8 (data_contract.hpp ABI)");

}  // namespace stcpp::data::feature_store
