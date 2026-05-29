# INTEGRATION-VERIFY.md — v5.3 量化区 AI Provenance + Sizing 真值渲染

owner: 小苏 (#12, E单元)
last_review: 2026-05-29
关联ADR: ADR-038 §3 schema 铁律 + v5 方向 A (老板选定) + ADR-040 book_pair + SolidJS 迁移 (老板拍板)
关联任务: feat(frontend): 量化区渲染真实 sizing + AI provenance (model_id/confidence/advisory/calibrated, 小邓 XD 红线)
SSOT: `src/stcpp/debug_api/endpoint_*.cpp` + `include/stcpp/debug_api/state_provider.hpp`

---

## v5.3 量化区 AI Provenance 渲染 (2026-05-29)

### 后端 Quote 字段确认

```bash
curl http://127.0.0.1:8080/api/v1/quote/0x1234
# → 返回完整 AI provenance 字段 (小卢/小邓 已落 main)
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `fair_value` | number | 真实公允价 |
| `market_mid` | number | 市场中间价 |
| `edge_bps` | number | 边际 bps |
| `kelly_fraction` | number | Kelly 分数 (真算) |
| `suggested_notional` | number | 建议额度 (过 cap 链) |
| `signal_strength` | number | 信号强度 |
| `model_conf` | number | deprecated alias |
| `model_id` | string | 模型 ID |
| `model_kind` | string | 模型种类 |
| `spec_version` | string | 特征规格版本 |
| `model_confidence` | number | 模型置信度 [0,1] |
| `model_calibrated` | bool | 是否已校准 |
| `fair_ci_lower` | number | CI 下界 |
| `fair_ci_upper` | number | CI 上界 |
| `predict_ok` | bool | 预测是否正常 |
| `advisory` | bool | 仅供参考模式 |
| `model_as_of_ts` | number | 模型快照 epoch_ns |

### 小邓 XD 红线落实

| 规则 | 实现 | 状态 |
|------|------|------|
| XD-1: fair_value + model_confidence + model_id 三位一体 | Row1 公允价 + Row2 模型ID/置信度并排 | PASS |
| XD-3: advisory=true → "仅供参考/不下单" 角标 | `.advisory-banner` amber 横幅 + advisory-icon | PASS |
| XD-4: model_calibrated=false → 视觉降级 + "未校准" | `.q-uncalibrated` 灰化 + `.uncalib-chip` | PASS |
| XD-5: predict_ok=false → 不画 edge/kelly/notional | `<Show when={predictOk()}>` 门控, fallback `.predict-fail-chip` | PASS |

### CI 区间

- `fair_ci_lower` / `fair_ci_upper` 在 Row2 (AI provenance 行) 以小字 `[lower–upper]` 显示
- title tooltip 显示完整 4 位精度
- 字段缺失时自动隐藏 (`<Show when={hasCi()}>`)

### Stub 演示场景 (stub.ts)

| 盘口 | 演示 XD |
|------|---------|
| `nba-lal-bos-ml` | advisory=true → amber 角标 |
| `nba-lal-bos-total` | model_calibrated=false → 灰化 + 未校准chip |
| `nba-lal-bos-spread` | predict_ok=false → 预测异常, 不画 edge/kelly |
| `epl-ars-che-total` | lgbm 模型 + 窄 CI |
| `nfl-kc-buf-spread` | 正常 stub |
| `mlb-nyy-bos-ml` | 正常 stub |

### 构建验证 (2026-05-29)

```
tsc --noEmit → 0 errors  PASS
vite build   → 16 modules transformed, 55.57 kB  PASS
```

---

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

## 启动命令 (标准 Vite 单一工作流)

```bash
# 开发 (HMR, 连后端 8080 API, CORS)
cd frontend/
npm install      # 首次
npm run dev      # dev server http://127.0.0.1:3000

# Stub 模式
open "http://127.0.0.1:3000/?stub=1"

# 看产物
npm run build    # 产物 dist/
npm run preview  # 标准 Vite 预览 dist/

# 后端单独跑 (只管 API, 不托管前端)
stcpp_debug_server --real --port 8080
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

## 小尤 v5.2 UX 验收 P0 修复 (2026-05-29)

关联报告: docs/RESEARCH/xiaoyou-solidjs-v52-ux-review.md

### P0-A: 占位符缺"下一步"指引 — 已修

| 组件 | 修改前 | 修改后 |
|------|--------|--------|
| GlobalBar stateText | "API OFFLINE" | "后端未连接" + title tooltip "后端未连接 · 请检查 8080 或点 ⚙ 改 API Base" |
| EventGrid fallback | "无市场数据 (positions 为空)" | "等待持仓建立 / 后端未接入 · 点 ⚙ 检查" |
| PnlSparkline fallback | "PnL 曲线加载中..." (永远显示) | 加载中 vs 失败两态: failCount≥3 时改为 "PnL 曲线拉取失败 — 点 ⚙ 确认 API Base, 或等待自动重试 (每 15s)" |

### P0-B: fetchErrorMap 错误未暴露 endpoint — 已修

- api.ts 新增 `failingEndpointsSummary(threshold)`: 遍历 fetchErrorMap, 返回所有 failCount≥threshold 的 path + failCount 文本
- api.ts 新增 `isEndpointFailingPrefix(prefix)`: 前缀匹配, 用于带 query string 的 endpoint
- GlobalBar "API 异常" chip 新增 `title={apiErrTooltip()}`, tooltip 格式: "失败端点:\n/path (连续失败 N 次)\n..."

### P1-B: .mini-col-lbl 字号 9px → 10px — 已修

style.css `.mini-col-lbl { font-size: 9px }` → `10px`, 对齐三档下限 (R3).

### 构建验证

```
tsc --noEmit → 0 errors  PASS
vite build   → 16 modules transformed, 52.87 kB  PASS
```

---

## 遗留事项 (下一 sprint)

- 浏览器截图验证: 留给小宫 #48 dogfood 轮次
- 单盘口赛事无 positions/rejects/attribution 时也应展示 (需后端 /api/v1/markets list endpoint)
- P1-A/P1-C/P1-D/P1-E: 本 sprint backlog (P1-B 已完成)
- P2-01/P2-02/P2-04/P2-05/P2-06: 下一 sprint backlog
