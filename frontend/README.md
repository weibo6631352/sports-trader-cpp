# sports-trader-cpp 操盘终端 v8 — SolidJS + SUID/Material Design

**owner:** 小苏 (E 产品业务保障部)
**last_review:** 2026-05-29
**关联:** ADR-038 (观测 API), ADR-040 (book_pair 端点)

---

## v8 核心变化

- **市场发现:** `GET /api/v1/events` (真实 10 个 NHL/NBA/FIFA 事件), 不再依赖 positions
- **折叠/展开:** Event 分组 Accordion + Market 摘要行折叠态 (~40px) + 点击展开三列详情
- **去 DEMO 改 LIVE:** DEMO 横幅已移除; StatusBar 改为 LIVE 实时标识 + 连接/stale 三态

---

## 技术栈 (v8)

- **框架:** SolidJS 1.8 + TypeScript 5.4
- **组件库:** @suid/material v0.19.0 (Material Design for SolidJS)
- **构建:** Vite 5.2
- **状态:** SolidJS createStore + sessionStorage 展开状态持久化

---

## 快速开始

```bash
cd frontend
npm install

# 开发 (连后端 8080 live 模式)
npm run dev          # → http://127.0.0.1:3000

# stub 模式 (后端未启动时)
open "http://127.0.0.1:3000/?stub=1"

# 生产构建 (tsc + vite)
npm run build        # 产物 dist/  (tsc 0 error)

# 后端 (无 flag = live)
stcpp_debug_server --port 8080
```

---

## 面板说明 (v8)

| 区域 | 内容 | API Endpoint | 轮询 |
|------|------|-------------|------|
| StatusBar (常驻) | mode/LIVE/状态/净PnL/运行时间/WSS/Gate/拒单/60s | /healthz + /status | 5s |
| PnL sparkline | 净值曲线 (手写 SVG) | /api/v1/pnl/timeseries | 15s |
| 盯盘页 (v8 Accordion) | Event 折叠分组 + Market 摘要行 + 展开三列详情 | /api/v1/events + /market + /book_pair + /score + /quote + /positions | 5s |
| Ops 观测页 | 系统健康 + WSS + staleness + rejects + metrics | /healthz + /status + /metrics | 5s/30s |
| PnL 分析页 | 净值曲线 + 归因瀑布 + 分市场 + Gate | /api/v1/pnl/* + /api/v1/gate/paper | 15s |
| 市场详情页 | condition 深钻: 全档订单簿 + quote + score + rejects | /api/v1/market + /book_pair + /score + /quote + /risk/rejects | 按需 |

---

## 轮询分层

| 数据 | 间隔 |
|------|------|
| status / healthz | 5s |
| events + positions + attribution + rejects | 5s |
| book / score / quote (per-condition) | 5s (与 events 合并) |
| sparkline (timeseries) | 15s |
| attribution + gate | 15s |
| metrics | 30s |
| market info (cache) | 60s |

---

## 文件结构

```
frontend/
├── index.html
├── package.json
├── vite.config.ts
├── tsconfig.json
├── README.md
├── INTEGRATION-VERIFY.md
├── dist/              # 产物 (gitignore)
├── node_modules/      # 依赖 (gitignore)
└── src/
    ├── index.tsx      # Solid 挂载入口
    ├── App.tsx        # 根组件 (v8 去 DEMO 横幅)
    ├── api.ts         # 类型化 API client (含 fetchEvents)
    ├── stub.ts        # 本地 mock 数据 (含 STUB_EVENTS)
    ├── store.ts       # createStore + 轮询 (v8: /events 发现市场)
    ├── i18n.ts        # 中文映射表
    ├── types.ts       # TS 类型 (含 EventSummary/EventsResponse)
    ├── style.css      # 深色量化终端样式 (v8 新增 Accordion/Collapse/LIVE 样式)
    └── components/
        ├── StatusBar.tsx       # 常驻状态条 (v8: LIVE badge + 无 DEMO 横幅)
        ├── TradingPage.tsx     # 盯盘页 (v8: Accordion + Collapse 折叠/展开)
        ├── OpsPage.tsx         # Ops 观测页 (保持 v7)
        ├── AnalyticsPage.tsx   # PnL 分析页 (保持 v7)
        ├── MarketDetailPage.tsx # 市场详情页 (保持 v7)
        └── ...
```

---

## 空数据处理

- `/api/v1/events` 返回 0 events → 显示 "加载赛事数据..."
- positions 空 (paper 未跑) → "无持仓 — 等待 paper runtime"
- book/quote null → 优雅降级, 不报错
- API 连续 3 次失败 → 顶部 "API 异常" 红色 Chip

---

## 4 时间戳字段

对齐 ADR-038 R-20: `event_ts / data_source_ts / ingestion_ts / as_of_ts` (epoch_ns).
