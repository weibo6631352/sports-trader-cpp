// tests/unit/test_ingest_raw.cpp — IngestRaw WAL 7 测试 (W6 Wave 29)
//
// Owner: 小冯 (api-watch-general, #34)
// Spec:  小余 DS-01
// last_review: 2026-05-28
//
// T1: PM_WSS frame 落 paper.ingest_raw.wal (Tier 1 Accepted)
// T2: 5 source_kind 各 1 case (PM_WSS + 4 GS_*)
// T3: ring 满 fallback Tier 2 (不丢 metadata, R-12 不阻塞)
// T4: R-20 4 ts UPSTREAM_PAYLOAD 优先 (data_source_ts=0 fallback + PIT reject)
// T5: paper / live build 物理隔离 (R-11 / R-7)
// T6: payload_blob 16 KB 边界 (truncate vs reject)
// T7: ML 训练读 ingest_raw.wal — WalRecord concept + serialize 还原

#include <array>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "stcpp/infra/wal/ingest_raw_record.hpp"
#include "stcpp/infra/wal/ingest_raw_writer.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/infra/wal/wal_writer.hpp"  // WalRecord concept

using namespace stcpp::infra::wal;

// ---------------------------------------------------------------------------
// Shared constants
// ---------------------------------------------------------------------------
static constexpr std::int64_t kTs_event = 1'000'000'000LL;
static constexpr std::int64_t kTs_ds = 1'000'001'000LL;
static constexpr std::int64_t kTs_ingest = 1'000'002'000LL;

// MakeWriter: unique_ptr — IngestRawWriter 有 atomic 成员, 禁 move
static std::unique_ptr<IngestRawWriter> MakeWriter(bool truncate = true) {
    IngestRawWriterConfig cfg;
    cfg.tier1_path_prefix = "/var/lib/stcpp/ingest/paper";
    cfg.tier2_path_prefix = "/var/lib/stcpp/ingest/paper/counter";
    cfg.truncate_oversized_payload = truncate;
    auto w = std::make_unique<IngestRawWriter>(cfg);
    w->set_now_fn_for_test([]() -> std::int64_t { return kTs_ingest; });
    return w;
}

// ---------------------------------------------------------------------------
// T1: PM_WSS frame 落 paper.ingest_raw.wal (Tier 1 Accepted)
// ---------------------------------------------------------------------------
TEST(IngestRaw, T1_PmWss_Tier1Accepted) {
    auto w = MakeWriter();

    const std::string payload = R"({"type":"price_change","market":"0xabc"})";
    const ApplyResult r = w->record_frame(IngestSourceKind::PM_WSS, payload, kTs_event, kTs_ds);

    EXPECT_EQ(r, ApplyResult::Tier1Accepted);
    EXPECT_EQ(w->tier1_accepted_total(), 1u);
    EXPECT_EQ(w->tier2_fallback_total(), 0u);
    EXPECT_EQ(w->pit_rejected_total(), 0u);
}

// ---------------------------------------------------------------------------
// T2: 5 source_kind 各 1 case
// ---------------------------------------------------------------------------
TEST(IngestRaw, T2_FiveSourceKinds) {
    auto w = MakeWriter();
    const std::string payload = "test_payload_1234";

    const IngestSourceKind kinds[] = {
        IngestSourceKind::PM_WSS,    IngestSourceKind::GS_OddsFeed, IngestSourceKind::GS_LiveScore,
        IngestSourceKind::GS_Inplay, IngestSourceKind::GS_Mapping,
    };

    std::uint64_t accepted = 0;
    for (const auto kind : kinds) {
        const ApplyResult r = w->record_frame(kind, payload, kTs_event, kTs_ds);
        EXPECT_EQ(r, ApplyResult::Tier1Accepted) << "source_kind=" << static_cast<int>(kind);
        ++accepted;
    }
    EXPECT_EQ(w->tier1_accepted_total(), accepted);
    EXPECT_EQ(w->tier2_fallback_total(), 0u);

    EXPECT_EQ(ToString(IngestSourceKind::PM_WSS), "PM_WSS");
    EXPECT_EQ(ToString(IngestSourceKind::GS_OddsFeed), "GS_OddsFeed");
    EXPECT_EQ(ToString(IngestSourceKind::GS_LiveScore), "GS_LiveScore");
    EXPECT_EQ(ToString(IngestSourceKind::GS_Inplay), "GS_Inplay");
    EXPECT_EQ(ToString(IngestSourceKind::GS_Mapping), "GS_Mapping");
}

