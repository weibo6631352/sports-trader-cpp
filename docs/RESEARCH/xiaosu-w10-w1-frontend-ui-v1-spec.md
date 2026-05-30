# Frontend UI v1 Spec — M1-H PnL 看板 (8 page)

- Owner: 小苏 (frontend-engineer)
- Date: 2026-05-29
- Status: DRAFT v1 — 待老胡 PM ack + 小尤 UX Wave 96 并行
- 关联:
  - `laozhou-w8-debug-rest-api-spec-v1.md` (老周 9 endpoint SSOT)
  - `xiaolu-w9-rest-api-skeleton-implementation-spec-v1.md` (小卢 C++ 实施)
  - `xiaoying-acceptance-spec-v1.md` §1.H (M1-H PnL 看板 3 条 acceptance)
  - `xiaosu-ui-wireframe-v0.1.md` (前序 wireframe, 技术栈历史背景)
  - `xiaoyou-ux-framework-v1.md` (7 维 + 4 色法 + P0 告警约定)
  - paper/live 单 binary 架构 (原 ADR-011, 已删; mode badge 仍须按 paper/live 分离显示)

---

## §1 技术栈选型

### 1.1 框架: React 18

选型来源: https://react.dev (React 官方文档, 2026-05-29 访问)

官方描述: "React lets you build user interfaces out of individual pieces called components."

选择 React 18 而非 Vue 3 / Svelte 5 的理由:

| 维度 | React 18 | Vue 3 | Svelte 5 |
|---|---|---|---|
| 与前序 wireframe 对齐 | 已在 xiaosu-ui-wireframe-v0.1.md §1.2 选定 React 18 | 重新决策成本高 | 生态较小 |
| TypeScript 支持 | 原生 + Create React App / Vite 模板均内置 | 原生 | 原生 |
| TanStack Query 生态 | 官方推荐搭配, polling/refetch 内置 | 有, 但非默认搭配 | 轻量但需手写 |
| 团队经验积累 | wireframe §1.2 已标注"团队 SaaS 经验" | N/A | N/A |
| W11 paper runtime 窗口 | CRA/Vite dev server 当日可启 | 可, 但需迁移成本 | 可 |

**结论: React 18 + TypeScript + Vite**

- Vite: https://vitejs.dev — "Next Generation Frontend Tooling". dev server 冷启动 < 300ms, HMR 即时. 无需 webpack 配置.
- TypeScript: 全量类型覆盖, REST API JSON response 类型定义直接从 endpoint schema 生成 (见 §2 各页数据类型).

### 1.2 数据拉取: TanStack Query v5

来源: https://tanstack.com/query/latest (TanStack Query 官方文档, 2026-05-29 访问)

官方描述: "Powerful asynchronous state management for TS/JS, React, Solid, Vue and Svelte."

用途:
- 所有 GET endpoint 走 `useQuery` + polling (`refetchInterval`)
- POST /drain / POST /resume 走 `useMutation` + audit trail hook
- 自动 stale/loading/error 状态管理, 减少手写 useState

polling 间隔策略 (与热路径隔离原则对齐, 不拖慢 vCPU0-2):

| 看板 | endpoint | refetchInterval |
|---|---|---|
| 系统状态 | /status | 2000ms |
| 当前持仓 | /positions | 3000ms |
| orderbook 镜像 | /orderbook/{token_id} | 1000ms |
| signal 历史 | /signals/history | 5000ms |
| risk reject | /risk/rejects | 5000ms |
| audit 时间线 | /audit/recent | 5000ms |
| metrics | /metrics | 10000ms |

注: /metrics 是 Prometheus text format, 前端解析 text → 数字展示, 不走 Grafana 渲染路径 (W11 paper 阶段简化).

### 1.3 样式: Tailwind CSS v3

来源: https://tailwindcss.com (Tailwind CSS 官方文档, 2026-05-29 访问)

官方描述: "A utility-first CSS framework packed with classes like flex, pt-4, text-center and rotate-90 that can be composed to build any design, directly in your markup."

选择理由:
- 不引入 CSS-in-JS 运行时 (符合 wireframe §1.1 "不闪烁不动画" 纪律)
- 4 色法 (green/yellow/red/gray) 直接映射 Tailwind 颜色 class
- 与 shadcn/ui 组件库搭配 (下见)

