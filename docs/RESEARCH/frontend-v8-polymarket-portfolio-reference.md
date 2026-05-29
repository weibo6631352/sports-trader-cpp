# Frontend v8 — Polymarket Portfolio / Positions / Market 页参考调研

> **owner:** 前端调研员 (frontend research)
> **last_review:** 2026-05-29
> **派单:** GM (老板: 参考 https://polymarket.com/zh/portfolio?tab=positions 看官方都有哪些功能、怎么展示, 给前端 v8 重设计参考)
> **约束背景:** 我们是 SUID/Material(谷歌风) 观测看板, 深色终端风, **重功能/可读不重美观**; 市场结构 Event→Market(condition, 多盘口)→双边订单簿(token0/token1); 老板明确要 **"主页内容折叠, 点击展开看详情"**。
> **调研方法说明:** Polymarket portfolio/positions 页是登录态 + JS 渲染 (WebFetch 只能拿到导航壳, 实页拿不到; analytics 第三方站 403)。本报告基于 **官方 docs (concepts/positions-tokens, markets-events, portfolio API schema) + 官方 help/legacy docs + 第三方拆解 (Avark 设计模式、Purrdict 开源 Polymarket-style 组件库、行业 UX 文章)** 交叉印证, 字段级以官方 `GET /positions` API schema 为准 (最可靠一手来源)。

---

## 0. TL;DR (给 GM 的 5 句话)

1. **Portfolio 页 = 顶部 KPI 汇总条 + Tab 切换 (Positions / Open Orders / History / Sponsorships) + 持仓表**。持仓表每行核心列就是我们 PnL 页要的那套: `市场/outcome名` · `份额(size)` · `Avg价` · `Cur价` · `当前市值` · `PnL($ + %)` · `actions(Sell/Claim/Redeem)`。
2. **官方 `GET /positions` API 字段表是 v8 持仓行的现成数据契约** (见 §1.2), 我们后端 positions 端点应对齐这套语义 (avgPrice/curPrice/currentValue/initialValue/cashPnl/percentPnl/realizedPnl/redeemable/mergeable), 直接喂给前端表。
3. **官方折叠/展开模式确实存在且正是老板想要的: 多盘口 Event = 折叠的 outcome 行列表 (每行一概率条), 点行 → 展开/进详情 = 概率图 + 双边订单簿 + 交易面板** (官方业界叫 progressive disclosure 三层渐进披露)。这套直接映射我们 Event→Market→book_pair 三级结构。
4. **数据呈现惯例: PnL 绿涨红跌但降饱和不闪烁 (避免 "ticker 焦虑")、价格双格式 (¢ 价 `72¢` + 概率 `72%` 并列)、实时数字 200-300ms 平滑过渡 + 方向角标**。这套和我们 v6 调研结论一致, v8 继续沿用。
5. **给我们 v8 的具体建议见 §5** (折叠表 + 行展开抽屉 + KPI 汇总条 + 字段对齐 + 配色), 文件: `docs/RESEARCH/frontend-v8-polymarket-portfolio-reference.md`。

---

## 1. Portfolio / Positions 页 — 功能与列

### 1.1 页面骨架 (官方 polymarket.com/portfolio)

```
┌─────────────────────────────────────────────────────────┐
│  顶部 KPI 汇总条                                          │
│   Portfolio Value (持仓市值 + 现金)   Profit/All-time     │
│   Cash 余额          Volume          [PnL 时序小图]       │
├─────────────────────────────────────────────────────────┤
│  Tab:  [Positions] [Open Orders] [History] [Sponsorships] │
├─────────────────────────────────────────────────────────┤
│  持仓表 (每行一个 position)                               │
│   Market/Outcome │ Qty │ Avg │ Cur │ Value │ P/L($ / %) │⋯│
└─────────────────────────────────────────────────────────┘
```

- **顶部 KPI 汇总条:** `Portfolio Value` = 所有持仓市值 + 现金余额; `Open Positions` = 持仓市值; 另有 `Cash`、`Profit`(累计盈亏)、`Volume`(累计成交额), 通常配一张账户级 PnL 时序小图。
- **4 个 Tab:**
  - **Positions** — 当前持仓 (默认, 即老板给的 `?tab=positions`)
  - **Open Orders** — 挂单中的 limit 单, 可跨市场集中管理/撤单
  - **History** — 历史成交/交易记录
  - **Sponsorships** — (奖励/赞助, 与我们无关)
- **空态:** 无持仓时显示引导文案 + CTA (去市场页), 不是空白表。我们看板对应应显示 "无持仓 / 等待首单" 占位行而非空表。

### 1.2 持仓行字段 (以官方 `GET /positions` API schema 为准 — 最可靠一手契约)

这是 v8 持仓表的**现成数据模型**, 强烈建议我们后端 positions 端点字段语义对齐:

| API 字段 | UI 列 | 说明 |
|---|---|---|
| `title` / `outcome` / `eventSlug` | 市场/Outcome 名 | 行标题, 显示 event + 选中的 outcome |
| `size` | Qty / 份额 | 持有的 outcome token 数量 |
| `avgPrice` | **Avg** | 平均建仓价 (官方 UI 标 `Avg`, 如 `94¢`) |
| `curPrice` | **Cur** | 当前市价 (官方 UI 标 `Cur`, 如 `97.5¢`) |
| `currentValue` | **Value** | size × curPrice (如 `$62.20`) |
| `initialValue` | (隐藏/hover) | 初始投入, 用于对比 |
| `cashPnl` | **P/L $** | 绝对盈亏 (如 `+$2.20`) |
| `percentPnl` | **P/L %** | 百分比盈亏 (如 `+3.67%`) |
| `realizedPnl` / `percentRealizedPnl` | (明细/hover) | 已实现盈亏 |
| `totalBought` | (明细) | 累计买入额 |
| `redeemable` | → **Redeem** 按钮 | 市场已结算可赎回 → 行内显示 Redeem |
| `mergeable` | → Merge | 可合并 Yes/No 回 pUSD |
| `endDate` | 到期/倒计时 | 市场结算日 |
| `conditionId` / `asset` / `oppositeAsset` | (内部 id) | condition + token0/token1, **直接对应我们 book_pair** |
| `negativeRisk` / `proxyWallet` / `icon` | metadata | 风险标记/钱包/图标 |

**P/L 列官方做法: 同一列里 `$` 和 `%` 并列显示** (如 `+$2.20 (3.67%)`), 一眼看绝对值 + 相对收益。

### 1.3 排序 / 筛选 / actions

- **排序:** 按 Value / P/L / 市场名 排序 (交易看板常见, 让用户先看最大/最亏仓)。
- **actions (行级):** `Sell` (结算前卖出锁利)、`Redeem` (结算后赎回赢的 token)、`Merge` (Yes/No 等量合并回 pUSD)。状态由 `redeemable`/`mergeable` flag 驱动 → 决定行内显示哪个按钮。
- **价值/盈亏计算 (官方语义, 我们须一致, 红线: 回测/实盘/看板同一套逻辑):**
  - 仓位价值 = `size × curPrice`
  - PnL = 建仓价 vs 当前价/结算结果; 结算前可卖出锁利, 结算后赢方每 token 赎回 $1。

---

## 2. 市场 / Event 页 — Event→Market→双边订单簿 的组织

### 2.1 三级结构 (官方 concepts/markets-events)

- **Market** = 最小可交易单元, 一个二元 Yes/No 问题, 两个 token (Yes/No), 都按 $1 结算。
- **Event** = 容器, 聚合一个或多个相关 market。
  - **单 market event:** event ≈ market, 简单一对 (一个二元问题, 一张图 + 一个订单簿 + 一个交易面板)。
  - **多 market event (多盘口/多 outcome):** 一个 event 含多个 market (如总统选举: Trump / Biden / Harris / Other, 每个一对 Yes/No), 互斥多 outcome。← **正对应我们 一赛事→多盘口(condition)**。

### 2.2 多 outcome 的呈现 (折叠列表, 老板想要的模式)

- 多 outcome event 用 **outcome 行列表**: 每行 = 一个 outcome, 显示 `名称 + 当前概率% + (倒计时/volume) + 一个 ProbabilityBar/比例条`, 按概率高低排序。
- **3+ outcome** 时配 **ProbabilityChart** (多线 step-function 图, 每个 outcome 一条线, 实时随价格走)。
- **点击某个 outcome 行 → 下钻进该 market 详情**: 概率历史图 + 完整订单簿 (bids/asks/shares/total/spread) + resolution 规则 + 交易面板。← **这就是 "主页折叠列表, 点开看详情" 的官方实现**。

### 2.3 市场详情页布局 (单 market)

```
┌──────────────────────────────────────────────┐
│  Resolution 规则 / 标题 (置顶, above the fold) │  ← 信任优先
│  概率图 (price history)                        │
├───────────────────────────┬──────────────────┤
│  订单簿 (Level 2 双 pane)  │  交易面板         │
│   Asks (卖, 堆上)          │   [Yes] [No] 切换 │
│   ── spread ──             │   Market / Limit  │
│   Bids (买, 堆下)          │   size 输入       │
│   列: Price·Shares·Total   │   slippage/tick   │
├───────────────────────────┴──────────────────┤
│  RecentTrades 成交流 + MarketStats 统计        │
└──────────────────────────────────────────────┘
```

- **订单簿 (Orderbook):** 双 pane Level 2, bid/ask depth + spread 显示 + 可点价格档 (点了自动填进交易面板)。列: `Price · Shares · Total`, 显示 spread。
- **二元市场特性 (关键, 对应我们 book_pair):** UI 上 Yes/No 看似两个独立 tab/订单簿, 但**一侧的单在另一侧表现为镜像反向单** (买 Yes @ 40¢ ≡ 卖 No @ 60¢)。我们 `book_pair` token0/token1 双边结构正是这个 — v8 可在一个 book 组件里用 tab 切 Yes/No, 或并排显示两边。
- **交易面板 (TradeForm):** 边选择 (Yes/No) + 订单类型 (Market/Limit) + size 输入 + slippage + tick size 校验。(我们是观测看板, 交易面板可能只读/弱化, 但布局可借鉴。)

---

## 3. 整体交互 — 折叠/展开模式 (老板核心诉求)

**官方/行业确实大量用折叠展开, 叫 "渐进式披露 (progressive disclosure)" 三层:**

| Layer | 受众 | 默认显示 (折叠态) | 展开后 (详情) |
|---|---|---|---|
| **L1** 全员 | event 卡片: outcome 名 + 概率% + Yes/No 动作 | — |
| **L2** 活跃用户 | (点开后) 概率图、成交历史、resolution 规则、仓位 sizing | 一次点击可达 |
| **L3** 高级用户 | (再下钻) 完整订单簿、depth chart、组合分析 | power user |

**可借鉴的具体折叠模式:**
1. **多 outcome event = 折叠的 outcome 行列表** → 点行展开 ProbabilityChart + 订单簿 (§2.2)。
2. **市场卡片网格 → 点卡片进详情页** (event grid → market detail)。
3. **设计准则: "advanced options 应 one interaction away, not absent" (高级信息不删, 但不和主信息抢注意力)** — 正好支撑老板 "主页折叠、点开看详情"。
4. Portfolio 持仓行也可做成 **可展开行**: 折叠态显示核心 7 列, 展开显示 realizedPnl / totalBought / 该市场订单簿 / 该仓位 PnL 小图。

---

## 4. 数据呈现细节

- **PnL 正负配色:** 绿涨 / 红跌, 但 **降饱和 (desaturated)、稀疏使用**, 避免激进红绿闪烁的 "stock ticker 焦虑"。配方向角标/箭头 (`+2%` / ▲▼) 而非只靠颜色 (色盲 + 截图可读)。
- **价格 / 概率显示:** **双格式并列** — cent 价 (`$0.72` / `72¢`) + 自然语言概率 (`72%`) 同时显示。预测市场标准做法。
- **实时更新:** 数字 **200-300ms 平滑过渡**, 不跳变/不闪烁; 配 "Updated Xs ago" 数据新鲜度时间戳 (对齐我们 4 时间戳契约 R-20)。多 outcome 概率图实时随价格移动 (step-function 多线)。
- **多 outcome 比例条:** 水平比例条, 按 outcome/队伍配色, hover 显精确值。

---

## 5. 给 v8 的具体建议 (5 条)

1. **Portfolio/PnL 页采用 "KPI 汇总条 + Tab + 持仓表" 三段骨架。** 顶部 KPI 条放 `组合市值 / 现金 / 累计 PnL / 累计成交额 + 账户 PnL 时序小图`; Tab 至少 `Positions / Open Orders / History` (我们 paper+live 可再加 mode 切换); 持仓表核心 7 列照搬官方: `市场·份额·Avg·Cur·市值·PnL($ / %)·actions`。

2. **持仓行字段直接对齐官方 `GET /positions` schema (§1.2)。** 我们后端 positions 端点输出 `avgPrice / curPrice / currentValue / initialValue / cashPnl / percentPnl / realizedPnl / redeemable / mergeable / conditionId / asset / oppositeAsset`, 让前端表零转换渲染。`conditionId + asset/oppositeAsset` 天然桥接我们 book_pair。

3. **落地老板的 "折叠→展开" = 可展开持仓行 + 多盘口折叠 outcome 列表。** Portfolio 持仓行折叠态显示 7 列, 点击行内展开抽屉显示 (realizedPnl / totalBought / 该仓位 PnL 小图 / 该 market 的双边订单簿快照)。Event 页则用折叠 outcome 行列表 → 点行展开/进 market 详情 (概率图 + book_pair 双边簿 + quote/edge)。符合 SUID/Material 的 expandable-row / detail-drawer 模式, 不需要重组件库。

4. **Event→Market→双边订单簿 三级导航照搬官方层级 (§2)。** 主屏 = event 网格/列表 (折叠); 多盘口 event 展开为 outcome 行 (每行概率条); 点 outcome 进 market 详情 = `Resolution规则置顶 + 概率图 + 双 pane 订单簿(Price·Shares·Total + spread) + (只读)交易/quote 面板 + 成交流`。订单簿用 Yes/No tab 切换 (镜像反向单语义), 直接喂我们 book_pair token0/token1。

5. **数据呈现规范固化 (写进 v8 设计 token):** PnL 绿涨红跌但**降饱和 + 方向角标 (不只靠色)**; 价格**双格式 (¢ + %)** 并列; 数字 **200-300ms 平滑过渡不闪烁** + "Updated Xs ago" 新鲜度戳 (挂我们 R-20 四时间戳, as_of_ts → 看板新鲜度)。这套与 v6 调研一致, v8 延续即可。

---

## 6. 来源 (Sources)

- [Your Portfolio | Polymarket](https://polymarket.com/portfolio) (登录态, 仅拿到骨架)
- [Positions & Tokens — Polymarket Documentation](https://docs.polymarket.com/concepts/positions-tokens) — split/trade/merge/redeem, 价值/PnL 语义
- [Markets & Events — Polymarket Documentation](https://docs.polymarket.com/concepts/markets-events) — Event/Market 层级, 单 vs 多 market
- [Get current positions for a user — Polymarket API](https://docs.polymarket.com/api-reference/core/get-current-positions-for-a-user) — **持仓字段一手契约 (§1.2)**
- [Monitoring positions — Polymarket legacy docs](https://legacy-docs.polymarket.com/getting-started/monitoring-positions)
- [Using the Orderbook — Polymarket Learn](https://docs.polymarket.com/polymarket-learn/trading/using-the-orderbook) — bids/asks/shares/total/spread
- [Prediction Market UX/UI Design Patterns — Avark](https://avark.agency/learn/prediction-market-design-patterns) — 三层渐进披露、PnL 配色、双格式、实时更新
- [Open-Source Prediction Market Components (Polymarket-style) — Purrdict](https://www.purrdict.xyz/blog/open-source-prediction-market-components/) — MarketCard/ProbabilityChart/ProbabilityBar/Orderbook/TradeForm/RecentTrades 组件拆解
- [How the Order Book Works — Guru Polymarket](https://gurupolymarket.com/en/tutorials/how-the-order-book-works/)
- [How Prediction Market Order Books Work on Kalshi and Polymarket — DefiRate](https://defirate.com/prediction-markets/how-order-books-work/)

---

> **关联:** 上一版 `docs/RESEARCH/frontend-v6-pro-dashboard-research.md` (专业看板/组件库选型, 结论一致, v8 在其基础上聚焦 Polymarket portfolio/折叠模式)。
> **未 commit (GM 统一合并)。**
