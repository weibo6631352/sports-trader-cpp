# INTEGRATION-VERIFY.md — v5 赛事分组卡布局 字段契约对齐验证记录

owner: 小苏 (#12, E单元)
last_review: 2026-05-29
关联ADR: ADR-038 §3 schema 铁律 + v5 方向 A (老板选定) + 小尤设计
SSOT: `src/stcpp/debug_api/endpoint_*.cpp` + `include/stcpp/debug_api/state_provider.hpp`

---

## v5.1 Bug Fix — LAL-BOS 多盘口未并列 (GM 截图验收, 2026-05-29)

### 根因

**Bug 1 — 时序依赖 (主因):** `refreshMarketGrid()` 原逻辑读 `cache.attribution` 推断
allConditionIds。`cache.attribution` 由独立的 `refreshAttribution()`（15s 轮询）写入，
与 `refreshMarketGrid`（5s 轮询）并发启动。首次执行时 `cache.attribution === null`，
导致 `pmPnlMap` 为空，LAL-BOS total/spread（仅在 attribution 中有记录，未在 positions 中）
无法进入 allConditionIds，赛事块只出现 1 列（ml）。

**Bug 2 — stub positions 数据不完整:** `STUB_POSITIONS` 缺少 LAL-BOS total/spread 持仓记录，
与后端 `DemoStateProvider.positions()`（含 5 条）不对齐，加剧了 Bug 1 的影响范围。

**Bug 3 — 布局宽度未充分利用:** `.event-columns` 缺少 `width: 100%`，多盘口列未充满赛事区块，
右侧大片空白。`.cond-col` 使用 `flex: 1 0 220px`（flex-shrink:0），单盘口赛事宽度固定不伸展，
多盘口块也不能均等分配。

**Bug 4 — fetchBook 路由错误:** `fetchBook` 调 `/api/v1/book/{conditionId}`，
后端实际路由为 `/api/v1/book_pair/{conditionId}` (ADR-040)，导致真实后端模式下订单簿全部 404。

### 修复

| 文件 | 修复内容 |
|---|---|
| `app.js` | `refreshMarketGrid` 内部并发拉取 positions + attribution + rejects，不再依赖 `cache.attribution` 时序；三个数据源首次渲染即全部到位 |
| `stub.js` | `STUB_POSITIONS` 补入 LAL-BOS total/spread 持仓（OVER_220.5 / LAL_-5.5），与后端 demo 5 条对齐 |
| `style.css` | `.event-columns` 加 `width: 100%`；`.cond-col` 改 `flex: 1 1 220px; min-width: 220px; max-width: 480px`，多盘口均等填充宽度 |
| `api.js` | `fetchBook` 路由修正为 `/api/v1/book_pair/${conditionId}`（ADR-040 显式端点） |

### v5.1 验证结果 (Node.js 模拟, 2026-05-29)

```
allConditionIds (首次执行, 修复后):
  epl-ars-che-total, mlb-nyy-bos-ml, nba-lal-bos-ml,
  nba-lal-bos-spread, nba-lal-bos-total, nfl-kc-buf-spread
  count: 6  PASS

赛事分组:
  nba-lal-bos-2026-05-29: 3 列 → [ml, spread, total]  PASS (LAL-BOS 三盘口横排)
  epl-ars-che-2026-05-29: 1 列 → [total]               PASS
  mlb-nyy-bos-2026-05-29: 1 列 → [ml]                  PASS
  nfl-kc-buf-2026-05-29:  1 列 → [spread]               PASS

JS 语法检查: node --check *.js → ALL syntax OK  PASS
```

---

## v5 核心变更 (方向 A: 赛事分组卡, 老板选定 + 小尤设计)

### 布局重构
- `renderMarketCard` (逐盘口卡) → `renderEventGroup` (赛事分组区块)
- `renderMarketGrid` → `renderEventGrid` (赛事纵向堆叠)
- 数据组装: 按 `event_id` 分组; `score` 按 `event_id` 去重拉取一次
- LAL-BOS 三盘口 (ml/total/spread) 共享 `event_id` → 同一赛事区块下 3 列并列

### 小尤 6 条去乱规则落实情况

| 规则 | 落实内容 | 文件 |
|---|---|---|
| R1: 比分只在赛事头 | `renderEventHeader` 渲染比分; `renderConditionColumn` 内无比分 | panels.js |
| R2: 颜色语义收敛 | green=bid/正PnL/正Kelly; wss-ok/stale-ok/accepting → 灰色小圆点 `.wss-dot-ok/.acc-dot-ok/.stale-dot-ok` | style.css |
| R3: 字号三档 | `.mono-main` 14px / `.mono-sub` 11px / `.q-lbl` 10px (删除原6档) | style.css |
| R4: 区块用色块分隔 | 量化区 bg3 / 订单簿区 bg(最深) / 持仓区 bg2; 无 border-bottom 横线 | style.css |
| R5: 拒单/gap → 右上角小红点 | `.reject-dot` absolute 定位 + tooltip; `.gap-dot` 小红点; 不内联主路径 | panels.js/style.css |
| R6: chip 禁 flex-wrap | `.event-header-main/.cond-header/.cond-quote-row/.mini-half-header` 全部 `flex-wrap:nowrap; overflow:hidden` | style.css |

---

## curl 实测 (2026-05-29, server 127.0.0.1:8080)

### 赛事分组关键字段: `market.event_id`

```
GET /api/v1/market/nba-lal-bos-ml
→ event_id: "nba-lal-bos-2026-05-29"  PASS

GET /api/v1/market/nba-lal-bos-total
→ event_id: "nba-lal-bos-2026-05-29"  PASS (三盘口同 event_id)

GET /api/v1/market/nba-lal-bos-spread
→ event_id: "nba-lal-bos-2026-05-29"  PASS (三盘口同 event_id)
```

### score 按 event_id 拉取

```
GET /api/v1/score/nba-lal-bos-2026-05-29
→ home: "LAL", away: "BOS", status: "inplay"  PASS
```

### positions (现有盘口)

```
GET /api/v1/positions
→ market_id 集合: nba-lal-bos-ml, nba-lal-bos-total, nba-lal-bos-spread,
                  epl-ars-che-total  PASS
  (v5.1: total/spread 持仓补入, 与后端 DemoStateProvider.positions() 5 条对齐)
```

### 分组逻辑验证 (Node.js 模拟)

```
赛事: nba-lal-bos-2026-05-29 → 3 盘口 (ml/spread/total)  PASS
赛事: epl-ars-che-2026-05-29 → 1 盘口 (total)             PASS
赛事: nfl-kc-buf-2026-05-29  → 1 盘口 (spread)            PASS (stub)
赛事: mlb-nyy-bos-2026-05-29 → 1 盘口 (ml)               PASS (stub)
```

---

## JS 语法检查 (node --check)

```
node --check frontend/src/api.js     → OK
node --check frontend/src/stub.js    → OK
node --check frontend/src/panels.js  → OK
node --check frontend/src/app.js     → OK
```

---

## v5 布局验证

- LAL-BOS 赛事头只渲染 1 次比分 (三盘口共享): PASS (renderEventHeader 在 renderEventGroup 顶部, renderConditionColumn 内无比分)
- LAL-BOS 下挂 3 个盘口列横向并列: PASS (event_id 分组 → .event-columns flex 横向)
- 其余赛事各 1 列: PASS
- 双边簿在列内左右并排 (.cond-dual-grid grid-template-columns: 1fr 1fr): PASS
- 中文化 (5 张映射表 STATUS_ZH/SPORT_ZH/MARKET_TYPE_ZH/REJECT_REASON_ZH/SIDE_ZH): PASS
- Polymarket 超链接 (.evt-link 在赛事头): PASS
- DEMO 红线标记 (.demo-chip 在赛事头 + 量化行): PASS
- P0-02 fail-safe (data_source !== 'live' → demo banner): PASS
- P0-03 错误态可读 (fail-chip/api-err-chip): PASS
- 不折行: 所有 chip 行 flex-wrap:nowrap + overflow:hidden: PASS

---

## serve.py 验证

```
python3 frontend/serve.py 8096
curl http://127.0.0.1:8096/index.html | grep "v5"  → 命中
curl http://127.0.0.1:8096/src/app.js | head -3    → v5 注释确认
```

---

## 变更文件清单

### v5 (a131a49)
- `frontend/index.html` — 标题升 v5
- `frontend/src/stub.js` — 全面重写: 5 盘口 STUB_MARKET_MAP/STUB_BOOK_MAP/STUB_SCORE_MAP/STUB_QUOTE_MAP; LAL-BOS 三盘口共享 event_id
- `frontend/src/app.js` — 重写数据组装: safeGetMapped 多盘口 stub 路由; 按 event_id 分组; score 去重拉取; renderEventGrid 替换 renderMarketGrid
- `frontend/src/panels.js` — 重写渲染: renderEventGrid/renderEventGroup/renderEventHeader/renderConditionColumn/renderCondQuote/renderCondDualBook/renderMiniHalfBook/renderCondPos/renderRejectDot; 6 条去乱规则全部落地
- `frontend/src/style.css` — 全面重写: event-group/event-header/event-columns/cond-col 布局; 字号三档; 颜色收敛; reject-dot/gap-dot; chip 禁 wrap
- `frontend/INTEGRATION-VERIFY.md` — 本文件, 更新至 v5

### v5.1 Bug Fix (本次)
- `frontend/src/app.js` — refreshMarketGrid 并发拉取 positions/attribution/rejects，消除时序依赖
- `frontend/src/stub.js` — STUB_POSITIONS 补入 LAL-BOS total/spread 持仓，与后端 demo 5 条对齐
- `frontend/src/style.css` — event-columns width:100%; cond-col flex:1 1 220px/max-width:480px，充分利用宽度
- `frontend/src/api.js` — fetchBook 路由修正为 /api/v1/book_pair/{conditionId}（ADR-040）
- `frontend/INTEGRATION-VERIFY.md` — 更新至 v5.1，记录 bug 根因 + 修复

---

## 遗留事项 (下一 sprint)

- 浏览器截图验证: 留给小宫 #48 dogfood 轮次
- 单盘口赛事无 positions/rejects/attribution 时也应展示 (需后端 /api/v1/markets list endpoint, 目前 stub 覆盖)
- P2-01/P2-02/P2-04/P2-05/P2-06: 下一 sprint backlog