### 1.4 组件库: shadcn/ui

来源: https://ui.shadcn.com (shadcn/ui 官方文档, 2026-05-29 访问)

官方描述: "Beautifully designed components that you can copy and paste into your apps. Accessible. Customizable. Open Source."

特点: 不是 npm 包依赖, 源码直接复制进项目, 零黑盒. 基于 Radix UI 原语 + Tailwind. 提供 Table / Badge / Button / Dialog / Toast 等 M1-H 看板所需基础组件.

用途:
- Table: 持仓表格 / signal 历史 / risk reject 列表 / audit 时间线
- Badge: system state (RUNNING/DRAIN/HALTED) 颜色标签
- Button + Dialog: DRAIN/RESUME 二次确认弹窗
- Toast: 操盘成功/失败通知

### 1.5 接口层: 原生 fetch (curl-friendly)

- 所有 API 调用走 `fetch()` (浏览器原生), 不引入 axios
- Base URL: `http://localhost:8080` (paper 阶段本地; Frankfurt 部署时切 env var)
- 无 session/cookie, 无 auth token (W8 MVP 约定, 公网部署前补 bearer token)
- Content-Type: `application/json; charset=utf-8`
- 与 curl 完全兼容: UI 发的 GET/POST 等价于老周 spec §6 curl 示例

### 1.6 环境配置

```
VITE_API_BASE_URL=http://localhost:8080   # .env.local (不入 git)
VITE_APP_MODE=paper                       # 来自后端 /version.build_mode
```

`.env.example`:
```
VITE_API_BASE_URL=http://localhost:8080
```

### 1.7 W11 paper runtime 适配约定

- UI 以 paper mode 启动, 顶部全局 badge 显示 `PAPER MODE` (橙色 Tailwind `bg-orange-500`)
- 所有写操作 (DRAIN/RESUME) 按钮在 paper mode 下仍可用 (便于调试), 但 audit trail 标注 `mode: paper`
- 无需 auth (paper 阶段 loopback only, 老周 spec §2.1)

---

## §2 M1-H PnL 看板 8 page

### 全局布局约定 (小尤 UX 框架对齐)

- 左侧 sidebar nav: 8 个看板入口 + mode badge
- 右上角: mode (PAPER/LIVE) badge + system state badge + 最后刷新时间
- 4 色法: green = 正常, yellow = WARNING, red = DRAIN/HALTED/error, gray = 未知/loading
- 一屏 ≤ 12 panel (wireframe 铁律)
- 固定等宽字体 (`font-mono`) 用于数字/token_id/audit_id 展示

---

### 看板 1: 系统状态 (System Status)

**对应 endpoint:** `GET /status`

**M1-H acceptance 关联:** M1-H01 (今日 paper PnL 一个数字首屏可见), M1-G01 (GM 一键 halt)

**数据类型 (TypeScript):**
```typescript
interface StatusResponse {
  state: "RUNNING" | "DRAIN" | "HALTED";
  mode: "paper" | "live" | "backtest";
  wss_connected: { sports_api: boolean; clob: boolean; user_channel: boolean };
  signals_active_count: number;
  positions_count: number;
  rm_rejects_last_60s: number;
  uptime_sec: number;
  as_of_ts: number; // epoch ns int64
}
```

**页面元素:**

| 区域 | 内容 | 颜色规则 |
|---|---|---|
| 主 state badge | RUNNING / DRAIN / HALTED (大字, 居中) | green / yellow / red |
| mode badge | PAPER / LIVE (右上角常驻) | orange (paper) / blue (live) |
| WSS 连接状态 | sports_api / clob / user_channel 三个圆点 | green=true, red=false |
| 活跃信号数 | signals_active_count 数字 | white |
| 持仓数 | positions_count 数字 | white |
| 近 60s 拒单数 | rm_rejects_last_60s | yellow (>0), green (==0) |
| 运行时长 | uptime_sec 格式化为 "Xd Xh Xm" | gray |
| as_of_ts | 格式化时间戳 (ET 时区) | gray small |

**刷新策略:** `refetchInterval: 2000` ms, loading skeleton 占位

**权限:** 只读, 无操盘按钮 (操盘按钮在看板 8)

---

### 看板 2: 当前持仓 (Positions)

