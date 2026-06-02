// src/stcpp/debug_api/endpoint_risk.cpp — GET /api/v1/risk/rejects (ADR-038 §4.2)
// Owner: 小卢 (senior-ic-pool)  ADR-038 MVP
// 关联:
//   ADR-038 §4.2 (RM 拒单列表 + reason_code)
//   §3 schema 铁律: 顶层 mode (R-11), rejected_ts epoch_ns int64 (R-20)
//   R-12: 只读 RM double-buffer reject snapshot, 绝不调 RM::evaluate
//   小白 §1: reason_code/intent_ref 为内部安全字段, 无签名字节/私钥
//
// 只读: provider.risk_rejects()。MVP stub 返回空列表。真实接入 = 老韩 RmDebugSnapshot。

#include "src/stcpp/debug_api/endpoint_common.hpp"
#include "src/stcpp/debug_api/endpoint_payloads.hpp"
#include "src/stcpp/debug_api/server.hpp"

namespace stcpp::debug_api {

void register_risk(httplib::Server& svr, const HttpServer& hs) {
    // body 由 payload::rejects 构建 (与 SSE /stream rejects 通道单一数据源)
    svr.Get("/api/v1/risk/rejects", [&hs](const httplib::Request& /*req*/, httplib::Response& res) {
        res.set_content(payload::rejects(hs.provider(), now_epoch_ns()), "application/json; charset=utf-8");
        res.status = 200;
    });
}

}  // namespace stcpp::debug_api
