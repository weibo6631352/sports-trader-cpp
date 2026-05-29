---
owner: 小宋 (test-replay, E 部产品业务保障部)
last_review: 2026-05-30
---

# 小宋测试反馈 — Live 系统实测报告

测试时间: 2026-05-30  
后端: http://127.0.0.1:8080 (paper mode, build 537d484, uptime ~4.5h)  
测试方法: curl 全端点 + 读前端源码 + 截图分析 + 数值验证

---

## 问题清单

### BUG-1: /metrics stcpp_uptime_seconds 恒为 0 (P1)

**现象:** `/metrics` 中 `stcpp_uptime_seconds{mode="paper"} 0`，但 `/status` 和 `/healthz` 均返回 `uptime_sec: 16292`（真实运行时长）。Prometheus 抓取到的 uptime 指标完全失效。

**复现:**
```
curl http://127.0.0.1:8080/metrics | grep uptime       # => 0
curl http://127.0.0.1:8080/status | jq .uptime_sec     # => 16292
```

**严重度:** P1 — 监控/告警依赖 uptime 指标判断重启，当前完全不可信。

**建议:** 后端维护 `start_time_ns` 静态变量，每次 metrics 请求时计算 `now - start_time_ns` 再写 gauge。

---

### BUG-2: /metrics stcpp_data_staleness_ms_max 恒为 0，数据实际已陈旧 2.4 小时 (P1)

**现象:** `stcpp_data_staleness_ms_max{mode="paper"} 0`，但 `/api/v1/book_pair` 中 `token0.event_ts = 1780088403554000000`，当前时刻 `event_ts` 已陈旧 ~8655 秒（2.4 小时）。WSS 三路全部断开 (`wss_connected` 全 false)，系统运行在完全过期的快照数据上，但 metrics 显示无异常。

**复现:**
```
curl http://127.0.0.1:8080/metrics | grep staleness    # => 0
curl http://127.0.0.1:8080/api/v1/book_pair/<cid> | jq .token0.event_ts
# python3 -c "import time; print((time.time()*1e9 - 1780088403554000000)/1e9, 's stale')"
# => 8655s
```

**严重度:** P1 — 核心可观测性缺口，运维无法通过 metrics 感知数据失联。

**建议:** staleness_ms_max 应 = `now_ns - min(token_event_ts across all cached books)`，在 metrics 收集时遍历 book cache。

---

### BUG-3: /metrics stcpp_rm_reject_total 与 /status rm_rejects_last_60s 均为 0，但 /api/v1/risk/rejects 存在 256 条拒单记录 (P1)

**现象:** `/api/v1/risk/rejects` 返回 256 条真实拒单，时间戳跨度约 32 秒，最新拒单时间距离本次轮询约 29 秒（在 60 秒窗口内）。但 `/status` 的 `rm_rejects_last_60s=0`，`/metrics` 的 `stcpp_rm_reject_total=0`，`stcpp_rm_decision_total=0`。两路指标全部失联。

**复现:**
```
curl http://127.0.0.1:8080/api/v1/risk/rejects | jq '.rejects | length'  # => 256
curl http://127.0.0.1:8080/status | jq .rm_rejects_last_60s              # => 0
curl http://127.0.0.1:8080/metrics | grep rm_reject                       # => 0
```

**严重度:** P1 — RiskManager 的拒单路径未接入 metrics/status 计数器，风控可观测性完全失效。实盘若发生大量拒单，监控不会告警。

**建议:** 确认 INVALID_INTENT 拒单的生成路径是否 bypass 了正常的 RM decision 计数器；如果这是 signal_engine 层的 pre-RM 过滤，需要单独的计数器 `stcpp_pre_rm_reject_total`。

---

### BUG-4: /api/v1/risk/rejects 全部记录精确重复 2 次 (P1)