**对应 endpoint:** `GET /positions`

**M1-H acceptance 关联:** M1-H01 (PnL 首屏可见), M1-H03 (PnL 与 audit chain 一致)

**数据类型:**
```typescript
interface Position {
  token_id: string;
  condition_id: string;
  outcome: string;
  pos_yes: string;  // decimal string
  pos_no: string;
  avg_entry_price: string;
  pnl_unrealized: string;  // 可为负
  as_of_ts: number;
}
interface PositionsResponse {
  mode: string;
  count: number;
  positions: Position[];
  total_pnl_unrealized: string;
  as_of_ts: number;
}
```

**页面元素:**

| 区域 | 内容 | 颜色规则 |
|---|---|---|
| 顶部 PnL 汇总 | `total_pnl_unrealized` 大字 + "$" 前缀 | green (>0), red (<0), gray (==0) |
| 持仓表格 | token_id (缩略) / outcome / pos_yes / pos_no / avg_entry / pnl_unrealized | pnl 列: green/red |
| mode badge | paper/live (与看板 1 联动) | orange/blue |
| 空态 | "No open positions" gray 文字 | gray |

**UX 细节:**
- token_id 截断显示前 8 位 + "..." + 后 4 位, hover 展开全串 (Tailwind `truncate` + tooltip)
- pnl_unrealized 负数加括号: `($0.50)` 而非 `-0.50` (操盘员习惯)
- 表格排序: 默认按 `pnl_unrealized` 降序 (最大盈利在顶)

**刷新策略:** `refetchInterval: 3000` ms

---

### 看板 3: OrderBook 镜像 (OrderBook Mirror)

**对应 endpoint:** `GET /orderbook/{token_id}`

**数据类型:**
```typescript
interface OrderLevel { price: string; size: string; }
interface OrderBookResponse {
  token_id: string;
  condition_id: string;
  outcome: string;
  bids: OrderLevel[];
  asks: OrderLevel[];
  mid_price: string;
  spread: string;
  event_ts: number;
  data_source_ts: number;
  ingestion_ts: number;
  as_of_ts: number;
}
```

**页面元素:**

| 区域 | 内容 |
|---|---|
| token_id 输入框 | 手动输入或从持仓看板点击跳转 (URL param `?token_id=xxx`) |
| 双边 book 表格 | bids (绿底渐变) / asks (红底渐变), 各显示 ≤ 10 档 |
| mid_price + spread | 居中展示, spread 单位 "ct" (cent) |
| 4-ts 延迟链 | event_ts → data_source_ts → ingestion_ts → as_of_ts 四格时间差 (ms 单位) |
| stale 告警 | `as_of_ts` 与 now() 差 > 5s 显示黄色 stale badge |

**UX 细节:**
- bid/ask size 可视化为横向 bar (宽度正比于 size, 相对最大档)
- 4-ts 延迟链是 R-20 可观测性展示, 格式: `+Xms` (每段时间差), 总链路 > 2000ms 变红

**刷新策略:** `refetchInterval: 1000` ms (book 更新最频繁)

---

### 看板 4: Signal 历史 (Signal History)

**对应 endpoint:** `GET /signals/history?limit=50`

**数据类型:**
```typescript
interface SignalRecord {
  signal_id: string;
  token_id: string;
  outcome: string;
  fair_value: string;
  side: "BUY" | "SELL";
  rm_verdict: string;  // "PASS" 或 "REJECT:XXX"
  as_of_ts: number;
}
interface SignalsHistoryResponse {
  limit: number;
  count: number;
  signals: SignalRecord[];
  as_of_ts: number;
}
```

**页面元素:**

| 区域 | 内容 | 颜色规则 |
|---|---|---|
| 表格列 | signal_id / token_id / outcome / fair_value / side / rm_verdict / 时间 | rm_verdict: PASS=green, REJECT=red |
| 筛选 | side (BUY/SELL/ALL) + rm_verdict (PASS/REJECT/ALL) | dropdown |
| limit 选择 | 20 / 50 / 100 / 200 (上限 clamp 200, 老周 spec) | select |
| 空态 | "No signals in history" | gray |

**刷新策略:** `refetchInterval: 5000` ms

---

### 看板 5: Risk Reject 历史 (Risk Rejects)

