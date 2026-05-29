# 开发者/运维观测页规格 v1

owner: 小郑 (A 系统工程部, 可观测性)
last_review: 2026-05-29
关联: ADR-038, state_provider.hpp, endpoint_metrics.cpp, endpoint_status.cpp, endpoint_healthz.cpp,
      orderbook_snapshot_hub.hpp, polymarket_clob_subscriber.hpp, xiaozheng-observability-endpoints-v1.md

定位: 前端 v6 "开发者/运维观测页" 的内容规格。
面向人群: 后台开发者 + 运维人员 (不是策略操盘 UI, 不是 Grafana 历史趋势)。
职责分工: 本页只管当前态 + 近期异常。Grafana (Sprint-3 W11+ 小郑立项) 接手趋势/历史。

热路径纪律 (不可妥协, R-12):
  前端轮询观测页 endpoint 是旁路线程响应, 热路径零感知。
  所有观测数据通过 double-buffer snapshot / atomic counter 拿,
  不在 WSS event loop 做任何 JSON 序列化 / 锁 / 格式化。

---

## 1. 观测页 Widget 清单

每条格式: 指标描述 | 数据来源 (endpoint + 字段) | widget 类型 | 优先级

优先级定义: P0 = MVP 必上 / P1 = Sprint-1 结束前 / P2 = Sprint-2

---

### 1.1 订阅状态区块 (GM 点名: "订阅了多少市场/token")

| # | 指标 | 数据来源 endpoint + 字段 | widget 类型 | 优先级 |
|---|---|---|---|---|
| S-01 | 已订阅 token 数 (CLOB market channel) | `GET /metrics` → `stcpp_subscribed_tokens_total` (新增, 见 §2) | stat (大数字) | P0 |
| S-02 | 已订阅 condition_id 数 (推导自 token/2) | `GET /metrics` → `stcpp_subscribed_markets_total` (新增, 见 §2) | stat (大数字) | P0 |
| S-03 | 已订阅 user channel condition_id 数 | `GET /metrics` → `stcpp_subscribed_user_conditions_total` (新增, 见 §2) | stat | P1 |
| S-04 | WSS sports_api 连接状态 | `GET /status` → `wss_connected.sports_api` (bool) | status-grid (绿/红) | P0 |
| S-05 | WSS clob (market channel) 连接状态 | `GET /status` → `wss_connected.clob` (bool) | status-grid | P0 |
| S-06 | WSS user channel 连接状态 | `GET /status` → `wss_connected.user_channel` (bool) | status-grid | P0 |
| S-07 | WSS reconnect 累计次数 | `GET /metrics` → `stcpp_wss_reconnect_total{mode}` | stat (带告警阈: >5 变橙) | P0 |
| S-08 | 最后断连时刻 (epoch_ns) | `GET /metrics` → `stcpp_wss_last_disconnect_ts_ns` (新增, 见 §2) | stat (人类时间格式) | P1 |
| S-09 | hot token 订阅数 / cold token 订阅数 | `GET /metrics` → `stcpp_subscribed_tokens_total{tier="hot"}` / `{tier="cold"}` (新增, 见 §2) | stat pair | P2 |

备注 S-01/S-02: 这是老板明确点名的指标 ("订阅了多少市场/token"), 当前后端完全没有, 是最大缺口。
  hub_.token_count() 方法已在 OrderBookSnapshotHub 存在, 只差 MetricsSnapshot 不含该字段,
  /metrics endpoint 不输出该值。需小冯暴露计数 (见 §2 详细说明)。

---

### 1.2 系统健康区块

| # | 指标 | 数据来源 endpoint + 字段 | widget 类型 | 优先级 |
|---|---|---|---|---|
| H-01 | 进程 uptime | `GET /healthz` → `uptime_sec` 或 `GET /status` → `uptime_sec` | stat (秒→人类可读) | P0 |
| H-02 | 各线程心跳状态 (5 线程) | `GET /healthz` → `threads.ingest_reactor / signal_engine / risk_manager / paper_signer / api_server` (当前 stub="alive", W10+ 接真实 watchdog) | status-grid (5 格子, 绿/红) | P0 |
| H-03 | 整体 ok 标记 | `GET /healthz` → `ok` | status-grid (顶部红绿灯) | P0 |
| H-04 | event loop p99 延迟 (μs) | `GET /metrics` → `stcpp_loop_latency_p99_us{mode}` | stat (带告警阈: >200us 变橙, >500us 变红) | P0 |
| H-05 | 运行模式 | `GET /status` → `mode` ("paper" / "live" / "backtest") | stat (badge 颜色: paper=蓝, live=绿, backtest=灰) | P0 |
| H-06 | 系统状态 | `GET /status` → `state` ("RUNNING" / "DRAIN" / "HALTED") | status-grid (HALTED=红 P0 告警) | P0 |
| H-07 | 数据源标识 | `GET /status` → `data_source` ("demo" / "stub" / "real") | stat (badge) | P0 |
| H-08 | 单实例锁状态 | `GET /metrics` → `stcpp_instance_lock_held` (新增, 见 §2) | status-grid | P1 |

