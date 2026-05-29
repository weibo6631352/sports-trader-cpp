# dogfood 报告 — 运营/操作员视角 看板盯盘实地使用
owner: 小宫 (dogfood)
last_review: 2026-05-30

## 0. 测试环境

- 后端: `http://127.0.0.1:8080` (paper mode, data=live)
- 前端: `frontend/src/` SolidJS v8 (4 Tab: 盯盘/Ops/PnL/市场详情)
- 截图: `docs/dashboard-final-live-honest.png`
- 测试时间: 2026-05-30 (系统已运行约 4.5 小时, uptime_sec≈16200s)
- 数据: 真实 Polymarket CLOB WSS, 10 个体育市场 (NHL/NBA/FIFA World Cup outright/futures)

---

## 1. 能接通的 endpoint (已验证)

| 路径 | 状态 | 返回质量 |
|---|---|---|
| `/healthz` | 200 正常 | ok=true, 5 线程全 alive |
| `/status` | 200 正常 | RUNNING, paper, data=live |
| `/version` | 200 正常 | 0.1.0, git_hash 存在 |
| `/metrics` | 200 正常 | Prometheus 格式, 但多项值错误 |
| `/api/v1/events` | 200 正常 | 10 个市场, 结构完整 |
| `/api/v1/book/{cond_id}` | 200 正常 | 双边盘口数据, 5档深度 |
| `/api/v1/book_pair/{cond_id}` | 200 正常 | BinaryMarketBookView |
| `/api/v1/quote/{cond_id}` | 200 正常 | fair_value/edge/kelly (advisory) |
| `/api/v1/positions` | 200 正常 | 空数组 (无 paper 成交) |
| `/api/v1/pnl/timeseries` | 200 正常 | 空 buckets (无成交) |
| `/api/v1/pnl/attribution` | 200 正常 | 全零 waterfall |
| `/api/v1/risk/rejects` | 200 正常 | 256 条记录 |
| `/api/v1/gate/paper` | 200 正常 | has_data=false (无成交) |
| `/api/v1/score/{event_id}` | 200 正常 | found=false (见 B3) |

**404 (不存在的 endpoint, 运营误触):**
- `/api/v1/markets` → 404 (前端不调但操作员会猜)
- `/api/v1/pnl` → 404 (需要加 /timeseries 或 /attribution 后缀)

---

## 2. 盯盘流实地体验

### 截图观察 (docs/dashboard-final-live-honest.png)

顶栏: `STCPP PAPER DATA:LIVE RUNNING 10 赛事 PnL +$0.00 运行 1510s WSS ●●● Gate— 拒单/60s 0`
- 10 个赛事全部展示: NHL Stanley Cup (3队) + NBA Finals (3队) + FIFA World Cup (4队)
- 每行有: 赛事标题、重复标题(问题见下)、延迟显示、盘口数

**盯盘页能一眼看到什么:**
- 赛事列表: 10 个, 全部 outright/futures 类型
- 折叠/展开: 点击展开后可看到双边订单簿 (5档深度, bid/ask 分色, 成交量柱状图)
- 价格: bid/ask 显示正常, cross_spread=1% (绿色, <2%)
- edge: 0 bps (paper model 未校准, 前端有 ADVISORY 标识)
- 延迟: 各市场显示 8000s+ 延迟 (book_as_of_ts 实时刷新但 ingestion_ts 固定 → 误报)

**操作员会立刻感到困惑的地方:**
1. 每行赛事标题重复: "Will the OKC Thunder win the 2026 NBA Finals? — Will the OKC Thunder win..." 全文重复显示两次
2. 延迟数字显示 2.x 小时, 但 wss_state 显示 CONNECTED — 矛盾
3. sport 字段全部为空字符串, Ops 页"运动类型"Chip 不显示
4. 无持仓提示"等待 paper runtime"— 新操作员不知道 paper 是否在运行

