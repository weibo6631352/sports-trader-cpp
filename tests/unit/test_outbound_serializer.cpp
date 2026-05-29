// tests/unit/test_outbound_serializer.cpp — A-NET-01 出站序列化单测
//
// Owner: 老陈 (A-NET-01, #03)  Last review: 2026-05-29
//
// 覆盖:
//   T1: OutboundBuffer reset O(1) + append 正确性 + view() 生命期
//   T2: OutboundBuffer 容量边界 (满 → append 返 false, 不越界)
//   T3: SerializeSignedOrder 正确性 (BUY + SELL + price/size 换算 + signatureType=1)
//   T4: SerializeSignedOrder HMAC bug #3 (signatureType 必须是整数 1, 不是字符串)
//   T5: OutboundSubmitQueue 背压 drop (满 256 → try_push 返 false + drop_count++)
//   T6: OutboundSubmitQueue SPSC FIFO 顺序
//   T7: SerializeCancelAll → "{}"
//   T8: SerializeSignedOrder 大 token_id (uint256 string) + 空 signature (paper mode)

#include <cstdint>
#include <string>
#include <string_view>
#include <thread>

#include <gtest/gtest.h>

#include "stcpp/net/outbound_buffer.hpp"
#include "stcpp/net/outbound_queue.hpp"
#include "stcpp/net/outbound_serializer.hpp"
#include "stcpp/polymarket/pm_client.hpp"

namespace {

using namespace stcpp::net;
using namespace stcpp::polymarket;

// 辅助: 构造最小合法 SignedOrder
SignedOrder MakeOrder(std::uint8_t side = 0) {
    SignedOrder o;
    o.condition_id = "0xabc123def456abc123def456abc123def456abc123def456abc123def456abcd";
    o.token_id = "12345678901234567890123456789012345678901234567890";
    o.side = side;
    o.limit_price_bps = 5500;        // 0.5500
    o.size_usdc_micro = 10'000'000;  // 10.000000 USDC
    o.expiration_unix_s = 0;         // GTC
    o.signature_type = 1;
    o.signature = "AAAA+base64sig==";
    o.maker_address = "0xDeadBeefDeadBeefDeadBeefDeadBeefDeadBeef";
    return o;
}

// ============================================================
// T1: OutboundBuffer reset O(1) + append 正确性 + view()
// ============================================================
TEST(OutboundBuffer, ResetAndAppend) {
    OutboundBuffer buf;
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.size(), 0u);

    EXPECT_TRUE(buf.append("hello"));
    EXPECT_EQ(buf.size(), 5u);
    EXPECT_EQ(buf.view(), "hello");

    EXPECT_TRUE(buf.append(','));
    EXPECT_EQ(buf.size(), 6u);
    EXPECT_EQ(buf.view(), "hello,");

    // reset: 游标归零, view 为空
    buf.reset();
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.size(), 0u);
    EXPECT_EQ(buf.view(), "");

    // 重新写入
    EXPECT_TRUE(buf.append("world"));
    EXPECT_EQ(buf.view(), "world");
}

// ============================================================
// T2: OutboundBuffer 容量边界
// ============================================================
TEST(OutboundBuffer, CapacityBoundary) {
    OutboundBuffer buf;
    const std::size_t cap = kMaxOutboundJson;  // 4096

    // 填满到 cap - 1
    std::string big(cap - 1, 'X');
    EXPECT_TRUE(buf.append(big));
    EXPECT_EQ(buf.size(), cap - 1);

    // 再加一个 char — 刚好满
    EXPECT_TRUE(buf.append('Y'));
    EXPECT_EQ(buf.size(), cap);

    // 再加一个 char — 溢出, 返 false, buf 不变
    EXPECT_FALSE(buf.append('Z'));
    EXPECT_EQ(buf.size(), cap);

    // 加字符串 — 同样溢出
    EXPECT_FALSE(buf.append("overflow"));
    EXPECT_EQ(buf.size(), cap);

    // reset 后可重新写
    buf.reset();
    EXPECT_TRUE(buf.empty());
    EXPECT_TRUE(buf.append("fresh"));
}

