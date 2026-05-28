// stcpp/ml/feature_snapshot.hpp — FeatureSnapshot v0.1 (W4 Wave 20 小邓)
//
// 落:
//   xiaodeng-ml-data-infra-v1.md §1.3 (features/snapshot table schema)
//   xiaodeng-ml-roadmap-v2.md §ML-R1..8 (ML 红线)
//   data-contract-v1.md §2.3 (4 ts PIT)
//   laotang-audit-schema-v1.1.md §2.x (header 4 ts 对齐)
//
// 红线:
//   ML-R1  ML 不进 RM 决策路径 — 本 struct 是只读快照, 没有 mutator 接 hot path
//   ML-R2  paper 期不进 OrderIntent — FeatureSnapshot 由 hook 旁路抓拍, 不喂 RM
//   ML-R5  推理走 ONNX/Treelite — header 不依赖 pybind11 / Python (POD only)
//   ML-R8  必带 model_id + feature_snapshot_id — 本 struct 含 feature_snapshot_id (老周 PIT)
//   R-20   4 ts + feature_snapshot_id 必带, ts<=0 → invalid
//   R-11   FeatureSnapshot 自身 mode-agnostic, paper / live 区分由 hook 的 WAL 路径承担
//
// 设计:
//   - immutable POD (trivially copyable), ML pipeline 零拷贝消费
//   - 32 feature 单精度 float (训练用 double 不必要, ONNX/LGBM 都吃 float32)
//   - 满足 infra::wal::WalRecord concept (4 ts getter + audit_id + serialize_into)
//   - sentinel: 缺失字段填 std::numeric_limits<float>::quiet_NaN(),
//               Python 训练侧 isnan() 直接识别 (LightGBM 支持 NaN sparse)
//
// 不耻下问:
//   - signal context 字段 @小程 (P0-01 spec)
//   - RM 决策 / state 字段 @老韩 (RmState / consec_loss)
//   - SlippageModel 输出字段 @小肖
//   - GameState / LiveSection @老彭 (Goalserve)
//   - feature_snapshot_id 64-bit 生成 @老周 (PIT 工程)

#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>

#include "stcpp/infra/wal/wal_record_header.hpp"   // 4 ts WAL header offset 校验同源
#include "stcpp/infra/wal/wal_writer.hpp"          // WalRecord concept

namespace stcpp::ml {

// ---------- 32 FeatureName enum (与 v1 doc §1.3 + 派单 32 字段对齐) -----------
//
// 顺序锁死: 训练侧 column index = enum 值. 新增 feature → append, 不插中间.
// 与 Parquet schema 列顺序 1:1 (小田 DWH W5 接).

enum class FeatureName : std::uint8_t {
    // --- Polymarket 报价 / 簿深 (0..3) ---
    PM_mid_bid                  = 0,    // PM YES bid (USDC / dollar prob)
    PM_mid_ask                  = 1,    // PM YES ask
    PM_book_depth_top3_yes      = 2,    // top-3 levels yes 总 USDC
    PM_book_depth_top3_no       = 3,    // top-3 levels no 总 USDC

    // --- Pinnacle no-vig (4..5) ---
    Pinnacle_p_yes_fair         = 4,    // multiplicative no-vig fair prob
    Pinnacle_overround          = 5,    // p_yes_raw + p_no_raw - 1

    // --- 信号 / 滑点 (6..9) ---
    edge_bps                    = 6,    // signal edge 单位 bps
    kelly_full                  = 7,    // Kelly full (未 ·0.25)
    expected_fill_rate          = 8,    // SlippageModel
    slippage_bps                = 9,    // SlippageModel.compute

    // --- LiveSection / GameState (10..15) ---
    live_section                = 10,   // LiveSection enum cast → float (Live=0..Future=4)
    game_state                  = 11,   // GameState bitmask: bit0=live bit1=ended bit2=delayed
    kickoff_seconds_until       = 12,   // (kickoff_ts - now) / 1e9, 负值 = 已开赛
    inplay_minutes              = 13,   // 已比赛分钟 (开赛后)
    score_home                  = 14,
    score_away                  = 15,

