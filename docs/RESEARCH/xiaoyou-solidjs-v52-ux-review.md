---
owner: 小尤 (ux-experience-evaluator, E-047)
last_review: 2026-05-29
sprint: Sprint-3 W10
relates_to:
  - frontend/src/App.tsx
  - frontend/src/components/EventGrid.tsx
  - frontend/src/components/GlobalBar.tsx
  - frontend/src/components/PnlSparkline.tsx
  - frontend/src/components/SecondaryFooter.tsx
  - frontend/src/store.ts
  - frontend/src/i18n.ts
  - frontend/src/style.css
  - docs/dashboard-v5-event-grouped.png
  - docs/RESEARCH/xiaoyou-ux-framework-v1.md
---

# UX 验收报告 — SolidJS 迁移后 v5.2 看板

- **评审人:** 小尤 (ux-experience-evaluator)
- **评审日期:** 2026-05-29
- **评审对象:** commit f57a0c9 迁移后 SolidJS+TS+Vite 版前端 (v5.2)
- **参照基线:** docs/dashboard-v5-event-grouped.png (v5.1 截图)
- **去乱规则核查:** R1-R6 (见 style.css 头部注释)
- **UX 评分卡框架:** docs/RESEARCH/xiaoyou-ux-framework-v1.md §3

---

## §1 D1-D7 评分卡

```
模块: SolidJS v5.2 看板 (完整)
评审日期: 2026-05-29
评审人: 小尤 (+ 建议小宫 dogfood 同步)

D1 信息密度        [7/10]   备注: 见下
D2 阅读路径        [7/10]   备注: 见下
D3 错误可读性      [6/10]   备注: 见下
D4 报警噪音        [N/A]    备注: 本次评审对象为 Web 看板, 无 alertmanager 配置; 跳过 D4
D5 决策延迟 (人)   [7/10]   备注: 见下
D6 心流            [7/10]   备注: 见下
D7 切换成本        [8/10]   备注: 见下

总分 (去 D4): 42/60  平均: 7.0/10
准入: [x] 有条件通过 (列改动见 §4)
```

### D1 信息密度 (7/10)

加分点:
- 赛事分组卡 (EventGroupCard) 把同一赛事的多盘口并列在一张卡内, 操作员一眼扫完胜负盘/大小盘/让分盘, 无需在不同区域跳转. 信息密度甜区内.
- 顶部 GlobalBar 把 模式/状态/净PnL/WSS/Gate/p99/延迟/拒单 压缩成一行, 满足"左上角最关键"原则.
- PnL Sparkline 作为单独区块, 48px 高, 占用垂直空间克制.
- SecondaryFooter 默认折叠, 瀑布图/Prometheus raw text 不挤主视野.

扣分点:
- MiniHalfBook 内字段较密: outcome/WSS点/gap点/bid+ask/价差+imbalance-bar/深度3档/seq, 在 min-width 220px 的列里 10px 以下字段重叠风险高 (尤其深度列头 .mini-col-lbl 是 9px). 凌晨 3 点眯眼读 9px 数字是痛点.
- 单赛事有 3 个盘口 (ml/total/spread) 时, 每列内纵向区块层叠: 量化决策/双边书/持仓 三段全展开, 单张赛事卡总高度易超过 600px. 多赛事时主盘需要大量纵向滚动, 不符合 D1 框架"不滚动"理想 (实际操作员屏幕 1440p 情况下约 2-3 张赛事卡可见).
- 主盘无"赛事数量摘要"计数器, 操作员无法瞬间知道当前监控了几场比赛.

### D2 阅读路径 (7/10)

