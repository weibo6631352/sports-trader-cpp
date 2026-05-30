# Sprint-1 Retro — 老李 (Polymarket 协议) 发言

- Speaker: 老李 (polymarket-protocol-expert)
- Date: 2026-05-28
- 上下文: Sprint-1 Retro Batch 2, 读完 Batch 1 八位 (老周 / 老韩 / 小梁 / 小余 / 老胡 / 老钱 / 老郭 / 老黄)
- 行长: ≤ 250 行
- 状态: 真发言, 不护短

---

## 0. 三句话开场

1. **HMAC 我 Sprint-1 自己写错 4 处**, 不护短, 当面认. 我 v1 spec 把 querystring / param_type / sigType / strip-padding 全留坑, 害得老孙 Rust 实现要返工, 老郭跨文档扫才捞出来 — 这是协议 owner 的硬伤, 不是"小笔误".
2. **vCPU0 单 reactor vs WSS 11 connection** — 老周 Escalate-1 我必须当面回. 实测数据我有, 拆 2-3 connection 我同意, 但**不是按 sport 拆 5-8 个**, 太碎.
3. **5¢ 阈值 + small bet 0¢ 滑点** — 我端 endpoint 这周能给小梁验证窗口数据, 不光是小袁 264 样本的复述.

---

## 1. 我读了 Batch 1 八位什么

| 发言人 | 我接 (Agreed) | 我有保留 (Concern) | 我驳 (Reject) |
|---|---|---|---|
| 老周 v0.3 / 生命周期 v1 | §17 R-12 落地 / §17.5 bulk 路由引用我矩阵 / §17.1.1 T0 Polymarket reactor / 生命周期 v1 §2.A.2 推 2 conn 分离 | §3 #4 Escalate-1 (vCPU0 11 connection 压测) **我有补充数据**, §1 推荐 N conn 单 reactor 而不是多 reactor | — |
| 老韩 v0.2 | §3.10 4 reject enum / §7 PER_ORDER_CAP 接小梁 6/4 数 / §11 STALE 三档分级 | Q1 hot token STALE 500ms/2000ms 我不反对, 但**hot 判定要看 token-level event_rate, 这需要我 endpoint 实测能给** | — |
| 小梁 (P0-01 改 5¢) | §2.2 阈值算账逻辑全对 / Pinnacle 路径 A/B/C 三线 / vendor health metric 我配合埋 | §2.1 "key 失效会被 fallback 链兜底" 我**部分同意, 但 vendor health metric 我承诺埋满** — 见 §5 | — |
| 小余 (数据契约) | §5 我 5 endpoint 决策路径接得住 / §5.3 v1 spec 4 处错小余已标 known issue / §2 兜底架构 / 客户端 diff blake3 | §5.2 痛点 "/books 500 batch silent skip 229/500" 我 owner, 见 §6 | — |
| 老胡 (PM) | M4.5 9/12 三道闸子 / R-01 降级但不关 / Sprint-2 三件大事 P0 | §4.5 老韩 RM 实现并行启动我配合 — endpoint 矩阵 v2 不卡 RM v0.3 | — |
| 老钱 (CPO) | §6 拒绝清单 #2 maker rebate 不顺便开 / §3 7 gate 全收 / §4 §7.4 alpha 容量 $5-20M | §5 P0 "Pinnacle 数据源闭环 6/12 决议" — 我做 endpoint 端我配合, 但**vendor 选择是商务决定**, 我不替老彭 + 小梁拍 | — |
| 老郭 (架构评审) | §4 跨文档扫 / §1.3 ADR-001 升 Accepted final / **§4 裁定 L-5 倾向 2 conn (故障域隔离)** | **L-5 我有更精细方案**: market channel 内 2-3 conn (按 token 数分片), user channel 1 conn, 一共 3-4 conn, 不是 1/2 二选一. 见 §3 | — |
| 老黄 (合规) | §2 Goalserve odds 商务 timeline / §2.3 R9 数据转售边界缓存 > 7 天红线 | §2.1 Goalserve odds **数据契约**老胡负责销售但**字段语义 + ToS 我配合 review**, 见 §7 | — |

