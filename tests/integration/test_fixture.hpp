// tests/integration/test_fixture.hpp — PaperE2EFixture (W5 Wave 24)
//
// Owner: 小宋 (test-replay-engineer)  Sprint-2 W5 Wave 24 (W5-E-03)
// 关联: docs/RESEARCH/laozhou-architecture-v0.6-e2e.md §1 (链路图)
//       docs/RESEARCH/xiaojiang-paper-engine-skeleton-v1.md §3 (3 virtual mock)
//       docs/RESEARCH/laohan-riskmanager-design-v0.3.1.md (RiskGateway evaluate)
//       docs/RESEARCH/laotang-audit-schema-v1.1.md      (AuditEmitter / 12 AET)
//       docs/RESEARCH/laowang-wal-framework-v0.2.md     (4 wal 物理隔离 R-11)
//
// 设计:
//   - 端到端链路: MockPmWss.push() → BookSnapshot → OrderIntent →
//                 RiskGateway.evaluate() → AuditEmitter.emit_decision() →
//                 PaperSigner.Sign() → VirtualMatcher.Match()
//   - 4 wal 物理隔离 (R-11): 4 个 WalWriter 各自 path_prefix, kPathRoots 白名单校验
//   - 不真打云端: MockPmWss 是 in-process struct, 不开 socket;
//                  MockChainRpc 占位 (paper signer 走 VirtualConfirm 不调它)
//   - W5 末替换点: 老李 paper_pm_client / 小冯 PM WSS subscriber 真件就绪后, 把
//                  MockPmWss::push() 接口替换为真 WSS subscriber callback. 不动 fixture
//                  其他部分.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "stcpp/execution/virtual_matcher.hpp"
#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/infra/wal/wal_writer.hpp"
#include "stcpp/observability/audit_emitter.hpp"
#include "stcpp/observability/audit_record.hpp"
#include "stcpp/risk/reject_enum.hpp"
#include "stcpp/risk/risk_gateway.hpp"
#include "stcpp/signer/paper/paper_signer.hpp"
#include "stcpp/signer/signer_iface.hpp"

namespace stcpp::test::integration {

// ---------- WSS book update event (in-process, 不开 socket) -----------------
// v0.5: market_id → condition_id, is_buy → side (Side::Buy/Sell), + token_id + outcome

struct PmBookUpdate {
    std::string market_id;                      // v0.4 compat alias → condition_id
    std::string condition_id;                   // v0.5 primary key (condition_id)
    std::string token_id{"1234567890"};         // v0.5: outcome 级标识 (mock default)
    risk::Side side{risk::Side::Buy};           // v0.5: was is_buy:bool
    risk::Outcome outcome{risk::Outcome::Yes};  // v0.5: new
    // v0.4 compat: is_buy kept for existing callers that set it directly
    bool is_buy{true};  // compat bridge: set side accordingly in MakeValidIntent
    double price{0.55};
    double book_depth_l1_usdc{20'000.0};
    std::int64_t event_ts_ns{0};
    std::int64_t data_source_ts_ns{0};
    std::int64_t ingestion_ts_ns{0};
};

// ---------- MockPmWss (W5 末换小冯 PM WSS subscriber) -----------------------
//
// 同步 in-process, 不起线程. R-12: 调用者在 worker thread 拉 next(), 不阻塞 event loop.
class MockPmWss {
public:
    void push(PmBookUpdate u) { queue_.push_back(std::move(u)); }
    [[nodiscard]] std::size_t size() const noexcept { return queue_.size(); }
    [[nodiscard]] bool empty() const noexcept { return queue_.empty(); }
    [[nodiscard]] PmBookUpdate pop() noexcept {
        PmBookUpdate u = std::move(queue_.front());
        queue_.erase(queue_.begin());
        return u;
    }

private:
    std::vector<PmBookUpdate> queue_;
};

// ---------- MockChainRpc (placeholder; paper signer 不调) -------------------

class MockChainRpc {
public:
    void note_call() noexcept { ++call_count_; }
    [[nodiscard]] std::uint64_t call_count() const noexcept { return call_count_; }

private:
    std::uint64_t call_count_{0};
};

// ---------- 计数 AuditEmitter (转发到真 WalWriter<AuditRecord>) -------------
//
// RiskGateway 通过 risk::AuditEmitter port 接入 — fixture 实现 counting emitter,
// 同时把每条 audit 转发给真 WalWriter<AuditRecord>, 让 R-11 path prefix + R-20 PIT
// 在真 framework 里跑.
class CountingAuditEmitter final : public risk::AuditEmitter {
public:
    using WriterT = stcpp::infra::wal::WalWriter<stcpp::observability::AuditRecord>;

