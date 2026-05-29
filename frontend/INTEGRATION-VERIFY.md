# INTEGRATION-VERIFY.md — v4 单屏盯盘终端 字段契约对齐验证记录

owner: 小苏 (#12, E单元)
last_review: 2026-05-29
关联ADR: ADR-038 §3 schema 铁律 + v4 设计评审决议 (老板双边订单簿要求)
SSOT: `src/stcpp/debug_api/endpoint_*.cpp` + `include/stcpp/debug_api/state_provider.hpp`

---

## v4 核心变更

- 双边订单簿卡片: `/api/v1/book/{id}` 返回 BinaryMarketBookView (token0 + token1)
- Polymarket 超链接: market.polymarket_url → 卡片标题 `<a>` 链接
- 中文化: 集中映射表 (STATUS_ZH/SPORT_ZH/WSS_STATE_ZH/REJECT_REASON_ZH/SIDE_ZH)
- 信息扩展: 盘口元信息全量/tokens两边价格/book health(seq/gap/wss)/signal_strength/model_conf
- 小尤 P0-01: 配色纪律修复 (PAPER灰/DEMO黄/score-pre灰/wf-net黄, 删语义蓝/橙)
- 小尤 P0-02: demo fail-safe (`data_source !== 'live'`)
- 小尤 P0-03: safeGet 错误追踪 + fail-chip + api-err-chip
- 小宫 P2-03: sparkline 时间范围标注 (windowLabel + 起止时间)
- P1-01: 净PnL 移到顶部条 state 之后 (第二位)

---

## 验证方法

1. `curl -s http://127.0.0.1:8080/<endpoint> | python3 -m json.tool` 记录真实 wire JSON
2. Python 逐 key 比对 panels.js 中读取的字段 vs 真实 wire key
3. `node --check frontend/src/*.js` JS 语法验证

---

## curl 实测 (2026-05-29, server 127.0.0.1:8080 DemoStateProvider)

### `/healthz`

```json
{"ok":true,"threads":{"ingest_reactor":"alive","signal_engine":"alive","risk_manager":"alive","paper_signer":"alive","api_server":"alive"},"uptime_sec":78,"as_of_ts":...}
```
PASS

### `/version`

```json
{"version":"0.1.0","git_hash":"b042254","build_mode":"paper","build_time":"May 29 2026 17:27:21","cpp_standard":"C++20","as_of_ts":...}
```
PASS

### `/status`

```json
{"state":"RUNNING","mode":"paper","data_source":"demo","wss_connected":{"sports_api":true,"clob":true,"user_channel":false},"rm_rejects_last_60s":2,"uptime_sec":78,"as_of_ts":...}
```
`data_source` 字段存在, P0-02 fail-safe 使用 `!== 'live'` 判断. **PASS**

### `/api/v1/market/{id}` — v4 新增字段

```json
{
  "condition_id": "nba-lal-bos-ml",
  "market_id": "nba-lal-bos-ml",
  "tick_size": 0.01, "fee_rate": 0.02, "neg_risk": false, "neg_risk_market_id": "",
  "accepting_orders": true, "active": true, "closed": false, "resolved": false,
  "source": "polymarket",
  "event_id": "nba-lal-bos-2026-05-29",
  "slug": "nba-lal-bos-2026-05-29",
  "polymarket_url": "https://polymarket.com/event/nba-lal-bos-2026-05-29",
  "tokens": [
    {"token_id":"tok-lal-001","outcome":"LAL","price":0.65,"winner":false},
    {"token_id":"tok-bos-001","outcome":"BOS","price":0.35,"winner":false}
  ]
}
```
condition_id/tokens[]/slug/polymarket_url/neg_risk_market_id 全部存在. **PASS**

### `/api/v1/book/{id}` — v4 BinaryMarketBookView (双边)

```json
{
  "condition_id": "nba-lal-bos-ml",
  "cross_spread": 0.012,
  "event_ts": ..., "data_source_ts": ..., "ingestion_ts": ..., "as_of_ts_ns": ...,
  "token0": {
    "found": true, "token_id": "tok-lal-001", "outcome": "LAL",
    "best_bid": 0.644, "best_ask": 0.656, "microprice": 0.648,
    "spread": 0.012, "imbalance": 0.23,
    "sequence_no": 88421, "gap_count": 0, "wss_state": "CONNECTED",
    "bids": [{"price":0.644,"size":3200},...],
    "asks": [{"price":0.656,"size":2700},...]
  },
  "token1": { ... "outcome": "BOS", "best_bid": 0.344, ... }
}
```
双边 book 结构完整, cross_spread/token0/token1/as_of_ts_ns 全部存在. **PASS**

### `/api/v1/score/{event_id}`

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
**PASS**

### `/api/v1/quote/{condition_id}`

```json
{
  "found": true, "market_id": "nba-lal-bos-ml",
  "fair_value": 0.662, "market_mid": 0.648,
  "edge_bps": 21.6, "kelly_fraction": 0.042,
  "suggested_notional": 850, "signal_strength": 0.71, "model_conf": 0.62,
  "quote_as_of_ts": ...
}
```
signal_strength/model_conf 已展示. **PASS**

### `/api/v1/positions`, `/api/v1/pnl/attribution`, `/api/v1/risk/rejects`, `/api/v1/gate/paper`

全部与 v3 结构一致, 字段无变化. **PASS**

---

## 全量字段审计 (python 逐 key 比对, 2026-05-29)

| 面板 | 字段数 | 结果 |
|---|---|---|
| renderTopBar / /status                           | 7  | 全 OK |
| renderHalfBook / /api/v1/book token0/token1      | 11 | 全 OK |
| renderDualBookBlock / cross_spread/as_of_ts_ns   | 4  | 全 OK |
| renderMarketInfoBlock / /api/v1/market           | 13 | 全 OK |
| renderQuoteSection / /api/v1/quote               | 7  | 全 OK |
| renderScoreBlock / /api/v1/score                 | 10 | 全 OK |
| renderPosBlock / /api/v1/positions               | 7  | 全 OK |
| renderPnlAttribution / /api/v1/pnl/attribution   | 8  | 全 OK |
| renderRejectBadge / /api/v1/risk/rejects         | 5  | 全 OK |

**总计 72 字段映射, 全部命中 wire key. 零 MISSING.**

---

## JS 语法检查

```
node --check frontend/src/api.js     -> OK
node --check frontend/src/stub.js    -> OK
node --check frontend/src/panels.js  -> OK
node --check frontend/src/app.js     -> OK
```

---

## 小尤 P0 修复确认

| P0 项 | 修复内容 | 文件 |
|---|---|---|
| P0-01 配色纪律 | 删 `--blue`/`--orange` 语义变量; PAPER→灰; DEMO横幅→黄; score-pre→灰; wf-net→黄; 控件蓝改名 `--ctrl-blue` | style.css |
| P0-02 demo fail-safe | `s.data_source !== 'live'` (非 live 均 demo); 顶部条 + isDemoData 传链 | panels.js/app.js |
| P0-03 错误可读 | `fetchErrorMap` 追踪; `isEndpointFailing()` API; `fail-chip`/`api-err-chip`; safeGet catch 不吞错误 | api.js/panels.js/app.js |

## 小宫 P2-03 修复确认

sparkline 标注: `windowLabel` (近 1h) + `firstTs – lastTs` (起止时间戳), 在 spark-header 中展示. 文件: panels.js `renderPnlSparkline`.

---

## v4 变更文件清单

- `frontend/index.html` — DEMO横幅改黄系; 净PnL移到顶部条第二位(P1-01); 新增 `top-api-err` slot; 标题升 v4
- `frontend/src/api.js` — 新增 `fetchErrorMap`/`isEndpointFailing`/`anyEndpointFailing`; `recordError` 内嵌 apiFetch; 新增 `fmtUptime`; apiFetch 区分 404/found:false
- `frontend/src/stub.js` — STUB_BOOK 改为 BinaryMarketBookView 双边结构; STUB_MARKET 补 condition_id/tokens[]/slug/polymarket_url/neg_risk_market_id
- `frontend/src/panels.js` — 全面重写: 中文映射表集中管理; 双边订单簿 (renderDualBookBlock/renderHalfBook); 盘口元信息全量; 量化决策区补 signal/conf; P0-01/02/03/P2-03 修复; 超链接; 拒单中文化
- `frontend/src/style.css` — P0-01 配色纪律全面修复; 新增双边 book/深度/vig/元信息/fail 样式; 卡片宽度扩至 minmax(400px,1fr)

---

## 遗留事项 (下一 sprint)

- 浏览器截图验证: 留给小宫 #48 dogfood 轮次
- P1-02: market_id 可读性 (formatMarketId 格式化函数)
- P1-03: 持仓行布局 (已优化为 avg→mk 对比显示)
- market info list endpoint (`/api/v1/markets`) 到位后, 市场集合改从 list API 拉
- P2-01/P2-02/P2-04/P2-05/P2-06: 下一 sprint backlog