备注 H-02: 当前 /healthz threads 全 hardcode "alive" (stub)。W10+ 小卢接 watchdog atomic。
  观测页现在可展示但需标注 "(stub)" 角标, 避免误导。

---

### 1.3 数据质量区块

| # | 指标 | 数据来源 endpoint + 字段 | widget 类型 | 优先级 |
|---|---|---|---|---|
| Q-01 | feed staleness 最大值 (ms) | `GET /metrics` → `stcpp_data_staleness_ms_max{mode}` | stat (带告警: >500ms 橙, >2000ms 红) | P0 |
| Q-02 | 序列号 gap 累计数 | `GET /metrics` → `stcpp_feed_gap_total{mode}` | stat (>0 即橙) | P0 |
| Q-03 | 跨源价格漂移 (bps, Goalserve vs PM) | `GET /metrics` → `stcpp_price_drift_bps{mode}` | stat (带告警: >50bps 橙) | P0 |
| Q-04 | staleness 时序趋势 (近 5min) | 前端本地环形缓冲轮询 /metrics 拼时序 | timeseries (折线) | P1 |
| Q-05 | R-20 4 时间戳延迟瀑布 (任意市场) | `GET /api/v1/data/latency/{market_id}` (ADR-038 §4.4, 尚未实现) | timeseries (堆叠条: event→source / source→ingest / ingest→asof) | P2 |

备注 Q-04: 前端轮询间隔 5s, 本地保留最近 60 个采样点, 不需要后端新 endpoint。
备注 Q-05: /api/v1/data/latency endpoint 在 ADR-038 §4.4 已规划, 尚未实现, P2。

---

### 1.4 业务吞吐区块

| # | 指标 | 数据来源 endpoint + 字段 | widget 类型 | 优先级 |
|---|---|---|---|---|
| B-01 | RM 决策总数 | `GET /metrics` → `stcpp_rm_decision_total{mode}` | stat | P0 |
| B-02 | RM 拒单总数 + 拒单率 | `GET /metrics` → `stcpp_rm_reject_total{mode}` ; 拒单率 = reject/decision | stat (百分比) | P0 |
| B-03 | fill 总数 | `GET /metrics` → `stcpp_fill_total{mode}` | stat | P0 |
| B-04 | 净 PnL (USDC) | `GET /metrics` → `stcpp_cum_net_pnl{mode}` | stat (正=绿, 负=红) | P0 |
| B-05 | 净 edge (bps) | `GET /metrics` → `stcpp_net_edge_bps{mode}` | stat | P0 |
| B-06 | PnL 时序曲线 (近 1h) | `GET /api/v1/pnl/timeseries?window=3600&bucket=300` → `cum_net_pnl` 分桶序列 | timeseries (折线) | P0 |
| B-07 | PnL 归因瀑布 | `GET /api/v1/pnl/attribution` → gross/fee/gas/slippage/spread/net | timeseries (瀑布柱状) | P1 |
| B-08 | 持仓快照 | `GET /api/v1/positions` → per market/outcome net_qty + pnl | status-grid (表格行) | P1 |
| B-09 | GM-PAPER-G 门禁仪表 | `GET /api/v1/gate/paper` → n_trades/sharpe/p_value/prelim_pass/confirm_pass | status-grid (通行灯 + 关键数字) | P0 |

---

### 1.5 错误 / 拒单区块

