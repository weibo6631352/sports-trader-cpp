// stcpp/observability/blake3_hash.hpp — BLAKE3 轻量 wrapper v1.0
//
// Owner: 老唐 (audit-expert, #38)  W6 Wave 29
// 落: laotang-audit-schema-v1.1.md §4.1 (hash chain: current = BLAKE3(prev || payload))
//     老韩 Smell #2 W5-B-02/B-03 M1-A06 前置条件
//
// 实现策略 (W6 选型):
//   FetchContent 拉取官方 BLAKE3 (https://github.com/BLAKE3-team/BLAKE3)
//   编译 portable C 实现 (blake3.c + blake3_portable.c + blake3_dispatch.c)
//   C++ wrapper: Blake3Hasher — 零额外依赖, BLAKE3_REAL 宏开关
//
// 红线:
//   R-7:  live / paper build 都 BLAKE3_REAL=1 (M1-A06 前置)
//   stub (BLAKE3_STUB=1) 仅测试 mock 用, CI grep 拦 live build 禁带 BLAKE3_STUB
//
// 接口:
//   Blake3Hasher::hash_256(span<uint8_t> data) -> array<uint8_t,32>
//   Blake3Hasher::hash_chain(prev, payload)    -> current (audit chain 链接)
//
// 不耻下问: BLAKE3 test vector @老孙 / CI grep enforce @老高 PR v1.2

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

// Build-time switch — 由 CMakeLists.txt 注入
// paper/live build: BLAKE3_REAL=1 (M1-A06 enforce)
// 测试 stub 路径: BLAKE3_STUB=1 (仅单测 mock, 不允许进 live build)
#if defined(BLAKE3_STUB) && defined(BLAKE3_REAL)
#    error "BLAKE3_STUB 和 BLAKE3_REAL 不可同时定义"
#endif

#if !defined(BLAKE3_REAL) && !defined(BLAKE3_STUB)
// 默认启用真 BLAKE3 (M1-A06 要求 paper/live 都启)
#    define BLAKE3_REAL 1
#endif

#if defined(BLAKE3_REAL)
// 引入官方 BLAKE3 C 头文件 (FetchContent 已注入 include path)
extern "C" {
#    include "blake3.h"
}
#endif

namespace stcpp::observability {

inline constexpr std::size_t kBlake3OutBytes = 32;  // BLAKE3-256

// ---------- Blake3Hasher — 轻量 C++ wrapper ----------------------------------
//
// 两个核心操作:
//   1. hash_256(data)          — 对任意字节序列计算 BLAKE3-256
//   2. hash_chain(prev, payload) — audit chain 链接: BLAKE3(prev_hash || payload_hash)
//
// 线程模型: Blake3Hasher 是无状态 static utility class (每次调用独立 hasher 实例).
// 有状态的 chain 持有在 AuditEmitterPool (共享 chain state).

class Blake3Hasher {
public:
    using Hash256 = std::array<std::uint8_t, kBlake3OutBytes>;

    // 计算任意字节流的 BLAKE3-256
    [[nodiscard]] static Hash256 hash_256(std::span<const std::uint8_t> data) noexcept {
#if defined(BLAKE3_REAL)
        return hash_256_real(data.data(), data.size());
#else
        return hash_256_stub(data.data(), data.size());
#endif
    }

    // audit chain 链接: current = BLAKE3(prev_hash || payload_hash)
    // R-20: hash chain 链接按 audit_id 顺序, ts 是 payload 的输入而非 chain 索引.
    [[nodiscard]] static Hash256 hash_chain(const Hash256& prev_hash, const Hash256& payload_hash) noexcept {
#if defined(BLAKE3_REAL)
        return hash_chain_real(prev_hash, payload_hash);
#else
        return hash_chain_stub(prev_hash, payload_hash);
#endif
    }

    // payload hash: 对 AuditRecord 可序列化字节计算 BLAKE3-256
    // 输入: seq(8B) + event_type(1B) + decision_ts(8B) + audit_id(16B) = 33B min
    // W6 实现: seq + event_type + decision_ts (简化, Sprint-3 扩展为 full serialize)
    [[nodiscard]] static Hash256 compute_payload_hash(std::uint64_t seq, std::uint8_t event_type_byte,
                                                      std::int64_t decision_ts_ns) noexcept {
        // 33B 输入缓冲
        std::array<std::uint8_t, 33> buf{};
        std::memcpy(buf.data(), &seq, sizeof(seq));
        buf[8] = event_type_byte;
        std::memcpy(buf.data() + 9, &decision_ts_ns, sizeof(decision_ts_ns));
        return hash_256(std::span<const std::uint8_t>{buf.data(), buf.size()});
    }

    Blake3Hasher() = delete;

private:
#if defined(BLAKE3_REAL)
    [[nodiscard]] static Hash256 hash_256_real(const std::uint8_t* data, std::size_t len) noexcept {
        ::blake3_hasher hasher;
        ::blake3_hasher_init(&hasher);
        ::blake3_hasher_update(&hasher, data, len);
        Hash256 out{};
        ::blake3_hasher_finalize(&hasher, out.data(), kBlake3OutBytes);
        return out;
    }

    [[nodiscard]] static Hash256 hash_chain_real(const Hash256& prev, const Hash256& payload) noexcept {
        // chain input = prev(32B) || payload(32B) = 64B
        std::array<std::uint8_t, kBlake3OutBytes * 2> buf{};
        std::memcpy(buf.data(), prev.data(), kBlake3OutBytes);
        std::memcpy(buf.data() + kBlake3OutBytes, payload.data(), kBlake3OutBytes);
        return hash_256_real(buf.data(), buf.size());
    }
#else
    // stub 实现 (仅测试 mock 用) — 与 W4 XOR stub 行为相同，保证 W4 测试 case 兼容
    [[nodiscard]] static Hash256 hash_256_stub(const std::uint8_t* data, std::size_t len) noexcept {
        Hash256 out{};
        if (len > 0) {
            std::memcpy(out.data(), data, len < kBlake3OutBytes ? len : kBlake3OutBytes);
        }
        return out;
    }

    [[nodiscard]] static Hash256 hash_chain_stub(const Hash256& prev, const Hash256& payload) noexcept {
        Hash256 out{};
        for (std::size_t i = 0; i < kBlake3OutBytes; ++i) {
            out[i] = static_cast<std::uint8_t>(prev[i] ^ payload[i]);
        }
        return out;
    }
#endif
};

}  // namespace stcpp::observability
