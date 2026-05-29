# 前端看板设计评审会 — v3 决议(单屏盯盘终端)

- **主持:** 老雷 (GM)
- **日期:** 2026-05-29
- **触发:** 老板连续指令 — ① 不要分 tab,一个页面聚合,以市场盘口结构挂信息;② 好好设计前端,看到更多信息(量化参数/比分/订单簿等);③ 评估小尤方案;④ 与会含后端·架构·前端·UX·AI·量化·测试·开发调试组,任何人可提想看的信息;⑤ **"页面不能太分散,尽可能在一个页面盯盘"(governing 约束)**
- **与会(12):** 老周(架构)·小卢(后端)·小苏(前端)·小尤(UX)·小邓(AI)·小梁(量化部)·小余(数据/比分)·老钱(CPO)·小宋(测试)·小郑(可观测/调试)·老吴(SRE)·老胡(PM 整合)

---

## §0 GM 定调(governing 原则)

**单屏盯盘终端,不是仪表盘集合。** 老板要的是像专业交易终端那样**一屏盯盘**:核心决策信息一眼直显、不靠切 tab/下钻/翻页。折叠只允许用于真正次要的内容(raw metrics 文本、调试/运维明细)。任何"为了整洁把交易决策信息折起来"的设计一律驳回——交易员盯盘时不该点开才看得到 edge/比分/持仓。

信息密度治理靠**视觉分层(颜色/字重/位置)**,不靠**隐藏(折叠/分页)**。

---

## §1 小尤 v2 评估结论

**采纳其信息架构骨架(单页 + market 卡片为主骨架 + 按 market_id join),但修正两点:**
1. 老板"一屏盯盘"覆盖 v2/老钱/老胡的"大量折叠到 L3/L4"——v3 把核心决策信息(比分/fair-edge-Kelly/top-of-book/持仓/PnL/拒单)全部**卡面直显**,不折叠。
2. v2 的"一屏不滚动 18 数字上限"是运维盘纪律,不适用密集交易盯盘;v3 允许卡片网格滚动,但单卡核心信息不超 6 块(老钱红线)。

---

## §2 v3 布局决议(单屏)

```
┌─ 顶部常驻条(全局盯盘摘要,极简) ───────────────────────────────────┐
│ [DEMO 横幅·演示数据非实盘] [PAPER/RUNNING] up · WSS●●○ · 净PnL +$307 │
│ PAPER-GATE Prelim✓ Confirm✗ · p99 · 数据源延迟 · 风控limit用量%      │
├──────────────────────────────────────────────────────────────────────┤
│ [全局净 PnL 曲线 sparkline · 全宽窄条]                                 │
├──────────────────────────────────────────────────────────────────────┤
│ ┌─ market 卡 ──────────┐ ┌─ market 卡 ──────────┐  ← 主盯盘区(网格)  │
│ │ LAL 87–91 BOS Q3 8:42● │ │ ...                  │  每卡核心 6 块直显: │
│ │ Moneyline·LAL          │ │                      │  ①比分/赛况         │
│ │ edge ▐▓▓▒░│░▌ +38bps    │ │                      │  ②盘口+fair/edge/Kelly│
│ │ Kelly 4.2%  fair 0.531 │ │                      │  ③top-of-book+spread │
│ │ bid .483×200 ask .519  │ │                      │  ④持仓+净PnL        │
│ │ 持仓 +120u  PnL +$42   │ │                      │  ⑤拒单角标          │
│ │ 拒单×2                 │ │                      │  ⑥数据源/stale 标记 │
│ └────────────────────────┘ └──────────────────────┘                   │
├──────────────────────────────────────────────────────────────────────┤
│ [▶ 折叠区(仅次要): PnL 归因瀑布 · raw /metrics · 调试/运维明细]      │
└──────────────────────────────────────────────────────────────────────┘
```

**信息分层裁定(老钱 L1-L5 + 老胡五层 + 老板一屏约束 综合):**
- **全局常驻条:** 系统健康红绿灯 + 净PnL + PAPER-GATE + p99 + 数据源延迟 + 风控limit% + DEMO 横幅。运维/SRE/调试只给"红绿灯级"摘要,不堆指标墙。
- **market 卡面(直显,核心 6 块):** 比分/赛况、盘口+fair value/edge_bps/Kelly、top-of-book(best bid/ask+spread+microprice/imbalance)、持仓+该市场净PnL、拒单角标、数据源/stale 标记。
- **折叠区(仅次要):** 订单簿全档、PnL 归因瀑布、AI 模型内部参数(置信度/特征/版本)、raw /metrics、调试(per-thread 延迟/trace/log tail/ring 占用)、运维(进程/资源/RTT)、测试(replay 游标/chaos 状态)。
- **暂不做(后续 sprint):** 控制面写操作(仅 halt 例外)、replay 回放控制、相关性热图、自定义 dashboard。