---

## 3. 发现问题清单

### B1 [P0] Risk Rejects 100% 重复 — 每条记录都有精确副本

**现象:** `/api/v1/risk/rejects` 返回 256 条, 其中 128 条是精确副本 (same reason_code + market_id + intent_ref + rejected_ts, 完全相同)。

**验证:**
```
total rejects: 256
duplicate rejects: 128  (exact duplicates, all 4 fields match)
reason codes: {'INVALID_INTENT'}
affected markets: 2
```

**运营影响:** Ops 页拒单日志显示数量翻倍, 运营误判系统"拒单率异常高"。计算 reject_rate 被高估 2 倍。

**严重度:** P0

**建议报到:** 小宋 (测试验证) → 小卢/paper_loop owner 查双 publish 路径

---

### B2 [P0] R-20 四时间戳契约违反 — 5/10 市场 data_source_ts > ingestion_ts

**现象:** R-20 要求 `event_ts <= data_source_ts <= ingestion_ts <= as_of_ts`。实测 10 个市场中有 5 个违反 `data_source_ts <= ingestion_ts`:

| 市场 (前20字符) | 违反方向 | diff (ns) |
|---|---|---|
| 0xf7b5491e70b477d451... | data_source_ts > ingestion_ts | +10,551,000 |
| 0x52847ca1413b76a557... | 同上 | +12,492,000 |
| 0xb6b3d7a2037b3faa7e... | 同上 | +22,965,000 |
| 0x7976b8dbacf9077eb1... | 同上 | +21,121,000 |
| 0x9b6fef249040fd17e9... | 同上 | +29,035,000 |

**原因分析:** `ingestion_ts` 是本地 `steady_clock::now()` 在解析帧时刻打的戳; `data_source_ts` 是 CLOB WSS 消息中的 Polymarket 服务器时间戳(ms*1e6)。当本地时钟与 Polymarket 服务器有 10-30ms 偏差时, 跨洋链路会出现此问题。

**运营影响:** staleness 计算的参考时间链断裂。Ops 页"4 时间戳 pipeline"面板会显示警告, 运营误判数据流断掉。这是 CLAUDE.md §8 红线级别违反。

**严重度:** P0 (红线明文: 4 时间戳契约 → P0)

**建议报到:** 老韩 (风控合规部主管) + 小卢 (数据时间戳处理 owner)

---

### B3 [P1] score 端点 100% miss — 所有市场得分永远 found=false

**现象:** 所有 10 个 event 调用 `/api/v1/score/{event_id}` 均返回 `found=false`。

**根因:** 系统当前处于"平铺 fallback 模式"(DiscoverSportsMarketsFlat), 此时 event_id = condition_id (Polymarket 合约哈希)。Goalserve InplayFeedThread 的 ScoreSnapshotStore 以 Goalserve 内部赛事 ID 为键, 两者 ID 体系完全不匹配, 永远对不上。

**附加原因:** 当前 10 个市场全部是 outright/futures 类型 (哪支球队赢得总冠军), 不是 inplay 赛事。Goalserve inplay feed 不覆盖 futures 市场, 即使 ID 对上也不会有比分。

**运营影响:**
- 盯盘页 EventAccordion 赛事头无比分显示, 无 LIVE 状态标识
- "进行中"过滤器永远为空, 操作员无法快速筛选进行中赛事
- Ops 页 staleness 热力表无法区分"真没比分" vs "数据没接上"

**严重度:** P1

**建议报到:** 小余 (数据基础设施部主管) — Goalserve ID 映射方案

---

### B4 [P1] /metrics 多项关键指标恒为 0 — 运营无法用 Prometheus 监控系统

