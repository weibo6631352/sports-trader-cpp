// include/stcpp/app/paper_daemon.hpp — PaperDaemon: paper 常驻进程编排层
//
// Owner: 老雷 (GM) — PaperDaemon 重构 (老郭 deadcode-review §A.1 配套落地)
// last_review: 2026-05-30
//
// 归属: app 编排层 (stcpp_paper_app 库). 老周架构裁定 B1:
//   PaperDaemon 不进 stcpp_debug_api 契约库 (该库现仅 link Threads). 装进库会把
//   11 个重依赖 (orderbook_hub/risk/paper_loop/pricing/signer/execution/inplay/OpenSSL)
//   灌进契约库, 坐实老郭 §A.2 "debug_api 什么都连" 的膨胀. 故新建 thin app 层,
//   依赖方向 app → debug_api 单向; debug_api 库 source 绝不含 paper_daemon.cpp.
//
// 职责: 把 paper daemon 的 8+ 组件装配逻辑从 debug_server_main.cpp 的 main() 函数体
//   抽出, 用 Build()/Start()/WaitForStop()/Shutdown() 表达 11 步启动序与反序关停.
//   两个 binary 共用同一份 Build() 写栈:
//     - stcpp_paper_server  → RunMode::PaperDaemon (带 HTTP 观测端)
//     - stcpp_paper_runtime → RunMode::Headless    (无 HTTP, systemd 常驻)
//
// 设计 (老周三段式裁定 + 老韩 R-11 硬 gate):
//   Build()       — 发现 + 装配全部组件, 不起任何线程 (异常安全: 半装配可析构).
//   Start()       — 按 11 步序起线程 (inplay → WSS → paper_loop → ml → http).
//   WaitForStop() — 阻塞至 RequestStop().
//   Shutdown()    — 反序优雅停 (幂等): server → ml → paper_loop(join) → detach
//                   → inplay → wss. ~PaperDaemon() 兜底再调 (双保险).
//
// 红线守法 (逐字保持自 debug_server_main.cpp, 不改 PaperLoop 本身):
//   R-11: paper 不污染真账本 —— paper_position_ledger_ 永远是私有
//         make_unique<PositionLedger>() (与 live 物理隔离); NullAuditEmitter 不落真 WAL.
//         **detach_rm_debug_snapshot() 是进程级全局单例 hook (老韩 INV-1 最高危):**
//         Shutdown 显式 detach + 析构兜底 detach (幂等); 成员声明序保证
//         paper_rm_snap_ 在 paper_loop_ 之前声明 (→ paper_loop_ 逆序先析构), 杜绝
//         全局 hook 指向已析构 snap 的 UAF.
//   R-12: 所有后台线程独立 (PaperLoop jthread / InplayFeed / WSS io_thread / HTTP),
//         绝不进彼此 event loop. hub Read/Publish 原子无锁.
//   R-20: 4 时间戳全链路透传 (data_source_ts 来自 hub 快照, 禁本地 now() 替代上游).
//
// ToS: 仅 paper 虚拟成交 (VirtualFill), 不向 Polymarket CLOB 下单; 发现走只读 gamma REST.

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "stcpp/polymarket/live/live_order_submitter.hpp"
#include "stcpp/polymarket/live_executor.hpp"
#include "stcpp/execution/live_executor_adapter.hpp"
#include "stcpp/polymarket/live_order_gate.hpp"
#include "stcpp/app/event_matcher.hpp"     // EventMatcher / EventMatchInput (A1 映射桥)
#include "stcpp/app/market_discovery.hpp"  // DiscoveredEvent
#include "stcpp/paper/paper_loop.hpp"      // PaperLoop / PaperLoopConfig + paper 栈全套类型

#include "src/stcpp/debug_api/real_state_provider.hpp"  // RealStateProvider / MarketTokenMap / LiveMetricsHooks / ExecMode / EventInfo