// ---------------------------------------------------------------------------
// T3: ring 满 fallback Tier 2 (不丢 metadata, R-12 不阻塞)
// ---------------------------------------------------------------------------
TEST(IngestRaw, T3_Tier1Full_FallbackTier2) {
    auto w = MakeWriter();
    const std::string payload = "some_frame_data";

    // 正常先 accept
    EXPECT_EQ(w->record_frame(IngestSourceKind::GS_Inplay, payload, kTs_event, kTs_ds),
              ApplyResult::Tier1Accepted);

    // Tier 1 满
    w->simulate_tier1_full(true);
    EXPECT_EQ(w->record_frame(IngestSourceKind::PM_WSS, payload, kTs_event, kTs_ds),
              ApplyResult::Tier2Fallback);
    EXPECT_EQ(w->tier2_fallback_total(), 1u);
    EXPECT_EQ(w->tier1_accepted_total(), 1u);

    // Tier 1 + Tier 2 都满 → Overflow
    w->simulate_tier2_full(true);
    EXPECT_EQ(w->record_frame(IngestSourceKind::GS_OddsFeed, payload, kTs_event, kTs_ds),
              ApplyResult::Tier2Overflow);
    EXPECT_EQ(w->tier2_overflow_total(), 1u);

    // 恢复
    w->simulate_tier1_full(false);
    w->simulate_tier2_full(false);
    EXPECT_EQ(w->record_frame(IngestSourceKind::GS_Mapping, payload, kTs_event, kTs_ds),
              ApplyResult::Tier1Accepted);
}

// ---------------------------------------------------------------------------
// T4: R-20 4 ts UPSTREAM_PAYLOAD 优先
// ---------------------------------------------------------------------------
TEST(IngestRaw, T4_R20_UpstreamPayloadPriority) {
    auto w = MakeWriter();
    const std::string payload = "frame_data";

    // data_source_ts = 0 → fallback, 仍 Accepted
    EXPECT_EQ(w->record_frame(IngestSourceKind::GS_LiveScore, payload, kTs_event, 0),
              ApplyResult::Tier1Accepted);
    EXPECT_GE(w->r20_fallback_total(), 1u);

    // event_ts = 0, ds = 0 → 均 fallback
    EXPECT_EQ(w->record_frame(IngestSourceKind::PM_WSS, payload, 0, 0), ApplyResult::Tier1Accepted);
    EXPECT_GE(w->r20_fallback_total(), 2u);

    // PIT violation: now=500, event=1000, ds=2000 → ds > ingest(500) → PitRejected
    IngestRawWriterConfig cfg2;
    cfg2.tier1_path_prefix = "/var/lib/stcpp/ingest/paper";
    cfg2.tier2_path_prefix = "/var/lib/stcpp/ingest/paper/counter";
    IngestRawWriter w2(cfg2);
    w2.set_now_fn_for_test([]() -> std::int64_t { return 500LL; });

    EXPECT_EQ(w2.record_frame(IngestSourceKind::GS_Inplay, payload, 1000LL, 2000LL),
              ApplyResult::PitRejected);
    EXPECT_EQ(w2.pit_rejected_total(), 1u);
}