    // --- 微观结构 (16..21) ---
    period                      = 16,   // 当前 period / quarter / inning (sport dependent)
    vol_24h                     = 17,   // 该 market 24h 成交量 (USDC)
    vol_1h                      = 18,
    vol_5m                      = 19,
    spread_bps                  = 20,   // (ask - bid) / mid * 10000
    quote_half_life_ms          = 21,   // 报价稳定度估计

    // --- RM 状态 (22..26) ---
    rm_state                    = 22,   // RmState enum (RUNNING=0..DRAIN=4)
    rm_consec_loss              = 23,
    rm_bankroll                 = 24,
    rm_exposure_pct             = 25,   // market_exposure_usdc / bankroll
    signal_confidence           = 26,   // clamp(edge/0.10, 0, 1)

    // --- 不确定性 / CI (27..31) ---
    ci_lower                    = 27,   // edge CI 下界 (小肖 W5)
    ci_upper                    = 28,
    N_pretrade                  = 29,   // pretrade observation count
    N_inplay                    = 30,
    N_settled                   = 31,   // 该 market 历史结算样本数 (drift)
};

inline constexpr std::size_t kFeatureCount = 32;

[[nodiscard]] constexpr std::string_view to_string(FeatureName f) noexcept {
    switch (f) {
        case FeatureName::PM_mid_bid:              return "PM_mid_bid";
        case FeatureName::PM_mid_ask:              return "PM_mid_ask";
        case FeatureName::PM_book_depth_top3_yes:  return "PM_book_depth_top3_yes";
        case FeatureName::PM_book_depth_top3_no:   return "PM_book_depth_top3_no";
        case FeatureName::Pinnacle_p_yes_fair:     return "Pinnacle_p_yes_fair";
        case FeatureName::Pinnacle_overround:      return "Pinnacle_overround";
        case FeatureName::edge_bps:                return "edge_bps";
        case FeatureName::kelly_full:              return "kelly_full";
        case FeatureName::expected_fill_rate:      return "expected_fill_rate";
        case FeatureName::slippage_bps:            return "slippage_bps";
        case FeatureName::live_section:            return "live_section";
        case FeatureName::game_state:              return "game_state";
        case FeatureName::kickoff_seconds_until:   return "kickoff_seconds_until";
        case FeatureName::inplay_minutes:          return "inplay_minutes";
        case FeatureName::score_home:              return "score_home";
        case FeatureName::score_away:              return "score_away";
        case FeatureName::period:                  return "period";
        case FeatureName::vol_24h:                 return "vol_24h";
        case FeatureName::vol_1h:                  return "vol_1h";
        case FeatureName::vol_5m:                  return "vol_5m";
        case FeatureName::spread_bps:              return "spread_bps";
        case FeatureName::quote_half_life_ms:      return "quote_half_life_ms";
        case FeatureName::rm_state:                return "rm_state";
        case FeatureName::rm_consec_loss:          return "rm_consec_loss";
        case FeatureName::rm_bankroll:             return "rm_bankroll";
        case FeatureName::rm_exposure_pct:         return "rm_exposure_pct";
        case FeatureName::signal_confidence:       return "signal_confidence";
        case FeatureName::ci_lower:                return "ci_lower";
        case FeatureName::ci_upper:                return "ci_upper";
        case FeatureName::N_pretrade:              return "N_pretrade";
        case FeatureName::N_inplay:                return "N_inplay";
        case FeatureName::N_settled:               return "N_settled";
    }
    return "unknown";
}

// ---------- FeatureSnapshot POD (immutable; satisfies WalRecord concept) -----
//
// Layout (logical):
//   - 4 ts (R-20) — 与 audit/header 同 offset 16/24/32/40 语义
//   - feature_snapshot_id (u64 PIT 锚, ML-R8 复盘 key)
//   - signal_id (P0_01 等; SignalId enum cast u8)
//   - market_id (fixed 32B)
//   - audit_id (ULID 16B) — caller 填, 与 RM/signer/matcher 同一笔决策同 id
//   - 32 float feature
//
// 缺失语义: 字段值 = NaN 表示缺失 (LightGBM / XGBoost native missing support).

inline constexpr std::size_t kMarketIdMax = 32;

struct FeatureSnapshot {
    // ---- R-20 4 ts (与 WAL header v2 offset 16/24/32/40 一致) ----
    std::int64_t event_ts        = 0;
    std::int64_t data_source_ts  = 0;
    std::int64_t ingestion_ts    = 0;
    std::int64_t as_of_ts        = 0;