// 重型 / 三方依赖 (HttpServer→httplib, LiveWss→OpenSSL) forward-declare, 仅 .cpp 实体化,
// 避免把 httplib/openssl 头泄进 app 层公共头.
namespace stcpp::debug_api {
class HttpServer;
}  // namespace stcpp::debug_api
namespace stcpp::polymarket::clob_wss {
class LiveWssTransport;   // R-3 评审 D-1: CLOB WSS 接入归 L1 数据层 (从 debug_api 迁出)
class LiveBookPublisher;  // 同上
}  // namespace stcpp::polymarket::clob_wss
namespace stcpp::data {
class InplayFeedThread;
class SettlementStore;     // M2 收盘/结算 store (forward; .cpp 实体化)
class SettlementPoller;    // M2 结算轮询线程 (forward)
class SettlementRecorder;  // Phase 2 缺口E 结算落盘 (forward; label y 来源)
class ScoreFrameRecorder;  // 回测 P0 比分帧落盘 (forward; 红线#3 闭合数据前提)
namespace livescore {
class LiveStatsStore;     // live_stats 快照 store (forward; .cpp 实体化)
class CommentariesPoller;  // commentaries 轮询线程 (forward)
}  // namespace livescore
}  // namespace stcpp::data
// (ml:: forward decls 已砍 2026-06-05: FeatureRecorder/FairValueModel/FeatureVectorHub/FeatureVectorRecorder)

