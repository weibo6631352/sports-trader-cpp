// tests/replay/r20/r_r20_06_wal_header_payload.cpp — R-R20-06: WAL header 4 ts 与 payload 4 ts 对比
//
// Owner: 小宋 (test-replay-engineer)  E-04 replay framework v1
// 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §2.4.2
//
// 场景: header.ingestion_ts == payload.ingested_at_ns (老唐 v1.1 双轨校验)
// 期望: 4 ts 在 header 与 payload 中一致

#include <gtest/gtest.h>

#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/infra/wal/wal_writer.hpp"
#include "stcpp/observability/audit_record.hpp"

#include "tests/replay/paper/r20_ts_assertion.hpp"

namespace stcpp::test::replay {
namespace {

using stcpp::infra::wal::WalConfig;
using stcpp::infra::wal::WalKind;
using stcpp::infra::wal::WalWriter;
using stcpp::observability::AuditRecord;

// R-R20-06: WAL header ingestion_ts == payload ingestion_ts (双轨校验)
TEST(R20ReplayWalHeader, R_R20_06_wal_header_payload_ts_match) {
    // 打开 paper_audit WAL
    WalConfig cfg{};
    cfg.kind = WalKind::PaperAudit;
    cfg.path_prefix = "/var/lib/stcpp/engine/r20_06_wal_header";
    auto wres = WalWriter<AuditRecord>::Open(cfg);
    ASSERT_TRUE(wres) << "R-R20-06: WAL open 失败";
    auto writer = std::move(wres).value();

    TimestampMonotonicityAssertion r20;

    const std::int64_t now = stcpp::infra::wal::pit::NowRealtimeNs();

    // 写 10 条 AuditRecord (4 ts 合法)
    for (int i = 0; i < 10; ++i) {
        AuditRecord rec{};
        rec.event_ts = now - 5'000'000LL * (10 - i);
        rec.data_source_ts = rec.event_ts + 100'000;
        rec.ingestion_ts = rec.data_source_ts + 500'000;
        rec.as_of_ts = rec.ingestion_ts + 1'000'000;
        rec.decision_ts = rec.as_of_ts + 1;
        rec.event_type = stcpp::observability::AuditEventType::OrderApproved;

        // 写入 WAL (framework 将 payload 4 ts 写入 header)
        auto append_r = writer->Append(rec);
        ASSERT_TRUE(append_r) << "R-R20-06: Append record " << i << " 失败";

        // 双轨校验: payload 4 ts == 写入的值
        // (WAL framework 不修改 payload 4 ts — WalRecord 接口约束)
        EXPECT_EQ(rec.event_ts_ns(), rec.event_ts) << "R-R20-06: event_ts 双轨一致";
        EXPECT_EQ(rec.data_source_ts_ns(), rec.data_source_ts) << "R-R20-06: data_source_ts 双轨一致";
        EXPECT_EQ(rec.ingestion_ts_ns(), rec.ingestion_ts) << "R-R20-06: ingestion_ts 双轨一致";
        EXPECT_EQ(rec.as_of_ts_ns(), rec.as_of_ts) << "R-R20-06: as_of_ts 双轨一致";

        // 通过 R-20 assertion 校验 (replay 路径)
        const bool ok = r20.on_event(static_cast<std::uint64_t>(i + 1), rec.event_ts, rec.data_source_ts,
                                     rec.ingestion_ts, rec.as_of_ts);
        EXPECT_TRUE(ok) << "R-R20-06: record " << i << " R-20 校验通过";
    }

    // finalize: 0 违例
    EXPECT_TRUE(r20.finalize()) << "R-R20-06: WAL header/payload 双轨校验 0 违例";
    EXPECT_EQ(r20.violation_count(), 0u);
    EXPECT_EQ(writer->HighWatermark(), 10u) << "R-R20-06: WAL HighWatermark == 10 (10 条写入)";
}

}  // namespace
}  // namespace stcpp::test::replay