// ============================================================
// T3: SerializeSignedOrder BUY 正确性
// ============================================================
TEST(OutboundSerializer, SerializeSignedOrderBuy) {
    OutboundBuffer buf;
    SignedOrder o = MakeOrder(0);  // BUY

    ASSERT_TRUE(OutboundSerializer::SerializeSignedOrder(o, buf));
    std::string json{buf.view()};

    // 检查关键字段存在且格式正确
    EXPECT_NE(
        json.find(R"("conditionId":"0xabc123def456abc123def456abc123def456abc123def456abc123def456abcd")"),
        std::string::npos);
    EXPECT_NE(json.find(R"("tokenId":"12345678901234567890123456789012345678901234567890")"),
              std::string::npos);
    EXPECT_NE(json.find(R"("side":"BUY")"), std::string::npos);
    EXPECT_NE(json.find(R"("price":0.5500)"), std::string::npos);
    EXPECT_NE(json.find(R"("size":10.000000)"), std::string::npos);
    EXPECT_NE(json.find(R"("expiration":0)"), std::string::npos);
    EXPECT_NE(json.find(R"("signatureType":1)"), std::string::npos);
    EXPECT_NE(json.find(R"("signature":"AAAA+base64sig==")"), std::string::npos);
    EXPECT_NE(json.find(R"("makerAddress":"0xDeadBeefDeadBeefDeadBeefDeadBeefDeadBeef")"), std::string::npos);

    // 开头 '{' 结尾 '}'
    EXPECT_EQ(json.front(), '{');
    EXPECT_EQ(json.back(), '}');

    // compact: 无换行无多余空格 (禁 pretty-print)
    EXPECT_EQ(json.find('\n'), std::string::npos);
    EXPECT_EQ(json.find("  "), std::string::npos);
}

// ============================================================
// T3b: SerializeSignedOrder SELL
// ============================================================
TEST(OutboundSerializer, SerializeSignedOrderSell) {
    OutboundBuffer buf;
    SignedOrder o = MakeOrder(1);  // SELL

    ASSERT_TRUE(OutboundSerializer::SerializeSignedOrder(o, buf));
    std::string json{buf.view()};
    EXPECT_NE(json.find(R"("side":"SELL")"), std::string::npos);
}

// ============================================================
// T4: HMAC bug #3 — signatureType 必须是整数 1, 不是字符串 "1"
// ============================================================
TEST(OutboundSerializer, SignatureTypeIsInteger) {
    OutboundBuffer buf;
    SignedOrder o = MakeOrder(0);
    ASSERT_TRUE(OutboundSerializer::SerializeSignedOrder(o, buf));
    std::string json{buf.view()};

    // 正确: "signatureType":1  (整数)
    EXPECT_NE(json.find(R"("signatureType":1)"), std::string::npos)
        << "HMAC bug #3: signatureType must be integer 1";

    // 错误模式不能出现: "signatureType":"1" (字符串)
    EXPECT_EQ(json.find(R"("signatureType":"1")"), std::string::npos)
        << "HMAC bug #3 violated: signatureType must NOT be a string";
}

// ============================================================
// T5: OutboundSubmitQueue 背压 — 满 256 时 drop + drop_count++
// ============================================================
TEST(OutboundSubmitQueue, BackpressureDrop) {
    OutboundSubmitQueue q;
    EXPECT_EQ(q.drop_count(), 0u);
    EXPECT_EQ(q.capacity(), 256u);

    // push capacity 个 item (ring 有 257 slots — 256 可用, 1 sentinel)
    std::uint64_t pushed = 0;
    for (std::size_t i = 0; i < q.capacity(); ++i) {
        OutboundSubmitItem item;
        item.path = "/clob/orders";
        item.json_body = "{}";
        item.seq = static_cast<std::uint64_t>(i);
        if (q.try_push(std::move(item))) {
            ++pushed;
        }
    }
    // 至少 push 了 capacity-1 个 (sentinel slot)
    EXPECT_GE(pushed, q.capacity() - 1);

    // 再 push 一个 — 必须 drop (队列已满)
    OutboundSubmitItem overflow;
    overflow.path = "/clob/orders";
    overflow.json_body = "{\"overflow\":true}";
    overflow.seq = 9999;
    EXPECT_FALSE(q.try_push(std::move(overflow)));
    EXPECT_GE(q.drop_count(), 1u);
}

