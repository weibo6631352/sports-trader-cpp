# INTEGRATION-VERIFY.md — 前端看板字段契约对齐验证记录

owner: 小苏 (#12, E单元)
last_review: 2026-05-29
关联ADR: ADR-038 §3 schema 铁律
SSOT: `src/stcpp/debug_api/endpoint_*.cpp` + `include/stcpp/debug_api/state_provider.hpp`

---

## 验证方法

1. 启动 `build/src/stcpp/debug_api/stcpp_debug_server --port 8080`
2. `curl -s http://127.0.0.1:8080/<endpoint> | python3 -m json.tool` 记录真实 wire JSON
3. grep 比对 `frontend/src/panels.js` 中 `data.xxx` / `p.xxx` / `r.xxx` 与真实 key
4. `node --check frontend/src/*.js` JS 语法验证
5. 检查 `stub.js` 与真实输出结构一致

---

## 修复工单 (老胡 P0,共 9 项)

### 面板 1: 持仓 `/api/v1/positions`

**真实 wire 字段 (已 curl 确认):**
```json
{
  "mode": "paper",
  "as_of_ts": 1780042160152850000,
  "positions": [
    {
      "market_id": "nba-lal-bos-ml",
      "outcome": "LAL",
      "net_qty": 1500,
      "avg_entry_price": 0.62,
      "mark_price": 0.65,
      "pnl_realized": 45,
      "pnl_unrealized": 45,
      "as_of_ts": 1780042100152847000
    }
  ]
}
```

| 字段 | 后端 wire key | 修复前 (错) | 修复后 (正) | panels.js 行 | stub.js 同步 |
|---|---|---|---|---|---|
| 数量 | `net_qty` | `p.size` | `p.net_qty` | 95 | DONE |
| 均价 | `avg_entry_price` | `p.avg_fill_price` | `p.avg_entry_price` | 90,96 | DONE |
| 行时间戳 | `as_of_ts` (行内) | `p.ingestion_ts` | `p.as_of_ts` | 102 | DONE |
| 表头 | — | `ingestion_ts` | `as_of_ts` | 119 | — |

差价计算 `markVsAvg = mark_price - avg_entry_price` 同步修正 (line 90)。

**验证结果: PASS** — panels.js 中 `p.net_qty`, `p.avg_entry_price`, `p.as_of_ts` 均可在 curl JSON 中找到对应 key。

---

### 面板 2: PnL 曲线 `/api/v1/pnl/timeseries`

**真实 wire 字段:** `mode`, `window_sec`, `bucket_sec`, `as_of_ts`, `buckets[]` (每桶: `bucket_start_ts`, `cum_net_pnl`, `realized`, `unrealized`, `fee`, `gas`, `n_trades`)

**改动:** 无需改动，原有读法已与 wire 一致。

**验证结果: PASS** — panels.js `renderPnlCurve` 读 `b.bucket_start_ts`, `b.cum_net_pnl`, `b.n_trades` 全部命中。

---

### 面板 3: PnL 归因瀑布 `/api/v1/pnl/attribution`

**真实 wire 字段:**
```json
{
  "mode": "paper",
  "as_of_ts": 1780042160518701000,
  "waterfall": {
    "gross": 312.5,
    "fee": -18.3,
    "gas": -2.1,
    "slippage": -9.7,
    "spread": 24.6,
    "net": 307
  },
  "per_market": [
    { "market_id": "nba-lal-bos-ml", "net_pnl": 90 }
  ]
}
```

| 字段 | 后端 wire | 修复前 (错) | 修复后 (正) |
|---|---|---|---|
| waterfall 结构 | object `{gross,fee,gas,slippage,spread,net}` | array of `{label,value}`, 读 `wf[0].value` | `const wfObj = data.waterfall; wfObj.gross` 等 |
| 渲染顺序 | 固定: gross→fee→gas→slippage→spread→net | 跟随 array | `WF_ORDER` 常量强制顺序 |
| net 行标识 | key === 'net' | item.label === 'net_pnl' | `isNet = key === 'net'` |
| per_market | `[{market_id, net_pnl}]` | 已正确 | 保持 |

**验证结果: PASS** — 结构性重写 `renderPnlAttribution`,所有 `wfObj.gross/fee/gas/slippage/spread/net` 均命中 wire object key。stub.js waterfall 从 array 改为 object。

---

### 面板 4: RM 拒单 `/api/v1/risk/rejects`

**真实 wire 字段:**
```json
{
  "rejects": [
    {
      "reason_code": "MAX_POSITION_EXCEEDED",
      "market_id": "nba-lal-bos-ml",
      "intent_ref": "intent-7f3a",
      "side": "BUY",
      "size": 500,
      "price": 0.41,
      "rejected_ts": 1780042163776408000
    }
  ]
}
```

| 字段 | 后端 wire key | 修复前 (错) | 修复后 (正) | panels.js 行 | stub.js 同步 |
|---|---|---|---|---|---|
| 拒单 ID | `intent_ref` | `r.reject_id` | `r.intent_ref` | 279 | DONE |
| 时间戳 | `rejected_ts` (单一) | `r.event_ts` + `r.ingestion_ts` 两列 | `r.rejected_ts` 单列 | 285 | DONE |
| 表头 | — | `Reject ID / event_ts / ingestion_ts` | `Intent Ref / rejected_ts` | 298,301 | — |
| side/size/price | 有 | 已有 | 保留 | 280-284 | DONE |

**验证结果: PASS** — `r.intent_ref`, `r.rejected_ts` 均在 wire JSON 中。

---

### 面板 5: GM-PAPER-G 门禁 `/api/v1/gate/paper`

**真实 wire 字段:** `positive_day_ratio` (无 s)

| 字段 | 后端 wire key | 修复前 (错) | 修复后 (正) | panels.js 行 | stub.js 同步 |
|---|---|---|---|---|---|
| 盈利日占比 | `positive_day_ratio` | `data.positive_days_ratio` | `data.positive_day_ratio` | 316 | DONE |
| has_data | `has_data` | (无) | stub 补字段 | — | DONE |

**验证结果: PASS** — `data.positive_day_ratio` 命中 wire key。

---

### 面板 6: 订单簿 `/api/v1/book/<id>`

**真实 wire 字段 (平铺,非嵌套):** `event_ts`, `data_source_ts`, `ingestion_ts`, `book_as_of_ts`, `condition_id` (别名 market_id), `bids[]`, `asks[]`

| 字段 | 后端 wire | 修复前 | 修复后 | panels.js 行 | stub.js 同步 |
|---|---|---|---|---|---|
| condition_id | `condition_id` (backend 已补别名) | `condition_id` | 保持 | 388 | 保持 |
| book 时间戳 (4个) | `event_ts`, `data_source_ts`, `ingestion_ts`, `book_as_of_ts` 平铺 | 只显示 `event_ts` + `ingestion_ts` | 全部4个平铺字段 | 413-416 | DONE (补 book_as_of_ts) |
| bids/asks | `[{price, size}]` | 已正确 | 保持 | 362-376 | 保持 |

**验证结果: PASS** — 4个时间戳字段全部为 wire 顶层平铺 key,已全部补入 footer。

---

## JS 语法检查

```
node --check frontend/src/panels.js  -> OK
node --check frontend/src/stub.js    -> OK
node --check frontend/src/api.js     -> OK
node --check frontend/src/app.js     -> OK
```

---

## 变更文件清单

- `frontend/src/panels.js` — 9项 divergence 修复 (持仓3项 + waterfall结构重写 + 门禁1项 + 拒单3项 + 订单簿时间戳补全)
- `frontend/src/stub.js` — 与 panels.js 同步对齐 (positions/waterfall/rejects/gate/book 全部)
- `frontend/INTEGRATION-VERIFY.md` — 本文件

**未改动:** `frontend/src/api.js`, `frontend/src/app.js`, 所有 C++/后端文件

---

## 遗留事项

- 浏览器截图验证: 需要有浏览器 + chrome-devtools-mcp 支持，本次 curl+静态检查范围内无法覆盖，留给下次 dogfood 轮次 (小宫 #48 负责)
- `?stub=1` 模式验证: stub 结构已与真实 wire 对齐，但端到端 stub 模式下浏览器渲染需 dogfood 确认