**现象:** 256 条拒单中，128 个唯一 (market_id, intent_ref, side, price, rejected_ts) 元组，每个恰好出现 2 次——即每笔拒单被写入两次，连 `rejected_ts` 都完全一致（纳秒级时间戳碰撞概率极低，可排除）。影响两个市场：Brazil (0x30d5...) 和 England (0x375...)。

**复现:**
```python
rejects = [...json...]
tuples = [(r['market_id'],r['intent_ref'],r['side'],r['price'],r['rejected_ts']) for r in rejects]
Counter(tuples).most_common(3)  # => all (key, 2)
```

**严重度:** P1 — 双写 bug，可能导致下游消费者（风控统计、PnL 归因）double-count。原因可能是 reject 事件被 publish 到了两个订阅者，或 append 逻辑调用了两次。

**建议:** 在 reject log append 处加 mutex + 去重检查；单元测试覆盖单次 reject 只产生一条记录。

---

### BUG-5: advisory=True + model_calibrated=False 的市场仍触发 INVALID_INTENT 拒单 (P1)

**现象:** Brazil 和 England 两个 outright 市场，`/api/v1/quote` 明确返回 `advisory: true`（API 语义：仅供参考，不下单）且 `model_calibrated: false`，但系统仍持续生成 BUY 意图（被 RM 以 INVALID_INTENT 拒绝）。`signal_strength=1` 说明 signal_engine 已激活这两个市场。

**复现:**
```
curl .../quote/0x30d5... | jq '{advisory, model_calibrated, edge_bps, signal_strength}'
# => {advisory: true, model_calibrated: false, edge_bps: 1692, signal_strength: 1}
curl .../risk/rejects | jq '[.rejects[] | select(.market_id == "0x30d5...")] | length'
# => 128
```

**严重度:** P1 — 红线违规候选。advisory 标志的语义是"不下单"，但 signal_engine 在 advisory=true 时仍生成 intent，依赖 RM 作为最后防线。若 RM 规则变化或被 bypass，将直接出单。应在 signal_engine 层就拦截 advisory 市场。

**建议:** signal_engine 在生成 intent 前检查 `quote.advisory`；advisory=true 直接 skip，不产生 intent，不消耗 RM 决策配额。

---

### BUG-6: /api/v1/market/{condition_id} 对所有已知 condition_id 返回 HTTP 404 (P1)

**现象:** `/api/v1/events` 返回 10 个事件，均带有真实 condition_id。对其中任意一个请求 `/api/v1/market/{cid}` 均返回 HTTP 404（body 包含 `found: false`）。book_pair / quote / book/token 对同一 cid 均返回有效数据。

**复现:**
```
CID=$(curl .../api/v1/events | jq -r '.events[0].condition_ids[0]')
curl -o /dev/null -w "%{http_code}" .../api/v1/market/$CID  # => 404
curl -o /dev/null -w "%{http_code}" .../api/v1/book_pair/$CID  # => 200 ✓
```

**严重度:** P1 — 前端 `store.ts` 在 `refreshMarketGrid` 中调用 `fetchMarket(condId)`，404 导致 `apiFetch` 返回 null，`market` 字段在整个前端生命周期内保持 null。影响：
  - `ConditionColumn` 中 `accepting_orders` 判断失效（永远不 inactive）
  - `inferMarketLabel` 退化为 condition_id 尾缀猜测
  - `event_id` 字段无法从 market 获取，score 缓存写回失效
  - `refreshMarketInfoSlow`（60s 周期）每次轮询都 404，属于无效 RPC 浪费

**建议:** 后端 market store 需要加载 Polymarket gamma/CLOB 市场元数据；当前 market endpoint 可能只有 book store 而无 market metadata store 初始化。

---

### BUG-7: WSS wss_state="CONNECTED" 显示矛盾——实为断线 2.4 小时的陈旧状态 (P1)

**现象:** `/status` 明确显示三路 WSS 全部 `false`（sports_api / clob / user_channel），但 `/api/v1/book_pair` 中每个 token 的 `wss_state` 字段均为 `"CONNECTED"`。前端 `MiniHalfBook` 根据 `wss_state` 渲染绿色 WssDot，与实际断线状态相悖。

