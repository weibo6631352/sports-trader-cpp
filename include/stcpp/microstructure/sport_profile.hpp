// stcpp/microstructure/sport_profile.hpp — 8 sport × 4 inplay phase profile v0.1
//
// Owner: 小袁 (quant-microstructure)
// Sprint-2 W4 Wave 20
//
// 落:
//   docs/RESEARCH/xiaoyuan-microstructure-v1.md §1.2 / §1.3 (5 sport 实测 + gameday only)
//   docs/RESEARCH/xiaoyuan-fill-rate-model-v0.1.md (v0.1 spec)
//   include/stcpp/data/goalserve_client.hpp (GoalserveSport 8 enum)
//
// 红线:
//   R-20  本 header 仅暴露 constexpr table + lookup, 无 I/O / now()
//   R-7   header-only, mode-agnostic
//   W6    实测校准: 当前数据来自 §1.3 gameday only 3 sport (NBA / MLB / Tennis 真值);
//         Soccer / NFL / Esports / Hockey / Volleyball / Baseball 为 alpha 占位,
//         paper 跑 1 周后小蒋回归 actual_fill / predicted_fill, 我重校
//
// ============================================================================

#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace stcpp::microstructure {

// ---------------------------------------------------------------------------
// 1. Sport enum (与 stcpp::data::goalserve::GoalserveSport 顺序 + 数值一一对应)
// ---------------------------------------------------------------------------
//
// 注: 此 lib 不依赖 goalserve_client.hpp (避免反向耦合), 但枚举值必须保持一致.
// 单测会 static_assert 两 enum 数值对齐.
enum class Sport : std::uint8_t {
    Soccer            = 0,
    Basketball        = 1,
    Tennis            = 2,
    Volleyball        = 3,
    AmericanFootball  = 4,
    Esports           = 5,
    Hockey            = 6,
    Baseball          = 7,
};

inline constexpr std::size_t kNumSports = 8;