| # | 指标 | 数据来源 endpoint + 字段 | widget 类型 | 优先级 |
|---|---|---|---|---|
| E-01 | 最近拒单列表 (最多 50 条) | `GET /api/v1/risk/rejects` → reason_code + market_id + side + size + rejected_ts_ns | log (表格, 时间倒序) | P0 |
| E-02 | reason_code 分布 (最近 500 条) | 前端在 /api/v1/risk/rejects 返回数据上做本地聚合 | timeseries (柱状, 按 reason_code 分色) | P1 |
| E-03 | WSS 错误/异常计数 | `GET /metrics` → `stcpp_wss_reconnect_total` (proxy 指标, 真实错误计数见 §2 新增) | stat | P0 |
| E-04 | feed gap 发生时刻 (最近 N 次) | `GET /metrics` → `stcpp_feed_gap_total` 变化 + `stcpp_feed_gap_last_ts_ns` (新增, 见 §2) | log (时间戳列表) | P1 |

---

## 2. 后端数据缺口分析

### 2.1 缺口总览

对照当前已实现:
- `GET /metrics`: MetricsSnapshot 11 个字段 (uptime/wss×3连接/reconnect/loop_p99/rm_decision/rm_reject/fill/net_edge/pnl/staleness/gap/drift)
- `GET /status`: state/mode/wss_connected×3/signals_active_count(stub0)/positions_count(stub0)/rm_rejects_last_60s(stub0)/uptime/as_of_ts/data_source
- `GET /healthz`: ok/threads×5(stub)/uptime/as_of_ts

缺口如下:

| 缺口 ID | 缺什么 | 影响 widget | 严重度 | 谁加 | 怎么加 |
|---|---|---|---|---|---|
| GAP-01 | `subscribed_tokens_total` — 订阅了多少 token | S-01 (GM 点名) | P0 | 小冯 (OrderBookSnapshotHub owner) + 小卢 (MetricsSnapshot + /metrics) | 见 §2.2 详细方案 |
| GAP-02 | `subscribed_markets_total` — 订阅了多少盘口 | S-02 (GM 点名) | P0 | 同上 | 见 §2.2 |
| GAP-03 | `subscribed_user_conditions_total` — user channel 订阅数 | S-03 | P1 | 小冯 (PolymarketCLOBSubscriber.user_condition_ids_.size()) + 小卢 | 同 §2.2 路径, 读 user_condition_ids_.size() |
| GAP-04 | `wss_last_disconnect_ts_ns` — 最后断连时刻 | S-08 | P1 | 小冯 (subscriber 记录断连 ts) + 小卢 | subscriber 新增 atomic<int64_t> last_disconnect_ts_ns_, OnDisconnected 时写入; MetricsSnapshot 加字段 |
| GAP-05 | `subscribed_tokens_total{tier="hot"/"cold"}` — hot/cold 分桶计数 | S-09 | P2 | 小冯 | hot_token_ids_.size() / cold_token_ids_.size() 已在 subscriber, 需暴露 |
| GAP-06 | `instance_lock_held` — 单实例锁 | H-08 | P1 | 小卢 (进程启动 flock + atomic bool 暴露) | /metrics 输出 gauge 0/1 |
| GAP-07 | `feed_gap_last_ts_ns` — 最近 gap 时刻 | E-04 | P1 | 小冯 (subscriber gap 检测时记录 ts) + 小卢 | MetricsSnapshot 新增 `feed_gap_last_ts_ns` |
| GAP-08 | WSS 错误计数 (非 reconnect) | E-03 | P2 | 小冯 | SubscriberMetrics 已有 `heartbeat_timeouts_total` 等, 需汇总进 MetricsSnapshot |
| GAP-09 | `stcpp_wss_connected` 当前只有布尔, 缺 per-channel reconnect 细分 | S-07 细化 | P1 | 小卢 | MetricsSnapshot 拆 `wss_reconnect_total` 为 per-channel |

### 2.2 GAP-01/02 核心缺口: 订阅了多少市场/token (GM 点名, P0)

**现状分析:**

`OrderBookSnapshotHub` 已有 `token_count() const noexcept` 方法 (返回 `slot_count_`, 已注册的 token 数)。
`PolymarketCLOBSubscriber` 的 `initial_market_token_ids` 记录启动时订阅列表, 动态追加走 `SubscribeMarketTokens`。
但 `MetricsSnapshot` 结构体没有 `subscribed_tokens_total` / `subscribed_markets_total` 字段。
`/metrics` endpoint 不输出这两个值。
前端无法获取订阅数量 — 这是最大观测盲区。

**补齐方案 (不需要新 endpoint, 在现有路径里加字段):**

