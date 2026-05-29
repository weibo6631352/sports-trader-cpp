// src/stcpp/debug_api/json_writer.hpp — 极简 JSON 拼装工具 (ADR-038 MVP)
// Owner: 小卢 (senior-ic-pool)  ADR-038 MVP
// 关联: ADR-038 §3 (epoch_ns int64, 禁 ISO); 老周 spec §2.2 (MVP 手拼 OK, Sprint-4 升 glaze)
//
// 非热路径, 性能不敏感。提供最小转义保证字段值不破坏 JSON 结构。
// 仅 ASCII 控制字符 + 引号 + 反斜杠转义 (内部字段不含 unicode/控制符, 防御性处理)。

#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace stcpp::debug_api::json {

// 转义字符串值 (含两端引号)
inline std::string str(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (char c : s) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    static const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[(c >> 4) & 0xF];
                    out += hex[c & 0xF];
                } else {
                    out += c;
                }
        }
    }
    out += '"';
    return out;
}

inline std::string i64(std::int64_t v) {
    return std::to_string(v);
}

inline std::string boolean(bool b) {
    return b ? "true" : "false";
}

// double: 固定写法, 保留足够精度, 处理非有限值 (JSON 不支持 NaN/Inf → 输出 0)
inline std::string num(double v) {
    if (!(v == v) || v > 1e308 || v < -1e308) {  // NaN / +-Inf
        return "0";
    }
    // %.10g: 紧凑且精度够 (价格/pnl 场景)
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.10g", v);
    return std::string(buf);
}

}  // namespace stcpp::debug_api::json