// ============================================================
// T6: OutboundSubmitQueue SPSC FIFO 顺序
// ============================================================
TEST(OutboundSubmitQueue, FifoOrder) {
    OutboundSubmitQueue q;

    // push 5 items
    for (std::uint64_t i = 0; i < 5; ++i) {
        OutboundSubmitItem item;
        item.seq = i;
        item.path = "/clob/orders";
        item.json_body = "{}";
        EXPECT_TRUE(q.try_push(std::move(item)));
    }

    // pop 5 items — 顺序必须 FIFO
    for (std::uint64_t i = 0; i < 5; ++i) {
        OutboundSubmitItem out;
        ASSERT_TRUE(q.try_pop(out));
        EXPECT_EQ(out.seq, i) << "FIFO order violated at i=" << i;
    }

    // queue now empty
    OutboundSubmitItem dummy;
    EXPECT_FALSE(q.try_pop(dummy));
}

// ============================================================
// T7: SerializeCancelAll → "{}"
// ============================================================
TEST(OutboundSerializer, SerializeCancelAll) {
    OutboundBuffer buf;
    ASSERT_TRUE(OutboundSerializer::SerializeCancelAll(buf));
    EXPECT_EQ(buf.view(), "{}");
}

// ============================================================
// T8: paper mode — 空 signature + 大 token_id
// ============================================================
TEST(OutboundSerializer, PaperModeEmptySignature) {
    OutboundBuffer buf;
    SignedOrder o = MakeOrder(0);
    o.signature = "";  // paper mode: signature 留空

    // 256-bit uint256 decimal string (最大 77 位)
    o.token_id = "115792089237316195423570985008687907853269984665640564039457584007913129639935";

    ASSERT_TRUE(OutboundSerializer::SerializeSignedOrder(o, buf));
    std::string json{buf.view()};

    // 空 signature → ""
    EXPECT_NE(json.find(R"("signature":"")"), std::string::npos);
    // 大 token_id 完整保留
    EXPECT_NE(json.find("115792089237316195423570985008687907853269984665640564039457584007913129639935"),
              std::string::npos);
    // 仍然合法 JSON (开头结尾)
    EXPECT_EQ(json.front(), '{');
    EXPECT_EQ(json.back(), '}');
}

// ============================================================
// T9: price / size 精度验证
// ============================================================
TEST(OutboundSerializer, PriceSizePrecision) {
    OutboundBuffer buf;
    SignedOrder o = MakeOrder(0);
    o.limit_price_bps = 1;  // 0.0001 (最小 tick)
    o.size_usdc_micro = 1;  // 0.000001 USDC (最小)

    ASSERT_TRUE(OutboundSerializer::SerializeSignedOrder(o, buf));
    std::string json{buf.view()};

    // price = 1/10000 = 0.0001
    EXPECT_NE(json.find(R"("price":0.0001)"), std::string::npos)
        << "min price precision failed, got: " << json;

    // size = 1/1000000 = 0.000001
    EXPECT_NE(json.find(R"("size":0.000001)"), std::string::npos)
        << "min size precision failed, got: " << json;
}

// ============================================================
// T10: buffer reset 后可重复使用
// ============================================================
TEST(OutboundBuffer, ReusableAfterReset) {
    OutboundBuffer buf;
    SignedOrder o = MakeOrder(0);

    // 第一次序列化
    ASSERT_TRUE(OutboundSerializer::SerializeSignedOrder(o, buf));
    std::size_t first_size = buf.size();
    EXPECT_GT(first_size, 0u);

    buf.reset();

    // 第二次序列化 (side=SELL)
    o.side = 1;
    ASSERT_TRUE(OutboundSerializer::SerializeSignedOrder(o, buf));
    EXPECT_GT(buf.size(), 0u);

    std::string json{buf.view()};
    EXPECT_NE(json.find(R"("side":"SELL")"), std::string::npos);
}

}  // namespace
