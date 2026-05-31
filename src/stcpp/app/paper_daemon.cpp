// src/stcpp/app/paper_daemon.cpp — PaperDaemon 实现
//
// Owner: 老雷 (GM) — PaperDaemon 重构 (老郭 §A.1 配套落地)
// last_review: 2026-05-30
//
// 逐字搬迁自 debug_server_main.cpp 的 main() 函数体 (Step 1-6), 行为不变.
// 红线 (R-11 / R-12 / R-20) 见头文件; 关键不变量在下方对应位置加 [R-11] / [R-20] 注.

#include "stcpp/app/paper_daemon.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <unordered_set>
#include <utility>

#include "stcpp/data/commentaries_poller.hpp"   // live_stats CommentariesPoller (commentaries 轮询)
#include "stcpp/data/inplay_feed_thread.hpp"    // InplayFeedThread / InplayFeedConfig
#include "stcpp/data/live_stats_store.hpp"      // live_stats LiveStatsStore
#include "stcpp/data/settlement_poller.hpp"     // M2 SettlementPoller (clob /markets 轮询)
#include "stcpp/data/settlement_store.hpp"      // M2 SettlementStore
#include "stcpp/data/score_snapshot_store.hpp"  // A1b: ScoreSnapshotStore::GetSnapshot
#include "stcpp/ml/feature_recorder.hpp"        // FeatureRecorder

#include "src/stcpp/debug_api/live_book_publisher.hpp"  // LiveBookPublisher
#include "src/stcpp/debug_api/live_wss_transport.hpp"   // LiveWssTransport
#include "src/stcpp/debug_api/server.hpp"               // HttpServer

namespace stcpp::app {

namespace {

// paper RiskGateway 专用 null audit emitter (M1: 不落真 WAL; R-11 隔离).
class NullAuditEmitter final : public risk::AuditEmitter {
public:
    bool emit(risk::AuditRecord const& /*rec*/) noexcept override { return true; }
};

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

