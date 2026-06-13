// live_chain_probe.cpp — daemon live 执行链定位探针 (LIVE only, 一次性诊断)
//
// Owner: GM (老雷) 2026-06-13 — 定位「daemon 真成交却不入账 + cap 失效」真因。
// 复刻 daemon 真实链: LiveOrderGate(armed) → LiveExecutor → LiveExecutorAdapter → apply_fill。
// 单发 1 笔受控单, 逐步打印每环结果 → 锁定断点在「adapter 没返成交」还是「apply_fill 没落账」。
// 不跑决策环 → 零重试风暴。红线: 私钥/HMAC 不 log。
//
// 用法: stcpp_live_chain_probe <token_id> <condition_id> <price> <size_usdc> [neg_risk:0|1]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "stcpp/execution/execution_mode.hpp"
#include "stcpp/execution/live_executor_adapter.hpp"
#include "stcpp/execution/virtual_matcher.hpp"
#include "stcpp/polymarket/live/live_order_submitter.hpp"
#include "stcpp/polymarket/live_executor.hpp"
#include "stcpp/polymarket/live_order_gate.hpp"
#include "stcpp/risk/fill_event.hpp"
#include "stcpp/risk/position_ledger.hpp"

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "用法: %s <token_id> <condition_id> <price> <size_usdc> [neg_risk:0|1]\n", argv[0]);
        return 2;
    }
    // 运行时 mode=Live → ledger accepted_mode_tag=1 / adapter mode_tag=1 (复刻 daemon)。
    stcpp::execution::ExecutionContext::Init(stcpp::execution::ExecutionMode::Live);

    stcpp::polymarket::LiveCredentials creds;
    std::string err;
    if (!stcpp::polymarket::LiveCredentials::FromEnv(creds, err)) {
        std::fprintf(stderr, "❌ 凭证: %s\n", err.c_str());
        return 1;
    }
    stcpp::polymarket::LiveOrderSubmitter submitter(std::move(creds), "https://clob.polymarket.com");
    if (!submitter.Ready()) {
        std::fprintf(stderr, "❌ submitter 未就绪\n");
        return 1;
    }
    stcpp::polymarket::LiveOrderGate gate(
        [&submitter](const stcpp::polymarket::LiveOrderRequest& rq) { return submitter.Submit(rq); });
    gate.Arm();
    stcpp::polymarket::LiveExecutor exec(gate);
    stcpp::polymarket::LiveExecutorAdapter adapter(exec);
    stcpp::risk::PositionLedger ledger(/*accepted_mode_tag=*/1);  // live 档 (复刻 daemon line 765)

    const std::string tok = argv[1];
    const std::string cond = argv[2];
    const double price = std::atof(argv[3]);
    const double size_usdc = std::atof(argv[4]);
    const bool neg_risk = (argc > 5) && argv[5][0] == '1';
    const std::int64_t now =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();

    stcpp::execution::VirtualOrder vord;
    vord.token_id = tok;
    vord.market_id = cond;
    vord.outcome = "YES";
    vord.is_buy = true;
    vord.quote_price = price;
    vord.size_usdc = size_usdc;
    vord.neg_risk = neg_risk;
    vord.tick_size = 0.01;
    vord.event_ts_ns = vord.data_source_ts_ns = vord.ingestion_ts_ns = vord.as_of_ts_ns = vord.wall_now_ns = now;

    std::printf("=== STEP 1: adapter.Execute (gate armed → executor → submitter → CLOB) ===\n");
    const stcpp::execution::VirtualFill fill = adapter.Execute(vord);
    std::printf("  reject=%d  fill_size_usdc(micro)=%lld  fill_price=%.4f  mode_tag=%d  order_id=%.12s\n",
                static_cast<int>(fill.reject), static_cast<long long>(fill.fill_size_usdc), fill.fill_price,
                static_cast<int>(fill.mode_tag), fill.order_id.data());

    if (fill.reject != stcpp::execution::MatchReject::Ok || fill.fill_size_usdc <= 0) {
        std::printf("  ❌ 断点在【adapter】: 没返回有效成交 (reject=%d) → daemon 在这丢成交, 不会调 apply_fill\n",
                    static_cast<int>(fill.reject));
        return 0;
    }
    std::printf("  ✓ adapter 返回有效成交。\n=== STEP 2: apply_fill (复刻决策环登账) ===\n");
    stcpp::risk::FillEvent ev;
    ev.filled_size_micro = fill.fill_size_usdc;
    ev.fill_price = fill.fill_price;
    ev.mode_tag = fill.mode_tag;
    ev.order_id = std::string(fill.order_id.data());
    ev.event_ts_ns = ev.data_source_ts_ns = ev.ingestion_ts_ns = now;
    ev.as_of_ts_ns = now;
    ledger.apply_fill(cond, tok, stcpp::strategy::Outcome::Yes, ev, "sharp");
    const auto pos = ledger.get_position(tok);
    if (pos && pos->size_usdc != 0) {
        std::printf("  ✓ 入账成功: size_usdc(micro)=%lld avg=%.4f → daemon 此链路【正常】, bug 在别处(并发/重启/喂RM)\n",
                    static_cast<long long>(pos->size_usdc), pos->avg_entry_price);
    } else {
        std::printf("  ❌ 断点在【apply_fill】: adapter 返了成交但账本没仓 → ledger 拒了 (mode_tag=%d vs accepted=1?)\n",
                    static_cast<int>(ev.mode_tag));
    }
    return 0;
}
