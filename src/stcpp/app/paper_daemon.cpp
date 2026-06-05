// src/stcpp/app/paper_daemon.cpp — PaperDaemon 实现
//
// Owner: 老雷 (GM) — PaperDaemon 重构 (老郭 §A.1 配套落地)
// last_review: 2026-05-30
//
// 逐字搬迁自 debug_server_main.cpp 的 main() 函数体 (Step 1-6), 行为不变.
// 红线 (R-11 / R-12 / R-20) 见头文件; 关键不变量在下方对应位置加 [R-11] / [R-20] 注.

#include "stcpp/app/paper_daemon.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <unordered_set>
#include <utility>

#include "stcpp/data/commentaries_poller.hpp"   // live_stats CommentariesPoller (commentaries 轮询)
#include "stcpp/data/tennis_scores_parser.hpp"  // 覆盖率: tennis_scores livescore → InjectSupplementalScores
#include "stcpp/data/team_livescore_parser.hpp"  // 覆盖率: cricket/esports livescore → InjectSupplementalScores
#include "stcpp/data/market_taxonomy.hpp"        // v0.7 类别码映射 (真实 Polymarket 结构 → categorical)
#include "stcpp/data/inplay_feed_thread.hpp"    // InplayFeedThread / InplayFeedConfig
#include "stcpp/data/live_stats_store.hpp"      // live_stats LiveStatsStore
#include "stcpp/data/settlement_poller.hpp"     // M2 SettlementPoller (clob /markets 轮询)
#include "stcpp/data/settlement_recorder.hpp"   // Phase 2 缺口E 结算落盘 (label y)
#include "stcpp/data/score_frame_recorder.hpp"  // 回测 P0 比分帧落盘 (红线#3 闭合数据前提)
#include "stcpp/data/settlement_store.hpp"      // M2 SettlementStore
#include "stcpp/data/score_snapshot_store.hpp"  // A1b: ScoreSnapshotStore::GetSnapshot

#include "src/stcpp/polymarket/clob_wss/live_book_publisher.hpp"  // LiveBookPublisher
#include "src/stcpp/polymarket/clob_wss/live_wss_transport.hpp"   // LiveWssTransport
#include "src/stcpp/debug_api/server.hpp"               // HttpServer
#include "stcpp/net/persistent_https.hpp"                // 149hz 主动 book 轮询热链 (老板 2026-06-04)

