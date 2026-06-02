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
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <unordered_set>
#include <utility>

#include "stcpp/data/commentaries_poller.hpp"   // live_stats CommentariesPoller (commentaries 轮询)
#include "stcpp/data/market_taxonomy.hpp"        // v0.7 类别码映射 (真实 Polymarket 结构 → categorical)
#include "stcpp/data/inplay_feed_thread.hpp"    // InplayFeedThread / InplayFeedConfig
#include "stcpp/data/live_stats_store.hpp"      // live_stats LiveStatsStore
#include "stcpp/data/settlement_poller.hpp"     // M2 SettlementPoller (clob /markets 轮询)
#include "stcpp/data/settlement_recorder.hpp"   // Phase 2 缺口E 结算落盘 (label y)
#include "stcpp/data/score_frame_recorder.hpp"  // 回测 P0 比分帧落盘 (红线#3 闭合数据前提)
#include "stcpp/data/settlement_store.hpp"      // M2 SettlementStore
#include "stcpp/data/score_snapshot_store.hpp"  // A1b: ScoreSnapshotStore::GetSnapshot
#include "stcpp/ml/fair_value_model.hpp"        // 步④ make_onnx_fair_value_model / StubFairValueModel
#include "stcpp/ml/feature_recorder.hpp"          // FeatureRecorder
#include "stcpp/ml/feature_vector_recorder.hpp"   // Phase 2 项6 完整向量 recorder (含 hub)
#include "stcpp/ml/label_pipeline.hpp"            // 自动训练 join: LoadLabelStoreFromJsonl + JoinFile
#include "stcpp/ml/model_feature_spec.hpp"      // kMlFeatureCount (ML 模型维度契约)

