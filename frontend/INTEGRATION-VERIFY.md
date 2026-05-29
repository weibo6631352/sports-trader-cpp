# INTEGRATION-VERIFY.md — v3 单屏盯盘终端 字段契约对齐验证记录

owner: 小苏 (#12, E单元)
last_review: 2026-05-29
关联ADR: ADR-038 §3 schema 铁律 + v3 设计评审决议 (2026-05-29)
SSOT: `src/stcpp/debug_api/endpoint_*.cpp` + `include/stcpp/debug_api/state_provider.hpp`

---

## 验证方法

1. `cmake --build build --target stcpp_debug_server` (已就绪 no-op)
2. `build/src/stcpp/debug_api/stcpp_debug_server --port 8092`
3. `curl -s http://127.0.0.1:8092/<endpoint> | python3 -m json.tool` 记录真实 wire JSON
4. Python 逐 key 比对 panels.js 中读取的字段 vs 真实 wire key
5. `node --check frontend/src/*.js` JS 语法验证

---

## v3 新增端点 (curl 验证 2026-05-29)

### `/status` — 新增 `data_source` 字段

```json
{
  "state": "RUNNING", "mode": "paper",
  "data_source": "demo",
  "wss_connected": {"sports_api": false, "clob": false, "user_channel": false},
  "rm_rejects_last_60s": 0, "uptime_sec": 5,
  "as_of_ts": 1780044039305689000
}
```

`data_source` 字段存在 — 用于 DEMO 横幅 (老钱红线 P0). **PASS**

---

### `/api/v1/market/{id}` — 新增 `event_id` + outcome/active/closed/resolved 字段

```json
{
  "found": true, "market_id": "nba-lal-bos-ml", "outcome": "YES",
  "tick_size": 0.01, "fee_rate": 0.02, "neg_risk": false,
  "accepting_orders": true, "active": true, "closed": false, "resolved": false,
  "source": "polymarket",
  "event_id": "nba-lal-bos-2026-05-29"
}
```

`event_id` 字段存在 — 前端用于拉 `/api/v1/score/{event_id}`. **PASS**

---

### `/api/v1/score/{event_id}` — 新 endpoint

```json
{
  "found": true, "event_id": "nba-lal-bos-2026-05-29",
  "sport": "basketball", "status": "inplay",
  "period": "Q3", "clock_sec": 522,
  "home": "LAL", "away": "BOS", "home_score": 87, "away_score": 91,
  "source": "goalserve",
  "event_ts": ..., "data_source_ts": ..., "ingestion_ts": ..., "score_as_of_ts": ...
}
```

字段审计: found/event_id/sport/status/period/clock_sec/home/away/home_score/away_score/source/score_as_of_ts 全部 OK. **PASS**

---

### `/api/v1/quote/{condition_id}` — 新 endpoint

```json
{
  "found": true, "market_id": "nba-lal-bos-ml",
  "fair_value": 0.662, "market_mid": 0.648,
  "edge_bps": 21.6, "kelly_fraction": 0.042,
  "suggested_notional": 850, "signal_strength": 0.71,
  "model_conf": 0.62, "quote_as_of_ts": ...
}
```

字段审计: found/market_id/fair_value/market_mid/edge_bps/kelly_fraction/suggested_notional/signal_strength/model_conf/quote_as_of_ts 全部 OK. **PASS**

---

## 全量字段审计 (python -c 逐 key 比对, 2026-05-29)

| 面板 | 字段数 | 结果 |
|---|---|---|
| renderTopBar / /status      | 6  | 全 OK |
| renderBookBlock / /api/v1/book  | 11 | 全 OK |
| renderScoreBlock / /api/v1/score | 12 | 全 OK |
| renderQuoteBlock / /api/v1/quote | 10 | 全 OK |
| renderPosBlock / /api/v1/positions | 8 | 全 OK |
| renderPnlAttribution / /api/v1/pnl/attribution | 8 | 全 OK |
| renderRejectBadge / /api/v1/risk/rejects | 7 | 全 OK |
| renderMarketCard / /api/v1/market | 11 | 全 OK |

**总计 73 字段映射, 全部命中 wire key. 零 MISSING.**

---

## JS 语法检查

```
node --check frontend/src/api.js     -> OK
node --check frontend/src/stub.js    -> OK
node --check frontend/src/panels.js  -> OK
node --check frontend/src/app.js     -> OK
```

---

## v3 变更文件清单

- `frontend/index.html` — 删除 5 tab 导航, 新增: DEMO 横幅 + 顶部常驻条 + PnL sparkline + market 卡片网格 + 底部折叠区
- `frontend/src/api.js` — 新增 fetchScore/fetchQuote, 新增 fmtClock/stalenessMs 工具函数
- `frontend/src/panels.js` — 全面重写: renderTopBar/renderPnlSparkline/renderMarketCard/renderMarketGrid + 6 卡片块函数; 保留 renderPnlAttribution/renderMetrics
- `frontend/src/app.js` — 全面重写: 删 tab 逻辑, 新增 market 网格数据组装(positions→并发fetch market/book/score/quote), 分层轮询
- `frontend/src/stub.js` — 新增 STUB_SCORE/STUB_QUOTE, STUB_STATUS 补 data_source, STUB_MARKET 补 event_id/outcome/active
- `frontend/src/style.css` — 新增市场卡片/比分条/量化参数块/订单簿块/持仓块/DEMO 横幅/sparkline 等全套 v3 样式

---

## 遗留事项 (下一 sprint)

- 浏览器截图验证: 无浏览器工具, 留给小宫 #48 dogfood 轮次
- `?stub=1` 端到端浏览器渲染: stub 结构已与 wire 对齐, 待 dogfood 确认
- 小尤 UX 评分卡验收 (信息密度/颜色语义/盯盘心流) — 决议 §5 验收门
- market info list endpoint (`/api/v1/markets`) 到位后, 市场集合改从 list API 拉 (替代现在从 positions 推断)