---

## 2. HMAC 4 bug — 我公开吃下教训

**老雷 ADR 已经书面记过这条**, 我现在当面再说一遍, 不只是把责任分摊给"vendor doc 不一致".

### 2.1 我写错的 4 处 (复述, 不洗)

| # | 错处 | v1 我写 | 实测正解 | 影响半径 |
|---|---|---|---|---|
| 1 | HMAC base string 含 querystring | "path only" | path + sorted_querystring | 任何带 cursor / next_cursor 的 endpoint 全 401 |
| 2 | sigType (Magic Safe) | =2 | =1 (1-of-1 Magic) | proxy 路径全签错, signer reject |
| 3 | activity query 字段名 | param_type=...&type=... | asset_type / type 二选一 | /activity 全 400 |
| 4 | base64 strip padding | rstrip(=) | 保留 `=` padding | HMAC mismatch 401 间歇 (encode 决定) |

**老雷 ack 我"key 失效"也草率了** — 是的, 我当时只看 401 status 没看 body, 第一反应是 "key 过期" 不是 "签名算错". 这是协议 owner 的**调试纪律**问题, 不只是文档问题.

### 2.2 我承诺的补救 (Sprint-2 内, 不拖)

| # | 承诺 | 截止 | 验收 |
|---|---|---|---|
| 1 | endpoint matrix v3 加 "HMAC 签名实例" 附录 (每 endpoint 一条 curl + 一条 base string + 一条 signature, 全部我亲手跑过) | Sprint-2 W2 (6/19) | 老孙 review, 任一行复现失败 = 我返工 |
| 2 | HMAC test vector 14 条 (含 cursor / 含 query / 不含 query / negRisk / proxy / EOA / browser_wallet) 给老孙做 wire 单测 | Sprint-2 W1 (6/13) | 老孙 binary 跑 14/14 pass |
| 3 | "401 调试 SOP" 写进 endpoint matrix: 看 body / 看 base string / 对比官方 SDK 输出 / 看 padding / 最后才是 key — **顺序固化** | Sprint-2 W1 | 小米归档 + 老练 (testing-coach) 加 CI 假阴性扫 |
| 4 | 每月对官方 SDK (py-clob-client) latest tag diff, 任一字段语义变化 24h 内报 ADR (跟老雷 longterm sweep policy 联动) | 月度 cadence 起 6/19 | 月度 sweep PR 我主笔 |

**不洗的话**: HMAC 是协议 owner 的最低门槛, 我连这都写错 4 处, 没资格抱怨别人不细心. 我接 Q21 自动化思路 — endpoint matrix v3 的 HMAC 附录上 CI, 任一 endpoint 签名生成与官方 SDK diff > 0 = pipeline 红.

---

## 3. 老周 Escalate-1: vCPU0 单 reactor vs WSS 11 connection — 我的实测态度

老周 §3 #4 + §4 Escalate-1 把这条拍我脸上, 我领. 但我有数据补.

### 3.1 我实测的 (Sprint-1 内跑过)

- 单 WSS connection 上限 **≥ 500 token sub** (我实测到 1000+ 未压, 但 server 端**静默 token = orderbook empty 占比 ~ 46%** — 即 500 个 sub 里有 229 个没 book event)
- 单 connection 高活跃 token 50 个时 message rate ~ 63 msg/s, p99 message size < 8KB
- MLB 1977 markets × 2 tokens (YES/NO) = **3954 token**, 不是 500 一个 connection 能接
- NBA 季后赛 ~ 702 markets × 2 = 1404 token
- NBA + MLB 同时跑 = 5358 token, **理论 11 connection**

### 3.2 我对老周三选项的具体回答

老周 §4 给 (A) 单线程多 connection / (B) vCPU0 拆 T0a/T0b / (C) MVP 单 sport.

**我推荐 (A) 的精细版, 不是 (B) 也不是 (C)**:

| 拆法 | conn 数 | 理由 |
|---|---|---|
| Polymarket market channel | **2-3 conn** (按 token 数分片, hot/cold 分离) | hot conn 跑临场 ±10min token (小袁 hot 判定), cold conn 跑 pregame far; hot conn 数据多但 token 少, p99 < 50us 可保; cold conn token 多但数据少 |
| Polymarket user channel | **1 conn** | 故障域隔离 (老郭 L-5 倾向 2 conn 的底层理由) |
| Polygon | **1 conn × 3 sub** | 老叶 v1 §3.2 已实证 |
| **总计** | **4-5 conn** 单 reactor | 不是 11 conn, 也不是 2 conn |

### 3.3 同 reactor 跑 N connection 行不行?

**行**, 前提两条:
1. 每 connection 独立 simdjson on-demand parser (栈对象, 不抢一个全局 parser)
2. message dispatch SPSC ring 是 per-connection 的, 不共享 (老周 §6 ring 拓扑里 wss_in_ring 要拆成 wss_in_ring_market_hot / wss_in_ring_market_cold / wss_in_ring_user)

**实测验证我承诺给老姜**: Sprint-2 W3 跑 4-5 connection burst, 给 vCPU0 p99 < 50us 数据. 老周 §5 #3 已经约我做, 我接.

### 3.4 我不签 (C) MVP 单 sport 的理由

小杜 PRD F-01 + 小程 P0-01 + 老钱 §4 都假设 NBA + NFL + MLB 三 sport. (C) 锁单 sport 是 scope 撤回, 不是工程妥协 — 走老钱 §5.2 战略升级路径, 不是我和老周拍.

---

## 4. 老郭 L-5 (Polymarket WSS 1 conn vs 2 conn) — 我的精细方案

老郭 §4 裁定 "倾向 2 conn (故障域隔离)", 老韩同. 我**不反对 2 conn 二选一**, 但我提**3-4 conn 分片**更好:

```
WSS 拓扑 (我推荐 v0.4 §17.1.1 落):
  T0a: market_hot_reactor (1 conn, ~500 hot token, 临场 ±10min) — vCPU0
  T0b: market_cold_reactor (1-2 conn, ~3500 cold token, pregame_far + outright) — vCPU0
  T0c: user_reactor (1 conn, user channel) — vCPU0
  T1:  polygon_reactor (1 conn × 3 sub, eth_subscribe) — vCPU0

vCPU0 总线程 = 3-4 个 connection × asio coroutine, 单 reactor 复用
```

**这与老郭 L-5 不冲突**, 是 "2 conn" 的精细化 (market 拆 hot/cold = 2 个 connection, user 单独 = 3 个, 加 Polygon 是 4 个).

**老周 v0.4 §17.1.1 我建议这么落, 等老周点头.**

---

## 5. 小梁 P0-01 5¢ 阈值 + small bet 0¢ 滑点 — 我端 endpoint 能验证什么

小梁 §2.2 算账逻辑全对, 5¢ - 3% fee - 0¢ 滑点 = 2¢ 净 edge, 没问题. 但我端 endpoint 能给的不只是小袁 264 样本的复述.

### 5.1 我承诺给小梁的实测数据 (Sprint-2 W1 内)

| 数据 | 来源 endpoint | 验证什么 |
|---|---|---|
| Pinnacle no-vig vs Polymarket midprice 偏离 > 5¢ 的**触发频率** (每日 / 每 sport / 每 game state) | gamma /events + data /trades + Pinnacle (老彭路径定后) | 5¢ 阈值的 trade/day 估算 |
| gameday $2K 单**实际 best ask depth 一档**深度分布 | clob /book endpoint snapshot | 验证小袁 0¢ 滑点是否站得住 |
| /trades endpoint **真实成交价分布** vs midprice 时差对照 | data /trades + market data | EDGE_NEGATED_BY_SLIPPAGE 触发率预估 |
| activity.type = TRADE 里 taker_side 占比 (我们做 taker, 知道 taker 流是谁的钱) | data /activity?type=TRADE | counterparty health (是 sharp / 是 dumb / 是 maker bot) |

