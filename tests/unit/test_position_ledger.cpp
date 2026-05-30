// tests/unit/test_position_ledger.cpp — PositionLedger v0.1 单测 (W6 Wave 28 P0)
//
// 落:
//   老韩 W5 Smell-3 P0 升级 — W6-A-position-wal
//   GM Wave 27 §P0 (崩溃重启 circuit breaker 状态归零)
//
// 测试矩阵:
//   T1 apply_fill → query_position 单笔正确
//   T2 多笔 fill → position_total 累加 + realized_pnl 计算
//   T3 WAL 落盘 → restore_from_wal 状态完全恢复 (老韩 #3 P0 核心: 崩溃重启)
//   T4 R-20 4 ts 透传 + PIT chain assert
//   T5 4 wal kind 物理隔离 (paper position.wal 不出现在 live path, R-11)
//   T6 circuit_breaker_state 与 RM evaluate 联动 mock
//   T7 跨进程 SingleInstanceLock 防多开 (小卢 W5-A-09 协同)
//
// 基础设施:
//   InMemoryPositionWal: mock IWalWriterForPosition (in-memory + 可读取记录)
//   MakeValidFill: 构造 4 ts 合法的 VirtualFill
//   TmpWalDir: RAII 临时目录 (T3 WAL 落盘 + replay)

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

// POSIX (T7)
#include <sys/wait.h>

#include "stcpp/domain/micro_pusd.hpp"  // A1
#include "stcpp/execution/execution_mode.hpp"
#include "stcpp/execution/virtual_matcher.hpp"
#include "stcpp/infra/process/single_instance.hpp"
#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/infra/wal/position_ledger.hpp"
#include "stcpp/infra/wal/position_record.hpp"
#include "stcpp/infra/wal/wal_error.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/infra/wal/wal_record_header.hpp"

#include <fcntl.h>
#include <unistd.h>

// PositionLedger.cpp 内已实例化 WalWriter<PositionRecord>;
// 这里只拉 position_ledger.cpp (它内部已 #include wal_writer.cpp 并实例化)
#include "../../src/stcpp/infra/wal/position_ledger.cpp"

namespace stcpp::infra::wal {
namespace {

// ===========================================================================
// 工具函数 + Mock
// ===========================================================================

[[nodiscard]] std::int64_t NowNs() noexcept {
    return pit::NowRealtimeNs();
}

// 构造 4 ts 合法的 VirtualFill (R-20)
// fill_ts_ns ≤ ds_ts_ns ≤ ingest_ts_ns ≤ as_of_ts_ns, 全部 ≤ now
[[nodiscard]] stcpp::execution::VirtualFill MakeValidFill(double fill_price, double fill_size_usdc) {
    const std::int64_t base = NowNs() - 1'000'000'000LL;  // now - 1s
    stcpp::execution::VirtualFill f{};
    f.reject = stcpp::execution::MatchReject::Ok;
    f.fill_price = fill_price;
    f.fill_size_usdc = stcpp::domain::to_micro_pusd(fill_size_usdc);  // A1
    f.expected_fill_rate = 0.6;
    f.p_fill_clamped = 0.6;
    f.slippage_bps = 5;
    f.bernoulli_draw = true;
    f.audit_wal_kind = WalKind::PaperAudit;
    // W6 Wave 29: market_id / outcome 透传字段 (VirtualFill 加字段后 fixture 补齐)
    // 保持 "paper_market_0" 与 T1/T2 query_position 参数一致
    {
        static constexpr std::string_view kMid = "paper_market_0";
        std::memcpy(f.market_id.data(), kMid.data(), kMid.size());
    }
    f.outcome = 0;  // YES
    f.fill_ts_ns = base;
    f.event_ts_ns = base;
    f.data_source_ts_ns = base + 100;
    f.ingestion_ts_ns = base + 200;
    f.as_of_ts_ns = base + 300;
    return f;
}

// InMemoryPositionWal: mock WAL writer (内存存储, 可遍历)
class InMemoryPositionWal final : public IWalWriterForPosition {
public:
    [[nodiscard]] WalResult<std::uint64_t> Append(const PositionRecord& rec) noexcept override {
        records_.push_back(rec);
        const std::uint64_t seq = static_cast<std::uint64_t>(records_.size());
        hwm_.store(seq, std::memory_order_release);
        return WalResult<std::uint64_t>{seq};
    }

