# INTEGRATION-VERIFY.md — v8 /events 真实市场发现 + 折叠/展开

owner: 小苏 (#12, E单元)
last_review: 2026-05-29
关联: ADR-038/040/041, GM 老板 v8 派单 (events 真实发现 + 折叠/展开 + 去 DEMO 改 LIVE)

---

## v8.0 主变更清单

| 变更 | 内容 |
|------|------|
| 市场发现 | `GET /api/v1/events` → 真实 10 个事件，不再靠 positions |
| 折叠/展开 | Event 分组: 原生 Accordion (CSS height 动画); Market 行: 摘要行 ~40px 折叠 + 展开区三列横排 |
| DEMO 横幅 | 彻底去除 (data_source 恒 live); 改为 StatusBar 内 LIVE 实时标识 + 连接/加载/stale 三态 |
| Stub 横幅 | 仅 `?stub=1` 时显示, 与 LIVE 模式无关 |
| 展开状态 | sessionStorage 持久化 per conditionId/eventId, 轮询刷新不重置 |
| 持仓空态 | "无持仓 — 等待 paper runtime" (不报错, live 模式 paper 未跑) |
| EventGroup 类型 | 增加 eventSlug / eventTitle / sport 字段 (来自 /api/v1/events) |
| ConditionData 类型 | 移除 isDemoData 字段 |

---

## 构建验证 (2026-05-29)

```
tsc --noEmit               → 0 errors (strict mode)        PASS
vite build                 → 306 modules transformed
                              dist/assets/*.css   25.87 kB  (gzip 4.87 kB)
                              dist/assets/*.js   256.17 kB  (gzip 71.79 kB)
                              built in 576ms               PASS
```

---

## 真实后端数据验证 (2026-05-29)

后端: `stcpp_debug_server` live 模式已运行 (port 8080)

| endpoint | 状态 | 样本 |
|----------|------|------|
| `GET /api/v1/events` | 返回 10 个真实事件 (NHL/NBA/FIFA) | Carolina Hurricanes NHL, OKC Thunder NBA, Spain FIFA... |
| `GET /api/v1/market/{conditionId}` | found=true, accepting_orders=true | tick_size=0.01, fee_rate=0.02 |
| `GET /api/v1/book_pair/{conditionId}` | 真实双边 book, token0/token1 均有 bids/asks | best_bid=0.56/best_ask=0.57 |
| `GET /status` | data_source="real", RUNNING | wss: sports_api✓ clob✓ |
| `GET /metrics` | subscribed_markets_total=10, subscribed_tokens_total=20 | 与 events 数对齐 |

---

## 验收检查清单

### 盯盘页 v8 (Trading)

- [x] /api/v1/events 发现市场 (真实 10 个 NHL/NBA/FIFA)
- [x] Event Accordion 分组 (▶/▼ 折叠/展开整组)
- [x] 进行中赛事分组默认展开 (isLive 判断)
- [x] 预赛/结束/无 score 赛事默认折叠
- [x] Market 摘要行 ~40px (盘口 Chip / 名称 / 最优买卖价 / edge / 持仓 / 浮盈 / 拒单 / 延迟)
- [x] 点击摘要行展开三列详情区 (订单簿 / 量化AI / 持仓拒单)
- [x] 多 Market 行可同时展开
- [x] 同 Event 下多 Market 同时展开
- [x] 展开状态 sessionStorage 持久化 (刷新不丢)
- [x] 轮询 5s 不重置展开状态
- [x] 全展开/全折叠快捷按钮
- [x] 延迟三色: <100ms 绿 / 100ms-1s 黄 / >1s 红
- [x] edge bps 正绿负红
- [x] 浮盈正绿负红, 括号负数格式 ($xx.x)
- [x] 拒单 Badge ×N (N>0 红色, N=0 灰)
- [x] ADVISORY 角标 (XD-3, AI 区强制显示)
- [x] 持仓空态显示 "无持仓 — 等待 paper runtime"
- [x] 订单簿 5 档 + 深度条 bid 绿/ask 红
- [x] vig Chip (cross_spread)
- [x] XD-1/4/5 红线保留

### 去 DEMO / 改 LIVE

- [x] 无 DEMO Alert 横幅 (data_source="real" → 不显示)
- [x] StatusBar 内 "实时 LIVE" 绿色闪烁标识
- [x] 后端未连接 → "后端离线" 红色标识
- [x] 连接中 → "连接中..." 黄色标识
- [x] Stub 横幅仅 ?stub=1 时显示

### Ops 观测页 (保持 v7, 已验证)

- [x] subscribed_markets_total=10 (真实)
- [x] subscribed_tokens_total=20 (真实)
- [x] wss_connected: sports_api✓ clob✓ user_channel✗ (真实)

### PnL / 市场详情 (保持 v7)

- [x] PnL 空态: 等待 paper runtime
- [x] 市场详情: condition_id 搜索正常

### 导航

- [x] StatusBar AppBar 常驻 (mode LIVE chip + LIVE badge + 运行时间 + WSS + Gate + 拒单/60s)
- [x] Tab 导航 Material 风格 (下划线 active)
- [x] URL hash 同步
- [x] Store 轮询跨 tab 切换不重置

---

## 启动命令

```bash
cd frontend/
npm install

# 开发 (HMR, 连后端 8080 API — live 模式)
npm run dev     # → http://127.0.0.1:3000

# stub 模式 (无后端, mock 数据)
open "http://127.0.0.1:3000/?stub=1"

# 生产构建
npm run build   # 产物 dist/  (tsc 0 error)

# 后端 (独立进程, 无 flag = live)
stcpp_debug_server --port 8080
```

---

## v8 数据流图

```
[后端 stcpp_debug_server :8080]
    |
    ├── GET /api/v1/events        → EventSummary[10] (event_id/slug/title/sport/condition_ids)
    |                              store: refreshMarketGrid → eventGroups
    |
    ├── GET /api/v1/market/{cid}  → Market (per condition, 60s 缓存)
    ├── GET /api/v1/book_pair/{cid} → BinaryMarketBookView (5s 轮询)
    ├── GET /api/v1/quote/{cid}   → Quote (5s 轮询, 可 null)
    ├── GET /api/v1/score/{eventId} → Score (5s 轮询, 可 null)
    |
    ├── GET /api/v1/positions     → Positions (5s, live 模式 paper 未跑 → [])
    ├── GET /api/v1/pnl/*         → PnlTimeseries/Attribution (15s)
    ├── GET /api/v1/risk/rejects  → RiskRejects (5s)
    └── GET /metrics              → Prometheus 裸文本 (30s)

[前端 Vite :3000]
    store.ts: initPolling() → 分层轮询 → setState → SolidJS 响应式
    TradingPage.tsx: EventAccordion → MarketSummaryRow + MarketExpandArea
    StatusBar.tsx: LIVE badge / 系统状态 / WSS / Gate
```

---

## v7.x 遗留验证记录 (继承)

- XD-1/3/4/5 AI provenance 红线: PASS
- 错误态: fail Chip / api-err Chip: PASS
- 轮询分层 5s/15s/30s/60s: PASS
- metrics 无条件 30s 轮询: PASS
- subscribed_markets/tokens 真实值: 10/20 PASS
