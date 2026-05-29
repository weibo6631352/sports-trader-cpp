# v3 单屏盯盘终端 — 全流程 Dogfood 报告

- **owner:** 小宫 (dogfood 测评员, E 部)
- **last_review:** 2026-05-29
- **测评版本:** v3 单屏盯盘终端
- **后端:** stcpp_debug_server DemoStateProvider @ 127.0.0.1:8080
- **前端:** serve.py @ 127.0.0.1:3000
- **测评方法:** curl 实测全部 endpoint + 代码走读 (app.js / panels.js / api.js / stub.js) + 截图对照

---

## 1. 环境确认

两端均在线:

- `127.0.0.1:8080/healthz` → `ok:true`, 5 线程全 alive
- `127.0.0.1:3000/` → HTTP 200

---

## 2. Endpoint 全量实测结果

| Endpoint | HTTP | 字段完整性 | 与前端消费逻辑对齐 | 备注 |
|---|---|---|---|---|
| `GET /healthz` | 200 | 完整 | 对齐 | uptime_sec, threads, ok |
| `GET /status` | 200 | 完整 | 对齐 | data_source="demo" 已存在 |
| `GET /api/v1/positions` | 200 | 完整 | 对齐 | 3条持仓 (nba-lal×2, epl×1) |
| `GET /api/v1/pnl/timeseries` | 200 | **有 bug** | **部分不对齐** | 见 P1-01 |
| `GET /api/v1/pnl/attribution` | 200 | 完整 | 对齐 | waterfall.net=307 |
| `GET /api/v1/risk/rejects` | 200 | 完整 | 对齐 | 3条拒单 |
| `GET /api/v1/gate/paper` | 200 | 完整 | 对齐 | has_data=true, prelim_pass=true |
| `GET /api/v1/market/{id}` | 200 | 完整 | **部分不对齐** | 见 P2-01 |
| `GET /api/v1/book/{id}` | 200 | 完整 | 对齐 | 4时间戳齐全 |
| `GET /api/v1/score/{event_id}` | 200 | 完整 | 对齐 | soccer/basketball 均正常 |
| `GET /api/v1/quote/{id}` | 200 | 完整 | 对齐 | fair_value/edge_bps/kelly 正常 |
| `GET /metrics` | 200 | 完整 | 对齐 | Prometheus text 格式正常 |

---

## 3. 问题清单

### P1-01: timeseries 查询参数解析 bug — 单位后缀丢失

**现象:**

```
GET /api/v1/pnl/timeseries?window=1h&bucket=1m
→ 返回: window_sec=1, bucket_sec=1  (应为 3600, 60)

GET /api/v1/pnl/timeseries?window=6h&bucket=5m
→ 返回: window_sec=6, bucket_sec=5  (应为 21600, 300)
```

服务端只取了参数的数字部分, 忽略了单位后缀 `h`/`m`。

**严重度:** P1

**影响范围:** sparkline 显示的 as_of_ts 时间戳标注错误 (标 window_sec=1 实为1秒窗口); 如果后续基于 window_sec/bucket_sec 做数据采样或告警 SLA, 会彻底算错。目前前端 `renderPnlSparkline` 只用了 `buckets` 数组和 `as_of_ts`, 未读 `window_sec`/`bucket_sec` 字段, 所以**视觉上无感知**, 但数据标注是错的。

**复现步骤:**
```bash
curl "http://127.0.0.1:8080/api/v1/pnl/timeseries?window=1h&bucket=1m" | python3 -c "import sys,json;d=json.load(sys.stdin);print(d['window_sec'],d['bucket_sec'])"
# 输出: 1 1  (期望: 3600 60)
```

**建议:** DemoStateProvider (和生产 handler) 的 `window`/`bucket` 参数解析函数需补上单位换算: `h`×3600, `m`×60。报给小卢/小宋。

---

### P1-02: /metrics 与 /status 的 WSS 状态矛盾

**现象:**

```
GET /metrics
stcpp_wss_connected{channel="sports_api"} 1   ← 显示已连
stcpp_wss_connected{channel="clob"} 1          ← 显示已连

GET /status
wss_connected.sports_api = false               ← 显示断连
wss_connected.clob = false                     ← 显示断连
```

**严重度:** P1

**影响:** 顶部条 WSS 小圆点 (来自 `/status`) 和 Prometheus 面板 (来自 `/metrics`) 呈现完全相反的状态, 操作员无法判断 WSS 实际是否连通。如果在真实盯盘场景下, 一个说绿灯、另一个说红灯, 属于观测系统自相矛盾, 是操作风险。

**复现步骤:**
```bash
curl -s http://127.0.0.1:8080/metrics | grep wss_connected
curl -s http://127.0.0.1:8080/status | python3 -c "import sys,json;d=json.load(sys.stdin);print(d['wss_connected'])"
```

**建议:** DemoStateProvider 内两处 WSS 状态应由同一变量驱动, 保证一致。报给小卢/老周。

---

