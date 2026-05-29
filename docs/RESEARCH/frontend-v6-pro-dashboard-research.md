# Frontend v6 重设计 — 专业看板调研报告

> **owner:** 研究员 (前端 v6 设计调研)
> **last_review:** 2026-05-29
> **派单:** GM (老板要求 "上网看看专业的网站是怎么做的, 不求多好看, 主要让人用着方便、看数据更直观更详细, 后台开发人员也能得到有用信息")
> **约束背景:** C++ 量化做市系统观测看板; SolidJS + TS + Vite; 深色量化终端风; 跨洋带宽紧 (组件库别太重); 市场结构 赛事→盘口(condition)→单边(token) 双边订单簿; 重功能/可读性, 不重美观。
> **现状基线:** 现 v5.2.0 单屏盯盘终端, 依赖仅 `solid-js`, 无组件库, 后端已暴露 healthz/status/positions/pnl(timeseries+attribution)/risk-rejects/paper-gate/market/book_pair/score/quote 等端点 (见 `frontend/src/types.ts`)。

---

## 0. TL;DR (给 GM 的三句话)

1. **组件库推荐: solid-ui (shadcn-for-solid, 基于 Kobalte + corvu + Tailwind, copy-paste 模式)** — 零运行时依赖、组件代码进我们仓库自己拥有、内置深色模式、自带 DataTable/Card/Tabs/Badge/Tooltip 正好覆盖 dashboard 需求、带宽友好。Kobalte 是它的底座 (a11y 状态机), 我们等于直接拿到 "Kobalte 无障碍内核 + 现成深色样式"。
2. **专业看板共识做法: 密集数据网格 + 模块化可组合面板 + 5 秒看懂系统健康 + 颜色编码状态 (绿涨红跌/绿健康红故障) + 数字平滑过渡不闪烁。** 交易端用 watchlist→订单簿→持仓 三段式; 观测端用 RED/Golden-Signals 框架 (顶部 KPI stat → 中部时序图 → 底部日志/明细)。
3. **v6 应拆成 3 类页面: ① 盯盘主屏 (赛事网格 + 双边订单簿 + quote/edge), ② 持仓/PnL 页 (waterfall + per-market 表 + 时序), ③ 开发者观测页 (feed 连接状态网格 + 延迟/错误率/吞吐 + 风控拒单流 + paper gate)。** 现 v5 单屏塞太多, v6 用左侧导航/标签分页 + 可保存 workspace。

---

## 1. 参考类别一 — 专业交易/做市/盘口监控看板

### 1.1 业界共识做法 (来源汇总)

**密度与可组合性 (Bloomberg / Quantower / Dhan DEXT 共识):**
- 专业交易员要 **最大密度 + 用户自控**, 不是极简。Bloomberg 终端是模块化可组合面板, "每块屏都是可移动 pane, 每个功能都能开独立窗口", 交易员围绕自己的工作流塑形工作区。
- 现代平台走 **空白画布 (blank canvas)**: 用户自己 add/resize/arrange widget (Order Book / Positions / Charts / Watchlist / Strategy Builder / News)。
- 支持 **预设布局 (preset layouts) + 多 workspace 一键切换** + 单屏到多屏可缩放。
- 零售 (Robinhood) vs 机构 (Bloomberg) 密度分层: 零售只给最重要的让用户下钻; 机构要并行多窗。**我们是机构档, 走密集网格。**

**布局模板 (Quantower / 日内交易屏布局共识):**
- 经典三段: **左侧主图占大半 → 右侧 1/3 竖向三段栈 (上: 订单录入/Level 2, 中: watchlist/scanner, 下: Time & Sales)**。
- Positions 面板带 hot-button 工具条 (一键平仓等)。
- Order Flow 面板 = 订单簿热力图 + 一键下单。

**订单簿呈现 (含 Polymarket 二元市场专属):**
- Polymarket 用 CLOB, **单一竖向 book: asks 堆在 bids 上方, 一个 tab 在 yes/no 间切换**。下限价单时系统自动镜像显示对侧 outcome 的单 (二元市场特性)。← 直接对应我们 `book_pair` 的 token0/token1 双边结构。
- 订单簿组件高度可配置, 根据尺寸在 **竖向/横向** 间自适应 (insilicoterminal)。

