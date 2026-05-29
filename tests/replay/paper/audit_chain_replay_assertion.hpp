// tests/replay/paper/audit_chain_replay_assertion.hpp — AuditChainReplayAssertion (E-04)
//
// Owner: 小宋 (test-replay-engineer)  E-04 replay framework v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §2.2
//
// 链式重算协议 (spec §2.2.1):
//   on_decision(seq, event_type, decision_ts):
//     digest = BLAKE3_payload_hash(seq, event_type, decision_ts)
//     expected_current = BLAKE3_chain_combine(mirror_prev, digest)
//     mirror_prev = expected_current
//     ASSERT emitter.last_hash() == expected_current
//
//   finalize():
//     ASSERT mirror_prev == golden_chain_head
//     ASSERT emitted_count == golden_record_count
//
// 注: BLAKE3_REAL=1 (真 BLAKE3); debug XOR stub 兼容

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "stcpp/observability/audit_record.hpp"
#include "stcpp/observability/blake3_hash.hpp"

namespace stcpp::test::replay {

// ---------- 链校验违例记录 ----------------------------------------------------

struct ChainViolationRecord {
    std::uint64_t                               seq{0};
    stcpp::observability::Blake3Hasher::Hash256 expected_hash{};
    stcpp::observability::Blake3Hasher::Hash256 actual_hash{};
    std::string                                 message;
};

// ---------- AuditChainReplayAssertion ----------------------------------------

class AuditChainReplayAssertion {
public:
    using Hash256 = stcpp::observability::Blake3Hasher::Hash256;

    explicit AuditChainReplayAssertion() noexcept = default;

    // 设置 golden chain head (从首次 paper runtime 跑出的 baseline)
    // spec §2.2.1 finalize() 断言用
    void set_golden(const Hash256& golden_head, std::uint64_t golden_count) noexcept {
        golden_chain_head_ = golden_head;
        golden_record_count_ = golden_count;
        has_golden_ = true;
    }

    // spec §2.2.1: on_decision — replay 每条 decision 重算并 mirror 链
    // Returns true if chain step matches, false if mismatch
    bool on_decision(std::uint64_t           seq,
                     stcpp::observability::AuditEventType event_type,
                     std::int64_t            decision_ts,
                     const Hash256&          emitter_current_hash) {
        const auto digest = stcpp::observability::Blake3Hasher::compute_payload_hash(
            seq,
            static_cast<std::uint8_t>(event_type),
            decision_ts);

        const auto expected_current =
            stcpp::observability::Blake3Hasher::hash_chain(mirror_prev_, digest);

        mirror_prev_ = expected_current;
        ++emitted_count_;

        if (expected_current != emitter_current_hash) {
            violations_.push_back({
                seq, expected_current, emitter_current_hash,
                "R-audit-chain: replay decision #" + std::to_string(seq) +
                " chain head mismatch"
            });
            return false;
        }
        return true;
    }

    // spec §2.2.1 finalize: golden head 对比 + record count 对比
    [[nodiscard]] bool finalize() {
        bool ok = true;
        finalized_ = true;

        if (has_golden_) {
            // Chain head 必须与 golden 一致
            if (mirror_prev_ != golden_chain_head_) {
                violations_.push_back({
                    emitted_count_, mirror_prev_, golden_chain_head_,
                    "R-audit-chain: replay 结束 chain head 必与 golden 一致"
                });
                ok = false;
            }
            // record 数量必须与 golden 一致
            if (emitted_count_ != golden_record_count_) {
                ok = false;
            }
        }

        return ok && violations_.empty();
    }

    [[nodiscard]] const Hash256&   last_mirror_hash()   const noexcept { return mirror_prev_; }
    [[nodiscard]] std::uint64_t    emitted_count()      const noexcept { return emitted_count_; }
    [[nodiscard]] std::size_t      violation_count()    const noexcept { return violations_.size(); }
    [[nodiscard]] bool             finalized()          const noexcept { return finalized_; }

    [[nodiscard]] const std::vector<ChainViolationRecord>& violations() const noexcept {
        return violations_;
    }

private:
    Hash256       mirror_prev_{};         // 起点 = 全 0 (chain 初始)
    Hash256       golden_chain_head_{};
    std::uint64_t golden_record_count_{0};
    std::uint64_t emitted_count_{0};
    bool          has_golden_{false};
    bool          finalized_{false};

    std::vector<ChainViolationRecord> violations_;
};

}  // namespace stcpp::test::replay
