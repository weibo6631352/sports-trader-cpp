// stcpp/ml/feature_recorder.hpp — ML 训练数据采集器 (header-only)
//
// Owner: 老雷 (GM, 亲写) 2026-05-30
// 背景: 老板 2026-05-30 verbatim — "ml模型需要做的, 我们有实时的市场数据和直播
//       数据, 没历史的也没关系啊, 如果必须要历史, 我们自己慢慢积累就行了啊"
//
// 用途:
//   paper daemon 常驻运行时, 持续把每次报价决策快照 (QuoteFeatures) 去重追加成
//   JSONL, 落 data/ml_capture/. 一边跑一边攒训练数据集, 解决"无历史数据"——
//   自建数据集。DuckDB / pandas 可直接读 JSONL 做后续 ML 训练 (训练离线 Python,
//   推理 C++, 见 §12.4)。
//
// 设计 (最小, 不碰 paper_loop 热路径):
//   - 独立 std::thread, 默认 5s 轮询
//   - 持有 condition_id 列表 (启动注入), 逐个 quote_hub.Read(cond) — Hub 无 ReadAll
//   - 按 (condition_id → as_of_ts_ns) 去重, 只记新快照, 避免重复行
//   - 手写 JSONL (无 glaze 依赖; 字段为受控数值, condition_id 为 hex 无需转义)
//   - 落 /data/ml_capture/ (已 gitignore, 不污染 git)
//
// 红线:
//   R-12: 独立线程, 低频, 不进 WSS event loop; 文件 append IO 在本线程内, 不阻塞热路径
//   R-20: 透传上游 4 ts (event/data_source/ingestion/as_of), 不用本地 now() 替代上游 ts
//   R-11: 只读 quote_hub 快照, 不写任何真账本 (position/pnl/nonce)
//   ToS:  纯本地落盘, 不向任何 vendor 发请求
//   §12.4: 不引入 Python; 落盘为 C++ ofstream, 训练侧离线读取

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "stcpp/sizing/quote_snapshot_hub.hpp"

namespace stcpp::ml {

class FeatureRecorder {
public:
    struct Config {
        std::string output_path = "data/ml_capture/quotes.jsonl";
        int poll_interval_sec = 5;
    };

    // condition_ids: 要采集的盘口列表 (启动时注入, 通常 = token_map 的 key 集合)
    FeatureRecorder(const sizing::QuoteSnapshotHub& quote_hub, std::vector<std::string> condition_ids,
                    Config cfg)
        : quote_hub_(quote_hub), condition_ids_(std::move(condition_ids)), cfg_(std::move(cfg)) {}

    ~FeatureRecorder() { Stop(); }

    FeatureRecorder(const FeatureRecorder&) = delete;
    FeatureRecorder& operator=(const FeatureRecorder&) = delete;
    FeatureRecorder(FeatureRecorder&&) = delete;
    FeatureRecorder& operator=(FeatureRecorder&&) = delete;

    // Start — 起采集线程. 重复调用幂等.
    void Start() {
        if (running_.exchange(true)) {
            return;
        }
        thread_ = std::thread([this] { Run(); });
    }

    // Stop — 停采集线程并 join. 重复调用幂等. 析构自动调用.
    void Stop() noexcept {
        if (!running_.exchange(false)) {
            return;
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    [[nodiscard]] std::uint64_t records_written() const noexcept {
        return records_written_.load(std::memory_order_relaxed);
    }

private:
    void Run() {
        // 确保输出目录存在 (data/ml_capture/)
        std::error_code ec;
        const std::filesystem::path p(cfg_.output_path);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path(), ec);
        }

        std::ofstream out(cfg_.output_path, std::ios::app);
        if (!out.is_open()) {
            std::fprintf(stderr, "[ml_recorder] WARN: 无法打开 %s, ML 采集禁用\n", cfg_.output_path.c_str());
            running_.store(false, std::memory_order_relaxed);
            return;
        }
        std::fprintf(stderr, "[ml_recorder] ML 训练数据采集启动 -> %s (%zu 盘口, 每 %ds 去重落盘)\n",
                     cfg_.output_path.c_str(), condition_ids_.size(), cfg_.poll_interval_sec);

        while (running_.load(std::memory_order_relaxed)) {
            for (const auto& cond : condition_ids_) {
                const auto opt = quote_hub_.Read(cond);
                if (!opt.has_value() || !opt->valid) {
                    continue;  // 该盘口尚无 quote (PaperLoop 还没 Publish)
                }
                const sizing::QuoteFeatures& q = *opt;
                // 去重: 同 condition 的 as_of_ts 未前进则跳过
                auto it = last_ts_.find(cond);
                if (it != last_ts_.end() && it->second >= q.as_of_ts_ns) {
                    continue;
                }
                last_ts_[cond] = q.as_of_ts_ns;
                WriteLine(out, cond, q);
                records_written_.fetch_add(1, std::memory_order_relaxed);
            }
            out.flush();

            // 分片 sleep, 对 Stop() 响应 (100ms 粒度)
            const int slices = cfg_.poll_interval_sec * 10;
            for (int i = 0; i < slices && running_.load(std::memory_order_relaxed); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    static void WriteLine(std::ofstream& out, const std::string& cond, const sizing::QuoteFeatures& q) {
        // JSONL 一行一决策快照. 字段为受控数值/hex, 无需 JSON 转义.
        out << "{\"condition_id\":\"" << cond << "\""
            << ",\"fair_value\":" << q.fair_value << ",\"market_mid\":" << q.market_mid
            << ",\"edge_bps\":" << q.edge_bps << ",\"fee_rate_coef\":"
            << q.fee_rate_coef
            // 盘口上下文 / 双边微观结构 (模型输入, 不 gate; 2026-05-31)
            << ",\"cross_spread\":" << q.cross_spread << ",\"no_microprice\":" << q.no_microprice
            << ",\"yes_imbalance\":" << q.yes_imbalance << ",\"no_imbalance\":" << q.no_imbalance
            << ",\"devig_ok\":" << (q.devig_ok ? "true" : "false")
            << ",\"joint_as_of_ts_ns\":" << q.joint_as_of_ts_ns << ",\"kelly_fraction\":" << q.kelly_fraction
            << ",\"suggested_notional\":" << q.suggested_notional
            << ",\"signal_strength\":" << q.signal_strength << ",\"model_confidence\":" << q.model_confidence
            << ",\"fair_ci_lower\":" << q.fair_ci_lower << ",\"fair_ci_upper\":" << q.fair_ci_upper
            << ",\"predict_ok\":" << (q.predict_ok ? "true" : "false")
            << ",\"advisory\":" << (q.advisory ? "true" : "false")
            << ",\"model_calibrated\":" << (q.model_calibrated ? "true" : "false")
            << ",\"event_ts_ns\":" << q.event_ts_ns << ",\"data_source_ts_ns\":" << q.data_source_ts_ns
            << ",\"ingestion_ts_ns\":" << q.ingestion_ts_ns << ",\"as_of_ts_ns\":" << q.as_of_ts_ns << "}\n";
    }

    const sizing::QuoteSnapshotHub& quote_hub_;
    std::vector<std::string> condition_ids_;
    Config cfg_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::unordered_map<std::string, std::int64_t> last_ts_;
    std::atomic<std::uint64_t> records_written_{0};
};

}  // namespace stcpp::ml