### 5.2 vendor health metric (小梁 §2.1 要的)

我配合小冯埋:
- `pinnacle.last_update_age_ms` (策略层兜底, 见小梁 P0-01 §3.1)
- `pinnacle.401_403_rate_5m`
- `polymarket.gamma_p99_latency_ms`
- `polymarket.clob_p99_latency_ms`
- `polymarket.wss_msg_rate_per_token_60s` (per-token freshness, 联老韩 hot 判定)

**这不是 rotation alert, 是 vendor health.** 小梁 §2.1 区分得对.

---

## 6. 小余 §5.2 痛点 "/books 500 batch silent skip 229/500" — 我 owner 回应

小余把这个直接点我名. 我领.

**问题复述**: clob /books 500 batch POST, 229/500 token 返回空 (server 端 orderbook empty), client 不知道是哪 229 个 — silent skip 没有 reason code.

**我的解法 (Sprint-2 W2 内交付)**:

| 步骤 | 内容 | Owner |
|---|---|---|
| 1 | 从 gamma /events + sampling-simplified-markets endpoint 拉 "active token 池" (active = 24h 有 trade or active=true) | 我 + 小冯 |
| 2 | /books 只对 active 池打, 预期 hit rate > 95% (不是 54%) | 我 |
| 3 | 仍然 empty 的少数 (~5%) 走 WSS subscribe 后等 book event (book event 不到 = 真 empty, 跳过) | 小冯 |
| 4 | endpoint matrix v3 把 "/books silent skip" 写成 known issue + 解法 | 我 |

**对小余兜底**: ETL 落库时 empty orderbook 不算 ETL 错误, 落 `book_state=EMPTY` enum, 不当 missing data 报警.

---

## 7. 老黄 Goalserve odds 0 字节 — 我配合小段做数据契约

老黄 §2 接 owner 商务路径, 我配合**数据契约**, 但 Pinnacle 对接**不归我**, 这条要说清楚.

### 7.1 我能配合的 (Sprint-2 内)

| 内容 | 我 |
|---|---|
| Pinnacle endpoint 字段语义 (如果走路径 A Pinnacle API) | 配合 — 我以前对接过 Pinnacle XML feed, 字段语义我知道 |
| odds 字段在 Polymarket 端的等价映射 (Pinnacle moneyline → Polymarket YES/NO) | 我主笔 |
| no-vig 公式审 (老彭 multiplicative 方法) | 配合 — 我看小梁 §3.3 公式 OK, 我端只验证数据可获取性 |
| Goalserve XML/JSON odds 字段 ToS 边界审 (R9 数据转售 7 天缓存) | 配合老黄 + 小段, 我管字段级 |

### 7.2 我不接的

- **Pinnacle vendor 商务对接** — 老黄 §2.2 timeline 走老黄 + 老胡, 我不替他俩拍 vendor
- **The Odds API ($500/月) vs Pinnacle 直连选择** — 是商务决定, 不是协议决定
- **路径 C 老彭手工 CSV** — 不是 endpoint, 我不管

### 7.3 我给小梁 + 老彭的数据契约 (Sprint-2 W1 草案)

```
fair_value_anchor:
  - source: enum {PINNACLE_API, PINNACLE_OPENING, THE_ODDS_API, MANUAL_CSV, POLYMARKET_MIDPRICE}
  - vig_strip_method: enum {MULTIPLICATIVE, SHIN, POWER}
  - last_update_ts_ms: int64
  - confidence: float [0,1]  # 来源置信, MANUAL_CSV < THE_ODDS_API < PINNACLE_API
  - is_fallback: bool  # true 则走兜底链
```

这是我端字段语义, 商务 vendor 老黄 + 老胡 选哪个, 数据契约都一样.

---

## 8. Sprint-2 我承诺做什么