**现象:** 以下指标在实际有数据的情况下仍为 0:
- `stcpp_uptime_seconds` = 0 (实际 uptime 16200s)
- `stcpp_rm_reject_total` = 0 (实际 /api/v1/risk/rejects 有 256 条)
- `stcpp_fill_total` = 0 (paper loop 在运行, 有 ticks)
- `stcpp_data_staleness_ms_max` = 0 (实际 book data 已 2+ 小时不更新)
- `stcpp_wss_connected{channel="clob"}` = 0 (但 book endpoint 返回 wss_state=CONNECTED)

**根因:** `RealStateProvider::metrics()` 仅填充了订阅计数字段 (subscribed_tokens/markets), 其余字段保持默认值 0。`uptime_sec` 字段未从 `HttpServer::start_time_` 计算。

**运营影响:**
- Ops 页"业务吞吐"卡片 RM 决策/拒单/成交全部显示 "—"
- Ops 页"数据质量"staleness 进度条恒为 0 (无法发现数据停更告警)
- Prometheus scrape 接入后图表全部平线, 无告警触发

**严重度:** P1

**建议报到:** 小卢 (metrics 接入 owner) + 老周 (系统工程部主管, 架构接线)

---

### B5 [P1] /status 的 wss_connected 与 book 端点 wss_state 自相矛盾

**现象:**
- `/status` → `wss_connected.clob: false`
- `/api/v1/book/{cond_id}` → `token0.wss_state: "CONNECTED"`

两个端点来自同一 provider, 却报出相反状态。

**根因:** book 端点中的 `wss_state` 是 OrderBookSnapshot 内存状态 (最后一次 WSS 推送时记录的状态字符串, 上次连接时写入后未刷新)。`/status` 读的是 `MetricsSnapshot.wss_clob_connected` (当前布尔值, 正确反映当前已断开)。

**运营影响:** 操作员看 Ops 页说"WSS 断了"(红灯), 再展开具体市场看 book 又显示"CONNECTED"(绿灯), 完全不知道相信哪个。新操作员必然提 incident 说"系统状态矛盾, 是坏了吗?"

**严重度:** P1

**建议报到:** 小卢 (endpoint_status / real_state_provider owner)

---

### B6 [P1] /api/v1/market/{cond_id} 永远 404 — 市场详情页无法使用

**现象:** 所有 10 个市场调用 `/api/v1/market/{cond_id}` 均返回 `{"found": false}`, HTTP 404。

**根因:** `RealStateProvider::market()` 硬编码返回 `found=false` (代码注释明确: "gamma /events discovery 仅启动时调用一次, 不持久维护 MarketInfo catalog")。

**运营影响:**
- 前端"市场详情"Tab 永远无内容 (tick_size / fee_rate / accepting_orders / polymarket_url 全无)
- 操作员无法获取盘口手续费、是否接受订单等关键决策信息
- 盯盘页"量化/AI"面板中模型 provenance (slug/polymarket_url) 无法展示

**严重度:** P1

**建议报到:** 老李 (MarketInfo catalog 接入 owner) + 老周 (主管)

---

### B7 [P2] sport 字段全部为空 — 盯盘页无运动类型标签, 过滤器部分失效

**现象:** `/api/v1/events` 返回的所有 10 个事件的 `sport` 字段均为空字符串 `""`。

**根因:** gamma /events 响应中 `sport` 字段为空 (Polymarket 对 outright/futures 未填 sport tag); `DiscoverSportsEvents` 回退到 `tag` 字段也为空。

**运营影响:**
- 盯盘页每个事件头的"运动类型"Chip 不显示 (前端 `Show when={sportZh()}` 条件不满足)
- Ops 页 WSS 热力图中无法按运动类型区分
- 未来"进行中"过滤器 (依赖 score.status=inplay) 完全无效

**严重度:** P2

**建议报到:** 小余 (数据部主管) — 考虑从标题关键词反推 sport 标签

---

### B8 [P2] 盯盘页赛事标题重复显示 — 用户体验很差

