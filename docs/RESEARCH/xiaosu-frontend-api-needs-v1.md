# 前端观测 UI — 后端 API 需求清单 v1

- Owner: 小苏 (frontend-engineer, E 单元)
- Last review: 2026-05-29
- Status: DRAFT — 后端 API 设计讨论输入 (代表前端消费方视角)
- 关联:
  - `laozhou-w8-debug-rest-api-spec-v1.md` (现有 12 endpoint SSOT, 别重造)
  - `xiaosu-w10-w1-frontend-ui-v1-spec.md` (8 看板 UI spec)
  - `laoqian-w8-w5-profitability-kr-v1.md` (O2 盈利可证 KR)
  - ADR-027 (核心数据结构 SSOT) / R-20 (4 时间戳契约)
- 边界: 我只列**前端需要后端给什么**, 不写 endpoint 实现 (派给小卢/老周), 不定权限模型 (派给老沈)

老板指令: "后端 API 展开, 主要用于观测/开发/调试, 可获取信息尽可能详细"。本文从消费方把"尽可能详细"翻译成具体字段 + 实时性需求。

---

## §0 现状对齐 (已有, 不重造)

现有 12 endpoint 全是 **REST 拉取** (polling), 无 WSS 推送:
`/healthz /version /status /signals/active /signals/history /positions /orderbook/{id} /risk/rejects /audit/recent /metrics /drain /resume`

前端现用 TanStack Query polling (2-10s)。下面分三块说**缺口**: ① 8 panel 的字段/频率, ② 详细度增量, ③ 实时性 (哪些必须 WSS)。

---

## §1 观测看板需要的 endpoint 清单 (8 panel)

| # | Panel | method+path | 用途 | 关键返回字段 | 刷新 | 传输 |
|---|---|---|---|---|---|---|
| 1 | 持仓 | GET `/positions` (有) | 当前持仓全量 | token_id/condition_id/outcome/pos_yes/pos_no/avg_entry_price/pnl_unrealized/pnl_realized/mark_price/4-ts | 3s | REST |
| 2 | 净 PnL 曲线 | **GET `/pnl/timeseries?window=1d&bucket=1m`** (缺) | O2 盈利可证, PnL 趋势曲线 | bucket_ts[]/cum_net_pnl[]/realized[]/unrealized[]/fee_cum[]/gas_cum[]/n_trades_cum[] | 10s | REST |
| 3 | 盈亏分解 | **GET `/pnl/attribution?window=1d`** (缺) | gross→net 瀑布图 (盈利可证扣项) | gross_pnl/fee/gas/slippage/spread_cost/net_pnl + per_market[]{token_id,net_pnl,n_trades,hit_rate} | 10s | REST |
| 4 | 挂单+撮合 | **GET `/orders/open` + WSS `fills`** (缺) | 在挂单 + 实时成交流 | open: order_id/token_id/side/price/size/filled_size/status; fill: order_id/fill_price/fill_size/fee/maker_or_taker/exec_ts/4-ts | open 2s / fill 推送 | REST+**WSS** |
| 5 | RM 拒单事件流 | GET `/risk/rejects` (有) + **WSS `risk_events`** (增推送) | 拒单分布 + 实时拒单告警 | seq/reject_code/sub_reason/token_id/side/intended_price/intended_size/rm_step/threshold_value/observed_value/4-ts; 分布 reject_code_distribution | 列表 5s / 事件推送 | REST+**WSS** |
| 6 | 信号 net edge vs fill | **GET `/signals/edge_vs_fill?limit=100`** (缺) | 信号预期 edge 对比实际成交 (alpha 验证) | signal_id/token_id/fair_value/expected_edge_bps/intended_price/actual_fill_price/realized_edge_bps/slippage_bps/rm_verdict/4-ts | 5s | REST |
| 7 | 门禁仪表 | **GET `/gates/status`** (缺) | O2 G3/G4/G5 gate 实时达标度 | per_gate[]{gate_id,metric,current_value,threshold,pass,window,n_trades}; hit_rate/oos_sharpe/mdd/cum_net_pnl/t_test_p | 30s | REST |
| 8 | 系统健康 | GET `/status`+`/healthz`+`/metrics` (有) | 进程/线程/WSS/延迟健康 | state/mode/wss_connected{sports_api,clob,user_channel,sports_ws}/thread_heartbeats/uptime/loop_lag_p99/queue_depths | 2s + 健康推送 | REST+**WSS** |

> 备注 panel 8 的 `loop_lag_p99` / `queue_depths`: R-12 红线相关, 前端要能一眼看出热路径是否被拖慢 (event loop > 100us 即 P0)。

### 调试视图 (开发/调试用, 详细度优先)

