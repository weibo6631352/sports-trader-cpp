// eip712_sign.cpp — Polymarket CLOB V2 EIP-712 Order 签名 + recover 自检
//
// Phase 1 (本文件): 纯密码学, 无网络。验证签名链路正确性 (小白 P1-5 门)。
//   1. 构造 V2 Order struct (11 字段: salt/maker/signer/tokenId/makerAmount/
//      takerAmount/side/signatureType/timestamp/metadata/builder)
//   2. typeHash → structHash → domainSeparator → digest(0x1901||dom||struct)
//   3. secp256k1_ecdsa_sign_recoverable → r||s||v (65B)
//   4. recover: 从 sig+digest 恢复公钥 → 地址, 必须 == signer(EOA)
//      (权威源: cengizmandros/polymarket-cheatsheet + 团队 laosun-w10-w1 §3.3)
//
// 红线: 私钥任何片段绝不 log。用完即 memset。
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>

#include <secp256k1.h>
#include <secp256k1_recovery.h>

#include "keccak256.h"

namespace {

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

std::string bytes2hex(const std::uint8_t* b, std::size_t n, bool prefix = true) {
    static const char* h = "0123456789abcdef";
    std::string s = prefix ? "0x" : "";
    for (std::size_t i = 0; i < n; ++i) {
        s += h[b[i] >> 4];
        s += h[b[i] & 0xf];
    }
    return s;
}

// uint64 → 32B big-endian (左填零) — abi.encode(uint256)
void u64_to_32(std::uint64_t v, std::uint8_t out[32]) {
    std::memset(out, 0, 32);
    for (int i = 0; i < 8; ++i)
        out[31 - i] = static_cast<std::uint8_t>(v >> (8 * i));
}

// 20B address → 32B big-endian (左填 12 零) — abi.encode(address)
void addr_to_32(const std::uint8_t addr20[20], std::uint8_t out[32]) {
    std::memset(out, 0, 32);
    std::memcpy(out + 12, addr20, 20);
}

// 十进制字符串 → 32B big-endian uint256 (tokenId 是 77 位, 超 uint64)
bool dec_to_32(const char* dec, std::uint8_t out[32]) {
    std::memset(out, 0, 32);
    for (const char* p = dec; *p; ++p) {
        if (*p < '0' || *p > '9')
            return false;
        unsigned carry = static_cast<unsigned>(*p - '0');
        for (int i = 31; i >= 0; --i) {  // out = out*10 + digit
            unsigned x = static_cast<unsigned>(out[i]) * 10 + carry;
            out[i] = static_cast<std::uint8_t>(x & 0xff);
            carry = x >> 8;
        }
        if (carry)
            return false;  // 溢出 256 bit
    }
    return true;
}

// 解析 "0x" 地址 → 20B
bool parse_addr(const char* s, std::uint8_t out[20]) { return hex2bytes(s, out, 20); }

}  // namespace

