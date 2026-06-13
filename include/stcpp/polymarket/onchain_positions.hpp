// include/stcpp/polymarket/onchain_positions.hpp — 链上持仓读取 (live 启动对账)
//
// owner: 老雷 (GM) | 2026-06-13
// 背景 (真钱事故配套): live sync 记账曾漏 → 链上有真仓但 PositionLedger 空 → cap 失明 → 累积穿透。
//   正解三件之一: live 启动时读链上【未结算 open】持仓 → seed 进 PositionLedger, 让 cap 一开机即见真敞口。
//   链上 = 持仓唯一真相 (data-api 报我们 funder 的实际 ERC1155 余额, 不受本地记账 bug 影响)。
//
// 数据源: data-api.polymarket.com/positions?user=<funder> (REST, 只读)。Mozilla UA 必带 (默认 UA 被 403)。
//   字段 (2026-06-13 实测): asset(token_id) / conditionId / size(股) / avgPrice / outcomeIndex(0=YES/1=NO) /
//                          redeemable(已结算) / negativeRisk。
//
// §8 红线: 只读 REST, 不签名不动私钥; funder 地址是公开链上信息, 非敏感。
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace stcpp::polymarket {

// 一笔链上【未结算 open】持仓 (redeemable=false)。
struct OnchainPosition {
    std::string token_id;       // "asset" (decimal ERC1155 outcome token)
    std::string condition_id;   // "conditionId" (0x...)
    int outcome_index{0};       // "outcomeIndex": 0=YES(第一 outcome) / 1=NO(第二)
    double shares{0.0};         // "size" (股数, >0)
    double avg_price{0.0};      // "avgPrice" (该 token 均价, 0..1)
    bool neg_risk{false};       // "negativeRisk"
};

// 读 funder 的【未结算 open】持仓 (跳过 redeemable=true 的已结算仓)。
//   funder_hex: "0x..." 20 字节地址 (POLYMARKET_FUNDER_ADDRESS)。
//   返回 nullopt = 读取/解析失败 (网络/HTTP/JSON); 调用方必须 fail-safe (别拿空当"无仓" → 反而该拒开闸/告警)。
//   返回空 vector = 成功且确无 open 仓。
[[nodiscard]] std::optional<std::vector<OnchainPosition>> ReadOpenPositions(const std::string& funder_hex,
                                                                            std::string& err) noexcept;

// 纯解析 (可测, 无网络): data-api positions JSON 数组 → 各【open】仓 (跳 redeemable + 健全性过滤)。
//   ReadOpenPositions 取回 HTTP body 后调它。body 须以 '[' 起 (数组)。
[[nodiscard]] std::vector<OnchainPosition> ParseOpenPositions(std::string_view json_body) noexcept;

}  // namespace stcpp::polymarket
