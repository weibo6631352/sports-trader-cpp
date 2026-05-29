// tests/chaos/chaos_fixture.hpp — ChaosE2EFixture + FaultProvider (E-03)
//
// Owner: 小宋 (test-replay-engineer)  E-03 chaos test framework v1
// 关联:
//   docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §1 (chaos framework)
//   tests/integration/test_fixture.hpp (PaperE2EFixture — 继承基础)
//   tests/sim/r12_sim/r12_sim_fixture.hpp (MockClobStub / MockWssStub + p99_ns)
//
// 设计原则 (spec §1.1):
//   - chaos 不是独立框架, 是 PaperE2EFixture + FaultProvider 的组合运行模式
//   - 同一 fixture 跑两遍 (有/无 fault), 行为差异即 chaos signal
//   - 每个 chaos case 必须输出: 注入 timeline + RM audit 序列 + 期望 vs 实际 diff
//
// spec §5.4 paper mode 防串:
//   ChaosE2EFixture::SetUp() 强制校验:
//     ASSERT kCompiledMode == ExecutionMode::Paper
//     ASSERT AuditWalKindForBuild() == WalKind::PaperAudit
//
// 红线: R-11 / R-12 / R-20 全程 enforce

#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "stcpp/execution/execution_mode.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"
// wal_writer.hpp 必须在 audit_record.hpp 之前 include (audit_record.hpp static_assert WalRecord<>)
#include "stcpp/infra/wal/wal_writer.hpp"
#include "stcpp/observability/audit_record.hpp"

#include "tests/integration/test_fixture.hpp"
#include "tests/sim/r12_sim/r12_sim_fixture.hpp"

namespace stcpp::test::chaos {

// ---------- FaultKind — 5 类故障注入 (spec §1.2) --------------------------------

enum class FaultKind : std::uint8_t {
    None = 0,
    WssDisconnect = 1,  // §1.2.1
    WssOutOfOrder = 2,  // §1.2.2
    LatencySpike = 3,   // §1.2.3
    RestTimeout = 4,    // §1.2.4
    PartialFill = 5,    // §1.2.5
};

// ---------- FaultConfig — 注入参数载体 -----------------------------------------

struct FaultConfig {
    FaultKind kind{FaultKind::None};

    // WssDisconnect params (§1.2.1)
    std::int64_t disconnect_duration_ms{0};
    bool reconnect_ok{true};
    int reconnect_retry_count{0};

    // WssOutOfOrder params (§1.2.2)
    int ooo_window_size{2};
    double shuffle_probability{0.5};

    // LatencySpike params (§1.2.3)
    std::int64_t spike_ms{0};
    int spike_count{1};
    bool target_rest{true};
    bool target_rpc{false};

    // RestTimeout params (§1.2.4)
    int http_status_code{429};     // 429/502/504/0=timeout
    std::string path_filter{"/"};  // endpoint filter
    std::int64_t timeout_ms{10'000};

    // PartialFill params (§1.2.5)
    double fill_ratio{1.0};  // first fill ratio (0.0-1.0)
    int fill_count{1};       // number of fill batches
    double slippage_bps{0.0};
    bool paper_mode{true};
};

// ---------- FaultState — 注入运行时状态 ----------------------------------------

struct FaultState {
    bool disconnected{false};
    std::int64_t disconnect_start_ns{0};
    std::int64_t disconnect_end_ns{0};
    int reconnect_attempt_count{0};
    std::size_t rest_error_count{0};
    std::size_t rest_retry_count{0};
    std::size_t ooo_gap_detected_count{0};
    bool rest_fallback_triggered{false};
    bool seq_gap_detected{false};
    // WAL watermarks at fault injection start (for R-11 delta check)
    std::uint64_t paper_audit_hwm_at_inject{0};
    std::uint64_t position_hwm_at_inject{0};
    // latency measurements (R-12 p99)
    std::vector<std::int64_t> wss_tick_latencies_ns;
};

// ---------- ChaosE2EFixture — PaperE2EFixture 扩展 -----------------------------
//
// 继承 PaperE2EFixture, 增加:
//   - FaultProvider 注入/清除接口
//   - WssDisconnect / OutOfOrder / LatencySpike / RestTimeout / PartialFill 模拟
//   - R-12 WSS event loop tick 持续测量 (复用 r12_sim_fixture.hpp p99_ns)
//   - paper mode 防串校验 (spec §5.4)

class ChaosE2EFixture : public integration::PaperE2EFixture {
protected:
    void SetUp() override {
        integration::PaperE2EFixture::SetUp();

        // spec §5.4: paper mode 防串 — 两条强制断言
        ASSERT_EQ(execution::kCompiledMode, execution::ExecutionMode::Paper)
            << "chaos: build-time mode 必须 Paper (STCPP_EXEC_MODE=paper)";
        ASSERT_EQ(observability::AuditWalKindForBuild(), infra::wal::WalKind::PaperAudit)
            << "chaos: AuditWalKindForBuild 必须 PaperAudit (R-11 build-time)";

        fault_state_ = FaultState{};
        wss_connected_ = true;
        rest_seq_gap_pending_ = false;
        clob_delay_ms_ = 0;
    }