int main(int argc, char** argv) {
    // keccak self-test
    {
        std::uint8_t out[32];
        kc::keccak256(nullptr, 0, out);
        if (bytes2hex(out, 32) != "0xc5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470") {
            std::printf("❌ keccak self-test FAIL\n");
            return 1;
        }
    }

    // ---- env ----
    const char* pk_hex = std::getenv("WALLET_PRIVATE_KEY");
    const char* funder = std::getenv("POLYMARKET_FUNDER_ADDRESS");
    if (!pk_hex || !funder) {
        std::printf("❌ 缺 WALLET_PRIVATE_KEY / POLYMARKET_FUNDER_ADDRESS (source .env)\n");
        return 1;
    }

    // ---- 订单参数 (argv 或默认测试值) ----
    // argv: tokenId makerAmount takerAmount  (都是 micro 整数字符串)
    const char* tokenId = (argc > 1) ? argv[1]
                                     : "71321045679252212594626385532706912750332728571942532289631379312455583992563";
    std::uint64_t makerAmount = (argc > 2) ? std::strtoull(argv[2], nullptr, 10) : 1000000ULL;  // $1
    std::uint64_t takerAmount = (argc > 3) ? std::strtoull(argv[3], nullptr, 10) : 1010101ULL;  // shares
    std::uint64_t timestamp_ms = (argc > 4) ? std::strtoull(argv[4], nullptr, 10) : 1748476800000ULL;
    // side=0 BUY, signatureType=1 POLY_PROXY (固定, 老李 R4)
    const std::uint64_t side = 0, sigType = 1;

    // salt: 8 随机字节
    std::uint64_t salt = 0;
    {
        int fd = ::open("/dev/urandom", O_RDONLY);
        if (fd < 0 || ::read(fd, &salt, sizeof(salt)) != sizeof(salt)) {
            std::printf("❌ /dev/urandom 读取失败\n");
            return 1;
        }
        ::close(fd);
        salt >>= 1;  // 确保 < 2^63, 正十进制
    }

    std::uint8_t funder20[20], pk[32];
    if (!parse_addr(funder, funder20) || !hex2bytes(pk_hex, pk, 32)) {
        std::printf("❌ funder/私钥 格式错\n");
        return 1;
    }

    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);

    // EOA = signer (从私钥推)
    std::uint8_t eoa20[20];
    {
        secp256k1_pubkey pub;
        if (!secp256k1_ec_pubkey_create(ctx, &pub, pk)) {
            std::memset(pk, 0, sizeof(pk));
            std::printf("❌ 私钥无效\n");
            return 1;
        }
        std::uint8_t pub65[65];
        std::size_t publen = 65;
        secp256k1_ec_pubkey_serialize(ctx, pub65, &publen, &pub, SECP256K1_EC_UNCOMPRESSED);
        std::uint8_t h[32];
        kc::keccak256(pub65 + 1, 64, h);
        std::memcpy(eoa20, h + 12, 20);
    }

    // ---- typeHash ----
    // Order(uint256 salt,address maker,address signer,uint256 tokenId,uint256 makerAmount,
    //       uint256 takerAmount,uint8 side,uint8 signatureType,uint256 timestamp,
    //       bytes32 metadata,bytes32 builder)
    static const char* ORDER_TYPE =
        "Order(uint256 salt,address maker,address signer,uint256 tokenId,uint256 makerAmount,"
        "uint256 takerAmount,uint8 side,uint8 signatureType,uint256 timestamp,"
        "bytes32 metadata,bytes32 builder)";
    std::uint8_t orderTypeHash[32];
    kc::keccak256(reinterpret_cast<const std::uint8_t*>(ORDER_TYPE), std::strlen(ORDER_TYPE), orderTypeHash);

    // ---- structHash: keccak256(typeHash || 11 个 32B 字段) ----
    std::uint8_t buf[32 * 12];
    std::memcpy(buf + 32 * 0, orderTypeHash, 32);
    u64_to_32(salt, buf + 32 * 1);
    addr_to_32(funder20, buf + 32 * 2);  // maker = FUNDER (Safe/proxy)
    addr_to_32(eoa20, buf + 32 * 3);     // signer = EOA
    if (!dec_to_32(tokenId, buf + 32 * 4)) {
        std::memset(pk, 0, sizeof(pk));
        std::printf("❌ tokenId 解析失败\n");
        return 1;
    }
    u64_to_32(makerAmount, buf + 32 * 5);
    u64_to_32(takerAmount, buf + 32 * 6);
    u64_to_32(side, buf + 32 * 7);
    u64_to_32(sigType, buf + 32 * 8);
    u64_to_32(timestamp_ms, buf + 32 * 9);
    std::memset(buf + 32 * 10, 0, 32);  // metadata = bytes32(0)
    std::memset(buf + 32 * 11, 0, 32);  // builder  = bytes32(0)
    std::uint8_t structHash[32];
    kc::keccak256(buf, sizeof(buf), structHash);

    // ---- domainSeparator ----
    // EIP712Domain(string name,string version,uint256 chainId,address verifyingContract)
    static const char* DOMAIN_TYPE =
        "EIP712Domain(string name,string version,uint256 chainId,address verifyingContract)";
    std::uint8_t domBuf[32 * 5];
    kc::keccak256(reinterpret_cast<const std::uint8_t*>(DOMAIN_TYPE), std::strlen(DOMAIN_TYPE), domBuf + 32 * 0);
    static const char* DOMAIN_NAME = "Polymarket CTF Exchange";
    kc::keccak256(reinterpret_cast<const std::uint8_t*>(DOMAIN_NAME), std::strlen(DOMAIN_NAME), domBuf + 32 * 1);
    static const char* DOMAIN_VER = "2";
    kc::keccak256(reinterpret_cast<const std::uint8_t*>(DOMAIN_VER), std::strlen(DOMAIN_VER), domBuf + 32 * 2);
    u64_to_32(137, domBuf + 32 * 3);  // chainId Polygon
    std::uint8_t verifying20[20];
    parse_addr("0xE111180000d2663C0091e4f400237545B87B996B", verifying20);  // 标准 (negRisk=false)
    addr_to_32(verifying20, domBuf + 32 * 4);
    std::uint8_t domainSep[32];
    kc::keccak256(domBuf, sizeof(domBuf), domainSep);

    // ---- digest = keccak256(0x19 0x01 || domainSep || structHash) ----
    std::uint8_t pre[2 + 32 + 32];
    pre[0] = 0x19;
    pre[1] = 0x01;
    std::memcpy(pre + 2, domainSep, 32);
    std::memcpy(pre + 34, structHash, 32);
    std::uint8_t digest[32];
    kc::keccak256(pre, sizeof(pre), digest);

    // ---- sign (recoverable) ----
    secp256k1_ecdsa_recoverable_signature rsig;
    if (!secp256k1_ecdsa_sign_recoverable(ctx, &rsig, digest, pk, nullptr, nullptr)) {
        std::memset(pk, 0, sizeof(pk));
        std::printf("❌ 签名失败\n");
        return 1;
    }
    std::memset(pk, 0, sizeof(pk));  // 私钥用完即清

    std::uint8_t sig64[64];
    int recid = 0;
    secp256k1_ecdsa_recoverable_signature_serialize_compact(ctx, sig64, &recid, &rsig);
    // 65B sig = r(32) || s(32) || v(1=recid+27)
    std::uint8_t sig65[65];
    std::memcpy(sig65, sig64, 64);
    sig65[64] = static_cast<std::uint8_t>(recid + 27);

    // ---- recover 自检 (小白 P1-5): 恢复地址必须 == signer(EOA) ----
    {
        secp256k1_pubkey rpub;
        if (!secp256k1_ecdsa_recover(ctx, &rpub, &rsig, digest)) {
            std::printf("❌ recover 失败\n");
            return 1;
        }
        std::uint8_t pub65[65];
        std::size_t publen = 65;
        secp256k1_ec_pubkey_serialize(ctx, pub65, &publen, &rpub, SECP256K1_EC_UNCOMPRESSED);
        std::uint8_t h[32], rec20[20];
        kc::keccak256(pub65 + 1, 64, h);
        std::memcpy(rec20, h + 12, 20);
        if (std::memcmp(rec20, eoa20, 20) != 0) {
            std::printf("❌ recover 地址 != signer EOA\n  rec=%s\n  eoa=%s\n",
                        bytes2hex(rec20, 20).c_str(), bytes2hex(eoa20, 20).c_str());
            return 1;
        }
        std::printf("✅ P1-5 recover 自检通过: 签名可恢复至 signer EOA %s\n", bytes2hex(eoa20, 20).c_str());
    }
    secp256k1_context_destroy(ctx);

    // ---- 输出 (供 Phase 2 拼 wire body) ----
    std::printf("\n=== 签名结果 (供 order POST) ===\n");
    std::printf("SALT=%llu\n", static_cast<unsigned long long>(salt));
    std::printf("MAKER=0x%s\n", bytes2hex(funder20, 20, false).c_str());
    std::printf("SIGNER=0x%s\n", bytes2hex(eoa20, 20, false).c_str());
    std::printf("TOKEN_ID=%s\n", tokenId);
    std::printf("MAKER_AMOUNT=%llu\n", static_cast<unsigned long long>(makerAmount));
    std::printf("TAKER_AMOUNT=%llu\n", static_cast<unsigned long long>(takerAmount));
    std::printf("TIMESTAMP_MS=%llu\n", static_cast<unsigned long long>(timestamp_ms));
    std::printf("SIGNATURE=%s\n", bytes2hex(sig65, 65).c_str());
    std::printf("DIGEST=%s\n", bytes2hex(digest, 32).c_str());
    return 0;
}
