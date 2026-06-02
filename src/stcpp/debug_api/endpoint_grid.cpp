// src/stcpp/debug_api/endpoint_grid.cpp
//   GET /api/v1/grid  — 全市场轻量摘要批量端点 (老雷 2026-06-02)
// 关联:
//   背景: 跨洋高延迟链路 (RTT ~200ms, HTTP/1.1 6 连接) 下, 前端逐 condition 拉
//         book_pair + quote (N×2 请求/轮) 会把链路打爆 → 排队超时 (见 store.ts 注释)。
//   方案: 一次请求返回【所有】discovered condition 的【顶档摘要】(best_bid/ask + edge/fair),
//         前端摘要行 (折叠态) 全部填价, 且可放心把轮询调快 (1 请求/轮)。
//         全档深度仍走 /api/v1/book_pair/{cid} 按需 (展开时拉)。
//   成本: quote_params()/book_pair() 均为 hub 原子读 (无锁/无 ML 推理); N≈350 × 3 读 = 亚毫秒级。
//   schema: ADR-038 铁律 — mode (R-11) + as_of_ts epoch_ns int64 (R-20)。G-FREEZE-W: 只增不改名。
//
// 响应 200:
//   {"mode":"paper","as_of_ts":<ns>,"count":N,"markets":[
//      {"condition_id":"0x..",
//       "book_found":true,"best_bid":0.81,"best_ask":0.82,"cross_spread":-0.01,"event_ts":<ns>,
//       "quote_found":true,"fair":0.83,"market_mid":0.815,"edge_bps":120.5,"sharp_fair":0.84,
//       "model_confidence":0.6,"advisory":true}, ...]}
//   未命中的字段省略 (book_found/quote_found=false → 仅占位)。

#include <string>
#include <unordered_set>

#include "src/stcpp/debug_api/endpoint_common.hpp"
#include "src/stcpp/debug_api/json_writer.hpp"
#include "src/stcpp/debug_api/server.hpp"

namespace stcpp::debug_api {

void register_grid(httplib::Server& svr, const HttpServer& hs) {
    svr.Get("/api/v1/grid", [&hs](const httplib::Request&, httplib::Response& res) {
        const StateProvider& sp = hs.provider();
        const std::vector<EventInfo> events = sp.events();
        const std::int64_t as_of = now_epoch_ns();

        std::string body;
        body.reserve(64 * 1024);
        body += "{\"mode\":";
        body += json::str(exec_mode_str(sp.mode()));
        body += ",\"as_of_ts\":";
        body += json::i64(as_of);
        body += ",\"markets\":[";

        std::unordered_set<std::string> seen;  // 去重 (理论上每 condition 属单一 event, 防御)
        std::size_t count = 0;
        bool first = true;
        for (const auto& ev : events) {
            for (const auto& cid : ev.condition_ids) {
                if (cid.empty() || !seen.insert(cid).second) {
                    continue;
                }
                const BinaryMarketBookView bv = sp.book_pair(cid);
                const QuoteParams q = sp.quote_params(cid);

                if (!first) {
                    body += ',';
                }
                first = false;
                ++count;

                body += "{\"condition_id\":";
                body += json::str(cid);

                // 顶档 (token0 = outcomes[0] = YES, 与 MarketSummaryRow 取值一致)
                body += ",\"book_found\":";
                body += json::boolean(bv.token0.found);
                if (bv.token0.found) {
                    body += ",\"best_bid\":";
                    body += json::num(bv.token0.best_bid);
                    body += ",\"best_ask\":";
                    body += json::num(bv.token0.best_ask);
                    body += ",\"cross_spread\":";
                    body += json::num(bv.cross_spread);
                    // 延迟锚: event_ts 优先 (最接近数据源), 降级 ingestion_ts
                    body += ",\"event_ts\":";
                    body += json::i64(bv.token0.ts.event_ts_ns);
                    body += ",\"ingestion_ts\":";
                    body += json::i64(bv.token0.ts.ingestion_ts_ns);
                }

                body += ",\"quote_found\":";
                body += json::boolean(q.found);
                if (q.found) {
                    body += ",\"fair\":";
                    body += json::num(q.fair_value);
                    body += ",\"market_mid\":";
                    body += json::num(q.market_mid);
                    body += ",\"edge_bps\":";
                    body += json::num(q.edge_bps);
                    body += ",\"sharp_fair\":";
                    body += json::num(q.sharp_fair);
                    body += ",\"model_confidence\":";
                    body += json::num(q.model_confidence);
                    body += ",\"advisory\":";
                    body += json::boolean(q.advisory);
                }

                body += '}';
            }
        }

        body += "],\"count\":";
        body += json::i64(static_cast<std::int64_t>(count));
        body += '}';

        res.set_content(body, "application/json; charset=utf-8");
        res.status = 200;
    });
}

}  // namespace stcpp::debug_api
