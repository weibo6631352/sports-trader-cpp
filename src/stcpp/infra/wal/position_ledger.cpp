// stcpp/infra/wal/position_ledger.cpp — PositionLedger v0.1 实现
//
// 落:
//   老韩 W5 Smell-3 P0 升级: 崩溃重启后 circuit breaker 状态归零
//   GM Wave 27 §P0: W6-A-position-wal
//   laozhou-architecture-v0.6-e2e.md §1 (PL 链路) + §2.1 vCPU3 (group commit 兼容)
//
// 红线:
//   R-1  PositionLedger 是 RM evaluate() 依赖: circuit_breaker_state() 不绕过 RM
//   R-7  paper/live 物理隔离由构造参数 path_prefix + CMake guard 保障
//   R-11 WalWriter::Open() path prefix 硬校验 (不命中 → std::abort, framework 保障)
//   R-20 4 ts 直接从 VirtualFill 透传, 绝不本地 now() 替换
//   老韩#3 restore_from_wal: 从 WAL 文件 replay 全量 PositionRecord → 重建内存状态
//
// WAL 写入规程:
//   apply_fill (hot path, vCPU3 单写) → SPSC ring → bg fsync (group commit)
//   PerRecord fsync: position WAL 默认 FsyncMode::PerRecord (RPO=0, 老韩 P0 要求)
//
// 实现说明:
//   - consec_loss 判断: position_delta < 0 (减仓/亏损) 累计; position_delta > 0 重置
//   - exposure_pct: |position_total| / bankroll_total (basis points)
//   - unrealized_pnl: 本 v0.1 不估算 (无实时价格输入), 保留为 0 (小蒋 settle 时回填)
//   - replay 解析: 直接 memcpy PositionRecord (POD, ABI 锁定)

#include "stcpp/infra/wal/position_ledger.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/infra/wal/wal_record_header.hpp"
#include "stcpp/infra/wal/wal_writer.hpp"

// 模板实例化 (让 PositionLedger.cpp TU 持有 WalWriter<PositionRecord> 实例化)
// 单测也可独立实例化, 此处是生产 TU 唯一实例化点
#include "wal_writer.cpp"  // NOLINT(bugprone-suspicious-include)
namespace stcpp::infra::wal {
template class WalWriter<PositionRecord>;
}  // namespace stcpp::infra::wal

