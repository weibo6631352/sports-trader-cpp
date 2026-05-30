// tests/unit/test_eip712_v2.cpp — Polymarket CTF Exchange V2 EIP-712 digest 编码锁定
//
// Owner: GM (老雷) 2026-05-31。
// 锁定值来自真实 CLOB 实盘验证 (digest 与官方 eth_account 逐字节一致, 首笔成交 orderID 0x28a666…)。
// 任何改动若动了 V2 struct/domain 编码, 此测试立即变红 → 防 V2 签名静默回归。
#include "stcpp/crypto/eip712_v2.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

using stcpp::crypto::Address;
using stcpp::crypto::Bytes32;

std::string Hex(const Bytes32& b) {
    static const char* h = "0123456789abcdef";
    std::string s = "0x";
    for (auto x : b) {
        s += h[x >> 4];
        s += h[x & 0xf];
    }
    return s;
}

// 实盘验证过的 V2 订单参数 (与 eth_account/py-clob-client-v2 digest 对齐)。
stcpp::crypto::OrderV2 ReferenceOrder() {
    stcpp::crypto::OrderV2 o;
    o.salt = stcpp::crypto::U256FromU64(12345);
    EXPECT_TRUE(stcpp::crypto::AddressFromHex("0x78dE3c8264C546Fffed8D9A1396cddEf7c8686BE", o.maker));
    EXPECT_TRUE(stcpp::crypto::AddressFromHex("0xb9c8261f9108856970b69c440e6913a33ad438d7", o.signer));
    EXPECT_TRUE(stcpp::crypto::U256FromDecimal(
        "61110221131780024146455604680603124331432024209700031479458348712198877498305", o.token_id));
    o.maker_amount = 90000;
    o.taker_amount = 5000000;
    o.side = 0;
    o.signature_type = 1;
    o.timestamp_ms = 1748476800000ULL;
    // metadata / builder = bytes32(0) (默认)
    return o;
}

}  // namespace

// keccak256 Ethereum 测试向量 (空串)。
TEST(Eip712V2, KeccakEmptyVector) {
    EXPECT_EQ(Hex(stcpp::crypto::Keccak256(std::string_view{""})),
              "0xc5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470");
}

TEST(Eip712V2, KeccakAbcVector) {
    // keccak256("abc")
    EXPECT_EQ(Hex(stcpp::crypto::Keccak256(std::string_view{"abc"})),
              "0x4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45");
}

// V2 digest 锁定 — 真实 CLOB 实盘验证过的精确值。
TEST(Eip712V2, OrderV2DigestMatchesLiveValidated) {
    const auto domain = stcpp::crypto::CtfExchangeV2Domain(/*neg_risk=*/false);
    const auto digest = stcpp::crypto::ComputeOrderV2Digest(ReferenceOrder(), domain);
    EXPECT_EQ(Hex(digest),
              "0x56ea6ea81ef77b01e9defeea8688c98b516a9e38dda899f46f84d6b9459f507b");
}

// domain 默认值正确 (name/version/exchange)。
TEST(Eip712V2, V2DomainFields) {
    const auto d = stcpp::crypto::CtfExchangeV2Domain(false);
    EXPECT_EQ(d.name, "Polymarket CTF Exchange");
    EXPECT_EQ(d.version, "2");
    EXPECT_EQ(d.chain_id, 137u);
    Address exch{};
    ASSERT_TRUE(stcpp::crypto::AddressFromHex("0xE111180000d2663C0091e4f400237545B87B996B", exch));
    EXPECT_EQ(d.verifying_contract, exch);
}

// negRisk domain 用不同 exchange → digest 不同。
TEST(Eip712V2, NegRiskDomainDiffers) {
    const auto normal = stcpp::crypto::ComputeOrderV2Digest(ReferenceOrder(),
                                                            stcpp::crypto::CtfExchangeV2Domain(false));
    const auto neg = stcpp::crypto::ComputeOrderV2Digest(ReferenceOrder(),
                                                         stcpp::crypto::CtfExchangeV2Domain(true));
    EXPECT_NE(Hex(normal), Hex(neg));
}

// U256 编码辅助。
TEST(Eip712V2, U256FromU64BigEndian) {
    const auto b = stcpp::crypto::U256FromU64(0x0102030405060708ULL);
    EXPECT_EQ(b[24], 0x01);
    EXPECT_EQ(b[31], 0x08);
    for (int i = 0; i < 24; ++i) EXPECT_EQ(b[static_cast<std::size_t>(i)], 0);
}

TEST(Eip712V2, U256FromDecimalRoundTrip) {
    Bytes32 b{};
    ASSERT_TRUE(stcpp::crypto::U256FromDecimal("255", b));
    EXPECT_EQ(b[31], 0xff);
    ASSERT_TRUE(stcpp::crypto::U256FromDecimal("256", b));
    EXPECT_EQ(b[30], 0x01);
    EXPECT_EQ(b[31], 0x00);
    // 非法 / 空
    EXPECT_FALSE(stcpp::crypto::U256FromDecimal("", b));
    EXPECT_FALSE(stcpp::crypto::U256FromDecimal("12a3", b));
}

TEST(Eip712V2, AddressFromHexRejectsBadLen) {
    Address a{};
    EXPECT_FALSE(stcpp::crypto::AddressFromHex("0x1234", a));
    EXPECT_TRUE(stcpp::crypto::AddressFromHex("0x78dE3c8264C546Fffed8D9A1396cddEf7c8686BE", a));
}
