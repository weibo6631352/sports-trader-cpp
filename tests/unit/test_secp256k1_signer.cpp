// tests/unit/test_secp256k1_signer.cpp — Ethereum ECDSA 签名/recover/地址推导锁定
//
// Owner: GM (老雷) 2026-05-31。
// 用公开测试向量 (privkey=1 → 0x7E5F4552091A69125d5DfCb7b8C2659029395Bdf), 不碰真私钥。
#include "stcpp/crypto/secp256k1_signer.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

using stcpp::crypto::Address;
using stcpp::crypto::Bytes32;
using stcpp::crypto::Signature65;

std::string Hex(const Address& a) {
    static const char* h = "0123456789abcdef";
    std::string s = "0x";
    for (auto x : a) {
        s += h[x >> 4];
        s += h[x & 0xf];
    }
    return s;
}

// 标量 1 的私钥 (32B big-endian)。
Bytes32 PrivKeyOne() {
    Bytes32 k{};
    k[31] = 1;
    return k;
}

}  // namespace

// privkey=1 → 已知以太坊地址 (锁定 secp256k1 + keccak 地址推导链)。
TEST(Secp256k1Signer, DeriveAddressVectorOne) {
    Address a{};
    ASSERT_TRUE(stcpp::crypto::DeriveAddress(PrivKeyOne(), a));
    // 小写比较 (我们输出小写 hex)
    EXPECT_EQ(Hex(a), "0x7e5f4552091a69125d5dfcb7b8c2659029395bdf");
}

// 签名 → recover 必须回到同一地址 (下单前 P1-5 自检的核心保证)。
TEST(Secp256k1Signer, SignRecoverRoundTrip) {
    const Bytes32 key = PrivKeyOne();
    Address eoa{};
    ASSERT_TRUE(stcpp::crypto::DeriveAddress(key, eoa));

    Bytes32 digest{};
    for (std::size_t i = 0; i < 32; ++i) digest[i] = static_cast<std::uint8_t>(i + 1);  // 任意 digest

    Signature65 sig{};
    ASSERT_TRUE(stcpp::crypto::SignDigest(digest, key, sig));
    EXPECT_TRUE(sig[64] == 27 || sig[64] == 28);  // Ethereum v

    Address recovered{};
    ASSERT_TRUE(stcpp::crypto::RecoverAddress(digest, sig, recovered));
    EXPECT_EQ(recovered, eoa);
}

// 对真实 V2 order digest 签名 → recover 回 signer (端到端: eip712_v2 + secp256k1)。
TEST(Secp256k1Signer, SignRealV2OrderDigestRecovers) {
    // 复用 eip712_v2 实盘验证过的 digest (0x56ea6e…) 不依赖私钥归属, 只验签名链路自洽。
    Bytes32 digest{};
    ASSERT_TRUE(stcpp::crypto::Bytes32FromHex(
        "0x56ea6ea81ef77b01e9defeea8688c98b516a9e38dda899f46f84d6b9459f507b", digest));
    const Bytes32 key = PrivKeyOne();
    Address eoa{};
    ASSERT_TRUE(stcpp::crypto::DeriveAddress(key, eoa));
    Signature65 sig{};
    ASSERT_TRUE(stcpp::crypto::SignDigest(digest, key, sig));
    Address rec{};
    ASSERT_TRUE(stcpp::crypto::RecoverAddress(digest, sig, rec));
    EXPECT_EQ(rec, eoa);
}

// 私钥非法 (全 0) → 返回 false, 不崩。
TEST(Secp256k1Signer, RejectsInvalidKey) {
    const Bytes32 zero{};
    Address a{};
    EXPECT_FALSE(stcpp::crypto::DeriveAddress(zero, a));
    Bytes32 digest{};
    Signature65 sig{};
    EXPECT_FALSE(stcpp::crypto::SignDigest(digest, zero, sig));
}
