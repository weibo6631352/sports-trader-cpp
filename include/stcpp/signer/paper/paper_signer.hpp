// stcpp/signer/paper/paper_signer.hpp — Paper mode signer (mock, 不上链)
//
// 落:
//   xiaojiang-paper-engine-skeleton-v1.md §3 (3 mock 接口)
//   laozhou-trade-orchestrator-cpp-v0.5.md §18 (ExecutionMode.Paper)
//
// 红线:
//   R-7  本 header 只进 paper binary (CMake STCPP_EXEC_MODE=paper 才 link)
//   R-11 SignResponse.audit_wal_kind = PaperAudit (硬填), 严禁 RiskAudit / Position
//   R-12 同步 API; caller 在 vCPU3 worker pool 调用 (orchestrator 层契约, signer 不 spawn 线程)
//   R-20 入口 AssertChain (event ≤ ds ≤ ingest ≤ as_of), 失败立即拒
//
// 3 virtual mock:
//   - VirtualNonceProvider:  in-memory monotonic counter (atomic)
//   - VirtualGasEstimator:   固定 80000 wei-equivalent (Polygon meta-tx)
//   - VirtualConfirmWatcher: ~2s polygon block + ±300ms jitter (deterministic seed)

#pragma once

#include <atomic>
#include <cstdint>
#include <random>

#include "stcpp/signer/signer_iface.hpp"

namespace stcpp::signer::paper {

// ---------- VirtualNonceProvider ----------
class VirtualNonceProvider final : public INonceProvider {
public:
    explicit VirtualNonceProvider(std::uint64_t initial = 0) noexcept : counter_(initial) {}

    [[nodiscard]] std::uint64_t Next() noexcept override {
        return counter_.fetch_add(1, std::memory_order_acq_rel) + 1;
    }

    [[nodiscard]] std::uint64_t Peek() const noexcept { return counter_.load(std::memory_order_acquire); }

private:
    std::atomic<std::uint64_t> counter_;
};

// ---------- VirtualGasEstimator ----------
// Polygon meta-tx Polymarket order: 经验 ~ 80k gas. 固定值 paper 不动态估.
class VirtualGasEstimator final : public IGasEstimator {
public:
    static constexpr std::uint64_t kFixedGasEstimate = 80'000;

    [[nodiscard]] std::uint64_t Estimate(const SignRequest& /*req*/) noexcept override {
        return kFixedGasEstimate;
    }
};

// ---------- VirtualConfirmWatcher ----------
// 模拟 Polygon 出块 ~2s + ±300ms jitter. Deterministic seed (单测可复现).
class VirtualConfirmWatcher final : public IConfirmWatcher {
public:
    static constexpr std::int64_t kBlockTimeNs = 2'000'000'000LL;   // 2s
    static constexpr std::int64_t kJitterMaxNs = 300'000'000LL;     // ±300ms
    static constexpr std::uint64_t kStartBlockNum = 50'000'000ULL;  // 当前 Polygon block 量级

    explicit VirtualConfirmWatcher(std::uint64_t seed = 0xC0FFEE'D00DULL) noexcept
        : rng_(seed), block_counter_(kStartBlockNum) {}

    [[nodiscard]] ConfirmResult Wait(std::uint64_t /*nonce*/, std::int64_t submit_ts_ns) noexcept override;

private:
    std::mt19937_64 rng_;
    std::atomic<std::uint64_t> block_counter_;
};

// ---------- PaperSigner ----------
//
// 同步 Sign(req) → mock signature + virtual confirm. 不真上链.
// 入口 R-20 AssertChain, 出口 audit_wal_kind = PaperAudit (R-11).
class PaperSigner final : public IPaperSigner {
public:
    PaperSigner(VirtualNonceProvider* nonce, VirtualGasEstimator* gas,
                VirtualConfirmWatcher* confirm) noexcept
        : nonce_(nonce), gas_(gas), confirm_(confirm) {}

    [[nodiscard]] execution::ExecutionMode Mode() const noexcept override {
        return execution::ExecutionMode::Paper;
    }

    [[nodiscard]] SignResponse Sign(const SignRequest& req) noexcept override;

private:
    VirtualNonceProvider* nonce_;
    VirtualGasEstimator* gas_;
    VirtualConfirmWatcher* confirm_;
};

}  // namespace stcpp::signer::paper
