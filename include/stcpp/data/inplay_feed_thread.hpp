// stcpp/data/inplay_feed_thread.hpp — Goalserve inplay 采集线程 (R-12 合规)
//
// Owner: 小段 (goalserve-specialist, #37)
// Date:  2026-05-29
// Last-updated: 2026-05-30 (security harden: token-bucket/min_fetch_interval, 小白审计 §2.2)
//
// 功能:
//   独立采集线程: HTTP GET inplay-<sport>.gz → gzip 解压
//   → InplayScoreParser::Parse → ScoreSnapshotStore::Publish
//   MVP sport: Soccer / Basketball / Tennis
//   轮询周期: ~1s (Goalserve inplay 刷新频率约 1s)
//
// R-12 合规:
//   独立 std::thread, 绝不在 WSS event loop 内调用 HTTP/阻塞 IO.
//   ScoreSnapshotStore::Publish 原子 swap, 非阻塞.
//
// R-20 合规:
//   data_source_ts_ns = Goalserve updated_ts (ms) × 1e6 (PayloadScoresTs)
//   event_ts_ns       = start_ts (Unix sec) × 1e9 (比赛排定开始时刻)
//   ingestion_ts_ns   = HTTP body 收完时刻 (CLOCK_MONOTONIC_RAW ns)
//   禁 now() 替代前三个 ts
//
// HTTP 实现:
//   POSIX 系统调用 (socket/connect/recv) + libz gzip 解压
//   无 cpp-httplib 依赖 (避免与 debug_api cpp-httplib 实例冲突)
//   代理: 读 http_proxy / HTTP_PROXY 环境变量 (CONNECT 隧道)
//
// 线程安全:
//   Start() 启动后台线程; Stop() 触发停止并 join.
//   store 线程安全 (ScoreSnapshotStore mutex-protected swap).
//
// 不耻下问:
//   @老周: 线程 affinity / vCPU3 pinning (本实现无 affinity, W6 可加)
//   @小余: ScoreSnapshotStore key 策略 (本实现用 inplay_match_id 做 key)

#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "stcpp/data/goalserve_adapter.hpp"  // adapter::GameScoreRecord
#include "stcpp/data/goalserve_client.hpp"
#include "stcpp/data/score_snapshot_store.hpp"

namespace stcpp::data {

// ============================================================================
// InplayFeedConfig — 采集线程配置
// ============================================================================
struct InplayFeedConfig {
    // Goalserve inplay endpoint (无 API key, 公开 endpoint)
    // 实测: http://inplay.goalserve.com/inplay-<sport>.gz
    std::string inplay_host = "inplay.goalserve.com";
    std::uint16_t inplay_port = 80;

    // 轮询周期 (ms); Goalserve inplay ~1s 刷新
    std::uint32_t poll_interval_ms = 1000;  // 2026-06-01: 1200→1000 压到 Goalserve per-sport 1 req/s 限

    // HTTP 超时 (ms)
    std::uint32_t http_timeout_ms = 8000;

    // 代理 (空串 = 直连; "host:port" 格式)
    // 自动从 http_proxy / HTTP_PROXY 环境变量读取 (若本字段为空)
    std::string http_proxy;

    // sport 列表 (2026-06-01 加 Esports: PM 有 dota2/lol/CS 盘, Goalserve inplay-esports.gz 实测 200,
    //   解析器 info.name "A vs B" split 适配 → 匹配可用。注: esports 比分在 stats."Res" 非 info.score,
    //   故 home/away_score 暂 0:0(不影响【匹配】, 影响【定价】— esports 定价模型另立)。cricket inplay 404 暂无。)
    std::vector<goalserve::GoalserveSport> sports = {
        goalserve::GoalserveSport::Soccer,
        goalserve::GoalserveSport::Basketball,
        goalserve::GoalserveSport::Tennis,
        goalserve::GoalserveSport::Esports,
    };

    // 连续失败后 backoff 最大时长 (ms)
    std::uint32_t max_backoff_ms = 30000;

    // 【每 sport 线程】fetch 最小间隔 (ms) — Goalserve 限速是 per-sport (实测 1 req/s/sport,
    //   laochen-api-rate-latency-ssot §rate), 不是 per-IP 全局, 故 per-sport 限速正确。
    //   1000ms = 每 sport ≤1 req/s (恰好贴限, 配 poll_interval_ms=1200 实际 0.83/s 留 margin)。
    //   2026-06-01: 800→1000, 给 1/s 硬上限留余量 (此前 429 实为诊断期手动 curl 叠加, 非 daemon 本身超速)。
    std::uint32_t min_fetch_interval_ms = 1000;
    // (相位对齐配置已移除 2026-06-05: 1 req/s ToS 限速下相位锁不住, 实验证明 no-op; 见
    //  experiments/laolei-phase-align。回到朴素固定 poll_interval_ms 轮询。)
};

// ============================================================================
// InplayFeedThread — 独立采集线程 (R-12 合规)
//
// 用法:
//   ScoreSnapshotStore store;
//   InplayFeedConfig cfg;
//   InplayFeedThread feed(store, cfg);
//   feed.Start();
//   // ... running ...
//   feed.Stop();  // 阻塞直到线程退出
//
// 内部每 sport 独立线程 (防止一个 sport 阻塞另一个).
// ============================================================================
class InplayFeedThread {
public:
    explicit InplayFeedThread(ScoreSnapshotStore& store, InplayFeedConfig cfg = {}) noexcept;