**现象 (截图可见):** 每行赛事显示: `"Will the OKC Thunder win the 2026 NBA Finals? — Will the Oklahoma City Thunder win the 2026 NBA Finals?"`

**根因:** 前端 EventAccordion 中 `homeTeam()` 和 `awayTeam()` 在 score 数据为 null 时 fallback 到 `grp().eventTitle?.split(' vs ')[0]` 和 `split(' vs ')[1]`。由于这些 outright/futures 标题不含 " vs ", split 结果是 `[全标题, undefined]`。`homeTeam() = 全标题`, `awayTeam() = "—"`, 而赛事头又显示 `eventTitle`, 导致标题重复。

**运营影响:** 界面难以阅读, 每行宽度被撑满, 视觉噪音严重。

**严重度:** P2

**建议报到:** 小尤 (UX) + 小苏 (前端 owner)

---

### B9 [P2] book data 实际已 2+ 小时未更新但系统无告警

**现象:** 所有市场 `ingestion_ts` 距 `as_of_ts` 约 7600-8440 秒 (2.1-2.3 小时), 但:
- Ops 页 staleness 进度条显示 0 (因 metrics 未接, 见 B4)
- 盯盘页延迟 Chip 的 `stalenessMs()` 用的是 `book_as_of_ts` (响应时实时生成), 不是 `ingestion_ts`
- 前端没有任何"数据陈旧"告警弹出

**运营影响:** WSS 可能已实际断开 (status.wss_connected.clob=false 一致), 但操作员看不到显著告警, 可能基于 2+ 小时前的旧价格做决策。

**严重度:** P2

**建议报到:** 小尤 (staleness 展示逻辑) + 小宋 (告警触发自动化测试)

---

### B10 [P2] positions 空时无解释性提示 — 新操作员以为系统坏了

**现象:** 盯盘页持仓面板显示"无持仓 — 等待 paper runtime"。positions API 返回空数组。Gate 页 has_data=false。

**实际:** paper loop 已启动, 但 paper 成交量 0 (因为 risk rejects 全是 INVALID_INTENT, 无成交)。

**运营影响:** 操作员不知道:
1. paper loop 是否在运行 (无运行中状态显示)
2. 为什么没有成交 (无成交原因展示)
3. INVALID_INTENT 是什么意思 (拒单理由仅在 Ops 页有中文翻译)

**严重度:** P2

**建议报到:** 老胡 (产品业务保障部主管, 补充运营提示文案)

---

## 4. 盯盘核心问题汇总 (运营视角)

| # | 问题 | 严重度 | 谁报 |
|---|---|---|---|
| B1 | 拒单日志100%重复, 数量虚报 | P0 | 小宋/小卢 |
| B2 | R-20 四时间戳契约5/10违反 | P0 | 老韩/小卢 |
| B3 | 比分数据永远 miss | P1 | 小余 |
| B4 | Prometheus metrics 多项恒为0 | P1 | 小卢/老周 |
| B5 | WSS状态两端点自相矛盾 | P1 | 小卢 |
| B6 | 市场详情页永远404 | P1 | 老李/老周 |
| B7 | sport字段全空, 类型标签不显示 | P2 | 小余 |
| B8 | 赛事标题重复显示 | P2 | 小尤/小苏 |
| B9 | book数据2+小时陈旧无告警 | P2 | 小尤/小宋 |
| B10 | 空持仓无解释, 新人困惑 | P2 | 老胡 |

---

## 5. 给老胡 / GM 的一句话结论

**当前看板不能放心用来盯盘。**

两个 P0 级问题: 拒单日志数量翻倍 (运营误判风控状况) + R-20 时间戳违反 (数据质量保证体系根基断裂)。四个 P1 问题使 Ops 监控面板实际失效 (metrics 全零、市场详情缺失、WSS 状态矛盾)。book 数据实际已 2+ 小时未更新但系统无有效告警。P0/P1 修复前, 不建议操作员用此看板做任何决策参考。
