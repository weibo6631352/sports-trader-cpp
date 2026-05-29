// tests/replay/paper/r20_ts_assertion.hpp — TimestampMonotonicityAssertion (E-04)
//
// Owner: 小宋 (test-replay-engineer)  E-04 replay framework v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §2.4
//
// R-20 四时间戳单调校验 (spec §2.4.1):
//   event_ts <= data_source_ts <= ingestion_ts <= as_of_ts
//   event_ts == 0 → 违例 (禁用本地 now() 替代上游 ts)
//
// 使用方式:
//   for each event: assertion.on_event(event_ts, data_source_ts, ingestion_ts, as_of_ts)
//   assertion.finalize() → violation_count == 0 → OK

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace stcpp::test::replay {

// ---------- 违例记录 ----------------------------------------------------------

struct R20ViolationRecord {
    std::uint64_t seq{0};
    std::string   violation_type;  // 违例类型描述
    std::int64_t  event_ts{0};
    std::int64_t  data_source_ts{0};
    std::int64_t  ingestion_ts{0};
    std::int64_t  as_of_ts{0};
};

// ---------- TimestampMonotonicityAssertion ------------------------------------

class TimestampMonotonicityAssertion {
public:
    // spec §2.4.1 on_event: R-20 不等式校验
    bool on_event(std::uint64_t seq,
                  std::int64_t event_ts,
                  std::int64_t data_source_ts,
                  std::int64_t ingestion_ts,
                  std::int64_t as_of_ts) {
        bool ok = true;

        // event_ts == 0 (禁用本地 now() 替代上游 ts)
        if (event_ts <= 0) {
            ++violation_count_;
            violations_.push_back({seq,
                "event_ts == 0 (禁用本地 now() 替代上游 ts)",
                event_ts, data_source_ts, ingestion_ts, as_of_ts});
            ok = false;
        }

        // data_source_ts >= event_ts
        if (data_source_ts < event_ts) {
            ++violation_count_;
            violations_.push_back({seq,
                "data_source_ts < event_ts",
                event_ts, data_source_ts, ingestion_ts, as_of_ts});
            ok = false;
        }

        // ingestion_ts >= data_source_ts
        if (ingestion_ts < data_source_ts) {
            ++violation_count_;
            violations_.push_back({seq,
                "ingestion_ts < data_source_ts",
                event_ts, data_source_ts, ingestion_ts, as_of_ts});
            ok = false;
        }

        // as_of_ts >= ingestion_ts
        if (as_of_ts < ingestion_ts) {
            ++violation_count_;
            violations_.push_back({seq,
                "as_of_ts < ingestion_ts",
                event_ts, data_source_ts, ingestion_ts, as_of_ts});
            ok = false;
        }

        ++checked_count_;
        return ok;
    }

    // finalize: violation_count == 0 → OK (spec §2.4.1)
    [[nodiscard]] bool finalize() {
        finalized_ = true;
        return violation_count_ == 0;
    }

    [[nodiscard]] std::uint64_t violation_count()  const noexcept { return violation_count_; }
    [[nodiscard]] std::uint64_t checked_count()    const noexcept { return checked_count_; }
    [[nodiscard]] std::uint64_t inferred_ts_count() const noexcept { return inferred_ts_count_; }
    [[nodiscard]] bool          finalized()         const noexcept { return finalized_; }

    [[nodiscard]] const std::vector<R20ViolationRecord>& violations() const noexcept {
        return violations_;
    }

    // 记录 INFERRED_FROM_INGESTION (不 FAIL, 月度 sweep 用)
    void note_inferred_ts(std::uint64_t /*seq*/) noexcept {
        ++inferred_ts_count_;
    }

    // dump 违例到 string (可读输出, spec §1.1 "diff 必须可读")
    [[nodiscard]] std::string dump_violations() const {
        std::string out;
        for (const auto& v : violations_) {
            out += "[R-20 violation seq=" + std::to_string(v.seq) +
                   " type=" + v.violation_type +
                   " event_ts=" + std::to_string(v.event_ts) +
                   " ds_ts=" + std::to_string(v.data_source_ts) +
                   " ing_ts=" + std::to_string(v.ingestion_ts) +
                   " as_of_ts=" + std::to_string(v.as_of_ts) + "]\n";
        }
        return out;
    }

private:
    std::uint64_t violation_count_{0};
    std::uint64_t inferred_ts_count_{0};
    std::uint64_t checked_count_{0};
    bool          finalized_{false};

    std::vector<R20ViolationRecord> violations_;
};

}  // namespace stcpp::test::replay