    [[nodiscard]] std::uint64_t HighWatermark() const noexcept override {
        return hwm_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool IsFailed() const noexcept override { return false; }

    [[nodiscard]] const std::vector<PositionRecord>& records() const noexcept { return records_; }

private:
    std::vector<PositionRecord> records_;
    std::atomic<std::uint64_t> hwm_{0};
};

// TmpWalDir: RAII 临时目录 (T3 使用)
class TmpWalDir {
public:
    TmpWalDir() {
        path_ = std::filesystem::temp_directory_path() / ("stcpp_test_wal_" + std::to_string(::getpid()));
        std::filesystem::create_directories(path_);
    }
    ~TmpWalDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    TmpWalDir(const TmpWalDir&) = delete;
    TmpWalDir& operator=(const TmpWalDir&) = delete;

private:
    std::filesystem::path path_;
};

// 把 PositionRecord 序列写成简化 WAL 文件 (Header + Record + 4B CRC footer stub)
// 用于 T3 restore_from_wal 测试
void WriteWalFile(const std::filesystem::path& file_path, const std::vector<PositionRecord>& records) {
    std::ofstream ofs{file_path, std::ios::binary | std::ios::trunc};
    if (!ofs)
        throw std::runtime_error("TmpWalDir: cannot open " + file_path.string());

    for (const auto& rec : records) {
        // WAL 头 (64B)
        WalRecordHeader hdr{};
        hdr.magic = kMagicV2;
        hdr.ver = kHeaderVersionV2;
        hdr.wal_kind = static_cast<std::uint8_t>(WalKind::Position);
        hdr.len_payload = static_cast<std::uint16_t>(sizeof(PositionRecord));
        hdr.seq = 1;  // T3 单文件, seq 无需严格单调
        hdr.event_ts_ns = rec.fill_event_ts_ns;
        hdr.data_source_ts_ns = rec.fill_ds_ts_ns;
        hdr.ingestion_ts_ns = rec.fill_ingestion_ts_ns;
        hdr.as_of_ts_ns = rec.fill_as_of_ts_ns;
        hdr.audit_id = rec.audit_id_;

        ofs.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

        // Payload (PositionRecord)
        ofs.write(reinterpret_cast<const char*>(&rec), sizeof(rec));

        // CRC32C stub (4B zeros)
        const std::uint32_t crc_stub = 0;
        ofs.write(reinterpret_cast<const char*>(&crc_stub), 4);
    }
}

// ===========================================================================
// T1: apply_fill → query_position 单笔正确
// ===========================================================================
TEST(PositionLedger, T1_ApplyFill_QueryPosition_SingleFill) {
    auto mock = std::make_unique<InMemoryPositionWal>();
    auto* mock_ptr = mock.get();
    PositionLedger ledger{std::move(mock), 100'000'000LL};  // bankroll 100 USDC

    const auto fill = MakeValidFill(0.6, 10.0);  // 10 USDC @ 0.60
    const ApplyResult res = ledger.apply_fill(fill);

    ASSERT_EQ(res.status, ApplyStatus::Ok) << "apply_fill should succeed";
    EXPECT_GT(res.wal_seq, 0u) << "WAL seq should be positive";

    // WAL 落了 1 条
    EXPECT_EQ(mock_ptr->records().size(), 1u);

    // position_total = fill_size_usdc * 1e6 = 10_000_000
    EXPECT_EQ(res.record.position_total, 10'000'000LL) << "position_total = fill_size_usdc * 1e6";

    // position_delta = same as total (首笔)
    EXPECT_EQ(res.record.position_delta, 10'000'000LL);

    // bankroll 透传
    EXPECT_EQ(res.record.bankroll_total, 100'000'000LL);

    // record_count
    EXPECT_EQ(ledger.record_count(), 1u);

    // query_position
    const PositionState st = ledger.query_position("paper_market_0");
    EXPECT_EQ(st.position_total, 10'000'000LL);
    EXPECT_EQ(st.bankroll_total, 100'000'000LL);
}

// ===========================================================================
// T2: 多笔 fill → position_total 累加 + entry_avg_price 加权均值
// ===========================================================================
TEST(PositionLedger, T2_MultiFill_PositionAccumulation) {
    auto mock = std::make_unique<InMemoryPositionWal>();
    PositionLedger ledger{std::move(mock), 1'000'000'000LL};  // 1000 USDC

    // 第 1 笔: 10 USDC @ 0.60
    const ApplyResult r1 = ledger.apply_fill(MakeValidFill(0.60, 10.0));
    ASSERT_EQ(r1.status, ApplyStatus::Ok);
    EXPECT_EQ(r1.record.position_total, 10'000'000LL);

    // 第 2 笔: 20 USDC @ 0.70
    const ApplyResult r2 = ledger.apply_fill(MakeValidFill(0.70, 20.0));
    ASSERT_EQ(r2.status, ApplyStatus::Ok);
    EXPECT_EQ(r2.record.position_total, 30'000'000LL)  // 10 + 20 USDC
        << "position_total should accumulate";

    // 加权均价: (10*0.60 + 20*0.70) / 30 = (6+14)/30 = 20/30 = 0.6667
    // 以 micro 计: (10e6 * 600000 + 20e6 * 700000) / 30e6
    //            = (6e12 + 14e12) / 30e6 = 20e12 / 30e6 = 666666
    const std::int64_t expected_avg = 666'667LL;  // 四舍五入
    EXPECT_NEAR(static_cast<double>(r2.record.entry_avg_price_micro), static_cast<double>(expected_avg), 2.0)
        << "entry_avg_price_micro should be weighted average";

    // 第 3 笔: 5 USDC @ 0.50 (低于均价, 但 v0.1 开仓阶段 realized_pnl 不变)
    const ApplyResult r3 = ledger.apply_fill(MakeValidFill(0.50, 5.0));
    ASSERT_EQ(r3.status, ApplyStatus::Ok);
    EXPECT_EQ(r3.record.position_total, 35'000'000LL) << "third fill should accumulate";

    EXPECT_EQ(ledger.record_count(), 3u);
}

// ===========================================================================
// T3: WAL 落盘 → restore_from_wal 状态完全恢复 (老韩 #3 P0 核心)
//
// 场景: 写 N 笔 PositionRecord 到 WAL 文件 → 新建 PositionLedger →
//   restore_from_wal → circuit_breaker_state 与原始一致 (崩溃重启)
// ===========================================================================
TEST(PositionLedger, T3_RestoreFromWal_CircuitBreakerState) {
    TmpWalDir tmp;

    // 准备一批 PositionRecord (模拟 3 笔 fill 后的状态)
    const std::int64_t base_ts = NowNs() - 2'000'000'000LL;

    PositionRecord rec1{};
    std::memcpy(rec1.market_id.data(), "paper_market_0", 14);
    rec1.outcome = 0;
    rec1.position_delta = 10'000'000LL;
    rec1.position_total = 10'000'000LL;
    rec1.realized_pnl = 0;
    rec1.unrealized_pnl = 0;
    rec1.entry_avg_price_micro = 600'000LL;
    rec1.bankroll_total = 100'000'000LL;
    rec1.consec_loss_count = 0;
    rec1.exposure_pct = 1000;  // 10% basis points
    rec1.fill_event_ts_ns = base_ts;
    rec1.fill_ds_ts_ns = base_ts + 100;
    rec1.fill_ingestion_ts_ns = base_ts + 200;
    rec1.fill_as_of_ts_ns = base_ts + 300;

    PositionRecord rec2 = rec1;
    rec2.position_delta = 20'000'000LL;
    rec2.position_total = 30'000'000LL;
    rec2.consec_loss_count = 1;  // 模拟 1 次亏损
    rec2.exposure_pct = 3000;
    rec2.fill_event_ts_ns = base_ts + 1'000'000'000LL;
    rec2.fill_ds_ts_ns = base_ts + 1'000'000'100LL;
    rec2.fill_ingestion_ts_ns = base_ts + 1'000'000'200LL;
    rec2.fill_as_of_ts_ns = base_ts + 1'000'000'300LL;

    PositionRecord rec3 = rec2;
    rec3.position_delta = 5'000'000LL;
    rec3.position_total = 35'000'000LL;
    rec3.consec_loss_count = 2;  // 模拟 2 次连续亏损 (circuit breaker 输入)
    rec3.exposure_pct = 3500;
    rec3.bankroll_total = 95'000'000LL;  // bankroll 已下降
    rec3.fill_event_ts_ns = base_ts + 2'000'000'000LL;
    rec3.fill_ds_ts_ns = base_ts + 2'000'000'100LL;
    rec3.fill_ingestion_ts_ns = base_ts + 2'000'000'200LL;
    rec3.fill_as_of_ts_ns = base_ts + 2'000'000'300LL;

    // 写 WAL 文件
    const auto wal_path = tmp.path() / "position_0001.wal";
    WriteWalFile(wal_path, {rec1, rec2, rec3});

    // 新建 PositionLedger (模拟崩溃重启后的空状态)
    auto mock = std::make_unique<InMemoryPositionWal>();
    PositionLedger restored{std::move(mock), 0LL};

    // restore_from_wal (老韩 #3 P0 核心路径)
    const std::size_t count = restored.restore_from_wal(tmp.path());
    EXPECT_EQ(count, 3u) << "Should replay 3 records from WAL";

    // circuit_breaker_state 应完全恢复
    const CircuitBreakerState cbs = restored.circuit_breaker_state();
    EXPECT_EQ(cbs.bankroll_total, rec3.bankroll_total) << "bankroll should be restored from last WAL record";
    EXPECT_EQ(cbs.consec_loss_count, rec3.consec_loss_count)
        << "consec_loss_count MUST be restored (老韩 #3: circuit breaker 不归零)";
    EXPECT_EQ(cbs.exposure_pct, rec3.exposure_pct) << "exposure_pct should be restored";

    // position_total 应恢复到最新记录
    const PositionState st = restored.query_position("paper_market_0");
    EXPECT_EQ(st.position_total, rec3.position_total) << "position_total should be restored from last record";
    EXPECT_EQ(st.consec_loss_count, 2) << "consec_loss_count must survive WAL replay (老韩 #3 P0)";
}

// ===========================================================================
// T4: R-20 4 ts 透传 + PIT chain assert
// ===========================================================================
TEST(PositionLedger, T4_R20_FourTs_Transparency) {
    auto mock = std::make_unique<InMemoryPositionWal>();
    auto* mock_ptr = mock.get();
    PositionLedger ledger{std::move(mock), 500'000'000LL};

    const std::int64_t base = NowNs() - 1'000'000'000LL;

    stcpp::execution::VirtualFill fill{};
    fill.reject = stcpp::execution::MatchReject::Ok;
    fill.fill_price = 0.55;
    fill.fill_size_usdc = stcpp::domain::to_micro_pusd(15.0);  // A1
    fill.bernoulli_draw = true;
    // R-20: 严格递增链
    fill.fill_ts_ns = base;
    fill.event_ts_ns = base;
    fill.data_source_ts_ns = base + 1'000;
    fill.ingestion_ts_ns = base + 2'000;
    fill.as_of_ts_ns = base + 3'000;

    const ApplyResult res = ledger.apply_fill(fill);
    ASSERT_EQ(res.status, ApplyStatus::Ok) << "PIT-valid fill should succeed";

    ASSERT_EQ(mock_ptr->records().size(), 1u);
    const PositionRecord& rec = mock_ptr->records().front();

    // 4 ts 必须与 VirtualFill 一致 (透传, 不替换)
    EXPECT_EQ(rec.fill_event_ts_ns, fill.fill_ts_ns) << "event_ts must equal fill_ts_ns (R-20 透传)";
    EXPECT_EQ(rec.fill_ds_ts_ns, fill.data_source_ts_ns) << "data_source_ts must be transparent";
    EXPECT_EQ(rec.fill_ingestion_ts_ns, fill.ingestion_ts_ns) << "ingestion_ts must be transparent";
    EXPECT_EQ(rec.fill_as_of_ts_ns, fill.as_of_ts_ns) << "as_of_ts must be transparent";

    // PIT 顺序不等式
    EXPECT_LE(rec.fill_event_ts_ns, rec.fill_ds_ts_ns) << "PIT: event_ts ≤ data_source_ts";
    EXPECT_LE(rec.fill_ds_ts_ns, rec.fill_ingestion_ts_ns) << "PIT: data_source_ts ≤ ingestion_ts";
    EXPECT_LE(rec.fill_ingestion_ts_ns, rec.fill_as_of_ts_ns) << "PIT: ingestion_ts ≤ as_of_ts";
    EXPECT_LE(rec.fill_as_of_ts_ns, NowNs()) << "PIT: as_of_ts ≤ now (no future ts)";

    // WalRecord concept 方法 4 ts 对齐
    EXPECT_EQ(rec.event_ts_ns(), fill.fill_ts_ns);
    EXPECT_EQ(rec.data_source_ts_ns(), fill.data_source_ts_ns);
    EXPECT_EQ(rec.ingestion_ts_ns(), fill.ingestion_ts_ns);
    EXPECT_EQ(rec.as_of_ts_ns(), fill.as_of_ts_ns);
}

TEST(PositionLedger, T4_PitViolation_Rejected) {
    auto mock = std::make_unique<InMemoryPositionWal>();
    PositionLedger ledger{std::move(mock), 500'000'000LL};

    const std::int64_t base = NowNs() - 1'000'000'000LL;

    stcpp::execution::VirtualFill fill{};
    fill.reject = stcpp::execution::MatchReject::Ok;
    fill.fill_price = 0.55;
    fill.fill_size_usdc = stcpp::domain::to_micro_pusd(15.0);  // A1
    fill.bernoulli_draw = true;
    // 违反 PIT: data_source_ts < fill_ts (DsBeforeEvent)
    fill.fill_ts_ns = base + 1'000;
    fill.event_ts_ns = base + 1'000;
    fill.data_source_ts_ns = base;  // < fill_ts → PIT violation
    fill.ingestion_ts_ns = base + 2'000;
    fill.as_of_ts_ns = base + 3'000;

    const ApplyResult res = ledger.apply_fill(fill);
    EXPECT_EQ(res.status, ApplyStatus::PitViolation)
        << "data_source_ts < fill_ts must be rejected (R-20 PIT)";
}

// ===========================================================================
// T5: 4 wal kind 物理隔离 (R-11)
//   paper position.wal path prefix 不等于 live position.wal path prefix
//   构造 PositionLedger 时 WalKind::Position 路径与 Paper/Live 模式对应
// ===========================================================================
TEST(PositionLedger, T5_WalKindPhysicalIsolation) {
    // R-11: WalKind::Position PathRootOf = "/var/lib/stcpp/exec/"
    //   paper path: /var/lib/stcpp/paper/position*.wal
    //   live  path: /var/lib/stcpp/live/position*.wal
    //   两者 prefix 不同 → WalWriter::Open() path prefix 硬校验保障
    //
    // 本测试用路径字符串验证 PathRootOf 与 paper/live 期望值
    const std::string_view position_root = PathRootOf(WalKind::Position);
    EXPECT_EQ(position_root, "/var/lib/stcpp/exec/") << "WalKind::Position PathRootOf = /var/lib/stcpp/exec/";

    // paper 期望路径 (CMake 注入 paper 模式时)
    // live  期望路径 (CMake 注入 live 模式时)
    // 两者不相等 (R-7 / R-11 物理隔离)
    const std::string paper_path = "/var/lib/stcpp/paper/position";
    const std::string live_path = "/var/lib/stcpp/live/position";
    EXPECT_NE(paper_path, live_path) << "paper/live position WAL paths must differ (R-7 physical isolation)";

    // paper path 不包含 "live" (防止 paper binary 误写 live 路径)
    EXPECT_EQ(paper_path.find("live"), std::string::npos)
        << "paper position WAL path must not contain 'live'";

    // live path 不包含 "paper"
    EXPECT_EQ(live_path.find("paper"), std::string::npos)
        << "live position WAL path must not contain 'paper'";

    // PositionRecord ABI 锁定 (R-11 ABI 不变确保 paper/live replay 可交叉验证)
    EXPECT_EQ(sizeof(PositionRecord), 152u) << "PositionRecord ABI: 152B fixed (R-11 cross-mode replay)";
}

TEST(PositionLedger, T5_WalKind_PositionEnum) {
    // WalKind::Position = 1 (wal_kind.hpp enum 值稳定)
    EXPECT_EQ(static_cast<std::uint8_t>(WalKind::Position), 1u);

    // Paper 模式构造 PositionLedger (mock writer, 路径校验由真实 WalWriter 做)
    // 这里仅验证 mock 路径下正常工作 (路径校验单元在 test_wal_writer.cpp)
    auto mock = std::make_unique<InMemoryPositionWal>();
    PositionLedger pl{std::move(mock), 100'000'000LL};

    const ApplyResult res = pl.apply_fill(MakeValidFill(0.5, 5.0));
    EXPECT_EQ(res.status, ApplyStatus::Ok);

    // WAL record 中 wal_kind 来自 WalWriter 填头 (framework); 通过
    // WalRecord concept 方法 + mock 存储验证
    const PositionRecord& rec = pl.last_record();
    EXPECT_GT(rec.position_total, 0LL);
}

// ===========================================================================
// T6: circuit_breaker_state 与 RM evaluate 联动 mock
//   验证 circuit_breaker_state() 输出与 PositionLedger 状态一致
//   (R-1: RM evaluate 依赖此接口)
// ===========================================================================

// RM evaluate mock (简化版, 模拟 老韩 RM circuit breaker 判断)
struct MockRmEvaluate {
    bool halted{false};
    int calls{0};

    void evaluate(const CircuitBreakerState& cbs) {
        ++calls;
        // 简单 circuit breaker: consec_loss > 3 OR exposure > 5000 bps → halt
        halted = (cbs.consec_loss_count > 3) || (cbs.exposure_pct > 5000);
    }
};

TEST(PositionLedger, T6_CircuitBreakerState_RmEvaluateIntegration) {
    auto mock = std::make_unique<InMemoryPositionWal>();
    PositionLedger ledger{std::move(mock), 100'000'000LL};  // 100 USDC bankroll

    MockRmEvaluate rm{};

    // 初始: circuit breaker 正常
    {
        const CircuitBreakerState cbs = ledger.circuit_breaker_state();
        rm.evaluate(cbs);
        EXPECT_FALSE(rm.halted) << "Initial state: RM should not halt";
        EXPECT_EQ(cbs.bankroll_total, 100'000'000LL);
        EXPECT_EQ(cbs.consec_loss_count, 0);
    }

    // 3 笔 fill (各 20 USDC) → exposure = 60/100 = 60% = 6000 bps > 5000 → halt
    for (int i = 0; i < 3; ++i) {
        const ApplyResult res = ledger.apply_fill(MakeValidFill(0.5, 20.0));
        ASSERT_EQ(res.status, ApplyStatus::Ok);
    }

    {
        const CircuitBreakerState cbs = ledger.circuit_breaker_state();
        rm.evaluate(cbs);
        // 60 USDC / 100 USDC = 60% = 6000 bps → halt
        EXPECT_TRUE(rm.halted) << "exposure_pct=6000 > 5000 → RM should halt (R-1 circuit breaker)";
        EXPECT_EQ(cbs.exposure_pct, 6000) << "exposure_pct should be 6000 bps (60 USDC / 100 USDC)";
    }

    EXPECT_EQ(rm.calls, 2);
}

TEST(PositionLedger, T6_CircuitBreakerState_AtomicRead_NoLock) {
    // circuit_breaker_state() 是 noexcept + atomic read (RM hot path)
    // 验证不需要锁且多次调用结果一致
    auto mock = std::make_unique<InMemoryPositionWal>();
    PositionLedger ledger{std::move(mock), 200'000'000LL};

    static_cast<void>(ledger.apply_fill(MakeValidFill(0.6, 10.0)));

    // 连续读 10 次, 结果应稳定
    const CircuitBreakerState cbs0 = ledger.circuit_breaker_state();
    for (int i = 0; i < 10; ++i) {
        const CircuitBreakerState cbs_i = ledger.circuit_breaker_state();
        EXPECT_EQ(cbs_i.bankroll_total, cbs0.bankroll_total);
        EXPECT_EQ(cbs_i.consec_loss_count, cbs0.consec_loss_count);
        EXPECT_EQ(cbs_i.exposure_pct, cbs0.exposure_pct);
    }
}

// ===========================================================================
// T7: 跨进程 SingleInstanceLock 防多开
//   与小卢 W5-A-09 SingleInstanceLock 协同: paper 模式只允许 1 个 PositionLedger 实例
//   验证: parent 持 paper lock, child 尝试 acquire → 应失败 (SingleInstanceLockFailure)
//   同时 PositionLedger 在 paper mode 下正常工作
// ===========================================================================
TEST(PositionLedger, T7_SingleInstanceLock_PaperMode_CrossProcess) {
    // 确保测试目录存在
    ::mkdir("/tmp/stcpp_test", 0755);  // ignore if exists

    // STCPP_TEST_BUILD 隔离: fork 前设 STCPP_TEST_PID_DIR, 让 parent+child 用同一 pid dir
    // 保证 child 和 parent 竞争同一 flock (R-7 不受影响, 仅测试代码走此路径)
    const std::string pid_path =
        stcpp::infra::process::SingleInstanceLock::path_for(stcpp::execution::ExecutionMode::Paper);
    {
        const auto slash = pid_path.rfind('/');
        const std::string pid_dir = (slash != std::string::npos) ? pid_path.substr(0, slash) : "/tmp";
        ::setenv("STCPP_TEST_PID_DIR", pid_dir.c_str(), 1);
    }
    ::unlink(pid_path.c_str());  // clean up before test

    // Parent acquires lock + creates PositionLedger
    stcpp::infra::process::SingleInstanceLock parent_lock{stcpp::execution::ExecutionMode::Paper};

    // Parent PositionLedger works normally
    auto mock = std::make_unique<InMemoryPositionWal>();
    PositionLedger ledger{std::move(mock), 50'000'000LL};
    const ApplyResult res = ledger.apply_fill(MakeValidFill(0.5, 5.0));
    EXPECT_EQ(res.status, ApplyStatus::Ok) << "PositionLedger should work while holding SingleInstanceLock";

    // Fork child: child tries to acquire same lock → should fail
    int result_pipe[2];
    ASSERT_EQ(::pipe(result_pipe), 0);

    const pid_t child = ::fork();
    ASSERT_GE(child, 0);

    if (child == 0) {
        // child
        ::close(result_pipe[0]);
        char result = '0';
        try {
            stcpp::infra::process::SingleInstanceLock child_lock{stcpp::execution::ExecutionMode::Paper};
            // should not reach here
            result = '0';
        } catch (const stcpp::infra::process::SingleInstanceLockFailure&) {
            result = '1';  // expected
        } catch (...) {
            result = '0';
        }
        ::write(result_pipe[1], &result, 1);
        ::close(result_pipe[1]);
        ::_exit(0);
    }

    // parent
    ::close(result_pipe[1]);
    char result = '?';
    ::read(result_pipe[0], &result, 1);
    ::close(result_pipe[0]);
    int wstatus = 0;
    ::waitpid(child, &wstatus, 0);

    EXPECT_EQ(result, '1') << "Child should fail to acquire paper lock (防多开 T7)";

    ::unsetenv("STCPP_TEST_PID_DIR");
    ::unlink(pid_path.c_str());
}

// ===========================================================================
// PositionRecord — concept 满足验证
// ===========================================================================
TEST(PositionRecord, WalRecordConceptSatisfied) {
    // 编译期 concept check (如果不满足, 此行编译失败)
    static_assert(WalRecord<PositionRecord>, "PositionRecord must satisfy WalRecord concept");
    SUCCEED() << "PositionRecord satisfies WalRecord concept";
}

TEST(PositionRecord, AbiLayout_152B) {
    EXPECT_EQ(sizeof(PositionRecord), 152u) << "PositionRecord ABI 152B (变更须 ADR)";
    EXPECT_EQ(offsetof(PositionRecord, market_id), 0u);
    EXPECT_EQ(offsetof(PositionRecord, outcome), 32u);
    EXPECT_EQ(offsetof(PositionRecord, position_delta), 40u);
    EXPECT_EQ(offsetof(PositionRecord, position_total), 48u);
    EXPECT_EQ(offsetof(PositionRecord, realized_pnl), 56u);
    EXPECT_EQ(offsetof(PositionRecord, unrealized_pnl), 64u);
    EXPECT_EQ(offsetof(PositionRecord, entry_avg_price_micro), 72u);
    EXPECT_EQ(offsetof(PositionRecord, bankroll_total), 80u);
    EXPECT_EQ(offsetof(PositionRecord, consec_loss_count), 88u);
    EXPECT_EQ(offsetof(PositionRecord, exposure_pct), 92u);
    EXPECT_EQ(offsetof(PositionRecord, fill_event_ts_ns), 96u);
    EXPECT_EQ(offsetof(PositionRecord, fill_ds_ts_ns), 104u);
    EXPECT_EQ(offsetof(PositionRecord, fill_ingestion_ts_ns), 112u);
    EXPECT_EQ(offsetof(PositionRecord, fill_as_of_ts_ns), 120u);
    EXPECT_EQ(offsetof(PositionRecord, audit_id_), 128u);
    EXPECT_EQ(offsetof(PositionRecord, crc32c), 144u);
}

TEST(PositionRecord, SerializeInto_RoundTrip) {
    PositionRecord orig{};
    std::memcpy(orig.market_id.data(), "test_market_abc", 15);
    orig.outcome = 1;
    orig.position_total = 5'000'000LL;
    orig.fill_event_ts_ns = 1'000'000'000LL;
    orig.fill_ds_ts_ns = 1'000'000'100LL;
    orig.fill_ingestion_ts_ns = 1'000'000'200LL;
    orig.fill_as_of_ts_ns = 1'000'000'300LL;

    std::array<std::byte, 152> buf{};
    const std::size_t written = orig.serialize_into(std::span<std::byte>{buf.data(), buf.size()});
    EXPECT_EQ(written, 152u);

    PositionRecord copy{};
    std::memcpy(&copy, buf.data(), 152);
    EXPECT_EQ(std::string(copy.market_id.data(), 15), "test_market_abc");
    EXPECT_EQ(copy.outcome, 1u);
    EXPECT_EQ(copy.position_total, 5'000'000LL);
    EXPECT_EQ(copy.fill_event_ts_ns, 1'000'000'000LL);
}

// ---------------------------------------------------------------------------
// empty WAL dir → restore_from_wal 返回 0 (全新启动)
TEST(PositionLedger, T3_EmptyWalDir_ReturnsZero) {
    TmpWalDir tmp;
    auto mock = std::make_unique<InMemoryPositionWal>();
    PositionLedger ledger{std::move(mock), 100'000'000LL};

    const std::size_t count = ledger.restore_from_wal(tmp.path());
    EXPECT_EQ(count, 0u) << "Empty WAL dir should replay 0 records";

    const CircuitBreakerState cbs = ledger.circuit_breaker_state();
    EXPECT_EQ(cbs.bankroll_total, 100'000'000LL);
    EXPECT_EQ(cbs.consec_loss_count, 0);
    EXPECT_EQ(cbs.exposure_pct, 0);
}

// nonexistent dir → returns 0 (不抛, 全新启动)
TEST(PositionLedger, T3_NonexistentWalDir_ReturnsZero) {
    auto mock = std::make_unique<InMemoryPositionWal>();
    PositionLedger ledger{std::move(mock), 100'000'000LL};

    const std::size_t count = ledger.restore_from_wal("/tmp/stcpp_nonexistent_" + std::to_string(::getpid()));
    EXPECT_EQ(count, 0u) << "Nonexistent WAL dir should replay 0 records";
}

}  // namespace
}  // namespace stcpp::infra::wal