**严重度:** P1 — 前端向用户呈现"已连接"，实为 2.4 小时前的快照数据，完全误导。

**建议:** `wss_state` 字段应在每次 book 响应时从当前连接状态动态注入，而非从 book snapshot 中读取初始值。

---

### BUG-8: R-20 四时间戳约束违反——ingestion_ts < data_source_ts (P1)

**现象:** 在 2 个市场（Hurricanes 和 Spain）中观察到 `data_source_ts > ingestion_ts`，差值约 -10~21ms（ingestion 早于 data_source）。CLAUDE.md 红线 R-20 明确要求 `event_ts <= data_source_ts <= ingestion_ts <= as_of_ts`。

**数据:**
```
Hurricanes: data_source_ts=1780088403554000000, ingestion_ts=1780088403543449000 (diff=-10.55ms)
Spain:      data_source_ts=1780089249571000000, ingestion_ts=1780089249549879000 (diff=-21.12ms)
```

**严重度:** P1 — R-20 是红线，违反即 P0。疑似 book snapshot 初始加载时，ingestion_ts 记录了 REST 拉取完成时间，而 data_source_ts 使用了 Polymarket 消息体内的时间戳；两者时钟来源不同，或消息体 ts 精度不一致。

**建议:** 如果 `data_source_ts` 来自 Polymarket payload，而 `ingestion_ts` 来自本地 `steady_clock`，需确认 Polymarket 服务端时钟与本地时钟偏差；或将 `ingestion_ts = max(ingestion_ts, data_source_ts)` 作为 floor 保护。

---

### BUG-9: 前端 DualBook 故障检测路径拼写错误 (P2)

**现象:** `EventGrid.tsx` 第 447 行检查 `isEndpointFailing('/api/v1/book/${conditionId}')` 来决定是否渲染"订单簿拉取失败" chip，但实际 fetch 路径是 `/api/v1/book_pair/${conditionId}`（见 `api.ts` 第 149 行）。路径不匹配导致：
  - `fetchErrorMap` 中的 key 是 `/api/v1/book_pair/...`
  - 查询时用的是 `/api/v1/book/...`
  - 始终 miss，"拉取失败"状态永不展示，用户看不到任何错误提示

**严重度:** P2 — 书单失败时 UI 静默，用户无法区分"数据真的空"和"fetch 出错"。

**建议:** 将第 447 行改为 `isEndpointFailing(\`/api/v1/book_pair/${props.conditionId}\`)`。

---

### BUG-10: 前端 StaleDot 使用 book_as_of_ts（请求时刻）而非 event_ts（数据时刻），持续显示绿色 (P2)

**现象:** `EventGrid.tsx` 第 120 行 `stalenessMs(c.book.as_of_ts_ns ?? c.book.token0?.book_as_of_ts)`。`as_of_ts_ns` 和 `book_as_of_ts` 在每次 API 请求时由后端注入当前时刻（约 1ms 前），而真实数据时刻是 `event_ts`（2.4 小时前）。结果：StaleDot 恒绿，永远不触发黄/红预警。

**严重度:** P2 — 与 BUG-7 联动：WSS 断线 + 数据过期但 UI 全程显示"实时正常"。实盘若 WSS 断线，运营人员无法从前端发现。

**建议:** 改为 `stalenessMs(c.book.token0?.event_ts)`，或后端在 book 响应中新增 `feed_staleness_ms` 字段供前端直接消费。

---

### BUG-11: /api/v1/book/token/{token_id} 返回的 condition_id 和 outcome 为空字符串 (P2)

**现象:**
```json
{"book": {"condition_id": "", "outcome": "", "market_id": "", ...}}
```
token0/token1 在 `/api/v1/book_pair` 中同样 `outcome: ""`，仅通过 token_id 可识别归属。前端 `MiniHalfBook` 中 `{h().outcome ?? '—'}` 因 outcome 为空串（truthy）不会显示 `—`，而是显示空白。

