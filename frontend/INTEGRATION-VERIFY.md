# INTEGRATION-VERIFY.md — v7 SUID/Material Design Google 风格

owner: 小苏 (#12, E单元)
last_review: 2026-05-29
关联: ADR-038/040/041, 老板 v7 定 (Google风格+经典组件+数据细化+4页铺满)

---

## v7.0 主变更清单

| 变更 | 内容 |
|------|------|
| 组件库 | `@suid/material` v0.19.0 (Material UI for SolidJS, Google Material Design) |
| 移除 | Kobalte / Tailwind / 自写 TabNav / Badge / ConfBar / StatCard (旧版) |
| 主题 | Material dark + quant 量化语义色 (green #4caf50 / red #f44336 / amber #ff9800 / blue #2196f3) |
| ThemeProvider | `createTheme({palette:{mode:'dark'}})` 包裹全局 |
| 经典 SUID 组件 | AppBar / Toolbar / Card / CardHeader / CardContent / Chip / LinearProgress / Table* / Grid / Alert / Button / ToggleButtonGroup / TextField / Divider / Paper / Box / Typography / IconButton |
| Tab 导航 | AppBar + Toolbar + 自定义 `quant-tab-btn` (SUID 无 Tabs 组件, 用 border-bottom 下划线风格) |
| StatCard | 换用 SUID Card + CardContent + Typography |
| ConfBar | 换用 SUID LinearProgress (置信度条) |
| 拒单日志 | 换用 SUID Table / TableHead / TableRow / TableCell |
| 分市场PnL | 换用 SUID Table |
| 筛选栏 | 换用 SUID ToggleButtonGroup |
| 4 页铺满 | 盯盘/Ops观测/PnL分析(时间窗+归因+分市场表)/市场详情(单盘口深钻完整) |
| 数据更全 | microprice/tick/fee/source/gap/seq/4ts/CI区间/Gate全指标/拒单分布 |

---

## 构建验证 (2026-05-29)

```
npm install @suid/material → PASS (已安装 v0.19.0)
tsc --noEmit               → 0 errors (strict mode)        PASS
vite build                 → 306 modules transformed
                              dist/assets/*.css   18.08 kB  (gzip 3.67 kB)
                              dist/assets/*.js   253.72 kB  (gzip 70.43 kB)
                              built in 566ms               PASS
```

---

## 验收检查清单

### 盯盘页 (Trading)

- [x] Material Card 赛事卡片 (Card variant="outlined")
- [x] Material Chip: sport 标签 / 状态 / DEMO / NR / advisory / 拒单数
- [x] Material LinearProgress: 置信度 (confBar) + edge 优势条
- [x] Material ToggleButtonGroup: 全部/进行中/有持仓 筛选
- [x] Material TextField: 搜索框
- [x] Material Alert: advisory 横幅 (XD-3) + predict_ok=false 异常提示
- [x] 数据补全: microprice / tick_size / fee_rate / source / gap_count / seq
- [x] v5 认可的 Event→Condition→DualBook 三层结构完整保留
- [x] 订单簿 5 档 + DepthBar 深度条 (买绿/卖红)
- [x] CI 区间主显示行
- [x] imbalance 数值 + 渐变条
- [x] LIVE 角标 (inplay)
- [x] DEMO 横幅 (Material Alert warning) + Chip
- [x] advisory 横幅 (XD-3)
- [x] XD-1/3/4/5 全部保留
- [x] per-event staleness 展示
- [x] vig badge (Chip color="success/warning/error")

### Ops 观测页

- [x] Material Card 各区块 (CardHeader + CardContent)
- [x] Material Grid 自适应布局
- [x] Material Table: 拒单日志 (stickyHeader + maxHeight 滚动)
- [x] Material LinearProgress: loop p99 延迟 + staleness 可视化
- [x] Material Chip: 状态/拒单类型/Gate/线程
- [x] 系统健康 8 卡片 (H-01~H-07 + loop p99)
- [x] 线程心跳 grid
- [x] 市场订阅数 GAP-01/02 兜底 + 标注
- [x] WSS 三通道 + reconnect 计数 (Chip color)
- [x] Condition 级 WSS 热力格
- [x] 数据质量 3 卡片: staleness/gap/drift
- [x] staleness LinearProgress
- [x] 4 时间戳瀑布 PipelineHealth (R-20)
- [x] 各市场 staleness 热力表
- [x] 业务吞吐 Grid (decision/reject/fill/pnl/edge + Sharpe/Gate)
- [x] PnL 迷你 SVG 曲线
- [x] 拒单表 (SUID Table, 最多50条)
- [x] reason_code 分布柱状图
- [x] Prometheus 裸文本折叠 (CardHeader action Button)
- [x] 私钥/签名字段不出现 (ADR-038 §5)

### PnL 分析页 (Analytics)

- [x] 时间窗选择 1h/6h/24h (Material ToggleButtonGroup)
- [x] 汇总统计 4 卡片 (净PnL/手续费/成交笔/时间窗)
- [x] 大图 SVG 净值曲线 (含面积填充 + 零线)
- [x] 归因瀑布 Material LinearProgress (毛收益/手续费/Gas/滑点/价差/净收益)
- [x] 分市场 PnL Material Table (stickyHeader, 按净PnL排)
- [x] Gate 仪表 7 卡片 (Sharpe/命中率/回撤/正收益日/笔数/初审/确认审)
- [x] Material Alert (无数据提示)

### 市场详情页 (Market Detail)

- [x] 第4页正式实现 (不再是 P2 占位)
- [x] condition_id 搜索 + Chip 列表选择
- [x] Market 全字段 Table (condition_id/market_id/event_id/slug/tick/fee/neg_risk/accepting/active/closed/resolved/source/mode/ts/url)
- [x] Tokens Chip 列表 (含价格 + winner 状态)
- [x] Score 实时比分 5 卡片 (sport/状态/节-时/主客队得分)
- [x] Quote 量化详情 (fair/mid/edge/kelly/notional/signal + CI + 置信度 LinearProgress + AI provenance 字段表)
- [x] 双边全档订单簿 Table (全部档位 + LinearProgress 深度)
- [x] 4 时间戳 PipelineHealth
- [x] 该市场拒单明细 Table

### 导航

- [x] Material AppBar 常驻 (系统状态 + mode Chip + 净PnL + WSS dots + Gate Chip + 拒单/60s + API异常 Chip)
- [x] DEMO Material Alert 横幅
- [x] Tab 导航 Material 风格 (下划线 active)
- [x] URL hash 同步 (#trading/#ops/#analytics/#market)
- [x] Store 轮询跨 tab 切换不重置

---

## 启动命令

```bash
cd frontend/
npm install

# 开发 (HMR, 连后端 8080 API)
npm run dev     # → http://127.0.0.1:3000

# stub 模式 (无后端)
open "http://127.0.0.1:3000/?stub=1"

# 生产构建
npm run build   # 产物 dist/

# 后端 (独立进程)
stcpp_debug_server --real --replay --port 8080
```

---

## 后端数据缺口

| 缺口 | 影响 | 临时兜底 | 严重度 |
|------|------|---------|--------|
| GAP-01: `stcpp_subscribed_tokens_total` 未在 /metrics 输出 | Ops订阅数不准 | conditionCache×2 估算 | P0 |
| GAP-02: `stcpp_subscribed_markets_total` 同上 | 同上 | conditionCache.keys().length | P0 |
| H-02: healthz threads 全是 stub "alive" | 线程心跳不真实 | "(stub)" Chip 标注 | P1 |

---

## v6.x 遗留验证记录 (继承)

- XD-1/3/4/5 AI provenance 红线: PASS
- 赛事分组 Event→Condition 三层: PASS
- 错误态: fail Chip / api-err Chip: PASS
- 轮询分层 5s/15s/30s/60s: PASS (store.ts 不变)
- metrics 无条件 30s 轮询: PASS