**对应 endpoint:** `GET /risk/rejects?limit=50`

**数据类型:**
```typescript
interface RejectRecord {
  seq: number;
  reject_code: string;
  token_id: string;
  sub_reason: string | null;
  as_of_ts: number;
}
interface RiskRejectsResponse {
  limit: number;
  count: number;
  reject_code_distribution: Record<string, number>;
  rejects: RejectRecord[];
  as_of_ts: number;
}
```

**页面元素:**

| 区域 | 内容 |
|---|---|
| 拒单分布横向 bar chart | reject_code_distribution, 按频次排序, 各 code 一行 |
| 拒单列表表格 | seq / reject_code / token_id (截断) / sub_reason / 时间 |
| total 统计 | "过去 N 条中 PASS X 条, REJECT Y 条" (从 /status 联动) |

**UX 细节:**
- reject_code 用 Badge 组件展示, 颜色固定 (STALE_BOOK=yellow, SIZE_EXCEEDS_CAP=orange, 其他 code=red)
- 分布图横向 bar: Tailwind 纯 CSS 实现, 无引入图表库 (保持零依赖原则)
- sub_reason 为 null 时显示 "—" (em dash)

**刷新策略:** `refetchInterval: 5000` ms

---

### 看板 6: Audit 时间线 (Audit Timeline)

**对应 endpoint:** `GET /audit/recent?limit=50`

**数据类型:**
```typescript
interface AuditEvent {
  audit_id: string;
  event_type: string;
  token_id?: string;
  outcome?: string;
  price?: string;
  size?: string;
  reject_code?: string;
  event_ts: number;
  data_source_ts: number;
  ingestion_ts: number;
  as_of_ts: number;
}
interface AuditRecentResponse {
  limit: number;
  count: number;
  events: AuditEvent[];
  as_of_ts: number;
}
```

**页面元素:**

| 区域 | 内容 |
|---|---|
| 时间线列表 | 每条 audit event 一行, audit_id (8位缩略) / event_type / 关键字段 / 时间 |
| event_type badge | AET_ORDER_PLACED=green, AET_RISK_REJECT=red, AET_STATE_TRANSITION=yellow, 其他=gray |
| 筛选 | event_type 下拉多选 |
| audit_id 复制按钮 | 点击复制完整 audit_id (便于 grep WAL) |

**M1-E acceptance 关联:** M1-E01 (audit_id 完整链路复盘, 查询 < 10s)

**UX 细节:**
- 时间线倒序 (最新在顶)
- audit_id 使用 `font-mono text-xs` 展示, 点击复制全串
- 4-ts 各字段 hover tooltip 显示具体 ns 值 (便于 R-20 人工核查)

**刷新策略:** `refetchInterval: 5000` ms

---

### 看板 7: Metrics Dashboard (Prometheus)

**对应 endpoint:** `GET /metrics` (Prometheus text format)

**注意:** /metrics 返回 Prometheus text format, 不是 JSON. 前端需要 text parser.

**数据解析:**
```typescript
// 简易 Prometheus text parser (inline 实现, 不引入 prom-client)
function parsePrometheusText(text: string): Map<string, number> {
  // 解析 "metric_name{labels} value" 格式
  // 跳过 # HELP / # TYPE 行
}
```

**关注 metric 列表 (来自老周 spec §6 /metrics 示例):**

| metric | 含义 | 展示 |
|---|---|---|
| `stcpp_signals_active_total` | 当前活跃 signal 数 | 数字 |
| `stcpp_rm_rejects_total{code=...}` | 各 code 拒单累计 | 横向 bar |
| `stcpp_wss_connected{channel=...}` | WSS 连接状态 | 3 个圆点 |
| `stcpp_uptime_seconds` | 进程 uptime | 格式化时间 |

**页面元素:**

| 区域 | 内容 |
|---|---|
| raw metrics 折叠面板 | 完整 text 格式 (便于 debug), 默认折叠 |
| 关键 metric 卡片 | 4 个关键 metric 各一个数字卡片 |
| 刷新时间 | "Last fetched Xs ago" |

**W11 约定:** 本看板是 Grafana 接入前的轻量替代. W12 小郑 observability 栈落地后, 此看板指向真 Grafana dashboard URL (iframe 或跳转), 本 text parser 可退休.

