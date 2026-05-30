// keccak256.h — Ethereum Keccak-256 (vendored, public domain reference)
//
// 用于实盘 EIP-712 签名 (live order test, 老孙/老李 spec)。
// 关键: 以太坊 keccak256 用原始 Keccak padding 0x01 (非 FIPS SHA3-256 的 0x06)。
// 验证 test vector (小白 §5): keccak256("") ==
//   c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470
//
// 基于公有领域 Keccak 参考实现 (compact tiny-keccak 风格)。
#pragma once

#include <cstdint>
#include <cstring>

namespace kc {

inline std::uint64_t rotl64(std::uint64_t x, unsigned n) noexcept {
    return (x << n) | (x >> (64 - n));
}

// Keccak-f[1600] 置换 (24 轮)
inline void keccakf(std::uint64_t st[25]) noexcept {
    static const std::uint64_t RC[24] = {
        0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL, 0x8000000080008000ULL,
        0x000000000000808bULL, 0x0000000080000001ULL, 0x8000000080008081ULL, 0x8000000000008009ULL,
        0x000000000000008aULL, 0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
        0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL, 0x8000000000008003ULL,
        0x8000000000008002ULL, 0x8000000000000080ULL, 0x000000000000800aULL, 0x800000008000000aULL,
        0x8000000080008081ULL, 0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL};
    static const unsigned ROT[24] = {1,  3,  6,  10, 15, 21, 28, 36, 45, 55, 2,  14,
                                     27, 41, 56, 8,  25, 43, 62, 18, 39, 61, 20, 44};
    static const unsigned PIL[24] = {10, 7,  11, 17, 18, 3, 5,  16, 8,  21, 24, 4,
                                     15, 23, 19, 13, 12, 2, 20, 14, 22, 9,  6,  1};

    for (int round = 0; round < 24; ++round) {
        std::uint64_t bc[5];
        // Theta
        for (int i = 0; i < 5; ++i)
            bc[i] = st[i] ^ st[i + 5] ^ st[i + 10] ^ st[i + 15] ^ st[i + 20];
        for (int i = 0; i < 5; ++i) {
            std::uint64_t t = bc[(i + 4) % 5] ^ rotl64(bc[(i + 1) % 5], 1);
            for (int j = 0; j < 25; j += 5)
                st[j + i] ^= t;
        }
        // Rho + Pi
        std::uint64_t t = st[1];
        for (int i = 0; i < 24; ++i) {
            int j = PIL[i];
            std::uint64_t tmp = st[j];
            st[j] = rotl64(t, ROT[i]);
            t = tmp;
        }
        // Chi
        for (int j = 0; j < 25; j += 5) {
            for (int i = 0; i < 5; ++i)
                bc[i] = st[j + i];
            for (int i = 0; i < 5; ++i)
                st[j + i] ^= (~bc[(i + 1) % 5]) & bc[(i + 2) % 5];
        }
        // Iota
        st[0] ^= RC[round];
    }
}

// keccak256: 任意输入 → 32 字节 hash (Ethereum padding 0x01)
inline void keccak256(const std::uint8_t* in, std::size_t inlen, std::uint8_t out[32]) noexcept {
    std::uint64_t st[25];
    std::memset(st, 0, sizeof(st));
    const std::size_t rate = 136;  // keccak256: rate = 1088 bits = 136 bytes
    auto* sb = reinterpret_cast<std::uint8_t*>(st);

    std::size_t i = 0;
    while (inlen >= rate) {
        for (std::size_t k = 0; k < rate; ++k)
            sb[k] ^= in[i + k];
        keccakf(st);
        i += rate;
        inlen -= rate;
    }
    // 吸收尾部 + padding (Ethereum: 0x01 ... 0x80)
    std::uint8_t tmp[136];
    std::memset(tmp, 0, rate);
    std::memcpy(tmp, in + i, inlen);
    tmp[inlen] ^= 0x01;       // Keccak 原始 padding (非 SHA3 的 0x06)
    tmp[rate - 1] ^= 0x80;
    for (std::size_t k = 0; k < rate; ++k)
        sb[k] ^= tmp[k];
    keccakf(st);
    std::memcpy(out, sb, 32);
}

}  // namespace kc
