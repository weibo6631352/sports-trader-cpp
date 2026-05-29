# UX 体感评审报告 — Dashboard v8 Live 看板

owner: 小尤 (UX)
last_review: 2026-05-30
评审对象: frontend/src/ v8 Material 折叠版 + 截图 docs/dashboard-final-live-honest.png
评审环境: 后端 http://127.0.0.1:8080 真实运行 (mode=paper, 10 outright 市场, 无持仓, WSS 全断)

---

## 0. 先说结论

系统运转正常，但当前状态是「真实 paper 启动期」：WSS 全断、positions 空、PnL $0.00、quote 都是 stub/advisory 模式。UI 如实呈现了这一切，没有撒谎。但**几乎每块关键区域都在用「空值占位符」说话，新操作员看到的第一眼是一片「—」和「等待...」**，认知负担极高，不知道系统是健康的还是坏了。

总评: **可用，但「诚实」代价太高，需要补充状态语境。**

---

## 1. D1-D7 评分卡

| 维度 | 分 (1-10) | 要点 |
|---|---|---|
| D1 信息层级 | 7 | AppBar 主状态 → Tab → Accordion 赛事 → 折叠行 → 展开三列，层级清晰；但 outright 赛事头「—」太多，打乱节奏 |
| D2 扫读路径 | 6 | 折叠行 10 列扫读顺序合理（类型→名称→买卖→edge→持仓→PnL→拒单→延迟）；问题是 10 列在 1080p 会挤压，延迟列容易截断 |
| D3 直觉触发点 | 7 | 折叠/展开箭头 + hover 颜色变化做得好；LIVE badge 闪烁吸引注意；但 WSS 断连 3 个红点在 AppBar 密集信息中容易被淹没 |
| D4 Material 落地质量 | 8 | SUID Chip/LinearProgress/Table 组合一致；Accordion 用原生 CSS max-height 动画代替 SUID（库未收录）可接受；偶有 Typography 组件与自定义 CSS 字号打架（.mono-sub 11px vs MUI caption 12px 混搭） |
| D5 颜色语义 | 7 | 绿/红/黄三档一致；WSS 状态用 StatusDot 颜色区分清楚；acc-dot 接单中用 #666 (灰)不够直觉，容易误读为「非活跃」 |
| D6 错误信息可读性 | 6 | 「量化未接入」「订单簿未接入」「后端离线」是人话；「无持仓 — 等待 paper runtime」略技术腔；拒单 reason_code 已有中文映射，好；缺少 WSS 全断时的全局 Alert |
| D7 信息密度/疲劳感 | 5 | AppBar 单行塞了 11 个信息块（Logo/PAPER/LIVE/RUNNING/赛事数/净PnL/运行时/WSS/Gate/拒单/设置），1024px 以下会折行，读完要 3+ 秒 |

**加权总分: 6.6 / 10**

---

## 2. P0 问题 (必须修，上线阻断)

### P0-1: WSS 全断无全局 Alert，操作员无感知

**现象**: /status 返回 wss_connected: {sports_api:false, clob:false, user_channel:false}，三条 WSS 全部断开。AppBar 只有三个小红点，折叠在 AppBar 密集信息流里，不抢眼。如果操作员正盯着 Trading 页折叠行，根本不会注意到 WSS 状态。

**影响**: 全部 book/quote 数据可能是陈旧快照，操作员在不知情的情况下看到的是过期数据，此时决策结果不可预期。

**组件定位**: `StatusBar.tsx` 缺少全局 Banner 或 Alert 组件；`TradingPage.tsx` 顶部区域。

**建议文案**: 在 TradingPage 顶部插入 SUID Alert severity="error"，文案：「WSS 全部断连 · 订单簿数据可能已过期 · 请检查网络或 /status」。WSS 部分断时 severity="warning"。

---

### P0-2: market.found=false 时赛事行标题退化为 slug 英文全句

**现象**: /api/v1/market/{condId} 返回 found=false（outright 市场 market info 接口未就绪）。EventAccordion 的 homeTeam/awayTeam 回退到 `evSummary.title.split(' vs ')` 拆分，但 title 是「Will the Carolina Hurricanes win the 2026 NHL Stanley Cup?」，split(' vs ') 拆不出两段，结果是 `homeTeam = 整句话`、`awayTeam = '—'`。UI 呈现：整句英文挤在一列，右边跟着「—」。

**影响**: 10 个 outright 市场全部如此，赛事头信息错乱，可读性极差。

**组件定位**: `TradingPage.tsx` `EventAccordion` 中 homeTeam/awayTeam 逻辑（第 635-636 行），以及 `EventGrid.tsx` EventHeader 的 score 空态 fallback。