// ---------------------------------------------------------------------------
// T5: paper / live build 物理隔离 (R-11 / R-7)
// ---------------------------------------------------------------------------
TEST(IngestRaw, T5_PaperLivePhysicalIsolation) {
    const std::string_view prefix =
#ifdef STCPP_INGEST_RAW_WAL_PREFIX
        STCPP_INGEST_RAW_WAL_PREFIX;
#else
        "/var/lib/stcpp/ingest/paper";
#endif
    EXPECT_NE(prefix.find("paper"), std::string_view::npos)
        << "R-7: paper build must contain 'paper' in STCPP_INGEST_RAW_WAL_PREFIX";
    EXPECT_NE(prefix.find("ingest"), std::string_view::npos)
        << "R-11: IngestRaw prefix must contain 'ingest'";

    EXPECT_NE(PathRootOf(WalKind::IngestRaw).find("ingest"), std::string_view::npos);
    EXPECT_EQ(ToString(WalKind::IngestRaw), "ingest_raw");
    EXPECT_EQ(kWalKindCount, 5u);

    EXPECT_EQ(static_cast<int>(WalKind::RiskAudit), 0);
    EXPECT_EQ(static_cast<int>(WalKind::Position), 1);
    EXPECT_EQ(static_cast<int>(WalKind::PaperAudit), 2);
    EXPECT_EQ(static_cast<int>(WalKind::ShadowAudit), 3);
    EXPECT_EQ(static_cast<int>(WalKind::IngestRaw), 4);
}

// ---------------------------------------------------------------------------
// T6: payload_blob 16 KB 边界
// ---------------------------------------------------------------------------
TEST(IngestRaw, T6_PayloadBoundary_16KB) {
    // Case A: truncate=true, oversized → Accepted (truncated)
    {
        auto w = MakeWriter(/*truncate=*/true);
        const std::string big(kIngestRawMaxPayloadBytes + 100, 'X');
        EXPECT_EQ(w->record_frame(IngestSourceKind::PM_WSS, big, kTs_event, kTs_ds),
                  ApplyResult::Tier1Accepted);
    }

    // Case B: truncate=false, oversized → ParseFailed
    {
        auto w = MakeWriter(/*truncate=*/false);
        const std::string big(kIngestRawMaxPayloadBytes + 1, 'Y');
        EXPECT_EQ(w->record_frame(IngestSourceKind::GS_Inplay, big, kTs_event, kTs_ds),
                  ApplyResult::ParseFailed);
        EXPECT_EQ(w->parse_failed_total(), 1u);
    }

    // Case C: exactly 16384 → Accepted (no truncation needed)
    {
        auto w = MakeWriter(/*truncate=*/true);
        const std::string exact(kIngestRawMaxPayloadBytes, 'Z');
        EXPECT_EQ(w->record_frame(IngestSourceKind::GS_Mapping, exact, kTs_event, kTs_ds),
                  ApplyResult::Tier1Accepted);
    }

    // Case D: empty → ParseFailed
    {
        auto w = MakeWriter();
        EXPECT_EQ(
            w->record_frame(IngestSourceKind::GS_OddsFeed, std::span<const std::byte>{}, kTs_event, kTs_ds),
            ApplyResult::ParseFailed);
    }
}

// ---------------------------------------------------------------------------
// T7: ML 训练读 ingest_raw.wal — WalRecord concept + serialize 还原
// ---------------------------------------------------------------------------

static_assert(WalRecord<IngestRawRecord>,
              "IngestRawRecord must satisfy WalRecord concept (小邓 ML pipeline)");

