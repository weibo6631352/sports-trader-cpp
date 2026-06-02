// src/stcpp/debug_api/endpoint_positions.cpp — GET /api/v1/positions (ADR-038 §4.1)
// Owner: 小卢 (senior-ic-pool)  ADR-038 MVP
// 关联:
//   ADR-038 §4.1 (per market/outcome 持仓 + pnl_realized/unrealized + mark_price)
//   §3 schema 铁律: 顶层 mode (R-11), as_of_ts epoch_ns int64 (R-20)
//   小白 §1: 黑名单字段 (私钥/签名字节/secret) 物理不在 HoldingView 中
//
// 只读: 从 provider.positions() 读 double-buffer front snapshot, 不碰交易/控制面。
// MVP 默认 StubStateProvider 返回空数组 (结构合法)。真实接入 = 小石 持仓账本 (position ledger) snapshot。

#include "src/stcpp/debug_api/endpoint_common.hpp"
#include "src/stcpp/debug_api/endpoint_payloads.hpp"
#include "src/stcpp/debug_api/server.hpp"

namespace stcpp::debug_api {

void register_positions(httplib::Server& svr, const HttpServer& hs) {
    // body 由 payload::positions 构建 (与 SSE /stream positions 通道单一数据源)
    svr.Get("/api/v1/positions", [&hs](const httplib::Request& /*req*/, httplib::Response& res) {
        res.set_content(payload::positions(hs.provider(), now_epoch_ns()), "application/json; charset=utf-8");
        res.status = 200;
    });
}

}  // namespace stcpp::debug_api