**建议**: 检测到 `!score && !title.includes(' vs ')` 时，直接用 `eventTitle` 整句作为单行赛事名展示，不要强行 split，不显示「—」。可用 `v8-evt-title` 全宽单行布局代替双队伍格式。

---

## 3. P1 问题 (强烈建议修，影响日常操作体验)

### P1-1: 净PnL $0.00 无语境，用户不知是「正常启动」还是「统计失效」

**现象**: AppBar 常驻显示「净PnL $0.00」（$+$0.00，见截图 dashboard-final-live-honest.png）。positions 空，attribution 未返回有效数据。

**问题**: 对新接手操作员来说，$0.00 与「系统坏了 PnL 不计算」视觉上无法区分。

**组件定位**: `StatusBar.tsx` 第 148-162 行 净PnL 区块；当前逻辑是 `netPnl() == null` 才显示「等待 paper runtime」，但 netPnl 实际返回了 0 (attribution 有 waterfall.net=0)，所以显示 $0.00 而非等待文案。

**建议文案**: 当 `fills_count === 0`（无成交记录）时，在 $0.00 旁加括号说明「(无成交)」或 tooltip 说「paper 启动中，尚无成交记录」。不要让 $0.00 裸奔。

### P1-2: outright 赛事 score 空时，sport Chip 也空（sport 字段为空字符串）

**现象**: /api/v1/events 返回的所有 outright 事件 sport 字段为空字符串 ""。EventAccordion 中 `sportZh()` 返回空，sport Chip 不渲染。没有运动类型标签，配合英文全句标题，赛事类型完全不可辨认。

**组件定位**: `TradingPage.tsx` EventAccordion 第 671 行 `<Show when={sportZh()}>` 正确做了空值保护，但上游 sport 字段本身是空字符串，须在 store/api 层或 EventAccordion 层对 slug 做 fallback 推断（slug 含 "nhl" → 冰球，"nba" → 篮球，"fifa-world-cup" → 足球）。

**建议**: 在 `inferSportFromSlug(slug: string)` 中用关键词匹配，注入到 sport 显示逻辑，i18n.ts 中 SPORT_ZH 覆盖 "ice_hockey"/"nhl"/"nba"/"fifa" 等。

### P1-3: quote advisory=true + model_calibrated=false 时，摘要行 Edge 列「—」与展开区 advisory 角标信息割裂

**现象**: live 数据 quote 返回 edge_bps=0, advisory=true, model_calibrated=false。折叠摘要行 Edge 列显示「—」（因为 edgeBps=0 被 `edgeBps() != null` 通过，但视觉上绿色0bps和「—」间缺乏语义区分）。展开区有 ADVISORY 角标和「未校准」chip，但摘要行没有任何 advisory 提示。操作员从摘要行无法知道当前 edge 不可信。

**组件定位**: `TradingPage.tsx` `MarketSummaryRow` Edge 列（第 240 行），缺少 advisory 状态标注。

**建议**: 当 quote.advisory=true 时，Edge 列改为显示 `[ADV]` 标注而不是 bps 数字，或在 edge 数字旁加一个警告色小点。

### P1-4: PnL Sparkline 区域「PnL 净值曲线加载中...」长期占位

**现象**: 截图 dashboard-final-live-honest.png 可见 sparkline 区域仅显示「PnL 净值加载中...」斜体文字，背景空白。实际上 timeseries 端点返回数据正常，但 paper 模式没有历史 bucket，vals.length < 2 触发 fallback。

**影响**: 这个区域长期是「加载中」斜体字，用户不知道是真在加载还是无数据。

**组件定位**: `TradingPage.tsx` spark-section 内 `PnlSparkline` 组件，以及 `AnalyticsPage.tsx` PnlTimeseriesSection fallback 文案（第 168 行：「PnL 时序数据加载中... (需要 ≥2 个 bucket)」）。

**建议文案**: 「PnL 时序暂无数据（尚无成交记录）」，并明确说明数据满 2 个 bucket 后自动绘制。避免「加载中」误导。

---

## 4. P2 问题 (建议改，改善体感)

### P2-1: AppBar 信息密度过高，11 个块挤一行

AppBar 在 1280px 宽度下呈现流畅，但 1024px（常见外接副屏）开始折行，LIVE/RUNNING/拒单/Gate 挤在一起，视觉层次崩塌。建议：把「Gate —」（无数据时）、「拒单/60s 0」（零值时）收起来，只在有值/异常时弹出。零值无信息量，不需要常驻。

**组件定位**: `StatusBar.tsx` 第 182-215 行。