    explicit CountingAuditEmitter(WriterT* paper_audit_writer) noexcept
        : paper_audit_writer_(paper_audit_writer) {}

    [[nodiscard]] bool emit(risk::AuditRecord const& rec) noexcept override {
        ++emitted_total_;
        if (rec.decision == risk::Decision::APPROVED)
            ++emitted_approved_;
        else if (rec.decision == risk::Decision::REJECTED)
            ++emitted_rejected_;
        else
            ++emitted_deferred_;
        const auto idx = static_cast<std::size_t>(rec.reject);
        if (idx < reject_hist_.size())
            reject_hist_[idx] += 1;

        if (paper_audit_writer_ == nullptr)
            return true;

        // 如果原始 audit record 自己 4 ts 不满足 R-20 (e.g. RM 拒了 PIT 违例 intent
        // 时, audit 仍带原 intent 4 ts), framework PIT 会再次拒. 生产语义下走 laotang
        // v1.1 RECON_DRIFT 兜底路径 — 这里 fixture 不双重 enforcement, 计数即可,
        // 让上游 reject_code 反映真实原因 (INVALID_INTENT.TS_*), 不被 WAL 二次覆盖为
        // AUDIT_WAL_BACKPRESSURE.
        const std::int64_t now_ns = stcpp::infra::wal::pit::NowRealtimeNs();
        const bool source_pit_ok = (rec.event_ts_ns > 0) && (rec.data_source_ts_ns >= rec.event_ts_ns) &&
                                   (rec.ingestion_ts_ns >= rec.data_source_ts_ns) &&
                                   (rec.as_of_ts_ns >= rec.ingestion_ts_ns) && (rec.as_of_ts_ns <= now_ns);
        if (!source_pit_ok) {
            // 真实 RECON_DRIFT 路径 — 不通过 WAL framework PIT enforcement,
            // 但 emitter 计数到 emitted_total/rejected, audit 不丢.
            ++recon_drift_count_;
            return true;
        }

        // 转发到真 WalWriter<AuditRecord> — R-11 path prefix + R-20 PIT 双闭环
        stcpp::observability::AuditRecord wal_rec{};
        wal_rec.event_ts = rec.event_ts_ns;
        wal_rec.data_source_ts = rec.data_source_ts_ns;
        wal_rec.ingestion_ts = rec.ingestion_ts_ns;
        wal_rec.as_of_ts = rec.as_of_ts_ns;
        wal_rec.decision_ts = rec.as_of_ts_ns + 1;
        wal_rec.audit_id_bytes = rec.audit_id;
        wal_rec.event_type = (rec.decision == risk::Decision::REJECTED)
                                 ? stcpp::observability::AuditEventType::OrderRejected
                                 : stcpp::observability::AuditEventType::OrderApproved;
        wal_rec.reject_code = rec.reject;
        wal_rec.sub_reason = rec.sub_reason;
        // v1.3: condition_id (正名; 原 market_id)
        const std::string& cid_str = rec.condition_id;
        const std::size_t mlen = std::min(cid_str.size(), wal_rec.condition_id.size() - 1);
        if (mlen > 0) {
            std::memcpy(wal_rec.condition_id.data(), cid_str.data(), mlen);
            wal_rec.condition_id[mlen] = '\0';
        }

        auto r = paper_audit_writer_->Append(wal_rec);
        if (!r) {
            ++wal_append_failed_;
            return false;
        }
        ++paper_audit_records_;
        return true;
    }

