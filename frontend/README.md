# sports-trader-cpp 观测/PnL 看板 v5 — SolidJS + TypeScript + Vite

**owner:** 小苏 (E 产品业务保障部)
**last_review:** 2026-05-29
**关联:** ADR-038 (观测 API), ADR-037 (本地优先), ADR-040 (book_pair 端点)

---

## 技术栈 (v5.2 — SolidJS + TS + Vite)

- **框架:** SolidJS 1.8 (细粒度响应式, 无 VDOM, 运行时轻量)
- **类型:** TypeScript 5.4 (strict mode, 全量类型化 API client)
- **构建:** Vite 5.2 (dev server + production build)
- **样式:** 原生 CSS (延续 v5 量化终端深色风格, 无组件库)
- **无 Python:** serve.py 已废弃; 开发用 `vite dev`, 产物用 `vite build` 静态文件

---

## 本地启动

```bash
cd frontend/
npm install          # 首次安装依赖
npm run dev          # dev server: http://127.0.0.1:3000
```

**Stub 模式 (后端未启动时):**

```
http://127.0.0.1:3000/?stub=1
```

**连后端:**

```bash
# 后端默认 127.0.0.1:8080 (CORS 已开)
# 无需额外配置, 直接 npm run dev 即可
# 或在看板右上角点设置按钮修改 API Base URL
```

---

## 生产构建

```bash
npm run build        # 产物输出到 dist/
npm run preview      # 预览 dist/ (vite preview)
```

产物规模 (2026-05-29 实测): JS 约 52 kB / gzip 18 kB, CSS 约 16 kB / gzip 3 kB.

---

## 文件结构

```
frontend/
├── index.html              # Vite 入口 HTML
├── package.json            # npm 配置
├── vite.config.ts          # Vite 配置
├── tsconfig.json           # TypeScript 配置
├── README.md               # 本文件
├── INTEGRATION-VERIFY.md   # 字段契约对齐验证记录
├── dist/                   # 产物 (gitignore)
├── node_modules/           # 依赖 (gitignore)
└── src/
    ├── index.tsx           # Solid 挂载入口
    ├── App.tsx             # 根组件 + 轮询初始化
    ├── api.ts              # ADR-038 类型化 API client
    ├── stub.ts             # 本地 mock 数据
    ├── store.ts            # Solid createStore 应用状态 + 轮询逻辑
    ├── i18n.ts             # 中文映射表 (5 张)
    ├── types.ts            # 所有 TS 类型定义 (对齐后端 wire)
    ├── style.css           # 深色量化终端样式 (v5 延续)
    ├── components/
    │   ├── GlobalBar.tsx       # 顶部常驻条 + 设置面板
    │   ├── PnlSparkline.tsx    # PnL 净值曲线 (手写 SVG)
    │   ├── EventGrid.tsx       # 赛事分组网格 (v5 核心)
    │   └── SecondaryFooter.tsx # 折叠次要区 (PnL 归因 + metrics)
    └── legacy/             # 原生三件套归档 (v5.1 及之前, 不参与 build)
        ├── app.js
        ├── api.js
        ├── panels.js
        └── stub.js
```

---

## 面板说明

| 区域 | 内容 | API Endpoint | 轮询 |
|------|------|-------------|------|
| 顶部常驻条 | 模式/状态/净PnL/WSS/Gate/p99/延迟/拒单 | /healthz + /status | 5s |
| PnL sparkline | 净值曲线 (手写 SVG) | /api/v1/pnl/timeseries | 15s |
| 赛事分组网格 | 赛事头比分 + 多盘口并列 (双边簿/量化/持仓) | /api/v1/positions + /market + /book_pair + /score + /quote | 5s |
| 折叠区 | PnL 归因瀑布 + Prometheus 原始 metrics | /api/v1/pnl/attribution + /metrics | 15s/30s |

---

## 轮询分层

| 数据 | 间隔 |
|------|------|
| status / healthz | 5s |
| positions + attribution + rejects (market grid) | 5s |
| book / score / quote (per-condition) | 5s (与 market grid 合并) |
| sparkline (timeseries) | 15s |
| attribution (单独) | 15s |
| gate | 15s |
| metrics | 30s (折叠时跳过) |
| market info | 60s |

---

## 空数据处理

- API 返回 404 → 显示 "未接入"
- `found: false` / `has_data: false` → 显示 "未接入"
- 网络不可达 → 显示占位符, 不崩溃
- P0-03: 连续 3 次失败 → 顶部 "API 异常" 红色 chip

---

## DEMO fail-safe (P0-02)

`data_source !== 'live'` 时强制显示黄色横幅 + 每盘口 [demo] 角标.
不可关闭, 老钱红线.

---

## 4 时间戳字段

对齐 ADR-038 R-20: `event_ts / data_source_ts / ingestion_ts / as_of_ts` (epoch_ns).
JS Number 精度上限约 9e15, epoch_ns 约 1.7e18, 精度损失约 1024ns, 显示层可接受.
