# UX 评审报告 — v3 单屏盯盘终端 (验收门)

- Owner: 小尤 (ux-experience-evaluator)
- Last review: 2026-05-29
- 评审对象: frontend/index.html + frontend/src/{app.js,panels.js,style.css,api.js,stub.js}
- 截图参考: docs/dashboard-v3-screen.png (GM 验证零 console error, 连真 demo provider)
- 设计决议依据: docs/MEETINGS/2026-05-29-frontend-dashboard-v3-design-review.md §0-§4
- UX 框架依据: docs/RESEARCH/xiaoyou-ux-framework-v1.md (D1-D7 评分卡 + 4 色纪律 + 报警噪音红线)

---

## 总评

**有条件通过** — 单屏心流骨架成立,信息分层执行到位,但存在 2 项 P0 缺陷(颜色纪律违规 + 顶部条关键数字可读性),必须改后方算完整通过。

---

## 一、D1-D7 评分卡

```
模块: v3 单屏盯盘终端   评审日期: 2026-05-29   评审人: 小尤

D1 信息密度        [7/10]
D2 阅读路径        [6/10]
D3 错误可读性      [5/10]
D4 报警噪音        [N/A — 本终端不直接生产告警, 依赖外部告警链路; 跳过此维不触发一票否决]
D5 决策延迟 (人)   [7/10]
D6 心流            [7/10]
D7 切换成本        [8/10]

总分: 40/60 (D4 跳过)   平均: 6.7/10
准入: [x] 有条件通过 (P0 项必须改)
```

---

## 二、各维详细打分与理由

### D1 信息密度 — 7/10

**正向:**
截图可见 4 张卡片 (EPL Arsenal/Chelsea, MLB NYY/BOS, NBA LAL/BOS, NFL KC/BUF) 在一屏横向排列,每张卡均包含比分条 + 量化块 + 订单簿 + 持仓行,符合决议 §2 "核心 6 块直显" 的结构。底部次要信息 (raw metrics / PnL 归因瀑布) 折入 details 区,交易决策信息不被埋。整体密度落在框架甜区 (12-18 个核心数字每卡),与 v2 "大量折叠到 L3/L4" 相比是质的进步。

**问题:**
1. 卡片宽度由 `minmax(340px, 1fr)` 自适应。在 1440px 屏下 4 张卡各约 340px,每张卡内部量化参数行 (fair/mid/edge/Kelly) 4 个数字平铺 + demo chip + advisory chip,字号混杂 (10px/12px/13px)。截图观察到卡内文字密度有轻微拥挤,但仍在可读边界内。
2. 顶部常驻条用 `flex-wrap: wrap` — 在 1280px 以下会换行折叠,丢失"常驻"语义。此时操作员看不全 p99/staleness/拒单 等信息,属密度失控而非过载。

### D2 阅读路径 — 6/10

**正向:**
顶部条左侧 "PAPER | RUNNING | up Xs" 做了状态锚点,符合"左上角最该看的"。PnL sparkline 居中全宽,作为全局健康信号也正确。

**问题 (主要减分):**
1. 顶部条 "净PnL" 数字跟在 "WSS 状态点" 后面,视觉路径是: mode-badge → state → uptime → 分隔符 → WSS → 分隔符 → 净PnL。操作员要扫过 4 个 DOM 节点才到最关键的资金数字。净PnL 应该更靠左或视觉加粗对比更强 (目前 CSS `.top-pnl` 只有 `font-weight:700 font-size:13px`,与旁边 state 标签视觉权重接近)。
2. 每张市场卡的 market_id 是原始字符串 (e.g. `epl-ars-che-total`),用等宽字体 11px 渲染。截图可见卡头溢出截断 (CSS `text-overflow: ellipsis`)。操作员无法一眼认出是哪个盘口 — 如果 4 张卡都在 F 视线区,卡头标识是路径第一跳,现在这里读取成本高。
3. 量化参数块内 fair/mid/edge/Kelly 四行没有明显视觉层级:block-label "盘口/量化" (10px 灰) 之下直接平铺所有数字,fair-val (13px 强) 和 mid-val (12px 灰) 差别尚可,但 Kelly 行 (13px 强) 和 fair 行字号完全相同。Kelly 是下单尺寸的关键决策数字,应有额外视觉强调。

### D3 错误可读性 — 5/10

**说明:** 本次评审以代码和截图为依据,实际 error toast/日志链路尚未暴露在前端,评分针对"前端已呈现的错误状态"部分:

1. `safeGet` 捕获任意异常后静默返回 null,UI 显示 "—" 或 "量化未接入"。操作员无法区分"数据真没有"还是"API 调用挂了"。这是 D3 核心缺陷:错误没有"是什么"。
2. "比分未接入" / "量化未接入" / "未接入" 三个占位文案各自独立,风格不统一 (一个中文长句,一个"未接入"两字)。
3. 没有任何错误状态提示"下一步":是正常 (数据尚未接入) 还是异常 (API down)?操作员在凌晨 3 点看到全卡 "—" 不知道该不该动手。
4. 正面: null 安全处理到位,不会崩溃渲染 — 这是底线做到了。

D3 < 6 按框架 §3.2 触发"不能上 P0 路径"规则。本终端属于操作员主盯盘界面 (隐性 P0 路径),因此 D3 = 5 构成 P0 改进项。

### D4 报警噪音 — N/A

本终端自身不生成告警推送,只消费 /status 等 pull 数据。告警噪音评估等 Sprint-3 观测栈 (小郑 alertmanager 链路) 上线后单独做。本轮跳过,不触发一票否决。

### D5 决策延迟 (人) — 7/10

**正向:**
- 核心数字直显,不需要点开折叠/切 tab。从截图到眼睛理解"这张卡有拒单"有明显视觉锚点 (reject-badge 红底红字,位于卡头右侧)。
- edge 方向用颜色 bar 直接可视化,省去心算正负。
- 持仓 PnL 用绿/红 + 加号/减号双重编码,色盲友好。

**问题:**
1. 拒单角标 (reject-badge) 只显示"拒单×N + reason_code 缩写" (截图可见 `KELLY_FRACTION_CAP` 等)。reason_code 是内部枚举值,不是"人话"。操作员需要记住每个 code 的含义。这增加认知负荷,拖慢"看到→理解"的时长。
2. 订单簿块内 "μprice" 标签是希腊字母,在终端字体下对非量化背景操作员可能不直觉。

### D6 心流 — 7/10

**正向:**
- 删 tab 是最大心流提升。v2 要在多个 tab 切来切去,现在 4 张市场卡并排可同时感知,操作员不需要记忆"上一个 tab 是哪个状态"。
- 5s 轮询刷新频率合理,不会造成数字跳动引起额外注意力分散。
- 折叠区默认收起,次要信息不占视线。

