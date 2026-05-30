// derive_address.cpp — 验证 crypto 链路 + 推导 EOA 地址 (live order 第一步)
//
// 1. keccak256 test vector 验证 (空串 → c5d2...)
// 2. WALLET_PRIVATE_KEY (env) → secp256k1 pubkey → keccak256 → EOA 地址
// 3. 与 POLYMARKET_FUNDER_ADDRESS 对比 (确认 EOA 直签 or Safe 形态, 老孙 §3)
//
// 红线: 私钥任何片段绝不 log (只输出地址)。
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <secp256k1.h>

#include "keccak256.h"

namespace {

// hex 字符串 → bytes (去 0x 前缀)
bool hex2bytes(const char* hex, std::uint8_t* out, std::size_t outlen) {
    if (hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X'))
        hex += 2;
    if (std::strlen(hex) != outlen * 2)
        return false;
    for (std::size_t i = 0; i < outlen; ++i) {
        unsigned b;
        if (std::sscanf(hex + i * 2, "%2x", &b) != 1)
            return false;
        out[i] = static_cast<std::uint8_t>(b);
    }
    return true;
}

std::string bytes2hex(const std::uint8_t* b, std::size_t n) {
    static const char* h = "0123456789abcdef";
    std::string s = "0x";
    for (std::size_t i = 0; i < n; ++i) {
        s += h[b[i] >> 4];
        s += h[b[i] & 0xf];
    }
    return s;
}

}  // namespace

int main() {
    // --- 1. keccak256 test vector (小白 §5) ---
    {
        std::uint8_t out[32];
        kc::keccak256(nullptr, 0, out);
        const std::string got = bytes2hex(out, 32);
        const std::string want = "0xc5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470";
        std::printf("[keccak test] empty → %s\n", got.c_str());
        if (got != want) {
            std::printf("❌ keccak256 test vector FAIL (want %s)\n", want.c_str());
            return 1;
        }
        std::printf("✅ keccak256 test vector PASS\n");
    }

    // --- 2. 私钥 (env) → EOA 地址 ---
    const char* pk_hex = std::getenv("WALLET_PRIVATE_KEY");
    if (!pk_hex) {
        std::printf("❌ WALLET_PRIVATE_KEY 未设 (source .env)\n");
        return 1;
    }
    std::uint8_t pk[32];
    if (!hex2bytes(pk_hex, pk, 32)) {
        std::printf("❌ WALLET_PRIVATE_KEY 格式错 (应 0x+64hex)\n");
        return 1;
    }

    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
    secp256k1_pubkey pub;
    if (!secp256k1_ec_pubkey_create(ctx, &pub, pk)) {
        std::memset(pk, 0, sizeof(pk));
        std::printf("❌ 私钥无效 (secp256k1)\n");
        return 1;
    }
    // 序列化为 uncompressed 65 字节 (0x04 || X || Y)
    std::uint8_t pub65[65];
    std::size_t publen = 65;
    secp256k1_ec_pubkey_serialize(ctx, pub65, &publen, &pub, SECP256K1_EC_UNCOMPRESSED);
    std::memset(pk, 0, sizeof(pk));  // 私钥用完即清

    // 地址 = keccak256(pubkey[1..65])[12..32] (去 0x04 前缀, 取后 20 字节)
    std::uint8_t hash[32];
    kc::keccak256(pub65 + 1, 64, hash);
    const std::string addr = bytes2hex(hash + 12, 20);
    std::printf("[EOA 地址] 从私钥推导: %s\n", addr.c_str());

    // --- 3. 与 FUNDER 对比 (老孙 §3 形态) ---
    const char* funder = std::getenv("POLYMARKET_FUNDER_ADDRESS");
    if (funder) {
        std::string f = funder;
        for (auto& c : f) c = static_cast<char>(std::tolower(c));
        std::printf("[FUNDER]   %s\n", f.c_str());
        if (f == addr) {
            std::printf("✅ FUNDER == EOA → maker=signer=EOA 直签 (sigType=1)\n");
        } else {
            std::printf("⚠️ FUNDER != EOA → maker=Safe(funder), signer=EOA 代签 (sigType=1, Polymarket Magic/proxy)\n");
        }
    }
    secp256k1_context_destroy(ctx);
    return 0;
}
