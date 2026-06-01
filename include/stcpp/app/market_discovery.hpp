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

#include <cstdint>
#include <limits>
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
    // A0 映射桥 (condition_id↔goalserve event) 锚定字段:
    //   outcome0/1_name: gamma market.outcomes 两项 (moneyline 即两队名, e.g. ["Galorys","LDP"]).
    //   game_start_ts_sec: gamma market.gameStartTime → Unix 秒 (真实开赛, 比 listing startDate 准; 0=缺).
    std::string outcome0_name;
    std::string outcome1_name;
    std::int64_t game_start_ts_sec{0};
    // gamma market.endDate → Unix 秒 (比赛结束/结算窗口; 0=缺)。判「已结束→不订阅/退订」用。
    std::int64_t end_ts_sec{0};
    // 手续费系数 (gamma feeSchedule.rate; fee = shares × rate × p × (1-p))。
    //   feesEnabled=false (老市场免费) → 0.0; 缺字段 → 0.03 默认 (体育保守)。
    //   官方明确: 别硬编码, 从 market 数据 feeSchedule 取 (docs.polymarket 2026-03-31)。R-fee-2 RM/sizing
    //   用此真值。
    double fee_rate_coef{0.03};
    // 派生盘口线值 (gamma market.line): totals 大小分线 (211.5) / spreads 让分线 (−3.5)。
    //   moneyline 无 → NaN。totals/spreads 定价模型 (derivative_fair_value) 的核心输入。
    double line{std::numeric_limits<double>::quiet_NaN()};
    // 市场活跃度/流动性 (gamma REST, 非 WSS — WSS outcomes 只推 resolution): 越活跃信号越可靠、滑点越小。
    //   volume_24h = 24h 成交量 (volume24hr); liquidity = book 流动性 (gamma liquidity)。喂 ML + Kelly 定仓。
    double volume_24h{std::numeric_limits<double>::quiet_NaN()};
    double liquidity{std::numeric_limits<double>::quiet_NaN()};
};

// 单个 event (含 markets[])
struct DiscoveredEvent {
    std::string event_id;
    std::string slug;
    std::string title;
    std::string sport;        // legacy: is_sports 过滤用 (title/tag 兜底); 真 sport 对象 → sport_code/id
    std::string sport_code;   // 真实 event.sport.sport 联赛码 ("nba"/"bkcba"=CBA/"atp"/"wta"/"lol"/...)
    std::int64_t sport_id{0};  // 真实 event.sport.id 稳定整数 (nba=34/bkcba=104; 联赛级, NBA≠CBA) → cat_league
    std::string neg_risk_market_id;
    bool live{false};          // gamma event.live=true (正在比赛); DiscoverSportsEvents 给 live 批标记
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

// GET gamma /events (tag_id=1 体育, ascending=false 近期优先). offset 分页. 失败返 "".
[[nodiscard]] std::string FetchGammaEvents(int offset = 0);

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

// gamma 手续费系数提取 — 解析 feeSchedule.rate + feesEnabled.
//   feesEnabled:false → 0.0 (老市场免费); feeSchedule.rate 存在 → 返该值;
//   均缺 → fallback_default (体育 0.03 保守)。返回值钳在 [0, 0.10] (防脏数据).
//   fee = shares × rate × p × (1-p); 官方禁硬编码 (docs.polymarket).
[[nodiscard]] double ExtractFeeRateCoef(const std::string& obj, double fallback_default = 0.03);

// 提取某 key 的 2-string 数组 — 处理两种 gamma 编码:
//   原生数组:    "key":["a","b"]
//   JSON 字符串: "key":"[\"a\",\"b\"]"  (gamma /events 此编码)
// 至少 2 项时填 out0/out1 并返 true.
[[nodiscard]] bool ExtractTwoStringArray(const std::string& obj, const std::string& key, std::string& out0,
                                         std::string& out1);

// 提取 clobTokenIds (= ExtractTwoStringArray(obj,"clobTokenIds",...)). 填 tok0/tok1 (YES/NO).
[[nodiscard]] bool ExtractClobTokenIds(const std::string& obj, std::string& tok0, std::string& tok1);

// 提取 outcomes (= ExtractTwoStringArray(obj,"outcomes",...)). moneyline 即两队名.
[[nodiscard]] bool ExtractOutcomes(const std::string& obj, std::string& out0, std::string& out1);

// 解析 gamma 时间 → Unix 秒 (UTC). 处理两种格式:
//   gameStartTime: "2026-05-30 20:00:00+00"
//   startDate ISO: "2026-05-30T05:25:21.889Z"
// 解析失败返 0.
[[nodiscard]] std::int64_t ParseGammaTimeToEpochSec(const std::string& s);

// sportsMarketType 归一化 → moneyline/spread/totals/outright/prop/series/unknown.
[[nodiscard]] std::string NormalizeSportsMarketType(const std::string& raw);

// 从 pos 起提取下一个平衡 { } 对象, 返回 {start, end} (含两端); 失败返 {npos, npos}.
[[nodiscard]] std::pair<std::size_t, std::size_t> ExtractNextObject(const std::string& s, std::size_t pos);

// 从 event 对象提取 "markets":[...] 子数组文本 (含两端 [ ]); 失败返 "".
[[nodiscard]] std::string ExtractMarketsArray(const std::string& event_obj);

}  // namespace discovery_detail

}  // namespace stcpp::app
