// stcpp/debug_api/frame_compress.hpp — SSE 帧压缩 (raw deflate + base64)
//
// Owner: 老雷 (GM)  last_review: 2026-06-05
//
// 老板 2026-06-05「gzip 压缩帧, 不要抢占交易主线程的 cpu」: 跨洋 SSE 帧 (focus detail / grid)
//   逐帧独立压缩 → 减字节 → 减包数 → 减跨洋丢包卡顿。
//
// 关键设计:
//   - 【每帧独立 Z_FINISH 完整压缩】(非流式 Z_NO_FLUSH): httplib 流式 gzip 用 Z_NO_FLUSH 会缓冲帧 →
//     延迟交付 → 恶化新鲜度 (正相反)。本helper 每帧一次性 deflate(Z_FINISH) → 完整 raw-deflate 块 →
//     立即可发, 前端 pako.inflateRaw 独立解。无流状态、无缓冲。
//   - 【raw deflate windowBits=-15】: 无 zlib/gzip 头+校验 → 最小; 前端 pako.inflateRaw 对应。
//   - 【level 1 (最快)】: JSON 重复键, level 1 已 3-4× 压缩; CPU 最小 (老板 CPU 约束)。
//   - 【运行在 httplib ThreadPool, 非 TradingLoop 交易线程】(server.cpp:88) + 仅 >1KB 帧 + ~1 帧/s
//     → CPU 占用可忽略, 不碰交易热路径。

#pragma once

#include <cstdint>
#include <string>

#include <zlib.h>

namespace stcpp::debug_api {

// raw deflate (windowBits -15, 指定 level) — 每帧独立完整压缩 (Z_FINISH)。失败返空。
[[nodiscard]] inline std::string raw_deflate(const std::string& in, int level = 1) {
    z_stream zs{};
    if (deflateInit2(&zs, level, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) return {};
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
    zs.avail_in = static_cast<uInt>(in.size());
    std::string out;
    out.resize(deflateBound(&zs, static_cast<uLong>(in.size())));
    zs.next_out = reinterpret_cast<Bytef*>(out.data());
    zs.avail_out = static_cast<uInt>(out.size());
    const int r = deflate(&zs, Z_FINISH);
    deflateEnd(&zs);
    if (r != Z_STREAM_END) return {};
    out.resize(out.size() - zs.avail_out);
    return out;
}

// 标准 base64 (字符集 +/ , = padding) — 前端 atob 解。
[[nodiscard]] inline std::string base64_encode(const std::string& in) {
    static constexpr char kT[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    const auto* d = reinterpret_cast<const unsigned char*>(in.data());
    std::size_t i = 0;
    for (; i + 3 <= in.size(); i += 3) {
        const std::uint32_t n = (static_cast<std::uint32_t>(d[i]) << 16) |
                                (static_cast<std::uint32_t>(d[i + 1]) << 8) | d[i + 2];
        out += kT[(n >> 18) & 63];
        out += kT[(n >> 12) & 63];
        out += kT[(n >> 6) & 63];
        out += kT[n & 63];
    }
    const std::size_t rem = in.size() - i;
    if (rem == 1) {
        const std::uint32_t n = static_cast<std::uint32_t>(d[i]) << 16;
        out += kT[(n >> 18) & 63];
        out += kT[(n >> 12) & 63];
        out += '=';
        out += '=';
    } else if (rem == 2) {
        const std::uint32_t n = (static_cast<std::uint32_t>(d[i]) << 16) |
                                (static_cast<std::uint32_t>(d[i + 1]) << 8);
        out += kT[(n >> 18) & 63];
        out += kT[(n >> 12) & 63];
        out += kT[(n >> 6) & 63];
        out += '=';
    }
    return out;
}

// 压缩帧 payload → base64(raw-deflate)。前端: pako.inflateRaw(atob(...))。
[[nodiscard]] inline std::string deflate_b64(const std::string& in) {
    return base64_encode(raw_deflate(in));
}

}  // namespace stcpp::debug_api