**信息层级 (Polymarket / 预测市场设计指南 — Avark):**
- **渐进式披露三层:**
  | Layer | 受众 | 元素 |
  |---|---|---|
  | L1 全员 | 概率%、Yes/No 动作、outcome 描述 |
  | L2 活跃用户 | 概率图、成交历史、resolution 标准、仓位 sizing |
  | L3 高级用户 | **完整订单簿、depth chart、规格、组合工具** |
- **双格式概率**: 同时显示 cent 价 ($0.72) + 自然语言% (72%)。
- 多 outcome 用 **水平比例条**, 按队伍/候选人配色, hover 显精确值。
- "Updated Xs ago" 时间戳传达数据新鲜度 (← 对齐我们 4 时间戳契约)。

### 1.2 配色与可读性惯例 (深色终端)
- **绿涨 / 红跌**; 状态: 绿=正常、黄=注意、红=危急 (conditional formatting 动态高亮)。
- **数字平滑过渡 (200-300ms), 不要激进闪烁**; 用微小方向箭头或 "+2%" 角标表示变动方向。
- **降饱和度** 减少焦虑性视觉噪声 (大盘密集数据尤其重要)。
- **颜色无关设计**: 状态指示同时用形状/方向线, 不只靠颜色 (色盲可用性 + 截图传阅可读)。

