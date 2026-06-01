// stcpp/data/live_stats_parser.hpp — Soccer live_stats KV parser
//
// Owner: 小段 (goalserve-specialist, #37)
// Date:  2026-05-31
//
// 背景:
//   livescore.goalserve.com/getfeed/<KEY>/soccernew/home XML 中包含
//   <live_stats> 节点, 内容为 pipe-separated KV 字符串:
//     "IDangerousAttacks=home:40,away:43|IOnTarget=home:3,away:6|
//      IPosession=home:57,away:43|IRedCard=home:0,away:0|..."
//
//   (注: IPosession 是 Goalserve 拼写, 保留原拼写做 key 匹配, 映射到
//    vendor-agnostic 字段 soccer_possession_*)
//
//   inplay.goalserve.com/inplay-soccer.gz 无此节点;
//   因此 live_stats 来自独立的 livescore endpoint, 不在 InplayScoreParser 里.
//
// 接入点:
//   LivescoreSoccerParser (待建) 或 goalserve_livescore_client 在解析
//   <match> 节点时, 如遇 <live_stats> 属性/子节点, 调用:
//     LiveStatsParser::Parse(live_stats_str) → LiveStatsFields
//   然后调用:
//     FillLiveStats(row, fields)  — 填 FeatureStoreGameRow
//
//   当前 (白名单未开放): 白名单开放 livescore endpoint 后, 数据即可流入.
//   代码已就位, 零网络依赖, build 直接通过.
//
// 格式:
//   pipe ('|') 分隔的 KV 对, 每对格式:
//     Key=home:N,away:N
//   其中 N 是非负整数. 若缺失某 key, 对应字段保持 -1.
//
// 实测 KV 键列表 (2026-05-30 livescore feed, soccer):
//   IDangerousAttacks, IOnTarget, IPosession (注意拼写), IRedCard, IYellowCard,
//   ICorner, IFreeKick, IAttacks, IGoalKick, IThrowIn
//
// Vendor-agnostic 守法 (CLAUDE.md §8 红线):
//   - LiveStatsFields 字段名不含 Goalserve 原始 I-prefix 名
//   - Parse() 内部做 Goalserve key → 语义字段的映射, 上层见不到 "IDangerousAttacks"
//   - 未来换数据源: 替换 Parse() 实现, FillLiveStats() 接口不变
//
// R-20 守法:
//   本文件仅做 CPU-bound string 解析, 无 IO, 无时间戳操作.
//   时间戳由 livescore client 按 R-20 规范填充 FourTs, 传给 FeatureStoreGameRow.
//
// 依赖:
//   stcpp/data/feature_store_contract.hpp (FeatureStoreGameRow 定义)
//   标准库: <string_view> <cstdint>
//   无外部库依赖

#pragma once

#include <cstdint>
#include <string_view>

#include "stcpp/data/feature_store_contract.hpp"

namespace stcpp::data::livescore {

// ============================================================================
// LiveStatsFields — Parse() 的中间产物
//
// 所有字段初始为 -1 (= 无数据).
// 解析成功的字段填入非负整数.
// 调用方可直接检查 field >= 0 判断是否有效.
// ============================================================================
struct LiveStatsFields {
    std::int32_t dangerous_attacks_home = -1;
    std::int32_t dangerous_attacks_away = -1;
    std::int32_t shots_on_target_home   = -1;
    std::int32_t shots_on_target_away   = -1;
    std::int32_t possession_home_pct    = -1;
    std::int32_t possession_away_pct    = -1;
    std::int32_t red_cards_home         = -1;
    std::int32_t red_cards_away         = -1;
    std::int32_t yellow_cards_home      = -1;
    std::int32_t yellow_cards_away      = -1;
    std::int32_t corners_home           = -1;
    std::int32_t corners_away           = -1;
    std::int32_t free_kicks_home        = -1;
    std::int32_t free_kicks_away        = -1;
    std::int32_t attacks_home           = -1;
    std::int32_t attacks_away           = -1;
    std::int32_t goal_kicks_home        = -1;
    std::int32_t goal_kicks_away        = -1;
    std::int32_t throw_ins_home         = -1;
    std::int32_t throw_ins_away         = -1;

