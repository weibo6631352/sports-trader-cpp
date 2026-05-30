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
#include <thread>
#include <utility>

#include "stcpp/data/inplay_feed_thread.hpp"  // InplayFeedThread / InplayFeedConfig
#include "stcpp/ml/feature_recorder.hpp"      // FeatureRecorder

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

            // P1-1: 填充 MarketInfo catalog (gamma 发现的真实元信息)
            MarketInfo mi;
            mi.found = true;
            mi.condition_id = dm.condition_id;
            mi.market_id = dm.condition_id;  // deprecated alias
            mi.tick_size = 0.01;             // Polymarket 默认 tick
            mi.fee_rate = 0.0;               // outright/futures: 无 maker fee
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
    risk::RiskConfig paper_rm_cfg;               // M1 demo cap (逐字对齐原 main)
    paper_rm_cfg.per_order_cap_usdc = 10;        // 10 pUSD demo cap
    paper_rm_cfg.market_exposure_cap_usdc = 50;  // 50 pUSD
    paper_rm_cfg.per_outcome_cap_usdc = 25;      // 25 pUSD
    paper_rm_cfg.bankroll_usdc = 1000;           // 1K pUSD demo bankroll
    paper_rm_cfg.edge_ci_lower_floor = -1.0;     // M1 放宽 CI 门
    paper_rm_cfg.enable_moneyline = true;
    paper_rm_ = std::make_unique<risk::RiskGateway>(paper_rm_cfg, paper_audit_emitter_);

    // BaselineFairValueModel (小肖 pricing v0.1; 先验 sigmoid)
    pricing::ScorePriorParams fv_params{0.30, 0.50};
    paper_fv_model_ = std::make_unique<pricing::BaselineFairValueModel>(fv_params, 0.20);

    // ---- Step 2c: PaperLoop (构造, 不 Start) ----
    // [R-12] PaperLoop 内部 std::jthread, 不进 WSS event loop.
    // [R-11] paper_position_ledger_ 与 live 物理隔离.
    paper_loop_ = std::make_unique<paper::PaperLoop>(*hub_, *paper_rm_, *paper_position_ledger_, *ledger_hub_,
                                                     *quote_hub_, paper_rm_snap_.get(), *paper_fv_model_,
                                                     token_map_, cfg_.paper_loop);

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

    // ---- Step 4 start: WSS AsyncConnect ----
    if (cfg_.start_live_feeds && live_transport_) {
        const std::string wss_url = "wss://ws-subscriptions-clob.polymarket.com/ws/market";
        std::printf("[paper_daemon] 连接 %s ...\n", wss_url.c_str());
        std::fflush(stdout);
        live_transport_->AsyncConnect(wss_url);
        std::printf("[paper_daemon] WSS io_thread_ 已启动, 等待 book 数据 (通常 1-5s)...\n");
        std::fflush(stdout);
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

}  // namespace stcpp::app