### P2-2: 折叠行 10 列 grid 在 1080p 标准宽度下延迟列被截断

`style.css` v8 market-list-header 的 grid-template-columns 把延迟列定为 60px，但含内容时实际会 overflow hidden，操作员看到「14」而非「14ms」，削弱延迟信息价值。

**组件定位**: `style.css` .v8-market-list-header grid 定义（第 924-927 行附近）。

### P2-3: acc-dot 接单状态颜色语义反直觉

`acc-dot-ok` (接单中) 是 #666 灰色，`acc-dot-off` (不接单) 是红色。正常交易状态反而是灰点，视觉权重与重要性相反。接单中应为绿色，不接单保持红色。

**组件定位**: `style.css` .acc-dot-ok (第 338 行)。

### P2-4: 折叠行「无持仓」纯文字，与有持仓时「Team 10.0k」视觉落差过大

无持仓时持仓列只有灰色「无持仓」，有持仓时是白色粗体，这个对比在初看时正常，但10行全是「无持仓」时整个列颜色统一成灰色，视觉疲劳，让操作员停止关注持仓列。

**建议**: 无持仓时保持「—」占位即可，省掉「无持仓」文字，减少噪声。

### P2-5: OpsPage 「系统健康」卡片线程区 stub 标注混在内容里

`OpsPage.tsx` 系统健康区线程心跳行（第 204 行）有 `<span class="uncalib-chip">stub (W10+ 接 watchdog)</span>` 内嵌在 UI 里。这是内部注释型信息，放在面向操作员的卡片里会造成疑惑：「stub 是什么意思？watchdog 没接？系统坏了？」应改为 tooltip 或从操作员 UI 移除，改放 Ops 内部文档。

---

## 5. 流畅点 (做得好，值得保留)

- v8-event-body max-height 过渡动画 0.28s cubic-bezier 节奏感好，折叠/展开不突兀
- sessionStorage 保留展开状态，刷新不丢失，省去重新展开操作
- 全展/全折快捷按钮位置合理（工具栏右侧），KeyDown 键盘支持到位
- 搜索框 live 过滤响应即时，team/slug/eventId 多字段匹配覆盖充分
- DualBook vig badge 颜色三档（绿/橙/红）直觉正确，vig<2% 绿让交易员立刻识别流动性
- REJECT_REASON_ZH 中文映射 + 拒单 tooltip 内容完整，报错是人话
- PnlSparkline 零线虚线 + 面积渐变方向语义清楚
- Gate Paper 门禁仪表 Sharpe/drawdown/命中率 布局整洁，数字优先
- Prometheus 裸文本折叠区是好的兜底：操作员可以自助查原始数据，不必开终端

---

## 6. 后端数据层观察（给小郑/小卢）

以下现象在 UX 层可见，根因在后端，转给相关同学：

1. /api/v1/events 所有 outright 事件 sport 字段返回 "" 空字符串（不是 null）。建议后端补 sport 推断逻辑（从 neg_risk_market_id 或 slug 推断）或 UI 层用 slug fallback。
2. /api/v1/market/{condId} 对 outright 条件全部返回 found=false，market info 缺失。book/quote 正常有数据但 market 的 tokens/outcome 字段为 null，导致 UI 推断 outcome 标签为「—」。
3. /api/v1/score/{eventId} 对 outright 事件全部返回 found=false（正常，outright 无进行中比赛）。但 EventGrid.tsx fallback 直接显示「—」而非「预测市场（无实时比赛）」之类的语境文案，需要 UI 层处理。
4. WSS sports_api/clob/user_channel 三条全为 false，但 /healthz 返回 ok=true。这是 paper 启动期正常现象，但 UI 层没有区分「paper 启动中 WSS 未就绪」与「生产 WSS 断连」两种状态，操作员看到同样的红点，焦虑感不同。

---

## 7. 总结评分

| 类别 | 分 |
|---|---|
| 信息架构 | 7/10 |
| Material 实现质量 | 8/10 |
| 空态/错误文案 | 5/10 |
| 操作员认知负担 | 5/10 |
| 键盘/交互可达性 | 7/10 |
| **加权综合** | **6.4/10** |

**准入结论: 有条件准入（P0 修完后可上线内部测试，P1 须在 Sprint-1 内完成）**

---

## 8. 给上级的一句话结论

> 系统数据真实、架构清晰，但 paper 启动期的空态（WSS 断连无全局告警、outright 赛事标题错乱、PnL $0.00 无语境）会让操作员第一眼以为系统故障，须在 Sprint-1 内补充状态文案和 WSS 全断 Alert，再交付内部操作员使用。