namespace stcpp::app {

namespace {

// paper RiskGateway 专用 null audit emitter (M1: 不落真 WAL; R-11 隔离).
class NullAuditEmitter final : public risk::AuditEmitter {
public:
    bool emit(risk::AuditRecord const& /*rec*/) noexcept override { return true; }
};

// outcome 是 Yes/No 二值 (足球 3-way 子盘) 而非真队名 → 队名得另寻 (event title).
[[nodiscard]] bool IsYesNoOutcome(const std::string& s) noexcept {
    auto lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower == "yes" || lower == "no";
}

// Over/Under (大小盘 outcome): 非对手名 → 该盘匹配须退回 event title 取两选手 (赛事级锚定)。
//   覆盖 "Over"/"Under" 精确 + "Over 21.5"/"Under 2.5" 带线值前缀。
[[nodiscard]] bool IsOverUnder(const std::string& s) noexcept {
    auto lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower == "over" || lower == "under" || lower.rfind("over ", 0) == 0 ||
           lower.rfind("under ", 0) == 0;
}

// 非对手名 outcome (Yes/No 或 Over/Under): 匹配锚须退回 event title 两参与者。
[[nodiscard]] bool IsNonOpponentOutcome(const std::string& s) noexcept {
    return IsYesNoOutcome(s) || IsOverUnder(s);
}

// 分局/分盘/分节盘检测 (2026-06-04 老板「第一局 第二局这样的盘」): group_item_title 含【分段词】→
//   per-segment 盘 (Game N Winner / Set N / 1st Half …)。这类盘【绝不能套全场赛果 sharp fair】(映射是
//   per-event + de-vig 选全场盘 → 把全场 fair 套到"第N局赢家" = 错价乱单, 比丢了更糟)。
//   正确交易需 per-segment bet365 赔率匹配 (A-step-2): PM "Set N Winner" → bet365 "Home/Away (Nth Set)"。
//   未建前: 不进 matching → 不套全场 fair → 不错价 (仍留 token_map/catalog, 不丢)。全场盘 (gi 空 /
//   Money Line / Match·Series Winner / 队名) 不命中 → 正常全场匹配交易。
[[nodiscard]] bool IsSegmentMarket(const std::string& gi) noexcept {
    if (gi.empty()) return false;
    std::string g = gi;
    std::transform(g.begin(), g.end(), g.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    static const char* const kSeg[] = {
        "game ",  "set ",   "map ",     "frame ", "1st ", "2nd ",   "3rd ",
        "4th ",   "5th ",   "6th ",     "7th ",   "half", "quarter", "period",
        "inning", "1h",     "2h",       "leg ",
    };
    for (const char* s : kSeg)
        if (g.find(s) != std::string::npos) return true;
    return false;
}

// 从 PM tennis 分盘盘 gi 解析【盘号】(A-step-2): "Set 2 Winner"→2 / "2nd Set"→2。无 → 0。
//   只认 1-7 盘。用于 tennis 分盘盘 un-fence + 标 seg_index (下游 paper_loop 用当前段 fair)。
[[nodiscard]] int ParseTennisSetIndex(const std::string& gi) noexcept {
    if (gi.empty()) return 0;
    std::string g = gi;
    std::transform(g.begin(), g.end(), g.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    // pattern 1: "set <digit>"
    for (std::size_t p = g.find("set "); p != std::string::npos; p = g.find("set ", p + 1)) {
        std::size_t q = p + 4;
        while (q < g.size() && g[q] == ' ') ++q;
        if (q < g.size() && g[q] >= '1' && g[q] <= '7') return g[q] - '0';
    }
    // pattern 2: "<ordinal> set"
    static const struct {
        const char* o;
        int n;
    } kOrd[] = {{"1st set", 1}, {"2nd set", 2}, {"3rd set", 3}, {"4th set", 4},
                {"5th set", 5}, {"6th set", 6}, {"7th set", 7}};
    for (const auto& e : kOrd)
        if (g.find(e.o) != std::string::npos) return e.n;
    return 0;
}

// 从 event title "Team A vs. Team B" 拆两队名 (分隔符 " vs. " / " vs " / " v. "). 失败返 false.
[[nodiscard]] bool SplitVsTitle(const std::string& title, std::string& a, std::string& b) {
    for (const char* sep : {" vs. ", " vs ", " v. ", " VS "}) {
        const auto pos = title.find(sep);
        if (pos != std::string::npos) {
            a = title.substr(0, pos);
            b = title.substr(pos + std::char_traits<char>::length(sep));
            // 去首尾空白
            auto trim = [](std::string& s) {
                const auto l = s.find_first_not_of(" \t");
                const auto r = s.find_last_not_of(" \t");
                s = (l == std::string::npos) ? "" : s.substr(l, r - l + 1);
            };
            trim(a);
            trim(b);
            return !a.empty() && !b.empty();
        }
    }
    return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// ctor / dtor
// ---------------------------------------------------------------------------

PaperDaemon::PaperDaemon(PaperDaemonConfig cfg) noexcept : cfg_(std::move(cfg)) {}

PaperDaemon::~PaperDaemon() {
    // R-11 INV-1 双保险之二: 析构兜底再调 Shutdown (幂等). 保证 detach 在
    // paper_rm_snap_ 析构前发生 (Shutdown 内 detach; 即使调用方漏调 Shutdown).
    Shutdown();
}

// ---------------------------------------------------------------------------
// InjectMarkets — 测试 seam
// ---------------------------------------------------------------------------

void PaperDaemon::InjectMarkets(std::vector<DiscoveredEvent> markets) {
    injected_markets_ = std::move(markets);
    has_injected_ = true;
}

// ---------------------------------------------------------------------------
// PopulateCatalog — DiscoveredEvent → token_map_/market_catalog_/event_infos_
// (逐字搬迁自 main Step 1 后半段 line 655-714)
// ---------------------------------------------------------------------------

void PaperDaemon::PopulateCatalog(const std::vector<DiscoveredEvent>& discovered) {
    using debug_api::EventInfo;
    using debug_api::MarketInfo;
    using debug_api::TokenInfo;

    const std::int64_t pop_now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        std::chrono::system_clock::now().time_since_epoch())
                                        .count();
    for (const auto& ev : discovered) {
        // 结束赛事黑名单: 直播 final 后拉黑(带 TTL), 即便 gamma 仍列(等结算)也不再订阅。未过期才挡 —
        //   第二场是不同 event_id 不在黑名单, 不受影响; 过期项自动放行(自愈)。
        if (!ev.event_id.empty()) {
            std::lock_guard<std::mutex> lk(ended_blacklist_mu_);
            const auto bit = ended_event_blacklist_.find(ev.event_id);
            if (bit != ended_event_blacklist_.end() && pop_now_ns < bit->second) continue;
        }
        std::printf("[paper_daemon]  event: %.40s | slug=%.30s | sport=%s\n", ev.title.c_str(),
                    ev.slug.c_str(), ev.sport.c_str());

        EventInfo ei;
        ei.event_id = ev.event_id;
        ei.slug = ev.slug;
        ei.title = ev.title;
        ei.sport = ev.sport;
        ei.neg_risk_market_id = ev.neg_risk_market_id;
        ei.live = ev.live;  // gamma live=true 透传 → 前端默认只显示正在比赛
        ei.icon_url = ev.icon_url;  // 赛事图 → 前端事件头

        for (const auto& dm : ev.markets) {
            // 只收录 moneyline 赛果盘 (2026-06-04 老板「prop 多了」): 非 moneyline (props/totals/handicap/
            //   outright/series) 无 bet365 sharp 源 → 不入 catalog (免污染 matching/grid/订阅)。
            //   MarketTypeCode==0 = moneyline (含 tennis To Win); !=0 跳过。
            if (cfg_.moneyline_only &&
                stcpp::data::taxonomy::MarketTypeCode(dm.sports_market_type) != 0) {
                continue;
            }
            std::printf("[paper_daemon]    market %.28s... | type=%s | gi=%s\n", dm.condition_id.c_str(),
                        dm.sports_market_type.c_str(), dm.group_item_title.c_str());
            token_map_[dm.condition_id] = {dm.token0_id, dm.token1_id};
            ei.condition_ids.push_back(dm.condition_id);

            // A1b: 捕获 EventMatcher 锚定输入 (condition ↔ Goalserve event, 取真实比分源).
            //   队名解析两路 (实证 gamma 结构):
            //   ① outcomes 是真队名 (如 MLB ["Chicago Cubs","St. Louis Cardinals"]) → 直接用, team0=YES.
            //   ② outcomes 是 Yes/No (足球 3-way 子盘) → 队名在 event title ("Cruzeiro EC vs. Fluminense FC"),
            //      拆两队; 再用 group_item_title (gi: "Clube do Remo" 赢盘 / "Draw(...)" 平局) 定向 YES 代表哪队
            //      (yes_is_home 定价正确性命门; gi 配不上=平局/未知 → 仅锚定取分, orientation 交 matcher margin 兜底).
            std::string team0 = dm.outcome0_name;  // YES (token0)
            std::string team1 = dm.outcome1_name;  // NO  (token1)
            // 赛事级锚定 (2026-06-02 老板「全盘口都该匹配上」): moneyline 的 outcome 是球员/队名 → 直接用;
            //   但同一 event 的大小盘 outcome 是 Over/Under、3-way 子盘是 Yes/No —— 非对手名 → 退回 event
            //   title 拆两参与者, 让全盘口(含大小盘)都锚到同一 Goalserve event。orientation 交 matcher
            //   max(直配,交叉) 兜底 (大小盘 yes_is_home 不参与定价方向)。
            if (team0.empty() || team1.empty() || IsNonOpponentOutcome(team0) || IsNonOpponentOutcome(team1)) {
                std::string ta, tb;
                if (SplitVsTitle(ev.title, ta, tb)) {
                    const std::string& gi = dm.group_item_title;
                    if (!gi.empty() && EventMatcher::TeamSimilarity(gi, ta) >= 0.5) {
                        team0 = ta;  // YES 代表 ta
                        team1 = tb;
                    } else if (!gi.empty() && EventMatcher::TeamSimilarity(gi, tb) >= 0.5) {
                        team0 = tb;  // YES 代表 tb
                        team1 = ta;
                    } else {
                        team0 = ta;  // 平局/未知 → 仅锚定 (任意序; matcher 取 max(直配,交叉))
                        team1 = tb;
                    }
                }
            }
            // 分局盘准入 (A-step-2, 老板「第一局/第二局」): tennis 分盘盘 (Set N) 解析盘号 → 进 matching +
            //   标 seg_index (下游用当前段 fair); 其余分段盘 (Game/Half/非 tennis 分盘) 继续围栏 (无 bet365
            //   分段赔率, 进了会套全场 fair 错价)。全场盘 seg_index=0 正常。
            const bool is_seg = IsSegmentMarket(dm.group_item_title);
            int seg_idx = 0;
            if (is_seg && stcpp::data::taxonomy::SportFamilyCode(ev.sport_code) == 2 /*tennis*/) {
                seg_idx = ParseTennisSetIndex(dm.group_item_title);  // >0 = 可交易的分盘盘
            }
            const bool seg_tradeable = (seg_idx > 0);
            if (!team0.empty() && !team1.empty() && !IsNonOpponentOutcome(team0) &&
                !IsNonOpponentOutcome(team1) && (!is_seg || seg_tradeable)) {
                EventMatchInput mi_in;
                mi_in.team0 = team0;
                mi_in.team1 = team1;
                mi_in.kickoff_ts_sec = dm.game_start_ts_sec;
                mi_in.sport = ev.sport;
                // 3-way 平局盘 (gi="Draw (...)") → 下游 sharp fair 取 draw 概率 (盈利修复)。
                mi_in.is_draw = dm.group_item_title.rfind("Draw", 0) == 0;
                mi_in.seg_index = seg_idx;  // A-step-2: tennis 分盘号 (0=全场)
                market_match_inputs_[dm.condition_id] = std::move(mi_in);
            }

            // P1-1: 填充 MarketInfo catalog (gamma 发现的真实元信息)
            MarketInfo mi;
            mi.found = true;
            mi.condition_id = dm.condition_id;
            mi.market_id = dm.condition_id;  // deprecated alias
            mi.tick_size = 0.01;             // Polymarket 默认 tick
            mi.fee_rate = dm.fee_rate_coef;  // R-fee-2: gamma feeSchedule.rate (体育0.03/加密0.072/老市场0)
            mi.neg_risk = !ev.neg_risk_market_id.empty();
            mi.neg_risk_market_id = ev.neg_risk_market_id;
            mi.accepting_orders = true;  // gamma active=true 时默认接单
            mi.active = true;
            mi.closed = false;
            mi.resolved = false;
            mi.source = "polymarket";
            mi.event_id = ev.event_id;
            mi.slug = ev.slug;
            mi.polymarket_url = ev.slug.empty() ? "" : ("https://polymarket.com/event/" + ev.slug);
            mi.sports_market_type = dm.sports_market_type;
            mi.group_item_title = dm.group_item_title;
            mi.game_start_ts_sec = dm.game_start_ts_sec;  // 开赛/结束时间 → grid game_state + 前端徽章
            mi.end_ts_sec = dm.end_ts_sec;
            mi.live = ev.live;  // 在打标志 (= kickoff<=now, gamma 原生 live 不可靠故 discovery 这么算)
                                //   → 比分匹配率真分母 markets_live_total (老板 2026-06-02)
            // tokens[]: YES (tok0) + NO (tok1)
            TokenInfo tk0;
            tk0.token_id = dm.token0_id;
            tk0.outcome = "Yes";
            tk0.price = 0.0;  // 实时价从 book hub 读
            tk0.winner = false;
            TokenInfo tk1;
            tk1.token_id = dm.token1_id;
            tk1.outcome = "No";
            tk1.price = 0.0;
            tk1.winner = false;
            mi.tokens.push_back(std::move(tk0));
            mi.tokens.push_back(std::move(tk1));

            market_catalog_[dm.condition_id] = std::move(mi);

            // v0.7: 真实 Polymarket 市场结构 → 类别上下文码 (此处 ev+dm 在 scope, 算一次喂 ML 特征 82-85)。
            //   联赛 = ev.sport_id (Polymarket sport.id, nba=34/bkcba=104=CBA); 家族 = ev.sport_code 滚动;
            //   盘口 = dm.sports_market_type (已归一)。映射 SSOT: data/market_taxonomy.hpp。
            namespace tax = stcpp::data::taxonomy;
            paper::MarketCat cat;
            cat.asset_class_id = static_cast<std::int32_t>(tax::AssetClass::kSports);  // 现仅发现体育
            cat.sport_family_id = tax::SportFamilyCode(ev.sport_code);
            cat.league_id = (ev.sport_id > 0) ? static_cast<std::int32_t>(ev.sport_id) : -1;
            cat.market_type_id = tax::MarketTypeCode(dm.sports_market_type);
            cat.line = dm.line;  // totals/spreads 线值 → 派生定价
            cat.volume_24h = dm.volume_24h;  // 市场活跃度 (gamma REST)
            cat.liquidity = dm.liquidity;    // book 流动性 (gamma REST)
            // totals 方向: outcomes[0] (=YES/token0) 是否 "Over" (大小写不敏感)。非 Over → YES=Under。
            std::string o0 = dm.outcome0_name;
            std::transform(o0.begin(), o0.end(), o0.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            cat.yes_is_over = (o0 != "under");  // 默认 Over (Polymarket totals outcomes[0] 常为 Over)
            market_cat_map_[dm.condition_id] = cat;
        }
        event_infos_.push_back(std::move(ei));
    }

    // Collect token_ids for WSS subscription — 源头 pass (2026-06-04 老板「从源头就不订阅无赔率源的比赛」):
    //   只订有 sharp(bet365) 赔率源的 condition 的 token。RefreshEventMapping 发布 eligible 集 (grace 滞回)。
    //   **null (映射线程尚未就绪, e.g. 初次 Build) → 一个都不订** (老板二次强调: 绝不 bootstrap 全订 →
    //   否则 WSS 连上先订全量 150, 之后靠 unsubscribe diff 收敛不可靠/被重连重置)。首个映射周期算出
    //   eligible 后, RediscoverOnce 增量订阅 sharp 盘。映射判定不需 PM 订阅 (靠 inplay 比分+赔率), 故无鸡蛋问题。
    //   注: market_match_inputs_ 不过滤 (全市场仍参与匹配 → 才能判定哪些有 sharp); 只过滤订阅集。
    const auto sharp_snap = SharpConditionsSnapshot();
    all_token_ids_.reserve(token_map_.size() * 2);
    std::vector<std::pair<std::string, double>> poll_plan;  // 149hz 主动轮询计划 (token, √liq+1 权重)
    poll_plan.reserve(token_map_.size() * 2);
    std::size_t passed_no_source = 0;
    for (const auto& [cond_id, tok_pair] : token_map_) {
        if (!sharp_snap || sharp_snap->count(cond_id) == 0) {
            ++passed_no_source;  // 无赔率源 (或 eligible 未就绪) → 源头 pass, 不订阅
            continue;
        }
        all_token_ids_.push_back(tok_pair.first);
        all_token_ids_.push_back(tok_pair.second);
        // 主动轮询权重 = √liquidity + 1 (压缩极差: 高流动性多刷但不饿死低流动性 sharp 盘)。
        double liq = 0.0;
        if (auto cit = market_cat_map_.find(cond_id); cit != market_cat_map_.end())
            liq = cit->second.liquidity;
        const double w = std::sqrt(std::max(0.0, liq)) + 1.0;
        poll_plan.emplace_back(tok_pair.first, w);
        poll_plan.emplace_back(tok_pair.second, w);
    }
    PublishPollPlan(std::move(poll_plan));  // 发布给 ActiveBookPoller (流动性加权 149hz)
    if (sharp_snap) {
        std::fprintf(stderr,
                     "[paper_daemon] 源头 pass: 订阅 %zu/%zu market (跳过 %zu 无赔率源), token=%zu\n",
                     token_map_.size() - passed_no_source, token_map_.size(), passed_no_source,
                     all_token_ids_.size());
    }
    // A1: 发布不可变 token 快照 (OnConnected/seed 等并发读方读它, 不碰裸 all_token_ids_ → 消 race)
    PublishTokenSnapshot();
}

// ---------------------------------------------------------------------------
// BuildPaperCatalog (R-3) — token_map_/market_catalog_/market_cat_map_ → 统一 PaperCatalog。
//   只装【静态元数据】(老周边界铁律): tokens + fee + cat + parent。动态态 (score/resolution/
//   live_stats) 不并入。一次原子 swap; R-6 周期重发现重建后复用。
// ---------------------------------------------------------------------------
std::shared_ptr<const paper::PaperCatalog> PaperDaemon::BuildPaperCatalog() const {
    auto pc = std::make_shared<paper::PaperCatalog>();
    pc->reserve(token_map_.size());
    // 新鲜度锚: 本次 catalog 构建/重发现时刻 (老板「每个源标时间」→ g_catalog_age_sec)。
    const std::int64_t built_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count();
    for (const auto& [cid, toks] : token_map_) {
        paper::PaperMarketEntry e;
        e.tokens = toks;
        e.discovered_at_ns = built_ns;
        if (const auto mit = market_catalog_.find(cid); mit != market_catalog_.end()) {
            e.fee_coef = mit->second.fee_rate;  // R-fee-2: gamma feeSchedule.rate
            e.parent = paper::ParentRef{mit->second.event_id, mit->second.neg_risk_market_id};
        }
        if (const auto cit = market_cat_map_.find(cid); cit != market_cat_map_.end()) {
            e.cat = cit->second;  // v0.7 类别码 (ML 特征 82-85)
        }
        (*pc)[cid] = std::move(e);
    }
    return pc;
}

// ---------------------------------------------------------------------------
// RediscoverOnce (R-6) — 周期重发现: 全量重建 catalog + WSS 全量重订。
//   在映射刷新线程跑 (market_match_inputs_ 同线程, 无竞争)。live 比赛滚动, 不周期重发现则跑几小时
//   后订阅全是死盘。老郭: 全量重订别增量 diff (幂等好测)。集合未变则跳过 (省 republish)。
// ---------------------------------------------------------------------------
bool PaperDaemon::RediscoverOnce(std::stop_token st) {
    auto events = DiscoverSportsEvents(cfg_.max_events);
    if (events.empty()) {
        return false;  // 无 live/近赛 → 不动 (保留现集, 让旧盘经 resolution 自然结算; 不抖动到空)
    }
    // 新 condition 集 vs 现集: 相同则跳过 (无变化不 republish)。
    std::unordered_set<std::string> new_conds;
    for (const auto& ev : events) {
        for (const auto& dm : ev.markets) new_conds.insert(dm.condition_id);
    }
    bool changed = (new_conds.size() != token_map_.size());
    if (!changed) {
        for (const auto& [cid, _t] : token_map_) {
            if (new_conds.find(cid) == new_conds.end()) {
                changed = true;
                break;
            }
        }
    }
    // 源头 pass: 即便市场集未变, sharp-eligible 集变了 (赔率源增删) 也须重建过滤 → 重订/退订。
    //   否则稳定市场集下 bootstrap 时的全订阅 (snap=null) 永不收敛到 sharp-only。
    const auto sharp_snap = SharpConditionsSnapshot();
    const std::unordered_set<std::string> cur_elig =
        sharp_snap ? *sharp_snap : std::unordered_set<std::string>{};
    const bool elig_changed = (cur_elig != last_sub_eligible_);
    if (!changed && !elig_changed) {
        return false;  // 市场集 + eligible 集均未变, 不动
    }
    last_sub_eligible_ = cur_elig;  // 记录本次据以订阅的 eligible 集
    // 增量订阅落地 (2026-06-01 设计, 2026-06-02 落地): 重建前快照旧 token 集, 重建后只对差异发
    //   operation:subscribe(新增)/unsubscribe(移除)。取代"全量重订"老格式帧 —— 后者在已订阅连接上
    //   语义不明(追加 vs 替换), 老李评审疑其催掉连接(recv_loop_ended 次因)。初次订阅(OnConnected)
    //   不变(known-good 老格式)。
    std::unordered_set<std::string> old_tokens(all_token_ids_.begin(), all_token_ids_.end());
    // 全量重建 (老郭): 清 6 表 → PopulateCatalog 重填 (含队名提取/cat/fee/parent/all_token_ids_)。
    token_map_.clear();
    market_catalog_.clear();
    market_cat_map_.clear();
    market_match_inputs_.clear();
    event_infos_.clear();
    all_token_ids_.clear();
    PopulateCatalog(events);
    // 发布: PaperLoop catalog (RCU 原子 swap) + RSP (meta_mu_ 守护) + WSS 全量重订。
    if (paper_loop_) {
        paper_loop_->SetPaperCatalog(BuildPaperCatalog());
    }
    if (real_provider_) {
        real_provider_->set_token_map(token_map_);
        real_provider_->set_events(event_infos_);
        real_provider_->set_market_catalog(market_catalog_);
    }
    // 2026-06-01: 把新市场集同步给 SettlementPoller (修启动时设死 bug → 新比赛得到结算轮询)。
    if (settlement_poller_) {
        std::vector<std::string> settle_cids;
        settle_cids.reserve(token_map_.size());
        for (const auto& [cid, _tok] : token_map_) settle_cids.push_back(cid);
        settlement_poller_->SetConditionIds(std::move(settle_cids));
    }
    if (live_transport_) {
        const std::unordered_set<std::string> new_tokens(all_token_ids_.begin(), all_token_ids_.end());
        // 新增 token → operation:subscribe (并收集到 added_tokens 供 REST 补 seed)
        std::string add_body;
        std::size_t n_add = 0;
        std::vector<std::string> added_tokens;
        for (const auto& t : all_token_ids_) {
            if (old_tokens.count(t)) continue;
            if (n_add) add_body.push_back(',');
            add_body.push_back('"');
            add_body.append(t);
            add_body.push_back('"');
            ++n_add;
            added_tokens.push_back(t);
        }
        // 移除 token (比赛结束/下架/拉黑) → operation:unsubscribe (单盘停推) + 释放 hub 书槽 (老板「释放资源」)。
        std::string del_body;
        std::size_t n_del = 0;
        for (const auto& t : old_tokens) {
            if (new_tokens.count(t)) continue;
            if (n_del) del_body.push_back(',');
            del_body.push_back('"');
            del_body.append(t);
            del_body.push_back('"');
            ++n_del;
            if (hub_) hub_->ResetToken(t);  // 释放该 token 的 book 槽 (退订赛事不再占 hub 资源)
        }
        if (n_add) {
            live_transport_->AsyncSendText(R"({"assets_ids":[)" + add_body + R"(],"operation":"subscribe"})");
        }
        if (n_del) {
            live_transport_->AsyncSendText(R"({"assets_ids":[)" + del_body + R"(],"operation":"unsubscribe"})");
        }
        std::fprintf(stderr,
                     "[paper_daemon] 周期重发现: 市场集变化 → +%zu 订阅 / -%zu 退订 (增量, 共 %zu market)\n",
                     n_add, n_del, token_map_.size());
        std::fflush(stderr);
        // 新增盘补 REST seed (修: 仅 operation:subscribe 不够 — 稀疏体育盘短期无 WSS diff 帧 →
        //   hub book 永远 found:false → 前端"订单簿未接入"。和启动 SeedInitialBooksFromRest 同路, 只打底新增子集)。
        if (!added_tokens.empty()) {
            SeedTokensFromRest(added_tokens, st);
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// SeedInitialBooksFromRest — 订阅时拉一次初始 book 快照 (REST), seed 进 hub.
//   修 WSS-only 的缺陷: 稳定盘/漏接初始快照 → hub 永远空。POST /books 批量拉,
//   交给 live_publisher_->SeedFromRestBooks (与 WSS book 同解析路径)。
//   非热路径 (Start 一次, 阻塞 ~秒级 popen curl); 失败优雅降级 (回落 WSS-only)。
//   recv_ts = 本地 now (= ingestion ts, 合法; data_source_ts 取自 REST 响应的 timestamp, 非 now)。
// ---------------------------------------------------------------------------
void PaperDaemon::SeedInitialBooksFromRest(std::stop_token st) {
    // A1: 读不可变 token 快照 (非裸 all_token_ids_ — 该线程与 RediscoverOnce 写并发, 消 race)
    const auto tokens_sp = TokenSnapshot();
    SeedTokensFromRest(*tokens_sp, st);
}

// ---------------------------------------------------------------------------
// SeedTokensFromRest — 按指定 token 集 POST /books 批量 REST seed (核心实现)。
//   SeedInitialBooksFromRest 用全量快照调它; RediscoverOnce 用"新增 token 子集"调它
//   (修 rediscovery 新增盘漏补 seed → Polymarket 有簿前端却显示未接入)。
// ---------------------------------------------------------------------------
void PaperDaemon::SeedTokensFromRest(const std::vector<std::string>& tokens, std::stop_token st) {
    if (!live_publisher_ || tokens.empty())
        return;
    const std::int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();
    constexpr std::size_t kChunk = 50;  // POST /books 分批 (避免单请求过大)
    std::size_t chunks_ok = 0;
    for (std::size_t i = 0; i < tokens.size(); i += kChunk) {
        if (st.stop_requested())  // 关停时提前退出 (不卡 shutdown)
            return;
        const std::size_t end = (i + kChunk < tokens.size()) ? i + kChunk : tokens.size();
        std::string body = "[";
        for (std::size_t j = i; j < end; ++j) {
            if (j > i)
                body += ',';
            body += "{\"token_id\":\"";
            body += tokens[j];  // uint256 十进制, 无 shell 特殊字符
            body += "\"}";
        }
        body += "]";
        // popen curl POST /books (token_id 全数字 → 单引号 body 安全; 沿用 discovery 的 popen 模式)
        const std::string cmd =
            "curl -s --max-time 15 -X POST 'https://clob.polymarket.com/books' "
            "-H 'Content-Type: application/json' --data '" +
            body + "' 2>/dev/null";
        std::string resp;
        if (FILE* p = ::popen(cmd.c_str(), "r")) {
            char buf[8192];
            std::size_t n = 0;
            while ((n = ::fread(buf, 1, sizeof(buf), p)) > 0)
                resp.append(buf, n);
            ::pclose(p);
        }
        if (!resp.empty() && resp.front() == '[') {
            live_publisher_->SeedFromRestBooks(resp, now_ns);
            ++chunks_ok;
        }
    }
    std::printf("[paper_daemon] REST 快照打底: %zu tokens (%zu 批 OK), 累计 books_published=%llu\n",
                tokens.size(), chunks_ok,
                static_cast<unsigned long long>(live_publisher_->books_published()));
    std::fflush(stdout);
}

// ---------------------------------------------------------------------------
// WssWatchdogLoop — CLOB WSS 心跳保活 + 断线重连 (2026-06-02 会议 d8d4bd6).
//   真因: Polymarket CLOB 服务端不主动发 ping, 客户端 ~10-15s 不发 "PING" → 30s idle 被踢
//         (recv_loop_ended); 且原 OnDisconnected 回调只打印不重连 → clob 永久 false。
//   修: 连着每 10s 发 "PING" (服务端回 "PONG", OnFrame 对非 [/{ 开头帧直接忽略, 不崩);
//       断开则指数退避后 AsyncConnect 重连 (本函数在独立 jthread, 非 io_thread → AsyncConnect
//       内 join 已死的旧 io_thread 不自 join 死锁), 重连后台重 seed。重订由 OnConnected 回调负责。
//   R-12: 只读 IsConnected() atomic + AsyncSendText(入队) + AsyncConnect, 不碰 on_text_frame 热路径。
// ---------------------------------------------------------------------------
void PaperDaemon::WssWatchdogLoop(std::stop_token st, std::string url) {
    using namespace std::chrono;
    if (!live_transport_)
        return;
    auto sleep_steps = [&st](int steps) {  // 小步 sleep 以及时响应 stop (steps×100ms)
        for (int i = 0; i < steps && !st.stop_requested(); ++i)
            std::this_thread::sleep_for(milliseconds(100));
    };
    sleep_steps(30);  // 给初次连接 ~3s 建立再监测
    auto last_ping = steady_clock::now();
    int backoff_sec = 1;
    // A2 半死检测: 跟踪收帧进度。连着且发了 PING 却长时间无任何帧(含 PONG/book)→ 连接半死
    //   (TCP 没断但服务端静默, IsConnected() 仍 true) → 主动 Close 触发重连。
    std::uint64_t last_frames = (live_publisher_ ? live_publisher_->frames_received() : 0);
    auto last_progress = steady_clock::now();
    constexpr auto kSilentTimeout = seconds(35);  // > 心跳 10s × 3, 留足 PONG 往返
    while (!st.stop_requested()) {
        if (live_transport_->IsConnected()) {
            backoff_sec = 1;
            if (steady_clock::now() - last_ping >= seconds(10)) {
                live_transport_->AsyncSendText("PING");  // 心跳: 防 idle 超时被踢 (真因)
                last_ping = steady_clock::now();
            }
            // A2: 收帧进度检测
            const std::uint64_t frames_now = (live_publisher_ ? live_publisher_->frames_received() : last_frames);
            if (frames_now != last_frames) {
                last_frames = frames_now;
                last_progress = steady_clock::now();
            } else if (steady_clock::now() - last_progress >= kSilentTimeout) {
                std::fprintf(stderr,
                             "[paper_daemon] WSS 半死 (≥%llds 无帧响应, 服务端静默), 主动 Close 触发重连\n",
                             static_cast<long long>(duration_cast<seconds>(kSilentTimeout).count()));
                std::fflush(stderr);
                live_transport_->Close();  // → 下一轮 IsConnected()==false → 走重连
                last_progress = steady_clock::now();  // 防连环 Close
            }
            sleep_steps(10);  // 1s 检查间隔
        } else {
            std::fprintf(stderr, "[paper_daemon] WSS 断开, %ds 后重连 (idle/网络/半死)...\n", backoff_sec);
            std::fflush(stderr);
            sleep_steps(backoff_sec * 10);
            if (st.stop_requested())
                break;
            live_transport_->AsyncConnect(url);  // OnConnected 回调用 token 快照重发 subscribe
            wss_reconnect_total_.fetch_add(1, std::memory_order_relaxed);  // A4: 重连计数 → /metrics
            // 重连后台重 seed (books 重新打底; 复用初次 seed 逻辑, 不阻塞看门狗)
            seed_thread_ = std::jthread([this](std::stop_token s) { SeedInitialBooksFromRest(s); });
            last_ping = steady_clock::now();
            last_progress = steady_clock::now();  // 重连后重置进度基线
            last_frames = (live_publisher_ ? live_publisher_->frames_received() : 0);
            backoff_sec = std::min(backoff_sec * 2, 30);  // 指数退避封顶 30s
        }
    }
}

// ---------------------------------------------------------------------------
// Build — 发现 + 装配 (不起线程). 幂等.
// ---------------------------------------------------------------------------

BuildResult PaperDaemon::Build() {
    if (built_) {
        return build_result_;  // 幂等: 返回缓存
    }

    std::printf("[paper_daemon] RunMode=%s exec_mode=%s 启动装配...\n", ToString(cfg_.mode),
                debug_api::exec_mode_str(cfg_.exec_mode));
    std::fflush(stdout);

    // ---- Step 1: 市场发现 (gamma 或注入) ----
    std::vector<DiscoveredEvent> discovered;
    if (has_injected_) {
        std::printf("[paper_daemon] 使用注入的 %zu 个 markets (跳过 gamma 发现, 测试 seam)\n",
                    injected_markets_.size());
        discovered = injected_markets_;
    } else {
        std::printf("[paper_daemon] gamma /events 发现活跃体育市场...\n");
        std::fflush(stdout);
        discovered = DiscoverSportsEvents(cfg_.max_events);
        // 资源优化 (老板 2026-06-01): 不再回退 /markets 平铺 — 那条路带的是赛季夺冠 outright 期货
        //   (World Cup/NBA 冠军, 几个月后才结算), 订阅它们最浪费 (无 in-play/无比分/book 浅)。
        //   DiscoverSportsEvents 已只留 [live + 开赛≤1h]; 空 = 此刻无近赛, 正确空闲 (省资源)。
        if (discovered.empty()) {
            std::printf("[paper_daemon] 当前无 live / 开赛≤1h 的赛事 → 不订阅 (省资源, 空闲等近赛)。\n");
            std::fflush(stdout);
        }
    }

    if (discovered.empty()) {
        std::fprintf(stderr,
                     "[paper_daemon] WARNING: 无体育市场, hub 将保持空状态 → book 端点 found=false.\n");
    } else {
        std::printf("[paper_daemon] 发现 %zu 个体育 event:\n", discovered.size());
    }
    PopulateCatalog(discovered);
    if (!market_catalog_.empty()) {
        std::printf("[paper_daemon] P1-1: MarketInfo catalog 已填充 %zu 条目\n", market_catalog_.size());
    }

    // ---- Step 2: hub + ScoreSnapshotStore + LedgerSnapshotHub + QuoteSnapshotHub ----
    // 容量 (2026-06-02 老板「有报价恒 512, 是不是写死上限」): 全盘口期发现 900+ 市场, 原 hub 默认
    //   1024 token(=512 市场)/ quote 512 key 满了静默丢 → 超出的盘没书没报价(覆盖率卡 55%)。
    //   提到 book 4096 token(2048 市场)/ quote 2048 市场, 容纳全盘口。预分配 ~几 MB, 可接受。
    constexpr std::size_t kHubMaxTokens = 4096;  // 2048 市场 × 2 token
    constexpr std::size_t kQuoteMaxKeys = 2048;  // 2048 市场
    hub_ = std::make_unique<polymarket::clob_wss::OrderBookSnapshotHub>(kHubMaxTokens);
    score_store_ = std::make_unique<data::ScoreSnapshotStore>();
    ledger_hub_ = std::make_unique<risk::LedgerSnapshotHub>();
    quote_hub_ = std::make_unique<sizing::QuoteSnapshotHub>(kQuoteMaxKeys);

    // ---- Step 2b: paper 隔离栈 (R-11) ----
    // [R-11] paper PositionLedger 独立实例, 与 live 路径物理隔离.
    paper_position_ledger_ = std::make_unique<risk::PositionLedger>();
    paper_rm_snap_ = std::make_unique<risk::RmDebugSnapshot>();
    // [R-11 INV-1] 注册全局 hook (RM 内部 push_reject 经此). Shutdown/dtor 必 detach.
    risk::attach_rm_debug_snapshot(paper_rm_snap_.get());
    attached_rm_snap_ = true;

    // paper RiskGateway (paper 专用; 与 live RM 隔离; NullAuditEmitter 不落真 WAL)
    paper_audit_emitter_ = std::make_shared<NullAuditEmitter>();
    // P0-2 单位统一 (老雷 2026-05-30, 拆 clamp 遮羞布): caps 单一真值源 = cfg_.paper_loop (pUSD)。
    //   RM check_position_caps_ 直接比 size_pUSD_micro (micro), 故 RM cfg 这里由 pUSD 源 × 1e6 派生;
    //   sizing 用同一 pUSD 源直接算 (paper_loop.cpp)。两端同源 → sizing notional 自然 ≤ RM cap,
    //   无需 paper_loop `min(notional,10.0)` clamp (已删)。
    //   原 main 两处独立硬编码 (sizing RiskConfig{} 10K pUSD vs RM 10 pUSD micro) 差 1000x, 靠 clamp
    //   摁住; advisory gate 长期挡着未爆, A2 第一笔成交才现形, 本次根治。
    risk::RiskConfig paper_rm_cfg;
    // 仓位上限放大 (2026-06-04 老板「跑通赔率 edge 盈利 200u」): MVP 占位 caps ($10/$25/$50) 在 $1000
    //   bankroll 上仅用 1% 资金/单, 比 Kelly(λ0.35, 5% edge≈$79/单) throttle 8x → +$200 累积极慢。
    //   实测 26 笔 sharp 成交全捕获正 edge (中位 5.3%, 0 笔买在 fair 上方) → 边真实, 放大有据。
    //   放大让 Kelly 主导 (λ0.35 仍是真风控); per-market ≤12% bankroll (守北极星 DD≤15%)。R-11 纯 paper。
    cfg_.paper_loop.per_order_cap_usdc = 50.0;        // was 10 (5% bankroll/单)
    cfg_.paper_loop.per_outcome_cap_usdc = 100.0;     // was 25
    cfg_.paper_loop.market_exposure_cap_usdc = 120.0; // was 50 (12% bankroll/市场)
    // c3 (P0-2 根治): RM caps 与 sizing 同源 = cfg_.paper_loop (whole pUSD), 同用 from_pusd 转 micro。
    //   RM 直接 micro 比 size_pUSD_micro; sizing 侧 .to_pusd() 回 whole 比 notional。同源同值。
    paper_rm_cfg.per_order_cap_usdc = domain::MicroPUSD::from_pusd(cfg_.paper_loop.per_order_cap_usdc);
    paper_rm_cfg.market_exposure_cap_usdc =
        domain::MicroPUSD::from_pusd(cfg_.paper_loop.market_exposure_cap_usdc);
    paper_rm_cfg.per_outcome_cap_usdc = domain::MicroPUSD::from_pusd(cfg_.paper_loop.per_outcome_cap_usdc);
    paper_rm_cfg.bankroll_usdc =
        domain::MicroPUSD::from_pusd(cfg_.paper_loop.bankroll_usdc);  // c2b: 与 cap 对称
    paper_rm_cfg.edge_ci_lower_floor = -1.0;                          // M1 放宽 CI 门
    // 2026-06-04 老板「不要卡他, 让他亏, 看亏的极限」: 解除日损熔断 (-3%软/-5%硬) + consec-loss halt,
    //   让 -EV sharp 策略在 paper 放开亏到 bankroll 见底 (INSUFFICIENT_BANKROLL 才是自然底)。纯观测, R-11 不碰真钱。
    paper_rm_cfg.daily_loss_soft_pct = 100.0;   // 实际不触发
    paper_rm_cfg.daily_loss_hard_pct = 100.0;
    paper_rm_cfg.daily_loss_halt_usdc = domain::MicroPUSD::from_pusd(1.0e9);  // 巨值 → 永不触发
    paper_rm_cfg.consec_loss_halt_count = 1'000'000'000;                       // 连亏门关
    // 盘口准入已移到定价层 (paper_loop: 非 moneyline 无专属定价 → fail-closed); RM enable_xxx 已删。
    paper_rm_ = std::make_unique<risk::RiskGateway>(paper_rm_cfg, paper_audit_emitter_);

    // BaselineFairValueModel (小肖 pricing v0.1; 先验 sigmoid)
    pricing::ScorePriorParams fv_params{0.30, 0.50};
    paper_fv_model_ = std::make_unique<pricing::BaselineFairValueModel>(fv_params, 0.20);

    // ---- Step 2c: PaperLoop (构造, 不 Start) ----
    // [R-12] PaperLoop 内部 std::jthread, 不进 WSS event loop.
    // [R-11] paper_position_ledger_ 与 live 物理隔离.
    // A2 (老韩红线1): advisory gate 翻转收口在此. enable_paper_fills=true → 解封 paper 成交;
    //   PaperLoop::Start() 内有运行期 mode 交叉断言 (非 paper mode + 解封 → abort).
    cfg_.paper_loop.advisory_markets_no_intent = !cfg_.enable_paper_fills;
    // Phase 0 联合评审 (2026-05-31): 生产开启动态 reservation (n_eff/margin 接时序+vig) + net-EV 门。
    //   lib 默认 false (向后兼容契约测试); 生产 daemon 置 true (可经 enable_phase0_gates 关, 供管线测试)。
    cfg_.paper_loop.dynamic_reservation = cfg_.enable_phase0_gates;
    cfg_.paper_loop.net_ev_gate = cfg_.enable_phase0_gates;
    // 决策源 = 直播源赔率 sharp (老板 2026-06-04「决策源就只用直播源赔率」+ 2026-06-05「砍掉大模型训练功能」):
    //   fair 由 sharp/derivative/score-prior 驱动 (见 pricing::ResolveFair), 无 ONNX blend。
    //   ml_fair_blend_weight/ml_drive_enabled 配置已随大模型一并砍。量化因子/统计
    //   (FeatureHistory/SharpFairTrack/RollingClv) 保留喂观测 + Stage2 sizing 乘子。
    cfg_.sharp_only_gate = true;   // 仅高置信 sharp 信号 (≥sharp_only_min_edge) 产单
    // sharp 驱动门 (2026-06-04): 生产 daemon 默认开 (cfg_.sharp_only_gate 默认 true) —— 仅高置信
    //   sharp(bet365) 信号产单, 其余源回退市场 (edge 归零)。管线机制测试可置 false (走 score-prior 出成交)。
    cfg_.paper_loop.sharp_only_gate = cfg_.sharp_only_gate;
    // sharp 偏离上界 (2026-06-04 老板「这个差的太多了」): >15pt 的 sharp-市场 gap 判为滞后/错配假信号,
    //   不产单 (实测快变盘 CS2/网球 sharp 滞后 2.3s 造 20-26pt 假 gap → 逆市场正确移动下单必亏)。
    cfg_.paper_loop.sharp_max_gap = 0.15;
    // 赔率源新鲜度门 (2026-06-04 老板「超过3秒的赔率源不进决策」): sharp feed 版本距决策刻 >3s 回退市场。
    cfg_.paper_loop.sharp_max_staleness_sec = 3.0;
    // 预测驱动平仓 (2026-06-04 老板「双边预测给出的双边仓位管理」): 生产开 —— 减仓随预测回 flat (收敛兑现),
    //   解「只买不卖持到结算」。lib 默认关 (契约/管线测试不变)。
    cfg_.paper_loop.predictive_unwind = true;
    // 入场价感知平仓 (2026-06-04 老板「别稍微亏本就卖, 要考虑持仓买卖价格」): 卖价低于均入(锁亏)时,
    //   仅当 sharp fair 真跌破均入超 5 分 (信号反转=止损) 才卖, 否则持有等回归/结算。治 predictive_unwind
    //   在小回撤里 churn 卖出实现亏损。取利平仓不受限。
    cfg_.paper_loop.loss_cut_fair_band = 0.05;
    // 订单簿结构感知 买/卖 (2026-06-04 老板「买卖都要看簿结构, 一直涨能卖就持仓, 簿转向才止盈」):
    //   买不接下跌的刀 (簿下行不进), 盈利骑趋势 (簿支撑不急止盈), 簿结构转向才止盈。持仓管理。
    cfg_.paper_loop.book_exit_enabled = true;
    cfg_.paper_loop.book_exit_imb_thr = 0.15;
    // 必输局保护 (2026-06-04 老板「用比赛阶段数学模型, 不是价格地板」): 改用既有 game_phase/garbage_time
    //   模型 (TickOne: 垃圾时间落后方 target=0 不开仓)。价格地板关闭 (boss「不是这样的」)。
    cfg_.paper_loop.min_buy_price = 0.0;
    // 临近末尾必输买入闸 (2026-06-05 老板「临近末尾必输的那种, 还得禁止买入」): 末段(phase>0.85)+ 本边
    //   exec_ask<0.15 (市场定为近必输) → 不开新仓, 防末段 longshot 结算归零。窄闸, 中前段/非便宜不受限。
    cfg_.paper_loop.near_end_max_buy_price = 0.15;
    // 必赢锁利买入 (2026-06-05 老板「必赢的, 除去买卖手续费有利润就买」): 决出赢方, (1−ask)−买卖费>0 → 强制
    //   买到此上限锁结算利润 (事件延迟真 edge)。50 = per_order_cap, 保守起步, 受 RM market cap(120) 兜底。
    cfg_.paper_loop.must_win_lock_usdc = 50.0;
    // 相对止损 (2026-06-05 老板「亏大就割」): 持仓 mark 跌破均入价 25% → 强平 (绕 fair-based loss_cut 的滞后)。
    //   修「bid 比 fair 跌得快, 等 fair 跌够时簿已 gap 到地板, 割在 −85%」; 把均亏 −0.70 压到 ~−0.25。
    cfg_.paper_loop.rel_stop_pct = 0.25;
    // 决策节拍 (2026-06-04 老板「三源都触发决策没」): 500ms→100ms。三源(WSS/149hz poll/赔率)写共享态,
    //   决策每 tick 读最新; 500ms 把 149hz 新鲜簿+簿结构反应硬卡住 → 簿转向止盈/不被吃单反应慢, 小赢大亏。
    //   降到 100ms: 决策 10×/s 采样新鲜簿; 48 盘×10/s 对 4 核轻松, 新加簿结构+入场价闸防过度交易。
    cfg_.paper_loop.tick_interval_ms = 100;
    // 老板 2026-06-03「把门都去了, 虚拟盘专门调模型, 模型自主, 识别各种情况」: 调模型模式 —
    //   去掉所有 edge 边门 (edge_ci/slippage/fee/net_ev + has_real_fair 对模型驱动放行), 让模型/sharp/
    //   score-prior 的任意正净 edge 在 paper 自由成交 → 全反馈供调模型。与 enable_paper_fills 同开同关
    //   (--enable-fills 的 paper daemon 本就是调模型用; 不开 fills 则本就无成交, 此闸无意义)。
    //   仍保: devig_ok + sizing Step5(净正) + RM cap 链 (仓位上限) + R-11 纯 VirtualFill 不碰真钱。
    // 老板 2026-06-03「把门都去了, 虚拟盘专门调模型, 模型自主, 识别各种情况」: 调模型模式 —
    //   去掉所有 edge 边门 (edge_ci/slippage/fee/net_ev + has_real_fair 对模型驱动放行), 让模型/sharp/
    //   score-prior 的任意正净 edge 在 paper 自由成交 → 全反馈供调模型。与 enable_paper_fills 同开同关。
    //   仍保: devig_ok + sizing Step5(净正) + RM cap 链 (仓位上限) + R-11 纯 VirtualFill 不碰真钱。
    cfg_.paper_loop.paper_no_edge_gates = cfg_.enable_paper_fills;
    paper_loop_ = std::make_unique<paper::PaperLoop>(*hub_, *paper_rm_, *paper_position_ledger_, *ledger_hub_,
                                                     *quote_hub_, paper_rm_snap_.get(), *paper_fv_model_,
                                                     token_map_, cfg_.paper_loop);
    // A1b: 注入真实比分源 (Start 前; 之后 loop_thread_ 只读). 映射由刷新线程 SetEventMapping.
    paper_loop_->SetScoreStore(score_store_.get());

    // (大模型 ONNX 推理装配已砍 2026-06-05「砍掉大模型训练功能」: 原 make_onnx_fair_value_model /
    //  StubFairValueModel / SetMlModelShared。fair_value 由 paper_fv_model_ baseline (score-prior 统计)
    //  + sharp/derivative 驱动, 不再有大模型推理 advisory。)

    // R-3 (老周/老郭 评审): per-condition 静态元数据 (token/fee/cat/parent) 统一为 PaperCatalog,
    //   一次原子注入 (替代原 3 个独立 setter)。BuildPaperCatalog 供 R-6 周期重发现复用。
    paper_loop_->SetPaperCatalog(BuildPaperCatalog());

    // ---- Step 2d: RealStateProvider (读模型) ----
    risk::RiskConfig rsp_risk_cfg;
    real_provider_ = std::make_unique<debug_api::RealStateProvider>(
        *hub_, /*snap=*/paper_rm_snap_.get(), /*score_store=*/score_store_.get(),
        /*token_map=*/token_map_, rsp_risk_cfg, cfg_.exec_mode,
        /*ledger_hub=*/ledger_hub_.get(), /*quote_hub=*/quote_hub_.get());
    real_provider_->set_events(event_infos_);
    real_provider_->set_market_catalog(market_catalog_);

    // [2026-06-01 凯利评审] /api/v1/account 回调: 翻译 paper_loop 发布的权益快照 → debug_api::AccountSnapshot。
    //   on-demand (HTTP 线程调用) → 经 published_account_equity() 线程安全拷贝, 零陈旧。翻译落此 (本层
    //   同时依赖 paper + debug_api; provider 头不许 include paper, line 49 边界)。
    real_provider_->set_account_snapshot_fn([this]() -> debug_api::AccountSnapshot {
        debug_api::AccountSnapshot a;
        if (!paper_loop_) return a;  // has_data=false
        const auto eq = paper_loop_->published_account_equity();
        a.mode = debug_api::exec_mode_str(cfg_.exec_mode);
        a.bankroll_initial = eq.bankroll_init;
        a.cash_available = eq.cash_available;
        a.position_mtm = eq.position_mtm;
        a.equity_mark = eq.equity_mark;
        a.equity_conservative = eq.equity_bid;
        a.cum_realized_pnl = eq.cum_realized;
        a.cum_unrealized_pnl = eq.unrealized_mark;
        a.cum_fee_paid = eq.cum_fee;
        a.net_pnl = eq.equity_mark - eq.bankroll_init;
        a.return_pct = eq.bankroll_init > 0.0 ? (eq.equity_mark - eq.bankroll_init) / eq.bankroll_init : 0.0;
        a.max_drawdown = eq.max_drawdown;
        a.sharpe = eq.sharpe;
        a.kelly_bankroll = eq.equity_bid;  // 实际喂 SizingCalculator 的 bankroll (best_bid 保守动态净值)
        a.kelly_bankroll_basis = "equity_conservative(best_bid, 动态)";
        a.open_positions = eq.open_positions;
        a.as_of_ts_ns = eq.as_of_ts_ns;
        a.has_data = true;
        return a;
    });

    // [2026-06-04 老板「多少价格买的/卖出的都不知道」] /api/v1/fills 成交流水回调:
    //   paper_loop RecentFills() (定长 ring, mutex 保护) → FillView (前端流水面板)。
    real_provider_->set_fills_fn([this](const std::string& market) -> std::vector<debug_api::FillView> {
        std::vector<debug_api::FillView> out;
        if (!paper_loop_) return out;
        // market 非空 → 按盘取(盯盘按盘看); 空 → 全局最近 (AnalyticsPage 全量日志)。
        // 全局(market 空)取 500 深 — 模型诊断需足量样本算偏差/胜率 (深环 5000 够; 已 gzip 传输)。
        const auto rows = market.empty() ? paper_loop_->RecentFills(500)
                                         : paper_loop_->RecentFills(30, market);
        out.reserve(rows.size());
        for (const auto& r : rows) {
            debug_api::FillView v;
            v.as_of_ts_ns = r.as_of_ts_ns;
            v.market_id = r.condition_id;
            v.is_yes = r.is_yes;
            v.is_buy = r.is_buy;
            v.is_close = r.is_close;
            v.price = r.price;
            v.size_usdc = r.size_usdc;
            v.realized = r.realized;
            v.cum_realized = r.cum_realized;
            v.fair = r.fair;
            v.mark = r.mark;
            out.push_back(std::move(v));
        }
        return out;
    });

    // [2026-06-01 凯利评审 Step3] /api/v1/pnl/timeseries 净值曲线回调: paper_loop equity_snapshot (每 tick
    //   等间隔权益样本) 按 bucket_sec 分桶 (按 ts), 每桶取末尾 equity → cum_net_pnl = equity − bankroll_init。
    real_provider_->set_pnl_timeseries_fn(
        [this](std::int64_t window_sec, std::int64_t bucket_sec) -> std::vector<debug_api::PnlBucket> {
            std::vector<debug_api::PnlBucket> out;
            if (!paper_loop_ || bucket_sec <= 0) return out;
            const auto series = paper_loop_->equity_snapshot();  // vector<pair<ts_ns, equity>>
            if (series.empty()) return out;
            const double bankroll_init = paper_loop_->published_account_equity().bankroll_init;
            const std::int64_t bucket_ns = bucket_sec * 1'000'000'000LL;
            const std::int64_t cutoff =
                (window_sec > 0) ? series.back().first - window_sec * 1'000'000'000LL : 0;
            std::int64_t cur_bucket = -1;
            for (const auto& [ts, eq] : series) {
                if (ts < cutoff) continue;
                const std::int64_t b = ts / bucket_ns;
                if (b != cur_bucket) {
                    debug_api::PnlBucket pb;
                    pb.bucket_start_ts_ns = b * bucket_ns;
                    pb.cum_net_pnl = eq - bankroll_init;
                    pb.unrealized = eq - bankroll_init;  // 近似: 曲线主用 cum_net_pnl
                    out.push_back(pb);
                    cur_bucket = b;
                } else {
                    out.back().cum_net_pnl = eq - bankroll_init;  // 同桶更新末尾值
                    out.back().unrealized = eq - bankroll_init;
                }
            }
            return out;
        });

    // LiveMetricsHooks (P1-2/P1-3): start_tp + fill_counter; wss_transport 在 Step 4 填.
    metrics_hooks_.start_tp = std::chrono::steady_clock::now();
    metrics_hooks_.fill_counter = &paper_loop_->stats().fills_completed;
    metrics_hooks_.last_tick_ts = &paper_loop_->stats().last_tick_ts_ns;  // 韧性 watchdog 心跳
    metrics_hooks_.wss_reconnect_counter = &wss_reconnect_total_;  // A4: WSS 看门狗重连计数
    real_provider_->set_live_metrics_hooks(metrics_hooks_);

    // ---- Step 3: InplayFeedThread (构造, 不 Start; Start() 内拉起) ----
    data::InplayFeedConfig feed_cfg;
    // 2026-06-03 老板「inplay 接口 1.005s 一次, 压着限速」: per-sport 贴 Goalserve ~1 req/s/sport 限,
    //   1005ms 留 5ms 抖动 margin (sharp 是策略 alpha 源 → 越新越好)。
    feed_cfg.poll_interval_ms = 1005;
    feed_cfg.min_fetch_interval_ms = 1005;
    feed_cfg.sports = {
        data::goalserve::GoalserveSport::Soccer,
        data::goalserve::GoalserveSport::Basketball,
        data::goalserve::GoalserveSport::Tennis,
        data::goalserve::GoalserveSport::Esports,  // 2026-06-01: PM dota2/lol/CS 盘对接 (inplay-esports.gz)
        // 2026-06-02 (老板「都不能交易」): 加主流联赛 inplay —— 原来只拉 soccer/basket/tennis/esports,
        //   Polymarket 发现的流动性盘多是 MLB(baseball)/NFL(amfootball)/NHL(hockey), 没对应比分源 →
        //   EventMatcher 匹配不上(实测 2/28)→ has_real_fair=false → 无 fair → 0 edge → 不交易。
        //   实测 inplay-baseball.gz 返真实时赛 (含 bet365id)。空档无赛 = 空 feed 轮询 (成本低)。
        data::goalserve::GoalserveSport::Baseball,
        data::goalserve::GoalserveSport::AmericanFootball,
        data::goalserve::GoalserveSport::Hockey,
    };
    inplay_feed_ = std::make_unique<data::InplayFeedThread>(*score_store_, feed_cfg);

    // ---- M2: SettlementStore + SettlementPoller (clob /markets 轮询 → 3b 权威结算 + CLV 收盘) ----
    //   fetcher: popen curl GET /markets/{cid} (cid 是 bytes32 hex, 单引号 body 安全, 沿用 discovery 模式)。
    //   R-12: poller 独立 jthread, popen 阻塞 IO 在本线程, 不进 WSS event loop。
    {
        settlement_store_ = std::make_unique<data::SettlementStore>();
        std::vector<std::string> settle_cids;
        settle_cids.reserve(token_map_.size());
        for (const auto& [cid, _tok] : token_map_) settle_cids.push_back(cid);
        auto fetcher = [](const std::string& cid) -> std::string {
            std::string cmd = "curl -s --max-time 15 'https://clob.polymarket.com/markets/";
            cmd += cid;  // bytes32 hex (0-9a-fx), 无 shell 特殊字符
            cmd += "'";
            std::string out;
            if (FILE* p = ::popen(cmd.c_str(), "r")) {
                char buf[4096];
                std::size_t n;
                while ((n = std::fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
                ::pclose(p);
            }
            return out;
        };
        settlement_poller_ = std::make_unique<data::SettlementPoller>(
            // 2026-06-01 老板「长的设 2s 压限速」: 60s→2s。扇出 = 顺序 popen curl 全部未结算 cid,
            //   fetch 延迟自节流 (~2-5 req/s, 2s floor 多不binding); 撞 CLOB 限速则 fail-closed 跳本轮。
            *settlement_store_, std::move(settle_cids), std::move(fetcher), /*poll_interval_ms=*/2'000);
    }

    // ---- live_stats: LiveStatsStore + CommentariesPoller (commentaries Feed → 5 个 g_*_diff 特征) ----
    //   fetcher: popen curl https www.goalserve.com/getfeed/{KEY}/commentaries/{league}.xml。
    //   key 在 URL → https + GOALSERVE_PROXY 保护 (小白审计: 明文泄 key)。league 校验纯数字防注入 (安全红线)。
    //   R-12: poller 独立 jthread, popen 阻塞 IO 在本线程。白名单未开 → curl 空 → store 空 → 特征 sentinel。
    {
        live_stats_store_ = std::make_unique<data::livescore::LiveStatsStore>();
        const char* gs_key_env = std::getenv("GOALSERVE_API_KEY");
        std::string gs_key = gs_key_env ? gs_key_env : "";
        const char* gs_proxy_env = std::getenv("GOALSERVE_PROXY");
        std::string gs_proxy = gs_proxy_env ? gs_proxy_env : "";
        auto ls_fetcher = [gs_key, gs_proxy](const std::string& league) -> std::string {
            if (gs_key.empty() || league.empty()) return {};
            // 安全红线: league 进 popen 命令 → 必须纯数字 (Goalserve league_id 全数字), 否则拒 (防 shell 注入)。
            for (char c : league) {
                if (c < '0' || c > '9') return {};
            }
            std::string cmd = "curl -s --max-time 15 ";
            if (!gs_proxy.empty()) {
                cmd += "-x '";
                cmd += gs_proxy;
                cmd += "' ";
            }
            cmd += "'https://www.goalserve.com/getfeed/";
            cmd += gs_key;  // hex key (.env), 无 shell 特殊字符
            cmd += "/commentaries/";
            cmd += league;  // 已校验纯数字
            cmd += ".xml'";
            std::string out;
            if (FILE* p = ::popen(cmd.c_str(), "r")) {
                char buf[4096];
                std::size_t n;
                while ((n = std::fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
                ::pclose(p);
            }
            return out;
        };
        commentaries_poller_ = std::make_unique<data::livescore::CommentariesPoller>(
            *live_stats_store_, std::vector<std::string>{}, std::move(ls_fetcher),
            /*poll_interval_ms=*/2'000);  // 2026-06-01: 30s→2s (顺序 per-league fetch 自节流)
    }

    // ---- Step 4: LiveWssTransport + LiveBookPublisher (构造 + 设回调, 不 AsyncConnect) ----
    //   R-6: 周期重发现开启时, 即便 0 起始 token 也构造 WSS (连上等重发现订阅; 否则 live 比赛来了无处订)。
    if (!all_token_ids_.empty() || cfg_.rediscover_interval_sec > 0) {
        live_publisher_ = std::make_unique<polymarket::clob_wss::LiveBookPublisher>(*hub_, all_token_ids_, cfg_.verbose);
        // 事件驱动 (2026-06-04 老板「别轮询直接触发」): book 落 hub 即唤醒决策。WSS frame + 149hz poll
        //   都经 publisher → Publish → 此回调 → RequestTick (短锁+notify, R-12 安全, 不阻塞数据线程)。
        if (paper_loop_) {
            live_publisher_->SetOnPublish(
                [this](const std::string& /*token_id*/) { paper_loop_->RequestTick(); });
        }
        live_transport_ = std::make_unique<polymarket::clob_wss::LiveWssTransport>(cfg_.verbose);

        // P1-2/P1-3 re-inject: wss_transport 指针就位后更新 hooks.
        metrics_hooks_.wss_transport = live_transport_.get();
        real_provider_->set_live_metrics_hooks(metrics_hooks_);

        // on_text_frame_ → LiveBookPublisher::OnFrame (同步, < 100us, R-12)
        live_transport_->SetOnTextFrame(
            [pub = live_publisher_.get()](std::string_view payload, std::int64_t recv_ts) {
                pub->OnFrame(payload, recv_ts);
            });

        live_transport_->SetOnConnected([this]() {
            // A1: 读不可变 token 快照 (此回调在 io_thread; 与 RediscoverOnce 写并发, 消 race)。
            //     初次订阅 known-good 老格式全量 (重连后亦同; 比赛增删走 RediscoverOnce 增量)。
            const auto tokens_sp = TokenSnapshot();
            const std::vector<std::string>& tokens = *tokens_sp;
            std::printf("[paper_daemon] WSS CONNECTED, 订阅 %zu tokens...\n", tokens.size());
            std::fflush(stdout);
            // CLOB market channel subscribe: {"type":"Market","assets_ids":[...]}
            std::string sub = R"({"type":"Market","assets_ids":[)";
            bool first = true;
            for (const auto& tid : tokens) {
                if (!first)
                    sub.push_back(',');
                sub.push_back('"');
                sub.append(tid);
                sub.push_back('"');
                first = false;
            }
            sub.append("]}");
            live_transport_->AsyncSendText(sub);
        });

        live_transport_->SetOnDisconnected([](std::string_view reason) {
            std::fprintf(stderr, "[paper_daemon] WSS DISCONNECTED: %s\n", std::string(reason).c_str());
            std::fflush(stderr);
        });
    } else {
        std::fprintf(
            stderr,
            "[paper_daemon] WARNING: 无 token 可订阅 (发现失败), WSS 未装配, book 回落 found=false.\n");
    }

    // ---- Step 4c: 结算/比分落盘 (回测等价数据; 大模型特征捕获 FeatureRecorder/FeatureVectorHub/
    //   FeatureVectorRecorder 已砍 2026-06-05「砍掉大模型训练功能」) ----
    if (cfg_.record_ml) {
        // Phase 2 缺口E: 结算落盘 (离线 label join 的 y 来源)。读 settlement_store_ 落 settlements.jsonl。
        if (settlement_store_) {
            data::SettlementRecorder::Config se_cfg;
            se_cfg.output_path = cfg_.ml_path + ".settlements.jsonl";
            settlement_recorder_ = std::make_unique<data::SettlementRecorder>(*settlement_store_, se_cfg);
        }

        // 回测等价性 P0 (红线#3 闭合数据前提): 比分帧落盘。读 score_store_ 落 scores.jsonl, 是小蒋 P2
        //   ReplayImpl 喂帧解锁回测 in-play 分支/下单/结算的原始数据 (resolution 由上方 settlement_recorder
        //   覆盖, book 由 ReplayDriver 覆盖, 比分是分叉杀伤力最大的缺口)。详见 spec / DecisionInputSnapshot。
        if (score_store_) {
            data::ScoreFrameRecorder::Config sf_cfg;
            sf_cfg.output_path = cfg_.ml_path + ".scores.jsonl";
            score_recorder_ = std::make_unique<data::ScoreFrameRecorder>(*score_store_, sf_cfg);
        }
    }

    // ---- Step 5: HttpServer (仅 RunMode::PaperDaemon; Headless 无 HTTP) ----
    if (cfg_.mode == RunMode::PaperDaemon) {
        server_ = std::make_unique<debug_api::HttpServer>(cfg_.port, real_provider_.get(), cfg_.host.c_str());
    }

    built_ = true;
    build_result_.ok = true;
    build_result_.market_count = market_catalog_.size();
    build_result_.token_count = all_token_ids_.size();
    return build_result_;
}

// ---------------------------------------------------------------------------
// Start — 按序起线程 (幂等)
// ---------------------------------------------------------------------------

void PaperDaemon::Start() {
    if (!built_) {
        std::fprintf(stderr, "[paper_daemon] ERROR: Start() 前必须先 Build()\n");
        return;
    }
    if (started_.exchange(true, std::memory_order_acq_rel)) {
        return;  // 幂等
    }

    // ---- Step 3 start: InplayFeedThread (Goalserve score feed, 常开) ----
    if (cfg_.start_live_feeds && inplay_feed_) {
        inplay_feed_->Start();
        std::printf("[paper_daemon] Goalserve InplayFeedThread 启动 (soccer/basketball/tennis/esports/baseball/amfootball/hockey, R-12)\n");
        std::fflush(stdout);
    }

    // ---- M2 start: SettlementPoller + 结算刷新线程 (喂 3b 权威结算 + CLV 收盘信号) ----
    if (cfg_.start_live_feeds && settlement_poller_) {
        settlement_poller_->Start();
        if (cfg_.enable_paper_trading && paper_loop_) {
            settlement_refresh_thread_ = std::jthread([this](std::stop_token st) { RefreshResolution(st); });
        }
        std::printf("[paper_daemon] M2 SettlementPoller + 结算刷新线程启动 (clob /markets 60s 轮询)\n");
        std::fflush(stdout);
    }

    // ---- live_stats start: soccernew/live 刷新线程 (喂 #19-23 g_*_diff 特征) ----
    //   2026-06-02 特征审计: 改用 soccernew/live (覆盖全部直播盘 + 内联 live_stats, 文档 soccer-data-feed.md
    //   §实时统计)。per-league CommentariesPoller (仅顶级联赛 + 标签找错) 已弃用, 不再启动。
    if (cfg_.start_live_feeds && cfg_.enable_paper_trading && paper_loop_) {
        live_stats_refresh_thread_ = std::jthread([this](std::stop_token st) { RefreshLiveStats(st); });
        std::printf("[paper_daemon] live_stats 刷新线程启动 (soccernew/live 12s 轮询)\n");
        std::fflush(stdout);
    }

    // ---- 覆盖率: tennis_scores livescore 补充源 (并入 inplay_feed_ 的 score store) ----
    //   2026-06-03: 拉高 tennis (PM 大头) 覆盖率 — 接 inplay 缺的 ITF/Challenger live 比分。
    if (cfg_.start_live_feeds && inplay_feed_) {
        tennis_scores_refresh_thread_ =
            std::jthread([this](std::stop_token st) { RefreshTennisScores(st); });
        std::printf("[paper_daemon] tennis_scores 补充比分线程启动 (拉高 ITF/Challenger 覆盖, 15s 轮询)\n");
        std::fflush(stdout);
        // 队制 livescore 补充源 (cricket/livescore + esports/home) — 填 inplay-cricket 404 的 0 缺口。
        team_livescore_refresh_thread_ =
            std::jthread([this](std::stop_token st) { RefreshTeamLivescores(st); });
        std::printf("[paper_daemon] 队制 livescore 补充线程启动 (cricket/esports, 30s 轮询)\n");
        std::fflush(stdout);
    }

    // ---- bm_slots start: 跨庄家赔率刷新线程 (getodds + inplay-mapping → g_bm_* 特征 #5/6/7/16) ----
    //   默认【关】(2026-06-02 事故: getodds 单 sport 达 45MB, 跨洋抓取吃光带宽 → WSS idle 断 + 前端卡。
    //   WSS 是交易命脉, 优先级 >> advisory 的 bm_slots)。需显式 STCPP_ENABLE_BM_SLOTS=1 才起。
    //   重开前提: getodds 45MB 跨洋不可行, 待改按场抓 (getodds/match?id=) 或代理侧过滤。
    const char* bm_en = std::getenv("STCPP_ENABLE_BM_SLOTS");
    if (bm_en != nullptr && std::string(bm_en) == "1" && cfg_.start_live_feeds &&
        cfg_.enable_paper_trading && paper_loop_) {
        odds_refresh_thread_ = std::jthread([this](std::stop_token st) { RefreshOdds(st); });
        std::printf("[paper_daemon] bm_slots 赔率刷新线程启动 (STCPP_ENABLE_BM_SLOTS=1; getodds 限速+300s)\n");
        std::fflush(stdout);
    } else {
        std::printf("[paper_daemon] bm_slots 赔率刷新线程【关】(getodds 45MB 吃带宽; 设 STCPP_ENABLE_BM_SLOTS=1 重开)\n");
        std::fflush(stdout);
    }

    // ---- Step 4 start: WSS AsyncConnect ----
    if (cfg_.start_live_feeds && live_transport_) {
        const std::string wss_url = "wss://ws-subscriptions-clob.polymarket.com/ws/market";
        std::printf("[paper_daemon] 连接 %s ...\n", wss_url.c_str());
        std::fflush(stdout);
        live_transport_->AsyncConnect(wss_url);
        std::printf("[paper_daemon] WSS io_thread_ 已启动, 等待 book 数据 (通常 1-5s)...\n");
        std::fflush(stdout);
        // REST 快照打底: 订阅时先拉一次初始 book, 不靠 WSS 推 (修"稳定盘/漏接初始快照永远空")。
        //   后台 jthread (不阻塞 HTTP/loop 启动, ~20s 完成; hub.Publish 线程安全)。WSS delta 随后更新。
        //   周期重打底 (2026-06-02 老板「订单为什么还是有空的订单簿」): 首次打底后每
        //   kReseedSec 重打底一次全部订阅 token —— 捞回 seed 之后才挂上单、但 WSS 未把后续簿
        //   推达的低活跃盘 (实证: Polymarket 有 13 档买盘, 我方 hub 却空)。读 TokenSnapshot 取
        //   当前集 (含 rediscover 后的变化), 成本低 (~80 token / 2 个 POST /books)。
        seed_thread_ = std::jthread([this](std::stop_token st) {
            SeedInitialBooksFromRest(st);  // 首次立即打底
            constexpr int kReseedSec = 45;
            while (!st.stop_requested()) {
                for (int i = 0; i < kReseedSec && !st.stop_requested(); ++i)
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                if (st.stop_requested()) break;
                SeedInitialBooksFromRest(st);  // 周期重打底: 捞回 WSS 漏推的后发簿
            }
        });
        // WSS 看门狗 (2026-06-02 会议): 心跳保活 (治 idle 超时真因) + 断线重连 (治不重连症状)。
        wss_watchdog_thread_ =
            std::jthread([this, wss_url](std::stop_token st) { WssWatchdogLoop(st, wss_url); });
        // 149hz 主动 book 轮询 (2026-06-04 老板): 热链 GET /book 压官方限速, 流动性加权, 主动补 WSS。
        active_poll_thread_ =
            std::jthread([this](std::stop_token st) { RunActiveBookPoller(st); });
        std::printf("[paper_daemon] 149hz 主动 book 轮询启动 (热链 /book, 流动性加权, 源头 pass 后 token)\n");
    }

    // ---- Step 4b start: PaperLoop (enable_paper_trading; "仅观测" flag=false 时不起) ----
    if (cfg_.enable_paper_trading && paper_loop_) {
        std::printf("[paper_daemon] 启动 paper 交易循环 (PaperLoop, 独立线程, 500ms tick)...\n");
        std::printf("[paper_daemon] [paper] R-11 隔离: PositionLedger 独立实例 (非 live 账本)\n");
        std::printf("[paper_daemon] [paper] R-20 透传: data_source_ts_ns 来自 CLOB hub 快照\n");
        std::printf("[paper_daemon] [paper] ToS: 仅 VirtualFill, 不向 CLOB 下单\n");
        std::fflush(stdout);
        paper_loop_->Start();
    }

    // ---- Step 4b' start: 映射刷新线程 (A1b; EventMatcher 周期匹配 condition↔goalserve + R-6 周期重发现) ----
    //   起交易 + 有 score_store + 刷新周期>0 即启动。R-6 修: 不再要求 market_match_inputs_ 非空 ——
    //   启动时 0 发现 (大赛空档) 也要起线程, 否则周期重发现永不跑, 卡死在 0 (live 比赛来了也捞不到)。
    if (cfg_.enable_paper_trading && cfg_.mapping_refresh_sec > 0 && score_store_ && paper_loop_ &&
        (!market_match_inputs_.empty() || cfg_.rediscover_interval_sec > 0)) {
        mapping_refresh_thread_ = std::jthread([this](std::stop_token st) { RefreshEventMapping(st); });
        std::printf("[paper_daemon] 映射刷新线程启动 (EventMatcher %ds + 周期重发现 %ds, 起始 %zu market)\n",
                    cfg_.mapping_refresh_sec, cfg_.rediscover_interval_sec, market_match_inputs_.size());
        std::fflush(stdout);
    }

    // (模型热重载 watcher + 进程内自动训练编排已砍 2026-06-05「砍掉大模型训练功能」: 原 RefreshModel /
    //  AutoTrain 线程。不再监测 .onnx mtime, 不再周期重训。)

    // ---- 采集数据磁盘守护 (老板「超过30g后开始删,一次删5G」) — 默认开 (仅超阈值才动) ----
    if (cfg_.disk_prune_threshold_gb > 0) {
        disk_prune_thread_ = std::jthread([this](std::stop_token st) { DiskPrune(st); });
        std::printf("[paper_daemon] 磁盘守护线程启动 (>%dGB 删 %dGB, 每 %ds 检查)\n",
                    cfg_.disk_prune_threshold_gb, cfg_.disk_prune_free_gb, cfg_.disk_prune_interval_sec);
        std::fflush(stdout);
    }

    // ---- Step 4c start: 结算/比分 recorder (回测数据; ML 特征 recorder 已砍 2026-06-05) ----
    if (settlement_recorder_) {
        settlement_recorder_->Start();
        std::printf("[paper_daemon] 结算落盘启动 (SettlementRecorder -> %s.settlements.jsonl, label y)\n",
                    cfg_.ml_path.c_str());
    }
    if (score_recorder_) {
        score_recorder_->Start();
        std::printf("[paper_daemon] 比分帧落盘启动 (ScoreFrameRecorder -> %s.scores.jsonl, 回测 P0)\n",
                    cfg_.ml_path.c_str());
    }

    // ---- Step 5 start: HttpServer ----
    if (server_) {
        server_->start();
        if (!server_->is_running()) {
            std::fprintf(stderr, "[paper_daemon] FATAL: HttpServer 无法在 %s:%u 启动 (端口被占用?)\n",
                         cfg_.host.c_str(), static_cast<unsigned>(cfg_.port));
        } else {
            std::printf("[paper_daemon] 观测/调试 API @ http://%s:%u (mode=%s)\n", cfg_.host.c_str(),
                        static_cast<unsigned>(cfg_.port), debug_api::exec_mode_str(cfg_.exec_mode));
        }
    } else {
        std::printf("[paper_daemon] Headless 模式: 无 HTTP 观测端 (stderr → journal)\n");
    }
    std::fflush(stdout);
}

// ---------------------------------------------------------------------------
// WaitForStop / RequestStop
// ---------------------------------------------------------------------------

void PaperDaemon::WaitForStop() {
    while (!stop_requested_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void PaperDaemon::RequestStop() noexcept {
    stop_requested_.store(true, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// Shutdown — 反序优雅停 (幂等). [R-11 INV-2] 严格序: paper_loop.Stop → detach → 析构.
// ---------------------------------------------------------------------------

void PaperDaemon::Shutdown() noexcept {
    if (shutdown_done_.exchange(true, std::memory_order_acq_rel)) {
        return;  // 幂等
    }

    // 0. A1b 映射刷新线程先停 (它 touch paper_loop_ + score_store_, 必在二者析构/停止前 join).
    if (mapping_refresh_thread_.joinable()) {
        mapping_refresh_thread_.request_stop();
        mapping_refresh_thread_.join();
    }
    // 0a. M2 结算刷新线程先停 (它 touch paper_loop_ + settlement_store_, 必在二者前 join).
    if (settlement_refresh_thread_.joinable()) {
        settlement_refresh_thread_.request_stop();
        settlement_refresh_thread_.join();
    }
    // 0a'. live_stats 刷新线程先停 (它 touch paper_loop_ + live_stats_store_ + score_store_ + poller).
    if (live_stats_refresh_thread_.joinable()) {
        live_stats_refresh_thread_.request_stop();
        live_stats_refresh_thread_.join();
    }
    if (commentaries_poller_) {
        commentaries_poller_->Stop();  // poller jthread join (先于 live_stats_store_ 析构)
    }
    // 0a''. WSS 看门狗先停 (它 touch live_transport_ + 会重赋 seed_thread_; 必在 seed_thread_ join
    //       与 live_transport_->Close() 之前 join, 否则重连/重赋有 race).
    if (wss_watchdog_thread_.joinable()) {
        wss_watchdog_thread_.request_stop();
        wss_watchdog_thread_.join();
    }
    // 0a'''. 149hz 主动轮询线程停 (它 touch live_publisher_/hub_ + 持 TLS 连接, 必在二者析构前 join).
    if (active_poll_thread_.joinable()) {
        active_poll_thread_.request_stop();
        active_poll_thread_.join();
    }
    // 0b. REST 快照打底线程先停 (它 touch live_publisher_/hub_, 必在二者析构前 join; st 令其提前退出).
    if (seed_thread_.joinable()) {
        seed_thread_.request_stop();
        seed_thread_.join();
    }

    // 1. HttpServer 先停 (停止读端, 不再读 real_provider_)
    if (server_) {
        server_->stop();
    }

    // 2. (ML 特征 recorder 已砍 2026-06-05) 结算/比分 recorder Stop (先于 store 析构)
    if (settlement_recorder_) {
        settlement_recorder_->Stop();  // 停读 settlement_store_ (先于其析构)
    }
    if (score_recorder_) {
        score_recorder_->Stop();  // 停读 score_store_ (先于其析构, 同 InplayFeedThread 之前)
    }

    // 3. PaperLoop (先于 hub/ledger/rm 析构; Stop 内含 jthread join)
    if (paper_loop_) {
        paper_loop_->Stop();
    }

    // 4. [R-11 INV-1/INV-2] detach 全局 hook —— 必在 paper_loop_ 停后 + paper_rm_snap_ 析构前.
    //    幂等: detach 只在曾 attach 时调一次 (避免清掉非本 daemon 的 hook).
    if (attached_rm_snap_) {
        risk::detach_rm_debug_snapshot();
        attached_rm_snap_ = false;
    }

    // 5. InplayFeedThread (先于 score_store_ 析构; Stop 内含 join)
    if (inplay_feed_) {
        inplay_feed_->Stop();
    }

    // 5a. M2 SettlementPoller (先于 settlement_store_ 析构; Stop 内含 jthread join)
    if (settlement_poller_) {
        settlement_poller_->Stop();
    }

    // 6. LiveWssTransport (Close 内含 join io_thread_ + send_thread_)
    if (live_transport_) {
        live_transport_->Close();
    }

    if (paper_loop_) {
        std::fprintf(stderr, "[paper_daemon] 已停止. ticks=%llu approved=%llu fills=%llu\n",
                     static_cast<unsigned long long>(paper_loop_->stats().ticks_total.load()),
                     static_cast<unsigned long long>(paper_loop_->stats().orders_approved.load()),
                     static_cast<unsigned long long>(paper_loop_->stats().fills_completed.load()));
    }
}

// ---------------------------------------------------------------------------
// Run — 便捷封装
// ---------------------------------------------------------------------------

int PaperDaemon::Run() {
    if (!built_) {
        std::fprintf(stderr, "[paper_daemon] ERROR: Run() 前必须先 Build()\n");
        return 1;
    }
    Start();
    WaitForStop();
    Shutdown();
    return 0;
}

// ---------------------------------------------------------------------------
// RefreshEventMapping — 映射刷新线程主体 (A1b)
//   周期: 取 score_store 快照 → 对每个 market 跑 EventMatcher → 构建 condition→event
//   映射 → paper_loop_->SetEventMapping(). Goalserve event 动态出现, 故周期重匹配.
//   fail-closed: 未匹配的 condition 不进映射 (paper_loop 退回 stub, has_real_fair=false).
// ---------------------------------------------------------------------------
void PaperDaemon::RefreshEventMapping(std::stop_token st) {
    using namespace std::chrono;
    // R-6: 周期重发现计时 (本线程跑 → match_inputs 同线程无竞争)。初始化为 now, 首次重发现在一个间隔后。
    auto last_rediscover = steady_clock::now();
    // 源头 pass (2026-06-04 老板「从源头就不订阅无赔率源的比赛」): condition → 末次有 sharp 赔率时刻。
    //   仅本线程读写 (无锁)。每周期更新 + 算 eligible 集 (grace 滞回内有过 sharp) 发布给 PopulateCatalog。
    std::unordered_map<std::string, std::int64_t> sharp_last_seen;
    constexpr std::int64_t kSharpGraceNs = 180LL * 1'000'000'000;  // 3min 滞回: halftime 赔率挂起不退订
    while (!st.stop_requested()) {
        // 0. R-6 周期重发现 (间隔到 → 全量重建 catalog + WSS 重订; 在 match 之前, match_inputs 已是新版)。
        if (cfg_.rediscover_interval_sec > 0 &&
            steady_clock::now() - last_rediscover >= seconds(cfg_.rediscover_interval_sec)) {
            RediscoverOnce(st);
            last_rediscover = steady_clock::now();
        }
        // 1. 取 Goalserve 比分快照 → 候选 EventScore 列表
        std::vector<debug_api::EventScore> candidates;
        if (score_store_ != nullptr) {
            const auto snap = score_store_->GetSnapshot();  // shared_ptr<const ScoreMap>
            if (snap) {
                candidates.reserve(snap->size());
                for (const auto& [_id, es] : *snap) {
                    candidates.push_back(es);
                }
            }
        }

        // 2. 对每个 market 跑 EventMatcher → 构建新映射 (fail-closed: 未匹配不入)
        // A5: 记录 match_confidence(team_score) + match_as_of_ns(本刷新时刻) — join 边质量一等暴露
        // (观测/输入)。
        const std::int64_t refresh_now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                std::chrono::system_clock::now().time_since_epoch())
                                                .count();
        auto new_map = std::make_shared<paper::ConditionEventMap>();
        std::size_t matched = 0;
        // 结束检测 (2026-06-05 老板 a: 扩到所有终态): status=="final"(Ended) 仅是终态之一; c.is_terminal
        //   覆盖 IsTerminal 全集 (Retired/Walkover/Abandoned/Cancelled/Postponed/Removed —— 这些被
        //   MapStatus 折成 "pregame", 单看 status 漏判)。网球退赛/比赛腰斩等"完赛"靠此识别 → 退订。
        std::unordered_map<std::string, bool> final_by_match_id;
        for (const auto& c : candidates)
            final_by_match_id[c.event_id] = (c.status == "final") || c.is_terminal;
        // [DIAG] 候选 Goalserve event + PM 待匹配 market 并排 (定位 0 匹配根因: 空候选/名不符/sport/kickoff).
        //   改: 候选非空时才 dump (避开启动 feed 未拉到的首轮空窗), 候选名 + 市场名并排各 16 条。
        static bool diag_mapping_dumped = false;
        if (!diag_mapping_dumped && !candidates.empty()) {
            std::fprintf(stderr, "[map-diag] 候选 EventScore=%zu, 待匹配 market=%zu\n", candidates.size(),
                         market_match_inputs_.size());
            std::size_t shown = 0;
            std::map<std::string, int> sport_cnt;  // 候选按 sport 计数
            for (const auto& c : candidates) {
                ++sport_cnt[c.sport];
                std::fprintf(stderr, "[map-diag]   cand: home='%s' away='%s' sport='%s' kickoff=%lld\n",
                             c.home.c_str(), c.away.c_str(), c.sport.c_str(),
                             static_cast<long long>(c.kickoff_ts_sec));
                if (++shown >= 60) break;
            }
            for (const auto& [sp, n] : sport_cnt)
                std::fprintf(stderr, "[map-diag] 候选 sport 计数: %s=%d\n", sp.c_str(), n);
            shown = 0;
            for (const auto& [cid, in] : market_match_inputs_) {
                std::fprintf(stderr, "[map-diag]   market: t0='%s' t1='%s' sport='%s' kickoff=%lld\n",
                             in.team0.c_str(), in.team1.c_str(), in.sport.c_str(),
                             static_cast<long long>(in.kickoff_ts_sec));
                if (++shown >= 16) break;
            }
            diag_mapping_dumped = true;
        }
        debug_api::MappingStatusReport map_report;  // 可观测: 本轮映射快照
        map_report.total_markets = static_cast<int>(market_match_inputs_.size());
        // [DIAG] 未匹配诊断 (一次): 对未匹配 market 扫全候选找最佳, 暴露"最接近候选+分数" →
        //   板上钉钉区分 覆盖没重叠(best 低) vs 匹配 bug(best 高却没匹配)。用进程内候选池, 不直连。
        static bool diag_unmatched_dumped = false;
        std::size_t unmatched_shown = 0;
        const bool do_unmatched_diag = (!diag_unmatched_dumped && !candidates.empty());
        // 无赔率源记录 (2026-06-04 老板「源头pass无赔率源, 匹配不上的记录, api可查」):
        //   候选按 event_id 索引 → matched 行 O(1) 查 bet365 赔率有无 (区分真可交易 vs matched-no-sharp)。
        std::unordered_map<std::string, const debug_api::EventScore*> cand_by_id;
        cand_by_id.reserve(candidates.size() * 2);
        for (const auto& c : candidates) cand_by_id[c.event_id] = &c;
        constexpr std::size_t kMaxNoSharpReport = 300;  // 明细上限 (api 清单, 防爆内存)
        int matched_with_sharp = 0, matched_no_sharp = 0, no_match = 0;
        const std::int64_t now_s_loop = std::chrono::duration_cast<std::chrono::seconds>(
                                            std::chrono::system_clock::now().time_since_epoch())
                                            .count();
        for (const auto& [cond_id, in] : market_match_inputs_) {
            const auto r = event_matcher_.Match(in, candidates);
            if (!r.matched) {
                ++no_match;
                // 无 Goalserve 候选可匹配 → 源头 pass。记最佳候选+相似分 (诊断覆盖缺口 vs 名字 bug)。
                if (map_report.no_sharp.size() < kMaxNoSharpReport) {
                    double best = -1.0;
                    std::string bh, ba;
                    for (const auto& c : candidates) {
                        if (c.home.empty() || c.away.empty()) continue;
                        const double d = std::min(EventMatcher::TeamSimilarity(in.team0, c.home),
                                                  EventMatcher::TeamSimilarity(in.team1, c.away));
                        const double x = std::min(EventMatcher::TeamSimilarity(in.team0, c.away),
                                                  EventMatcher::TeamSimilarity(in.team1, c.home));
                        const double s = std::max(d, x);
                        if (s > best) { best = s; bh = c.home; ba = c.away; }
                    }
                    const char* ks = (in.kickoff_ts_sec <= 0)          ? "无ts"
                                     : (in.kickoff_ts_sec <= now_s_loop) ? "在打"
                                                                         : "赛前";
                    if (do_unmatched_diag && unmatched_shown < 40) {
                        std::fprintf(stderr,
                                     "[map-unmatched][%s] '%s' vs '%s' (%s) → 最佳候选 '%s' vs '%s' "
                                     "score=%.2f\n",
                                     ks, in.team0.c_str(), in.team1.c_str(), in.sport.c_str(), bh.c_str(),
                                     ba.c_str(), best);
                        ++unmatched_shown;
                    }
                    debug_api::MappingNoSharpRow nr;
                    nr.condition_id = cond_id;
                    nr.team0 = in.team0;
                    nr.team1 = in.team1;
                    nr.sport = in.sport;
                    nr.reason = "no_goalserve_match";
                    nr.best_home = bh;
                    nr.best_away = ba;
                    nr.best_score = best;
                    nr.kickoff_state = ks;
                    map_report.no_sharp.push_back(std::move(nr));
                }
            }
            if (r.matched) {
                // 完赛/终态前置判定 (2026-06-05 老板 a+b): final_by_match_id 已含所有终态 (上方 Fix a)。
                //   终态 → ① 立即 sharp_last_seen.erase (本轮即掉出 eligible, 不等 3min grace, 不刷新)
                //   ② 入黑名单 (discovery 层不再重订)。eligible 同时驱动 all_token_ids_(WSS)+poll_plan
                //   (149hz 订单簿轮询), 故掉出 eligible = 两路一起断 (老板「wss 和访问订单簿的 api 都断」)。
                //   否则: 完赛盘 feed 仍带冻结 bet365 赔率 → has_sharp=true → sharp_last_seen 每轮刷新 →
                //   永留 eligible → 永不退订 (本次修复的真 bug)。
                auto fit = final_by_match_id.find(r.inplay_match_id);
                const bool is_final = (fit != final_by_match_id.end() && fit->second);

                // sharp 赔率源校验 (源头 pass): matched event 须有 bet365 inplay 赔率才算真可交易;
                //   matched-no-sharp = 匹配上但无赔率 → 记清单 + 不计入可交易。
                const auto cf = cand_by_id.find(r.inplay_match_id);
                const debug_api::EventScore* cand = (cf != cand_by_id.end()) ? cf->second : nullptr;
                const bool has_sharp =
                    cand && (cand->inplay_bet365_home_fair >= 0.0 || cand->inplay_bet365_away_fair >= 0.0 ||
                             (in.is_draw && cand->inplay_bet365_draw_fair >= 0.0));
                if (is_final) {
                    // 完赛: 立即掉出 eligible (即便 feed 仍挂冻结赔率); 下轮 RediscoverOnce 退订 WSS + 轮询。
                    //   只在【转移瞬间】(原在 eligible, 此刻被 erase 掉) 打一行可追溯日志 (§7); 后续刷新
                    //   erase 返 0 不再 log (防刷屏)。这是 Fix B 终态即退的活证锚点。
                    if (sharp_last_seen.erase(cond_id) > 0) {
                        std::fprintf(stderr,
                                     "[paper_daemon] 完赛退订: cond=%.18s.. 终态(is_terminal) → 立即掉出 "
                                     "eligible (下轮 RediscoverOnce 断 WSS+订单簿轮询)\n",
                                     cond_id.c_str());
                        std::fflush(stderr);
                    }
                } else if (has_sharp) {
                    ++matched_with_sharp;
                    sharp_last_seen[cond_id] = refresh_now_ns;  // 源头 pass: 记末次有赔率时刻 (grace 滞回)
                } else {
                    ++matched_no_sharp;
                    if (map_report.no_sharp.size() < kMaxNoSharpReport) {
                        debug_api::MappingNoSharpRow nr;
                        nr.condition_id = cond_id;
                        nr.team0 = in.team0;
                        nr.team1 = in.team1;
                        nr.sport = in.sport;
                        nr.reason = "matched_no_sharp";
                        nr.best_score = -1.0;
                        nr.kickoff_state = (in.kickoff_ts_sec <= 0)          ? "无ts"
                                           : (in.kickoff_ts_sec <= now_s_loop) ? "在打"
                                                                               : "赛前";
                        nr.matched_event_id = r.inplay_match_id;
                        map_report.no_sharp.push_back(std::move(nr));
                    }
                }
                // 结束→拉黑 (老板): 终态 → 把该盘的 PM event_id 入黑名单, 下轮 RediscoverOnce 退订其
                //   token + 释放 hub, 且不再重订 (即便 gamma 仍列, 等结算)。
                if (is_final) {
                    const auto cit = market_catalog_.find(cond_id);
                    if (cit != market_catalog_.end() && !cit->second.event_id.empty()) {
                        std::lock_guard<std::mutex> lk(ended_blacklist_mu_);
                        if (ended_event_blacklist_.size() > kBlacklistCap) ended_event_blacklist_.clear();
                        ended_event_blacklist_[cit->second.event_id] = refresh_now_ns + kBlacklistTtlNs;
                    }
                }
                paper::EventMapEntry entry;
                entry.inplay_match_id = r.inplay_match_id;
                entry.yes_is_home = r.yes_is_home;
                entry.is_draw = in.is_draw;  // 3-way 平局盘标志透传 → 下游 sharp fair 选 draw
                entry.seg_index = in.seg_index;  // A-step-2: 分盘号透传 → 下游用当前段 fair
                entry.match_confidence = r.team_score;
                entry.match_as_of_ns = refresh_now_ns;
                (*new_map)[cond_id] = std::move(entry);
                ++matched;
                // 可观测: 仅记 matched 行 (诊断面板; 未匹配的 378 行不全记)
                debug_api::MappingMarketRow row;
                row.condition_id = cond_id;
                row.team0 = in.team0;
                row.team1 = in.team1;
                row.is_draw = in.is_draw;
                row.matched = true;
                row.inplay_match_id = r.inplay_match_id;
                row.match_confidence = r.team_score;
                map_report.markets.push_back(std::move(row));
            }
        }
        if (do_unmatched_diag) diag_unmatched_dumped = true;  // 未匹配诊断一次即停

        // 3. 推送映射给 PaperLoop (热刷)
        if (paper_loop_) {
            paper_loop_->SetEventMapping(std::shared_ptr<const paper::ConditionEventMap>(std::move(new_map)));
        }

        // 3b. 可观测: 构建映射状态报告 (matched 行 + Goalserve live 候选) → push debug_api。
        map_report.matched = static_cast<int>(matched);
        map_report.matched_with_sharp = matched_with_sharp;
        map_report.matched_no_sharp = matched_no_sharp;
        map_report.no_match = no_match;
        map_report.live_games = static_cast<int>(candidates.size());
        for (const auto& c : candidates) {
            debug_api::MappingLiveGame g;
            g.event_id = c.event_id;
            g.home = c.home;
            g.away = c.away;
            g.sport = c.sport;
            g.status = c.status;
            g.home_score = c.home_score;
            g.away_score = c.away_score;
            map_report.games.push_back(std::move(g));
        }
        if (real_provider_) {
            real_provider_->set_mapping_status(std::move(map_report));
        }

        // 源头 pass: 发布 sharp-eligible condition 集 (grace 滞回内有过 bet365 赔率) → PopulateCatalog
        //   据此过滤 all_token_ids_, 只订有赔率源的盘 (老板「从源头就不订阅无赔率源的比赛」)。过期清理。
        {
            auto eligible = std::make_shared<std::unordered_set<std::string>>();
            eligible->reserve(sharp_last_seen.size());
            for (auto it = sharp_last_seen.begin(); it != sharp_last_seen.end();) {
                if (refresh_now_ns - it->second < kSharpGraceNs) {
                    eligible->insert(it->first);
                    ++it;
                } else {
                    it = sharp_last_seen.erase(it);  // 超 grace: 清理 + 不入 eligible (退订)
                }
            }
            PublishSharpConditions(std::move(eligible));
        }
        std::fprintf(stderr,
                     "[paper_daemon] 映射刷新: %zu/%zu market 匹配, sharp-eligible(订阅)=%zu (源头 pass 无赔率源)\n",
                     matched, market_match_inputs_.size(), sharp_last_seen.size());

        // [COVERAGE-DIAG] 覆盖率门诊断 (2026-06-03, 老板「是不是名字不匹配」):
        //   隔离「名字配上 threshold 却被后续门拒」的 market = 可恢复缺口 + 凶手门。
        //   threshold 拒绝(错选手)不计入 — 那是正常的会淹没信号。
        {
            const auto d = event_matcher_.DiagSnapshot();
            std::fprintf(stderr,
                         "[cov-diag] calls=%lld matched=%lld | 名字配上=%lld 其中没匹配=%lld "
                         "(orientation门拒=%lld kickoff门拒=%lld)\n",
                         static_cast<long long>(d.calls), static_cast<long long>(d.matched),
                         static_cast<long long>(d.namematch_found),
                         static_cast<long long>(d.namematch_but_unmatched),
                         static_cast<long long>(d.rej_orientation),
                         static_cast<long long>(d.rej_kickoff));
            event_matcher_.DiagReset();  // 每周期清零, 看 per-cycle
        }

        // 4. 间隔 sleep (响应 stop_token; 不 spinlock)
        const auto deadline = steady_clock::now() + seconds(cfg_.mapping_refresh_sec);
        while (steady_clock::now() < deadline) {
            if (st.stop_requested()) {
                return;
            }
            std::this_thread::sleep_for(milliseconds(100));
        }
    }
}

// ---------------------------------------------------------------------------
// RunActiveBookPoller — 149hz 主动 book 轮询 (2026-06-04 老板「主动查订单簿压官方限速,
//   全市场共享 149hz, 按流动性分配, 用热链」)。
//   热链 (PersistentHttps keep-alive) GET /book?token_id=X → live_publisher_->SeedFromRestBooks → hub。
//   smooth weighted round-robin 按 √liquidity 分配; 节流到 ~149 req/s (压着 PM /book 150/s 限速)。
//   只轮询源头 pass 后有赔率源的 token (poll_plan_snapshot_)。WSS 仍并行推 (此为主动补/压频)。
//   R-12: 独立线程, 非 WSS event loop。
// ---------------------------------------------------------------------------
void PaperDaemon::RunActiveBookPoller(std::stop_token st) noexcept {
    using namespace std::chrono;
    if (live_publisher_ == nullptr) return;
    constexpr double kTargetRps = 149.0;
    const auto kReqInterval = nanoseconds(static_cast<long long>(1e9 / kTargetRps));  // ~6.71ms/req
    net::PersistentHttps https("clob.polymarket.com", /*timeout_ms=*/4000);

    std::unordered_map<std::string, double> cw;  // smooth-WRR current_weight (跨迭代保留, 仅本线程)
    std::shared_ptr<const std::vector<std::pair<std::string, double>>> plan;
    auto last_plan_refresh = steady_clock::now() - seconds(10);  // 立即首刷
    auto next_req = steady_clock::now();
    std::uint64_t since_log = 0;

    while (!st.stop_requested()) {
        // 每 1s 刷新 plan (源头 pass eligible 变化时); 清理 cw 中已退出 plan 的 token。
        if (steady_clock::now() - last_plan_refresh >= seconds(1)) {
            plan = PollPlanSnapshot();
            last_plan_refresh = steady_clock::now();
            if (plan && !plan->empty()) {
                std::unordered_set<std::string> live;
                live.reserve(plan->size() * 2);
                for (const auto& [t, _w] : *plan) live.insert(t);
                for (auto it = cw.begin(); it != cw.end();) {
                    if (live.find(it->first) == live.end()) it = cw.erase(it);
                    else ++it;
                }
            }
        }
        if (!plan || plan->empty()) {
            std::this_thread::sleep_for(milliseconds(200));  // 无赔率源 token → 等
            continue;
        }
        // smooth weighted round-robin: 选 current_weight 最大者, 选后减 total_weight (nginx 法)。
        double total_w = 0.0, best = -1e300;
        std::string pick;
        for (const auto& [t, w] : *plan) {
            double& c = cw[t];
            c += w;
            total_w += w;
            if (c > best) {
                best = c;
                pick = t;
            }
        }
        if (!pick.empty()) cw[pick] -= total_w;

        // 节流到 149 req/s。
        const auto now = steady_clock::now();
        if (now < next_req && !st.stop_requested()) {
            std::this_thread::sleep_for(std::min(next_req - now, nanoseconds(kReqInterval)));
        }
        next_req = steady_clock::now() + kReqInterval;
        if (pick.empty() || st.stop_requested()) continue;

        // GET /book 单 token (token_id 全数字, path 安全; 公开端点无 key)。
        const std::string resp = https.Get("/book?token_id=" + pick);
        active_poll_total_.fetch_add(1, std::memory_order_relaxed);
        if (!resp.empty() && resp.find("\"asset_id\"") != std::string::npos) {
            const std::int64_t now_ns =
                duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
            live_publisher_->SeedFromRestBooks(resp, now_ns);
            active_poll_ok_.fetch_add(1, std::memory_order_relaxed);
        }
        if (++since_log >= 1490) {  // ~每 10s 记一次 (149/s × 10)
            since_log = 0;
            std::fprintf(stderr, "[active-poll] 主动轮询 total=%llu ok=%llu tokens=%zu (149hz 热链)\n",
                         static_cast<unsigned long long>(active_poll_total_.load()),
                         static_cast<unsigned long long>(active_poll_ok_.load()), plan->size());
        }
    }
}

// ---------------------------------------------------------------------------
// RefreshResolution — M2 结算刷新线程主体
//   周期取 SettlementStore 快照 → 构建 condition→ResolutionEntry → SetResolutionByCondition。
//   ResolutionEntry.status = SettlementRecord.resolution_status() (0Open/1Resolving/2Resolved);
//   .winner = settlement_value (-1/0/1, 语义直对齐)。3b 权威结算 (SettleCondition) 已接, 此线喂数。
// ---------------------------------------------------------------------------
void PaperDaemon::RefreshResolution(std::stop_token st) {
    using namespace std::chrono;
    while (!st.stop_requested()) {
        if (settlement_store_ && paper_loop_) {
            const auto snap = settlement_store_->GetSnapshot();  // shared_ptr<const SettlementMap>
            if (snap) {
                std::unordered_map<std::string, paper::ResolutionEntry> res_map;
                res_map.reserve(snap->size());
                std::size_t resolved = 0;
                // 新鲜度锚: 本次 resolution 刷新时刻 (老板「每个源标时间」→ g_resolution_age_sec)。
                const std::int64_t res_fetch_ns =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
                for (const auto& [cid, rec] : *snap) {
                    paper::ResolutionEntry e;
                    e.status = rec.resolution_status();
                    e.winner = rec.settlement_value;  // -1/0/1 直对齐 ResolutionEntry.winner
                    e.fetched_at_ns = res_fetch_ns;
                    res_map[cid] = e;
                    if (e.status == 2) ++resolved;
                }
                paper_loop_->SetResolutionByCondition(std::move(res_map));
                std::fprintf(stderr, "[paper_daemon] 结算刷新: %zu market (%zu resolved)\n", snap->size(),
                             resolved);
            }
        }
        const auto deadline = steady_clock::now() + seconds(2);  // 2026-06-01: 30s->2s (本地刷新, 无 API)
        while (steady_clock::now() < deadline) {
            if (st.stop_requested()) return;
            std::this_thread::sleep_for(milliseconds(100));
        }
    }
}

// ---------------------------------------------------------------------------
// RefreshLiveStats — live_stats 刷新线程主体
//   每周期: ① 从 score store 收集当前活跃 league_id → 更新 poller 轮询集 (league 仅运行期才知);
//           ② live_stats_store 快照 → paper_loop_->SetLiveStatsByTeams (join_key→LiveStatsFields)。
//   poller 自身线程负责 fetch/parse; 本线程只做 league 收集 + 快照转交。喂 5 个 g_*_diff 特征。
// ---------------------------------------------------------------------------
void PaperDaemon::RefreshLiveStats(std::stop_token st) {
    using namespace std::chrono;
    // 2026-06-02 特征审计 #19-23: 改用 soccernew/live (后台验证: 真实 live_stats 在此, 含
    //   ICorner/IDangerousAttacks/IOnTarget/IPosession/IRedCard KV)。替换失效的 per-league
    //   commentaries (找错标签 + live 小联赛返空)。一次拉全部直播联赛, 逐 match 取 <category id> 作 league。
    const char* gs_key_env = std::getenv("GOALSERVE_API_KEY");
    const std::string gs_key = gs_key_env ? gs_key_env : "";
    const char* gs_proxy_env = std::getenv("GOALSERVE_PROXY");
    const std::string gs_proxy = gs_proxy_env ? gs_proxy_env : "";
    // 2026-06-02 实测教训 (老板「自己验证」): soccernew/live 与 inplay feed 的 league_id 与队名
    //   **两者都不同空间** ("China U20" vs "China PR Youth"; Asean U19 2417 vs 1362) → 原 (league,队名)
    //   join 两端永不匹配 (真 bug 非覆盖)。改用 inplay-mapping 桥 (与 bm_slots 同): soccernew match id
    //   (pregame) → inplay_match_id → paper_loop 按 inplay_match_id join。
    auto fetch = [&gs_proxy](const std::string& url) -> std::string {
        std::string cmd = "curl -s --max-time 15 ";
        if (!gs_proxy.empty()) {
            cmd += "-x '";
            cmd += gs_proxy;
            cmd += "' ";
        }
        cmd += "'";
        cmd += url;
        cmd += "'";
        std::string out;
        if (FILE* p = ::popen(cmd.c_str(), "r")) {
            char buf[8192];
            std::size_t n;
            while ((n = std::fread(buf, 1, sizeof(buf), p)) > 0)
                out.append(buf, n);
            ::pclose(p);
        }
        return out;
    };
    while (!st.stop_requested()) {
        if (!gs_key.empty() && paper_loop_ != nullptr) {
            const std::string live_xml = fetch("https://www.goalserve.com/getfeed/" + gs_key + "/soccernew/live");
            data::livescore::LiveStatsMap by_pregame;  // 键 = soccernew match id (pregame)
            if (!live_xml.empty())
                data::livescore::CommentariesParser::ParseSoccernewLiveInto(by_pregame, live_xml);
            if (!by_pregame.empty()) {
                // inplay-mapping: pregame_match_id → inplay_match_id (桥到 paper_loop join key)
                const std::string map_xml =
                    fetch("https://www.goalserve.com/getfeed/" + gs_key + "/soccernew/inplay-mapping");
                std::unordered_map<std::string, std::string> pre2inp;
                for (auto& [pre, inp] : data::goalserve::ParseInplayMappingXml(map_xml))
                    pre2inp.emplace(std::move(pre), std::move(inp));
                data::livescore::LiveStatsMap by_inplay;  // 键 = inplay_match_id (= paper_loop es.event_id)
                const std::int64_t as_of_ns = duration_cast<nanoseconds>(
                                                  system_clock::now().time_since_epoch())
                                                  .count();
                for (auto& [pre, stats] : by_pregame) {
                    const auto it = pre2inp.find(pre);
                    if (it == pre2inp.end())
                        continue;  // 无 inplay 映射 (非直播) → 跳过
                    stats.as_of_ts_ns = as_of_ns;  // 新鲜度 → g_live_stats_age_sec
                    by_inplay[it->second] = stats;
                }
                const std::size_t n = by_inplay.size();
                paper_loop_->SetLiveStatsByTeams(std::move(by_inplay));
                static int ls_log_throttle = 0;
                if ((ls_log_throttle++ % 5) == 0)  // 每 60s 一行
                    std::fprintf(stderr,
                                 "[live_stats] soccernew/live %zu 场解析 → %zu 场桥到 inplay_match_id\n",
                                 by_pregame.size(), n);
            }
        }
        const auto deadline = steady_clock::now() + seconds(12);  // soccernew/live 单拉 (有 API), 12s 周期
        while (steady_clock::now() < deadline) {
            if (st.stop_requested())
                return;
            std::this_thread::sleep_for(milliseconds(150));
        }
    }
}

// ---------------------------------------------------------------------------
// RefreshOdds — bm_slots 跨庄家赔率刷新 (特征审计 #5/6/7/16; 2026-06-02)。
//   周期 popen curl getodds (HTTPS, HTTP 500 但 body 有效) + inplay-mapping (pregame↔inplay id),
//   join → OddsMap[inplay_match_id] → paper_loop_->SetOddsByMatchId()。
//   R-12: 独立 jthread, popen 阻塞 IO 在本线程, 不进 WSS event loop。key 在 URL → https+proxy 保护。
//   cat/slug 来自 enum (无注入)。getodds 覆盖 in-play (Agent C 实测); 无 mapping 的盘 (非直播) 跳过。
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// RefreshTennisScores — 覆盖率杠杆 (2026-06-03): tennis_scores/home 全巡回 livescore (含 inplay 缺的
//   ITF/Challenger) → ParseTennisScoresLive (仅 live Set N) → InjectSupplementalScores 并入 score store
//   → EventMatcher 配 PM ITF 盘。tennis_scores ~194KB (远小于 getodds 45MB, 无 WSS 带宽风险)。
//   无 bet365 赔率 → 这些场走 score-prior fair (无 sharp 锚), 但覆盖率↑+比分特征。
// ---------------------------------------------------------------------------
void PaperDaemon::RefreshTennisScores(std::stop_token st) {
    using namespace std::chrono;
    const char* gs_key_env = std::getenv("GOALSERVE_API_KEY");
    const std::string gs_key = gs_key_env ? gs_key_env : "";
    const char* gs_proxy_env = std::getenv("GOALSERVE_PROXY");
    const std::string gs_proxy = gs_proxy_env ? gs_proxy_env : "";
    auto fetch = [&gs_proxy](const std::string& url) -> std::string {
        std::string cmd = "curl -s --max-time 15 ";
        if (!gs_proxy.empty()) {
            cmd += "-x '";
            cmd += gs_proxy;
            cmd += "' ";
        }
        cmd += "'";
        cmd += url;
        cmd += "'";
        std::string out;
        if (FILE* p = ::popen(cmd.c_str(), "r")) {
            char buf[8192];
            std::size_t n;
            while ((n = std::fread(buf, 1, sizeof(buf), p)) > 0)
                out.append(buf, n);
            ::pclose(p);
        }
        return out;
    };
    while (!st.stop_requested()) {
        if (!gs_key.empty() && inplay_feed_) {
            const std::string xml =
                fetch("https://www.goalserve.com/getfeed/" + gs_key + "/tennis_scores/home");
            if (!xml.empty()) {
                const std::int64_t ing = duration_cast<nanoseconds>(
                                             system_clock::now().time_since_epoch())
                                             .count();
                auto recs = data::tennis_scores::ParseTennisScoresLive(xml, ing);
                const std::size_t n = recs.size();
                const std::size_t injected =
                    inplay_feed_->InjectSupplementalScores("tennis_scores", std::move(recs));
                static int ts_throttle = 0;
                if ((ts_throttle++ % 4) == 0)  // 每 60s 一行
                    std::fprintf(stderr,
                                 "[tennis_scores] live=%zu 场, 净注入=%zu (去重后 inplay 没有的; 增益来源)\n",
                                 n, injected);
            }
        }
        const auto deadline = steady_clock::now() + seconds(15);
        while (steady_clock::now() < deadline) {
            if (st.stop_requested())
                return;
            std::this_thread::sleep_for(milliseconds(150));
        }
    }
}

// RefreshTeamLivescores — 覆盖率杠杆 (2026-06-03, 老板「GS 其他接口有没有」+「加吧加吧」):
//   队制 livescore 补充源 — cricket/livescore (inplay-cricket 404 → 候选 0, PM 有 crint/blast)
//   + esports/home (比 inplay-esports ~4 更宽)。覆盖率诊断证: 匹配器无问题, 瓶颈是候选池覆盖。
//   feed 小 (cricket ~320KB / esports ~45KB, www 端点不限速) → 30s 轮询, 无 WSS 带宽风险。
//   无 bet365 赔率 → 走 score-prior fair; cricket 定价模型另立 (本期只为覆盖 + 比分特征)。
// ---------------------------------------------------------------------------
void PaperDaemon::RefreshTeamLivescores(std::stop_token st) {
    using namespace std::chrono;
    const char* gs_key_env = std::getenv("GOALSERVE_API_KEY");
    const std::string gs_key = gs_key_env ? gs_key_env : "";
    const char* gs_proxy_env = std::getenv("GOALSERVE_PROXY");
    const std::string gs_proxy = gs_proxy_env ? gs_proxy_env : "";
    auto fetch = [&gs_proxy](const std::string& url) -> std::string {
        std::string cmd = "curl -s --max-time 20 ";
        if (!gs_proxy.empty()) {
            cmd += "-x '";
            cmd += gs_proxy;
            cmd += "' ";
        }
        cmd += "'";
        cmd += url;
        cmd += "'";
        std::string out;
        if (FILE* p = ::popen(cmd.c_str(), "r")) {
            char buf[8192];
            std::size_t n;
            while ((n = std::fread(buf, 1, sizeof(buf), p)) > 0)
                out.append(buf, n);
            ::pclose(p);
        }
        return out;
    };
    // (source_id, feed 路径, 解析 spec)
    struct Src {
        const char* source_id;
        const char* path;
        data::team_livescore::TeamLivescoreSpec spec;
    };
    const Src srcs[] = {
        {"cricket", "cricket/livescore",
         {"cricket", "In Progress", "totalscore", "visitorteam"}},
        {"esports", "esports/home", {"esports", "Started", "score", "awayteam"}},
    };
    int throttle = 0;
    while (!st.stop_requested()) {
        if (!gs_key.empty() && inplay_feed_) {
            for (const auto& s : srcs) {
                if (st.stop_requested())
                    return;
                const std::string xml =
                    fetch("https://www.goalserve.com/getfeed/" + gs_key + "/" + s.path);
                if (xml.empty())
                    continue;
                const std::int64_t ing =
                    duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
                auto recs = data::team_livescore::ParseTeamLivescoreLive(xml, ing, s.spec);
                const std::size_t n = recs.size();
                const std::size_t injected =
                    inplay_feed_->InjectSupplementalScores(s.source_id, std::move(recs));
                if ((throttle % 2) == 0)  // 每 60s 一轮 (2 源 × 30s)
                    std::fprintf(stderr,
                                 "[%s] live=%zu 场, 净注入=%zu (候选池补充; inplay 缺的)\n",
                                 s.source_id, n, injected);
            }
            ++throttle;
        }
        const auto deadline = steady_clock::now() + seconds(30);
        while (steady_clock::now() < deadline) {
            if (st.stop_requested())
                return;
            std::this_thread::sleep_for(milliseconds(150));
        }
    }
}

void PaperDaemon::RefreshOdds(std::stop_token st) {
    using namespace std::chrono;
    namespace gs = data::goalserve;
    const char* gs_key_env = std::getenv("GOALSERVE_API_KEY");
    const std::string gs_key = gs_key_env ? gs_key_env : "";
    const char* gs_proxy_env = std::getenv("GOALSERVE_PROXY");
    const std::string gs_proxy = gs_proxy_env ? gs_proxy_env : "";
    // bm_slots 覆盖运动 (有 getodds cat + inplay-mapping slug; esports 无 odds dict → 跳过)
    static constexpr gs::GoalserveSport kOddsSports[] = {
        gs::GoalserveSport::Soccer,   gs::GoalserveSport::Basketball,       gs::GoalserveSport::Tennis,
        gs::GoalserveSport::Baseball, gs::GoalserveSport::AmericanFootball, gs::GoalserveSport::Hockey,
    };
    auto fetch = [&gs_proxy](const std::string& url) -> std::string {
        // --limit-rate: 跨洋带宽紧, getodds 大 (45MB), 限速防吃光带宽挤垮 WSS (2026-06-02 事故)。
        std::string cmd = "curl -s --max-time 40 --limit-rate 250k ";
        if (!gs_proxy.empty()) {
            cmd += "-x '";
            cmd += gs_proxy;
            cmd += "' ";
        }
        cmd += "'";
        cmd += url;
        cmd += "'";
        std::string out;
        if (FILE* p = ::popen(cmd.c_str(), "r")) {
            char buf[8192];
            std::size_t n;
            while ((n = std::fread(buf, 1, sizeof(buf), p)) > 0)
                out.append(buf, n);
            ::pclose(p);
        }
        return out;
    };
    auto interruptible_sleep = [&st](milliseconds dur) -> bool {
        const auto deadline = steady_clock::now() + dur;
        while (steady_clock::now() < deadline) {
            if (st.stop_requested())
                return false;
            std::this_thread::sleep_for(milliseconds(100));
        }
        return true;
    };

    while (!st.stop_requested()) {
        if (!gs_key.empty() && paper_loop_ != nullptr) {
            data::OddsMap merged;
            std::size_t sports_with_odds = 0;
            for (const gs::GoalserveSport sp : kOddsSports) {
                if (st.stop_requested())
                    return;
                const std::string cat(gs::SportOddsCat(sp));
                const std::string slug(gs::SportPregameSlug(sp));
                if (cat.empty() || slug.empty())
                    continue;
                // getodds 路径恒 /getodds/soccer, cat 参数区分运动
                const std::string odds_xml =
                    fetch("https://www.goalserve.com/getfeed/" + gs_key + "/getodds/soccer?cat=" + cat +
                          "_10");
                const std::int64_t ing_ns = duration_cast<nanoseconds>(
                                                system_clock::now().time_since_epoch())
                                                .count();
                auto matches = gs::ParseGetOddsXml(odds_xml, sp, ing_ns);
                if (matches.empty()) {
                    if (!interruptible_sleep(milliseconds(800)))
                        return;
                    continue;
                }
                // inplay-mapping: pregame_match_id ↔ inplay_match_id
                const std::string map_xml =
                    fetch("https://www.goalserve.com/getfeed/" + gs_key + "/" + slug + "/inplay-mapping");
                std::unordered_map<std::string, std::string> pre2inp;
                for (auto& [pre, inp] : gs::ParseInplayMappingXml(map_xml))
                    pre2inp.emplace(std::move(pre), std::move(inp));
                // join: getodds (pregame match_id) → inplay_match_id (系统 join key)
                for (auto& mo : matches) {
                    const auto mit = pre2inp.find(mo.match_id);
                    if (mit == pre2inp.end())
                        continue;  // 无 mapping (非直播/未上架) → 跳过 (fail-soft)
                    merged[mit->second] = std::move(mo);
                }
                ++sports_with_odds;
                if (!interruptible_sleep(milliseconds(800)))  // per-sport 间隔, 压 www 限速
                    return;
            }
            const std::size_t n = merged.size();
            paper_loop_->SetOddsByMatchId(std::move(merged));
            // 常开 (每 90s 一行, 低噪声): bm_slots 可观测性 — join 到几场跨庄家赔率。
            std::fprintf(stderr, "[odds] bm_slots 刷新: %zu 场跨庄家赔率注入 (%zu/6 sport 有 getodds)\n", n,
                         sports_with_odds);
        }
        if (!interruptible_sleep(seconds(300)))  // 赔率变化慢 + getodds 大 → 300s 周期 (省带宽)
            return;
    }
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// DiskPrune — 采集数据磁盘守护 (老板「超过30g后开始删,一次删5G」, 2026-06-01)
//   周期算 ml_capture 目录 *.jsonl 总大小; 超 disk_prune_threshold_gb → 从最大文件头部截 (删最老数据)
//   释放 disk_prune_free_gb。安全: recorder 每 poll 重开文件 (ofstream app), 故 rename 替换不冲突
//   (最多丢一个 poll 周期的写入, 训练数据可容忍)。排除 <100MB 小文件 (settlements 标签等不动)。
// ---------------------------------------------------------------------------
void PaperDaemon::DiskPrune(std::stop_token st) {
    using namespace std::chrono;
    namespace fs = std::filesystem;
    if (cfg_.disk_prune_threshold_gb <= 0) return;
    const fs::path dir = fs::path(cfg_.ml_path).parent_path();  // data/ml_capture
    const std::int64_t threshold = static_cast<std::int64_t>(cfg_.disk_prune_threshold_gb) * (1LL << 30);
    const std::int64_t free_target = static_cast<std::int64_t>(cfg_.disk_prune_free_gb) * (1LL << 30);
    constexpr std::int64_t kMinFileSize = 100LL << 20;  // 只动 >100MB 大文件 (排除标签/小文件)
    // 头部截断: 删 path 头部 ~cut 字节 (对齐行边界), 流式拷尾部 → tmp → 原子 rename。返回实际释放。
    auto truncate_head = [](const fs::path& p, std::int64_t cut) -> std::int64_t {
        std::error_code ec;
        const std::int64_t sz = static_cast<std::int64_t>(fs::file_size(p, ec));
        if (ec || cut <= 0 || cut >= sz) return 0;
        std::ifstream in(p, std::ios::binary);
        if (!in.is_open()) return 0;
        in.seekg(cut);
        std::string discard;
        std::getline(in, discard);  // 跳到下一行边界 (丢被切断的半行)
        const fs::path tmp = p.string() + ".prune.tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) return 0;
            out << in.rdbuf();  // 流式拷保留尾部 (不全载入内存)
        }
        in.close();
        fs::rename(tmp, p, ec);
        if (ec) {
            fs::remove(tmp, ec);
            return 0;
        }
        return cut;
    };
    while (!st.stop_requested()) {
        const auto deadline = steady_clock::now() + seconds(cfg_.disk_prune_interval_sec);
        while (steady_clock::now() < deadline) {
            if (st.stop_requested()) return;
            std::this_thread::sleep_for(seconds(2));
        }
        std::int64_t total = 0;
        std::vector<std::pair<fs::path, std::int64_t>> files;
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            if (ec) break;
            std::error_code fec;
            if (!e.is_regular_file(fec) || e.path().extension() != ".jsonl") continue;
            const std::int64_t sz = static_cast<std::int64_t>(fs::file_size(e.path(), fec));
            if (fec) continue;
            total += sz;
            if (sz >= kMinFileSize) files.emplace_back(e.path(), sz);
        }
        if (total <= threshold) continue;  // 未超阈值
        std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        std::int64_t freed = 0;
        for (const auto& [p, sz] : files) {
            if (freed >= free_target) break;
            const std::int64_t cut = std::min(free_target - freed, sz / 2);  // 单文件最多砍一半
            const std::int64_t got = truncate_head(p, cut);
            if (got > 0) {
                freed += got;
                const std::string fn = p.filename().string();
                std::printf("[disk_prune] 截 %s 头部 %.1fGB\n", fn.c_str(), static_cast<double>(got) / (1LL << 30));
            }
        }
        std::printf("[disk_prune] ml_capture %.1fGB > %dGB → 释放 %.1fGB (留最近数据)\n",
                    static_cast<double>(total) / (1LL << 30), cfg_.disk_prune_threshold_gb,
                    static_cast<double>(freed) / (1LL << 30));
        std::fflush(stdout);
    }
}

}  // namespace stcpp::app
