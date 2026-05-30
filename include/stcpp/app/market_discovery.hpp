// include/stcpp/app/market_discovery.hpp — Gamma /events + /markets 体育市场发现
//
// Owner: 老雷 (GM) — 从 debug_server_main.cpp 抽出 (PaperDaemon 重构, 老郭 §A.1)
// last_review: 2026-05-30
//
// 归属: app 编排层 (stcpp_paper_app 库, 老周架构裁定 B1 — 不进 debug_api 契约库).
//   依赖方向 app → debug_api 单向.
//
// 职责: 从 Polymarket gamma REST 发现活跃体育 Event/Market/Token, 产出
//   DiscoveredEvent 列表 (纯数据). 不依赖任何 daemon 状态.
//
// 来源: 原内嵌于 debug_server_main.cpp 匿名 namespace (~400 行 JSON 解析).
//   逐字搬迁, 行为不变. 老周补充: IO (popen Fetch) 与解析 (Parse 纯函数) 拆开,
//   解析层可喂 fixture 单测 ("独立可测"才兑现).
//
// 三层 API:
//   Fetch*    — popen curl 拉原始 JSON (IO 副作用; 非热路径, 启动一次)
//   Parse*    — 纯函数, 喂 JSON 字符串 → DiscoveredEvent (可单测, 无网络)
//   Discover* — Fetch + Parse 便捷封装 (daemon 实际调用此层)
//
// 正确的 Polymarket 体育层次结构 (laoli SSOT §6, GM 实测确认):
//   Event → markets[] → conditionId + clobTokenIds[2]
//   发现走 /events (而非 /markets); /events 无体育时回退 /markets 平铺.
//
// ToS: 只读公开 gamma REST, curl 自动读 HTTPS_PROXY/HTTP_PROXY, 不下单.

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace stcpp::app {

// ---------------------------------------------------------------------------
// 发现结果数据结构 (纯数据, 无依赖)
// ---------------------------------------------------------------------------

// 单个 market (gamma /events markets[] 一项)
struct DiscoveredMarket {
    std::string condition_id;
    std::string question;
    std::string group_item_title;
    std::string sports_market_type;  // 归一化: moneyline/spread/totals/outright/prop/series/unknown
    std::string token0_id;           // YES
    std::string token1_id;           // NO
};

// 单个 event (含 markets[])
struct DiscoveredEvent {
    std::string event_id;
    std::string slug;
    std::string title;
    std::string sport;
    std::string neg_risk_market_id;
    std::vector<DiscoveredMarket> markets;
};

// ---------------------------------------------------------------------------
// Parse 层 — 纯函数 (无网络副作用, 喂 JSON 字符串, 可单测)
// ---------------------------------------------------------------------------

// 解析 gamma /events 响应 JSON → DiscoveredEvent 列表 (主路径).
// max_events: 最多解析的 event 数.
[[nodiscard]] std::vector<DiscoveredEvent> ParseSportsEvents(const std::string& json_buf,
                                                             int max_events = 30);

// 解析 gamma /markets 平铺响应 JSON → DiscoveredEvent 列表 (回退路径; 每 market 包成 synthetic event).
// max_markets: 最多解析的 market 数.
[[nodiscard]] std::vector<DiscoveredEvent> ParseSportsMarketsFlat(const std::string& json_buf,
                                                                  int max_markets = 10);

// ---------------------------------------------------------------------------
// Fetch 层 — popen curl 拉原始 JSON (IO; 非热路径)
// ---------------------------------------------------------------------------

// GET gamma /events (tag_id=1 体育, ascending=false 近期优先). 失败返 "".
[[nodiscard]] std::string FetchGammaEvents();

// GET gamma /markets (平铺回退). 失败返 "".
[[nodiscard]] std::string FetchGammaMarketsFlat();

// ---------------------------------------------------------------------------
// Discover 层 — Fetch + Parse 便捷封装 (daemon 实际入口)
// ---------------------------------------------------------------------------

[[nodiscard]] std::vector<DiscoveredEvent> DiscoverSportsEvents(int max_events = 30);
[[nodiscard]] std::vector<DiscoveredEvent> DiscoverSportsMarketsFlat(int max_markets = 10);

// ---------------------------------------------------------------------------
// 纯解析器 (discovery_detail) — 暴露供单测; 无网络副作用.
// ---------------------------------------------------------------------------
namespace discovery_detail {

// 最小 JSON string 值提取 ("key":"value"). 找不到返 "".
[[nodiscard]] std::string ExtractJsonStr(const std::string& json, const std::string& key);

// 提取 clobTokenIds — 处理两种 gamma 编码:
//   原生数组:    "clobTokenIds":["tok0","tok1"]
//   JSON 字符串: "clobTokenIds":"[\"tok0\",\"tok1\"]"  (gamma /events 此编码)
// 至少 2 个 token 时填 tok0/tok1 并返 true.
[[nodiscard]] bool ExtractClobTokenIds(const std::string& obj, std::string& tok0, std::string& tok1);

// sportsMarketType 归一化 → moneyline/spread/totals/outright/prop/series/unknown.
[[nodiscard]] std::string NormalizeSportsMarketType(const std::string& raw);

// 从 pos 起提取下一个平衡 { } 对象, 返回 {start, end} (含两端); 失败返 {npos, npos}.
[[nodiscard]] std::pair<std::size_t, std::size_t> ExtractNextObject(const std::string& s, std::size_t pos);

// 从 event 对象提取 "markets":[...] 子数组文本 (含两端 [ ]); 失败返 "".
[[nodiscard]] std::string ExtractMarketsArray(const std::string& event_obj);

}  // namespace discovery_detail

}  // namespace stcpp::app
