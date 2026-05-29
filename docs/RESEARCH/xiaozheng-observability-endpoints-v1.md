# 后端 API — 可观测性维度 endpoint + instrumentation 清单 (v1)

owner: 小郑 (A 系统工程部, 可观测性)
last_review: 2026-05-29
视角: 仅可观测性 (metrics / trace / 门禁仪表 / 事件流 / log / R-20)。不写代码, 不碰 collection 部署 (派给小肖 SRE)。
关联: xiaolu-w9-rest-api-skeleton (debug_api 6 endpoint), laohan-riskmanager-design-v0.2 (RejectEvent ring + audit WAL), laoye-w10-w1-g3-kr-financial-review (PAPER gate 条件), CLAUDE.md §8 R-20。

热路径纪律: 所有埋点热路径只做 lazy 计数 (atomic counter / SPSC ring append, wait-free); 聚合/导出全在旁路线程 (复用 debug_api HttpServer)。绝不在 WSS event loop 做 string 格式化或锁 (ADR R-12)。

## 1. metrics endpoint (Prometheus 文本格式)
`GET /metrics` (旁路线程拉取 atomic 快照渲染, 非热路径)。低基数 label 铁律: 不用 market_id/intent_id 当 label (高基数爆炸), 只用 `{source, sport, market_type, mode}` 这类有界枚举。

- 系统健康: `stcpp_uptime_seconds`; `stcpp_wss_connected{source}` (gauge 0/1); `stcpp_wss_reconnect_total{source}` (counter); `stcpp_rest_rtt_seconds{source,endpoint}` (histogram, 跨洋链路关键); `stcpp_event_loop_latency_seconds{loop}` (histogram, 暴露 p99/p999); `stcpp_audit_wal_ring_fill_ratio` (gauge, 背压预警)。
- 业务: `stcpp_rm_decision_total{decision,reason_code}` (拒单率分子分母); `stcpp_fill_total / stcpp_order_total{market_type}` (fill rate); `stcpp_net_edge_bps{sport}` (histogram, 净 edge 分布); `stcpp_pnl_usd{mode}` / `stcpp_exposure_usd` / `stcpp_bankroll_usd` (gauge)。
- 数据质量: `stcpp_data_staleness_seconds{source,market_type}` (gauge, now − event_ts); `stcpp_data_gap_total{source}` (counter, 序号断裂); `stcpp_price_drift_bps{market_id_bucketed}` (Goalserve vs PM 漂移, bucket 化避高基数)。

## 2. tracing endpoint (单笔决策全链)
`GET /trace/{intent_id}` 返回单笔 decision 的 span 树, 调试用 (非热路径; 从 audit WAL + trace ring 重建)。Span 链: `signal.emit → rm.evaluate → signer.sign → exec.match`。每 span 含 start/end ns + attributes (reason_code / edge_bps / nonce)。trace_id = audit_id (ULID), W3C traceparent 跨 source 透传。`GET /trace/recent?n=50` 列最近 trace 摘要。OTel OTLP 导出由 SRE 部署 (派给小肖 + 小石)。

## 3. GM-PAPER-G 门禁实时仪表
`GET /gate/paper` 实时输出 30 日滚动窗指标, 让 GM/老钱 随时看放行状态 (复用 PositionLedger RCU 快照计算):
```
{ "window_days":30, "n_trades":N, "positive_day_ratio":0.xx,
  "sharpe_30d":0.xx, "sharpe_se":0.xx, "p_value":0.xx,
  "hit_rate":0.xx, "mdd":0.xx, "net_pnl_usd":N,
  "prelim_pass":bool, "confirm_pass":bool, "as_of_ts":... }
```
对齐 laoye Option C 两阶段 (Prelim 14d / Confirm 30d)。Prometheus 同步暴露 `stcpp_gate_sharpe_30d` 等便于 Grafana 阈值告警。

## 4. 审计/事件流 (WSS 推送)
`GET /risk/rejects?n=512` (已有, RejectEvent ring tail copy) + 新增 `WS /stream/events` 实时推送: RM 拒单事件 (intent_id/reason_code/bankroll 快照)、STATE_TRANSITION、告警 (reconnect 风暴 / staleness 超阈 / WAL 背压)。事件 schema 与 audit WAL 字段对齐 (与审计专家老沈共同定 schema, 不单方改)。

## 5. 结构化 log 查询 (开发调试)
`GET /logs?level=&module=&trace_id=&since=&until=&limit=` 检索 jsonl 结构化日志 (按日切, 走索引非全扫)。`trace_id` 关联 §2 trace 与 §4 事件, 一次定位全链。生产只读, 不暴露敏感字段 (私钥/明文凭证, 红线)。

## 6. 4 时间戳 (R-20) 可观测
每条数据响应/事件必带 4 ts (event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts)。`GET /data/latency/{market_id}` 拆解各段延迟 (source→ingest / ingest→decision), 排查跨洋链路瓶颈。Prometheus: `stcpp_ts_lag_seconds{stage,source}` (event→source / source→ingest / ingest→asof 三段 histogram)。任一段倒挂 = 数据契约违例告警。
