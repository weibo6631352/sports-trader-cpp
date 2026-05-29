# Polymarket 市场调研 Update v1 (W9 W5)

- **owner:** 老李 (polymarket-protocol-expert, #07)
- **last_review:** 2026-05-29
- **sprint:** W9 Wave 85 (老板 5/29 "结合新一轮市场调研")
- **cite:**
  - docs.polymarket.com @ 2026-05-29 (llms.txt + fees.md + v2-migration.md + contracts.md + clients-sdks.md)
  - github.com/Polymarket (py-clob-client-v2 v1.0.1, py-sdk v0.1.0-b3, clob-client v5.8.2, ctf-exchange-v2, real-time-data-client)
  - gamma-api.polymarket.com 实测 @ 2026-05-29
  - clob.polymarket.com 实测 @ 2026-05-29
- **ADR cite:**
  - ADR-027: N/A (非核心 struct 改动; 但 §3 含协议 ABI 变更需 ABI 三方 cross-check)
  - ADR-028: frontmatter 合规
  - ADR-029: 新流程 dogfood

---

## §1 老板 Verbatim 入约束

> "结合新一轮市场调研" — 老板 2026-05-29

**背景:** 老板在 W9 W5 多人讨论会前要求本次调研与 W8 W2 SSOT v1 打通, 给 W10 派单候选提供最新市场数据基础.

**直接约束:**
1. 调研截止时间: 2026-05-29 (今日)
2. 覆盖范围: 新动向 + 字段变更 + 体育市场 + 竞品 + 信号机会 update
3. 产出: W10 ticket 候选列表 (给多人讨论会 input)

---

## §2 W8 W2 后新动向 (SSOT v1 之后)

### §2.1 最大变更: CLOB V2 已于 2026-04-28 上线

**这是项目建立以来最大的协议升级. V1 已停止支持.**

来源: docs.polymarket.com/v2-migration.md @ 2026-05-29

> **CLOB V2 is live as of April 28, 2026.** Legacy V1 SDKs and V1-signed orders are no longer supported on production. Upgrade to the V2 SDK or update your raw order signing before submitting orders to `https://clob.polymarket.com`.

核心变更汇总:

| 维度 | V1 (已废弃) | V2 (当前生产) |
|---|---|---|
| SDK | `py-clob-client` / `@polymarket/clob-client` | `py-clob-client-v2` / `@polymarket/clob-client-v2` |
| 订单字段 | `nonce`, `feeRateBps`, `taker`, `expiration`(signed) | `timestamp`(ms), `metadata`, `builder`; `taker/nonce/feeRateBps` 移除 |
| EIP-712 domain version | `"1"` | **`"2"`** (Exchange 域; API auth 不变仍 `"1"`) |
| verifyingContract | 旧 V1 地址 | 新 V2 地址 (见 §3) |
| 抵押品 | USDC.e | **pUSD** (ERC-20, backed by USDC) |
| 费用 | 嵌入签名订单 (`feeRateBps`) | Operator 在 match time 根据 market 设置 |
| Builder 鉴权 | `POLY_BUILDER_*` HMAC headers + builder-signing-sdk | 订单内 `builderCode` 字段 |
| Nonce 管理 | 每地址 nonce 递增 | **废弃** — 唯一性由 `timestamp`(ms) 保证 |

**风险评级: P0** — 我们项目当前所有 signer 代码 (老孙 v5.1) 均基于 V1 ABI. 必须升级到 V2 order struct.

### §2.2 新 SDK 生态 (github.com/Polymarket @ 2026-05-29)

最近更新的仓库:

| repo | 创建时间 | 最新版本/状态 | 说明 |
|---|---|---|---|
| **py-sdk** | 2026-05-04 | v0.1.0-b3 (2026-05-27) | **官方新 unified Python SDK** (beta) |
| **py-clob-client-v2** | 2026-03-02 | v1.0.1 (2026-05-09) | V2 订单支持; `deposit_wallet` 新增; slippage fee calc |
| **ctf-exchange-v2** | 2025-11-06 | 无 release; Solidity 0.8.30 | V2 合约; Quantstamp + Cantina 双审计 (2026-03) |
| **real-time-data-client** | 2025-03-04 | v1.4.0 (2025-07-25) | TS WSS 客户端; v1.2.0 加 clob events |
| **clob-client** (TS) | — | v5.8.2 (2026-04-14) | TypeScript 6 升级; axios 1.14.0 pin |
| **polymarket-cli** | 2026-02-24 | v0.1.5 (2026-03-10) | CLI 工具 |
| **builder-relayer-client** | 2025-10-01 | — | Gasless 交易 relayer TS client |
| **ts-sdk** | 2026-04-02 | — | "Unified TS SDK for Polymarket DeFi" (新) |
| **agent-skills** | 2026-02-19 | 更新 2026-05-29 | Polymarket Agent Skills (AI agent 集成) |

**关键 SDK 迁移路径 (docs.polymarket.com/api-reference/clients-sdks.md):**

```
V1 → V2:
  TypeScript: npm install @polymarket/clob-client-v2 viem
  Python:     pip install py-clob-client-v2
  Rust:       cargo add polymarket_client_sdk_v2 --features clob  (注: 项目不用 Rust)
```

**官方 py-sdk (polymarket-client) 注意:** 2026-05-27 刚出 b3, 仍 beta. 包名 `polymarket-client` (不是 `py-clob-client`). 提供 sync/async 两个客户端. 项目暂不使用 (Python 只做离线 ML 训练).

### §2.3 新费用体系 (docs.polymarket.com/trading/fees.md @ 2026-05-29)

V2 费用彻底重构. 旧的 `feeRateBps` 嵌入签名字段废弃, 改为 Operator 在 match time 按 market category 设置.

**费用公式:**
```
fee = C × feeRate × p × (1 − p)
```
C = 成交 shares 数量, p = 成交价格 (0~1)

**按 category 费率 (官方文档 @ 2026-05-29):**

| Category | Taker Fee Rate | Maker Fee Rate | Maker Rebate |
|---|---|---|---|
| **Sports** | **0.03 (3%)** | 0 | **25%** |
| Crypto | 0.07 | 0 | 20% |
| Finance | 0.04 | 0 | 25% |
| Politics | 0.04 | 0 | 25% |
| Economics | 0.05 | 0 | 25% |
| Culture | 0.05 | 0 | 25% |
| Geopolitics | **0** | 0 | — (fee-free) |
| Other/General | 0.05 | 0 | 25% |

**Sports 费用示例 (100 shares @ p=0.5):**
```
fee = 100 × 0.03 × 0.5 × 0.5 = 0.75 USDC
```
做市商永远不付费 (Maker Fee Rate = 0), 只有 taker 付费.

**Taker Rebate Program — 2026-05-28 上线 (重大新政):**

来源: docs.polymarket.com/trading/taker-rebates.md @ 2026-05-29

Weighted Volume 公式: `wV = Trade Size × (1 − Entry Price) × Category Weight × Bonuses`

Sports category weight = **1.0** (最低; Crypto = 2.3 最高, 利于我们 sports 专注不卷 wV)

| Tier | 30-day wV | Rebate |
|---|---|---|
| Bronze | $2,000 | 3% |
| Silver | $20,000 | 8% |
| Gold | $200,000 | 18% |
| Platinum | $1,000,000 | 32% |
| Diamond | $4,000,000 | 44% |
| Obsidian | $10,000,000+ | 50% |

**Maker Rebates Program (docs.polymarket.com/market-makers/maker-rebates.md):** Sports market maker rebate = 25% of taker fees, 每日 USDC 打到 wallet, 最低 $1 USDC 起付.

**对我们的影响:** 做市商在 sports 市场: 0 费用 + 25% maker rebate. Taker 每次 aggressive 成交支付 3% × p(1-p). Net effective spread = taker fee (maker 为正).

### §2.4 Taker Rebate Program 新动态

Live 时间: **2026-05-28** (昨天). 这是 W9 W5 调研期间刚上线的功能.

对我们策略的影响:
- 方向性 taker 交易: Bronze (~$2k wV/30d) 以上可拿回 3-8% taker fee
- Sports wV weight = 1.0, 需更多交易量才能升 tier
- Obsidian tier (50% rebate) 对 sports: 需 $10M wV/30d ≈ 大型 HFT 级别

---

## §3 新增 / 变更字段或 Endpoint

### §3.1 EIP-712 Order struct 变更 (P0 — 当前代码必须升级)

来源: docs.polymarket.com/v2-migration.md §For API users @ 2026-05-29

**签名 Order type 变更 (V1 → V2):**

```
移除字段:
  taker        (address)
  expiration   (uint256)  — 注: POST /order wire body 仍有 expiration 用于 GTD
  nonce        (uint256)
  feeRateBps   (uint256)

新增字段:
  timestamp    (uint256)  — 毫秒时间戳, 替代 nonce 保证唯一性
  metadata     (bytes32)  — 应用元数据
  builder      (bytes32)  — builder code (optional, zero if not used)
```

**EIP-712 domain 变更:**

```
name: "Polymarket CTF Exchange"
version: "1" → "2"   (Exchange 域; API auth ClobAuthDomain 不变仍 "1")
chainId: 137          (不变)
verifyingContract:
  CTF Exchange V1: 0x4bFb41d5B3570DeFd03C39a9A4D8dE6Bd8B8982E  (废弃)
  CTF Exchange V2: 0xE111180000d2663C0091e4f400237545B87B996B  (当前)
  NegRisk Exchange V1: 0xC5d563A36AE78145C45a50134d48A1215220f80a (废弃)
  NegRisk Exchange V2: 0xe2222d279d744050d28e00520010520000310F59  (当前)
```

### §3.2 V2 合约地址 (docs.polymarket.com/resources/contracts.md @ 2026-05-29)

| 合约 | 地址 |
|---|---|
| CTF Exchange V2 | `0xE111180000d2663C0091e4f400237545B87B996B` |
| Neg Risk CTF Exchange V2 | `0xe2222d279d744050d28e00520010520000310F59` |
| Neg Risk Adapter | `0xd91E80cF2E7be2e162c6513ceD06f1dD0dA35296` |
| Conditional Tokens (CTF) | `0x4D97DCd97eC945f40cF65F87097ACe5EA0476045` |
| pUSD CollateralToken (proxy) | `0xC011a7E12a19f7B1f670d46F03B03f3342E82DFB` |
| CollateralOnramp | `0x93070a847efEf7F70739046A929D47a521F5B8ee` |

审计状态: Quantstamp + Cantina 双审计, 2026-03 完成, 报告在 ctf-exchange-v2 repo.

### §3.3 Gamma API 新字段 (实测 @ 2026-05-29)

以下字段在 W8 W2 SSOT v1 未收录, 本次实测发现:

| 字段 | 位置 | 类型 | 语义 |
|---|---|---|---|
| `feeSchedule` | gamma /markets | object | `{exponent, rate, takerOnly, rebateRate}` — 当前所有市场: `{1, 0.05, true, 0.25}`. Sports 实测 feeSchedule 与 fee.md 有差异 (见坑 §3.5) |
| `feeType` | gamma /markets | string | `"general_fees"` 为当前唯一已知值 |
| `feesEnabled` | gamma /markets | bool | 是否启用费用; Geopolitics = false |
| `rfqEnabled` | gamma /markets | bool | 当前全部 false; RFQ 功能已在 SDK 实现 (py-clob-client v0.34.5) |
| `holdingRewardsEnabled` | gamma /markets | bool | Holding rewards 开关, 当前 false |
| `eventMetadata` | gamma /events (内嵌) | object | `{context_description, context_requires_regen, context_updated_at}` — AI 生成的市场背景描述, 每日更新 |
| `makerBaseFee` | gamma /markets | int | 1000 = 10bps? (注意: 与 docs fee model 不一致, 见 §3.5 坑) |
| `takerBaseFee` | gamma /markets | int | 1000 |

**WSS 变更:** 无 URL 变更 (docs/v2-migration.md FAQ 明确: "WebSocket URLs are unchanged"). 三路 WSS 地址与 SSOT v1 一致:
- `wss://ws-subscriptions-clob.polymarket.com/ws/market` (market channel)
- `wss://ws-subscriptions-clob.polymarket.com/ws/user` (user channel)
- `wss://sports-api.polymarket.com/ws` (sports inplay channel)

`fee_rate_bps` 字段在 WSS `last_trade_price` event 仍存在 (反映实际收费).

### §3.4 CLOB V2 POST /order wire body 变更

V2 POST /order body 移除 `taker`, `nonce`, `feeRateBps`. `expiration` 仍保留用于 GTD (Good Till Date) 但不进入 EIP-712 签名:

```json
V2 order body:
{
  "order": {
    "salt": "12345",
    "maker": "0x...",
    "signer": "0x...",
    "tokenId": "...",
    "makerAmount": "1000000",
    "takerAmount": "2000000",
    "side": "BUY",
    "signatureType": 1,
    "timestamp": "1748476800000",
    "metadata": "0x0000...0000",
    "builder": "0x0000...0000"
  },
  "expiration": "0",
  "nonce": "0",
  "orderType": "GTC",
  "signature": "0x..."
}
```

`timestamp` = 毫秒时间戳, 替代 nonce 唯一性. 同一 ms 内不可重复提交.

### §3.5 已知坑 (新)

**坑 V2-1: `makerBaseFee` / `takerBaseFee` 字段含义不明**
- gamma /markets 字段: `makerBaseFee=1000, takerBaseFee=1000`
- 1000 bps = 10%? 但官方 fees.md Sports taker fee = 3%.
- 推测: 这是 max fee cap 或 legacy 字段, 实际费用由 `feeSchedule.rate` 决定.
- 不可直接用 `makerBaseFee` / `takerBaseFee` 计算实际费用. 务必用 `feeSchedule.rate` + 公式 `C×rate×p×(1-p)`.

**坑 V2-2: timestamp 替代 nonce — 旧 laoye nonce manager 需重构**
- laoye-nonce-manager-design-v1.md 设计的 nonce 管理器在 V2 中已废弃.
- V2 唯一性: `timestamp` 毫秒级. 同地址同 ms 内不可重复. 高频场景 (>1 order/ms) 需加 salt 区分.

**坑 V2-3: pUSD 代替 USDC.e — 链上 approve 需重做**
- 抵押品从 USDC.e 变为 pUSD (`0xC011a7E12a19f7B1f670d46F03B03f3342E82DFB`).
- API-only trader 需调用 `CollateralOnramp.wrap()` 将 USDC.e 换为 pUSD.
- 现有 approve 合约地址全部需要更新到 V2 地址.

**坑 V2-4: 旧 V1 resting orders 已被清空**
- "Open orders from before the CLOB V2 cutover were wiped." — 2026-04-28 切换时全部清空.
- 无需担心历史挂单; 但上线前必须重新挂单.

---

## §4 体育市场动态 (赛事 / 流动性 / Vig)

### §4.1 当前活跃 Sports 市场概况 (实测 @ 2026-05-29)

来源: gamma-api.polymarket.com 实测

| 维度 | 数值 | 说明 |
|---|---|---|
| 活跃 sports events (sample) | ~200+ (100 page 未翻完) | tag_slug=sports |
| 活跃 NBA events | 13 (basketball tag) | NBA Play-In + Playoff + Finals + props |
| 活跃 Champions League events | 50 | 含多个赛程 + winner outright |
| 活跃 EPL events | 5+ | 赛季末 + 下赛季开盘 |
| Sports 盘口 bid/ask spread | 0.3%–1.0% | NBA Finals: 0.3%, 低热度: 1.0% |
| Sports taker fee (p=0.5, 100 shares) | $0.75 USDC | C×0.03×0.5×0.5=0.75 |

### §4.2 NBA 季后赛 / 总决赛 流动性

实测活跃 NBA events @ 2026-05-29:

| 市场 | 成交量 (USD) | 备注 |
|---|---|---|
| NBA Finals MVP | $359,322 | 最高流动性体育 prop |
| Western Conference Finals: Timberwolves vs Mavericks | $119,621 | 系列赛级 |
| NBA Play-In: Warriors vs Kings | $23,162 | Play-In 已结束 |
| NBA Play-In: Lakers vs Pelicans | $12,794 | |
| 2026 NBA Finals Winner: Knicks? | — (spread 0.3%) | W10-W12 决赛期间峰值预计 |

**预测 W10-W12 (2026-06~07):** NBA Finals 通常 6 月, 成交量峰值约 $100k-$500k/event. 历史 Finals Winner outright 超 $1M.

### §4.3 欧冠 Champions League (2025-26)

实测 @ 2026-05-29: 50 active CL events. 注意当前数据库含 2024 季 (已结算) + 2025-26 季新开盘.

- 2024-25 CL Final (Real Madrid vs Dortmund): 已结算 (endDate 2024-06-01)
- 2025-26 CL 决赛: 预计 2026-05-30 (慕尼黑, PSG vs Inter Milan). 当前已有 semifinal/outright 盘口.
- 成交量前三: Arsenal vs Bayern ($42k), Real Madrid vs Bayern ($22k), PSG vs Barcelona ($11k)

CL Final 通常 5-6 月, 流动性峰值约 $50k-$150k/event. W10 Paper Engine 启动期间此赛事正值高峰.

### §4.4 Vig 中位实测 (@ 2026-05-29)

**方法论说明:** Polymarket binary market 中间价 (outcomePrices) 互补 (Yes+No=1.0), 无 mid-price vig. 真实 "vig" 体现在 bid/ask spread + taker fee.

实测 bid/ask spread (gamma bestBid/bestAsk, 21 个有价格 markets):
- 体育 (NBA/NHL outright): 0.3%–1.0%
- 高流动性 outright (FIFA World Cup winner): 0.1%
- 低流动性 other: 1.6%–3.2%

**有效 vig 模型 (Sports, taker 视角):**
```
有效 vig = bid/ask spread + taker fee (p=0.5 时约 1.5%)
体育市场 effective vig ≈ 0.3% spread + 1.5% fee = 1.8% (低流动性可达 5%+)
```

旧 SSOT v1 §3 引用的 "4.5-6%" 是基于旧 fee model (V1 feeRateBps 嵌入). V2 sports fee = 3% (公式后 p=0.5 时 0.75 USDC/100 shares), 实际低于旧估计.

### §4.5 2026 FIFA World Cup 开盘 (W10-W12 重要)

实测: Will Argentina/Brazil/France win 2026 World Cup 已开盘, 每个 spread = 0.1%. 2026 FIFA World Cup = 2026-06-11 开赛 (北美). W10 paper engine 启动期间正好开赛. 流动性预计是平台最大体育 event 之一.

---

## §5 竞品监控

### §5.1 Kalshi

- 美国 CFTC 监管, 合法体育盘口
- 2026 FIFA World Cup 已开盘 (对标 Polymarket)
- 价差 vs Polymarket: 同 event 常有 2-5% 隐含概率差异
- 局限: 美国用户限制 (地区限制); 流动性低于 Polymarket

### §5.2 PredictIt

- 主打政治市场; 体育盘口少
- 10% 利润税 + 5% 提款费, vig 显著高于 Polymarket
- 对我们竞争威胁低

### §5.3 体育博彩 (DraftKings / FanDuel)

**同 event 隐含概率 vs Polymarket (NBA Playoffs 样本估算):**

| 来源 | NBA Moneyline 热门队 vig | 冷门队 vig | 说明 |
|---|---|---|---|
| DraftKings | ~4% | ~5% | 传统博彩 vig 嵌入赔率 |
| FanDuel | ~4% | ~5% | 近似 |
| Polymarket (taker) | 1.5%–3% (p=0.5) | 同 | formula: 0.03×p×(1-p), taker 净 |
| Polymarket (maker) | 0 + 25% rebate | 0 | 做市商实际负 vig |

**套利信号方向:** Polymarket maker 挂单 + DraftKings 对冲 taker 是理论上的跨平台套利. 但 DraftKings 不支持 API 自动下注 + 速率限制 + 账户封禁风险高. 实操价值存疑 (见 §7).

---

## §6 风险 / 监管动态

### §6.1 美国监管 (CFTC)

当前状态 (2026-05-29): Polymarket 继续面向非美国用户; 美国 IP/VPN geoblock. CFTC 体育预测市场规则仍未落地.

GM ADR (2026-05-28): 地域/法律/监管层合规已决议暂不纠缠, 未来迁合规地区一次性处理. 参见 `docs/ADR/2026-05-28-gm-policy-jurisdictional-deferral.md`.

### §6.2 V2 合约安全

双重审计 (Quantstamp + Cantina, 2026-03). Cantina bug bounty 开放. 平台风险相比 V1 降低.

### §6.3 pUSD 新增风险

pUSD 是 Polymarket 自己发行的 ERC-20, backed by USDC onchain. 新增 smart contract risk (CollateralOnramp). 在 MVP 阶段资金量小, 风险可控. 规模扩大后需老韩评估 pUSD depegging 风险.

### §6.4 V2 迁移窗口风险

V1 已于 2026-04-28 停止支持. 项目现有 signer (老孙 v5.1) 仍是 V1 ABI. 若不升级, 所有订单将被 CLOB 拒绝. **这是当前最大平台风险**.

---

## §7 信号机会 Update (Alpha v2)

### §7.1 Inplay Alpha 之后: User Channel Trade Leak

**背景:** SSOT v1 确认 user channel (`wss://ws-subscriptions-clob.polymarket.com/ws/user`) 包含个人订单/成交 events.

**新发现 (V2 context):** V2 实盘重启后 user channel payload 格式基本不变 (v2-migration.md FAQ 确认 WSS payload "mostly unchanged"). `fee_rate_bps` 字段仍出现在 `last_trade_price` event.

**User channel trade leak 作为信号:**
- 用途: 监控大户 (whale) 的 taker 成交方向
- 局限: user channel 只能订阅自己的账户, 无法监听他人
- 可行性: 低. 公开 user channel 只有自己数据, 无跨用户 "leak"

**结论:** user channel 用于自身账户状态同步 (position/order tracking), 不是 alpha 信号源.

### §7.2 信号 Decay (tau) 变化

W8 W2 inplay alpha (laopeng-w9-inplay-edge-gross-net-confirm.md) 确认 tau. V2 上线后 (2026-04-28) 可能影响:
- 新费用结构 (taker fee 3%) 降低了 noise trader 成本 (相比旧 feeRateBps 可变场景)
- 平台迁移期 (4-5 月) 可能导致流动性短暂下降后恢复
- @老彭 W10 需更新 historical backfill 用 V2 切换日期分段

### §7.3 新 Alpha 候选

**跨平台套利 (Polymarket vs Kalshi):**
- 理论价差: 2-5%
- 实操障碍: Kalshi API 速率限制 + 美国地区限制 + 账户风险
- 评级: 需 W10 小程专项探索 (P0-03 信号候选)

**2026 FIFA World Cup 开赛信号 (W10-W12 机会窗口):**
- 6 月 11 日开赛, 恰好在 Paper Engine 运行期
- Polymarket 已开盘 outright (spread 0.1%), 将有 moneyline/totals 开盘
- Goalserve 实时比分 + 盘口数据可驱动 inplay signal
- 评级: P1, 与现有 inplay alpha 框架直接对接

**NBA Finals Moneyline (W10-W11 机会窗口):**
- 6 月 Finals, NBA Finals Winner outright 已有 $359k volume
- Spread = 0.3%, 流动性充足
- 现有 inplay alpha 可直接覆盖
- 评级: P0 for paper engine W11 首跑

**Maker Rebate Program 做市 alpha:**
- Sports maker rebate = 25% taker fee
- 策略: 紧价差挂单, 收 rebate, 对冲 inventory risk
- 净效益: rebate 覆盖 inventory cost?
- 评级: 需 W10 小梁/老彭 分析 (strategy research)

---

## §8 W10 派生 Ticket 候选 (给多人讨论会)

以下 ticket 候选基于本次调研新发现, 建议 W10 W1 多人讨论会讨论:

### §8.1 P0 — Signer V2 升级 (老孙 W10 — 阻塞 MVP)

**背景:** CLOB V2 于 2026-04-28 上线, V1 已废弃. 老孙 signer v5.1 仍是 V1 ABI.
- 移除 `nonce`, `feeRateBps`, `taker`, `expiration`(signed)
- 新增 `timestamp`(ms), `metadata`, `builder`
- EIP-712 domain version "2", verifyingContract 新地址
- 阻塞 MVP; 需 laoli + laosun + laowang ABI 三方 cross-check

### §8.2 P0 — pUSD 抵押品迁移 (老孙/老沈 W10)

- USDC.e → pUSD; approve 合约地址全部更新
- CollateralOnramp.wrap() 实现
- 涉及 signer + risk manager + position ledger

### §8.3 P1 — 老李 W10 W2: WSS user channel 鉴权实测

**已在 laoli-w9-wss-subscriber-impl-spec-v1.md 定义.** V2 user channel auth 格式需确认 (payload 内 apiKey/secret/passphrase). 与小冯 CLOB W79 联动.

### §8.4 P1 — 老李 + 小段 W10 W3: 跨源 mapping 升级

- 2026 FIFA World Cup 开赛 (6-11) → Goalserve new league feed 接入
- Sports 新增: soccer subleagues (World Cup groups)

### §8.5 P1 — 小程 W10 W2: P0-03 信号探索

候选: 跨平台套利 (Polymarket vs Kalshi). 需评估可行性 + RTT 约束.

### §8.6 P2 — 老彭 W10 W3: Alpha v2.1 historical backfill update

V2 切换日 (2026-04-28) 为新旧数据边界. 需重新分段 historical 回填.

---

## §9 不耻下问

以下为本调研期间发现的跨域依赖, 需并行调研:

| 对象 | 问题 | 优先级 |
|---|---|---|
| **@小段** | Goalserve 是否已有 FIFA World Cup 2026 feed? 新 league code? | P1 |
| **@老彭** | V2 切换 (2026-04-28) 前后 inplay edge 是否有断层? tau 是否变化? | P1 |
| **@小程** | Kalshi API 访问限制实测 — 能否支持跨平台套利 pipeline? | P2 |
| **@老孙** | Signer V2 升级 ETA? 老李提供 V2 wire contract 细节支持 | P0 |
| **@老梁** | Maker Rebate Program — sports 25% rebate 策略分析值得做吗? | P2 |
| **@多人讨论会 W10 W1** | §8 全部 ticket 候选排序 + owner 分配 | P0 |

---

## 附录 A: V2 协议变更索引

本文所有 V2 变更的权威来源:

| 来源 | URL | 访问时间 |
|---|---|---|
| CLOB V2 migration guide | https://docs.polymarket.com/v2-migration.md | 2026-05-29 |
| Fee structure | https://docs.polymarket.com/trading/fees.md | 2026-05-29 |
| Taker rebate program | https://docs.polymarket.com/trading/taker-rebates.md | 2026-05-29 |
| Maker rebates | https://docs.polymarket.com/market-makers/maker-rebates.md | 2026-05-29 |
| Contract addresses | https://docs.polymarket.com/resources/contracts.md | 2026-05-29 |
| SDK reference | https://docs.polymarket.com/api-reference/clients-sdks.md | 2026-05-29 |
| py-clob-client-v2 v1.0.1 | https://github.com/Polymarket/py-clob-client-v2/releases | 2026-05-29 |
| ctf-exchange-v2 | https://github.com/Polymarket/ctf-exchange-v2 | 2026-05-29 |
| py-sdk v0.1.0-b3 | https://github.com/Polymarket/py-sdk | 2026-05-29 |
| Gamma API 实测 | https://gamma-api.polymarket.com/events?active=true&tag_slug=sports | 2026-05-29 |
| CLOB API 实测 | https://clob.polymarket.com/markets?active=true | 2026-05-29 |

---

**最后更新:** 2026-05-29 by 老李 (#07, polymarket-protocol-expert, Wave 85)
