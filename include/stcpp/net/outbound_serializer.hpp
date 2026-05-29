// stcpp/net/outbound_serializer.hpp — 出站 JSON 序列化 (REST POST body)
//
// Owner: 老陈 (A-NET-01, #03)  Last review: 2026-05-29
//
// 红线:
//   noexcept, zero-alloc (使用 OutboundBuffer, 无 heap)
//   不在 WSS event loop 调用 (R-12; caller = vCPU3 worker)
//   compact JSON only (跨洋带宽紧, 禁 pretty-print)
//   HMAC bug #3: signature_type 必须序列化为整数 1
//
// 字段映射: CLOB v2 API (老李 endpoint-matrix-v3 §A + 老孙 signer_v62 §2.3)
//   ABI lock v1 SignedOrder 已锁 (老李+老孙 W6 签字, handshake §3)
//
// Sprint-4 glaze 路径:
//   v1 手写 JSON, 接口与 glz::write_json<Opts> 约定兼容
//   Sprint-4 升级只换函数体, 调用点 (live_pm_client.cpp) 不改

#pragma once

#include "stcpp/net/outbound_buffer.hpp"
#include "stcpp/polymarket/pm_client.hpp"  // SignedOrder

namespace stcpp::net {

class OutboundSerializer {
public:
    // 序列化 SignedOrder → compact JSON, 写入 buf.
    //
    // 用途: live_pm_client POST /clob/orders body
    //
    // 生成 JSON (示意, compact 无空格):
    //   {"conditionId":"0x...","tokenId":"123...","side":"BUY","price":0.5500,
    //    "size":10.000000,"expiration":0,"signatureType":1,
    //    "signature":"base64==","makerAddress":"0x..."}
    //
    // 字段换算:
    //   price  = limit_price_bps / 10000.0      (4 小数位)
    //   size   = size_usdc_micro / 1_000_000.0  (6 小数位)
    //   side   = 0→"BUY" / 1→"SELL"
    //   expiration = expiration_unix_s (0 = GTC)
    //   signatureType = 1 (HMAC bug #3 enforce — 必须整数 1, 不得写 2)
    //
    // 返 false: buf 溢出 (caller drop + emit rest_submit_overflow_total)
    [[nodiscard]] static bool SerializeSignedOrder(const polymarket::SignedOrder& order,
                                                   OutboundBuffer& buf) noexcept;

    // 序列化 cancel-all 请求 body.
    // POST /cancel-all: body = "{}" 即可 (Polymarket spec)
    // 返 false: 理论上不会溢出 4096B, 但保持接口一致性.
    [[nodiscard]] static bool SerializeCancelAll(OutboundBuffer& buf) noexcept;

private:
    // 内部辅助: 追加 JSON key (带双引号 + 冒号)
    [[nodiscard]] static bool AppendKey(std::string_view key, OutboundBuffer& buf) noexcept;

    // 内部辅助: 追加 JSON string value (带双引号, 不转义 — 字段值已是合法 JSON string)
    [[nodiscard]] static bool AppendStringValue(std::string_view val, OutboundBuffer& buf) noexcept;

    // 内部辅助: 追加 uint64 decimal (无引号)
    [[nodiscard]] static bool AppendUint64(std::uint64_t v, OutboundBuffer& buf) noexcept;

    // 内部辅助: 追加 double (fixed precision)
    [[nodiscard]] static bool AppendDouble(double v, int precision, OutboundBuffer& buf) noexcept;
};

}  // namespace stcpp::net
