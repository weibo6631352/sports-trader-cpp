// stcpp/net/outbound_serializer.cpp — 出站 JSON 序列化实现 (手写 v1, no glaze)
//
// Owner: 老陈 (A-NET-01, #03)  Last review: 2026-05-29
//
// 实现说明:
//   v1 手写 JSON (与现有 pm_wss_subscriber MakeSubscribeFrame 风格一致).
//   接口与 glz::write_json<Opts> 约定兼容 — Sprint-4 升 glaze 只换函数体.
//
// 字段换算 (老李 endpoint-matrix-v3 §A + 老孙 signer_v62 §2.3):
//   price  = limit_price_bps / 10000.0   精度 4 位小数
//   size   = size_usdc_micro / 1e6       精度 6 位小数
//   side   = 0→"BUY" / 1→"SELL"         (其他值 → false)
//   signatureType = 1                     (HMAC bug #3: 必须整数 1)
//
// 无 snprintf / std::to_string 依赖: 手写 uint64 / double 转换以保持 noexcept

#include "stcpp/net/outbound_serializer.hpp"

#include <cstdint>
#include <cstring>
#include <string_view>

namespace stcpp::net {

namespace {

// 手写 uint64 → decimal digits, 写入 buf.
// 返 false = 溢出 (理论 20 位够, 4096B buffer 绰绰有余)
[[nodiscard]] bool AppendUint64Impl(std::uint64_t v, OutboundBuffer& buf) noexcept {
    // 最多 20 位 (UINT64_MAX = 18446744073709551615)
    char tmp[24];
    int len = 0;
    if (v == 0) {
        tmp[len++] = '0';
    } else {
        while (v > 0) {
            tmp[len++] = static_cast<char>('0' + (v % 10));
            v /= 10;
        }
        // reverse
        for (int i = 0, j = len - 1; i < j; ++i, --j) {
            char c = tmp[i];
            tmp[i] = tmp[j];
            tmp[j] = c;
        }
    }
    return buf.append(std::string_view{tmp, static_cast<std::size_t>(len)});
}

// 手写 double → fixed decimal, precision 位小数, 写入 buf.
// noexcept — 不用 snprintf (避免格式字符串 / locale 依赖).
// 精度上限 9 位 (price=4, size=6 足够).
[[nodiscard]] bool AppendDoubleImpl(double v, int precision, OutboundBuffer& buf) noexcept {
    // 处理负号 (price/size 不应为负, 但防御)
    if (v < 0.0) {
        if (!buf.append('-'))
            return false;
        v = -v;
    }
    // 整数部分
    auto int_part = static_cast<std::uint64_t>(v);
    double frac = v - static_cast<double>(int_part);

    if (!AppendUint64Impl(int_part, buf))
        return false;
    if (precision <= 0)
        return true;
    if (!buf.append('.'))
        return false;

    // 小数部分: 乘以 10^precision 取整
    // 精度 <= 9 时用整数算避免浮点误差累积
    std::uint64_t mul = 1;
    for (int i = 0; i < precision; ++i)
        mul *= 10;
    auto frac_int = static_cast<std::uint64_t>(frac * static_cast<double>(mul) + 0.5);

    // 补零前缀 (e.g. 0.0050 → frac_int=50, precision=4 → "0050")
    char frac_buf[12];
    int flen = 0;
    std::uint64_t tmp_frac = frac_int;
    if (tmp_frac == 0) {
        for (int i = 0; i < precision; ++i)
            frac_buf[flen++] = '0';
    } else {
        // 写 precision 位 (右对齐, 补前缀零)
        for (int i = precision - 1; i >= 0; --i) {
            frac_buf[i] = static_cast<char>('0' + (tmp_frac % 10));
            tmp_frac /= 10;
            flen++;
        }
    }
    return buf.append(std::string_view{frac_buf, static_cast<std::size_t>(flen)});
}

}  // namespace

// ---------- OutboundSerializer 私有辅助实现 ----------

bool OutboundSerializer::AppendKey(std::string_view key, OutboundBuffer& buf) noexcept {
    return buf.append('"') && buf.append(key) && buf.append('"') && buf.append(':');
}

bool OutboundSerializer::AppendStringValue(std::string_view val, OutboundBuffer& buf) noexcept {
    return buf.append('"') && buf.append(val) && buf.append('"');
}

bool OutboundSerializer::AppendUint64(std::uint64_t v, OutboundBuffer& buf) noexcept {
    return AppendUint64Impl(v, buf);
}

bool OutboundSerializer::AppendDouble(double v, int precision, OutboundBuffer& buf) noexcept {
    return AppendDoubleImpl(v, precision, buf);
}

// ---------- SerializeSignedOrder ----------
//
// 生成 compact JSON:
// {"conditionId":"0x...","tokenId":"123...","side":"BUY","price":0.5500,
//  "size":10.000000,"expiration":0,"signatureType":1,
//  "signature":"base64==","makerAddress":"0x..."}
//
// HMAC bug #3 enforce: signatureType = 1 (整数, 不得写字符串 "1")

bool OutboundSerializer::SerializeSignedOrder(const polymarket::SignedOrder& order,
                                              OutboundBuffer& buf) noexcept {
    // 侧名 (side=0→BUY, side=1→SELL; 其他值视为 SELL 降级处理)
    std::string_view side_str = (order.side == 0) ? "BUY" : "SELL";

    // price = limit_price_bps / 10000.0  (4 小数位)
    double price = static_cast<double>(order.limit_price_bps) / 10000.0;

    // size  = size_usdc_micro / 1_000_000.0  (6 小数位)
    double size = static_cast<double>(order.size_usdc_micro) / 1'000'000.0;

#define APPEND_OR_RETURN(expr) \
    do {                       \
        if (!(expr))           \
            return false;      \
    } while (false)

    APPEND_OR_RETURN(buf.append('{'));

    // conditionId
    APPEND_OR_RETURN(AppendKey("conditionId", buf));
    APPEND_OR_RETURN(AppendStringValue(order.condition_id, buf));
    APPEND_OR_RETURN(buf.append(','));

    // tokenId
    APPEND_OR_RETURN(AppendKey("tokenId", buf));
    APPEND_OR_RETURN(AppendStringValue(order.token_id, buf));
    APPEND_OR_RETURN(buf.append(','));

    // side
    APPEND_OR_RETURN(AppendKey("side", buf));
    APPEND_OR_RETURN(AppendStringValue(side_str, buf));
    APPEND_OR_RETURN(buf.append(','));

    // price (4 小数位)
    APPEND_OR_RETURN(AppendKey("price", buf));
    APPEND_OR_RETURN(AppendDouble(price, 4, buf));
    APPEND_OR_RETURN(buf.append(','));

    // size (6 小数位)
    APPEND_OR_RETURN(AppendKey("size", buf));
    APPEND_OR_RETURN(AppendDouble(size, 6, buf));
    APPEND_OR_RETURN(buf.append(','));

    // expiration (integer, unix seconds; 0 = GTC)
    APPEND_OR_RETURN(AppendKey("expiration", buf));
    APPEND_OR_RETURN(AppendUint64(
        static_cast<std::uint64_t>(order.expiration_unix_s >= 0 ? order.expiration_unix_s : 0), buf));
    APPEND_OR_RETURN(buf.append(','));

    // signatureType = 1 (HMAC bug #3: 必须整数 1, 不得写字符串)
    APPEND_OR_RETURN(AppendKey("signatureType", buf));
    APPEND_OR_RETURN(buf.append('1'));
    APPEND_OR_RETURN(buf.append(','));

    // signature (base64 保留 padding, R-12: paper mode 留空也合法)
    APPEND_OR_RETURN(AppendKey("signature", buf));
    APPEND_OR_RETURN(AppendStringValue(order.signature, buf));
    APPEND_OR_RETURN(buf.append(','));

    // makerAddress
    APPEND_OR_RETURN(AppendKey("makerAddress", buf));
    APPEND_OR_RETURN(AppendStringValue(order.maker_address, buf));

    APPEND_OR_RETURN(buf.append('}'));

#undef APPEND_OR_RETURN

    return true;
}

// ---------- SerializeCancelAll ----------

bool OutboundSerializer::SerializeCancelAll(OutboundBuffer& buf) noexcept {
    return buf.append('{') && buf.append('}');
}

}  // namespace stcpp::net