TEST(IngestRaw, T7_MlTraining_WalRecordConceptAndSerialize) {
    IngestRawRecord rec{};
    rec.set_event_ts_ns(kTs_event);
    rec.set_data_source_ts_ns(kTs_ds);
    rec.set_ingestion_ts_ns(kTs_ingest);
    rec.set_as_of_ts_ns(kTs_ingest);
    rec.set_source_kind(IngestSourceKind::GS_Inplay);
    rec.set_outcome(ReceptionOutcome::Accepted);

    const std::string raw = R"(<scores ts="1000001" sport="soccer"><match id="42"/></scores>)";
    rec.set_payload_size(static_cast<std::uint32_t>(raw.size()));
    std::memcpy(rec.payload_blob_ref().data(), raw.data(), raw.size());
    rec.set_crc32c(0xDEAD'BEEFu);
    rec.audit_id_ref().fill(0xAB);

    // (a) WalRecord concept accessor 接口
    EXPECT_EQ(rec.event_ts_ns(), kTs_event);
    EXPECT_EQ(rec.data_source_ts_ns(), kTs_ds);
    EXPECT_EQ(rec.ingestion_ts_ns(), kTs_ingest);
    EXPECT_EQ(rec.as_of_ts_ns(), kTs_ingest);
    EXPECT_EQ(rec.audit_id().size(), 16u);

    // (b) serialize_into + payload 还原
    const std::size_t max_sz = IngestRawRecord::max_serialized_size();
    EXPECT_GE(max_sz, kIngestRawMaxPayloadBytes + 26u);

    std::vector<std::byte> buf(max_sz);
    const std::size_t written = rec.serialize_into(std::span<std::byte>(buf.data(), buf.size()));
    EXPECT_GT(written, 0u);

    EXPECT_EQ(static_cast<std::uint8_t>(buf[0]), static_cast<std::uint8_t>(IngestSourceKind::GS_Inplay));
    EXPECT_EQ(static_cast<std::uint8_t>(buf[1]), static_cast<std::uint8_t>(ReceptionOutcome::Accepted));

    std::uint32_t de_psz = 0;
    std::memcpy(&de_psz, buf.data() + 2, 4);
    EXPECT_EQ(de_psz, static_cast<std::uint32_t>(raw.size()));

    const std::string_view recovered(reinterpret_cast<const char*>(buf.data() + 6), de_psz);
    EXPECT_EQ(recovered, raw) << "payload 完整还原 (ML 训练源数据)";

    // (c) Tier 2 metadata-only = 26B
    IngestRawRecord meta{};
    meta.set_event_ts_ns(kTs_event);
    meta.set_data_source_ts_ns(kTs_ds);
    meta.set_ingestion_ts_ns(kTs_ingest);
    meta.set_as_of_ts_ns(kTs_ingest);
    meta.set_source_kind(IngestSourceKind::GS_Inplay);
    meta.set_outcome(ReceptionOutcome::DroppedRingFull);
    meta.set_payload_size(0);
    meta.set_crc32c(0);

    std::vector<std::byte> buf2(max_sz);
    EXPECT_EQ(meta.serialize_into(std::span<std::byte>(buf2.data(), buf2.size())), 26u);

    // (d) ReceptionOutcome ToString
    EXPECT_EQ(ToString(ReceptionOutcome::Accepted), "ACCEPTED");
    EXPECT_EQ(ToString(ReceptionOutcome::DroppedRingFull), "DROPPED_RING_FULL");
    EXPECT_EQ(ToString(ReceptionOutcome::DroppedParseFail), "DROPPED_PARSE_FAIL");
    EXPECT_EQ(ToString(ReceptionOutcome::DroppedR20Pit), "DROPPED_R20_PIT");

    // (e) ApplyResult ToString
    EXPECT_EQ(ToString(ApplyResult::Tier1Accepted), "Tier1Accepted");
    EXPECT_EQ(ToString(ApplyResult::Tier2Fallback), "Tier2Fallback");
    EXPECT_EQ(ToString(ApplyResult::Tier2Overflow), "Tier2Overflow");
    EXPECT_EQ(ToString(ApplyResult::PitRejected), "PitRejected");
    EXPECT_EQ(ToString(ApplyResult::ParseFailed), "ParseFailed");
}
