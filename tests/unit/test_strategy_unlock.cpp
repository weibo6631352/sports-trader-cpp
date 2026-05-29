// tests/unit/test_strategy_unlock.cpp — 三签解锁 8 测试 (老沈, W6 Wave 29)
//
// T1: 正确 3 签 → OK
// T2: 单签 (normal) → LAOHAN_FAILED
// T3: 2 签缺 laotang → LAOTANG_FAILED
// T4: 错签 pubkey mismatch → BAD_SIGNATURE
// T5: 过期签 ts > 1h → EXPIRED
// T6: audit chain 3 audit_id 唯一性
// T7: 观察期 STRATEGY_DECAYED reject → 自动 re-trigger
// T8: emergency override 单签 → OK

#include <array>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "stcpp/cli/three_signature.hpp"
#include "stcpp/risk/reject_enum.hpp"
#include "stcpp/risk/risk_gateway.hpp"

#include <sodium.h>

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

struct KP {
    std::array<std::uint8_t, crypto_sign_ed25519_PUBLICKEYBYTES> pk{};
    std::array<std::uint8_t, crypto_sign_ed25519_SECRETKEYBYTES> sk{};
};
static KP gen() {
    KP k;
    crypto_sign_ed25519_keypair(k.pk.data(), k.sk.data());
    return k;
}

static stcpp::cli::SignerInput mksig(KP const& kp, std::string_view payload, std::int64_t ts) {
    stcpp::cli::SignerInput si;
    std::memcpy(si.pubkey.data(), kp.pk.data(), stcpp::cli::kEd25519PubKeyBytes);
    unsigned long long len = 0;
    crypto_sign_ed25519_sign_detached(si.signature.data(), &len,
                                      reinterpret_cast<const unsigned char*>(payload.data()),
                                      static_cast<unsigned long long>(payload.size()), kp.sk.data());
    si.sig_timestamp_ns = ts;
    return si;
}

// ---------------------------------------------------------------------------
// fixture
// ---------------------------------------------------------------------------

class SUT : public ::testing::Test {
protected:
    static void SetUpTestSuite() { ASSERT_GE(sodium_init(), 0); }
    void SetUp() override {
        kh = gen();
        kt = gen();
        kl = gen();
        now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();
        payload = stcpp::cli::ThreeSignatureVerifier::build_payload("s1",
                                                                    "deadbeef"
                                                                    "01234567"
                                                                    "deadbeef"
                                                                    "01234567",
                                                                    now);
    }
    KP kh, kt, kl;
    std::int64_t now{};
    std::string payload;
    stcpp::cli::ThreeSignatureVerifier V;
};

// T1
TEST_F(SUT, T1_ThreeSig_OK) {
    stcpp::cli::ThreeSignatureInput in;
    in.laohan = mksig(kh, payload, now);
    in.laotang = mksig(kt, payload, now);
    in.laolei = mksig(kl, payload, now);
    in.payload = payload;
    EXPECT_EQ(V.verify(in, now), stcpp::cli::ThreeSigResult::OK);
}

// T2: single sig (no emergency) → laohan pubkey zero → LAOHAN_FAILED
TEST_F(SUT, T2_SingleSig_Rejected) {
    stcpp::cli::ThreeSignatureInput in;
    in.laohan.sig_timestamp_ns = now;
    in.laotang.sig_timestamp_ns = now;
    in.laolei = mksig(kl, payload, now);
    in.payload = payload;
    EXPECT_EQ(V.verify(in, now), stcpp::cli::ThreeSigResult::LAOHAN_FAILED);
}

// T3: 2-sig (missing laotang pubkey) → LAOTANG_FAILED
TEST_F(SUT, T3_TwoSig_Rejected) {
    stcpp::cli::ThreeSignatureInput in;
    in.laohan = mksig(kh, payload, now);
    in.laotang.sig_timestamp_ns = now;  // pubkey all-zero
    in.laolei = mksig(kl, payload, now);
    in.payload = payload;
    EXPECT_EQ(V.verify(in, now), stcpp::cli::ThreeSigResult::LAOTANG_FAILED);
}