    [[nodiscard]] std::uint64_t emitted_total() const noexcept { return emitted_total_; }
    [[nodiscard]] std::uint64_t emitted_approved() const noexcept { return emitted_approved_; }
    [[nodiscard]] std::uint64_t emitted_rejected() const noexcept { return emitted_rejected_; }
    [[nodiscard]] std::uint64_t paper_audit_records() const noexcept { return paper_audit_records_; }
    [[nodiscard]] std::uint64_t wal_append_failed() const noexcept { return wal_append_failed_; }
    [[nodiscard]] std::uint64_t recon_drift_count() const noexcept { return recon_drift_count_; }
    [[nodiscard]] std::uint64_t reject_count(risk::RejectCode c) const noexcept {
        const auto idx = static_cast<std::size_t>(c);
        return idx < reject_hist_.size() ? reject_hist_[idx] : 0;
    }

private:
    WriterT* paper_audit_writer_{nullptr};
    std::uint64_t emitted_total_{0};
    std::uint64_t emitted_approved_{0};
    std::uint64_t emitted_rejected_{0};
    std::uint64_t emitted_deferred_{0};
    std::uint64_t paper_audit_records_{0};
    std::uint64_t wal_append_failed_{0};
    std::uint64_t recon_drift_count_{0};
    std::array<std::uint64_t, 21> reject_hist_{};
};

// ---------- Fixture ----------------------------------------------------------

class PaperE2EFixture : public ::testing::Test {
protected:
    void SetUp() override {
        using stcpp::infra::wal::WalConfig;
        using stcpp::infra::wal::WalKind;
        using stcpp::infra::wal::WalWriter;
        using stcpp::observability::AuditRecord;

        // 4 wal 物理隔离 — 各自 path prefix, framework path_prefix R-11 硬校验.
        // (skeleton writer 不真写文件 fd; path prefix check + PIT chain 是真的)
        auto open = [](WalKind k, const char* suffix) -> std::unique_ptr<WalWriter<AuditRecord>> {
            WalConfig cfg{};
            cfg.kind = k;
            cfg.path_prefix = std::string(stcpp::infra::wal::PathRootOf(k)) + suffix;
            auto r = WalWriter<AuditRecord>::Open(cfg);
            if (!r)
                return nullptr;
            return std::move(r).value();
        };
        paper_audit_ = open(WalKind::PaperAudit, "paper_e2e_smoke");
        risk_audit_ = open(WalKind::RiskAudit, "paper_e2e_smoke");
        position_ = open(WalKind::Position, "paper_e2e_smoke");
        shadow_audit_ = open(WalKind::ShadowAudit, "paper_e2e_smoke");

        // RiskGateway (老韩 v0.3.1) — 真件
        risk::RiskConfig rcfg{};
        rcfg.per_order_cap_usdc = stcpp::domain::MicroPUSD::from_micro(10'000);
        rcfg.market_exposure_cap_usdc = stcpp::domain::MicroPUSD::from_micro(50'000);
        rcfg.bankroll_usdc = stcpp::domain::MicroPUSD::from_micro(100'000);       // c2b 保值
        rcfg.daily_loss_halt_usdc = stcpp::domain::MicroPUSD::from_micro(5'000);  // c2b 保值
        rcfg.consec_loss_halt_count = 5;
        rcfg.excessive_slippage_bps = 200;
        rcfg.edge_ci_lower_floor = 0.0;
        rcfg.strategy_decay_min_ev_ratio = 0.3;

        audit_emitter_ = std::make_shared<CountingAuditEmitter>(paper_audit_.get());
        rg_ = std::make_unique<risk::RiskGateway>(rcfg, audit_emitter_);
        rg_->set_state(risk::RmState::RUNNING);  // 跳过 SAFE_MODE (单测 fixture)
        rg_->set_bankroll(100'000);

        // PaperSigner (小蒋 + 3 virtual mock)
        nonce_ = std::make_unique<signer::paper::VirtualNonceProvider>(0);
        gas_ = std::make_unique<signer::paper::VirtualGasEstimator>();
        confirm_ = std::make_unique<signer::paper::VirtualConfirmWatcher>(0xC0FFEED00DULL);
        signer_ = std::make_unique<signer::paper::PaperSigner>(nonce_.get(), gas_.get(), confirm_.get());

        // VirtualMatcher (小蒋 Mode A++ — deterministic seed for reproducibility)
        matcher_ = std::make_unique<execution::VirtualMatcher>(0xBEEFCAFEULL);
    }

    // 构造合法 OrderIntent (4 ts 满足 R-20, signal_id 唯一)
    [[nodiscard]] risk::OrderIntent MakeValidIntent(const PmBookUpdate& book, const std::string& signal_id) {
        risk::OrderIntent it{};
        it.event_ts_ns = book.event_ts_ns;
        it.data_source_ts_ns = book.data_source_ts_ns;
        it.ingestion_ts_ns = book.ingestion_ts_ns;
        it.as_of_ts_ns = stcpp::infra::wal::pit::NowRealtimeNs();
        // v0.5: condition_id + token_id + outcome + side
        it.condition_id = book.condition_id.empty() ? book.market_id : book.condition_id;
        it.token_id = book.token_id;
        it.outcome = book.outcome;
        // v0.5: side (compat bridge: if is_buy was set, map to Side::Buy)
        it.side = book.side;
        it.strategy_id = "strat_p001_paper";
        it.signal_id = signal_id;
        it.feature_snapshot_id = "fs_" + signal_id;
        it.price = book.price;
        it.size_pUSD_micro = 100;  // 小单 < per_order_cap_usdc
        it.book_depth_l1_usdc = book.book_depth_l1_usdc;
        it.book_snapshot_ts_ns = book.data_source_ts_ns;
        it.tick_size = 0.01;
        it.is_close = false;
        // v0.6 (Wave 3): V2 CLOB 必填字段 — 上游 Orchestrator/策略层填入, fixture 代填.
        //   timestamp_ms: EIP-712 Order.timestamp (ms). 取 as_of_ts (R3.6/R3.7/R3.8 in-window).
        //   metadata/builder: 用 OrderIntent 默认 bytes32(0) (合法 ^0x[0-9a-f]{64}$ 66chars).
        // RM v0.6 check_invalid_intent_ 步骤 (i)(j)(k) 对这三字段硬校验; 不填 → TS_V2_MISSING.
        it.timestamp_ms = it.as_of_ts_ns / 1'000'000LL;
        return it;
    }

    // 一次完整 e2e: book → intent → RM.evaluate → signer.Sign → matcher.Match.
    struct E2EOutcome {
        risk::RiskDecision rm_decision{};
        signer::SignResponse sign_resp{};
        execution::VirtualFill fill{};
        std::int64_t latency_ns{0};
        bool went_through_signer{false};
    };

    [[nodiscard]] E2EOutcome RunOneE2E(const PmBookUpdate& book, const std::string& signal_id) {
        const auto t0 = std::chrono::steady_clock::now();
        E2EOutcome out{};
        auto intent = MakeValidIntent(book, signal_id);
        out.rm_decision = rg_->evaluate(intent);

        if (out.rm_decision.is_approved()) {
            // Wave 97 ABI V2: SignRequest 升级至 OrderIntent v0.6 字段语义
            signer::SignRequest req{};
            std::memcpy(req.audit_id.data(), out.rm_decision.audit_id.data(),
                        out.rm_decision.audit_id.size());
            req.intent_id = static_cast<std::uint64_t>(intent.size_pUSD_micro);
            // V2: condition_id (市场级 bytes32 hex), 替代 V1 market_id
            req.condition_id = intent.condition_id;
            // V2: token_id (uint256 decimal, 替代 V1 outcome="YES"/"NO")
            req.token_id = intent.token_id;
            req.price = intent.price;
            // V2: size_pUSD_micro (int64_t), 替代 V1 size_usdc:double
            req.size_pUSD_micro = intent.size_pUSD_micro;
            // V2: side (0=Buy/1=Sell)
            req.side = static_cast<std::uint8_t>(intent.side);
            // V2: timestamp_ms 非零 (R-R20-01); 从 intent 透传 (若可用) 否则从 as_of 推算
            req.timestamp_ms =
                (intent.timestamp_ms != 0) ? intent.timestamp_ms : intent.as_of_ts_ns / 1'000'000LL;
            // V2: metadata/builder bytes32 零值 (不使用时)
            req.metadata = "0x0000000000000000000000000000000000000000000000000000000000000000";
            req.builder = "0x0000000000000000000000000000000000000000000000000000000000000000";
            req.event_ts_ns = intent.event_ts_ns;
            req.data_source_ts_ns = intent.data_source_ts_ns;
            req.ingestion_ts_ns = intent.ingestion_ts_ns;
            req.as_of_ts_ns = intent.as_of_ts_ns;
            out.sign_resp = signer_->Sign(req);
            out.went_through_signer = true;

            execution::VirtualOrder vo{};
            std::memcpy(vo.audit_id.data(), out.rm_decision.audit_id.data(), out.rm_decision.audit_id.size());
            vo.intent_id = req.intent_id;
            vo.market_id = intent.condition_id;  // v0.5: was market_id
            vo.outcome = "YES";
            vo.size_usdc = static_cast<double>(intent.size_pUSD_micro);
            vo.quote_price = intent.price;
            vo.book_depth_l1_usdc = intent.book_depth_l1_usdc;
            vo.tick_size = intent.tick_size;
            vo.event_ts_ns = intent.event_ts_ns;
            vo.data_source_ts_ns = intent.data_source_ts_ns;
            vo.ingestion_ts_ns = intent.ingestion_ts_ns;
            vo.as_of_ts_ns = intent.as_of_ts_ns;
            vo.wall_now_ns = stcpp::infra::wal::pit::NowRealtimeNs();
            out.fill = matcher_->Match(vo);
        }

        const auto t1 = std::chrono::steady_clock::now();
        out.latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        return out;
    }

    static std::int64_t p99_ns(std::vector<std::int64_t> xs) {
        if (xs.empty())
            return 0;
        std::sort(xs.begin(), xs.end());
        std::size_t idx = static_cast<std::size_t>(static_cast<double>(xs.size()) * 0.99);
        if (idx >= xs.size())
            idx = xs.size() - 1;
        return xs[idx];
    }

    // ---- fixture state ----
    MockPmWss pm_wss_;
    MockChainRpc chain_rpc_;
    std::unique_ptr<stcpp::infra::wal::WalWriter<stcpp::observability::AuditRecord>> paper_audit_,
        risk_audit_, position_, shadow_audit_;
    std::shared_ptr<CountingAuditEmitter> audit_emitter_;
    std::unique_ptr<risk::RiskGateway> rg_;
    std::unique_ptr<signer::paper::VirtualNonceProvider> nonce_;
    std::unique_ptr<signer::paper::VirtualGasEstimator> gas_;
    std::unique_ptr<signer::paper::VirtualConfirmWatcher> confirm_;
    std::unique_ptr<signer::paper::PaperSigner> signer_;
    std::unique_ptr<execution::VirtualMatcher> matcher_;
};

}  // namespace stcpp::test::integration
