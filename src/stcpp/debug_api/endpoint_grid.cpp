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
#include "src/stcpp/debug_api/endpoint_payloads.hpp"
#include "src/stcpp/debug_api/server.hpp"

namespace stcpp::debug_api {

void register_grid(httplib::Server& svr, const HttpServer& hs) {
    // body 由 payload::grid 构建 (与 SSE /stream grid 通道 + grid_market_obj 单一数据源)
    svr.Get("/api/v1/grid", [&hs](const httplib::Request&, httplib::Response& res) {
        res.set_content(payload::grid(hs.provider(), now_epoch_ns()), "application/json; charset=utf-8");
        res.status = 200;
    });
}

}  // namespace stcpp::debug_api