**刷新策略:** `refetchInterval: 10000` ms

---

### 看板 8: 运维操作 (Operations — DRAIN / RESUME)

**对应 endpoint:** `POST /drain`, `POST /resume`

**M1-G acceptance 关联:** M1-G01 (GM 一键 halt 二次确认 < 1s 生效), M1-H01 (看板首屏)

**数据类型:**
```typescript
interface DrainRequest { reason: string; }
interface DrainResponse {
  ok: boolean;
  prev_state?: string;
  new_state?: string;
  reason?: string;
  error?: string;
  current_state?: string;
  as_of_ts: number;
}
```

**页面元素:**

| 区域 | 内容 |
|---|---|
| 当前 state 大字 | 与看板 1 联动, RUNNING/DRAIN/HALTED |
| DRAIN 按钮 | 红色, 仅在 state==RUNNING 时 enabled |
| RESUME 按钮 | 绿色, 仅在 state==DRAIN 时 enabled |
| reason 输入框 | DRAIN 操作必填 reason (非空校验) |
| 二次确认 Dialog | 弹窗 + 3s 倒计时取消 (wireframe §1.1 铁律: 凌晨 3 点不能按错) |
| 操作结果 Toast | 成功: "DRAIN triggered. prev=RUNNING → new=DRAIN"; 失败: "Error: ALREADY_DRAIN" |
| audit trail 展示 | 操作后显示 `as_of_ts` 时间戳 (audit 留痕) |

**二次确认弹窗 UX (P0 操作规范):**
```
[Dialog]
  Title: "确认 DRAIN 系统?"
  Body:  "当前状态: RUNNING
          操作原因: [reason 输入内容]
          此操作将停止所有新订单.
          [3 秒后可确认]"
  Buttons: [取消] [确认 DRAIN (3s 倒计时)]
```

**权限标注:** W8 MVP 无 auth. M5 实盘前补 bearer token + Yubikey 2FA (老沈 review, wireframe §1.2).

**Audit trail 规范:** 每次 POST /drain 或 POST /resume 成功后, 前端在本地 sessionStorage 记录操作日志 `{action, reason, as_of_ts, operator: "manual-ui"}`, 供 dogfood (小宫) 核查. 真 audit 在后端 WAL (老唐 AuditEmitter).

---

## §3 实施 Timeline

| 周期 | 任务 | 交付物 | 状态 |
|---|---|---|---|
| W10 W1 (本 wave) | 本 spec 撰写 | `xiaosu-w10-w1-frontend-ui-v1-spec.md` | 完成 |
| W10 W2 | 项目脚手架 + 全局布局 + 看板 1/2 | `ui/` 目录 + Vite + React 18 + Tailwind + shadcn/ui 初始化; 看板 1 Status + 看板 2 Positions | 待开始 |
| W10 W3 | 看板 3/4/5 | OrderBook / Signal History / Risk Rejects | 待开始 |
| W10 W4 | 看板 6/7/8 + 二次确认 Dialog + Toast | Audit Timeline / Metrics / Operations | 待开始 |
| W11 W1 | paper runtime 同步启动 | UI dev server 与 paper binary 同时启动, 8 页均可访问 curl-tested endpoint | 待开始 |

**依赖约束:**
- W10 W2 启动前提: 小卢 W9 W3 完成 (debug API 6 endpoint curl 全过, 老胡 PM ack)
- W10 W4 看板 7 /metrics 前提: 小郑 W10 W2 /metrics Prometheus text format 接入完成
- W11 W1 联调前提: 老周 + 老吴 paper binary 启动流程含 API server (CMake BUILD_DEBUG_API=ON)

**文件结构 (预期):**
```
ui/
  package.json
  vite.config.ts
  src/
    api/          — fetch 封装 + TypeScript 类型
    components/   — shadcn/ui 组件 + 业务组件
    pages/        — 8 个看板页面 (Dashboard1Status...Dashboard8Ops)
    hooks/        — useQuery hooks (useStatus, usePositions, ...)
    lib/          — Prometheus text parser + utils
  .env.example
```

---

## §4 与小尤 UX 协作 (Wave 96 并行)

**协作范围:** 小尤负责 7 维 UX 评分 + P0 告警 UX 约定; 小苏负责代码实施.

