// include/stcpp/polymarket/onchain_balance.hpp — 链上 pUSD 余额读取 (live 真实本金)
//
// owner: 老雷 (GM) | 2026-06-12
// 背景: live 账户净值此前用写死 bankroll=150 (live_risk_profile.hpp), 不反映真实钱包。
//   本读取器在 daemon 启动(live)时打 Polygon RPC 读 pUSD.balanceOf(funder) → 作为真实本金起点。
//
// pUSD (Polymarket USD, 抵押币) 合约: 0xc011a7e12a19f7b1f670d46f03b03f3342e82dfb (6 位小数)。
//   ERC20 balanceOf(address) selector = 0x70a08231。
// RPC: 默认 publicnode (libcurl 默认 UA 可过; 注 Python-urllib UA 被拦)。POLYGON_RPC_URL 可覆盖。
//
// §8 红线: 只读余额, 不签名不动私钥。返回值仅本金, 不含敏感信息。
#pragma once

#include <optional>
#include <string>

namespace stcpp::polymarket {

// 读 funder 地址的 pUSD 余额 (USD, 已 /1e6)。
//   funder_hex: "0x..." 20 字节地址 (POLYMARKET_FUNDER_ADDRESS)。
//   rpc_url:    Polygon RPC (空 → 默认 publicnode)。
//   返回 nullopt = 读取失败 (网络/解析/RPC error); 调用方必须 fail-safe (别拿 0 当本金)。
[[nodiscard]] std::optional<double> ReadPusdBalanceUsd(const std::string& funder_hex,
                                                       const std::string& rpc_url,
                                                       std::string& err) noexcept;

}  // namespace stcpp::polymarket
