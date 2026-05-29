// stcpp/bin/paper.cpp — paper engine main 入口 (W4 Wave 19 stub loop; W5 Wave 25 防多开)
//
// 落:
//   xiaojiang-paper-engine-skeleton-v1.md §5 (main loop skeleton)
//   ADR gm-signoff-paper-trade R-7  build-time mode 锁
//   laozhou-single-instance-spec-v1.md §3 集成点 3 (main() 第一行 SingleInstanceLock)
//
// W4 当前: 仅启动 ExecutionContext(Paper) + 跑一个 stub loop, 打印 1 次心跳后退出.
// W5 接: Polymarket WSS → 信号 → RiskGateway → PaperSigner → VirtualMatcher → paper_audit.wal
// W5 Wave 25: SingleInstanceLock 作为 main() 第一行强约束, 失败 exit(4).

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "stcpp/execution/execution_mode.hpp"
#include "stcpp/execution/virtual_matcher.hpp"
#include "stcpp/infra/process/single_instance.hpp"
#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/signer/paper/paper_signer.hpp"

namespace {

void RunStubLoop() {
    using namespace stcpp;

    // 3 mock 接口 (生产 paper engine 也走这条路, 单线程不 spawn worker; W5 接 vCPU3 worker pool)
    signer::paper::VirtualNonceProvider nonce{0};
    signer::paper::VirtualGasEstimator gas;
    signer::paper::VirtualConfirmWatcher confirm{/*seed=*/0xC0FFEE'D00DULL};
    signer::paper::PaperSigner psigner{&nonce, &gas, &confirm};

    execution::VirtualMatcher matcher{/*seed=*/0xBE'EFCAFEULL};

    // 单条 stub intent (W5 替换为 WSS 出的 RM-approved intent)
    const std::int64_t t0 = infra::wal::pit::NowRealtimeNs();

    // Wave 97 ABI V2: SignRequest 升级至 OrderIntent v0.6 语义
    // condition_id / token_id / size_pUSD_micro / side / timestamp_ms / metadata / builder
    signer::SignRequest req;
    req.intent_id = 1;
    // V2: condition_id (市场级 bytes32 hex), 替代 V1 market_id
    req.condition_id = "0xstub000000000000000000000000000000000000000000000000000000000000";
    // V2: token_id (uint256 decimal, 替代 outcome="YES"), 替代 V1 outcome="YES"/"NO"
    req.token_id = "1";
    req.price = 0.55;
    // V2: size_pUSD_micro (int64_t micro), 替代 V1 size_usdc=100.0
    req.size_pUSD_micro = 100'000'000LL;  // 100 pUSD stub
    // V2: side=0 Buy
    req.side = 0U;
    // V2: timestamp_ms 非零 (R-R20-01 stub 填入近似当前 ms)
    req.timestamp_ms = t0 / 1'000'000LL;  // ns → ms
    // V2: metadata/builder bytes32 零值
    req.metadata = "0x0000000000000000000000000000000000000000000000000000000000000000";
    req.builder = "0x0000000000000000000000000000000000000000000000000000000000000000";
    req.event_ts_ns = t0 - 10'000'000;  // event 10ms 前
    req.data_source_ts_ns = t0 - 8'000'000;
    req.ingestion_ts_ns = t0 - 4'000'000;
    req.as_of_ts_ns = t0;

    const auto resp = psigner.Sign(req);

    // VirtualOrder 保留 V1 字段 (VirtualMatcher 内部, 不在 ABI V2 F2 范围);
    // stub loop 从 SignRequest V2 字段推算 VirtualOrder 兼容填充.
    execution::VirtualOrder ord;
    ord.intent_id = req.intent_id;
    // VirtualOrder.market_id (V1 兼容 string_view): 用 condition_id 替代
    ord.market_id = req.condition_id;
    // VirtualOrder.outcome (V1 兼容 string_view): stub "YES" (token_id=1 对应 YES side)
    ord.outcome = "YES";
    // VirtualOrder.size_usdc (V1 兼容 double): size_pUSD_micro / 1e6
    ord.size_usdc = static_cast<double>(req.size_pUSD_micro) / 1'000'000.0;
    ord.quote_price = req.price;
    ord.book_depth_l1_usdc = 500.0;
    ord.tick_size = 0.01;
    ord.event_ts_ns = req.event_ts_ns;
    ord.data_source_ts_ns = req.data_source_ts_ns;
    ord.ingestion_ts_ns = req.ingestion_ts_ns;
    ord.as_of_ts_ns = req.as_of_ts_ns;
    ord.wall_now_ns = t0;

    const auto fill = matcher.Match(ord);

    std::fprintf(
        stderr,
        "[paper.stub] mode=%s sign.err=%s sign.nonce=%llu sign.gas=%llu sign.block=%llu "
        "fill.reject=%d fill.size=%.4f fill.price=%.4f p_clamped=%.3f draw=%d\n",
        std::string(execution::ToString(execution::ExecutionContext::Mode())).c_str(),
        std::string(signer::ToString(resp.error)).c_str(), static_cast<unsigned long long>(resp.nonce),
        static_cast<unsigned long long>(resp.gas_estimate),
        static_cast<unsigned long long>(resp.block_number), static_cast<int>(fill.reject),
        fill.fill_size_usdc, fill.fill_price, fill.p_fill_clamped, static_cast<int>(fill.bernoulli_draw));

    // 真生产: 这里循环 select on WSS + 队列; W5 接.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

}  // namespace

int main() {
    using namespace stcpp::execution;

    // W5 Wave 25 — 防多开 (R-7/R-11/R-12):
    // SingleInstanceLock 是 main() 第一行强约束, 在任何 WSS/WAL/RM/signer 启动之前.
    // 失败抛 SingleInstanceLockFailure → stderr 打印 + exit(4) (spec §2.2 / §3 集成点 3).
    // lock 持锁至 main 退出 (RAII), 不进 event loop (R-12).
    try {
        static stcpp::infra::process::SingleInstanceLock s_lock{ExecutionMode::Paper};
        stcpp::infra::process::InstallSigtermHandler(
            stcpp::infra::process::SingleInstanceLock::path_for(ExecutionMode::Paper));
    } catch (const stcpp::infra::process::SingleInstanceLockFailure& e) {
        std::fprintf(stderr, "[paper.main] %s\n", e.what());
        return 4;
    }

    // R-7: build-time 锁; 不允许 runtime 切换. Init 一次性.
#if !(defined(STCPP_EXEC_MODE_paper) || defined(STCPP_EXEC_MODE_PAPER))
    std::fprintf(stderr, "[paper.main] FATAL: paper binary built without STCPP_EXEC_MODE_paper\n");
    return 2;
#endif
    ExecutionContext::Init(ExecutionMode::Paper);

    if (ExecutionContext::Mode() != ExecutionMode::Paper) {
        std::fprintf(stderr, "[paper.main] FATAL: mode mismatch after Init\n");
        return 3;
    }

    RunStubLoop();

    std::fprintf(stderr, "[paper.main] stub loop done; W5 接 WSS + RM + audit WAL.\n");
    return 0;
}