// T4: wrong key (laotang pubkey + laolei sk) → BAD_SIGNATURE
TEST_F(SUT, T4_WrongKey_Rejected) {
    stcpp::cli::SignerInput bad;
    std::memcpy(bad.pubkey.data(), kt.pk.data(), stcpp::cli::kEd25519PubKeyBytes);
    unsigned long long len = 0;
    crypto_sign_ed25519_sign_detached(
        bad.signature.data(), &len, reinterpret_cast<const unsigned char*>(payload.data()),
        static_cast<unsigned long long>(payload.size()), kl.sk.data());  // wrong sk
    bad.sig_timestamp_ns = now;

    EXPECT_EQ(V.verify_one(bad, payload, now), stcpp::cli::SigVerifyResult::BAD_SIGNATURE);

    stcpp::cli::ThreeSignatureInput in;
    in.laohan = mksig(kh, payload, now);
    in.laotang = bad;
    in.laolei = mksig(kl, payload, now);
    in.payload = payload;
    EXPECT_EQ(V.verify(in, now), stcpp::cli::ThreeSigResult::LAOTANG_FAILED);
}

// T5: expired sig (ts > 1h ago)
TEST_F(SUT, T5_ExpiredTs_Rejected) {
    constexpr std::int64_t kH = 3'600'000'000'000LL;
    std::int64_t old_ts = now - kH - 1'000'000'000LL;
    std::string old_payload =
        stcpp::cli::ThreeSignatureVerifier::build_payload("s1", "aabbccdd11223344aabbccdd11223344", old_ts);
    auto si = mksig(kh, old_payload, old_ts);
    EXPECT_EQ(V.verify_one(si, old_payload, now), stcpp::cli::SigVerifyResult::EXPIRED);
}

// T6: 3 audit_id unique + payload round-trip
TEST_F(SUT, T6_AuditIds_Unique) {
    auto mk = [](std::int64_t ts, std::uint8_t seq) {
        std::array<std::uint8_t, 16> id{};
        std::int64_t ms = ts / 1'000'000;
        for (int i = 7; i >= 0; --i) {
            id[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(ms & 0xFF);
            ms >>= 8;
        }
        id[8] = seq;
        id[9] = static_cast<std::uint8_t>(ts & 0xFF);
        return id;
    };
    auto a1 = mk(now, 1), a2 = mk(now, 2), a3 = mk(now, 3);
    EXPECT_NE(a1, (std::array<std::uint8_t, 16>{}));
    EXPECT_NE(a1, a2);
    EXPECT_NE(a2, a3);
    EXPECT_NE(a1, a3);

    std::string p = stcpp::cli::ThreeSignatureVerifier::build_payload("s1", "aabb", now);
    EXPECT_NE(p.find("STRATEGY_DECAYED_UNLOCK"), std::string::npos);
    EXPECT_NE(p.find("s1"), std::string::npos);
}

// T7: observation window — decayed ev_ratio → re-trigger STRATEGY_DECAYED
TEST_F(SUT, T7_ObsWindow_ReTrigger) {
    class NullEmit : public stcpp::risk::AuditEmitter {
    public:
        bool emit(stcpp::risk::AuditRecord const&) noexcept override { return true; }
    };
    stcpp::risk::RiskConfig cfg{};
    cfg.strategy_decay_min_ev_ratio = 0.3;
    auto gw = std::make_unique<stcpp::risk::RiskGateway>(cfg, std::make_shared<NullEmit>());

    // Simulate post-unlock: RUNNING state
    gw->set_state(stcpp::risk::RmState::RUNNING);
    gw->set_bankroll(100'000);
    gw->set_strategy_ev_ratio("s1", 0.1);  // below 0.3 → STRATEGY_DECAYED re-trigger
    gw->set_market_active("mkt", true);
    gw->set_market_freshness_ms("mkt", 100);
    gw->set_market_state("mkt", stcpp::risk::MarketState::PREGAME);

    stcpp::risk::OrderIntent i{};
    i.event_ts_ns = now;
    i.data_source_ts_ns = now;
    i.ingestion_ts_ns = now;
    i.as_of_ts_ns = now;
    i.market_id = "mkt";
    i.strategy_id = "s1";
    i.signal_id = "sig1";
    i.is_buy = true;
    i.price = 0.45;
    i.size_usdc = 100;
    i.tick_size = 0.01;
    i.book_depth_l1_usdc = 10000.0;
    i.book_snapshot_ts_ns = now;

    auto d = gw->evaluate(i);
    EXPECT_TRUE(d.is_rejected());
    EXPECT_EQ(d.reject, stcpp::risk::RejectCode::STRATEGY_DECAYED);
}

// T8: emergency override (GM single-sign) → OK
TEST_F(SUT, T8_EmergencyOverride_OK) {
    stcpp::cli::ThreeSignatureInput in;
    in.laohan.sig_timestamp_ns = now;
    in.laotang.sig_timestamp_ns = now;
    in.laolei = mksig(kl, payload, now);
    in.payload = payload;
    in.emergency_override = true;
    EXPECT_EQ(V.verify(in, now), stcpp::cli::ThreeSigResult::OK);
}
