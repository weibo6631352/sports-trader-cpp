// src/stcpp/debug_api/endpoint_risk.cpp — GET /api/v1/risk/rejects (ADR-038 §4.2)
// Owner: 小卢 (senior-ic-pool)  ADR-038 MVP
// 关联:
//   ADR-038 §4.2 (RM 拒单列表 + reason_code)
//   §3 schema 铁律: 顶层 mode (R-11), rejected_ts epoch_ns int64 (R-20)
//   R-12: 只读 RM double-buffer reject snapshot, 绝不调 RM::evaluate
//   小白 §1: reason_code/intent_ref 为内部安全字段, 无签名字节/私钥
//
// 只读: provider.risk_rejects()。MVP stub 返回空列表。真实接入 = 老韩 RmDebugSnapshot。

#include <string>
#include <vector>

#include "src/stcpp/debug_api/endpoint_common.hpp"
#include "src/stcpp/debug_api/json_writer.hpp"
#include "src/stcpp/debug_api/server.hpp"

namespace stcpp::debug_api {

void register_risk(httplib::Server& svr, const HttpServer& hs) {
    svr.Get("/api/v1/risk/rejects", [&hs](const httplib::Request& /*req*/, httplib::Response& res) {
        const StateProvider& sp = hs.provider();
        const std::vector<RiskRejectRow> rows = sp.risk_rejects();
        const std::int64_t as_of = now_epoch_ns();

        std::string body;
        body.reserve(256 + rows.size() * 192);
        body += "{\"mode\":";
        body += json::str(exec_mode_str(sp.mode()));
        body += ",\"as_of_ts\":";
        body += json::i64(as_of);
        body += ",\"rejects\":[";
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const RiskRejectRow& r = rows[i];
            if (i) {
                body += ',';
            }
            body += "{\"reason_code\":";
            body += json::str(r.reason_code);
            body += ",\"market_id\":";
            body += json::str(r.market_id);
            body += ",\"intent_ref\":";
            body += json::str(r.intent_ref);
            body += ",\"rejected_ts\":";
            body += json::i64(r.rejected_ts_ns);
            body += '}';
        }
        body += "]}";

        res.set_content(body, "application/json; charset=utf-8");
        res.status = 200;
    });
}

}  // namespace stcpp::debug_api