        for (const auto& dm : ev.markets) {
            std::printf("[paper_daemon]    market %.28s... | type=%s | gi=%s\n", dm.condition_id.c_str(),
                        dm.sports_market_type.c_str(), dm.group_item_title.c_str());
            token_map_[dm.condition_id] = {dm.token0_id, dm.token1_id};
            ei.condition_ids.push_back(dm.condition_id);

            // A1b: 捕获 EventMatcher 锚定输入 (仅 moneyline 且两队名齐 → 可匹配 Goalserve).
            //   outcome0/1 即两队名; game_start_ts_sec = kickoff; sport 取 event 级.
            if (!dm.outcome0_name.empty() && !dm.outcome1_name.empty()) {
                EventMatchInput mi_in;
                mi_in.team0 = dm.outcome0_name;  // YES (token0)
                mi_in.team1 = dm.outcome1_name;  // NO  (token1)
                mi_in.kickoff_ts_sec = dm.game_start_ts_sec;
                mi_in.sport = ev.sport;
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
        if (discovered.empty()) {
            std::printf("[paper_daemon] /events 无体育 event, 回退 /markets 平铺发现...\n");
            std::fflush(stdout);
            discovered = DiscoverSportsMarketsFlat(cfg_.max_markets_flat);
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
    paper_rm_cfg.enable_moneyline = true;
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
    paper_loop_ = std::make_unique<paper::PaperLoop>(*hub_, *paper_rm_, *paper_position_ledger_, *ledger_hub_,
                                                     *quote_hub_, paper_rm_snap_.get(), *paper_fv_model_,
                                                     token_map_, cfg_.paper_loop);
    // A1b: 注入真实比分源 (Start 前; 之后 loop_thread_ 只读). 映射由刷新线程 SetEventMapping.
    paper_loop_->SetScoreStore(score_store_.get());

    // R-fee-2: 注入 per-market 手续费系数 (condition_id → gamma feeSchedule.rate)。
    //   Start 前一次性注入, 之后 loop_thread_ 只读。官方禁硬编码 (docs.polymarket)。
    {
        std::unordered_map<std::string, double> fee_map;
        fee_map.reserve(market_catalog_.size());
        for (const auto& [cid, mi] : market_catalog_)
            fee_map[cid] = mi.fee_rate;
        paper_loop_->SetFeeByCondition(std::move(fee_map));
    }

    // 统一数据树: 注入 condition → 父级引用 (event_id / neg_risk_market_id), 决策/模型带父级。
    {
        std::unordered_map<std::string, paper::ParentRef> parent_map;
        parent_map.reserve(market_catalog_.size());
        for (const auto& [cid, mi] : market_catalog_)
            parent_map[cid] = paper::ParentRef{mi.event_id, mi.neg_risk_market_id};
        paper_loop_->SetParentRefs(std::move(parent_map));
    }

    // ---- Step 2d: RealStateProvider (读模型) ----
    risk::RiskConfig rsp_risk_cfg;
    real_provider_ = std::make_unique<debug_api::RealStateProvider>(
        *hub_, /*snap=*/paper_rm_snap_.get(), /*score_store=*/score_store_.get(),
        /*token_map=*/token_map_, rsp_risk_cfg, cfg_.exec_mode,
        /*ledger_hub=*/ledger_hub_.get(), /*quote_hub=*/quote_hub_.get());
    real_provider_->set_events(event_infos_);
    real_provider_->set_market_catalog(market_catalog_);

    // LiveMetricsHooks (P1-2/P1-3): start_tp + fill_counter; wss_transport 在 Step 4 填.
    metrics_hooks_.start_tp = std::chrono::steady_clock::now();
    metrics_hooks_.fill_counter = &paper_loop_->stats().fills_completed;
    real_provider_->set_live_metrics_hooks(metrics_hooks_);

    // ---- Step 3: InplayFeedThread (构造, 不 Start; Start() 内拉起) ----
    data::InplayFeedConfig feed_cfg;
    feed_cfg.sports = {
        data::goalserve::GoalserveSport::Soccer,
        data::goalserve::GoalserveSport::Basketball,
        data::goalserve::GoalserveSport::Tennis,
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
            *settlement_store_, std::move(settle_cids), std::move(fetcher), /*poll_interval_ms=*/60'000);
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
            /*poll_interval_ms=*/30'000);
    }

    // ---- Step 4: LiveWssTransport + LiveBookPublisher (构造 + 设回调, 不 AsyncConnect) ----
    if (!all_token_ids_.empty()) {
        live_publisher_ = std::make_unique<debug_api::LiveBookPublisher>(*hub_, all_token_ids_, cfg_.verbose);
        live_transport_ = std::make_unique<debug_api::LiveWssTransport>(cfg_.verbose);

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
        std::printf("[paper_daemon] Goalserve InplayFeedThread 启动 (soccer/basketball/tennis, R-12)\n");
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

    // ---- Step 4b' start: 映射刷新线程 (A1b; EventMatcher 周期匹配 condition↔goalserve) ----
    //   仅当: 起交易 + 有 score_store + 有可匹配 market + 刷新周期>0.
    if (cfg_.enable_paper_trading && cfg_.mapping_refresh_sec > 0 && score_store_ && paper_loop_ &&
        !market_match_inputs_.empty()) {
        mapping_refresh_thread_ = std::jthread([this](std::stop_token st) { RefreshEventMapping(st); });
        std::printf("[paper_daemon] 映射刷新线程启动 (EventMatcher, %ds 周期, %zu 可匹配 market)\n",
                    cfg_.mapping_refresh_sec, market_match_inputs_.size());
        std::fflush(stdout);
    }

    // ---- Step 4c start: FeatureRecorder ----
    if (ml_recorder_) {
        ml_recorder_->Start();
        std::printf("[paper_daemon] ML 训练数据采集启动 (FeatureRecorder -> %s)\n", cfg_.ml_path.c_str());
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
    // 0b. REST 快照打底线程先停 (它 touch live_publisher_/hub_, 必在二者析构前 join; st 令其提前退出).
    if (seed_thread_.joinable()) {
        seed_thread_.request_stop();
        seed_thread_.join();
    }

    // 1. HttpServer 先停 (停止读端, 不再读 real_provider_)
    if (server_) {
        server_->stop();
    }

    // 2. FeatureRecorder (先于 quote_hub_ 析构; Stop 内含 join)
    if (ml_recorder_) {
        ml_recorder_->Stop();
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
    while (!st.stop_requested()) {
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
        for (const auto& [cond_id, in] : market_match_inputs_) {
            const auto r = event_matcher_.Match(in, candidates);
            if (r.matched) {
                (*new_map)[cond_id] =
                    paper::EventMapEntry{r.inplay_match_id, r.yes_is_home, r.team_score, refresh_now_ns};
                ++matched;
            }
        }

        // 3. 推送映射给 PaperLoop (热刷)
        if (paper_loop_) {
            paper_loop_->SetEventMapping(std::shared_ptr<const paper::ConditionEventMap>(std::move(new_map)));
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
                for (const auto& [cid, rec] : *snap) {
                    paper::ResolutionEntry e;
                    e.status = rec.resolution_status();
                    e.winner = rec.settlement_value;  // -1/0/1 直对齐 ResolutionEntry.winner
                    res_map[cid] = e;
                    if (e.status == 2) ++resolved;
                }
                paper_loop_->SetResolutionByCondition(std::move(res_map));
                std::fprintf(stderr, "[paper_daemon] 结算刷新: %zu market (%zu resolved)\n", snap->size(),
                             resolved);
            }
        }
        const auto deadline = steady_clock::now() + seconds(30);
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
            // ② live_stats 快照 → paper_loop join 表
            if (const auto ls_snap = live_stats_store_->GetSnapshot()) {
                paper_loop_->SetLiveStatsByTeams(*ls_snap);
            }
        }
        const auto deadline = steady_clock::now() + seconds(30);
        while (steady_clock::now() < deadline) {
            if (st.stop_requested()) return;
            std::this_thread::sleep_for(milliseconds(100));
        }
    }
}

}  // namespace stcpp::app
