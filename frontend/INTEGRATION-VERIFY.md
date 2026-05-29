# INTEGRATION-VERIFY.md — v6 solid-ui + Tailwind + 多页

owner: 小苏 (#12, E单元)
last_review: 2026-05-29
关联: ADR-038/040/041, 小尤 v6 设计方案, 小郑观测页规格

---

## v6.0 主变更清单

| 变更 | 内容 |
|------|------|
| 组件库 | `@kobalte/core` + Tailwind v4 (`@tailwindcss/vite`) copy-paste 模式 |
| 多页导航 | 顶部 TabNav (盯盘 / Ops观测 / PnL分析 / 市场详情 占位) |
| 常驻 StatusBar | 替换 GlobalBar, 跨页不变 |
| 盯盘增强 | 5档订单簿 + DepthBar + ConfBar + CI主显示行 + 筛选栏 + per-event staleness |
| Ops观测页 | 按小郑规范: 系统健康/订阅状态/WSS/数据质量/业务吞吐/拒单/裸文本折叠 |
| PnL分析页 | 升格 v5 SecondaryFooter: 净值曲线大图 + 瀑布图 + Gate仪表 |
| URL hash 路由 | `#trading / #ops / #analytics / #market` 刷新后还原 |
| 公共组件 8 个 | StatusDot / Badge / StatCard / DepthBar / ConfBar / TabNav / StalenessHeatCell / PipelineHealth |

---

## 构建验证 (2026-05-29)

```
npm install  → added 37 packages (Kobalte + Tailwind v4)  PASS
tsc --noEmit → 0 errors (strict mode)                       PASS
vite build   → 25 modules transformed
               dist/assets/*.css  35.02 kB (gzip 6.88 kB)
               dist/assets/*.js   80.57 kB (gzip 26.74 kB)
               built in 275ms                               PASS
```

---

## 验收检查清单

### 盯盘页 (Trading)

- [x] v5 认可的 Event→Condition→DualBook 结构完整保留 (TradingPage.tsx)
- [x] 订单簿 5 档 (stub 数据 bids[0..4] + asks[0..4])
- [x] DepthBar 可视深度条 (买绿/卖红, 相对最大量归一化)
- [x] ConfBar 置信度条形图 (替换数字, 绿/黄/红)
- [x] CI 区间主显示行 (v6 升格为独立 cond-ci-row)
- [x] imbalance 数值标注 (v6 新增 mini-imb-val)
- [x] 筛选栏: 全部/进行中/有持仓 + 搜索框 (本地 filter)
- [x] per-event staleness 展示 (EventHeader_v6 evt-staleness)
- [x] LIVE 角标 (inplay 状态)
- [x] DEMO 横幅 + demo-chip 角标 (ADR-041 §5)
- [x] advisory 角标 (XD-3)
- [x] XD-1/3/4/5 全部保留

### Ops 观测页

- [x] 系统健康 StatCard 2x4 网格 (H-01~H-07)
- [x] 线程心跳 status-grid 5 行 + stub 标注
- [x] 市场订阅数 (GAP-01/02 兜底: conditionCache 推算 + 标注)
- [x] WSS 三通道 status-grid + reconnect 计数 (S-04~S-07)
- [x] Condition 级 WSS 状态热力格
- [x] 数据质量 StatCard: staleness/gap/drift (Q-01~Q-03)
- [x] staleness 可视化 latency bar
- [x] 4 时间戳瀑布 PipelineHealth (R-20, 采样首个市场)
- [x] 各市场 staleness 热力表 StalenessHeatCell
- [x] 业务吞吐 StatCard: decision/reject/fill/pnl/edge (B-01~B-05)
- [x] PnL 迷你曲线 SVG (B-06)
- [x] Gate 门禁仪表 (B-09)
- [x] 拒单列表表格 (E-01, 最多50条)
- [x] reason_code 分布柱状图 (E-02, 前端本地聚合)
- [x] Prometheus 裸文本折叠 (兜底展开)
- [x] 私钥/签名字段不出现 (ADR-038 §5)

### 导航

- [x] TabNav 4个标签, 高亮当前
- [x] URL hash 同步 (#trading/#ops/#analytics/#market)
- [x] StatusBar 跨页不变
- [x] Store 轮询跨 tab 切换不重置

### 兼容性

- [x] PnlSparkline 保留 (盯盘页 + 分析页各用一次)
- [x] v5 GlobalBar / SecondaryFooter / EventGrid 文件保留 (未删除, 防引用遗漏)
- [x] stub 模式 (?stub=1) 正常工作

---

## 后端数据缺口提示 (须告知小冯+小卢)

| 缺口 | 影响 | 临时兜底 | 严重度 |
|------|------|---------|--------|
| GAP-01: `stcpp_subscribed_tokens_total` 未在 /metrics 输出 | Ops订阅数显示不准 | conditionCache×2 估算 + "(前端估算)" 标注 | P0 |
| GAP-02: `stcpp_subscribed_markets_total` 同上 | 同上 | conditionCache.keys().length | P0 |
| H-02: healthz threads 全是 stub "alive" | 线程心跳不真实 | 已标 "(stub)" 角标 | P1 W10+ |
| Q-05: `/api/v1/data/latency/{id}` 未实现 | 4ts 瀑布无精确分段 | 直接读 book 字段前端算 | P2 |

---

## 启动命令

```bash
# 安装 (首次或新依赖)
cd frontend/
npm install

# 开发 (HMR, 连后端 8080 API)
npm run dev     # → http://127.0.0.1:3000

# stub 模式 (无后端)
open "http://127.0.0.1:3000/?stub=1"

# 生产构建
npm run build   # 产物 dist/

# 后端 (独立进程, 不托管前端)
stcpp_debug_server --real --replay --port 8080
```

---

## v5.x 遗留验证记录

以下 v5 验收项在 v6 中全部继承:

- XD-1/3/4/5 AI provenance 红线: PASS (TradingPage.tsx CondQuote_v6)
- 赛事分组 Event→Condition 三层: PASS
- R1~R6 小尤去乱规则: PASS (style.css 完整保留)
- 错误态 fail-chip / api-err-chip: PASS
- staleness 状态点: PASS (升级为 StatusDot 组件)
- 轮询分层 5s/15s/30s/60s: PASS (store.ts 不变)
- v6 新增: metrics 无条件 30s 轮询 (v5 只在 secondaryOpen 时才拉)

---

## 未做事项 (后续)

- P2: 市场详情页 `/market/:conditionId` (依赖 ADR-038 trace 端点)
- P1 扩展: Analytics 时间窗选择器 (1h/6h/24h/7d)
- 小宫 #48 dogfood: 浏览器截图验证
- 小郑 Sprint-3: Grafana 接入 (Prometheus scrape 观测历史)
- 后端补齐 GAP-01/02 后删前端兜底估算