加分点:
- 视觉层级清晰: 赛事头 (#0d1520 深蓝背景) > 盘口标题 (bg3) > 量化区 (bg3) > 订单簿区 (bg 最深) > 持仓区 (bg2), 3 档背景色分区明确, 眼睛能自然落区.
- 最重要的数字 (净PnL) 在 GlobalBar 左侧第二位, 跟"系统正常/运行中" 紧邻, 符合 F-pattern 主路径.
- RejectDot (×N) 右上角绝对定位, 视觉突出点在预期位置.
- bid/ask 采用 .mono-main 14px bold, Kelly % 同等字号, 主要决策数值突出.

扣分点:
- EventHeader 中: 运动标签/主队/主队比分/短横线/客队比分/客队/分隔符/节次/时钟/状态/Polymarket链接/DEMO角标/stale点 共 13 个视觉元素排成一行 (flex nowrap), 在赛事头里眼睛要从左到右扫完. 操作员想快速确认"现在哪队领先几分"需要在这 13 个元素中定位, 稍微费力.
- CondQuote 的"公允/中间"标签 (10px .q-lbl) 与数值毗邻但字号差距大 (10px vs 14px), 第一次用的操作员容易把"公允"和数字的关系读混.
- 盘口列顶部的 RejectDot 是绝对定位 (top:4px right:6px), 与 cond-header 在视觉上叠加. 实际渲染时 reject-dot 浮在 cond-type-label 右上方, 在某些分辨率下会和 NR 标签重叠 (两者都在右侧区域). 需要实测确认.

### D3 错误可读性 (6/10)

加分点:
- "API 异常" chip 在顶部常驻条出现, 连续 3 次失败后显示 (isEndpointFailing 阈值 3). 有颜色+文字+动画三件套.
- 订单簿区分"未接入" (fallback 占位) 和"拉取失败" (fail-chip), 两种情况语义不同.
- 量化区 "量化未接入" + "demo" 角标组合, 区分了"功能未部署"和"演示数据"两种语义.
- DEMO 横幅 (#demo-banner) 黄色全宽醒目, 非实盘保护有效.

扣分点 (本次 6 分主要扣这里):
- GlobalBar 的 stateText 在 API 不可达时显示 "API OFFLINE" (英文), 在连接中显示"连接中...", 但没有给操作员任何"下一步"提示. 符合 D3 框架"三件套"中缺第 3 件 (下一步做什么).
- 占位符文案"无市场数据 (positions 为空)" 仅描述现象, 未给下一步 (是 API 问题? 是真的没持仓? 后端异常?).
- PnlSparkline 的 fallback "PnL 曲线加载中..." 在 API 不通时会永远显示, 无法与"正在加载"区分. 操作员不知道是等还是去查问题.
- fetchErrorMap 的错误内容 (e.error 字段) 没有呈现给操作员 — 后端返回的错误文本被静默丢弃, 操作员能看到的只有"API 异常" chip, 无具体哪个 endpoint 挂了. (fetchErrorMap 数据有但未渲染)
- 错误信息全部是中文短句, 没有错误码. 不符合框架 §5.2 错误码 5 段编码要求. 操作员写 incident 时无法引用具体错误码.

### D5 决策延迟-人 (7/10)

加分点:
- 状态颜色即时可辨: RUNNING 绿 / HALTED 红 / 其他黄, 不需要读文字即可判断系统状态.
- RejectDot 角标 tooltip 展开后有 "原因 · 方向 数量 @价格" 格式, 能在 hover 内完成诊断, 减少跳窗口.
- Gate chip (初审/确认 ✓/✗) 双态直接显示, 操作员无需 hover 即可判断 paper gate 状态.
- StaleDot 三色 (灰/黄/红) + title tooltip 带延迟数值, 符合"颜色+文字+图标"三件套.

扣分点:
- wss_connected 展示的是 3 个灰色/红色小圆点, 没有文字标签直接标注哪个 WSS 连接挂了 (hover tooltip 是 key 名, key 名是英文字段名如 "sports_api" / "clob" / "user_channel"). 操作员在凌晨看到一个红点, 需要 hover 才知道是哪路 WSS. 判断延迟 +1-2s.
- p99 显示 "p99 / XXX us", 但哪个 loop 的 p99 不标注 (从 metrics 正则提取 stcpp_loop_latency_p99_us 但不显示来源上下文). 量化员/SRE 要看到这个数字时可能不确定它代表什么.
- 没有 kill switch 按钮. 框架 FE-03 要求有 kill switch + 二次确认, 当前版本不存在. (理解可能在 pmctl CLI 层, 但 Web 看板本身没有任何人工干预入口)

### D6 心流 (7/10)

加分点:
- 主盘信息在一个视图内, 赛事分组卡加载后不需要切 tab.
- 轮询自动刷新 (5s/15s/30s), 操作员不需要手动刷.
- SecondaryFooter 默认折叠, 不抢注意力.
- 没有弹窗、toast 通知 — 信息都是原地更新, 不打断视线焦点.
- blink 动画仅用于 .score-live (进行中比赛状态) 和 .api-err-chip, 非常克制, 符合"仅 P0/P1 可闪烁"原则.

扣分点:
- initPolling 启动时同时触发 5 个 every() 调用, 所有 interval 同时 fire. 首次页面打开时会并发打出大量请求 (TopBar + Sparkline + MarketGrid + Attribution + Gate + Metrics), 网络瀑布可能导致初始加载时界面闪动/空态时间偏长, 操作员接班时"进入状态"的那 5 秒体验可能不好.
- 设置面板 (Settings Panel) 是内联展开 (Show/Hide), 没有 ESC 关闭支持, 按钮对焦后 Tab 键行为未知. 键盘友好性弱.
- 没有主题/字号切换入口. 框架 §6.1 提到字号三档, 但目前字号是硬编码, 操作员在高分辨率小屏下无法调大字.

### D7 切换成本 (8/10)

加分点:
- 赛事分组设计把同一比赛的所有盘口整合在一张卡里, 操作员一个视图内可以看 ml/total/spread 三个盘口, 无需切页面. 对比 v4 双边同屏但无分组, 这是显著进步.
- Polymarket 超链接 (.evt-link) 在赛事头直接提供, 需要去 Polymarket 官网确认时单击即跳, 节省查找时间.
- SecondaryFooter 的 PnL 归因瀑布和 Prometheus raw metrics 都在同页折叠区, 不需要切到 Grafana 就能看到基本归因数据.
- API Base 配置在右上角设置按钮, 操作员调后端地址不需要改 .env 重启.

扣分点:
- 发现异常 (如 wss_dot_off) → 诊断具体原因 → 决定动作, 这条路径需要: 看 GlobalBar 红点 → hover tooltip → 确认是哪路 WSS → 去 CLI 或 Grafana 看详细日志. 第 3 步强制离开看板. 窗口切换 = 2 次 (看板 → Grafana/CLI → 回来). 已达框架"≤ 3 次"阈值边缘.
- SecondaryFooter 的 "分市场" PnL 只显示 market_id (原始字符串, 如 "nba-lal-bos-ml"), 没有聚合到赛事级别, 和上方赛事卡的视觉语言不统一. 操作员要心算把哪几个 market_id 对应哪个赛事.

---

## §2 v5 去乱规则 R1-R6 逐条核查

### R1: 比分头去重 (每赛事只渲染一次)

状态: **保留, 通过**

EventHeader 组件只在 EventGroupCard 顶部渲染一次, ConditionColumn 内部没有比分样式 (.evt-score / .evt-team 等类全在 EventHeader 作用域内). store.ts 的 eventScoreCache 按 event_id 去重拉取 Score, 写回时也是 eventGroupMap 级别. R1 严格执行.

### R2: 颜色收敛 (四色语义)

状态: **基本保留, 有一处偏差**

style.css 颜色语义:
- green (#22c55e): bid价/正PnL/正Kelly/edge-pos/score-live/vig-low/gate-ok/wss-dot-ok (存疑, 见下) -- 覆盖正确
- red (#ef4444): ask价/负PnL/gap-dot/reject-dot/state-halted/api-err-chip/gate-fail/wss-dot-off/acc-dot-off -- 覆盖正确
- yellow (#f59e0b): stale-warn/vig-mid/score-ht/state-drain/demo-banner -- 覆盖正确
- 灰色: wss-dot-ok (#4b5563) / stale-dot-ok (#374151) / acc-dot-ok (#4b5563) / score-ft/pre -- R2 规则要求 ok 状态降调为灰, 已执行

偏差项: .wss-dot-ok 用的是 #4b5563 (灰), 符合 R2. 但在 GlobalBar 的 Gate chip 使用了绿色边框/背景, 而 Gate 并非"ok/连接"语义而是"通过/不通过"语义. 绿色在这里是合理的, 无违规.

另: span class="top-sep" 分隔符颜色是 var(--border) = #30363d, 这是中性色, 不占语义色位. 正确.

控件蓝 (--ctrl-blue #3b82f6) 仅用于 btn-primary 和 input focus outline, 未渗入语义色区域. 正确.

整体: R2 通过, 无四色之外的滥用.

### R3: 字号三档

状态: **保留, 通过**

三档定义 (style.css):
- .mono-main: 14px mono bold (bid/ask/Kelly/净PnL) -- EventGrid/GlobalBar 中的核心数值均挂此类
- .mono-sub: 11px mono dim (次要标注: qty/mark/价差/seq 等) -- 覆盖准确
- .q-lbl: 10px 标签 (公允/中间/Kelly/优势/价差 等标签) -- 覆盖准确

额外字号:
- .mini-col-lbl: 9px (深度列头标签) -- 低于三档定义下限, 是本次发现的 P1 问题 (见 §4)
- .mini-depth-row: 10px (深度数据行) -- 与 .q-lbl 同档, 可接受
- .mini-outcome: 10px -- 可接受
- .evt-score: 16px (赛事比分数字) -- 高于 mono-main, 赛事头特例, 语义合理, 不计违规
- .evt-team: 13px -- 接近 body font-size, 合理

整体: 三档体系成立, 9px 列头是边缘违规, 建议升至 10px.

### R4: 色块分隔 (背景色块代替 border-bottom 横线)

状态: **保留, 通过**

盘口列内各区块:
- cond-header: bg3 背景 + border-bottom -- 有 border-bottom, 但同时有背景色, R4 精神是"不用大量纯横线分割", 这里是区块边界一条, 可接受
- cond-quote-section: bg3 + border-bottom -- 同上
- cond-book-section: bg (最深) + border-bottom -- 同上
- cond-pos-section: bg2 -- 无 border-bottom, 纯色块. 完全符合 R4

赛事级:
- event-header: #0d1520 独立背景 + border-bottom 一条 -- 可接受, 区块边界
- event-group: border: 1px solid var(--border), border-radius -- 整体卡片边框, 合理

"区块分隔用背景色块代替 border-bottom 横线" 的精神是避免满屏水平线. 当前实现: 区块边界保留一条 border-bottom 做视觉锚点, 但 R4 的核心背景色三档 (bg/#0d1520/bg3/bg2) 已清晰实现. 通过.

### R5: 拒单/gap 小红点 (右上角绝对定位)

状态: **保留, 通过**

- .reject-dot: 挂在 .cond-col-pos-anchor (position:absolute top:4px right:6px z-index:10), RejectDot 组件 Show when has() 条件渲染
- tooltip 内容: "原因 · 方向 数量 @价格" (多条换行), 信息充分
- .gap-dot: 挂在 mini-half-header 内 Show when gap_count>0, 同样红点+title
- 字号/形态: reject-dot 是 9px mono bold 红底白字 (×N), 比 gap-dot (6px 圆点) 更显眼, 语义优先级符合

通过.

### R6: chip/行禁折行 (overflow 截断)

状态: **保留, 通过**

已检查所有关键容器的 flex-wrap 设置:
- .event-header-main: flex-wrap: nowrap; overflow: hidden; -- 正确
- .cond-header: flex-wrap: nowrap; overflow: hidden; -- 正确
- .cond-quote-row / .cond-edge-row / .cond-kelly-row: flex-wrap: nowrap; overflow: hidden; -- 正确
- .cond-book-header: flex-wrap: nowrap; overflow: hidden; -- 正确
- .mini-half-header: flex-wrap: nowrap; overflow: hidden; -- 正确
- .cond-pos-header / .cond-pos-row: flex-wrap: nowrap; overflow: hidden; -- 正确
- .mini-ba-row / .mini-spread-row: flex-wrap: nowrap; -- 正确

顶部条例外: .top-bar 有 flex-wrap: wrap (响应式), 注释标注"顶部条允许 wrap (响应式)". 合理.

.cond-type-label 有 text-overflow: ellipsis -- 正确截断处理.

R6 全面通过.

---

## §3 v5 设计等价性综合判断 (对比 v5.1 截图)

截图 (docs/dashboard-v5-event-grouped.png) 观察到的 v5.1 核心设计特征, 逐一与代码比对:

| v5.1 特征 | v5.2 代码状态 | 结论 |
|---|---|---|
| PAPER RUNNING 顶部 badge | GlobalBar .badge.badge-paper + .state-label.state-running | 等价 |
| 净PnL 顶部显示 | GlobalBar top-pnl + attribution waterfall.net | 等价 |
| WSS 点阵 | GlobalBar wssEntries() For 循环 | 等价 |
| 演示数据黄色横幅 | GlobalBar #demo-banner Show when isDemo() | 等价 |
| DEMO 角标 per 盘口 | CondQuote/EventHeader Show when isDemoData | 等价 |
| 净值曲线 SVG | PnlSparkline SparklineSvg 手写 SVG | 等价 |
| 赛事分组卡 (赛事头+比分) | EventGroupCard + EventHeader | 等价 |
| 赛事头: 运动标签+主队+比分+客队+状态 | EventHeader 完整实现 | 等价 |
| Polymarket 超链接 | EventHeader .evt-link href=polymarket_url | 等价 |
| 盘口列横向并列 | .event-columns display:flex overflow-x:auto | 等价 |
| 大小盘 / 胜负盘 / 让分盘标签 | inferMarketLabel() i18n.ts MARKET_TYPE_ZH | 等价 |
| 双边订单簿并排 | DualBook .cond-dual-grid grid 1fr 1fr | 等价 |
| bid 绿 ask 红 | .mini-bid var(--green) .mini-ask var(--red) | 等价 |
| 深度三档 | MiniHalfBook bids/asks slice(0,3) | 等价 |
| 拒单小红点 | RejectDot .reject-dot 绝对定位 | 等价 |
| PnL 归因瀑布折叠区 | SecondaryFooter PnlAttributionPanel | 等价 |
| 中文全界面 | i18n.ts STATUS_ZH/SPORT_ZH/MARKET_TYPE_ZH/REJECT_REASON_ZH | 等价 |
| 持仓行 (outcome/qty/mark/PnL) | CondPos cond-pos-row | 等价 |
| 字号三档 | style.css mono-main/mono-sub/q-lbl | 等价 |
| 四色收敛 | style.css :root 语义色 | 等价 |
| 色块分隔 | bg/bg2/bg3/#0d1520 四档背景 | 等价 |

结论: **v5.2 相对 v5.1 无功能退化**. 所有 v5 核心设计特征均在 SolidJS 版中得到等价实现.

SolidJS 迁移带来的改进 (相比 v5.1 legacy JS):
- 类型安全 (TypeScript strict mode), 字段对齐 ADR-038 wire 类型
- per-endpoint 错误追踪 (fetchErrorMap, 连续 3 次失败触发 chip)
- 订单簿拉取失败 vs 未接入 两种状态区分
- score 按 event_id 去重拉取 (store.ts 379 行附近), 减少重复请求
- Solid createStore 细粒度响应, 避免全量重渲染

---

## §4 改进项清单

### P0 — 必须改, 否则阻止上线

**P0-A: 占位符/错误态缺"下一步"指引 (D3)**

受影响组件: GlobalBar.tsx (stateText), EventGrid.tsx (fallback), PnlSparkline.tsx (fallback)

当前: "API OFFLINE" / "加载市场数据..." / "PnL 曲线加载中..." 仅描述现象.

建议: 至少加一行次级文字, 例如:
- "API OFFLINE — 请检查后端连接 (默认 127.0.0.1:8080) 或点击 ⚙ 修改 API Base"
- "无市场数据 — positions 返回空, 请确认后端 /api/v1/positions 是否有数据"
- "PnL 曲线加载失败 — 点击 ⚙ 确认 API Base, 或等待自动重试 (每 15s)"

具体改动文件: `frontend/src/components/GlobalBar.tsx` stateText() 分支; `frontend/src/components/EventGrid.tsx` fallback; `frontend/src/components/PnlSparkline.tsx` fallback.

---

**P0-B: fetchErrorMap 错误内容不呈现 (D3)**

当前: GlobalBar 只展示"API 异常" chip, 但 fetchErrorMap 里存有 failCount 和具体 endpoint path. 操作员无法从 UI 上确认是哪个 endpoint 挂了.

建议: "API 异常" chip 的 title tooltip 或展开区域列出失败 endpoint 路径及 failCount, 例如 title="失败端点: /api/v1/positions (连续 4 次) / /status (连续 3 次)".

具体改动文件: `frontend/src/components/GlobalBar.tsx` hasApiErr() 相关 JSX; 可能需要从 `frontend/src/api.ts` 导出 fetchErrorMap 迭代函数.

---

### P1 — 建议在本 sprint 内补改

**P1-A: WSS 点阵无文字标签, hover 才知道是哪路 (D5)**

受影响: GlobalBar.tsx wssEntries() For 循环

当前: 三个圆点 title=key (英文字段名 "sports_api" / "clob" / "user_channel").

建议: 至少在圆点旁显示缩写 (如 "SA" / "CLOB" / "UC"), 或将 key 名中文化后展示在 tooltip (WSS_STATE_ZH 已有基础, 可扩展 WSS_KEY_ZH). 让操作员不 hover 就能辨认哪路 WSS 挂了.

具体改动文件: `frontend/src/components/GlobalBar.tsx` wssEntries For 块; 可在 `frontend/src/i18n.ts` 补 WSS_KEY_ZH 映射.

---

**P1-B: MiniHalfBook 深度列头 9px 字体低于三档下限 (R3)**

受影响: `frontend/src/style.css` .mini-col-lbl

当前: `font-size: 9px` (低于三档 10px 下限).

建议: 升至 10px. 与 .q-lbl 同档. 若空间不够, 考虑缩短列头文字 ("量↑" 改 "量" 或去掉列头只靠对齐暗示).

具体改动: style.css 第 712 行 `.mini-col-lbl { font-size: 9px; }` 改为 `10px`.

---

**P1-C: SecondaryFooter "分市场" PnL 显示原始 market_id, 与上方赛事卡视觉语言不一致 (D7)**

受影响: `frontend/src/components/SecondaryFooter.tsx` pm-row

当前: `.pm-id` 直接渲染 `m.market_id` (如 "nba-lal-bos-ml").

建议: 用 inferMarketTypeZh 或 i18n 把 market_id 尾缀翻译为中文盘口类型, 或至少把 event_id 前缀提取出来对应到赛事头显示的队名. 保持与上方赛事卡的语义一致.

具体改动文件: `frontend/src/components/SecondaryFooter.tsx` pm-row JSX; 可复用 `frontend/src/i18n.ts` inferMarketTypeZh().

---

**P1-D: RejectDot 绝对定位与 cond-header 右侧元素潜在重叠 (D2)**

受影响: `frontend/src/components/EventGrid.tsx` ConditionColumn; `frontend/src/style.css` .cond-col-pos-anchor

当前: .cond-col-pos-anchor top:4px right:6px 绝对定位, cond-header 内 NR 标签也在右侧区域 (flex-end 方向).

建议: 实测 1080p 下多 reject + NR 同时出现的渲染效果. 若有重叠, 将 reject-dot 改为在 cond-header 内 flex 布局内的最右元素 (去掉绝对定位), 或将锚点移到盘口列右上角 (当前 top:4px 已经是右上角, 但需确认与 cond-header 的 z 层不冲突).

---

**P1-E: initPolling 并发打出大量请求导致初始加载闪动 (D6)**

受影响: `frontend/src/store.ts` initPolling()

当前: 5 个 every() 立即执行, 无 stagger 间隔.

建议: 第一次加载时先串行加载关键数据 (healthz + status + positions), 再启动轮询. 非关键数据 (metrics/gate) 延迟 2-3s 后再首次拉取. 避免接班时页面同时发出 8 个请求导致白屏期延长.

具体改动文件: `frontend/src/store.ts` initPolling() 函数; 可在 initPolling 最开始加一个 await refreshTopBar() + await refreshMarketGrid() 串行初始化, 再启 setInterval.

---

### P2 — 建议 backlog, 下个 sprint 内跟进

**P2-A: 顶部 p99 来源不标注**

GlobalBar 的 p99Text() 从 Prometheus raw text 正则提取, 显示 "XXX us". 没有提示"这是 loop latency p99". 量化员或 SRE 第一次看时不确定含义. 建议 title tooltip 标注 "stcpp_loop_latency_p99_us".

文件: `frontend/src/components/GlobalBar.tsx` p99Text 相关 span.

---

**P2-B: 主盘无赛事/盘口数量摘要**

操作员无法不滚动就知道当前监控几场比赛、几个盘口. 建议在 market-grid 顶部或 GlobalBar 末尾加一行小字 "当前: N 场赛事 / M 盘口".

文件: `frontend/src/components/EventGrid.tsx` 或 `frontend/src/components/GlobalBar.tsx`.

---

**P2-C: 无键盘操作支持 (设置面板)**

Settings Panel 打开后无 ESC 关闭. 操作员用键盘操作时无法不用鼠标关闭面板.

文件: `frontend/src/components/GlobalBar.tsx` settingsOpen 逻辑; 加 document keydown listener onMount.

---

**P2-D: 没有 kill switch / 干预入口**

框架 FE-03 要求 kill switch + 二次确认. 当前看板完全只读. 理解 kill switch 可能在 pmctl CLI, 但建议在 Web 看板加一个"紧急暂停"按钮 (哪怕只是调 API) 减少操作员切窗口次数 (降 D7). 需对齐老韩/老胡需求后再设计.

---

## §5 总评

**准入结论: 有条件通过**

SolidJS 迁移后 v5.2 相对 v5.1 **无设计退化**, v5 核心布局语言 (赛事分组卡/双边同屏/中文/超链接/DEMO保护/四色/字号三档/色块分隔/拒单小红点/不折行) 全部等价保留.

框架评分 7.0/10 (6 维均分), 达到"有条件通过"门槛. 无单维低于 5 分, 无 D3 < 6 (本次 D3 正好 6).

上线前必须完成: **P0-A (占位符加下一步指引) + P0-B (fetchErrorMap 错误信息暴露给操作员)**. 这两项是 D3 错误可读性的基线要求, 不完成则 D3 从 6 分降至 5 分, 触发一票驳回.

P1 项目建议在本 sprint 内补齐; P2 进 backlog.

**P0 清单 (必须改后重报):**

1. P0-A: GlobalBar / EventGrid / PnlSparkline 占位符文案加"下一步"指引
2. P0-B: "API 异常" chip 暴露具体失败 endpoint 路径 (fetchErrorMap 已有数据, 差展示层)

P0 完成后本报告自动升级为"通过"状态, 无需重新打分, 小苏改完在报告后 append 确认即可.

---

**协作动作:**

- 小苏: 请处理 P0-A / P0-B / P1-A / P1-B (P1-B 5 分钟改完, 请优先)
- 小宫: 建议 stub 模式下跑一遍 dogfood playbook, 验证赛事分组卡在 3+ 赛事下的滚动体验
- 老胡: 本报告作为 v5.2 上线前 UX 准入记录存档

---

## §6 P0 修复确认 (2026-05-29 小苏)

**P0-A 和 P0-B 均已完成, 验收状态升为: 通过**

修改文件:
- `frontend/src/api.ts`: 新增 `failingEndpointsSummary()` (遍历 fetchErrorMap 返回失败 endpoint 列表) + `isEndpointFailingPrefix()` (前缀匹配用于带 query string 的 path)
- `frontend/src/components/GlobalBar.tsx`: stateText "API OFFLINE" → "后端未连接", 加 title tooltip "后端未连接 · 请检查 8080 或点 ⚙ 改 API Base"; "API 异常" chip 加 `title={apiErrTooltip()}`, tooltip 列出失败 endpoint 路径 + failCount
- `frontend/src/components/EventGrid.tsx`: fallback "无市场数据 (positions 为空)" → "等待持仓建立 / 后端未接入 · 点 ⚙ 检查"
- `frontend/src/components/PnlSparkline.tsx`: fallback 区分加载中 vs 拉取失败 (isEndpointFailingPrefix 判断 failCount≥3)
- `frontend/src/style.css`: `.mini-col-lbl font-size: 9px` → `10px` (P1-B 同步完成)

构建验证: `tsc --noEmit` 0 errors, `vite build` 通过.

D3 错误可读性: 6/10 → 预期升 7/10 (P0-A/B 补全"三件套"第 3 件下一步指引 + 失败 endpoint 可见)

---

**附: 评分摘要**

| 维度 | 得分 | 阈值 | 状态 |
|---|---|---|---|
| D1 信息密度 | 7/10 | ≥5 | 通过 |
| D2 阅读路径 | 7/10 | ≥5 | 通过 |
| D3 错误可读性 | 6/10 | ≥6 | 边缘通过 (P0 修完可升 7) |
| D4 报警噪音 | N/A | — | 跳过 (Web 看板无 alert) |
| D5 决策延迟 | 7/10 | ≥5 | 通过 |
| D6 心流 | 7/10 | ≥5 | 通过 |
| D7 切换成本 | 8/10 | ≥5 | 通过 |
| **均分** | **7.0/10** | ≥6 有条件 / ≥7 通过 | **有条件通过** |