    // ---- PIT 锚 (ML-R8) ----
    std::uint64_t feature_snapshot_id = 0;   // 老周 PIT: hash(market_id || as_of_ts || signal_id)

    // ---- 业务键 ----
    std::array<std::uint8_t, 16>   audit_id_bytes{};   // ULID, RM/signer/matcher 同链
    std::uint8_t                   signal_id_u8 = 0;   // SignalId enum 值 cast (避免 header 引 signal_iface)
    std::array<char, kMarketIdMax> market_id{};

    // ---- 32 feature (float32, sparse NaN = missing) ----
    std::array<float, kFeatureCount> features{};

    // ---- ctor: 默认所有 feature = NaN (sparse) ----
    FeatureSnapshot() noexcept {
        for (auto& v : features) v = std::numeric_limits<float>::quiet_NaN();
    }

    // ---- accessor by enum (零开销, 编译器 inline) ----
    [[nodiscard]] float  get(FeatureName f) const noexcept {
        return features[static_cast<std::size_t>(f)];
    }
    void set(FeatureName f, float v) noexcept {
        features[static_cast<std::size_t>(f)] = v;
    }

    // ---- WalRecord concept 适配 ----
    [[nodiscard]] std::int64_t event_ts_ns()       const noexcept { return event_ts; }
    [[nodiscard]] std::int64_t data_source_ts_ns() const noexcept { return data_source_ts; }
    [[nodiscard]] std::int64_t ingestion_ts_ns()   const noexcept { return ingestion_ts; }
    [[nodiscard]] std::int64_t as_of_ts_ns()       const noexcept { return as_of_ts; }
    [[nodiscard]] std::array<std::uint8_t, 16> audit_id() const noexcept { return audit_id_bytes; }

    // serialize_into: POD memcpy (W4 stub, W5 切 Parquet flat).
    // 注: 单 record W5 仍走 WAL framework, Parquet 由小田 DWH 离线消费 WAL bytes 再翻译.
    [[nodiscard]] std::size_t serialize_into(std::span<std::byte> out) const noexcept {
        const std::size_t n = sizeof(FeatureSnapshot);
        if (out.size() < n) return 0;
        std::memcpy(out.data(), this, n);
        return n;
    }
    static constexpr std::size_t max_serialized_size() noexcept {
        return sizeof(FeatureSnapshot);
    }

    // ---- 4 ts 不等式自检 (R-20, 不调 framework PIT, 仅 caller 入口校验) ----
    [[nodiscard]] bool ts_chain_ok() const noexcept {
        return (event_ts        >  0)
            && (data_source_ts >= event_ts)
            && (ingestion_ts   >= data_source_ts)
            && (as_of_ts       >= ingestion_ts);
    }

    // ---- 全字段填充检查 (debug 用: NaN count) ----
    [[nodiscard]] std::size_t nan_count() const noexcept {
        std::size_t n = 0;
        for (auto v : features) if (std::isnan(v)) ++n;
        return n;
    }
    [[nodiscard]] bool all_filled() const noexcept { return nan_count() == 0; }
};

// 单条 ≤ u16 LEN (framework 约束)
static_assert(sizeof(FeatureSnapshot) <= 65535,
              "FeatureSnapshot 单条 ≤ u16 LEN");
// concept 静态自检
static_assert(stcpp::infra::wal::WalRecord<FeatureSnapshot>,
              "FeatureSnapshot 必须满足 WalRecord concept");

}  // namespace stcpp::ml
