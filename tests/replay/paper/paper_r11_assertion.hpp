// tests/replay/paper/paper_r11_assertion.hpp — PaperLedgerIsolationAssertion (E-04)
//
// Owner: 小宋 (test-replay-engineer)  E-04 replay framework v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §2.3
//
// R-11 校验: 将 integration test 的 paper 账本零污染断言提升为 replay assertion,
//            使其能在历史数据回放中持续验证.
//
// 接口 (spec §2.3.1):
//   set_baselines(paper_audit_base, risk_audit_base, position_base, shadow_audit_base)
//   on_decision(d) → 检查 audit_wal_kind
//   on_fill(f)     → 检查 audit_wal_kind
//   finalize()     → 3 live WAL 水位线必须未变; paper_audit 必须有新增

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/infra/wal/wal_writer.hpp"
#include "stcpp/observability/audit_record.hpp"

namespace stcpp::test::replay {

// ---------- R-11 WAL kind 校验结果 -------------------------------------------

enum class R11ViolationKind : std::uint8_t {
    None = 0,
    DecisionNotPaperAudit = 1,  // paper decision → wrong WAL kind
    FillNotPaperAudit = 2,      // paper fill → wrong WAL kind
    LiveWalWritten = 3,         // live WAL water-mark increased
    PaperWalEmpty = 4,          // paper_audit HWM not increased at finalize
};

struct R11Violation {
    R11ViolationKind kind{R11ViolationKind::None};
    std::uint64_t seq{0};
    std::string message;
};

// ---------- PaperLedgerIsolationAssertion ------------------------------------
//
// 使用方式:
//   assertion.set_baselines(paper_audit_->HighWatermark(), ...)
//   for each event: assertion.on_decision(d) / assertion.on_fill(f)
//   assertion.finalize()

class PaperLedgerIsolationAssertion {
public:
    using WriterT = stcpp::infra::wal::WalWriter<stcpp::observability::AuditRecord>;

    explicit PaperLedgerIsolationAssertion(WriterT* paper_audit_writer, WriterT* risk_audit_writer,
                                           WriterT* position_writer, WriterT* shadow_audit_writer) noexcept
        : paper_audit_writer_(paper_audit_writer),
          risk_audit_writer_(risk_audit_writer),
          position_writer_(position_writer),
          shadow_audit_writer_(shadow_audit_writer) {}

    // spec §2.3.1: 在 replay 开始时注入 4 个 WalWriter 的 HighWatermark 基线
    void set_baselines(std::uint64_t paper_audit_base, std::uint64_t risk_audit_base,
                       std::uint64_t position_base, std::uint64_t shadow_audit_base) noexcept {
        paper_audit_base_ = paper_audit_base;
        risk_audit_base_ = risk_audit_base;
        position_base_ = position_base;
        shadow_audit_base_ = shadow_audit_base;
    }

    // paper decision → audit_wal_kind 必须是 PaperAudit
    bool on_decision_wal_kind(stcpp::infra::wal::WalKind kind, std::uint64_t seq) {
        ++decision_count_;
        if (kind != stcpp::infra::wal::WalKind::PaperAudit) {
            violations_.push_back(
                {R11ViolationKind::DecisionNotPaperAudit, seq,
                 "R-11: paper decision wal_kind != PaperAudit (seq=" + std::to_string(seq) + ")"});
            return false;
        }
        return true;
    }

    // paper fill → audit_wal_kind 必须是 PaperAudit
    bool on_fill_wal_kind(stcpp::infra::wal::WalKind kind, std::uint64_t seq) {
        ++fill_count_;
        if (kind != stcpp::infra::wal::WalKind::PaperAudit) {
            violations_.push_back(
                {R11ViolationKind::FillNotPaperAudit, seq,
                 "R-11: paper fill wal_kind != PaperAudit (seq=" + std::to_string(seq) + ")"});
            return false;
        }
        return true;
    }