    // 数据新鲜度 (2026-06-01 老板「每个源标时间」): CommentariesPoller 解析此 live_stats 的时刻
    //   (30s 轮询 → 此源可达 30s 陈旧)。喂 g_live_stats_age_sec, 模型据此给 g_*_diff 动量特征降权。0=未知。
    std::int64_t as_of_ts_ns = 0;

    // 便利方法: 有任意字段有效则返回 true (白名单开放后的 feed 健康检查)
    [[nodiscard]] bool any_valid() const noexcept {
        return dangerous_attacks_home >= 0 || shots_on_target_home >= 0 ||
               possession_home_pct >= 0   || red_cards_home >= 0 ||
               yellow_cards_home >= 0     || corners_home >= 0;
    }
};

// ============================================================================
// LiveStatsParser — stateless, 纯函数
//
// 线程安全: 无成员状态, 可多线程并发调用.
// ============================================================================
class LiveStatsParser {
public:
    // ------------------------------------------------------------------------
    // Parse — 主入口
    //
    // 参数:
    //   live_stats_str: <live_stats> 节点内容 (pipe-separated KV 字符串)
    //     示例: "IDangerousAttacks=home:40,away:43|IOnTarget=home:3,away:6|..."
    //     允许前后有空白 / 空串 (空串返回全 -1 的 LiveStatsFields, 不出错)
    //
    // 返回: LiveStatsFields (未知 key 忽略, 缺失 key 保持 -1)
    //
    // 异常: 不抛出. 数值解析失败的字段保持 -1.
    //
    // 容错行为:
    //   - 缺 '|': 当做单 KV 对处理
    //   - 缺 '=': 跳过该 segment
    //   - 缺 ',': home 有值 away 保持 -1
    //   - 缺 'home:'/'away:' 前缀: 跳过该侧
    //   - 负数 / 非数字: 保持 -1
    // ------------------------------------------------------------------------
    [[nodiscard]] static LiveStatsFields Parse(std::string_view live_stats_str) noexcept;

    // ------------------------------------------------------------------------
    // ParseKvPair — 解析单个 "Key=home:N,away:N" 段
    //
    // 供测试直接调用; 正常路径通过 Parse() 批量处理.
    // 返回 false 表示格式错误 (缺 '=' 或值区格式不符).
    // home_val / away_val: -1 = 该侧缺失/解析失败.
    // ------------------------------------------------------------------------
    [[nodiscard]] static bool ParseKvPair(std::string_view segment,
                                          std::string_view& key_out,
                                          std::int32_t& home_val,
                                          std::int32_t& away_val) noexcept;
};

// ============================================================================
// FillLiveStats — LiveStatsFields → FeatureStoreGameRow soccer live_stats 字段
//
// 纯粹的字段赋值, 无逻辑判断.
// 调用方确保 row.sport == "Soccer" (或 "soccer") 再调用.
// ============================================================================
inline void FillLiveStats(feature_store::FeatureStoreGameRow& row,
                          const LiveStatsFields& f) noexcept {
    row.soccer_dangerous_attacks_home = f.dangerous_attacks_home;
    row.soccer_dangerous_attacks_away = f.dangerous_attacks_away;
    row.soccer_shots_on_target_home   = f.shots_on_target_home;
    row.soccer_shots_on_target_away   = f.shots_on_target_away;
    row.soccer_possession_home_pct    = f.possession_home_pct;
    row.soccer_possession_away_pct    = f.possession_away_pct;
    row.soccer_red_cards_home         = f.red_cards_home;
    row.soccer_red_cards_away         = f.red_cards_away;
    row.soccer_yellow_cards_home      = f.yellow_cards_home;
    row.soccer_yellow_cards_away      = f.yellow_cards_away;
    row.soccer_corners_home           = f.corners_home;
    row.soccer_corners_away           = f.corners_away;
    row.soccer_free_kicks_home        = f.free_kicks_home;
    row.soccer_free_kicks_away        = f.free_kicks_away;
    row.soccer_attacks_home           = f.attacks_home;
    row.soccer_attacks_away           = f.attacks_away;
    row.soccer_goal_kicks_home        = f.goal_kicks_home;
    row.soccer_goal_kicks_away        = f.goal_kicks_away;
    row.soccer_throw_ins_home         = f.throw_ins_home;
    row.soccer_throw_ins_away         = f.throw_ins_away;
}

}  // namespace stcpp::data::livescore