### P2-01: DemoStateProvider 对任意 market_id 返回 found:true + 伪数据

**现象:**

```bash
curl http://127.0.0.1:8080/api/v1/market/nonexistent-market-xyz
→ HTTP 200, found:true, outcome:"YES", event_id:"nonexistent-market-xyz-2026-05-29"

curl http://127.0.0.1:8080/api/v1/score/nonexistent-event-id-xyz
→ HTTP 200, found:true, home:"HOME", away:"AWAY"

curl http://127.0.0.1:8080/api/v1/book/nonexistent-market
→ HTTP 200, found:true, best_bid:0.644, best_ask:0.656
```

任意 ID 均返回合法 JSON + `found:true`。无 404 路径, 无 `found:false`。

**严重度:** P2 (demo 模式下可接受, 但前端边界逻辑无法被测试)

**影响:**

1. 前端 `apiFetch` 中的 `if (data.found === false) return null` 分支**在整个 demo 模式下永远不会触发**, 导致"score 未接入"/"book 未接入"/"量化未接入"这三个占位 UI 状态在 demo 模式下完全无法被观测验证。
2. attribution 里有 `nfl-kc-buf-spread` 和 `mlb-nyy-bos-ml` 两个无真实持仓的市场, 前端也会拉取这两个市场的 market/book/score/quote, 全部返回合法数据并渲染为"有数据"卡片 — 4 张卡片数据高度相似, 可辨识度差。
3. 若将来 DemoStateProvider 用于新人 onboarding 演示, 不存在的 market 也显示为"已连"状态, 可能误导操作员对系统能力的判断。

**复现步骤:**
```bash
curl "http://127.0.0.1:8080/api/v1/score/this-market-does-not-exist" | python3 -c "import sys,json;d=json.load(sys.stdin);print('found:', d.get('found'))"
# 期望: found:false 或 HTTP 404
# 实测: found:true, home:"HOME", away:"AWAY"
```

**建议:** DemoStateProvider 维护一个已知 market_id 白名单 (nba-lal-bos-ml / epl-ars-che-total / nfl-kc-buf-spread), 白名单外返回 `found:false` 或 HTTP 404。这样降级路径才可在 demo 模式下被真实演练。报给小卢。

---

### P2-02: stub.js 中 market.outcome 与真后端不一致

**现象:**

```
stub.js STUB_MARKET.outcome = "LAL"   (具体队名)
后端 GET /api/v1/market/nba-lal-bos-ml → outcome = "YES"  (二元市场通用标识)
```

`?stub=1` 模式下盘口显示 "LAL", 真后端模式下显示 "YES"。

**严重度:** P2

**影响:** stub 模式与真后端视觉不一致, 若用 stub 模式做 UX 评审, 演示效果与真实不符。面板 `renderQuoteBlock` 第287行: `outcome = market ? (market.outcome || market.market_id || '—') : '—'` — 真后端显示 "YES" 不如 "LAL" 直观。

**建议两个方向:**

1. (推荐) 后端 market 响应补充 `outcome_label` 字段 (如 "LAL Win"), 与二元 outcome 字段分开; 前端优先显示 `outcome_label`。
2. (备用) stub.js 改 outcome 为 "YES" 与后端对齐, 降低演示歧义。

报给小苏 (前端) + 小卢 (后端)。

---

### P2-03: sparkline bucket 数量仅 12 (demo 模式), 时间跨度标注语义歧义

**现象:**

`?window=1h&bucket=1m` 请求下, 后端返回 12 个 bucket (因 P1-01 的解析 bug 实际是 12秒/1秒)。正常应返回 60 个 bucket 覆盖 1 小时。

**严重度:** P2 (配合 P1-01 修复后自动解决)

**影响:** sparkline 在 demo 模式下折线点密度不足, x 轴没有时间标注, 操作员无法判断曲线覆盖的时间范围。`spark-header` 只显示 `as_of_ts` 没有 `from_ts`, 无法看出"这是过去1小时"还是"过去12秒"。

**建议:** panels.js `renderPnlSparkline` 补充 from_ts 到 as_of_ts 的范围标注。

---

## 4. 正向通过项 (跑通无问题)