#include "src/stcpp/polymarket/clob_wss/live_book_publisher.hpp"  // LiveBookPublisher
#include "src/stcpp/polymarket/clob_wss/live_wss_transport.hpp"   // LiveWssTransport
#include "src/stcpp/debug_api/server.hpp"               // HttpServer

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

    for (const auto& ev : discovered) {
        std::printf("[paper_daemon]  event: %.40s | slug=%.30s | sport=%s\n", ev.title.c_str(),
                    ev.slug.c_str(), ev.sport.c_str());

        EventInfo ei;
        ei.event_id = ev.event_id;
        ei.slug = ev.slug;
        ei.title = ev.title;
        ei.sport = ev.sport;
        ei.neg_risk_market_id = ev.neg_risk_market_id;
        ei.live = ev.live;  // gamma live=true 透传 → 前端默认只显示正在比赛

        for (const auto& dm : ev.markets) {
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
            if (team0.empty() || team1.empty() || IsYesNoOutcome(team0) || IsYesNoOutcome(team1)) {
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
            if (!team0.empty() && !team1.empty() && !IsYesNoOutcome(team0) && !IsYesNoOutcome(team1)) {
                EventMatchInput mi_in;
                mi_in.team0 = team0;
                mi_in.team1 = team1;
                mi_in.kickoff_ts_sec = dm.game_start_ts_sec;
                mi_in.sport = ev.sport;
                // 3-way 平局盘 (gi="Draw (...)") → 下游 sharp fair 取 draw 概率 (盈利修复)。
                mi_in.is_draw = dm.group_item_title.rfind("Draw", 0) == 0;
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

    // Collect all token_ids for WSS subscription
    all_token_ids_.reserve(token_map_.size() * 2);
    for (const auto& [cond_id, tok_pair] : token_map_) {
        all_token_ids_.push_back(tok_pair.first);
        all_token_ids_.push_back(tok_pair.second);
    }
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
bool PaperDaemon::RediscoverOnce() {
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
    if (!changed) {
        return false;  // 市场集未变, 不动
    }
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
    if (live_transport_ && !all_token_ids_.empty()) {
        std::string sub = R"({"type":"Market","assets_ids":[)";
        bool first = true;
        for (const auto& tid : all_token_ids_) {
            if (!first) sub.push_back(',');
            sub.push_back('"');
            sub.append(tid);
            sub.push_back('"');
            first = false;
        }
        sub.append("]}");
        live_transport_->AsyncSendText(sub);  // CLOB 接受追加订阅; hub 无 allowlist, 新 token 帧自动流入
    }
    std::fprintf(stderr, "[paper_daemon] 周期重发现: 市场集变化 → %zu market 重订 (WSS 全量重订)\n",
                 token_map_.size());
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
    if (!live_publisher_ || all_token_ids_.empty())
        return;
    const std::int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();
    constexpr std::size_t kChunk = 50;  // POST /books 分批 (避免单请求过大)
    std::size_t chunks_ok = 0;
    for (std::size_t i = 0; i < all_token_ids_.size(); i += kChunk) {
        if (st.stop_requested())  // 关停时提前退出 (不卡 shutdown)
            return;
        const std::size_t end = (i + kChunk < all_token_ids_.size()) ? i + kChunk : all_token_ids_.size();
        std::string body = "[";
        for (std::size_t j = i; j < end; ++j) {
            if (j > i)
                body += ',';
            body += "{\"token_id\":\"";
            body += all_token_ids_[j];  // uint256 十进制, 无 shell 特殊字符
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
                all_token_ids_.size(), chunks_ok,
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
    while (!st.stop_requested()) {
        if (live_transport_->IsConnected()) {
            backoff_sec = 1;
            if (steady_clock::now() - last_ping >= seconds(10)) {
                live_transport_->AsyncSendText("PING");  // 心跳: 防 idle 超时被踢 (真因)
                last_ping = steady_clock::now();
            }
            sleep_steps(10);  // 1s 检查间隔
        } else {
            std::fprintf(stderr, "[paper_daemon] WSS 断开, %ds 后重连 (idle/网络)...\n", backoff_sec);
            std::fflush(stderr);
            sleep_steps(backoff_sec * 10);
            if (st.stop_requested())
                break;
            live_transport_->AsyncConnect(url);  // OnConnected 回调用 all_token_ids_ 重发 subscribe
            // 重连后台重 seed (books 重新打底; 复用初次 seed 逻辑, 不阻塞看门狗)
            seed_thread_ = std::jthread([this](std::stop_token s) { SeedInitialBooksFromRest(s); });
            last_ping = steady_clock::now();
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
    hub_ = std::make_unique<polymarket::clob_wss::OrderBookSnapshotHub>();
    score_store_ = std::make_unique<data::ScoreSnapshotStore>();
    ledger_hub_ = std::make_unique<risk::LedgerSnapshotHub>();
    quote_hub_ = std::make_unique<sizing::QuoteSnapshotHub>();

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
    // c3 (P0-2 根治): RM caps 与 sizing 同源 = cfg_.paper_loop (whole pUSD), 同用 from_pusd 转 micro。
    //   RM 直接 micro 比 size_pUSD_micro; sizing 侧 .to_pusd() 回 whole 比 notional。同源同值。
    paper_rm_cfg.per_order_cap_usdc = domain::MicroPUSD::from_pusd(cfg_.paper_loop.per_order_cap_usdc);
    paper_rm_cfg.market_exposure_cap_usdc =
        domain::MicroPUSD::from_pusd(cfg_.paper_loop.market_exposure_cap_usdc);
    paper_rm_cfg.per_outcome_cap_usdc = domain::MicroPUSD::from_pusd(cfg_.paper_loop.per_outcome_cap_usdc);
    paper_rm_cfg.bankroll_usdc =
        domain::MicroPUSD::from_pusd(cfg_.paper_loop.bankroll_usdc);  // c2b: 与 cap 对称
    paper_rm_cfg.edge_ci_lower_floor = -1.0;                          // M1 放宽 CI 门
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
    // ML 驱动决策 (老板放开 paper ML-R2): 有真 ONNX 模型时 ML 全驱动决策 fair (weight=1.0)。
    //   stub 永不驱动 (blend 内 kind==Onnx 门); 无 onnx_model_path → stub → 纯 baseline, 安全。
    cfg_.paper_loop.ml_fair_blend_weight = 1.0;
    paper_loop_ = std::make_unique<paper::PaperLoop>(*hub_, *paper_rm_, *paper_position_ledger_, *ledger_hub_,
                                                     *quote_hub_, paper_rm_snap_.get(), *paper_fv_model_,
                                                     token_map_, cfg_.paper_loop);
    // A1b: 注入真实比分源 (Start 前; 之后 loop_thread_ 只读). 映射由刷新线程 SetEventMapping.
    paper_loop_->SetScoreStore(score_store_.get());

    // 步④: ML 推理模型装配 + 注入 (advisory, ML-R1/R2 — 旁路, 不进决策)。
    //   优先 ONNX (make_onnx_fair_value_model; W11+ 接 ONNXRuntime, 当前返 nullptr) →
    //   回落 StubFairValueModel(kMlFeatureCount=24) 管道占位, 跑通 inplay 赔率+live_stats →
    //   FeatureVector → predict → QuoteFeatures.ml_advisory_p_yes 全路径。训出真 ONNX 后,
    //   仅换工厂返回值, paper_loop 推理路径零改码。白名单决定特征是真值还是 NaN。
    {
        ml::OnnxModelConfig onnx_cfg;
        onnx_cfg.onnx_path = cfg_.onnx_model_path;  // 配置路径: 放训好的 .onnx 即激活 (零改码)
        onnx_cfg.expected_feature_count = ml::kMlFeatureCount;
        onnx_cfg.output_outcome_count = 2;  // Moneyline YES/NO
        onnx_cfg.model_id = "paper-onnx-fair";
        fair_value_model_ = ml::make_onnx_fair_value_model(onnx_cfg);  // 空路径/无文件 → nullptr → stub
        if (!fair_value_model_) {
            fair_value_model_ =
                std::make_shared<ml::StubFairValueModel>(ml::kMlFeatureCount, /*outcome_count=*/2);
        }
        // 热加载注入 (老板「模型可重新加载」): 经 HotSwapHolder Store shared 引用; watcher 线程后续原子换。
        paper_loop_->SetMlModelShared(fair_value_model_);
        std::printf("[paper_daemon] 步④ ML 推理模型注入: kind=%s id=%.*s feat=%zu (advisory ML-R2)\n",
                    std::string(ml::to_string(fair_value_model_->kind())).c_str(),
                    static_cast<int>(fair_value_model_->model_id().size()),
                    fair_value_model_->model_id().data(), fair_value_model_->expected_feature_count());
        std::fflush(stdout);
    }

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
    real_provider_->set_live_metrics_hooks(metrics_hooks_);

    // ---- Step 3: InplayFeedThread (构造, 不 Start; Start() 内拉起) ----
    data::InplayFeedConfig feed_cfg;
    feed_cfg.sports = {
        data::goalserve::GoalserveSport::Soccer,
        data::goalserve::GoalserveSport::Basketball,
        data::goalserve::GoalserveSport::Tennis,
        data::goalserve::GoalserveSport::Esports,  // 2026-06-01: PM dota2/lol/CS 盘对接 (inplay-esports.gz)
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
            std::printf("[paper_daemon] WSS CONNECTED, 订阅 %zu tokens...\n", all_token_ids_.size());
            std::fflush(stdout);
            // CLOB market channel subscribe: {"type":"Market","assets_ids":[...]}
            std::string sub = R"({"type":"Market","assets_ids":[)";
            bool first = true;
            for (const auto& tid : all_token_ids_) {
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

    // ---- Step 4c: FeatureRecorder (构造, 不 Start) ----
    if (cfg_.record_ml) {
        ml::FeatureRecorder::Config rec_cfg;
        rec_cfg.output_path = cfg_.ml_path;
        rec_cfg.poll_interval_sec = 5;
        std::vector<std::string> ml_cond_ids;
        ml_cond_ids.reserve(token_map_.size());
        for (const auto& [cond_id, _tok] : token_map_) {
            ml_cond_ids.push_back(cond_id);
        }
        ml_recorder_ = std::make_unique<ml::FeatureRecorder>(*quote_hub_, std::move(ml_cond_ids), rec_cfg);

        // Phase 2 项6: 完整 75 列向量 hub + recorder (训练 X 含 0-17 原始列, FeatureRecorder 落不到的)。
        //   paper_loop loop_thread_ Publish → 独立 recorder 线程落盘 (IO 离决策线程)。
        fv_hub_ = std::make_unique<ml::FeatureVectorHub>();
        paper_loop_->SetFeatureVectorHub(fv_hub_.get());
        // 可观测: debug_api /api/v1/features/health 从同一 fv_hub 聚合特征健康 (只读)。
        if (real_provider_) real_provider_->set_feature_vector_hub(fv_hub_.get());
        ml::FeatureVectorRecorder::Config fv_cfg;
        fv_cfg.output_path = cfg_.ml_path + ".fv.jsonl";  // 与 quotes.jsonl 并列
        fv_cfg.poll_interval_sec = 5;
        fv_recorder_ = std::make_unique<ml::FeatureVectorRecorder>(*fv_hub_, fv_cfg);

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
        std::printf("[paper_daemon] Goalserve InplayFeedThread 启动 (soccer/basketball/tennis/esports, R-12)\n");
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

    // ---- live_stats start: CommentariesPoller + 刷新线程 (喂 5 个 g_*_diff 特征) ----
    if (cfg_.start_live_feeds && commentaries_poller_) {
        commentaries_poller_->Start();
        if (cfg_.enable_paper_trading && paper_loop_) {
            live_stats_refresh_thread_ = std::jthread([this](std::stop_token st) { RefreshLiveStats(st); });
        }
        std::printf("[paper_daemon] live_stats CommentariesPoller + 刷新线程启动 (commentaries 30s 轮询)\n");
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
        seed_thread_ = std::jthread([this](std::stop_token st) { SeedInitialBooksFromRest(st); });
        // WSS 看门狗 (2026-06-02 会议): 心跳保活 (治 idle 超时真因) + 断线重连 (治不重连症状)。
        wss_watchdog_thread_ =
            std::jthread([this, wss_url](std::stop_token st) { WssWatchdogLoop(st, wss_url); });
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

    // ---- Step 4b'' start: 模型热重载 watcher (老板「边训边跑边更新模型可重新加载」, 2026-06-01) ----
    //   onnx_model_path 非空 + interval>0 + 起交易 → 启线程周期 stat mtime, 训练旁路产新 .onnx 自动换上。
    if (cfg_.enable_paper_trading && paper_loop_ && !cfg_.onnx_model_path.empty() &&
        cfg_.model_reload_interval_sec > 0) {
        model_reload_thread_ = std::jthread([this](std::stop_token st) { RefreshModel(st); });
        std::printf("[paper_daemon] 模型热重载 watcher 启动 (监测 %s, 周期 %ds)\n",
                    cfg_.onnx_model_path.c_str(), cfg_.model_reload_interval_sec);
        std::fflush(stdout);
    }

    // ---- Step 4b''' start: 进程内自动训练编排 (老板「A. 周期重训 + 热加载」, 2026-06-01) ----
    //   起交易 + onnx_model_path 非空 (产模型目标) + interval>0 才启 (默认关; 需 ops 配 venv python)。
    if (cfg_.enable_paper_trading && paper_loop_ && !cfg_.onnx_model_path.empty() &&
        cfg_.auto_train_interval_sec > 0) {
        auto_train_thread_ = std::jthread([this](std::stop_token st) { AutoTrain(st); });
        std::printf("[paper_daemon] 自动训练编排线程启动 (周期 %ds, python=%s, 脚本=%s, 最小样本 %zu)\n",
                    cfg_.auto_train_interval_sec, cfg_.train_python_bin.c_str(),
                    cfg_.train_script_path.c_str(), cfg_.min_train_samples);
        std::fflush(stdout);
    }

    // ---- 采集数据磁盘守护 (老板「超过30g后开始删,一次删5G」) — 默认开 (仅超阈值才动) ----
    if (cfg_.disk_prune_threshold_gb > 0) {
        disk_prune_thread_ = std::jthread([this](std::stop_token st) { DiskPrune(st); });
        std::printf("[paper_daemon] 磁盘守护线程启动 (>%dGB 删 %dGB, 每 %ds 检查)\n",
                    cfg_.disk_prune_threshold_gb, cfg_.disk_prune_free_gb, cfg_.disk_prune_interval_sec);
        std::fflush(stdout);
    }

    // ---- Step 4c start: FeatureRecorder + 完整向量 recorder (项6) ----
    if (ml_recorder_) {
        ml_recorder_->Start();
        std::printf("[paper_daemon] ML 训练数据采集启动 (FeatureRecorder -> %s)\n", cfg_.ml_path.c_str());
    }
    if (fv_recorder_) {
        fv_recorder_->Start();
        std::printf("[paper_daemon] 完整 75 列向量采集启动 (FeatureVectorRecorder -> %s.fv.jsonl)\n",
                    cfg_.ml_path.c_str());
    }
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
    // 0b. REST 快照打底线程先停 (它 touch live_publisher_/hub_, 必在二者析构前 join; st 令其提前退出).
    if (seed_thread_.joinable()) {
        seed_thread_.request_stop();
        seed_thread_.join();
    }

    // 1. HttpServer 先停 (停止读端, 不再读 real_provider_)
    if (server_) {
        server_->stop();
    }

    // 2. FeatureRecorder + 完整向量 recorder (项6) (先于 hub/paper_loop 析构; Stop 内含 join)
    if (ml_recorder_) {
        ml_recorder_->Stop();
    }
    if (fv_recorder_) {
        fv_recorder_->Stop();  // 停读 fv_hub_ (paper_loop 随后 Stop 停写; 二者先于 fv_hub_ 析构)
    }
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
    while (!st.stop_requested()) {
        // 0. R-6 周期重发现 (间隔到 → 全量重建 catalog + WSS 重订; 在 match 之前, match_inputs 已是新版)。
        if (cfg_.rediscover_interval_sec > 0 &&
            steady_clock::now() - last_rediscover >= seconds(cfg_.rediscover_interval_sec)) {
            RediscoverOnce();
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
        // [DIAG] 候选 Goalserve event 数 + 前若干队名 (定位 0 匹配根因: 空候选 / 名不符 / kickoff).
        static bool diag_mapping_dumped = false;
        if (!diag_mapping_dumped) {
            std::fprintf(stderr, "[map-diag] Goalserve 候选 EventScore 数=%zu\n", candidates.size());
            std::size_t shown = 0;
            for (const auto& c : candidates) {
                std::fprintf(stderr, "[map-diag]   cand: home='%s' away='%s' status='%s' id=%s\n",
                             c.home.c_str(), c.away.c_str(), c.status.c_str(), c.event_id.c_str());
                if (++shown >= 14) break;
            }
            diag_mapping_dumped = true;
        }
        debug_api::MappingStatusReport map_report;  // 可观测: 本轮映射快照
        map_report.total_markets = static_cast<int>(market_match_inputs_.size());
        for (const auto& [cond_id, in] : market_match_inputs_) {
            const auto r = event_matcher_.Match(in, candidates);
            if (r.matched) {
                paper::EventMapEntry entry;
                entry.inplay_match_id = r.inplay_match_id;
                entry.yes_is_home = r.yes_is_home;
                entry.is_draw = in.is_draw;  // 3-way 平局盘标志透传 → 下游 sharp fair 选 draw
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

        // 3. 推送映射给 PaperLoop (热刷)
        if (paper_loop_) {
            paper_loop_->SetEventMapping(std::shared_ptr<const paper::ConditionEventMap>(std::move(new_map)));
        }

        // 3b. 可观测: 构建映射状态报告 (matched 行 + Goalserve live 候选) → push debug_api。
        map_report.matched = static_cast<int>(matched);
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
        std::fprintf(stderr, "[paper_daemon] 映射刷新: %zu/%zu market 匹配到 Goalserve event\n", matched,
                     market_match_inputs_.size());

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
    while (!st.stop_requested()) {
        if (live_stats_store_ && commentaries_poller_ && paper_loop_ && score_store_) {
            // ① 活跃 league_id (去重) → poller
            if (const auto score_snap = score_store_->GetSnapshot()) {
                std::vector<std::string> leagues;
                std::unordered_set<std::string> seen;
                for (const auto& [mid, es] : *score_snap) {
                    if (!es.league_id.empty() && seen.insert(es.league_id).second) {
                        leagues.push_back(es.league_id);
                    }
                }
                commentaries_poller_->SetLeagues(std::move(leagues));
            }
            // ② live_stats 快照 → paper_loop join 表 (盖新鲜度 as_of: 老板「每个源标时间」→ g_live_stats_age_sec)
            if (const auto ls_snap = live_stats_store_->GetSnapshot()) {
                data::livescore::LiveStatsMap stamped = *ls_snap;  // 拷贝再盖 ts
                const std::int64_t ls_as_of_ns =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
                for (auto& [_k, v] : stamped) v.as_of_ts_ns = ls_as_of_ns;
                paper_loop_->SetLiveStatsByTeams(std::move(stamped));
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
// RefreshModel — 模型热重载 watcher (老板「边训边跑边更新模型可重新加载」, 2026-06-01)
//   周期 stat onnx_model_path mtime; 变了 → make_onnx 加载新模型 → 校验 (ready+维度+真 ONNX) →
//   paper_loop_->SetMlModelShared 原子换上 (推理线程 Load 拿存活引用, 不停盘)。失败 → 保留旧模型 (fail-safe)。
//   注: 加载在本线程 (非 loop_thread_), 不阻塞决策。onnxruntime 未装时 make_onnx 返 nullptr → 校验不过 →
//   保留旧 (stub), 机制就位待 runtime 装好 + 真 .onnx 产出即自动生效。
// ---------------------------------------------------------------------------
void PaperDaemon::RefreshModel(std::stop_token st) {
    using namespace std::chrono;
    namespace fs = std::filesystem;
    const std::string path = cfg_.onnx_model_path;
    auto file_mtime = [](const std::string& p) -> std::int64_t {
        std::error_code ec;
        const auto t = fs::last_write_time(p, ec);
        return ec ? 0 : t.time_since_epoch().count();
    };
    std::int64_t last_mtime = file_mtime(path);  // Build 已加载一次; 仅文件变化后才重载
    while (!st.stop_requested()) {
        const auto deadline = steady_clock::now() + seconds(cfg_.model_reload_interval_sec);
        while (steady_clock::now() < deadline) {
            if (st.stop_requested()) return;
            std::this_thread::sleep_for(milliseconds(200));
        }
        const std::int64_t m = file_mtime(path);
        if (m == 0 || m == last_mtime) continue;  // 无文件 / 未变
        ml::OnnxModelConfig onnx_cfg;
        onnx_cfg.onnx_path = path;
        onnx_cfg.expected_feature_count = ml::kMlFeatureCount;
        onnx_cfg.output_outcome_count = 2;
        onnx_cfg.model_id = "paper-onnx-fair";
        std::shared_ptr<ml::FairValueModel> fresh = ml::make_onnx_fair_value_model(onnx_cfg);
        if (!fresh || !fresh->ready() || fresh->expected_feature_count() != ml::kMlFeatureCount ||
            fresh->kind() != ml::ModelKind::Onnx) {
            std::fprintf(stderr, "[paper_daemon] ⚠ 模型热重载校验失败 (加载/ready/维度/kind), 保留旧模型: %s\n",
                         path.c_str());
            last_mtime = m;  // 不反复重试同一坏文件
            continue;
        }
        fair_value_model_ = fresh;             // daemon 持引用 (旧模型最后引用释放回收)
        paper_loop_->SetMlModelShared(fresh);  // 原子换上, 不停盘
        last_mtime = m;
        std::printf("[paper_daemon] ✓ 模型热重载: %s (feat=%zu) → 原子换上不停盘\n", path.c_str(),
                    fresh->expected_feature_count());
        std::fflush(stdout);
    }
}

// ---------------------------------------------------------------------------
// AutoTrain — 进程内自动训练编排 (老板「A. 周期重训 + 热加载」, 2026-06-01)
//   周期循环 (auto_train_interval_sec):
//     ① 进程内 C++ join: feature_vectors.jsonl(X) × settlements.jsonl(y) → training.jsonl
//        (直接调 label_pipeline, 不经外部 CLI; 只收已结算监督集)。
//     ② 标注样本 ≥ min_train_samples → spawn Python 训练子进程 (train_fair_value.py → candidate.onnx)。
//        §12.4 红线: 训练栈 LightGBM 只能 Python 离线, daemon 仅编排 + spawn (跑完即弃), 不进 C++ 进程。
//     ③ 训练成功 → 原子 mv candidate → onnx_model_path → RefreshModel watcher 接力热加载换上 (不停盘)。
//   失败任一步 → 跳过本轮, 保留旧模型 (fail-safe)。冷启动样本不足 → 等积累 (体育结算稀疏, 按天/周)。
// ---------------------------------------------------------------------------
void PaperDaemon::AutoTrain(std::stop_token st) {
    using namespace std::chrono;
    namespace fs = std::filesystem;
    const std::string fv = cfg_.ml_path + ".fv.jsonl";            // FeatureVectorRecorder 落 (X)
    const std::string settle = cfg_.ml_path + ".settlements.jsonl";  // SettlementRecorder 落 (y)
    const std::string training = cfg_.ml_path + ".training.jsonl";   // join 产出 (X+label)
    const std::string candidate = cfg_.onnx_model_path + ".candidate";
    while (!st.stop_requested()) {
        const auto deadline = steady_clock::now() + seconds(cfg_.auto_train_interval_sec);
        while (steady_clock::now() < deadline) {
            if (st.stop_requested()) return;
            std::this_thread::sleep_for(seconds(1));
        }
        // ① 进程内 join (C++; label_pipeline 纯函数核已单测)。滑动窗口: 只 join 最近 train_window_days 天
        //   (老板「就 5 天」): join 量/训练时间有界 + 模型不被陈旧数据拖累。窗口下界用 now (训练窗口边界,
        //   非数据源 ts, 不违 R-20)。
        std::int64_t min_as_of_ns = 0;
        if (cfg_.train_window_days > 0) {
            const std::int64_t now_ns = duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
            min_as_of_ns = now_ns - static_cast<std::int64_t>(cfg_.train_window_days) * 86400LL * 1'000'000'000LL;
        }
        const auto store = ml::LoadLabelStoreFromJsonl(settle);
        const auto js = ml::JoinFile(fv, store, training, /*drop_unlabeled=*/true, min_as_of_ns);
        std::printf("[auto_train] join(窗口%d天): 标注 %zu / 读 %zu / 窗口外 %zu (已结算 condition %zu)\n",
                    cfg_.train_window_days, js.labeled, js.total, js.out_of_window, store.size());
        std::fflush(stdout);
        if (js.labeled < cfg_.min_train_samples) {
            std::printf("[auto_train] 标注 %zu < 阈值 %zu → 跳过 (样本不足, 等积累)\n", js.labeled,
                        cfg_.min_train_samples);
            std::fflush(stdout);
            continue;
        }
        // ② spawn Python 训练子进程 (§12.4: 训练只能 Python 离线; 跑完即弃, 不进 C++ 进程)
        const std::string cmd = cfg_.train_python_bin + " " + cfg_.train_script_path + " --features " +
                                training + " --out " + candidate + " > /tmp/auto_train.log 2>&1";
        std::printf("[auto_train] spawn 训练: %s\n", cmd.c_str());
        std::fflush(stdout);
        const int rc = std::system(cmd.c_str());  // NOLINT: ops 编排 spawn (路径内部 config, 非用户输入)
        if (rc != 0) {
            std::fprintf(stderr, "[auto_train] ⚠ 训练子进程 rc=%d → 不换模型 (见 /tmp/auto_train.log)\n", rc);
            continue;
        }
        // ③ 原子换 candidate → onnx_model_path → RefreshModel watcher 接力热加载
        std::error_code ec;
        fs::rename(candidate, cfg_.onnx_model_path, ec);
        if (ec) {
            std::fprintf(stderr, "[auto_train] ⚠ 原子 mv 失败 (%s) → 不换\n", ec.message().c_str());
            continue;
        }
        std::printf("[auto_train] ✓ 新模型就位 → watcher 将热加载: %s\n", cfg_.onnx_model_path.c_str());
        std::fflush(stdout);
    }
}

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
