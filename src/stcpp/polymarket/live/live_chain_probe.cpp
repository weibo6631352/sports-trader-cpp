// live_chain_probe.cpp — daemon live 执行链定位探针 (LIVE only, 一次性诊断)
//
// Owner: GM (老雷) 2026-06-13 — 定位「live 真成交却不入账 → cap 瞎 → 风暴」真因。
// v3: 走 daemon 真实同步执行链一笔受控真单, 逐环打印, 锁定不入账断点:
//   adapter.Execute (真 CLOB matched) → 看 VirtualFill (reject/fill_size/mode_tag)
//   → apply_fill 到 accepted_mode_tag=1 账本 (复刻 daemon Build line 765) → 看账本是否真落仓。
//   单测只验过 synthetic fill; 此处验【真 ExecReport→adapter→apply_fill】这条从没端到端验过的链。
// 红线: 私钥/HMAC 不 log; order_id 是公开 hash 可 log。
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
    stcpp::execution::ExecutionContext::Init(stcpp::execution::ExecutionMode::Live);

    stcpp::polymarket::LiveCredentials creds;
    std::string err;
    if (!stcpp::polymarket::LiveCredentials::FromEnv(creds, err)) {
        std::fprintf(stderr, "❌ 凭证: %s\n", err.c_str());
        return 1;
    }
    stcpp::polymarket::LiveOrderSubmitter submitter(std::move(creds), "https://clob.polymarket.com");
    if (!submitter.Ready()) { std::fprintf(stderr, "❌ submitter 未就绪\n"); return 1; }
    stcpp::polymarket::LiveOrderGate gate(
        [&submitter](const stcpp::polymarket::LiveOrderRequest& rq) { return submitter.Submit(rq); });
    gate.Arm();
    stcpp::polymarket::LiveExecutor exec(gate);
    stcpp::polymarket::LiveExecutorAdapter adapter(exec);
    // 复刻 daemon Build line 765: live 账本 accepted_mode_tag=1。
    stcpp::risk::PositionLedger ledger(/*accepted_mode_tag=*/1);

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

    std::printf("=== STEP 1: adapter.Execute (真 CLOB; 复刻 daemon executor_->Execute) ===\n");
    const stcpp::execution::VirtualFill fill = adapter.Execute(vord);
    std::printf("  reject=%d  fill_size_usdc(micro)=%lld  fill_price=%.4f  mode_tag=%d  order_id=%.20s\n",
                static_cast<int>(fill.reject), static_cast<long long>(fill.fill_size_usdc), fill.fill_price,
                static_cast<int>(fill.mode_tag), fill.order_id.data());
    std::printf("  解读: reject=0=Ok(成交) / 2=BernoulliMissed(被当未成交→daemon不记账→风暴!) / 6=ClobRejected(硬拒)\n");

    if (fill.reject != stcpp::execution::MatchReject::Ok || fill.fill_size_usdc <= 0) {
        std::printf("  ❌ 断点【adapter】: 真单可能已成交但 adapter 返回非 Ok/零量 → daemon 在这丢成交 → 不 apply_fill →\n"
                    "     cap 瞎 → 同 token 重发风暴 (这就是 $129 风暴的机制!)。看上面 reject 码 + live_exec 日志定性。\n");
        return 0;
    }

    std::printf("=== STEP 2: apply_fill (复刻 daemon line 2341; accepted_mode_tag=%d vs fill.mode_tag=%d) ===\n",
                static_cast<int>(ledger.accepted_mode_tag()), static_cast<int>(fill.mode_tag));
    stcpp::risk::FillEvent ev;
    ev.filled_size_micro = fill.fill_size_usdc;
    ev.fill_price = fill.fill_price;
    ev.mode_tag = fill.mode_tag;
    ev.order_id = std::string(fill.order_id.data());
    ev.event_ts_ns = ev.data_source_ts_ns = ev.ingestion_ts_ns = ev.as_of_ts_ns = now;
    ledger.apply_fill(cond, tok, stcpp::strategy::Outcome::Yes, ev, "sharp");
    const auto pos = ledger.get_position(tok);
    if (pos && pos->size_usdc != 0) {
        std::printf("  ✅ 入账成功: size_usdc(micro)=%lld avg=%.4f → 此链路【正常】, bug 在 daemon 别处(并发/喂cap/重启)。\n",
                    static_cast<long long>(pos->size_usdc), pos->avg_entry_price);
    } else {
        std::printf("  ❌ 断点【apply_fill】: adapter 返了 Ok 成交但账本没仓 → ledger 拒了 (mode_tag %d vs accepted %d 不匹配?)\n"
                    "     这就是不入账根因。\n",
                    static_cast<int>(ev.mode_tag), static_cast<int>(ledger.accepted_mode_tag()));
    }
    return 0;
}
