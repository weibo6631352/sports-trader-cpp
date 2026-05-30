// tests/unit/test_clob_wire.cpp — Polymarket CLOB V2 wire 协议锁定
//
// Owner: GM (老雷) 2026-05-31。
// 锁 V2 body 字节 + L2 HMAC 向量 + L1 ClobAuth digest (向量用 dummy key / privkey=1 地址, 不碰真凭证)。
#include "stcpp/polymarket/clob_wire.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

std::string DigestHex(const stcpp::crypto::Bytes32& b) {
    static const char* h = "0123456789abcdef";
    std::string s = "0x";
    for (auto x : b) {
        s += h[x >> 4];
        s += h[x & 0xf];
    }
    return s;
}

}  // namespace

// base64url 往返。
TEST(ClobWire, Base64UrlRoundTrip) {
    const std::string key = "0123456789abcdef0123456789abcdef";  // 32B
    const std::string enc = stcpp::polymarket::Base64UrlEncode(
        reinterpret_cast<const std::uint8_t*>(key.data()), key.size());
    EXPECT_EQ(enc, "MDEyMzQ1Njc4OWFiY2RlZjAxMjM0NTY3ODlhYmNkZWY=");
    EXPECT_EQ(stcpp::polymarket::Base64UrlDecode(enc), key);
}

// L2 HMAC 向量 (dummy 32B key, 与 python hmac 对齐)。
TEST(ClobWire, L2SignatureVector) {
    const std::string secret_b64url = "MDEyMzQ1Njc4OWFiY2RlZjAxMjM0NTY3ODlhYmNkZWY=";
    const std::string sig = stcpp::polymarket::ComputeL2Signature(
        secret_b64url, "1700000000", "POST", "/order", "{\"a\":1}");
    EXPECT_EQ(sig, "KuAtBdxlNSRO7yFe_5Qikip_BbnrGVoJBwVUQ47TuHA=");
}

// V2 wire body 字节锁定 (signature/owner 占位)。
TEST(ClobWire, OrderBodyBytes) {
    stcpp::polymarket::OrderV2Wire o;
    o.salt = 12345;
    o.maker = "0x78de3c8264c546fffed8d9a1396cddef7c8686be";
    o.signer = "0xb9c8261f9108856970b69c440e6913a33ad438d7";
    o.token_id = "611102211317800241464556";
    o.maker_amount = 1020000;
    o.taker_amount = 51000000;
    o.is_buy = true;
    o.signature_type = 1;
    o.timestamp_ms = 1748476800000ULL;
    o.signature = "0xSIG";
    o.owner = "api-key-uuid";
    o.order_type = "FOK";
    const std::string body = stcpp::polymarket::BuildOrderV2Body(o);
    const std::string expect =
        "{\"order\":{\"salt\":12345,"
        "\"maker\":\"0x78de3c8264c546fffed8d9a1396cddef7c8686be\","
        "\"signer\":\"0xb9c8261f9108856970b69c440e6913a33ad438d7\","
        "\"tokenId\":\"611102211317800241464556\","
        "\"makerAmount\":\"1020000\",\"takerAmount\":\"51000000\","
        "\"side\":\"BUY\",\"expiration\":\"0\",\"signatureType\":1,"
        "\"timestamp\":\"1748476800000\","
        "\"metadata\":\"0x0000000000000000000000000000000000000000000000000000000000000000\","
        "\"builder\":\"0x0000000000000000000000000000000000000000000000000000000000000000\","
        "\"signature\":\"0xSIG\"},"
        "\"owner\":\"api-key-uuid\",\"orderType\":\"FOK\",\"deferExec\":false,\"postOnly\":false}";
    EXPECT_EQ(body, expect);
}

TEST(ClobWire, OrderBodySellSide) {
    stcpp::polymarket::OrderV2Wire o;
    o.is_buy = false;
    const std::string body = stcpp::polymarket::BuildOrderV2Body(o);
    EXPECT_NE(body.find("\"side\":\"SELL\""), std::string::npos);
    EXPECT_EQ(body.find("\"side\":\"BUY\""), std::string::npos);
}

// L1 ClobAuth digest 向量 (privkey=1 地址, 与 eth_account 对齐)。
TEST(ClobWire, ClobAuthDigestVector) {
    const auto digest = stcpp::polymarket::ComputeClobAuthDigest(
        "0x7e5f4552091a69125d5dfcb7b8c2659029395bdf", "1700000000", 0);
    EXPECT_EQ(DigestHex(digest),
              "0x29cc0fe956d73b8f2962f2e3939a1248a4b094e07b914f8ef1ece85c7ee7e59a");
}