| # | 承诺 | 截止 | 度量 |
|---|---|---|---|
| 1 | endpoint matrix v3 (含 HMAC test vector 14 条 + curl + base string + signature 三件套) | Sprint-2 W2 (6/19) | 老孙 binary 跑 14/14 pass, 老郭 review |
| 2 | "401 调试 SOP" + 月度官方 SDK diff sweep | Sprint-2 W1 (6/13) | 小米归档, 老练 CI 接 |
| 3 | vCPU0 4-5 connection 拓扑实测 (与老姜 + 老周联跑) | Sprint-2 W3 (6/22) | p99 < 50us, 老周 v0.4 §17.1.1 落 |
| 4 | /books silent skip 解法 (active 池过滤) | Sprint-2 W2 | hit rate > 95%, 小余 ETL 落 EMPTY enum |
| 5 | P0-01 5¢ 阈值 vendor health metric 5 项埋点 (配合小冯 + 小梁) | Sprint-2 W3 | metric exporter 接, 老韩 RM 用得上 |
| 6 | fair_value_anchor 数据契约 v0.1 (给小梁 + 老彭 + 小段) | Sprint-2 W1 | 老黄 vendor 商务出来后 schema 不动 |

---

## 9. 我承认做错的 (HMAC 4 bug 之外)

1. **endpoint matrix v1 没有 HMAC test vector 附录** — 协议 owner 不给可复现的签名实例, 等于把锅甩给 wire 工程师. 老孙 Rust 实现期间发现 sigType=2 错, 是他读官方 SDK 源码逆推回来的, 不是我交付的. **下次任何协议 fact 必须附 test vector**, 不附 = 没交付.
2. **"key 失效" 草率结论** — 老雷 ack 我了, 我当时只看 401 status, body 没看, 没对比 base string. 是协议 owner 的调试纪律问题, 不是知识问题. SOP 化, 见 §2.2 #3.
3. **/books silent skip 229/500 我 v1 spec 没写 known issue** — 我以为是"vendor 行为, 用户不关心", 结果小余 ETL 看到 46% 空数据当 missing 报警, 上下游全乱. **vendor 已知行为差异都是协议 fact**, 必须 known issue 显式标, 不能藏.
4. **vCPU0 11 connection capacity sizing 我没主动给** — 老周 §3 #4 把这个数学拍他自己脸上的时候, 应该是我先算出来主动找老周. 我等老周读完我 v2 §9.2 才意识到, 顺序反了. 跨域听取义务我 Sprint-2 起每周扫一遍老周架构图, 不等他来问.

---

## 10. 给 GM 老雷的汇报

**已完成 (Sprint-1 内)**:
- endpoint matrix v2 (47 endpoint 实测, 决策路径 5 endpoint 浓缩) ✓
- api-call-optimization v1 (与小段联签, TTL L0-L3 四级 cache) ✓
- v1 spec 4 处错 v2 修正并 ack 老郭跨文档扫 ✓
- 协议 fact 接入老周 v0.3 §17.5 / 老韩 v0.2 §3.10 reject enum / 小余 schema v1 ✓

**我对 vCPU0 / WebSocket conn 数 / 5¢ 阈值的态度**:
- **vCPU0**: 同 reactor 跑 3-4 connection 可行 (不是 11, 不是 1-2), 按 hot/cold 拆 market + user 分离, Sprint-2 W3 给老姜实测数据
- **WebSocket conn 数**: market hot 1 / market cold 1-2 / user 1 / Polygon 1, 共 4-5 conn (不是按 sport 拆 5-8)
- **5¢ 阈值**: 我端 endpoint 能验证, Sprint-2 W1 给小梁 4 项实测数据 (触发频率 / depth 分布 / 成交价时差 / taker counterparty)

**Sprint-2 我承诺 6 项 (见 §8)**, 全部可度量, 截止 6/19-6/22 之间.

**我承认做错的**: HMAC 4 bug + "key 失效"草率 + /books silent skip 没标 known issue + 11 connection capacity 没主动算. 4 条都是协议 owner 纪律问题, Sprint-2 起改.

---

— 老李 (polymarket-protocol-expert), 2026-05-28