namespace stcpp::app {

// ---------------------------------------------------------------------------
// RunMode — paper daemon 进程角色 (老郭 §A.1.2: 钉死合法集, 不用裸 bool 组合)
//
// 老周裁定: 只留两档真实角色 (各有 binary/systemd unit 需要). "只观测不交易" 用
//   PaperDaemonConfig.enable_paper_trading flag 表达, 不上升为枚举档 (避免过度设计).
//
// 注意 (R-7 正交): RunMode 是运行期进程角色 (http vs headless), 与 build-time
//   STCPP_EXEC_MODE (paper/live/backtest, R-7/R-11 真相源) 正交. RunMode 不切交易模式.
// ---------------------------------------------------------------------------
enum class RunMode : std::uint8_t {
    PaperDaemon = 0,  // 带 HTTP 观测端 (= 现 stcpp_paper_server)
    Headless = 1,     // 无 HTTP, systemd 常驻 (= stcpp_paper_runtime)
};

[[nodiscard]] constexpr const char* ToString(RunMode m) noexcept {
    switch (m) {
        case RunMode::PaperDaemon:
            return "paper-daemon";
        case RunMode::Headless:
            return "headless";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// PaperDaemonConfig — 运行参数
// ---------------------------------------------------------------------------
struct PaperDaemonConfig {
    RunMode mode{RunMode::PaperDaemon};

    // build-time 执行模式 (R-7/R-11 真相源; main 持 STCPP_EXEC_MODE 宏注入, app 库不依赖宏)
    debug_api::ExecMode exec_mode{debug_api::ExecMode::Paper};

    // HTTP 观测端 (仅 RunMode::PaperDaemon 生效)
    std::uint16_t port{8080};
    std::string host{"127.0.0.1"};

    bool verbose{false};  // WSS/parser 调试日志

    // 结算/比分落盘 (回测等价数据; record_ml 名沿用)。大模型训练采集字段已砍 2026-06-05
    //   「砍掉大模型训练功能」: onnx_model_path / model_reload_interval_sec / auto_train_interval_sec /
    //   train_python_bin / train_script_path / min_train_samples / train_window_days。
    bool record_ml{true};

    // [2026-06-01 老板「超过30g后开始删,一次删5G」] 采集数据磁盘守护: ml_capture 目录总大小超阈值 →
    //   从最大采集文件头部截掉 (删最老数据) 释放一批。默认开 (安全护栏, 仅超阈值才动)。排除小文件 (标签)。
    std::int32_t disk_prune_threshold_gb{30};  // 总大小超此 GB → 触发删 (0=关)
    std::int32_t disk_prune_free_gb{5};        // 一次释放 GB
    std::int32_t disk_prune_interval_sec{600}; // 检查周期 (10 分钟)
    std::string ml_path{"data/ml_capture/quotes.jsonl"};  // settlement/score 落盘路径基 (record_ml; "ml" 名沿用)
    // (ml_poll_sec [FeatureRecorder 采集间隔] 已砍 2026-06-05: FeatureRecorder 删, settlement/score recorder 自有周期)

    // 仅观测不交易 (老周: 替代 ObserverOnly 枚举档). true=起 PaperLoop (默认).
    bool enable_paper_trading{true};

    // A2 (老韩 D3/D4 签字放行): 解封 paper 成交. true → advisory_markets_no_intent=false
    //   (has_real_fair=true 时产生 paper intent → RM → VirtualFill → 写私有 paper ledger).
    //   false → 仅观测 (advisory gate 拦 intent, 零成交). qf.advisory 恒 true 不受影响 (ML-R2).
    //   收口在此 (老韩红线1): PaperLoopConfig 默认仍 true (backward compat), 仅 daemon 显式翻.
    //   **默认 false (P0-3 安全默认, 老郭审查): 生产 daemon 默认仅观测, 显式 --enable-fills 才开火.
    //   价值观 #1/#2 (实盘优先 + 纪律>收益): 解封成交必须运维显式开, 非编译期默认.**
    bool enable_paper_fills{false};

    // Phase 0 联合评审 (2026-05-31): 生产开启动态 reservation (n_eff/margin 接时序+vig) + net-EV 门。
    //   true (生产默认) → daemon 置 PaperLoopConfig.dynamic_reservation/net_ev_gate=true。
    //   false → 用 lib 静态默认 (n=200/static floor; 管线机制测试关掉新门, 单测新门另测)。
    bool enable_phase0_gates{true};

    // sharp 驱动门 (2026-06-04 老板「sharp 驱动 + 赔率 edge 线」): 仅高置信 sharp(bet365) 信号产单。
    //   true (生产默认) → daemon 置 PaperLoopConfig.sharp_only_gate=true (其余源回退市场, edge 归零)。
    //   false → 通用 fill 管线 (管线机制测试用 score-prior/任意 edge 出成交; sharp 选盘逻辑另有单测)。
    bool sharp_only_gate{true};

    // 只收录 moneyline 赛果盘 (2026-06-04 老板「Dota2 Roshan 等 prop 多了」): 发现/catalog 阶段只留
    //   moneyline (MarketTypeCode==0, 含 tennis To Win), 丢弃 props (Roshan/Rampage/completed_match)/
    //   totals/handicap/outright/series —— 它们无 bet365 sharp 源 (market_implied 本就不交易) → 不入
    //   catalog 免污染 matching/grid/订阅。true (生产默认, 聚焦 sharp 赔率 edge); 全盘口 dispatch 需要时置 false。
    bool moneyline_only{true};

    // 离线测试 seam (小宋): false → Start() 不起真 WSS/inplay 网络线程.
    // Build() 仍完整装配 (供装配正确性单测, 不发外网请求).
    bool start_live_feeds{true};

    // A1b: condition↔goalserve event 映射刷新周期 (秒). 刷新线程低频跑 EventMatcher
    //   (Goalserve event 动态出现, 周期重匹配). 0 → 不起刷新线程 (退回纯 stub).
    int mapping_refresh_sec{2};  // 2026-06-01: 5s→2s (本地 EventMatcher, 无 API; 匹配更新更勤)

    // R-6 (老周/老郭 评审 2026-06-01): 周期重发现间隔 (秒). 0 → 不周期重发现 (退回启动一次性).
    //   live 比赛滚动 (比完一场/开始一场), 不周期重发现则跑几小时后订阅全是死盘。在映射刷新线程跑
    //   (与 market_match_inputs_ 同线程, 无竞争); 全量重发现 + WSS 全量重订 (幂等, 老郭反增量 diff)。
    // 2026-06-01: 300s→2s。discovery 已改 early-stop (扫到连续无在打/即将比赛就停, 不写死页数, 实测
    //   ~10 页自适应), 够轻可高频跑。新比赛 ≤2s 发现。扫描时长 (~3s 顺序拉) 自然托底 gamma 负载 ~2-3 req/s。
    int rediscover_interval_sec{2};

    // gamma 发现规模 (老板 2026-06-01「不要限制, 搞大, 验证期不能限制太狠」: 30→2000 全量发现)
    int max_events{2000};
    int max_markets_flat{10};

    // PaperLoop 参数 (daemon 默认: 500ms tick, 1K pUSD demo bankroll — 逐字对齐原 main).
    // 指定初始化器仅覆盖 bankroll, 其余沿用 PaperLoopConfig 在类默认 (tick=500/n_eff=30/
    // z=1.645/strategy_id="paper-demo-v1"/set_rm_running=true).
    stcpp::paper::PaperLoopConfig paper_loop{.bankroll_usdc = 1000.0};
};

// ---------------------------------------------------------------------------
// BuildResult — Build() 结果 (小宋 seam: 返回结果而非 throw, 便于 CI 断言)
// ---------------------------------------------------------------------------
struct BuildResult {
    bool ok{false};
    std::string error;            // 失败原因 (ok=false 时填)
    std::size_t market_count{0};  // 发现/注入的 market 数
    std::size_t token_count{0};   // 订阅 token 数
};

// ---------------------------------------------------------------------------
// PaperDaemon — paper 常驻进程编排
//
// 生命周期: 构造 → [InjectMarkets()] → Build() → Start() → WaitForStop() → Shutdown().
//   或便捷: 构造 → [InjectMarkets()] → Build() → Run() (= Start+WaitForStop+Shutdown).
// 线程安全: Build/Start/Shutdown 由主线程调用 (非热路径). RequestStop() 可信号上下文调.
// ---------------------------------------------------------------------------
class PaperDaemon {
public:
    explicit PaperDaemon(PaperDaemonConfig cfg) noexcept;

    PaperDaemon(const PaperDaemon&) = delete;
    PaperDaemon& operator=(const PaperDaemon&) = delete;
    PaperDaemon(PaperDaemon&&) = delete;
    PaperDaemon& operator=(PaperDaemon&&) = delete;

    // 析构 — 兜底调 Shutdown() (R-11 INV-1: 保证 detach 在 paper_rm_snap_ 析构前).
    ~PaperDaemon();

    // 测试 seam (小宋): Build() 前注入预置 markets, 跳过真 gamma 发现 (离线可测).
    //   不调用则 Build() 走真实 gamma REST 发现.
    void InjectMarkets(std::vector<DiscoveredEvent> markets);

    // Build — 发现 + 装配全部组件 (不起线程). 幂等 (重复调返回缓存结果).
    BuildResult Build();

    // Start — 按序起后台线程 (幂等). 必须先 Build(). start_live_feeds=false 时跳过真网络.
    void Start();

    // WaitForStop — 阻塞主线程至 RequestStop() (100ms 轮询 stop flag).
    void WaitForStop();

    // RequestStop — 请求停止 (noexcept, 可信号 handler 调).
    void RequestStop() noexcept;

    // Shutdown — 反序优雅停 (幂等, noexcept). detach 全局 hook (R-11 双保险之一).
    void Shutdown() noexcept;

    // Run — 便捷: Start() + WaitForStop() + Shutdown(). 返回退出码 (0=正常).
    int Run();

    // ---- 访问器 (测试 / 观测) ----
    [[nodiscard]] RunMode mode() const noexcept { return cfg_.mode; }
    [[nodiscard]] bool is_built() const noexcept { return built_; }
    [[nodiscard]] bool is_started() const noexcept { return started_.load(std::memory_order_acquire); }
    [[nodiscard]] const debug_api::MarketTokenMap& token_map() const noexcept { return token_map_; }

    // 装配组件只读句柄 (测试断言用; nullptr 若未 Build)
    [[nodiscard]] const polymarket::clob_wss::OrderBookSnapshotHub* hub() const noexcept {
        return hub_.get();
    }
    [[nodiscard]] const sizing::QuoteSnapshotHub* quote_hub() const noexcept { return quote_hub_.get(); }
    [[nodiscard]] const risk::LedgerSnapshotHub* ledger_hub() const noexcept { return ledger_hub_.get(); }
    [[nodiscard]] const risk::PositionLedger* paper_position_ledger() const noexcept {
        return paper_position_ledger_.get();
    }
    [[nodiscard]] const paper::PaperLoop* paper_loop() const noexcept { return paper_loop_.get(); }
    // 测试用 (A1b 集成): 向内部 score_store 发布比分 / 读 quote_hub.
    [[nodiscard]] data::ScoreSnapshotStore* score_store_for_test() noexcept { return score_store_.get(); }
    // 测试用 (A2 端到端): 向内部 book hub 发布合成 book (离线无 WSS 时驱动 paper_loop tick).
    [[nodiscard]] polymarket::clob_wss::OrderBookSnapshotHub* hub_for_test() noexcept { return hub_.get(); }
    [[nodiscard]] const sizing::QuoteSnapshotHub* quote_hub_for_test() const noexcept {
        return quote_hub_.get();
    }
    [[nodiscard]] std::size_t market_match_input_count() const noexcept {
        return market_match_inputs_.size();
    }
    [[nodiscard]] const debug_api::RealStateProvider* state_provider() const noexcept {
        return real_provider_.get();
    }

private:
    // 发现 → token_map_/market_catalog_/event_infos_/all_token_ids_ (gamma 或注入).
    void PopulateCatalog(const std::vector<DiscoveredEvent>& discovered);
    // R-3: 从 token_map_/market_catalog_/market_cat_map_ 构建统一 PaperCatalog (静态元数据)。
    //   一次原子注入 paper_loop_->SetPaperCatalog; R-6 周期重发现复用 (重建后 swap)。
    [[nodiscard]] std::shared_ptr<const paper::PaperCatalog> BuildPaperCatalog() const;
    // R-6: 周期重发现一次 — 全量重建 catalog (清+PopulateCatalog) → SetPaperCatalog + RSP 刷新 +
    //   WSS 全量重订。在映射刷新线程跑 (match_inputs 同线程, 无竞争)。返回 true 若市场集变化。
    bool RediscoverOnce(std::stop_token st);

    // REST 快照打底 (Start 起后台 jthread): POST /books 批量拉初始 book → SeedFromRestBooks。
    //   修"稳定盘/漏接 WSS 初始快照永远空"。后台跑 (不阻塞启动), st 关停时提前退出, 失败优雅降级。
    void SeedInitialBooksFromRest(std::stop_token st);
    // 按指定 token 集 REST seed (SeedInitialBooksFromRest 的核心; RediscoverOnce 增量新增盘也复用):
    //   修"rediscovery 新增盘没补 seed → Polymarket 有簿前端却未接入"。
    void SeedTokensFromRest(const std::vector<std::string>& tokens, std::stop_token st);

    // CLOB WSS 看门狗 (2026-06-02 会议: 修 clob 断开不重连 + 缺心跳 idle 超时):
    //   连着 → 每 10s 发 "PING" 心跳 (Polymarket 服务端不主动 ping, 不发会被 idle 踢, 真因);
    //   断开 → 指数退避后 AsyncConnect 重连 (独立线程, 非 io_thread, 无自 join) + 后台重 seed。
    //   重连重订由 live_transport_ 的 OnConnected 回调 (读 all_token_ids_) 负责。
    void WssWatchdogLoop(std::stop_token st, std::string url);

    // A1b: 映射刷新线程主体 — 周期跑 EventMatcher (score_store 快照 × market 元数据)
    //   → 构建 condition→event 映射 → paper_loop_->SetEventMapping(). Goalserve event
    //   动态出现, 故周期重匹配 (非 boot 一次性)。
    void RefreshEventMapping(std::stop_token st);

    // M2 结算刷新线程: 周期取 SettlementStore 快照 → 构建 ResolutionEntry map →
    //   paper_loop_->SetResolutionByCondition() (喂 3b 权威结算 + CLV 收盘信号)。
    void RefreshResolution(std::stop_token st);

    // live_stats 刷新线程: 周期从 score store 收集活跃 league → poller; LiveStatsStore 快照 →
    //   paper_loop_->SetLiveStatsByTeams() (喂 5 个 g_*_diff 特征)。
    void RefreshLiveStats(std::stop_token st);

    // bm_slots 刷新线程: 周期 popen curl getodds (跨庄家赔率) + inplay-mapping (pregame↔inplay id),
    //   join → OddsMap[inplay_match_id] → paper_loop_->SetOddsByMatchId() (喂 g_bm_* 特征 #5/6/7/16)。
    void RefreshOdds(std::stop_token st);

    // tennis_scores 刷新线程 (覆盖率杠杆 2026-06-03): popen curl tennis_scores/home (全巡回 livescore,
    //   含 inplay 缺的 ITF/Challenger) → ParseTennisScoresLive → inplay_feed_->InjectSupplementalScores
    //   并入 score store → EventMatcher 配上 PM 的 ITF 盘 → 拉高 tennis 覆盖率。
    void RefreshTennisScores(std::stop_token st);

    // 队制 livescore 补充源线程 (覆盖率杠杆 2026-06-03): popen curl cricket/livescore (inplay-cricket
    //   404 → 候选 0) + esports/home (比 inplay-esports 宽) → ParseTeamLivescoreLive → 仅 live →
    //   inplay_feed_->InjectSupplementalScores 并入 score store → EventMatcher 配上 PM 的 crint/esports 盘。
    void RefreshTeamLivescores(std::stop_token st);

    // (RefreshModel / AutoTrain 已砍 2026-06-05「砍掉大模型训练功能」: 不再有模型热重载 watcher /
    //  进程内自动训练编排。)

    // [2026-06-01 老板「超过30g后开始删,一次删5G」] 采集数据磁盘守护线程: 周期算 ml_capture 总大小,
    //   超阈值 → 从最大采集文件头部截 (删最老数据, recorder 每 poll 重开文件故安全) 释放 disk_prune_free_gb。
    void DiskPrune(std::stop_token st);

    PaperDaemonConfig cfg_;

    // ---- 测试注入的 markets (空 → Build 走真发现) ----
    std::vector<DiscoveredEvent> injected_markets_;
    bool has_injected_{false};

    // ---- 发现结果 (Build 填) ----
    debug_api::MarketTokenMap token_map_;
    debug_api::MarketInfoMap market_catalog_;
    std::unordered_map<std::string, paper::MarketCat> market_cat_map_;  // v0.7 类别上下文 (ML 特征 82-85)
    std::vector<debug_api::EventInfo> event_infos_;
    std::vector<std::string> all_token_ids_;

    // 结束赛事黑名单 (2026-06-02 老板「直播结束应及时退订+拉黑不再订阅」):
    //   赛事 Goalserve 比分=final → 加 event_id 黑名单(带过期 ts); PopulateCatalog 跳过未过期项 →
    //   下轮 RediscoverOnce 退订其 token + 释放 hub 书槽, 且 gamma 仍列(等结算)也不再重订。
    //   **按 event_id 拉黑 (每场比赛唯一; 第二场是不同 event_id 不受影响, 老板「第二场会不会进不来」)**。
    //   **带 TTL 自愈 (老板保险): 超 kBlacklistTtlNs 自动失效, 防 id 异常复用/误判永久挡。** 映射线程写,
    //   PopulateCatalog(同线程)读; 加锁防 settlement 线程并发。超 kBlacklistCap 清空防无界。
    mutable std::mutex ended_blacklist_mu_;
    std::unordered_map<std::string, std::int64_t> ended_event_blacklist_;  // event_id → 过期 epoch ns
    static constexpr std::size_t kBlacklistCap = 20000;
    static constexpr std::int64_t kBlacklistTtlNs = 6LL * 3600 * 1'000'000'000;  // 6h 自动失效

    // A1 (2026-06-02): token 集快照 — 消 all_token_ids_ 的 data race (OnConnected io_thread 读 /
    //   RediscoverOnce 映射线程写)。mutex 守护的 shared_ptr<const vector> COW (atomic<shared_ptr>
    //   非全平台可用)。写方 PopulateCatalog 后 PublishTokenSnapshot(); 读方 (OnConnected/seed)
    //   读 TokenSnapshot() 不可变快照, 不碰裸 all_token_ids_。
    mutable std::mutex token_ids_mu_;
    std::shared_ptr<const std::vector<std::string>> token_ids_snapshot_{
        std::make_shared<const std::vector<std::string>>()};
    // A4: WSS 重连计数 (看门狗每次重连 +1; 经 LiveMetricsHooks 暴露给 /metrics)
    std::atomic<std::uint64_t> wss_reconnect_total_{0};

    void PublishTokenSnapshot() {
        std::lock_guard<std::mutex> lk(token_ids_mu_);
        token_ids_snapshot_ = std::make_shared<const std::vector<std::string>>(all_token_ids_);
    }
    std::shared_ptr<const std::vector<std::string>> TokenSnapshot() const {
        std::lock_guard<std::mutex> lk(token_ids_mu_);
        return token_ids_snapshot_;
    }

    // 源头 pass (2026-06-04 老板「从源头就不订阅无赔率源的比赛」): 有 bet365 赔率源的 condition 集快照。
    //   RefreshEventMapping (映射线程) 每周期算出有 sharp 赔率的 condition (含 grace 滞回 — 赔率短暂
    //   挂起 halftime 不立即退订), 发布此快照。PopulateCatalog (发现/重订线程) 读它过滤 all_token_ids_:
    //   只订阅有赔率源的盘 → 退订无源盘, 腾出 WSS/book 配额给 149hz 主动轮询。
    //   null = 映射线程尚未就绪 (bootstrap): 订全量, 首个映射周期后收敛 sharp-only。
    mutable std::mutex sharp_conds_mu_;
    std::shared_ptr<const std::unordered_set<std::string>> sharp_conditions_snapshot_;  // null=未就绪
    void PublishSharpConditions(std::shared_ptr<const std::unordered_set<std::string>> snap) {
        std::lock_guard<std::mutex> lk(sharp_conds_mu_);
        sharp_conditions_snapshot_ = std::move(snap);
    }
    [[nodiscard]] std::shared_ptr<const std::unordered_set<std::string>> SharpConditionsSnapshot() const {
        std::lock_guard<std::mutex> lk(sharp_conds_mu_);
        return sharp_conditions_snapshot_;
    }
    // 上次据以构建订阅集的 eligible 快照内容 (仅映射线程读写, 无锁): RediscoverOnce 比对 → 即便市场集
    //   未变, eligible 集变了 (赔率源增删) 也要重订/退订。否则稳定市场集下 bootstrap 全订阅永不收敛。
    std::unordered_set<std::string> last_sub_eligible_;
    // live 装配件 (2026-06-12 实盘准备; 仅 live build 构造, paper build 恒 null)
    std::unique_ptr<polymarket::LiveOrderSubmitter> live_submitter_;
    std::unique_ptr<polymarket::LiveOrderGate> live_gate_;
    std::unique_ptr<polymarket::LiveExecutor> live_exec_;

    // 149hz 主动 book 轮询计划 (2026-06-04 老板「主动查订单簿压限速, 全市场共享 149hz, 按流动性分配」):
    //   (token_id, weight=√liquidity+1) 列表, 仅含有赔率源 (源头 pass 过滤后) 的 token。映射线程写
    //   (PopulateCatalog), ActiveBookPoller 线程读 (COW + mutex 快照)。weighted-RR 按 weight 分配 149 req/s。
    mutable std::mutex poll_plan_mu_;
    std::shared_ptr<const std::vector<std::pair<std::string, double>>> poll_plan_snapshot_{
        std::make_shared<const std::vector<std::pair<std::string, double>>>()};
    void PublishPollPlan(std::vector<std::pair<std::string, double>> plan) {
        std::lock_guard<std::mutex> lk(poll_plan_mu_);
        poll_plan_snapshot_ =
            std::make_shared<const std::vector<std::pair<std::string, double>>>(std::move(plan));
    }
    [[nodiscard]] std::shared_ptr<const std::vector<std::pair<std::string, double>>> PollPlanSnapshot()
        const {
        std::lock_guard<std::mutex> lk(poll_plan_mu_);
        return poll_plan_snapshot_;
    }
    // ActiveBookPoller — 热链 GET /book?token_id=X 主动轮询 (149 req/s, 流动性加权), 喂 live_publisher_。
    void RunActiveBookPoller(std::stop_token st) noexcept;
    std::atomic<std::uint64_t> active_poll_total_{0};  // 主动轮询请求计数 (观测)
    std::atomic<std::uint64_t> active_poll_ok_{0};     // 成功 (非空响应) 计数

    // ---- A1b: 映射桥 (EventMatcher + 元数据 + 刷新线程) ----
    EventMatcher event_matcher_;
    // condition_id → market 锚定输入 (两队名 + kickoff + sport; Build 从 DiscoveredMarket 填).
    std::unordered_map<std::string, EventMatchInput> market_match_inputs_;
    std::jthread mapping_refresh_thread_;
    std::jthread settlement_refresh_thread_;  // M2 结算刷新 (SettlementStore → SetResolutionByCondition)
    std::jthread live_stats_refresh_thread_;  // live_stats 刷新 (LiveStatsStore → SetLiveStatsByTeams)
    std::jthread odds_refresh_thread_;        // bm_slots 刷新 (getodds + inplay-mapping → SetOddsByMatchId)
    std::jthread tennis_scores_refresh_thread_;  // 覆盖率: tennis_scores livescore → InjectSupplementalScores
    std::jthread team_livescore_refresh_thread_;  // 覆盖率: cricket/esports livescore → InjectSupplementalScores
    // (model_reload_thread_ / auto_train_thread_ 已砍 2026-06-05「砍掉大模型训练功能」)
    std::jthread disk_prune_thread_;          // 采集数据磁盘守护 (>阈值 → 截最老数据)
    std::jthread seed_thread_;  // REST 快照打底后台线程 (jthread: 析构自动 request_stop + join)
    std::jthread active_poll_thread_;  // 149hz 主动 book 轮询 (热链 GET /book, 流动性加权; 老板 2026-06-04)
    std::jthread wss_watchdog_thread_;  // CLOB WSS 心跳(10s PING)+ 断线重连看门狗 (2026-06-02)

    // =====================================================================
    // 装配组件 —— 声明顺序即析构逆序的逆 (老韩 R-11 INV-1 + 老周钉死1):
    //   被依赖者先声明 (后析构); paper_rm_snap_ 必在 paper_loop_ 之前;
    //   server_/ml_recorder_ 最后声明 (先析构, 先停读端).
    // =====================================================================

    // 基础 hub (被 RealStateProvider/PaperLoop/publisher 引用 → 最先声明, 最后析构)
    std::unique_ptr<polymarket::clob_wss::OrderBookSnapshotHub> hub_;
    std::unique_ptr<data::ScoreSnapshotStore> score_store_;
    std::unique_ptr<risk::LedgerSnapshotHub> ledger_hub_;
    std::unique_ptr<sizing::QuoteSnapshotHub> quote_hub_;

    // paper 账本 (R-11: 私有独立实例)
    std::unique_ptr<risk::PositionLedger> paper_position_ledger_;

    // R-11 INV-1: paper_rm_snap_ 在 paper_loop_ 之前声明 (paper_loop_ 逆序先析构).
    std::unique_ptr<risk::RmDebugSnapshot> paper_rm_snap_;

    // paper RM 栈
    std::shared_ptr<risk::AuditEmitter> paper_audit_emitter_;
    std::unique_ptr<risk::RiskGateway> paper_rm_;
    std::unique_ptr<pricing::BaselineFairValueModel> paper_fv_model_;  // score-prior 统计估计器 (留)
    // (大模型 fair_value_model_ (ml::FairValueModel ONNX) 已砍 2026-06-05「砍掉大模型训练功能」)

    // PaperLoop (用上述全部; 必在 paper_rm_snap_ 之后声明)
    std::unique_ptr<paper::PaperLoop> paper_loop_;

    // feeds & transport
    std::unique_ptr<data::InplayFeedThread> inplay_feed_;
    std::unique_ptr<data::SettlementStore> settlement_store_;    // M2 收盘/结算快照
    std::unique_ptr<data::SettlementPoller> settlement_poller_;  // M2 clob /markets 轮询
    std::unique_ptr<data::livescore::LiveStatsStore> live_stats_store_;        // live_stats 快照
    std::unique_ptr<data::livescore::CommentariesPoller> commentaries_poller_;  // commentaries 30s 轮询
    std::unique_ptr<polymarket::clob_wss::LiveBookPublisher> live_publisher_;
    std::unique_ptr<polymarket::clob_wss::LiveWssTransport> live_transport_;

    // 读模型 (server_ 持其指针 → real_provider_ 在 server_ 之前声明)
    std::unique_ptr<debug_api::RealStateProvider> real_provider_;
    debug_api::LiveMetricsHooks metrics_hooks_;

    // (ML 训练采集成员已砍 2026-06-05: ml_recorder_/fv_hub_/fv_recorder_)
    std::unique_ptr<data::SettlementRecorder> settlement_recorder_;  // 结算落盘 (回测数据)
    std::unique_ptr<data::ScoreFrameRecorder> score_recorder_;       // 回测 P0: 比分帧落盘 (红线#3 闭合数据前提)

    // HTTP 观测端 (最后声明, 最先析构; 仅 RunMode::PaperDaemon)
    std::unique_ptr<debug_api::HttpServer> server_;

    // ---- 状态 ----
    bool built_{false};
    BuildResult build_result_{};
    // R-11 INV-1: 是否已 attach 全局 hook (Shutdown 仅在 attach 过时 detach, 防误清).
    bool attached_rm_snap_{false};
    std::atomic<bool> started_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> shutdown_done_{false};
};

}  // namespace stcpp::app