**严重度:** P2 — UI 中双边订单簿头部 outcome 标签为空，用户无法判断哪边是 YES/NO。

**建议:** 后端 book store 在加载时从 Polymarket gamma 拉取 token outcome 字段并关联存储；或至少在 `/api/v1/book_pair` 中从 events 反查。

---

### OBS-1: INVALID_INTENT 拒单原因未在前端 i18n 翻译表中定义 (P3)

**现象:** `i18n.ts` 的 `REJECT_REASON_ZH` 仅定义了三种 reason_code，不含 `INVALID_INTENT`。前端 RejectDot tooltip 回退显示原始英文字符串。

**严重度:** P3 — 可读性问题。

**建议:** 在 `REJECT_REASON_ZH` 中补充 `INVALID_INTENT: '意图格式非法'`，并补全其他可能的 reason_code（与后端 RiskManager 对齐）。

---

### OBS-2: stcpp_loop_latency_p99_us 恒为 0 (P3)

**现象:** `/metrics` 中热路径 p99 延迟指标始终为 0，无法判断 hot loop 真实负载。

**严重度:** P3 — 观测盲区。

**建议:** 确认 hot loop 的 timing instrumentation 是否已接入；或标注该指标在 paper mode 无 hot loop 时预期为 0，避免误解。

---

### OBS-3: /api/v1/gate/paper has_data=false 导致 store.ts refreshGate 不更新状态 (P3)

**现象:** `store.ts` 第 122-124 行：`if (data) setState({ gate: data })`，但 `apiFetch` 对 `has_data === false` 返回 null（第 122-125 行），导致 `gate` 状态永远不从 null 更新。前端 StatusBar 显示"Gate —"。此行为逻辑自洽（paper 无交易历史），但 gate null vs gate.has_data=false 在 UI 上无差异可能遮盖真正的 gate API 失联。

**严重度:** P3 — 建议 gate null（请求失败）与 gate.has_data=false（正常无数据）在 UI 上有视觉区分。

---

## 数据完整性验证（通过项）

- **book_pair 双边互补**: 所有 10 个市场验证通过。`bid_sum < 1`，`ask_sum > 1`，`mid_sum ≈ 1.000`。cross_spread 正确（0.001 或 0.01）。
- **imbalance 对称**: token0 和 token1 的 imbalance 数值相等、符号相反，全部 10 个市场验证通过。
- **HTTP 404 对非法 cid**: `/api/v1/book_pair/0xdeadbeef`、`/api/v1/quote/0xdeadbeef`、`/api/v1/score/0xdeadbeef` 均正确返回 HTTP 404。
- **score 404 for outright**: 所有测试的 event_id（outright 市场）均正确返回 HTTP 404 + `found: false`，符合预期（无 Goalserve 比分数据）。
- **pnl/timeseries 空桶**: paper 无交易，`buckets: []` 正确。
- **positions 为空**: paper 无成交，`positions: []` 正确，符合预期。
- **healthz 线程存活**: 5 个线程（ingest_reactor / signal_engine / risk_manager / paper_signer / api_server）全部 `"alive"`。
- **/healthz /status /version /metrics**: 全部返回 HTTP 200，结构正确。

---

## 给老胡/GM 的一句话结论

后端核心数据（book、quote、binary 互补性）基本正确，但有 **5 个 P1 级可观测性/风控 bug**：metrics 计数器全部失效（uptime/staleness/reject count 均为 0 虚报）、advisory 市场仍生成意图违反风控语义、market 元数据 endpoint 全线 404 导致前端永久 null、拒单双写计数翻倍；另有 2 个前端 UI P1（WSS 绿灯假阳性、StaleDot 不反映真实数据龄）。系统当前 **不具备实盘上线条件**，尤其是 BUG-5（advisory bypass）和 BUG-3/4（拒单指标失联+双写）需优先修复，再走 paper gate 验证。
