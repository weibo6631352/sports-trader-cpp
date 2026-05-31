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
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <type_traits>
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

    // 数值 → JSON 安全串。浮点 NaN/Inf → "null" (C++ << 对 NaN 输出小写 "nan"/"inf" 是非法 JSON,
    //   破坏 Python json.loads 整行; loader 逐行 json.loads, 一行坏全炸)。有限浮点 %g (与 << 同 6 位
    //   有效口径); 整数 %lld 全精度 (ts_ns 用 %g 会科学计数丢精度)。所有数值字段统一过此, 杜绝 nan 泄漏。
    template <class T>
    static std::string JsonNum(T v) {
        char buf[40];
        if constexpr (std::is_floating_point_v<T>) {
            if (!std::isfinite(static_cast<double>(v)))
                return "null";
            std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(v));
        } else {
            std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
        }
        return buf;
    }

    static void WriteLine(std::ofstream& out, const std::string& cond, const sizing::QuoteFeatures& q) {
        // JSONL 一行一决策快照. 字段为受控数值/hex, 无需 JSON 转义. 数值全过 JsonNum (NaN→null)。
        out << "{\"condition_id\":\"" << cond << "\""
            << ",\"fair_value\":" << JsonNum(q.fair_value) << ",\"market_mid\":" << JsonNum(q.market_mid)
            << ",\"edge_bps\":" << JsonNum(q.edge_bps) << ",\"fee_rate_coef\":" << JsonNum(q.fee_rate_coef)
            // v0.7 类别上下文码 (真实 Polymarket 市场结构; 与 fv.jsonl 82-85 列同源 QuoteFeatures 载体,
            //   此处对齐写入 quotes.jsonl — 离线分析/join 免再去查 fv 向量)。unknown=-1。
            << ",\"cat_asset_class_id\":" << JsonNum(q.cat_asset_class_id)
            << ",\"cat_sport_family_id\":" << JsonNum(q.cat_sport_family_id)
            << ",\"cat_league_id\":" << JsonNum(q.cat_league_id)
            << ",\"cat_market_type_id\":" << JsonNum(q.cat_market_type_id) << ",\"line\":" << JsonNum(q.line)
            // 树父级引用 (按 event join 兄弟盘口; neg_risk 一致性) + 双边微观结构 (模型输入, 不 gate;
            // 2026-05-31)
            << ",\"event_id\":\"" << q.event_id << "\""
            << ",\"neg_risk_market_id\":\"" << q.neg_risk_market_id << "\""
            << ",\"cross_spread\":" << JsonNum(q.cross_spread) << ",\"no_microprice\":" << JsonNum(q.no_microprice)
            << ",\"yes_imbalance\":" << JsonNum(q.yes_imbalance) << ",\"no_imbalance\":" << JsonNum(q.no_imbalance)
            << ",\"devig_ok\":" << (q.devig_ok ? "true" : "false")
            << ",\"joint_as_of_ts_ns\":" << JsonNum(q.joint_as_of_ts_ns)
            // 当前持仓 (库存感知; 目标仓位范式)。双边量 (老板「各边买了多少」): YES/NO 各持仓 + avg。
            << ",\"pos_yes_qty\":" << JsonNum(q.pos_yes_qty) << ",\"pos_no_qty\":" << JsonNum(q.pos_no_qty)
            << ",\"pos_yes_avg_entry\":" << JsonNum(q.pos_yes_avg_entry)
            << ",\"pos_no_avg_entry\":" << JsonNum(q.pos_no_avg_entry)
            << ",\"pos_net_qty\":" << JsonNum(q.pos_net_qty) << ",\"pos_avg_entry\":" << JsonNum(q.pos_avg_entry)
            << ",\"pos_condition_exposure_usdc\":" << JsonNum(q.pos_condition_exposure_usdc)
            << ",\"kelly_fraction\":" << JsonNum(q.kelly_fraction)
            << ",\"suggested_notional\":" << JsonNum(q.suggested_notional)
            << ",\"signal_strength\":" << JsonNum(q.signal_strength)
            << ",\"model_confidence\":" << JsonNum(q.model_confidence)
            << ",\"fair_ci_lower\":" << JsonNum(q.fair_ci_lower) << ",\"fair_ci_upper\":" << JsonNum(q.fair_ci_upper)
            << ",\"predict_ok\":" << (q.predict_ok ? "true" : "false")
            << ",\"advisory\":" << (q.advisory ? "true" : "false")
            << ",\"model_calibrated\":" << (q.model_calibrated ? "true" : "false")
            << ",\"ml_advisory_p_yes\":" << JsonNum(q.ml_advisory_p_yes)
            // 时序微结构 YES 边 (Phase 2: 训练 X 含第一梯队 alpha b_ofi; 联合评审 2026-05-31)
            << ",\"mp_roc_per_sec\":" << JsonNum(q.mp_roc_per_sec) << ",\"realized_vol\":" << JsonNum(q.realized_vol)
            << ",\"ts_window_samples\":" << JsonNum(q.ts_window_samples)
            << ",\"bid_absence_frac\":" << JsonNum(q.bid_absence_frac)
            << ",\"exit_depth_mean\":" << JsonNum(q.exit_depth_mean)
            << ",\"b_amihud\":" << JsonNum(q.b_amihud) << ",\"b_bid_depth_vol\":" << JsonNum(q.b_bid_depth_vol)
            << ",\"b_ofi\":" << JsonNum(q.b_ofi) << ",\"b_vol_ratio\":" << JsonNum(q.b_vol_ratio)
            << ",\"b_mp_roc_30s\":" << JsonNum(q.b_mp_roc_30s) << ",\"b_mp_roc_5m\":" << JsonNum(q.b_mp_roc_5m)
            // 时序微结构 NO 边 (双边对称; 独立信号)
            << ",\"no_mp_roc_per_sec\":" << JsonNum(q.no_mp_roc_per_sec)
            << ",\"no_realized_vol\":" << JsonNum(q.no_realized_vol)
            << ",\"no_ts_window_samples\":" << JsonNum(q.no_ts_window_samples)
            << ",\"no_bid_absence_frac\":" << JsonNum(q.no_bid_absence_frac)
            << ",\"no_exit_depth_mean\":" << JsonNum(q.no_exit_depth_mean)
            << ",\"no_b_amihud\":" << JsonNum(q.no_b_amihud)
            << ",\"no_b_bid_depth_vol\":" << JsonNum(q.no_b_bid_depth_vol) << ",\"no_b_ofi\":" << JsonNum(q.no_b_ofi)
            << ",\"no_b_vol_ratio\":" << JsonNum(q.no_b_vol_ratio)
            << ",\"no_b_mp_roc_30s\":" << JsonNum(q.no_b_mp_roc_30s)
            << ",\"no_b_mp_roc_5m\":" << JsonNum(q.no_b_mp_roc_5m)
            // cross / log-odds (残差框架 base 特征)
            << ",\"x_log_odds_fair\":" << JsonNum(q.x_log_odds_fair)
            << ",\"x_log_odds_edge\":" << JsonNum(q.x_log_odds_edge)
            << ",\"x_pin_risk\":" << JsonNum(q.x_pin_risk) << ",\"x_pin_x_expiry\":" << JsonNum(q.x_pin_x_expiry)
            << ",\"b_dislocation\":" << JsonNum(q.b_dislocation)
            // v0.8 L2-L5 深度分布 (双边独立)
            << ",\"b_bid_depth_5lvl\":" << JsonNum(q.b_bid_depth_5lvl)
            << ",\"b_ask_depth_5lvl\":" << JsonNum(q.b_ask_depth_5lvl)
            << ",\"b_l1_concentration\":" << JsonNum(q.b_l1_concentration)
            << ",\"b_depth_imbalance_5lvl\":" << JsonNum(q.b_depth_imbalance_5lvl)
            << ",\"no_b_bid_depth_5lvl\":" << JsonNum(q.no_b_bid_depth_5lvl)
            << ",\"no_b_ask_depth_5lvl\":" << JsonNum(q.no_b_ask_depth_5lvl)
            << ",\"no_b_l1_concentration\":" << JsonNum(q.no_b_l1_concentration)
            << ",\"no_b_depth_imbalance_5lvl\":" << JsonNum(q.no_b_depth_imbalance_5lvl)
            // sports 动态 (第一梯队 alpha: g_time_x_lead / g_goal_freshness)
            << ",\"g_time_x_lead\":" << JsonNum(q.g_time_x_lead) << ",\"g_fld_signal\":" << JsonNum(q.g_fld_signal)
            << ",\"g_remaining_sec\":" << JsonNum(q.g_remaining_sec)
            << ",\"g_periods_won_home\":" << JsonNum(q.g_periods_won_home)
            << ",\"g_periods_won_away\":" << JsonNum(q.g_periods_won_away)
            << ",\"g_game_phase\":" << JsonNum(q.g_game_phase)
            << ",\"g_garbage_time\":" << JsonNum(q.g_garbage_time) << ",\"g_clutch\":" << JsonNum(q.g_clutch)
            << ",\"g_goal_freshness\":" << JsonNum(q.g_goal_freshness)
            << ",\"g_net_momentum_5m\":" << JsonNum(q.g_net_momentum_5m)
            // inplay 赔率 + live_stats (sharp 锚 g_bm_inplay_fair)
            << ",\"g_bm_inplay_fair\":" << JsonNum(q.g_bm_inplay_fair)
            << ",\"g_danger_attack_diff\":" << JsonNum(q.g_danger_attack_diff)
            << ",\"g_shot_on_target_diff\":" << JsonNum(q.g_shot_on_target_diff)
            << ",\"g_possession_home\":" << JsonNum(q.g_possession_home)
            << ",\"g_red_card_diff\":" << JsonNum(q.g_red_card_diff)
            << ",\"g_corner_diff\":" << JsonNum(q.g_corner_diff)
            // 市场生命周期
            << ",\"time_to_resolution_frac\":" << JsonNum(q.time_to_resolution_frac)
            << ",\"resolution_status\":" << static_cast<int>(q.resolution_status)
            << ",\"event_ts_ns\":" << JsonNum(q.event_ts_ns)
            << ",\"data_source_ts_ns\":" << JsonNum(q.data_source_ts_ns)
            << ",\"ingestion_ts_ns\":" << JsonNum(q.ingestion_ts_ns)
            << ",\"as_of_ts_ns\":" << JsonNum(q.as_of_ts_ns) << "}\n";
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
