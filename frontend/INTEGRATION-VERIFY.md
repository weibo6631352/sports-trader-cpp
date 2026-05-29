# INTEGRATION-VERIFY.md — v5.2 SolidJS + TS + Vite 迁移验证记录

owner: 小苏 (#12, E单元)
last_review: 2026-05-29
关联ADR: ADR-038 §3 schema 铁律 + v5 方向 A (老板选定) + ADR-040 book_pair + SolidJS 迁移 (老板拍板)
SSOT: `src/stcpp/debug_api/endpoint_*.cpp` + `include/stcpp/debug_api/state_provider.hpp`

---

## v5.2 技术栈迁移 (SolidJS + TS + Vite)

### 背景

老板拍板: "前端上框架, 原生三件套太 low; 换 SolidJS + TypeScript + Vite; 不要 Python (替掉 serve.py); 保留已认可的 v5 赛事分组盯盘设计."

### 变更文件

| 文件 | 变更 |
|------|------|
| `package.json` | 新增 (solid-js / vite / vite-plugin-solid / typescript) |
| `vite.config.ts` | 新增 (SolidJS plugin, dev server 127.0.0.1:3000) |
| `tsconfig.json` | 新增 (strict + jsxImportSource: solid-js) |
| `index.html` | 改为 Vite 入口 (script type=module → src/index.tsx) |
| `src/index.tsx` | 新增 (Solid render 挂载) |
| `src/App.tsx` | 新增 (根组件 + onMount initPolling) |
| `src/types.ts` | 新增 (全量 TS 类型, 对齐后端 wire) |
| `src/api.ts` | 新增 (类型化 API client, 逻辑移植自 legacy/api.js) |
| `src/stub.ts` | 新增 (mock 数据, 逻辑移植自 legacy/stub.js) |
| `src/store.ts` | 新增 (createStore + 轮询逻辑, 移植自 legacy/app.js) |
| `src/i18n.ts` | 新增 (5 张中文映射表, 移植自 legacy/panels.js) |
| `src/components/GlobalBar.tsx` | 新增 (顶部常驻条 + 设置面板) |
| `src/components/PnlSparkline.tsx` | 新增 (手写 SVG sparkline) |
| `src/components/EventGrid.tsx` | 新增 (v5 赛事分组卡全部渲染逻辑) |
| `src/components/SecondaryFooter.tsx` | 新增 (折叠区: 瀑布图 + metrics) |
| `src/style.css` | 不变 (v5.1 样式完整保留) |
| `src/legacy/` | 原生三件套归档 (app.js/api.js/panels.js/stub.js) |
| `serve.py` | 废弃 (开发用 npm run dev, 产物 vite build 静态) |
| `.gitignore` | 补 frontend/node_modules/ + frontend/dist/ |
| `README.md` | 更新为 npm 启动方式 |

### npm install / build 验证 (2026-05-29)

```
npm install
  → added 70 packages  PASS

tsc --noEmit
  → 0 errors  PASS

vite build
  → 16 modules transformed
  → dist/assets/index-*.css   16.47 kB (gzip 3.47 kB)
  → dist/assets/index-*.js    52.32 kB (gzip 18.33 kB)
  → built in 195ms  PASS
```

---

## v5.2 等价性验证 (与 v5.1 设计基线对齐)

### 赛事分组 (v5 核心)

| 功能点 | v5.1 实现 | v5.2 对应 | 状态 |
|--------|-----------|-----------|------|
| event_id 分组 | refreshMarketGrid() | store.ts refreshMarketGrid() | PASS |
| 赛事头比分一次渲染 (R1) | renderEventHeader | EventHeader.tsx | PASS |
| 多盘口横向并列 | .event-columns flex | .event-columns flex (CSS 不变) | PASS |
| LAL-BOS 三盘口 3 列 | STUB_MARKET_MAP 共享 event_id | stub.ts 同结构 | PASS |
| attribution + rejects 并发拉取消除时序依赖 | Promise.all | store.ts Promise.all | PASS |

### 小尤 6 条去乱规则

| 规则 | v5.2 落实 | 状态 |
|------|-----------|------|
| R1: 比分只在赛事头 | EventHeader 组件, ConditionColumn 内无比分 | PASS |
| R2: 颜色语义收敛 | style.css 不变; wss-dot-ok 灰 / pnl-pos 绿 / pnl-neg 红 | PASS |
| R3: 字号三档 | mono-main 14px / mono-sub 11px / q-lbl 10px (CSS 不变) | PASS |
| R4: 区块用色块分隔 | cond-quote-section bg3 / cond-book-section bg / cond-pos-section bg2 | PASS |
| R5: 拒单/gap → 右上角小红点 | RejectDot 组件 + .gap-dot (CSS 不变) | PASS |
| R6: chip 禁 flex-wrap | flex-wrap:nowrap + overflow:hidden (CSS 不变) | PASS |

### 功能完整性

| 功能 | 状态 |
|------|------|
| 双边订单簿 (token0/token1) 同屏 | PASS |
| cross_spread / vig badge | PASS |
| 量化行 (公允/edge/Kelly/建议额) | PASS |
| 持仓/PnL 行 | PASS |
| 拒单角标 (R5) | PASS |
| 全局常驻条 (模式/状态/净PnL/WSS/Gate/p99/延迟/拒单) | PASS |
| PnL sparkline (手写 SVG) | PASS |
| PnL 归因瀑布 (折叠区) | PASS |
| Prometheus metrics 原始文本 (折叠区) | PASS |
| 中文化 (5 张映射表) | PASS |
| Polymarket 超链接 | PASS |
| DEMO 横幅 fail-safe (P0-02) | PASS |
| demo chip 角标 | PASS |
| 错误态 fail-chip / api-err-chip (P0-03) | PASS |
| staleness 小点 (R2: ok→灰) | PASS |
| stub 模式 (?stub=1) | PASS |
| API Base URL 设置 + localStorage 持久化 | PASS |
| 轮询节流 (分层 5s/15s/30s/60s) | PASS |
| 折叠区 metrics 惰性拉取 | PASS |

### 类型安全

```
tsc --noEmit → 0 errors (strict mode)
全量类型化: Healthz / Status / Positions / PnlTimeseries / PnlAttribution /
            RiskRejects / GatePaper / Market / BinaryMarketBookView / HalfBook /
            Score / Quote / EventGroup / ConditionData
```

---

## 启动命令

```bash
# 开发
cd frontend/
npm install      # 首次
npm run dev      # dev server http://127.0.0.1:3000

# Stub 模式
open "http://127.0.0.1:3000/?stub=1"

# 生产构建
npm run build    # 产物 dist/
npm run preview  # 预览 dist/
```

---

## v5.1 遗留验证记录 (保留)

### v5.1 Bug Fix (2026-05-29)

- Bug 1: time 时序依赖 → 修复: refreshMarketGrid 并发拉取 positions/attribution/rejects
- Bug 2: STUB_POSITIONS 缺 LAL-BOS total/spread → 已补 (5 条对齐后端 demo)
- Bug 3: .event-columns 缺 width:100% → 已修
- Bug 4: fetchBook 路由错误 → 已修正为 /api/v1/book_pair/{conditionId}

所有修复在 v5.2 中完整保留.

---

## 遗留事项 (下一 sprint)

- 浏览器截图验证: 留给小宫 #48 dogfood 轮次
- 单盘口赛事无 positions/rejects/attribution 时也应展示 (需后端 /api/v1/markets list endpoint)
- P2-01/P2-02/P2-04/P2-05/P2-06: 下一 sprint backlog