    // 禁止拷贝/移动 (线程句柄不可拷贝)
    InplayFeedThread(const InplayFeedThread&) = delete;
    InplayFeedThread& operator=(const InplayFeedThread&) = delete;
    InplayFeedThread(InplayFeedThread&&) = delete;
    InplayFeedThread& operator=(InplayFeedThread&&) = delete;

    ~InplayFeedThread();

    // Start — 为每个 sport 启动独立采集线程
    // 幂等: 已启动则无操作
    void Start();

    // Stop — 停止所有采集线程 (设 stop flag → join)
    // 阻塞直到所有线程退出
    void Stop();

    // IsRunning — 是否有线程在运行
    [[nodiscard]] bool IsRunning() const noexcept { return running_.load(std::memory_order_acquire); }

    // Diagnostics — 各 sport 最后一次成功拉取的 updated_ts_ms
    // (0 = 尚未成功拉取)
    [[nodiscard]] std::int64_t last_updated_ts_ms(goalserve::GoalserveSport sport) const noexcept;

    // 各 sport 最后一次拉取的 event count
    [[nodiscard]] std::int64_t last_event_count(goalserve::GoalserveSport sport) const noexcept;

    // InjectSupplementalScores — 外部 (daemon RefreshTennisScores/RefreshTeamLivescores) 注入补充
    //   比分源 (tennis_scores / cricket / esports livescore: 覆盖 inplay-*.gz bet365 联动缺的场)。
    //   merge 进同一 merged_map_ 并 republish (单一发布者口径, 不与 RunSportLoop 的 Publish 互踩 —
    //   同 merged_mu_)。
    //   source_id: 多源隔离键 ("tennis_scores"/"cricket"/"esports") — 每源独立 key 集, 刷新时只删
    //     【本源】上轮 key (不互相清掉; 旧实现单一 supplemental_keys_ 调多次会互踩, 已修)。
    //   去重 (sport-aware): 同 sport 同对阵已被现有条目 (inplay 带 odds 或其他源) 占 → 跳过, 不盖。
    //     tennis 用末段姓 (格式差异容忍); 队制 (cricket/esports) 用归一化全名 token 集。
    //   返回净注入数 (post-dedup, 实际进 store 的场数; 调用方日志诊断覆盖增益)。
    std::size_t InjectSupplementalScores(const std::string& source_id,
                                         std::vector<debug_api::EventScore> recs) noexcept;

private:
    // 单 sport 的采集循环 (在独立线程中运行)
    void RunSportLoop(goalserve::GoalserveSport sport) noexcept;

    // HTTP GET inplay-<sport>.gz → 原始 gzip body
    // 返回 true = 成功; body 填充 gzip 字节; ingestion_ns 填充收完时刻
    [[nodiscard]] bool FetchGz(goalserve::GoalserveSport sport, std::string& gz_body,
                               std::int64_t& ingestion_ns) noexcept;

    // HTTP GET dictionaries/odds-markets/<sport> → 明文 JSON (2026-06-04 老板「用 goalserve 字典」)。
    //   字典无 key (公开端点) + 非 gz (明文)。返回 true = 200 且 body 非空。失败 → 调用方回退启发式选盘。
    [[nodiscard]] bool FetchDict(goalserve::GoalserveSport sport, std::string& json_body) noexcept;

    // gzip 解压 (zlib inflate)
    // 返回 true = 成功; out_json 填充解压后 JSON
    [[nodiscard]] static bool DecompressGz(const std::string& gz_body, std::string& out_json) noexcept;

    // GameScoreRecord → EventScore (ScoreSnapshotStore key = inplay_match_id)
    // 用于 ScoreSnapshotStore::Publish
    [[nodiscard]] static debug_api::EventScore ToEventScore(const data::adapter::GameScoreRecord& rec,
                                                            goalserve::GoalserveSport sport) noexcept;

    // 从 http_proxy / HTTP_PROXY 环境变量解析代理 "host:port"
    [[nodiscard]] static std::string ReadProxyFromEnv() noexcept;

    ScoreSnapshotStore& store_;
    InplayFeedConfig cfg_;

    std::atomic<bool> stop_{false};
    std::atomic<bool> running_{false};
    std::vector<std::thread> threads_;

    // 诊断计数器 (每 sport 一个槽, 按 GoalserveSport 枚举值索引)
    static constexpr std::size_t kNumSports = 8;
    std::atomic<std::int64_t> last_updated_ts_ms_[kNumSports]{};
    std::atomic<std::int64_t> last_event_count_[kNumSports]{};

    // 多 sport 合并 map — 每 sport 线程 fetch 后 merge 到此, 再 Publish
    // merged_mu_ 保护 merged_map_ + sport_keys_ (非热路径, O(events) 操作)
    // R-12: 持锁时间 = O(events) hash op << 100us (无 IO, 纯内存)
    mutable std::mutex merged_mu_;
    ScoreMap merged_map_;
    std::set<std::string> sport_keys_[kNumSports];  // per-sport key set (删旧用)
    // 补充源 key 集: 按 source_id 隔离 (tennis_scores/cricket/esports), 刷新只删本源旧 key。
    std::map<std::string, std::set<std::string>> supplemental_keys_by_source_;
};

}  // namespace stcpp::data