步骤 1 (小冯): `MetricsSnapshot` 结构体加 3 个字段:
```
struct MetricsSnapshot {
    // ... 现有字段不动 ...
    // 新增: 订阅计数 (由 OrderBookSnapshotHub + CLOBSubscriber 填充)
    std::int64_t subscribed_tokens_total{0};    // hub_.token_count()
    std::int64_t subscribed_markets_total{0};   // subscribed_tokens_total / 2 (双 token 规则)
    std::int64_t subscribed_user_conditions{0}; // user_condition_ids_.size()
    std::int64_t wss_last_disconnect_ts_ns{0};  // GAP-04: 最后断连 epoch_ns (0=从未断连)
    std::int64_t feed_gap_last_ts_ns{0};        // GAP-07: 最近 gap 检测时刻
};
```

步骤 2 (小冯): `RealStateProvider::metrics()` 填充新字段:
  - `subscribed_tokens_total` = `hub_.token_count()`  (hub_ 已是 const 引用, 直接调用)
  - `subscribed_markets_total` = `hub_.token_count() / 2`  (老李 spec §2.1: 每 condition_id 订双 token)
  - `subscribed_user_conditions` = 从 subscriber 读 `user_condition_ids_.size()`
    (需 subscriber 暴露只读方法: `std::size_t user_condition_count() const noexcept`)
  - `wss_last_disconnect_ts_ns` = subscriber 新 atomic 字段
  - `feed_gap_last_ts_ns` = subscriber gap 检测时写入 atomic

步骤 3 (小卢): `/metrics` endpoint 输出新 metric:
```
stcpp_subscribed_tokens_total{mode="paper"} 42
stcpp_subscribed_markets_total{mode="paper"} 21
stcpp_subscribed_user_conditions_total{mode="paper"} 8
stcpp_wss_last_disconnect_ts_ns{mode="paper"} 1717000000000000000
stcpp_feed_gap_last_ts_ns{mode="paper"} 0
```

  低基数保证: 只有 `mode` label (固定 3 值: paper/live/backtest), 不加 market_id。

步骤 4 (小卢): `DemoStateProvider::metrics()` 填合法 demo 值 (如 subscribed_tokens=42, subscribed_markets=21)。

**注意: MetricsSnapshot 是 state_provider.hpp 的公共契约, 改动需小冯 + 小卢联合 PR, 老周 review。**

### 2.3 订阅数的 Prometheus metric label 设计

遵循 ADR-038 §3 低基数铁律:

- 正确: `stcpp_subscribed_tokens_total{mode="paper"}` — mode label 固定 3 值
- 禁止: `stcpp_subscribed_tokens_total{market_id="0xabc..."}` — 高基数爆炸 (每市场一条时序)

前端 "订阅了多少市场" 的数字直接读 `stcpp_subscribed_markets_total` 这个 gauge 的 value, 简单明了。

### 2.4 现有 /metrics 已有但 Widget 尚未对接的字段

以下字段后端已输出, 前端观测页直接消费即可, 无需后端改动:
- `stcpp_uptime_seconds` → H-01
- `stcpp_wss_connected{channel}` × 3 → S-04/S-05/S-06
- `stcpp_wss_reconnect_total` → S-07
- `stcpp_loop_latency_p99_us` → H-04
- `stcpp_rm_decision_total` → B-01
- `stcpp_rm_reject_total` → B-02
- `stcpp_fill_total` → B-03
- `stcpp_net_edge_bps` → B-05
- `stcpp_cum_net_pnl` → B-04
- `stcpp_data_staleness_ms_max` → Q-01
- `stcpp_feed_gap_total` → Q-02
- `stcpp_price_drift_bps` → Q-03

---

## 3. 看板 vs Grafana 分工

| 维度 | 开发者/运维观测页 (本规格) | Grafana (Sprint-3 W11+, 小郑立项) |
|---|---|---|
| 数据时效 | 当前态 + 近期 (5~60s 轮询) | 历史趋势 (分钟/小时/天粒度) |
| 数据量 | 轻量 (单次响应 < 5KB) | 重 (时序数据库, 可 TB 量级) |
| 告警 | 简单阈值 (颜色变化) | 复杂告警规则 + PagerDuty 集成 |
| 使用场景 | 开发调试 / 盯盘 / 快速巡检 | SRE 值班 / 事后分析 / 周报 |
| 部署依赖 | 零额外组件 (复用 debug_api) | Prometheus + Grafana + Loki + Tempo |
| 订阅数 widget | 实时 gauge (当前值) | 趋势图 (订阅数随时间变化) |
| PnL | 近 1h 分桶 (前端轮询拼) | 全量历史回测对比 |
| 拒单列表 | 最近 50 条 raw log | 聚合统计 + reason_code 趋势 |
| 4ts 延迟 | 单市场按需查 (REST pull) | 全市场热力图 / p99 趋势 |
| 当前阶段 | 立即开发 (v6 观测页) | Sprint-3 W11+ 启动 |