**问题:**
1. `grid-template-columns: repeat(auto-fill, minmax(340px, 1fr))` — 如果活跃市场超过 6 个,卡片网格会垂直延伸需要滚动。决议 §1 改动提到"v3 允许卡片网格滚动",但框架 §6.1 说"主盘一屏不滚动"。这是一个已知张力,目前 demo 4 卡刚好不滚动,一旦实盘市场数增加会打破心流。建议在 P2 明确最大同屏卡片数上限或提供分页/筛选。
2. DEMO 横幅在顶部是静态文字条,颜色深橙背景 (#431407) + 橙色文字,每次刷新不变化,操作员长期盯盘容易习惯性忽视 ("banner blindness")。这是一个已知 UX 陷阱,对于"老钱红线"来说存在风险。

### D7 切换成本 — 8/10

**正向:**
单屏设计使"处理一次市场异常"的信息基本在同一屏完成:比分 → 量化参数 → 订单簿 → 持仓 → 拒单角标全部在卡内,不需要切窗口。这是 v3 相比 v2 最大的 D7 改善。

顶部 settings 齿轮藏在右上角 (不打扰盯盘),可接受。

**问题:**
1. 当需要深究拒单原因时,操作员仍需要去折叠区或外部日志确认详情 (前端只显示最近一条 rejectRow 的 reason_code)。这算合理的 D7 成本 (折叠区一步),在可接受范围内。
2. 没有快速链接到 Grafana 对应 panel 或 runbook — 这本轮不要求,但记录为 P2 后续改进点。

---

## 三、颜色纪律专项审查 (4 色纪律 vs 实现对比)

框架 §6.2 规定: 绿=正常/on / 黄=注意 / 红=异常 / 灰=未启用。禁蓝/紫/橙/粉。

**发现:**

| 场景 | CSS 变量 / 实际颜色 | 语义合规? |
|---|---|---|
| PnL 正 | `--green` #22c55e | 合规 |
| PnL 负 | `--red` #ef4444 | 合规 |
| state RUNNING | `--green` | 合规 |
| state DRAIN | `--yellow` | 合规 |
| state HALTED | `--red` | 合规 |
| PAPER badge | `--blue` #3b82f6 底色+边框 | **违规 — 蓝色被框架明确禁止** |
| PnL 归因瀑布 "net" 条 | `--blue` | **违规** |
| score-pre (赛前) 状态 | `--blue` | **违规** |
| wss-off dot | `--border` (灰) | 合规 (未启用语义) |
| DEMO 横幅 + demo chip | `--orange` #f97316 + 橙底 | **违规 — 橙色被框架明确禁止** |
| settings save 按钮 | `--blue` | 这是控件色可接受 (框架针对"语义色"而非交互控件), 边界模糊 |
| advisory-chip | 棕灰 `#57534e` | 合规 (近灰系) |

**P0 违规:**
1. DEMO 横幅使用橙色 (--orange) — 这是老钱 P0 红线的标记机制,用了被框架禁止的颜色。框架说灰=未启用,红=异常,DEMO 应该是"注意但不紧急"→ 黄色。或者,DEMO 标记的语义是"警告操作员不要把演示数据当实盘",可以用红。但不应该是橙色 (框架明确禁)。
2. PAPER badge、score-pre、PnL 归因瀑布 net 条使用蓝色 — 框架明确禁蓝。

需要与小苏协商: DEMO 横幅改黄色 (--yellow) 或红色 (--red) 系; PAPER badge 改灰系 (paper 是正常未激活状态,灰语义合适); score-pre 改灰; 瀑布 net 条改黄 (net 是关键但不是"正常/异常"语义)。

---

## 四、DEMO 标记专项审查 (老钱红线 §4 红线 1)

**要求:** demo 数据必须显式标记 — 全局常驻 DEMO 横幅(不可关闭) + 每个量化数字旁标 demo/live。

**实现评估:**

1. 全局横幅: `#demo-banner` 绑定 `/status` 的 `data_source` 字段自动显示/隐藏,不可关闭 (只有 `hidden` class 控制,没有关闭按钮)。机制正确,满足"不可关闭 + 自动切换"。截图中可见横幅显示"演示数据·非实盘 — 所有量化参数仅供参考,不触发下单",文案清晰。合规。
2. 量化参数旁 demo chip: `renderQuoteBlock` 在 block-label 行渲染 `demoBadge(isDemoData)` — 即 `[demo]` chip 出现在"盘口/量化"标题旁。截图可见。合规。
3. 卡头 stale-group 区域另有 `demo-chip-sm` "DEMO" 小标记。双重标记,合规。
4. **潜在风险 (非 P0,P1 级):** `isDemoData` 判断来自 `cache.status.data_source === 'demo'`。如果 `/status` 请求暂时失败 (cache.status = null),则 `isDemoData = false`,demo 横幅消失、量化数字旁 demo chip 消失,造成"demo 数据无标记"的时间窗口。`safeGet` 的 catch 返回 null 会触发此场景。建议: status 拉取失败时应 fallback 到"假设 demo"而非"假设 live"。

---

## 五、比分/量化/订单簿/持仓各块视觉优先级审查

决议 §2 定义的 6 块直显顺序: ①比分赛况 → ②盘口+fair/edge/Kelly → ③top-of-book+spread → ④持仓+净PnL → ⑤拒单角标 → ⑥数据源/stale。

**实现顺序:** 卡头 (市场 ID + 拒单角标 + stale) → ①比分条 → ②量化参数块 → ③订单簿块 → ④持仓块。

**评估:**
- 拒单角标 (⑤) 被提到卡头,而不是卡底。这符合"拒单是异常信号,应该早看到"的逻辑,是合理的视觉优先级调整。赞。
- stale 标记 (⑥) 同在卡头右侧,和拒单角标并列。截图可见二者都在 header 右侧,视觉权重平等。但 stale 是"数据新鲜度信号",拒单是"风控异常信号",两者语义权重不同,不应等权。建议拒单用更高对比度/更大字号,stale 保持小 chip 形态 (目前 9px)。当前实现已接近合理,差异微小,记为 P2。
- 比分条在卡的最上方 (卡头下),视觉上是最显眼的内容块。截图中比分 "LAL 87 – 91 BOS Q3 8:42 ●" 确实第一眼就看得到。符合操作员"先看赛况再看边际"的心理模型。
- 持仓块 (④) 在卡的底部,包含 outcome + qty + avg + mark + pnl 五个字段。每个 pos-row 用 `flex-wrap: wrap`,在 340px 宽卡下五列可能换行显示,操作员需要阅读两行才能完整理解一个持仓。这是密度和宽度的张力,340px 是目前最小卡宽。记为 P1 (持仓行布局优化)。

---

## 六、P0 / P1 / P2 改进清单

### P0 — 阻塞,必须改才算完整通过

**P0-01 颜色纪律违规 (CSS :root + 实际用色)**
- 违规点: `--blue` (#3b82f6) 用于 PAPER badge、score-pre 状态、PnL 归因瀑布 net 条。框架 §6.2 明确禁蓝。
- 违规点: `--orange` (#f97316) 用于 DEMO 横幅背景/边框/chip。框架 §6.2 明确禁橙。
- 可执行改动:
  - `style.css :root` 删掉 `--blue` 和 `--orange` 语义色变量(保留交互控件色可另命名);
  - PAPER badge 改用 `#374151` 底色 + `#9ca3af` 文字 (灰系,表示"未激活/paper");
  - LIVE badge 保持红系 (已合规);
  - DEMO 横幅改用 `--yellow` (#f59e0b) 边框 + 深黄底 `#451a03` (近棕) 或改 `--red` 系 (警告强语义);
  - score-pre 改灰 (`--text-dim`);
  - `.wf-net { background: var(--yellow) }` (net 行改黄)。
- 文件: `frontend/src/style.css` 第 26-28 行 (--blue/--indigo/--orange 变量) + 第 125-126 行 (badge-paper) + 第 298-299 行 (score-pre) + 第 624 行 (wf-net)。

**P0-02 API 失败时 demo 标记消失的安全默认值**
- 场景: `/status` 请求失败 → `cache.status = null` → `s.data_source` undefined → `isDemo = false` → DEMO 横幅消失、demo chip 消失 → 操作员看 demo 数字以为是实盘。
- 可执行改动: `panels.js renderTopBar` 中 `const isDemo = s.data_source === 'demo'` 改为 `const isDemo = s.data_source !== 'live'` (fail-safe: 不确定时假设 demo);同时 `app.js applyTopBar` 中也需要对应处理。
- 文件: `frontend/src/panels.js` 第 52 行; `frontend/src/app.js` 第 118-122 行 (`if (result.isDemo)` 分支)。

**P0-03 错误状态不可读 (D3 = 5,触发"不能上 P0 路径"规则)**
- 问题: `safeGet` catch 后返回 null,UI 只显示 "量化未接入" / "—",操作员无法判断是"正常数据未接入"还是"API 调用失败"。
- 可执行改动:
  1. `app.js safeGet` 在 catch 时不仅返回 null,还写一个全局 `lastFetchError` map (endpoint → error message);
  2. 在卡头或顶部条增加一个 error indicator: 当 N 个 endpoint 连续失败 >3 次时,显示一个简短 chip (例如 "API ERR" 橙色 — 等等,橙色已禁,改红色);
  3. 占位文案改为两档: "数据未接入 (首次)" vs "拉取失败 — 请检查 API Base" (带重试提示)。
- 文件: `frontend/src/app.js` 第 85-92 行 (safeGet 函数); `frontend/src/panels.js` 各 noData / ph 调用处。

### P1 — 本轮可改,不阻塞通过但应在本 sprint 内完成

**P1-01 顶部条净 PnL 位置与视觉权重**
- 问题: 净 PnL 是操作员最关心的全局数字,但在顶部条中排在 WSS 点之后 (第 5 个元素)。
- 建议: 将 `净PnL` 移到 `RUNNING` 状态标签之后紧跟 (即第 2 位),让操作员左扫第一步就命中。CSS 无需大改,只需调整 `index.html` DOM 顺序。
- 文件: `frontend/index.html` 第 27-28 行 (top-pnl span) 前移至 `top-state` 之后。

**P1-02 市场卡头 market_id 可读性**
- 问题: `epl-ars-che-total` 等原始 ID 用等宽 11px 渲染,截断后读取成本高。
- 建议: `renderMarketCard` 中增加一个简短的"display name"派生逻辑 (如把 `epl-ars-che-total` 拆解显示为 `EPL · ARS vs CHE · Total`),或者在 `/api/v1/market/{id}` 响应中增加 `display_name` 字段由后端提供。前端实现可在 `panels.js` renderMarketCard 第 229 行加一个 `formatMarketId(marketId)` 工具函数。
- 文件: `frontend/src/panels.js` 第 229 行 `mkt-id` span。

**P1-03 持仓行布局: 5 列在 340px 可能换行**
- 问题: pos-row 用 `flex-wrap: wrap`,outcome + qty + avg + mark + pnl 五列在 340px 下可能分两行显示,操作员需要双行阅读一个持仓。
- 建议: 减少持仓行显示的字段数。avg 和 mark 可以压缩为一个 "avg→mk" 对比显示 (`0.620→0.650`),或将 avg 移入 tooltip。这样 pos-row 变为 3 列 (outcome | qty@mk | pnl) 在 340px 内单行。
- 文件: `frontend/src/panels.js` 第 412-419 行 (pos-row 渲染); `frontend/src/style.css` 第 441-447 行。

**P1-04 status 请求失败时顶部条无任何提示**
- 问题: `refreshTopBar` 中 healthz/status 双请求失败后 `cache.status = null`,顶部条所有字段显示 "—" 但没有明确提示"数据拉取失败"。操作员可能误以为系统正常但数据为空。
- 建议: 当 healthz 和 status 同时返回 null 时,state 标签显示 "API OFFLINE" (红色),而不是静默显示 "—"。
- 文件: `frontend/src/panels.js` 第 60-61 行 (stateHtml 逻辑)。

**P1-05 拒单 reason_code 翻译为人话**
- 问题: reason_code = `KELLY_FRACTION_CAP` / `MAX_POSITION_EXCEEDED` / `MARKET_NOT_ACCEPTING_ORDERS` 等内部枚举值直接暴露给操作员,D5 认知成本增加。
- 建议: 在 `panels.js` 中加一个 `REJECT_REASON_LABEL` map,将 reason_code 翻译为操作员可读中文短句 (如 `KELLY_FRACTION_CAP` → "Kelly 仓位上限"; `MAX_POSITION_EXCEEDED` → "最大持仓超限"; `MARKET_NOT_ACCEPTING_ORDERS` → "市场暂停接单")。这不改变后端,纯前端改动。
- 文件: `frontend/src/panels.js` 第 434 行 (renderRejectBadge)。

### P2 — 后续 sprint 排期

**P2-01 顶部条在 <1280px 下 flex-wrap 导致关键信息换行消失**
- 当操作员在较小屏幕使用时,顶部条换行后"常驻"失去意义。建议设置 `overflow-x: auto` 或对顶部条内容按优先级分组,低优先级组 (`<768px` 时) 折叠进 dropdown。

**P2-02 DEMO 横幅 banner blindness 风险**
- 长期盯盘操作员会对固定位置的静态横幅产生视觉适应。建议: 横幅加微弱 CSS animation (edge glow 闪烁,频率 ≤ 0.5Hz,低于框架上限 1Hz),或者定时 (30min) 弹一次确认提示"您当前在 DEMO 模式下查看演示数据"。

**P2-03 超过 6 张市场卡时的滚动体验**
- 决议 §1 明确允许网格滚动,但实盘市场增加后长页滚动会打破"单屏盯盘"心流。建议加 market 筛选器 (按运动项目/状态) 或卡片固定行数上限 (操作员自选关注市场列表)。

**P2-04 拒单角标与 stale 标记视觉权重区分**
- 当前拒单 badge 和 stale chip 在卡头视觉权重接近。建议拒单 badge 加大字号至 11px (当前 10px) 并加左侧红色竖条 border,与 stale chip 形态拉开差异。

**P2-05 快速链接到 Grafana / runbook**
- 每张市场卡的拒单角标 hover 时,在 tooltip 里加 Grafana 查询链接 (市场 ID 过滤),方便操作员一步跳转。待 Sprint-3 观测栈 (小郑) 上线后实施。

**P2-06 uptime 格式可读性**
- 当前显示 `up 3721s`,对操作员不直觉。建议格式化为 `up 1h 2m` 或 `up 62m`。
- 文件: `frontend/src/panels.js` 第 64-65 行。

---

## 七、验收结论

**有条件通过。**

v3 单屏盯盘终端的核心架构决策 (删 tab / 卡片网格 / 核心 6 块直显 / 折叠次要区) 执行到位,盯盘心流相比 v2 有质的改善,决策信息不被折叠隐藏。DEMO 标记机制在正常链路下合规,绑定 `/status.data_source` 自动切换。

**阻塞项:** P0-01 (蓝色/橙色违反 4 色纪律)、P0-02 (status 失败时 demo 标记安全默认值)、P0-03 (错误状态不可读,D3=5 触发框架禁入规则)。三项必须改完,小尤复核,方可正式过验收门。

P1 项建议本轮 sprint 内由小苏跟进。P2 项进 backlog,后续 sprint 排期。

---

*报告归档: docs/RESEARCH/xiaoyou-v3-dashboard-ux-review.md*
*后续跟进: @小苏 (P0/P1 改动) @老胡 (验收门状态更新)*