- **DEMO 横幅 (老钱红线 P0): 通过。** `data_source=demo` → `#demo-banner` 移除 `.hidden`, 强制可见, 不可关闭。代码路径: `app.js` 第118-122行 + `renderTopBar` 返回 `isDemo:true`。
- **量化数字 demo 标记: 通过。** `renderQuoteBlock` 调用 `demoBadge(isDemoData)` → `<span class="demo-chip">demo</span>` 附在 fair/edge/Kelly 旁。`isDemoData` 来自 `cache.status.data_source === 'demo'`。
- **降级路径 (后端断连): 通过。** `safeGet` catch 分支返回 null; `renderScoreBlock(null)` → "比分未接入"; `renderQuoteBlock(null)` → "量化未接入"; `renderBookBlock(null)` → "未接入"; `renderPosBlock([], null)` → "无持仓"。白屏风险: 无。
- **?stub=1 模式: 通过 (数据结构层面)。** stub.js 所有字段与真后端 wire 格式对齐 (4时间戳/金额 Number/found 字段等)。主要差异仅 P2-02 的 outcome 值。
- **STUB 横幅: 通过。** `?stub=1` 时 `#stub-banner` 显示。
- **顶部条各字段: 通过。** mode-badge / state / uptime / WSS 点 / 净PnL / gate / p99 / staleness / 拒单/60s 全部正确渲染。
- **market 卡片网格组装逻辑: 通过。** positions → posMap; attribution.per_market → pmPnlMap; rejects → rejectMap; 并集取 allMarketIds; 并发拉 market/book/score/quote; renderMarketGrid 调 renderMarketCard 逐卡渲染。
- **score 用 event_id 反查: 通过。** `app.js` 第220行: `if (market && market.event_id) { score = await safeGet(() => fetchScore(market.event_id), STUB_SCORE); }` — 逻辑正确, 实测 NBA/EPL score 均正常返回。
- **book staleness stale-chip: 通过。** `renderStaleBlock` 读 `book_as_of_ts` → `stalenessMs()` → 颜色分级 (2s绿/10s黄/10s+红)。
- **拒单角标: 通过。** rejectRows 有数据 → `reject-badge` 显示 "拒单×N reason_code"。
- **market 无持仓占位: 通过。** posRows 为空 → "无持仓" 占位, 卡片正常渲染不崩。
- **settings 面板 API Base 切换: 通过 (代码路径)。** localStorage → setBaseUrl → reload。
- **metrics 折叠暂停: 通过。** `refreshMetrics` 第292行检测 `details.open`, 折叠时 early return。
- **PnL 归因瀑布图: 通过。** WF_ORDER gross/fee/gas/slippage/spread/net 全部渲染, 分市场 per_market 列表正常。
- **4时间戳契约: 通过。** book_as_of_ts: event_ts ≤ data_source_ts ≤ ingestion_ts ≤ book_as_of_ts 顺序正确; score 同理。

---

## 5. 截图与实测交叉核对

| 截图元素 | 实测对应 | 一致性 |
|---|---|---|
| "演示数据" 横幅 | data_source=demo → 触发 | 一致 |
| PAPER RUNNING 徽章 | mode=paper, state=RUNNING | 一致 |
| NET PnL CURVE +$43.16 | timeseries 最后 bucket cum_net_pnl (demo 随机游走) | 一致 (快照时刻不同) |
| 顶部净PnL +$387 | attribution.waterfall.net (当时快照, 现为307) | 一致 (demo 随机值) |
| 4 张卡片 (soccer/baseball/basketball/football) | positions (2市场) + attribution (4市场) → allMarketIds 并集 | 一致 |
| DEMO chip 在每张卡片 | isDemoData=true → demoBadge | 一致 |
| ADVISORY chip | advisoryBadge() 固定显示 (paper期旁路标记) | 一致 |
| 拒单角标 (KELLY_FRACTION_CAP 等) | rejects 数组挂到各卡片 | 一致 |
| 订单簿 CONNECTED 状态 | book.wss_state="CONNECTED" | 一致 |

---

## 6. Dogfood 结论

**单句结论: 暂不建议以当前 demo server 状态给老板独立盯盘 — P1-02 (WSS 状态矛盾) 会让操作员在出现真实网络抖动时无法信任观测数据; P1-01 (timeseries 单位解析错误) 是后端逻辑 bug 需修复; 其余核心流程 (DEMO 横幅红线、降级路径、卡片渲染) 全部跑通, 前端代码质量较高。**

| 维度 | 评分 (1-10) | 说明 |
|---|---|---|
| 启动/加载体验 | 8 | 两端均正常, 无需额外操作 |
| DEMO 红线合规 | 10 | 横幅强制 + demo-chip 覆盖全面 |
| 降级/容错 | 9 | safeGet + 占位全路径通过; 唯一扣分: demo 下降级路径无法真实演练 (P2-01) |
| 数据完整性 | 6 | P1-01 timeseries 单位 bug / P1-02 WSS 矛盾严重拉低 |
| stub vs 真后端一致性 | 7 | 结构对齐良好, outcome 值偏差 P2-02 |
| 可辨识度/操作员自助 | 6 | 4 卡片数据高度雷同 (P2-01 后果), 操作员难以区分各市场真实状态 |

**P0 问题: 无**

**P1 问题需修复后再放老板盯盘:**
- P1-01: timeseries 查询参数单位解析 bug (报小卢/小宋)
- P1-02: /metrics vs /status WSS 状态矛盾 (报小卢/老周)

**P2 问题可排期:**
- P2-01: DemoStateProvider 通配符返回 found:true (报小卢, 影响降级路径可测性)
- P2-02: stub outcome 值与真后端不一致 (报小苏+小卢)
- P2-03: sparkline 无时间范围标注 (报小苏)