**原则:** 观测页是"急诊室" (快速判断系统现在是否健康, 订阅了哪些市场); Grafana 是"病历档案" (历史回顾 + 深度分析)。两者不重复建设, 观测页不做时序存储。

---

## 4. 前端轮询策略建议

| 区块 | 轮询 endpoint | 建议间隔 | 备注 |
|---|---|---|---|
| 订阅状态 / 系统健康 | `GET /metrics` + `GET /healthz` | 5s | 健康关键, 频繁刷 |
| 系统状态 / 模式 | `GET /status` | 5s | 含 data_source 标记 |
| 业务吞吐 stat | `GET /metrics` | 5s | 与健康合并请求 |
| PnL 时序 | `GET /api/v1/pnl/timeseries` | 10s | bucket 粒度 5min, 更快无意义 |
| 拒单列表 | `GET /api/v1/risk/rejects` | 10s | 低频即可 |
| 持仓快照 | `GET /api/v1/positions` | 15s | 持仓变化慢 |
| PAPER-G 门禁 | `GET /api/v1/gate/paper` | 30s | 30 日窗口, 变化极慢 |

前端拼 Q-04 staleness 时序: 每次 /metrics 返回后把 `stcpp_data_staleness_ms_max` 值 push 到本地环形数组 (60 个采样点 = 5min), 渲染 timeseries。不需要后端新 endpoint。

---

## 5. 新增 Prometheus metric 命名汇总

以下是本文档定义的新增 metric (需后端实现后才输出):

```
stcpp_subscribed_tokens_total{mode}          gauge  已订阅 token 数 (hub.token_count)
stcpp_subscribed_markets_total{mode}         gauge  已订阅盘口数 (token/2)
stcpp_subscribed_user_conditions_total{mode} gauge  user channel 订阅 condition_id 数
stcpp_wss_last_disconnect_ts_ns{mode,channel} gauge  最后断连 epoch_ns (0=从未断连)
stcpp_feed_gap_last_ts_ns{mode}              gauge  最近 seq gap 检测时刻 epoch_ns
stcpp_instance_lock_held                     gauge  单实例锁是否持有 (1=持有, 0=未持有)
stcpp_wss_reconnect_total{mode,channel}      counter WSS reconnect 次数 (现有按 channel 细分)
```

低基数保证: 所有新增 metric 的 label 集合有界:
- `mode`: paper | live | backtest (固定 3 值)
- `channel`: sports_api | clob | user (固定 3 值)
无任何 market_id / token_id / intent_id label。

---

## 6. 实施派单建议 (供老周统筹)

| 任务 | 执行人 | 依赖 | 优先级 |
|---|---|---|---|
| MetricsSnapshot 加 5 个新字段 (§2.2 步骤 1) | 小冯 (OrderBookSnapshotHub owner) | 无 | P0 |
| subscriber 暴露 user_condition_count() + last_disconnect_ts_ns atomic | 小冯 | 无 | P0/P1 |
| RealStateProvider::metrics() 填充新字段 (步骤 2) | 小冯 | MetricsSnapshot 先改好 | P0 |
| /metrics endpoint 输出新 metric + DemoStateProvider demo 值 (步骤 3/4) | 小卢 | MetricsSnapshot 先改好 | P0 |
| 前端 v6 观测页 widget 接入 (消费 /metrics + /status + /healthz) | 小苏 (前端) | 后端 GAP-01/02 填好 | P0 |
| wss_last_disconnect_ts_ns / feed_gap_last_ts_ns atomic (GAP-04/07) | 小冯 | 无 | P1 |
| /api/v1/data/latency/{market_id} endpoint (Q-05) | 小卢 | R-20 4ts 数据流接入 | P2 |

注: MetricsSnapshot 是跨小冯/小卢的公共契约头文件 (state_provider.hpp)。
改动走联合 PR, 老周 review, 防止接口冻结后各自出现不同版本。
