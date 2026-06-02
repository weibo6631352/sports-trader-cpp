// tests/unit/test_live_book_publisher.cpp
//
// Owner: 老雷 (GM)
// last_review: 2026-06-02
//
// 回归测试 — 单边订单簿有效性 (2026-06-02 修):
//   现象: 临近结算/极端价市场常出现单边簿 (如网球 25 个买单 @0.999、无卖单)。
//   旧 ParseObject 用 `have_bid && have_ask` 判 valid → 单边簿整本被丢 → hub 空 →
//   前端"订单簿未接入"。直连 Polymarket /books 实证该 token 有 25 档买盘, 我方 hub found:null。
//   修法: `have_bid || have_ask` 即 valid (下游 TickOne 有自己的双边价格门 fail-closed,
//   不会拿单边乱定价)。派生量 (spread/mid/microprice/imbalance) 无双边无意义, 仍只双边算。
//
// 测试覆盖:
//   T01: 双边簿 → valid + spread/mid 已算
//   T02: 仅买单 (单边) → valid=true, bids 有值, asks 为 NaN, 派生量未算
//   T03: 仅卖单 (单边) → valid=true
//   T04: 空簿 (两边皆空) → valid=false (不污染 hub)

#include <cmath>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "src/stcpp/polymarket/clob_wss/live_book_publisher.hpp"
#include "stcpp/polymarket/clob_wss/orderbook_snapshot_hub.hpp"

namespace {

using stcpp::polymarket::clob_wss::LiveBookPublisher;
using stcpp::polymarket::clob_wss::OrderBookSnapshotHub;

// REST /books 响应格式: 数组, 每元素一个 book 对象 (asset_id + timestamp(ms) + bids[] + asks[])。
std::string MakeBookPayload(const std::string& token, const std::string& bids_json,
                            const std::string& asks_json) {
    return "[{\"asset_id\":\"" + token + "\",\"timestamp\":\"1780000000000\",\"bids\":" + bids_json +
           ",\"asks\":" + asks_json + "}]";
}

constexpr std::int64_t kRecvTs = 1780000000000LL * 1'000'000LL;  // recv_ts_ns (≥ data_source_ts)

TEST(LiveBookPublisherOneSided, T01_TwoSided_Valid_DerivedComputed) {
    OrderBookSnapshotHub hub;
    std::vector<std::string> toks{"t_two"};
    LiveBookPublisher pub(hub, toks, /*verbose=*/false);
    pub.SeedFromRestBooks(
        MakeBookPayload("t_two", R"([{"price":"0.40","size":"100"}])", R"([{"price":"0.45","size":"80"}])"),
        kRecvTs);
    auto b = hub.Read("t_two");
    ASSERT_TRUE(b.has_value());
    EXPECT_TRUE(b->valid);
    EXPECT_NEAR(b->spread, 0.05, 1e-9);   // 双边 → 派生量已算
    EXPECT_NEAR(b->mid, 0.425, 1e-9);
}

TEST(LiveBookPublisherOneSided, T02_BidsOnly_StillValid) {
    OrderBookSnapshotHub hub;
    std::vector<std::string> toks{"t_bid"};
    LiveBookPublisher pub(hub, toks, /*verbose=*/false);
    // 网球临近结算: 25 档买盘, 0 卖盘 (用 best 1 档代表)。
    pub.SeedFromRestBooks(MakeBookPayload("t_bid", R"([{"price":"0.999","size":"500"}])", R"([])"), kRecvTs);
    auto b = hub.Read("t_bid");
    ASSERT_TRUE(b.has_value());
    EXPECT_TRUE(b->valid) << "单边买盘簿必须 valid (真实流动性, 前端要显示)";
    EXPECT_NEAR(b->bids[0].price, 0.999, 1e-9);
    EXPECT_FALSE(std::isfinite(b->asks[0].price));  // 无卖盘 → NaN
    EXPECT_FALSE(std::isfinite(b->spread));          // 派生量未算 (无双边)
}

TEST(LiveBookPublisherOneSided, T03_AsksOnly_StillValid) {
    OrderBookSnapshotHub hub;
    std::vector<std::string> toks{"t_ask"};
    LiveBookPublisher pub(hub, toks, /*verbose=*/false);
    pub.SeedFromRestBooks(MakeBookPayload("t_ask", R"([])", R"([{"price":"0.02","size":"300"}])"), kRecvTs);
    auto b = hub.Read("t_ask");
    ASSERT_TRUE(b.has_value());
    EXPECT_TRUE(b->valid) << "单边卖盘簿必须 valid";
    EXPECT_NEAR(b->asks[0].price, 0.02, 1e-9);
    EXPECT_FALSE(std::isfinite(b->bids[0].price));
}

TEST(LiveBookPublisherOneSided, T04_EmptyBook_Invalid) {
    OrderBookSnapshotHub hub;
    std::vector<std::string> toks{"t_empty"};
    LiveBookPublisher pub(hub, toks, /*verbose=*/false);
    pub.SeedFromRestBooks(MakeBookPayload("t_empty", R"([])", R"([])"), kRecvTs);
    auto b = hub.Read("t_empty");
    // 两边皆空 → 不应判 valid (否则污染 hub / 误导前端)。
    if (b.has_value()) {
        EXPECT_FALSE(b->valid) << "空簿不得 valid";
    }
}

}  // namespace