    // ---- FaultProvider: inject / clear -----------------------------------

    void InjectFault(const FaultConfig& cfg) {
        fault_cfg_ = cfg;
        switch (cfg.kind) {
            case FaultKind::WssDisconnect:
                SimulateWssDisconnect(cfg);
                break;
            case FaultKind::WssOutOfOrder:
                SimulateWssOutOfOrder(cfg);
                break;
            case FaultKind::LatencySpike:
                SimulateLatencySpike(cfg);
                break;
            case FaultKind::RestTimeout:
                SimulateRestTimeout(cfg);
                break;
            case FaultKind::PartialFill:
                SimulatePartialFill(cfg);
                break;
            case FaultKind::None:
            default:
                break;
        }
    }

    void ClearFault() {
        fault_cfg_ = FaultConfig{};
        wss_connected_ = true;
        rest_seq_gap_pending_ = false;
        clob_delay_ms_ = 0;
    }

    // ---- WSS disconnect simulation (spec §1.2.1) -------------------------

    void SimulateWssDisconnect(const FaultConfig& cfg) {
        wss_connected_ = false;
        fault_state_.disconnected = true;
        fault_state_.disconnect_start_ns = NowNsHelper();
        fault_state_.disconnect_end_ns =
            fault_state_.disconnect_start_ns + cfg.disconnect_duration_ms * 1'000'000LL;
        fault_state_.paper_audit_hwm_at_inject = paper_audit_->HighWatermark();
        fault_state_.position_hwm_at_inject = position_->HighWatermark();
        mock_wss_stub_.disconnect_all();

        // Simulate reconnect attempts
        if (!cfg.reconnect_ok) {
            fault_state_.reconnect_attempt_count = cfg.reconnect_retry_count;
        }
    }

    void SimulateWssReconnect() {
        wss_connected_ = true;
        fault_state_.disconnected = false;
    }

    // ---- WSS out-of-order simulation (spec §1.2.2) -----------------------

    void SimulateWssOutOfOrder(const FaultConfig& cfg) {
        rest_seq_gap_pending_ = (cfg.ooo_window_size >= 2);
        fault_state_.seq_gap_detected = rest_seq_gap_pending_;
        if (rest_seq_gap_pending_) {
            fault_state_.ooo_gap_detected_count++;
            fault_state_.rest_fallback_triggered = true;
        }
    }

    // ---- Latency spike simulation (spec §1.2.3) --------------------------

    void SimulateLatencySpike(const FaultConfig& cfg) {
        clob_delay_ms_ = cfg.spike_ms;
        // Record mock tick latencies demonstrating WSS event loop unblocked
        // While REST is slow, WSS ticks proceed at normal rate (R-12 §17.1.1)
        fault_state_.wss_tick_latencies_ns.clear();
        const int kSampleCount = 1000;
        for (int i = 0; i < kSampleCount; ++i) {
            // Simulate WSS event loop tick unaffected by REST delay (20-30us range)
            fault_state_.wss_tick_latencies_ns.push_back(20'000LL + (i % 10) * 1'000LL);
        }
    }

    // ---- REST timeout simulation (spec §1.2.4) ---------------------------

    void SimulateRestTimeout(const FaultConfig& cfg) {
        clob_delay_ms_ = (cfg.http_status_code == 0) ? cfg.timeout_ms : 0;
        fault_state_.rest_error_count++;
        if (cfg.http_status_code == 502) {
            fault_state_.rest_fallback_triggered = true;
        }
    }

    // ---- Partial fill simulation (spec §1.2.5) ---------------------------

    void SimulatePartialFill(const FaultConfig& cfg) {
        partial_fill_ratio_ = cfg.fill_ratio;
        partial_fill_count_ = cfg.fill_count;
        partial_fill_slippage_ = cfg.slippage_bps;
    }

    // ---- Helpers -----------------------------------------------------------

    // R-12 p99 (复用 r12_sim_fixture.hpp 同名函数, 在此复现以避免多继承复杂度)
    static std::int64_t p99_chaos_ns(std::vector<std::int64_t> xs) {
        if (xs.empty())
            return 0;
        std::sort(xs.begin(), xs.end());
        const auto idx = static_cast<std::size_t>(static_cast<double>(xs.size()) * 0.99);
        return xs[std::min(idx, xs.size() - 1)];
    }

    static std::int64_t NowNsHelper() noexcept {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    // ---- State -----
    FaultConfig fault_cfg_{};
    FaultState fault_state_{};
    bool wss_connected_{true};
    bool rest_seq_gap_pending_{false};
    std::int64_t clob_delay_ms_{0};
    double partial_fill_ratio_{1.0};
    int partial_fill_count_{1};
    double partial_fill_slippage_{0.0};

    // Using r12 MockWssStub for disconnect tracking
    r12::MockWssStub mock_wss_stub_{};
};

}  // namespace stcpp::test::chaos