### 1.3 来源
- [Trading GUI: Building Interfaces for Financial Applications (Somco)](https://somcosoftware.com/en/blog/trading-gui-building-interfaces-for-financial-applications)
- [Prediction Market UX/UI Design Patterns (Avark)](https://avark.agency/learn/prediction-market-design-patterns)
- [Polymarket Orderbook Docs](https://docs.polymarket.com/polymarket-learn/trading/using-the-orderbook)
- [How Order Books Work on Kalshi and Polymarket (DefiRate)](https://defirate.com/prediction-markets/how-order-books-work/)
- [Quantower Trading Platform Overview](https://quantower.medium.com/quantower-trading-platform-detailed-overview-9588203fe666)
- [Trading Screen Layout Setup Guide](https://daytradingtoolkit.com/beginners-guide/trading-screen-layout-setup-guide/)
- [Bloomberg-style terminal density / Dhan DEXT T3 customizable layouts](https://scanx.trade/stock-market-news/companies/dhan-launches-dext-t3-trading-terminal-with-customizable-layouts-and-multi-screen-support/35450783)
- [insilicoterminal Orderbook component docs](https://docs.insilicoterminal.com/documentation/application-elements/orderbook)

---

## 2. 参考类别二 — 开发者/运维可观测性看板

### 2.1 业界共识做法 (Grafana / Datadog / 交易系统 ops)

**页面组织 — overview-first 分层 (Grafana 共识):**
- **5 秒规则**: 看板必须 5 秒内传达系统健康。最重要信号置顶 (latency / error rate / request rate / resource usage)。
- **三段式布局:**
  - **顶部**: 关键 KPI + 整体健康 (stat / gauge)
  - **中部**: 服务级指标, 用 **repeating panels + variables** (一个面板配置自动按变量重复 — 我们可按 feed/market 重复)
  - **底部**: 日志、trace、部署标注 (investigation 下钻区)

**设计框架 (选其一组织指标):**
- **RED** (Rate 速率 / Errors 错误 / Duration 时延) — 面向请求/服务。
- **Four Golden Signals** (Latency / Traffic / Errors / Saturation)。
- **USE** (Utilization / Saturation / Errors) — 面向资源。
- → 我们 feed 健康用 RED + 连接状态; 系统资源用 USE。

**Widget → 指标映射 (Grafana 官方 + groundcover):**
| 指标类型 | 面板类型 | 用途 |
|---|---|---|
| 单值 KPI (当前订阅市场数、活跃信号数) | **Stat panel** (大数字 + sparkline) | 一眼看清 |
| 百分比 / 阈值 (CPU、内存、book 填充率) | **Gauge** | 立即看阈值 |
| 随时间趋势 (吞吐、延迟、PnL) | **Time series 线图** + 阈值带 | 找模式/异常 |
| 延迟分布 (p50/p95/p99) | **Heatmap / Histogram** | 百分位分析 |
| 周期性状态 (连接 up/down 历史) | **Status history / Status grid** | 服务健康态 |
| 分类明细 (各 market、各拒单原因) | **Table** + 条件格式 | 跨维比较 |
| 事件流 (风控拒单、错误日志) | **Logs / Log tail panel** | 关联 metrics→logs |

**交易系统 ops 专属指标 (Luxoft / oneuptime / OMS dashboards):**
- 核心: 成交时间、订单 fill rate、滑点、延迟。
- **OPS (Orders Per Second) vs 延迟** 双轴线图 (左轴 OPS, 右轴 ms)。
- Feed 延迟: histogram (分布) + counter (吞吐) + trace (pipeline)。
- 网络: RTT、jitter、packet loss (← 我们跨洋链路关键)。
- **连接状态/协议失配 → 高 venue 拒单率**, 连接状态是 ops 看板一等公民。

### 2.2 来源
- [Grafana Visualizations (官方面板类型)](https://grafana.com/docs/grafana/latest/visualizations/panels-visualizations/visualizations/)
- [Grafana Observability Dashboards Best Practices (groundcover)](https://www.groundcover.com/learn/observability/grafana-dashboards)
- [Monitor Market Data Feed Latency with OpenTelemetry (oneuptime)](https://oneuptime.com/blog/post/2026-02-06-monitor-stock-market-data-feed-latency-opentelemetry/view)
- [Monitor HFT Latency at Microsecond Granularity (oneuptime)](https://oneuptime.com/blog/post/2026-02-06-monitor-hft-latency-microsecond-opentelemetry/view)
- [Role of Monitoring for Trading Systems (Luxoft)](https://www.luxoft.com/blog/role-of-monitoring-for-trading-systems)
- [Key Metrics to Track Trading OMS Performance](https://www.mdmarketinsights.com/insights/key-metrics-to-track-trading-oms-performance-analyst-centric-dashboards)
- [Create a Dashboard to correlate APM metrics (Datadog)](https://docs.datadoghq.com/tracing/guide/apm_dashboard/)

---

## 3. 参考类别三 — SolidJS 组件库选型

### 3.1 候选对比

| 库 | 样式方案 | 需 Tailwind? | 使用模式 | 体积/带宽 | 深色模式 | 数据密集 dashboard 适配 | 维护 |
|---|---|---|---|---|---|---|---|
| **Kobalte** | Headless (完全无样式) | 否 | npm 依赖, 自带 a11y primitives | 极小 (tree-shake, 只装用到的 primitive) | 自己实现 | 中 (要自己写全部样式) | 活跃, Solid 生态事实标准 a11y 内核 |
| **solid-ui** ✅ | Kobalte + corvu + Tailwind, 预置样式 | **是** (Tailwind) | **copy-paste (CLI 生成代码进你仓库)** | **极小 (零运行时依赖, 只进你用的组件)** | **内置 toggle** | **高 (自带 DataTable/Card/Tabs/Chart/Badge/Tooltip)** | 1.3k★ MIT, 活跃 (shadcn+tremor 双移植) |
| **Park UI** | Ark UI + **Panda CSS** | 否 (用 Panda) | 多框架, 设计 token 体系 | 中 (Panda 构建时生成, 但引入 Panda 工具链) | 内置 | 高 (Ark 45+ 组件) | 活跃, 但绑 Panda 生态 |
| **Ark UI** | Headless (Zag.js 状态机) | 否 (BYO CSS) | npm 依赖, 45+ 无样式组件 | 小 | 自己实现 | 中 (要自己全样式) | 活跃, 跨 React/Vue/Solid/Svelte |
| **SUID** | CSS-in-JS (Material UI 移植) | 否 | npm 依赖, 50+ 组件 | **重** (继承 MUI 复杂度 + 运行时 CSS-in-JS) | Material 主题 | 中 (组件多但偏通用 admin, 非量化终端风) | 活跃, 适合 MUI 老用户 |
| **Hope UI** | CSS-in-JS (Stitches-like) | 否 | npm 依赖 | 中 | 内置 | 中 | **维护放缓 (风险)** — 不推荐新项目 |
| Solid Bootstrap | Bootstrap CSS | 否 | npm 依赖 | 中 | Bootstrap 主题 | 中 (admin 风, 非终端风) | 一般 |
| Flowbite Solid | Tailwind | 是 | npm/copy | 中 | 内置 | 中 | 一般 |

### 3.2 推荐: **solid-ui** (理由)

**为什么是它 (对齐我们五个约束):**
1. **带宽友好 (跨洋紧约束):** copy-paste 模式 = **零运行时依赖**, 组件源码直接进我们仓库, 只打包真正用到的组件, 没有整库 runtime。比 SUID (重 CSS-in-JS) / Hope UI 体积小一个量级。
2. **数据密集 dashboard 现成:** 自带 **DataTable / Card / Tabs / Chart / Badge / Tooltip / Sidebar** — 正好覆盖我们 持仓表/拒单表/feed 状态网格/quote 卡片/盯盘标签页。它还移植了 **tremor-raw** (专门做 dashboard 图表的库), 时序/KPI 图开箱即用。
3. **深色量化终端风:** 内置深色模式 toggle, shadcn 设计语言本就偏暗色专业风, 改 Tailwind theme token 即可定制成量化终端配色。
4. **可维护性 + 自主可控:** 代码进仓库我们自己拥有 (component ownership), 不会被上游 breaking change 绑架; 底座是 Kobalte (Solid 生态最权威 a11y 内核), 等于 "Kobalte 内核 + 现成样式" 两全。
5. **TS 友好:** 全 TS, 与现有 `frontend/src/types.ts` wire 类型无缝。

**唯一代价:** **引入 Tailwind** (现 v5 用手写 `style.css`)。但 Tailwind 在数据密集 dashboard 是净收益 (utility class 改密度/间距极快, 构建时摇树体积可控), 团队学习成本一次性。**这是值得的一次性投资。**

**不选其他的原因:**
- **Kobalte 裸用**: 要从零写全部样式, v6 时间成本高 — 但它是 solid-ui 的底座, 选 solid-ui = 间接拿到 Kobalte。
- **Park UI**: 好, 但绑 **Panda CSS** 工具链, 团队再学一套构建体系; 不如 Tailwind 生态成熟普及。
- **SUID**: **太重** (MUI 复杂度 + 运行时 CSS-in-JS), 跨洋带宽下劣势明显; Material 风也不是量化终端风。
- **Hope UI**: **维护放缓**, 新项目有风险, 直接排除。

### 3.3 来源
- [Best 8 SolidJS UI Libraries: Pros and Cons (yon.fun)](https://yon.fun/solidjs-ui-libs/)
- [solid-ui 官网](https://www.solid-ui.com/)
- [Best SolidJS component libraries (Backlight.dev)](https://backlight.dev/mastery/best-solidjs-component-libraries-for-design-systems)
- [Ark UI 官网](https://ark-ui.com/) / [Park UI 官网](https://park-ui.com/)
- [SUID vs Park UI vs Kobalte StackBlitz 对比](https://stackblitz.com/edit/k1y6zw-4axtmy)
- [Building production-ready data tables with shadcn/ui](https://shadcncraft.com/blog/building-production-ready-data-tables-with-shadcn-ui)

---

## 4. 给 v6 的具体可借鉴点

### 4.1 总体布局 (从单屏 → 多页 + 可组合)
- 现 v5 单屏 (`GlobalBar + PnlSparkline + EventGrid + SecondaryFooter`) 塞太多。v6 改 **左侧窄导航栏 + 顶部全局状态条 (mode/连接/PnL/时间戳) + 主内容区分页 (Tabs/路由)**。
- **顶部全局条常驻关键健康** (5 秒规则): paper/live mode 角标、3 个 WSS 连接灯 (sports_api/clob/user_channel)、活跃信号数、持仓数、近 60s 风控拒单数、`as_of_ts` 新鲜度 "Updated Xs ago"。← 这些字段 `Status` 已全有。
- 提供 **预设布局 + 可保存 workspace** (借 Quantower/Bloomberg), 至少先做 3 个预设页 (见下)。

### 4.2 三个主页面 (多页结构)

**① 盯盘主屏 (Trading Floor)**
- 中心: **赛事网格 (EventGrid)** 按 赛事→盘口→单边 三层, 沿用现有 `EventGroup/ConditionData` 聚合。
- 每个 condition 卡片: **双格式概率 (¢ 价 + %)** + 比分 (score) + 双边订单簿。
- **订单簿借 Polymarket 二元做法**: token0/token1 并排, asks 堆 bids 上方, 显示 best_bid/best_ask/microprice/spread/imbalance (字段 `HalfBook` 已全有) + sequence_no/gap_count 角标 (book 健康)。
- 右侧 quote 面板: fair_value / edge_bps / kelly_fraction / suggested_notional + **AI provenance 角标** (advisory / model_calibrated / predict_ok — XD 红线已在 `Quote` 类型里, UI 必须照 XD-3/4/5 渲染)。
- 概率/价格更新 **平滑过渡 200-300ms + 方向箭头, 不闪烁**。

**② 持仓 & PnL 页 (Portfolio)**
- 顶部 stat: 净 PnL / realized / unrealized (大数字 + sparkline)。
- **PnL waterfall** (gross→fee→gas→slippage→spread→net, `PnlWaterfall` 已有) 用瀑布图。
- **per-market PnL 表** (DataTable, 条件格式绿/红)。
- **PnL 时序** (`PnlTimeseries` buckets) 用 time series 线图。
- 持仓 DataTable: market/outcome/net_qty/avg_entry/mark/realized/unrealized, 行内绿红。

**③ 开发者/运维观测页 (Ops / Observability)** ← 老板特别强调 "后台开发人员也能得到有用信息"
按 **Grafana overview-first + RED/Golden Signals** 组织:
- **顶部 stat 行**: uptime、订阅市场数、活跃信号数、持仓数、近 60s 拒单数。
- **Feed 连接状态网格 (Status grid)**: sports_api / clob / user_channel 三灯 + 每个 book 的 `wss_state` + `gap_count` (gap>0 标黄)。← healthz.threads 各线程状态也铺成 status grid。
- **延迟面板**: 用 4 时间戳契约算分段延迟 — `event_ts→data_source_ts→ingestion_ts→as_of_ts` 各段 RTT, time series + 阈值带; p50/p95/p99 用 heatmap。(跨洋链路这是核心)
- **吞吐**: 每秒消息数 / book 更新数 time series。
- **错误率 / 风控拒单流**: `RiskRejects` 用 Log tail panel (intent_ref/market_id/reason_code/side/size/price/rejected_ts), reason_code 按类聚合成 table。
- **Paper gate 状态卡 (`GatePaper`)**: n_trades / sharpe±se / p_value / hit_rate / max_drawdown / prelim_pass / confirm_pass — gauge + 阈值红绿, 直接告诉开发/风控 "策略能不能上"。
- **数据新鲜度**: 每个数据块显 "Updated Xs ago" (对齐 4 时间戳契约红线 R-20)。

### 4.3 Widget 类型清单 (映射到 solid-ui/tremor 组件)
- **Stat panel** (大数字+sparkline): KPI 行、PnL 汇总 — tremor Card+Metric。
- **Time series 线图**: PnL 时序、延迟、吞吐 — tremor LineChart/AreaChart。
- **Status grid / 连接灯**: feed 连接、线程健康 — Badge + 自定义 grid。
- **Heatmap**: 延迟 p50/p95/p99 分布。
- **DataTable** (条件格式): 持仓、per-market PnL、拒单聚合 — solid-ui DataTable。
- **Log tail**: 风控拒单流、错误事件 — 虚拟滚动列表。
- **Gauge**: paper gate 指标、阈值类。
- **订单簿组件**: 双边 bids/asks 表 + spread/imbalance 头 (自定义, 用 Kobalte 无障碍底座)。

### 4.4 配色/可读性落地
- 深色底; 绿涨红跌 + 黄=注意; **降饱和度**减噪。
- 状态同时用形状 (灯/箭头/图标), 不只颜色 (色盲 + 截图传阅)。
- 数字平滑过渡, 变动用箭头/角标, 不闪烁。
- 顶部常驻 mode 角标 (paper=蓝 / live=红), 防止 paper 误当 live (呼应红线 R-11)。

---

## 5. 风险/取舍提示
- **引入 Tailwind** 是本次唯一架构性改动 (现 v5 手写 CSS)。建议 v6 起步时一次性切换, 老周/小苏过架构评审。
- solid-ui 是 **非官方移植**, copy-paste 进仓库后我们自己维护更新 — 这反而是优点 (不被上游 breaking 绑架), 但需指定 owner 跟踪上游安全更新。
- 不要照搬 Polymarket 的 **社交 feed / 卡片娱乐化 / confetti / XP** 那套 — 那是面向散户的; 我们是内部专业终端, 走 **机构密集网格档**。
- 多页/workspace 是增量目标; v6 MVP 先把 **3 个主页面 + 顶部全局健康条** 做出来即可, 可组合 workspace 留后续。