**Wave 96 并行分工:**

| 小苏 (实施) | 小尤 (评审) |
|---|---|
| 4 色法 Tailwind class 映射 | 确认 green/yellow/red/gray 映射规则 |
| 看板 8 二次确认 Dialog (凌晨 3 点铁律) | P0 操作 UX 3s 倒计时 spec review |
| 字体: `font-mono` 数字 + 单位后缀 | 信息密度 D1 评分 (目标 ≥ 8/10) |
| Toast 告警分级 | P0/P1/P2/P3/P4 噪音控制 D5 评分 |
| 时区显示 ET + 国内 +12/+13 | 阅读路径 D2 评分 |

**接口契约:** 小尤提 UX spec (评分卡 + 标注图), 小苏实施. 分歧走老胡 PM 协调 (需求-工程协商会, CLAUDE.md §5).

**Wave 96 output 预期:** 小尤输出 `xiaoyou-w96-ui-v1-ux-review.md`, 包含 8 个看板的 7 维评分. 任一维 < 准入阈值 → 小苏 W10 W4 修复后再评.

---

## §5 Dogfood (小宫 Wave 97+, W11+)

**触发条件:** W11 W1 paper runtime + UI 联合启动通过后, 小宫开始 dogfood.

**dogfood 覆盖的 M1-H acceptance:**

| acceptance | dogfood 验证方式 |
|---|---|
| M1-H01 今日 paper PnL 一个数字首屏可见 | 小宫打开看板 2 Positions, 截图验证 total_pnl_unrealized 在首屏无滚动可见 |
| M1-H02 7/30 天趋势 (W12+ 扩展) | W11 暂不覆盖 (需历史数据积累), Wave 97 占位 |
| M1-H03 PnL 数据准确 | 小宫 curl /positions 对比 UI 展示数字, diff==0 |
| M1-G01 GM 一键 halt < 1s | 小宫计时 Dialog 弹出 → 确认 → /status state==DRAIN 时间差 < 1000ms |

**dogfood playbook 参照:** `xiaogong-dogfood-playbook-v1.md` (11 边缘场景). 小宫需额外补 3 个 UI-specific 场景:
1. /status endpoint 返回 null 时 UI loading skeleton 行为
2. POST /drain 返回 409 时 Toast 错误显示
3. orderbook token_id 不存在时 404 处理 (看板 3 error state)

---

## §6 ADR-027 cite (数据结构 SSOT 关系)

**结论: N/A**

前端不直接依赖 ADR-027 定义的 C++ 数据结构 SSOT. 前端通过 REST API JSON 间接消费数据, JSON schema 由老周 `laozhou-w8-debug-rest-api-spec-v1.md` §6 定义为 SSOT.

数据流图:

```
C++ struct (ADR-027 SSOT)
    ↓ nlohmann/json 序列化 (小卢 C++ 实施)
JSON response body (老周 spec §6 为 schema SSOT)
    ↓ fetch() + TypeScript 类型 (本 spec §2 各页数据类型)
React component (看板 1-8 展示层)
```

**前端 TypeScript 类型 ≠ C++ struct 镜像**. 前端类型仅保证与 JSON schema 一致, 不引入 C++ 端 ABI 约束. 若后端 JSON schema 变更, 老周/小卢走 ADR + 通知小苏, 前端跟进更新类型定义 (CLAUDE.md §7 规范 3: 最小惊喜).

---

## §7 ADR-029 + ADR-032 流程标注

- **ADR-029** (前端模块归属 + 目录约定): `ui/` 目录归 E 单元小苏, 不入 hot path 构建, CMake 不感知. 独立 `package.json` / Vite 构建链. 生产部署走静态文件 (nginx) 或 Tauri 打包 (未来).
- **ADR-032** (push 后不等 CI): 本 spec 推送后不等 CI 结果. CI 失败由小苏自行跟进.

---

**最后更新:** 2026-05-29 by 小苏 (frontend-engineer)
**下次 review:** W10 W2 实施启动前, 老胡 PM ack + 小卢 W9 W3 API 可用 confirm
**升级路径:** 技术栈争议 → 老胡协调 (需求-工程协商会); UX 评分不过 → 小尤 + 小苏迭代; P0 blocker → @老胡 → @老雷
