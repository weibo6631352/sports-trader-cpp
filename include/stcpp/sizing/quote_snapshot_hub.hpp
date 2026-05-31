// include/stcpp/sizing/quote_snapshot_hub.hpp
//
// v0.1 — per-market double-buffer QuoteFeatures 只读快照发布器
//
// Owner: 小石 (#41, data-structures-expert, G-LEDGER-OWNER)
// last_review: 2026-05-29
//
// 架构说明 — double-buffer 原子发布 (同构 orderbook_snapshot_hub, R-12 零反向依赖):
//   热路径写端 (策略线程 / SizingCalculator 后) 通过 Publish() 写 back-buffer, 完成后 atomic swap.
//   观测线程 (debug_api / 监控) 通过 Read(market_key) 读 front-buffer 的原子快照 (值拷贝).
//
//   内部结构: per-key 双槽 (slot[0] / slot[1]).
//     atomic<uint8_t>[key_idx]       — 读者可见的 front-slot (原子 load)
//
//   flip 序列:
//     写端:
//       1. back = 1 - front_atomic.load(relaxed)
//       2. 写入 buf_[key_idx][back]
//       3. front_atomic.store(back, release)    // 读者看到一致快照
//     读端:
//       1. idx = front_atomic.load(acquire)
//       2. 复制 buf_[key_idx][idx]              // 纯读, 无锁, 无阻塞
//
// R-12 保证:
//   - 写端: noexcept, 无 malloc (key 由写端在 cold path 预分配)
//   - 读端: 只做 atomic::load(acquire) + value copy, < 1us
//   - 无锁: 无 mutex/spinlock; 依靠 C++20 atomic release/acquire
//   - 零反向依赖: 不 #include debug_api / RM / signer / exec 热路径头
//
// R-20 时间戳:
//   QuoteFeatures 包含完整 4-ts (event / data_source / ingestion / as_of)
//   所有 ts 来自上游链路, 禁本地 now() 替代
//
// 使用方式:
//   热路径写端 (策略线程):
//     hub_.Publish(market_key, features);   // double-buffer swap
//
//   观测线程 (debug_api state_provider impl):
//     auto snap = hub_.Read(market_key);    // 无锁原子读 → optional<QuoteFeatures>
//     if (snap) { /* 映射到 QuoteParams */ }
//
//   不要在本文件里 #include RM / signer / exec / debug_api.
//
// debug_api 映射说明 (见文件末尾注释块):
//   QuoteFeatures → debug_api::QuoteParams (per /api/v1/quote endpoint)
//
// ============================================================================

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace stcpp::sizing {

// ---------------------------------------------------------------------------
// ModelKindTag — AI provenance (不引入 ml/hook.hpp 保持零反向依赖)
// ---------------------------------------------------------------------------
enum class ModelKindTag : std::uint8_t {
    kStub = 0,      // 无 ML 模型 / 纯量化
    kOnnx = 1,      // ONNX 推理
    kTreelite = 2,  // Treelite 推理
};

// ---------------------------------------------------------------------------
// QuoteFeatures — per-market 量化报价快照 POD (double-buffer 单元)
//
// 设计原则:
//   - 纯 POD (trivially copyable) → 观测侧可安全 memcpy 或 value copy
//   - 不含 std::string (model_id / spec_version 用固定 char 数组)
//   - 4-ts 字段全部来自上游链路 (R-20)
//   - 完整对应 debug_api::QuoteParams (含 ML provenance 字段)
// ---------------------------------------------------------------------------
struct QuoteFeatures {
    // ---- R-20: 4 时间戳 (严格透传上游; 禁本地 now() 替代) ----
    std::int64_t event_ts_ns{0};
    std::int64_t data_source_ts_ns{0};
    std::int64_t ingestion_ts_ns{0};
    std::int64_t as_of_ts_ns{0};  // 快照发布时刻 (策略线程填入)

    // ---- 核心量化字段 (对应 QuoteParams 主字段) ----
    // fair_value: de-vig fair prob ∈ (0, 1) (来自 FairValueEstimator)
    double fair_value{0.0};
    // market_mid: book microprice (best_bid + best_ask) / 2 附近
    double market_mid{0.0};
    // edge_bps: net edge = |fair_value - market_mid| × 10000 (bps)
    double edge_bps{0.0};
    // fee_rate_coef: per-market 手续费系数 (gamma feeSchedule.rate; fee=rate×p×(1-p))。
    //   R-fee-2 (老雷): fee 吃净 edge 且 per-market 变化, 进 ML 训练特征 + 推理 (feature #32)。
    //   体育0.03/加密0.072/老市场0; 默认 0.03 (调用方未填则保守)。
    double fee_rate_coef{0.03};

    // ---- 盘口上下文 / 双边微观结构 (2026-05-31 盘口上下文会) ----
    //   全部「模型输入 + 观测」, 绝不接任何 gate (老板 2026-05-31: 新鲜度/信号质量是输入不是草率守门)。
    char condition_id[72]{};            // 自身主键 (bytes32 hex 66 char; 冗余进值 → ML/序列化自包含)
    char event_id[64]{};                // 树上行: 父 event (同场比赛多盘口共享; ML 按 event join 兄弟盘口)
    char neg_risk_market_id[72]{};      // 树上行: negRisk 互斥组父合约 (可空; neg_risk 一致性信号锚)
    double no_microprice{0.0};          // NO 边 microprice (de-vig 对边输入, 原当帧丢弃 → 回收)
    double cross_spread{0.0};           // = YES_ask + NO_ask − 1 (等效 vig; 流动性/定价健康度信号)
    double yes_imbalance{0.0};          // YES L1 簿口失衡 ∈ [−1,1]
    double no_imbalance{0.0};           // NO  L1 簿口失衡 ∈ [−1,1]
    bool devig_ok{false};               // de-vig 成功? (观测: 区分「de-vig 失败」vs「edge 不足」)
    std::int64_t joint_as_of_ts_ns{0};  // 联合新鲜度 = min(score.as_of, book.as_of); 模型输入+观测, 绝不 gate

    // ---- 当前持仓 (老板 2026-05-31: 持仓入模型; 库存感知 — 目标仓位范式控制器需知现仓才能定调整) ----
    double pos_net_qty{0.0};                  // 本盘口净持仓 (signed; ledger size_usdc/1e6, 正=多 YES)
    double pos_avg_entry{0.0};                // 加权平均入场价
    double pos_condition_exposure_usdc{0.0};  // 本 condition 累计敞口 (whole pUSD)
    // kelly_fraction: Kelly 仓位比例 (已 cap; 来自 SizingCalculator)
    double kelly_fraction{0.0};
    // suggested_notional: 建议名义仓位 (USDC; 来自 SizingCalculator)
    double suggested_notional{0.0};
    // signal_strength: α 信号强度 ∈ [0, 1]
    double signal_strength{0.0};

    // ---- 目标仓位范式 (老雷 controller spec v1, 2026-05-31; 观测 + 训练, 不守门) ----
    //   控制器把当前仓位连续调到 target; reservation 是限价不追的净 edge=0 临界价。
    //   全部加性字段 (§8.1 carve-out #5: struct 末尾增, 无重命名/单位变更; 消费方忽略即旧行为)。
    double target_signed_notional{0.0};  // 目标净仓位 (signed pUSD; +多YES −多NO=空YES; 控制器目标)
    double reservation_buy_px{0.0};      // 买入保留价上界 (fair − fee − margin; best_ask≤它才买)
    double reservation_sell_px{0.0};     // 卖出保留价下界 (fair + fee + margin; best_bid≥它才卖)
    double required_margin{0.0};         // reservation 安全边际 (小梁 Q-梁-1; CI 半宽与 floor 取大)

    // ---- ML provenance (小邓 spec v1 §3.2; 对应 QuoteParams ML 字段) ----
    // model_id: char 数组 (空 = 无模型; 对应 ModelPrediction.model_id)
    char model_id[64]{};  // 最长 model_id ~48 char; 留余量
    // spec_version: FeatureVector.spec_version
    char spec_version[32]{};  // e.g. "v1.2"; 留余量
    // model_kind: stub / onnx / treelite
    ModelKindTag model_kind{ModelKindTag::kStub};
    // model_confidence: 校准后置信度 ∈ [0, 1] (取代 model_conf)
    double model_confidence{0.0};
    // fair_ci_lower / fair_ci_upper: fair prob 置信区间
    double fair_ci_lower{0.0};
    double fair_ci_upper{0.0};
    // model_as_of_ts_ns: feature PIT 锚 (≤ as_of_ts_ns; 非快照读取时刻)
    std::int64_t model_as_of_ts_ns{0};
    // predict_ok: 推理成功标记 (false → 灰显 fair)
    bool predict_ok{false};
    // advisory: ML-R2, paper 期恒 true
    bool advisory{true};
    // model_calibrated: confidence 是否已校准
    bool model_calibrated{false};

    // ---- 有效性标记 (首次 Publish 后为 true) ----
    bool valid{false};

    // ---- 快速访问 ----
    [[nodiscard]] bool has_edge() const noexcept { return edge_bps > 0.0; }
    [[nodiscard]] bool has_model() const noexcept { return model_id[0] != '\0'; }

    // R-20: 4-ts 单调链验证
    [[nodiscard]] bool ts_chain_ok() const noexcept {
        return event_ts_ns > 0 && data_source_ts_ns >= event_ts_ns && ingestion_ts_ns >= data_source_ts_ns &&
               as_of_ts_ns >= ingestion_ts_ns;
    }
};

static_assert(std::is_trivially_copyable_v<QuoteFeatures>,
              "QuoteFeatures must be trivially copyable for lock-free double-buffer");

// ---------------------------------------------------------------------------
// QuoteSnapshotHub — per-market double-buffer 快照发布/读取
//
// 线程安全: 单写多读 (SWMR)
//   - 写端 (策略线程, single writer): Publish()
//   - 读端 (任意线程, debug_api 等观测线程): Read() — 原子 acquire + value copy
//
// 容量: max_keys 在构造时预分配, hot path 不 grow
// key 语义: condition_id (一个盘口一个 QuoteFeatures)
// ---------------------------------------------------------------------------
class QuoteSnapshotHub {
public:
    static constexpr std::size_t kDefaultMaxKeys = 512;

    explicit QuoteSnapshotHub(std::size_t max_keys = kDefaultMaxKeys);

    // Delete copy/move (状态机, 不允许拷贝)
    QuoteSnapshotHub(const QuoteSnapshotHub&) = delete;
    QuoteSnapshotHub& operator=(const QuoteSnapshotHub&) = delete;
    QuoteSnapshotHub(QuoteSnapshotHub&&) = delete;
    QuoteSnapshotHub& operator=(QuoteSnapshotHub&&) = delete;

    ~QuoteSnapshotHub() = default;

    // -----------------------------------------------------------------------
    // Publish — 写端 hot path (无锁, noexcept)
    //
    //   1. 找/分配 key 的内部 index (首次见到 market_key 时 cold-path 分配)
    //   2. 写入 back-buffer slot
    //   3. atomic store(release) 令读端可见
    //
    //   market_key: caller 保证生命周期 >= 本次调用 (string_view)
    //   features:   值拷贝进 back-buffer (trivially copyable POD)
    //
    // R-12: key_idx 已分配 → O(1) unordered_map lookup + atomic swap
    //        首次分配 (cold path) 可 malloc; hot path 后续无 malloc
    // -----------------------------------------------------------------------
    void Publish(std::string_view market_key, const QuoteFeatures& features) noexcept;

    // -----------------------------------------------------------------------
    // Read — 观测线程只读快照 (无锁, 线程安全)
    //
    //   返回 std::optional<QuoteFeatures>:
    //     - nullopt: market_key 未曾 Publish 过 (或 max_keys 超限)
    //     - value:   最新原子快照 (可能含 valid=false 的初始状态 — 调用方检查)
    //
    //   时延: atomic load + trivially copyable copy ≈ < 500ns (远低于 100us R-12 限制)
    // -----------------------------------------------------------------------
    [[nodiscard]] std::optional<QuoteFeatures> Read(std::string_view market_key) const noexcept;

    // -----------------------------------------------------------------------
    // ReadRaw — 零拷贝指针 (zero-copy, caller 必须在 front 有效期内使用)
    //   仅供高频内部使用; 一般消费方用 Read() 值语义
    // -----------------------------------------------------------------------
    [[nodiscard]] const QuoteFeatures* ReadRaw(std::string_view market_key) const noexcept;

    // -----------------------------------------------------------------------
    // ResetKey — 清除单 key 状态
    // -----------------------------------------------------------------------
    void ResetKey(std::string_view market_key) noexcept;

    // -----------------------------------------------------------------------
    // ResetAll — 清除全部 key 状态 (系统重启)
    // -----------------------------------------------------------------------
    void ResetAll() noexcept;

    // -----------------------------------------------------------------------
    // key_count — 当前已注册 key 数量 (供监控)
    // -----------------------------------------------------------------------
    [[nodiscard]] std::size_t key_count() const noexcept;

    // -----------------------------------------------------------------------
    // publish_count — 累计 Publish 调用次数 (供监控)
    // -----------------------------------------------------------------------
    [[nodiscard]] std::uint64_t publish_count() const noexcept { return publish_count_; }

private:
    // -----------------------------------------------------------------------
    // KeySlot — per-key double-buffer 存储
    //
    // buf_[0] / buf_[1]: 两个 QuoteFeatures 槽
    // front_: 当前读者可见的槽 index (0 or 1), atomic<uint8_t>
    //   - store: release (writer)
    //   - load:  acquire (reader)
    // -----------------------------------------------------------------------
    struct KeySlot {
        std::array<QuoteFeatures, 2> buf{};
        std::atomic<std::uint8_t> front{0};  // 0 or 1

        KeySlot() noexcept = default;
        KeySlot(KeySlot&&) = delete;
        KeySlot& operator=(KeySlot&&) = delete;
        KeySlot(const KeySlot&) = delete;
        KeySlot& operator=(const KeySlot&) = delete;
    };

    // market_key (string) → KeySlot index into slots_
    std::unordered_map<std::string, std::size_t> index_map_;

    // slots_ 用 unique_ptr<KeySlot[]> 避免 KeySlot 不可拷贝带来的 vector 问题
    std::unique_ptr<KeySlot[]> slots_;
    std::size_t max_keys_;
    std::size_t slot_count_{0};  // 写端 only

    std::uint64_t publish_count_{0};  // 写端 only

    static constexpr std::size_t kInvalidIdx = std::numeric_limits<std::size_t>::max();
    [[nodiscard]] std::size_t GetOrAllocSlot(std::string_view market_key) noexcept;
    [[nodiscard]] std::size_t FindSlot(std::string_view market_key) const noexcept;
};

// ---------------------------------------------------------------------------
// 映射说明 (QuoteFeatures → debug_api::QuoteParams; 集成步骤后续独立做, 不改 debug_api)
//
// 集成时 RealQuoteStateProvider::quote_params(condition_id) 需完成以下映射:
//   features.valid                 → QuoteParams::found
//   market_key (外部 key)          → QuoteParams::market_id
//   features.fair_value            → QuoteParams::fair_value
//   features.market_mid            → QuoteParams::market_mid
//   features.edge_bps              → QuoteParams::edge_bps
//   features.kelly_fraction        → QuoteParams::kelly_fraction
//   features.suggested_notional    → QuoteParams::suggested_notional
//   features.signal_strength       → QuoteParams::signal_strength
//   features.model_confidence      → QuoteParams::model_conf (deprecated alias)
//                                 + QuoteParams::model_confidence
//   features.model_id (char[])     → QuoteParams::model_id (string)
//   features.spec_version (char[]) → QuoteParams::spec_version (string)
//   ModelKindTag::kStub  → "stub" / kOnnx → "onnx" / kTreelite → "treelite"
//                                  → QuoteParams::model_kind
//   features.model_calibrated      → QuoteParams::model_calibrated
//   features.fair_ci_lower         → QuoteParams::fair_ci_lower
//   features.fair_ci_upper         → QuoteParams::fair_ci_upper
//   features.predict_ok            → QuoteParams::predict_ok
//   features.model_as_of_ts_ns     → QuoteParams::model_as_of_ts_ns
//   features.advisory              → QuoteParams::advisory
//   features.as_of_ts_ns           → QuoteParams::as_of_ts_ns
// ---------------------------------------------------------------------------

}  // namespace stcpp::sizing