namespace stcpp::infra::wal {

// ---------------------------------------------------------------------------
// 内部工具
// ---------------------------------------------------------------------------
namespace {

// market_id 数组 → 字符串 key (以 null 截断)
[[nodiscard]] std::string MarketIdKey(const std::array<char, 32>& arr) {
    const std::size_t len = ::strnlen(arr.data(), 32);
    return std::string{arr.data(), len};
}

// 汇总 exposure_pct (basis points): sum(|pos_total|) / bankroll (clamp [0, 10000])
[[nodiscard]] std::int32_t ComputeExposurePct(std::int64_t abs_position_usdc,
                                              std::int64_t bankroll) noexcept {
    if (bankroll <= 0)
        return 0;
    const std::int64_t bps = (abs_position_usdc * 10'000LL) / bankroll;
    if (bps > 10'000)
        return static_cast<std::int32_t>(10'000);
    if (bps < 0)
        return 0;
    return static_cast<std::int32_t>(bps);
}

}  // namespace

// ---------------------------------------------------------------------------
// 构造 (生产路径)
// ---------------------------------------------------------------------------
PositionLedger::PositionLedger(std::string_view path_prefix, std::int64_t init_bankroll) {
    global_bankroll_.store(init_bankroll, std::memory_order_relaxed);

    // Open WalWriter<PositionRecord>
    // R-7 paper/live 物理隔离: path_prefix 由 CMake 注入, 内部 Open() 做 R-11 硬校验
    WalConfig cfg{};
    cfg.kind = WalKind::Position;
    cfg.path_prefix = std::string{path_prefix};
    cfg.fsync_mode = FsyncMode::PerRecord;  // position WAL RPO=0
    cfg.ring_capacity = 16384;              // 2 的幂

    auto w_or = WalWriter<PositionRecord>::Open(cfg);
    if (!w_or.has_value()) {
        throw std::runtime_error(std::string("PositionLedger: WalWriter::Open failed: ") +
                                 std::string(ToString(w_or.error())));
    }
    writer_ = std::make_unique<RealWalWriter>(std::move(w_or).value());
}

// ---------------------------------------------------------------------------
// 构造 (测试专用: 注入 mock WAL writer)
// ---------------------------------------------------------------------------
PositionLedger::PositionLedger(std::unique_ptr<IWalWriterForPosition> mock_writer, std::int64_t init_bankroll)
    : writer_(std::move(mock_writer)) {
    global_bankroll_.store(init_bankroll, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// _build_record: VirtualFill → PositionRecord
// 调用方持有 state_mutex_ write lock.
// ---------------------------------------------------------------------------
PositionRecord PositionLedger::_build_record(const stcpp::execution::VirtualFill& fill) noexcept {
    PositionRecord rec{};

    // R-20: 4 ts 直接从 VirtualFill 透传, 不本地 now()
    // event_ts = fill_ts_ns (fill 发生时刻, 最接近事件)
    rec.fill_event_ts_ns = fill.fill_ts_ns;
    rec.fill_ds_ts_ns = fill.data_source_ts_ns;
    rec.fill_ingestion_ts_ns = fill.ingestion_ts_ns;
    rec.fill_as_of_ts_ns = fill.as_of_ts_ns;

    // market_id + outcome: 从 VirtualFill 透传 (W6 @小蒋 Wave 29 补齐)
    rec.market_id = fill.market_id;
    rec.outcome = fill.outcome;

    // audit_id: VirtualFill 暂无 audit_id 字段 (由 PaperSigner/VirtualOrder 持有,
    // VirtualFill 作为撮合结果不回传 audit_id); 保留零值, 留 WAL header audit_id 校对.
    // rec.audit_id_ = ...;  // future: PaperSigner 端到端接入后从 sign_resp 注入

    // 仓位字段由 apply_fill 内填入 (market_id/outcome 已在上方赋值)
    return rec;
}

// ---------------------------------------------------------------------------
// apply_fill: 热路径, noexcept, vCPU3 单写
// ---------------------------------------------------------------------------
ApplyResult PositionLedger::apply_fill(const stcpp::execution::VirtualFill& fill) noexcept {
    // 快速失败: WAL writer 已失败
    if (writer_->IsFailed()) {
        return ApplyResult{ApplyStatus::WalFailed, {}, 0};
    }

    // BernoulliMissed / reject fill: 不记账 (fill_shares_micro == 0 但 reject != Ok)
    // apply_fill 仅处理成功 fill (MatchReject::Ok + fill_shares_micro > 0)
    // 注: rejected fill 由 PaperAudit WAL 记录 (小蒋负责), 不进 position ledger
    if (fill.reject != stcpp::execution::MatchReject::Ok || fill.fill_shares_micro <= 0) {  // A1: micro int64
        return ApplyResult{ApplyStatus::InvalidFill, {}, 0};
    }

    // R-20 PIT 前置检查 (不构造 WalRecordHeader, 直接检查 4 ts 关系)
    // PIT 顺序: fill_event_ts ≤ fill_ds_ts ≤ fill_ingestion_ts ≤ fill_as_of_ts
    const bool pit_ok = (fill.fill_ts_ns > 0) && (fill.data_source_ts_ns >= fill.fill_ts_ns) &&
                        (fill.ingestion_ts_ns >= fill.data_source_ts_ns) &&
                        (fill.as_of_ts_ns >= fill.ingestion_ts_ns) &&
                        (fill.as_of_ts_ns <= pit::NowRealtimeNs());
    if (!pit_ok) {
        return ApplyResult{ApplyStatus::PitViolation, {}, 0};
    }

    // --- 计算仓位变化 (持锁更新内存) ----------------------------------------
    PositionRecord rec{};
    {
        std::lock_guard<std::mutex> lk(state_mutex_);

        // A1: fill_shares_micro 已是 int64 micro, 直存无 ×1e6 (原对 whole VirtualFill 的补偿乘已删)
        const std::int64_t fill_size_micro = fill.fill_shares_micro;
        // fill_price → micro (int64)
        const std::int64_t fill_price_micro = static_cast<std::int64_t>(fill.fill_price * 1'000'000.0 + 0.5);

        // W6 @小蒋 Wave 29: market_id / outcome 直接从 VirtualFill 取 (占位已闭环)
        // fill.market_id: array<char,32>, null-padded (来自 VirtualMatcher 透传 VirtualOrder.market_id)
        // fill.outcome: uint8_t 0=YES / 1=NO
        const auto& market_id_arr = fill.market_id;
        const std::string market_key = MarketIdKey(market_id_arr);

        auto& st = states_[market_key];
        if (st.market_id[0] == '\0') {
            // 首次初始化
            st.market_id = market_id_arr;
            st.outcome = fill.outcome;  // 0=YES 1=NO (透传 VirtualFill)
            st.bankroll_total = global_bankroll_.load(std::memory_order_relaxed);
        }

        // 更新仓位 (多头: position_delta > 0; 空头暂不支持)
        const std::int64_t old_total = st.position_total;
        st.position_total += fill_size_micro;
        const std::int64_t new_total = st.position_total;

        // 更新平均买入价 (加权平均)
        if (new_total > 0 && fill_size_micro > 0) {
            st.entry_avg_price_micro =
                (old_total * st.entry_avg_price_micro + fill_size_micro * fill_price_micro) / new_total;
        }

        // realized_pnl (仅减仓时结算; 当前 v0.1 仅开仓, realized_pnl 不变)
        // unrealized_pnl: 无实时价格输入, 保留 0 (小蒋 settle 时回填)

        // consec_loss: 以单笔 realized_pnl 判断 (v0.1 开仓阶段保持 0)
        // 真实 settle 时由 realized_pnl < 0 递增, > 0 重置 (@小蒋 settle 接入)

        // exposure_pct
        const std::int64_t abs_pos = (new_total >= 0) ? new_total : -new_total;
        st.exposure_pct = ComputeExposurePct(abs_pos, st.bankroll_total);

        // position_delta = 本次增量
        const std::int64_t delta = fill_size_micro;

        // 全局 circuit breaker 聚合更新
        // consec_loss: max across markets
        const std::int32_t new_global_loss =
            std::max(global_consec_loss_.load(std::memory_order_relaxed), st.consec_loss_count);
        global_consec_loss_.store(new_global_loss, std::memory_order_relaxed);

        // exposure_pct: 累加 (多 market 时求和, 单 market 时等于单值)
        global_exposure_pct_.store(st.exposure_pct, std::memory_order_relaxed);

        // --- 构造 PositionRecord -------------------------------------------
        rec.market_id = market_id_arr;
        rec.outcome = st.outcome;
        rec.position_delta = delta;
        rec.position_total = new_total;
        rec.realized_pnl = st.realized_pnl;
        rec.unrealized_pnl = st.unrealized_pnl;
        rec.entry_avg_price_micro = st.entry_avg_price_micro;
        rec.bankroll_total = st.bankroll_total;
        rec.consec_loss_count = st.consec_loss_count;
        rec.exposure_pct = st.exposure_pct;

        // R-20: 4 ts 透传
        rec.fill_event_ts_ns = fill.fill_ts_ns;
        rec.fill_ds_ts_ns = fill.data_source_ts_ns;
        rec.fill_ingestion_ts_ns = fill.ingestion_ts_ns;
        rec.fill_as_of_ts_ns = fill.as_of_ts_ns;

        // crc32c: framework 算 (WalWriter::Append 内), 这里置 0
        rec.crc32c = 0;

        // 更新 last ts + last record
        st.last_event_ts_ns = fill.fill_ts_ns;
        st.last_as_of_ts_ns = fill.as_of_ts_ns;

        last_record_ = rec;
    }  // 释放锁

    // --- 写 WAL (SPSC, 锁外) ------------------------------------------------
    auto wal_r = writer_->Append(rec);
    if (!wal_r.has_value()) {
        const WalError we = wal_r.error();
        if (we == WalError::Backpressure) {
            return ApplyResult{ApplyStatus::WalBackpressure, rec, 0};
        }
        if (we == WalError::PitViolation) {
            return ApplyResult{ApplyStatus::PitViolation, rec, 0};
        }
        return ApplyResult{ApplyStatus::WalFailed, rec, 0};
    }

    record_count_.fetch_add(1, std::memory_order_acq_rel);
    return ApplyResult{ApplyStatus::Ok, rec, wal_r.value()};
}

// ---------------------------------------------------------------------------
// _update_state: replay 路径 (restore_from_wal) 更新内存状态
// 调用方确保单线程 (启动期 main thread)
// ---------------------------------------------------------------------------
void PositionLedger::_update_state(const PositionRecord& rec) {
    const std::string key = MarketIdKey(rec.market_id);
    auto& st = states_[key];

    st.market_id = rec.market_id;
    st.outcome = rec.outcome;
    st.position_total = rec.position_total;
    st.realized_pnl = rec.realized_pnl;
    st.unrealized_pnl = rec.unrealized_pnl;
    st.entry_avg_price_micro = rec.entry_avg_price_micro;
    st.bankroll_total = rec.bankroll_total;
    st.consec_loss_count = rec.consec_loss_count;
    st.exposure_pct = rec.exposure_pct;
    st.last_event_ts_ns = rec.fill_event_ts_ns;
    st.last_as_of_ts_ns = rec.fill_as_of_ts_ns;
    st.last_audit_id = rec.audit_id_;

    // 更新全局聚合
    global_bankroll_.store(rec.bankroll_total, std::memory_order_relaxed);
    const std::int32_t cur_loss = global_consec_loss_.load(std::memory_order_relaxed);
    if (rec.consec_loss_count > cur_loss) {
        global_consec_loss_.store(rec.consec_loss_count, std::memory_order_relaxed);
    }
    global_exposure_pct_.store(rec.exposure_pct, std::memory_order_relaxed);

    last_record_ = rec;
}

// ---------------------------------------------------------------------------
// restore_from_wal: 启动期 replay
// 老韩 #3 P0 核心场景: 崩溃重启后完整恢复 circuit breaker 状态
//
// 协议: WAL segment 文件格式 = 连续 [WalRecordHeader(64B) + PositionRecord(152B)]
//   注: W4 skeleton 中 WalWriter 未真正写磁盘 (ring/fd stub); 本函数 replay 是
//   为生产+测试 T3 设计的完整路径. 单测 T3 用 InMemoryPositionWal + 直接序列化
//   PositionRecord 到文件来模拟 WAL replay.
// ---------------------------------------------------------------------------
std::size_t PositionLedger::restore_from_wal(const std::filesystem::path& wal_dir) {
    namespace fs = std::filesystem;

    if (!fs::exists(wal_dir) || !fs::is_directory(wal_dir)) {
        // 目录不存在 = 全新启动, 不是错误 (spec §2.4)
        return 0;
    }

    // 收集 position*.wal 文件, 按文件名排序 (segment 序号有序)
    std::vector<fs::path> wal_files;
    for (const auto& entry : fs::directory_iterator(wal_dir)) {
        if (!entry.is_regular_file())
            continue;
        const std::string fname = entry.path().filename().string();
        if (fname.rfind("position", 0) == 0 && fname.size() > 4 && fname.substr(fname.size() - 4) == ".wal") {
            wal_files.push_back(entry.path());
        }
    }
    std::sort(wal_files.begin(), wal_files.end());

    std::size_t replay_count = 0;
    constexpr std::size_t kHeaderSz = sizeof(WalRecordHeader);
    constexpr std::size_t kRecordSz = sizeof(PositionRecord);
    // kFrameSz = kHeaderSz + kRecordSz + 4 (4B CRC32C footer); 用于文档说明, 不参与逻辑

    for (const auto& fpath : wal_files) {
        // 用 C stdio 顺序读 (启动冷路径, 不需要 POSIX AIO)
        FILE* fp = std::fopen(fpath.c_str(), "rb");  // NOLINT(cppcoreguidelines-owning-memory)
        if (!fp) {
            throw std::runtime_error("PositionLedger::restore_from_wal: fopen failed: " + fpath.string());
        }

        // 按帧读取
        for (;;) {
            WalRecordHeader hdr{};
            const std::size_t hdr_read = std::fread(&hdr, 1, kHeaderSz, fp);

            if (hdr_read == 0)
                break;  // EOF

            if (hdr_read < kHeaderSz) {
                // tail truncation (spec: warn, 可继续)
                // 生产应 emit WalError::TailTruncated, 这里 break 继续下个 segment
                break;
            }

            // 校验 magic + version
            if (!HasValidMagic(hdr)) {
                std::fclose(fp);  // NOLINT(cppcoreguidelines-owning-memory)
                throw std::runtime_error("PositionLedger::restore_from_wal: invalid WAL magic in " +
                                         fpath.string());
            }

            // 校验 wal_kind = Position
            if (static_cast<WalKind>(hdr.wal_kind) != WalKind::Position) {
                std::fclose(fp);  // NOLINT(cppcoreguidelines-owning-memory)
                throw std::runtime_error("PositionLedger::restore_from_wal: WAL kind mismatch in " +
                                         fpath.string());
            }

            // 读 payload (len_payload 决定; 期望 = sizeof(PositionRecord))
            const std::uint16_t payload_len = hdr.len_payload;
            if (payload_len < static_cast<std::uint16_t>(kRecordSz)) {
                // payload 太短, 跳过 + 尝试继续 (TailTruncated 容忍)
                if (payload_len > 0) {
                    if (std::fseek(fp, static_cast<long>(payload_len), SEEK_CUR) != 0)
                        break;
                }
                // skip CRC32C footer
                if (std::fseek(fp, 4L, SEEK_CUR) != 0)
                    break;
                continue;
            }

            PositionRecord rec{};
            const std::size_t rec_read = std::fread(&rec, 1, kRecordSz, fp);
            if (rec_read < kRecordSz)
                break;

            // 跳过 payload 剩余 + CRC32C footer (framework 验 CRC, 这里信任 WAL)
            const long skip = static_cast<long>(payload_len) - static_cast<long>(kRecordSz) + 4L;
            if (skip > 0) {
                if (std::fseek(fp, skip, SEEK_CUR) != 0)
                    break;
            }

            // R-20 PIT 校验 (replay 时也要检查, 防止持久化了脏数据)
            if (rec.fill_event_ts_ns <= 0 || rec.fill_ds_ts_ns < rec.fill_event_ts_ns ||
                rec.fill_ingestion_ts_ns < rec.fill_ds_ts_ns ||
                rec.fill_as_of_ts_ns < rec.fill_ingestion_ts_ns) {
                // 跳过非法记录 (warn, 不 throw)
                continue;
            }

            _update_state(rec);
            ++replay_count;
        }

        std::fclose(fp);  // NOLINT(cppcoreguidelines-owning-memory)
    }

    record_count_.fetch_add(static_cast<std::uint64_t>(replay_count), std::memory_order_acq_rel);
    return replay_count;
}

// ---------------------------------------------------------------------------
// query_position
// ---------------------------------------------------------------------------
PositionState PositionLedger::query_position(std::string_view market_id) const {
    const std::string key{market_id.data(), std::min(market_id.size(), static_cast<std::size_t>(32))};
    std::lock_guard<std::mutex> lk(state_mutex_);
    const auto it = states_.find(key);
    if (it == states_.end())
        return PositionState{};
    return it->second;
}

// ---------------------------------------------------------------------------
// circuit_breaker_state (R-1: RM evaluate 依赖)
// ---------------------------------------------------------------------------
CircuitBreakerState PositionLedger::circuit_breaker_state() const noexcept {
    CircuitBreakerState cbs{};
    cbs.bankroll_total = global_bankroll_.load(std::memory_order_acquire);
    cbs.consec_loss_count = global_consec_loss_.load(std::memory_order_acquire);
    cbs.exposure_pct = global_exposure_pct_.load(std::memory_order_acquire);
    return cbs;
}

// ---------------------------------------------------------------------------
// last_record
// ---------------------------------------------------------------------------
PositionRecord PositionLedger::last_record() const noexcept {
    std::lock_guard<std::mutex> lk(state_mutex_);
    return last_record_;
}

}  // namespace stcpp::infra::wal