    // finalize: 3 live WAL 水位线必须未变; paper_audit 必须有新增
    [[nodiscard]] bool finalize() {
        bool ok = true;

        // 3 live WAL 水位线不变
        if (risk_audit_writer_ && risk_audit_writer_->HighWatermark() != risk_audit_base_) {
            violations_.push_back(
                {R11ViolationKind::LiveWalWritten, 0,
                 "R-11: paper replay 不得写 risk_audit WAL (delta=" +
                     std::to_string(risk_audit_writer_->HighWatermark() - risk_audit_base_) + ")"});
            ok = false;
        }
        if (position_writer_ && position_writer_->HighWatermark() != position_base_) {
            violations_.push_back({R11ViolationKind::LiveWalWritten, 0,
                                   "R-11: paper replay 不得写 position WAL (delta=" +
                                       std::to_string(position_writer_->HighWatermark() - position_base_) +
                                       ")"});
            ok = false;
        }
        if (shadow_audit_writer_ && shadow_audit_writer_->HighWatermark() != shadow_audit_base_) {
            violations_.push_back(
                {R11ViolationKind::LiveWalWritten, 0,
                 "R-11: paper replay 不得写 shadow_audit WAL (delta=" +
                     std::to_string(shadow_audit_writer_->HighWatermark() - shadow_audit_base_) + ")"});
            ok = false;
        }

        // paper_audit 必须有新增记录 (replay 实际产生了 audit)
        if (paper_audit_writer_ && paper_audit_writer_->HighWatermark() <= paper_audit_base_) {
            violations_.push_back({R11ViolationKind::PaperWalEmpty, 0,
                                   "R-11: paper replay 必须有 PaperAudit 写入 (HWM=" +
                                       std::to_string(paper_audit_writer_->HighWatermark()) +
                                       " <= base=" + std::to_string(paper_audit_base_) + ")"});
            ok = false;
        }

        finalized_ = true;
        return ok;
    }

    [[nodiscard]] std::uint64_t violation_count() const noexcept {
        return static_cast<std::uint64_t>(violations_.size());
    }
    [[nodiscard]] std::uint64_t decision_count() const noexcept { return decision_count_; }
    [[nodiscard]] std::uint64_t fill_count() const noexcept { return fill_count_; }
    [[nodiscard]] bool finalized() const noexcept { return finalized_; }

    // live WAL delta (老韩 RM 主权签字用: delta == 0 → 签字)
    [[nodiscard]] std::uint64_t finalized_live_wal_delta() const noexcept {
        std::uint64_t delta = 0;
        if (risk_audit_writer_) {
            const auto hwm = risk_audit_writer_->HighWatermark();
            delta += (hwm > risk_audit_base_) ? (hwm - risk_audit_base_) : 0;
        }
        if (position_writer_) {
            const auto hwm = position_writer_->HighWatermark();
            delta += (hwm > position_base_) ? (hwm - position_base_) : 0;
        }
        if (shadow_audit_writer_) {
            const auto hwm = shadow_audit_writer_->HighWatermark();
            delta += (hwm > shadow_audit_base_) ? (hwm - shadow_audit_base_) : 0;
        }
        return delta;
    }

    [[nodiscard]] const std::vector<R11Violation>& violations() const noexcept { return violations_; }

private:
    WriterT* paper_audit_writer_{nullptr};
    WriterT* risk_audit_writer_{nullptr};
    WriterT* position_writer_{nullptr};
    WriterT* shadow_audit_writer_{nullptr};

    std::uint64_t paper_audit_base_{0};
    std::uint64_t risk_audit_base_{0};
    std::uint64_t position_base_{0};
    std::uint64_t shadow_audit_base_{0};

    std::uint64_t decision_count_{0};
    std::uint64_t fill_count_{0};
    bool finalized_{false};

    std::vector<R11Violation> violations_;
};

}  // namespace stcpp::test::replay