---

## §3 数据与后端契约决议(老周架构裁定,守 G-FREEZE-W 只增不改名 + R-12 零反向依赖)

| 新增 | 内容 | owner | 优先级 |
|---|---|---|---|
| `MarketInfo.event_id`(追加字段) | market→event 锚,前端按它拉比分 | 小卢(契约)+ 小余/老李(映射) | MVP |
| `EventScore` struct + `score(event_id)` + `/api/v1/score/{event_id}` | 比分/赛况(home/away/score/period/clock/status,vendor-agnostic,源 Goalserve) | 小卢(endpoint)+ 小余(provider)+ 老李/小段(event↔market 映射) | MVP(demo 先行) |
| `QuoteParams` struct + `quote_params(condition_id)` + `/api/v1/quote/{condition_id}` | fair_value/edge_bps/kelly_fraction/signal_strength/model_conf,字段集小梁拍板 | 小卢(endpoint)+ 小梁(Kelly sizing ADR)+ 小袁(供数)+ 老韩(cap 联签) | MVP(demo 先行) |
| `/status` 加 `data_source: demo\|live` | DEMO 标记判定(老钱红线) | 小卢 | MVP |
| `/api/v1/markets` 列表 | 活跃 market 集合(替代前端从 positions 推断) | 小卢 + 老陈/小马 | 后续 |

**解耦原则(老胡关键路径):前端 v3 不等后端真实数据。** 后端先在 DemoStateProvider 填代表性 score/quant **demo 数据**,前端立即可渲染完整盯盘视图(清晰 DEMO 标记);真实 provider(小余 比分 / 小梁·小袁 量化 / 小邓 AI)按各单元排期到位后逐项切真,到位一个点亮一个。

---

## §4 红线 / 纪律

1. **DEMO 标记(老钱红线):** demo 数据必须显式标记——全局常驻 DEMO 横幅(数据源=demo 时强制、不可关闭)+ 每个量化数字(fair/edge/Kelly)旁标 demo/live。未标记 demo 数字 = 等同报假 PnL,P0。横幅切换绑 `/status` 的 `data_source`,不靠人工。
2. **AI advisory 角标(小邓):** paper 期模型输出旁路不下单(ML-R2),fair/edge 等 AI 参数标 "advisory/不下单"。
3. **零反向依赖(R-12)+ 4 时间戳(R-20):** 新 score/quant provider 只读 double-buffer,比分 ts 用 Goalserve 自带,禁本地 now()。
4. **量化展示值 ≤ RM 放行口径(老韩):** Kelly 建议仓位不得超 RiskManager 实际放行,否则误导=红线擦边。
5. **盯盘页不堆工程明细(老钱):** 测试/调试/运维明细进折叠区或留给 Grafana(Sprint-3 观测栈),不污染交易盯盘主区。

---

## §5 分期 + 派单(GM → 主管 → IC)

**v3 本轮(MVP,可立即起):**
- **小卢(A):** 后端 demo 数据先行 — `/status` data_source、`MarketInfo.event_id`、`/api/v1/score/{id}`+`/api/v1/quote/{id}`(DemoStateProvider 填代表性 score/fair/edge/Kelly)。守 G-FREEZE-W/R-12/R-20。
- **小苏(E):** 前端 v3 单屏盯盘终端重构 — 删 tab,全局常驻条 + market 卡片网格(核心 6 块直显)+ 比分条 + fair/edge/Kelly + DEMO 标记 + 折叠次要区。消费现有 + 小卢新 demo endpoint,缺数据优雅占位。
- **小尤(E):** v3 上线前过 UX 评分卡(信息密度/颜色语义/盯盘心流),验收门。

**后续 sprint(各主管排期,老胡 milestone 跟):**
- 小余 + 老李/小段:比分真实 provider + event↔market 映射物化(gameId 填充率达标)。
- 小梁 + 小袁 + 老韩:Kelly sizing ADR + SizingOutput 实现 + cap 联签 → quant 切真。
- 小邓:AI 推理元数据(confidence/区间/model_id/drift)暴露 → AI 参数切真。
- 小郑/老吴:调试/运维指标 endpoint(ring 占用/per-thread 延迟/资源/RTT)→ 折叠区 + Grafana。
- 小宋:replay 游标 / chaos 状态 ReplayStatus 暴露 → 折叠区测试面板。

**关键路径:** 小卢 demo endpoint → 小苏 v3 重构 → 小尤验收 → GM 验收截图。前端不阻塞于真实数据。

---

*纪要 owner: 老雷(GM)。归档: 小米(doc-curator)。各主管会后认领 §5 后续节点回排期。*
