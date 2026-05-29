# 后端观测 API — Polymarket 协议维度暴露清单 v1

> owner: 老李 (#07, polymarket-protocol-expert)
> last_review: 2026-05-29
> 关联: `pm_client.hpp` ABI LOCK v1 / `polymarket_clob_subscriber.hpp` (小冯 Wave 79) / debug_api skeleton (小卢 W9)
> 视角: 仅 Polymarket 协议/数据。wire 实现 @老周, JSON parse @序列化, schema watch @通用 API watch。

## 1. market / 盘口状态 endpoint `GET /pm/market/{condition_id}`
直出 `MarketInfo` (F-05) 全字段, 观测/调试用:
- `condition_id` (CTF bytes32) + 每 outcome 的 `token_id` (ERC1155 uint256 string) + `name`
- `tick_size_bps` (默认 10 = 0.001; 注意可被 `tick_size_change` 事件改, 必须实时反映)
- `fee_rate_bps` (taker 默认见 §4) + `neg_risk` (CTF v2 标识) + `accepting_orders`
- 市场状态机: `active` / `accepting_orders` / `closed` / `resolved` — 三态分开暴露, 不要折叠成一个 bool
- `game_start_time_unix_s` + `liquidity_usdc` + `clob_token_ids` 双 token + 每 outcome `last_price_bps` / `volume_24h`
- 必带 `TimestampQuad` 4 ts + `ds_ts_source` (R-20)

## 2. 订单簿 endpoint `GET /pm/book/{condition_id}`
直出 `OrderBookSnapshot` (F-01, L5 双侧) + 派生质量指标:
- `yes_bids[5]` / `yes_asks[5]` 各档 `price_bps` + `size_usdc_micro` + `level_ts_ns`
- 派生 (后端算, 别让调试方自己算): `microprice` = (bid·ask_sz + ask·bid_sz)/(sz合), `spread_bps`, `imbalance` = (bid_sz−ask_sz)/(和), best_bid/best_ask
- **行情质量观测核心**: 暴露 CLOBSubscriber 的 `sequence_no` (每 token 最新) + gap 计数 + `snapshot_received` flag + `last_market_msg_ts_ns` / `last_user_msg_ts_ns` (RM STALE 同源) + WSS `market_state` / `user_state` + reconnect_attempt + drop metric
- 双 token 提醒: YES/NO 两 token_id 应同订, 暴露两侧订阅状态

## 3. 订单生命周期 trace `GET /pm/order/{client_order_id}`
全链路 trace (调试最关键), 按时间线串:
- OrderIntent → 签名 (sigType=1 Magic Safe; sig 必 redact) → 提交 → ack → fill/reject/cancel
- `OrderStatus` 7 态机 (Booked/PartiallyFilled/Filled/Canceled/Expired/Rejected/Settled) + 每次转移留 ts + 校验 `IsLegalTransition`
- `OrderAck`: `order_id` (server UUID) / `client_order_id` echo / `nonce` (server-issued) / `reject_reason` (Rejected 必带) / `PMError` (kind+http_status+redact body)
- fill 明细 `Trade[]`: `trade_id` / `price_bps` / `size_usdc_micro` / `fee_usdc_micro` (实收) / `match_time_ns`
- paper/live 隔离标记 `audit_wal_kind` (R-11, paper 硬绑 PaperAudit)

## 4. fee / 结算可观测 `GET /pm/settlement/{condition_id}`
- C2 fee: 暴露 `fee_rate_bps` 名义 vs `Trade.fee_usdc_micro` 实收, 二者并列 (实收 ≠ 名义×size, Polymarket 取 min(yes,no) 侧计费, 必须看实收)
- 结算: `Position.redeemable` (已 settle 等 redeem) / `mergeable` (CTF mergePositions) / `OrderStatus::Settled` (~4h 延迟) / neg_risk 转换状态

## 5. gotcha (易误读 / ToS)
- `price` 是概率非美元, 0..1 → 内部 `price_bps` 0..10000; size 是 USDC notional 非股数
- `token_id` ≠ `condition_id`; market channel 订 token_id, user channel 订 condition_id (P-07 易错)
- fee 看实收别按名义算; `Settled` 有数小时延迟, 别当卡死
- 401 走 v3 §B SOP, 5min 内禁说 "key 失效" (可能 clock skew)
- **ToS 红线**: book/trade 数据不可对外 reselling; 观测 API 仅内网, 不暴露公网; REST poll 必自我限流 (429 退避), debug 端点别变相高频拉 Polymarket 当代理