[[nodiscard]] constexpr std::string_view sport_name(Sport s) noexcept {
    switch (s) {
        case Sport::Soccer:           return "Soccer";
        case Sport::Basketball:       return "Basketball";
        case Sport::Tennis:           return "Tennis";
        case Sport::Volleyball:       return "Volleyball";
        case Sport::AmericanFootball: return "AmericanFootball";
        case Sport::Esports:          return "Esports";
        case Sport::Hockey:           return "Hockey";
        case Sport::Baseball:         return "Baseball";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// 2. InplayPhase — 比赛 4 阶段 (与老韩 MarketState 5 档同语义, 此处压成 4 因 SETTLED 不下单)
// ---------------------------------------------------------------------------
enum class InplayPhase : std::uint8_t {
    Pregame  = 0,  // > 5 min before kickoff
    Early    = 1,  // 0-30% of match duration (e.g., NBA Q1, Soccer 0-30min, Tennis set 1)
    Mid      = 2,  // 30-70%
    Late     = 3,  // > 70% (NBA Q4, Soccer 70+min, Tennis set 3+, MLB 8+ inning)
};

inline constexpr std::size_t kNumPhases = 4;

[[nodiscard]] constexpr std::string_view phase_name(InplayPhase p) noexcept {
    switch (p) {
        case InplayPhase::Pregame: return "Pregame";
        case InplayPhase::Early:   return "Early";
        case InplayPhase::Mid:     return "Mid";
        case InplayPhase::Late:    return "Late";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// 3. SportFillRateProfile — 单元素
// ---------------------------------------------------------------------------
//
// 字段:
//   - base_fill_rate: maker 在该 sport + phase 下的基础 fill 概率 (paper Mode A++ 用)
//   - qhl_ms:         典型 quote half-life (ms), < QHL_THRESHOLD_MS 触发 -0.15 penalty
//   - depth_pref_usdc: 该 sport 该 phase 的典型 ±2 tick 深度中位 (USD), 给 sizing 参考
//   - late_decay_penalty: 比赛末期附加扣减 (Late phase only, 其他 phase = 0)
//
struct SportFillRateProfile {
    double       base_fill_rate;
    std::int64_t qhl_ms;
    double       depth_pref_usdc;
    double       late_decay_penalty;  // Late phase 额外 -X (公式 §2 time_decay)
};

// ---------------------------------------------------------------------------
// 4. 8 sport × 4 phase table (v0.1 实测 + alpha 占位)
// ---------------------------------------------------------------------------
//
// 数据源:
//   - Soccer/NBA/MLB/Tennis Pregame + Mid: §1.2 mainline (300 markets), §1.3 gameday only
//   - Tennis Late (set 3+): §1.3 N=16 Roland Garros R1-R2 (深度 $27508 ±2tick)
//   - NFL/Esports/Hockey/Volleyball/Baseball: alpha 估计, W6 校准
//
// 设计原则:
//   - Pregame: qhl 长 (临场 > 1h 实测 > 120s), depth 中位偏厚, base 高
//   - Early: qhl 中, base 高
//   - Mid: qhl 短-中, base 中
//   - Late: qhl 极短 (NBA Q4 < 2min, MLB 8+ 实测 ms 级), base 低 + late_decay_penalty
//
// 编排: profile[sport][phase]
inline constexpr std::array<std::array<SportFillRateProfile, kNumPhases>, kNumSports>
    kSportProfiles = {{
        // [0] Soccer — pregame outright 多, gameday 临场少 (§1.2 spread_med=8.8¢, 偏宽)
        {{
            {0.70, 60'000,  500.0,  0.00},  // Pregame (远期 outright qhl 长)
            {0.65, 10'000,  300.0,  0.00},  // Early   (前 30min)
            {0.62,  3'000,  250.0,  0.00},  // Mid
            {0.55,    500,  150.0,  0.10},  // Late    (70+min, 进球密集, qhl 极短)
        }},
        // [1] Basketball — NBA gameday (§1.3 spread_med=1¢, $2K 滑点 0¢, $10K 0.82¢)
        {{
            {0.80, 30'000, 1'500.0, 0.00},  // Pregame
            {0.78,  3'000, 4'800.0, 0.00},  // Early (NBA Q1)
            {0.72,  1'000, 4'500.0, 0.00},  // Mid   (Q2-Q3)
            {0.58,    300, 1'200.0, 0.15},  // Late  (Q4 < 2min hot, qhl 0.21s 实测)
        }},
        // [2] Tennis — Roland Garros gameday (§1.3 Tennis $10K 滑点 0.42¢, 流动比预期厚)
        {{
            {0.75, 30'000, 5'000.0, 0.00},  // Pregame
            {0.73,  5'000, 21'800.0, 0.00}, // Early (set 1)
            {0.68,  2'000, 15'000.0, 0.00}, // Mid   (set 2)
            {0.55,    500,  3'000.0, 0.10}, // Late  (set 3+, break point hot)
        }},
        // [3] Volleyball — alpha 占位 (W6 校准)
        {{
            {0.55, 30'000,   500.0, 0.00},
            {0.55,  5'000,   400.0, 0.00},
            {0.50,  2'000,   300.0, 0.00},
            {0.45,    500,   200.0, 0.10},
        }},
        // [4] AmericanFootball — §1.2 NFL spread_med=1.5¢, gameday 与 offseason outright 数据混合
        //     drive-based: snap 之间 qhl 长, 红区 / 2-min drill 极短
        {{
            {0.72, 60'000,   500.0, 0.00},  // Pregame
            {0.70,  5'000, 1'000.0, 0.00},  // Early  (Q1)
            {0.65,  2'000,   800.0, 0.00},  // Mid    (Q2-Q3)
            {0.55,    400,   400.0, 0.12},  // Late   (Q4 / 2-min)
        }},
        // [5] Esports — 极快 (RTS / FPS event 密集), alpha 占位
        {{
            {0.55, 10'000,   300.0, 0.00},  // Pregame
            {0.50,  1'000,   200.0, 0.00},  // Early
            {0.45,    500,   150.0, 0.00},  // Mid
            {0.35,    200,   100.0, 0.20},  // Late  (objective decisive moment)
        }},
        // [6] Hockey — alpha (Period 3 close 类比 NBA Q4)
        {{
            {0.65, 30'000,   500.0, 0.00},
            {0.62,  3'000,   400.0, 0.00},
            {0.58,  1'500,   300.0, 0.00},
            {0.48,    400,   200.0, 0.12},
        }},
        // [7] Baseball — §1.3 MLB gameday $2K 滑点 0¢, $10K 0.88¢ — 流动好
        //     但 8+ inning close game (老韩 v0.3 §14.1 INPLAY_HOT_CRIT) qhl 短
        {{
            {0.78, 30'000, 1'000.0, 0.00},  // Pregame
            {0.76,  5'000, 8'900.0, 0.00},  // Early  (inning 1-3)
            {0.72,  2'000, 5'000.0, 0.00},  // Mid    (inning 4-6)
            {0.58,    400, 1'200.0, 0.12},  // Late   (inning 8+ tie/1-run)
        }},
    }};

// ---------------------------------------------------------------------------
// 5. Lookup
// ---------------------------------------------------------------------------

[[nodiscard]] constexpr SportFillRateProfile const& profile_of(Sport s,
                                                               InplayPhase p) noexcept {
    return kSportProfiles[static_cast<std::size_t>(s)][static_cast<std::size_t>(p)];
}

// 派单 §3 Soccer vs Esports 差异化校验: Pregame base_fill_rate Esports << Soccer
static_assert(profile_of(Sport::Soccer,  InplayPhase::Pregame).base_fill_rate
              > profile_of(Sport::Esports, InplayPhase::Pregame).base_fill_rate,
              "Esports Pregame base must be lower than Soccer Pregame (§1.2 实测 + alpha)");

// 同 sport Late phase base 必须比 Pregame 低 (流动性枯竭基本规律)
static_assert(profile_of(Sport::Basketball, InplayPhase::Late).base_fill_rate
              < profile_of(Sport::Basketball, InplayPhase::Pregame).base_fill_rate);
static_assert(profile_of(Sport::Tennis, InplayPhase::Late).base_fill_rate
              < profile_of(Sport::Tennis, InplayPhase::Pregame).base_fill_rate);

}  // namespace stcpp::microstructure
