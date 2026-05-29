// stcpp/signer/paper/paper_signer.cpp — PaperSigner + 3 virtual mock 实现
//
// 落: xiaojiang-paper-engine-skeleton-v1.md §3
// 红线: R-7 / R-11 / R-20 (见 .hpp 头注)

#include "stcpp/signer/paper/paper_signer.hpp"

#include <cstring>

#include "stcpp/infra/wal/pit.hpp"

namespace stcpp::signer::paper {

namespace {

// 简单 deterministic mock signature: 把 intent_id + nonce + as_of 拼一下塞 32 字节.
// 不是密码学签名 — paper 不上链, 只为审计 trace 提供稳定字节流.
void FillMockSignature(std::array<std::uint8_t, 32>& sig, std::uint64_t intent_id, std::uint64_t nonce,
                       std::int64_t as_of_ts_ns) noexcept {
    std::memset(sig.data(), 0, sig.size());
    std::memcpy(sig.data() + 0, &intent_id, sizeof(intent_id));
    std::memcpy(sig.data() + 8, &nonce, sizeof(nonce));
    std::memcpy(sig.data() + 16, &as_of_ts_ns, sizeof(as_of_ts_ns));
    // 16..32 留 0; signature 在 paper mode 不验签, 留作 trace 哈希前缀.
    sig[24] = 'P';  // "Paper" tag, 让 audit 一眼区分
    sig[25] = 'A';
    sig[26] = 'P';
    sig[27] = 'R';
}

// PIT 4 ts 顺序断言 (R-20). signer 入口 / WAL 同步路径都调; 这里复用 pit 模块.
[[nodiscard]] bool AssertChainTs(std::int64_t event_ts, std::int64_t ds_ts, std::int64_t ingest_ts,
                                 std::int64_t as_of_ts) noexcept {
    return event_ts > 0 && ds_ts >= event_ts && ingest_ts >= ds_ts && as_of_ts >= ingest_ts &&
           as_of_ts <= infra::wal::pit::NowRealtimeNs();
}

}  // namespace

// ---------- VirtualConfirmWatcher::Wait ----------
//
// 同步 sleep ~2s + jitter. 单测会 mock 掉 (注入更短常量),
// 生产 paper engine 直接 std::this_thread::sleep_for 走完 budget.

VirtualConfirmWatcher::ConfirmResult VirtualConfirmWatcher::Wait(std::uint64_t /*nonce*/,
                                                                 std::int64_t submit_ts_ns) noexcept {
    // Deterministic jitter: ±300ms 均匀
    std::uniform_int_distribution<std::int64_t> dist(-kJitterMaxNs, kJitterMaxNs);
    const std::int64_t jitter = dist(rng_);
    const std::int64_t latency_ns = kBlockTimeNs + jitter;

    // 注: 真等 2s 会让单测慢, 这里只算 virtual ts, 不真 sleep.
    //  W5 联调时 orchestrator 层若需 wallclock 等待, 在 worker pool 调 sleep_for(latency_ns).
    //  signer mock 仅返回 "如果等了 2s 出块的话 confirm_ts_ns 会是多少" — 单测可观测.
    const std::uint64_t blk = block_counter_.fetch_add(1, std::memory_order_acq_rel) + 1;

    ConfirmResult r;
    r.error = SignerError::Ok;
    r.confirm_ts_ns = submit_ts_ns + latency_ns;
    r.block_number = blk;
    return r;
}

// ---------- PaperSigner::Sign ----------

SignResponse PaperSigner::Sign(const SignRequest& req) noexcept {
    SignResponse resp;
    // 出口默认携带 4 ts (R-20 闭环)
    resp.event_ts_ns = req.event_ts_ns;
    resp.data_source_ts_ns = req.data_source_ts_ns;
    resp.ingestion_ts_ns = req.ingestion_ts_ns;
    resp.as_of_ts_ns = req.as_of_ts_ns;
    // R-11: paper signer 出口硬绑 PaperAudit, 严禁污染 RiskAudit / Position
    resp.audit_wal_kind = infra::wal::WalKind::PaperAudit;

    // R-20: 入口 PIT AssertChain
    if (!AssertChainTs(req.event_ts_ns, req.data_source_ts_ns, req.ingestion_ts_ns, req.as_of_ts_ns)) {
        resp.error = SignerError::PitViolation;
        return resp;
    }

    // 1) Nonce
    if (nonce_ == nullptr) {
        resp.error = SignerError::NonceUnavailable;
        return resp;
    }
    resp.nonce = nonce_->Next();

    // 2) Gas
    if (gas_ == nullptr) {
        resp.error = SignerError::GasEstimateFailed;
        return resp;
    }
    resp.gas_estimate = gas_->Estimate(req);

    // 3) Mock signature (deterministic)
    FillMockSignature(resp.signature, req.intent_id, resp.nonce, req.as_of_ts_ns);

    // 4) Virtual confirm (~2s polygon block, jitter)
    if (confirm_ == nullptr) {
        resp.error = SignerError::ConfirmTimeout;
        return resp;
    }
    const auto cr = confirm_->Wait(resp.nonce, req.as_of_ts_ns);
    if (cr.error != SignerError::Ok) {
        resp.error = cr.error;
        return resp;
    }
    resp.confirm_ts_ns = cr.confirm_ts_ns;
    resp.block_number = cr.block_number;
    resp.error = SignerError::Ok;
    return resp;
}

}  // namespace stcpp::signer::paper