| 视图 | method+path | 用途 | 关键返回字段 | 传输 |
|---|---|---|---|---|
| D1 单笔决策全链 trace | **GET `/trace/decision/{audit_id}`** (缺) | 一个 audit_id 回放从行情→信号→RM→撮合全链 | 按时序的 step[]{stage(INGEST/SIGNAL/RM/ORDER/FILL), inputs, outputs, verdict, latency_us, 4-ts}; 含 orderbook 快照 + fair_value 计算中间量 + RM 每步判定 | REST |
| D2 单 market 实时状态 | **GET `/market/{token_id}/state`** + **WSS `market:{token_id}`** (缺) | 一个 market 的 book/信号/持仓/挂单聚合视图 | orderbook(全档)/mid/spread/microstructure(imbalance,microprice)/active_signal/position/open_orders/last_fill/4-ts | REST+**WSS** |
| D3 信号→RM→撮合逐步快照 | **GET `/trace/pipeline?token_id=&limit=20`** (缺) | 按 market 看最近 N 次 pipeline 流转快照 | snapshot[]{signal_in, rm_verdict + 每步 threshold, order_out, fill_result, 各 stage 4-ts + latency} | REST |

---

## §2 "信息尽可能详细" — 希望暴露的底层细节

老板要"尽可能详细", 前端能用到的底层增量 (现有 endpoint 普遍偏摘要, 调试时不够):

1. **OrderBook 各档全量**: 现 `/orderbook` 限 10 档; 调试视图 (D2) 希望可选 `?depth=full` 返回全部价档 + 每档 `order_count` (微观结构挂单分布)。
2. **微观结构指标**: `imbalance` / `microprice` / `weighted_mid` / `book_pressure` (小袁 microstructure-v1 已算, 前端只需读出), 用于解释信号为何触发。
3. **RM 每步判定明细**: 现 `/risk/rejects` 只给 reject_code; 调试要 `rm_step` (哪一步拒)、`threshold_value` vs `observed_value`、以及**通过的单**也留判定链 (不只拒单), 即 RM verdict trace, 否则只能看到失败看不到为何放行。
4. **audit record 完整字段**: audit_id / event_type / 完整 payload (price/size/side/token_id/condition_id) / chain prev_hash (blake3 链可验) / 全 4-ts。前端要能 deep-link `?audit_id=` 直跳 trace。
5. **4 时间戳全暴露** (R-20): 每个数据点带 `event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts`, 前端渲染端到端延迟瀑布 (跨洋链路诊断刚需), 并校验单调性 (违反即标红 = 上游时间戳 bug)。
6. **fee/gas/slippage 拆分**: PnL 必须 gross/net 拆开 (O2 "扣除 gas+fee+slippage+spread cost"), 否则盈利可证看板算不出 net edge。
7. **vendor 标注** (ADR-027 vendor-agnostic): 行情/信号数据点带 `source_vendor` (polymarket/goalserve) + `data_source_ts`, 前端可按 vendor 分诊延迟。

---

## §3 实时性需求 (WSS 推送 vs REST 拉取)

**必须 WSS 推送** (低延迟观测, polling 会漏事件 / 延迟不可接受):

| 流 | WSS topic | 理由 |
|---|---|---|
| 成交流 (fills) | `fills` | 成交是离散事件, 5s polling 会漏单、PnL 跳变看不到瞬时 |
| RM 拒单事件 | `risk_events` | 拒单是告警级事件, 操盘需即时感知 (尤其连续拒单 = 策略异常) |
| 系统状态变更 | `system_state` | RUNNING→DRAIN→HALTED 必须秒级推送 (M1-G01 一键 halt < 1s 生效要可视确认) |
| 单 market 聚合 (调试) | `market:{token_id}` | 调试时盯盘需 book/信号实时跳动, polling 1s 仍卡顿 |
| 健康/loop_lag 告警 | `health_alert` | R-12 热路径超阈 (>100us) 是 P0, 必须推送不能等 polling |

> WSS 设计约束 (前端侧诉求): 单连接多 topic 订阅 (`{"sub":["fills","risk_events"]}`); 每条消息带 `seq` + `as_of_ts` 供前端去重/补洞; 断线重连后给 `snapshot` 再续增量 (避免漏事件)。后端实现需遵守 R-12 (推送在独立线程, 不阻塞 vCPU0/1/2) — 这是后端约束, 派给老周/小卢确认。

**REST 拉取够用** (状态快照, 容忍秒级陈旧):

- `/positions` `/pnl/timeseries` `/pnl/attribution` `/signals/history` `/signals/edge_vs_fill` `/gates/status` `/orderbook/{id}` `/metrics` `/trace/*` `/market/{id}/state` (初始快照)
- 这些是"当前状态/历史聚合", 2-30s polling 满足看板需求, 不必上 WSS (省跨洋带宽)。

---

## §4 边界声明

- 本文是**消费方需求**, 不是 endpoint spec。具体 path/schema/序列化由老周 + 小卢定 (REST API SSOT)。
- WSS 推送的线程模型 / R-12 合规由老周/小冯定 (后端约束)。
- 操盘写接口 (`/drain` `/resume` 及未来下单) 的权限/auth/audit 由老沈 review (权限模型不在我 scope)。
- 缺口 endpoint (标"缺"的 6 个 GET + 3 调试 + 5 WSS topic) 是 O2 盈利可证 + 调试详细度的前端硬需求, 建议进后端 API 讨论排期。

---

最后更新: 2026-05-29 by 小苏
下次 review: 后端 API 讨论收口后, 据老周/小卢反馈对齐缺口排期
