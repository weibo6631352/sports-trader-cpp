// stcpp/app/team_alias.hpp — 按运动分派的队名规范化 (sport-aware 匹配, 2026-06-02 老板)
//
// Owner: 老雷 (GM)  last_review: 2026-06-02
//
// 背景: 老板「会不会专门做不同体育项目下的队伍名匹配? 通用匹配太不严谨」。
//   通用 token-overlap 在各运动失败模式不同 (足球俱乐部 "Man Utd"≠"Manchester United" 漏;
//   网球应以姓为锚; NBA "LA Lakers"↔"Los Angeles Lakers")。本模块提供:
//   ① 运动分类 (个人 / 团队 / 未知);
//   ② 团队项目: 队名 → 联盟内唯一规范队 ID (昵称), 供精确 ID 匹配。
//
// 设计原则 (安全): sport-aware 解析是「加法」—— 解析成功用精确结果, 失败回退现有通用匹配,
//   故只会更好、绝不退化。fail-closed: 歧义 (多队命中) 返 "" 让上游回退/不猜。

#pragma once

#include <string>
#include <string_view>

namespace stcpp::app {

enum class SportMatchCategory {
    kIndividual,  // 网球/MMA/拳击/高尔夫... 以选手姓为锚
    kTeam,        // NBA/MLB/NFL/NHL/足球... 队名 → 规范 ID
    kUnknown,     // 未知 → 通用匹配
};

// 按 Polymarket sport 码 (ev.sport, 如 "nba"/"mlb"/"atp"/"itf"/"soccer") 分类。大小写不敏感。
[[nodiscard]] SportMatchCategory ClassifySportMatch(std::string_view sport_code);

// 团队项目: 队名 → 规范队 ID (联盟内唯一昵称 token, 如 "lakers"/"yankees"/"chiefs")。
//   sport_code 决定查哪张联盟表 (NBA/MLB/NFL/NHL 已建全; 其余联盟返 "" → 上游回退通用)。
//   已折叠变音符 + 大小写不敏感。解析不出 / 歧义 → 返 "" (fail-closed, 上游回退)。
[[nodiscard]] std::string CanonicalTeam(std::string_view sport_code, const std::string& name);

}  // namespace stcpp::app
