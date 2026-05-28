# 可观测性栈 v0.1

- Owner: 小郑 (observability-engineer)
- Last review: 2026-05-28
- 验收人: 老吴 (deployment) + 老周 (chief-architect)
- 关联 ticket: S1-018
- 关联依赖: S1-001 (老周架构 5 层) / S1-011 (老姜延迟预算) / S1-004 (老韩 RM audit) / S1-016 (老沈威胁模型) / S1-010 (老吴跨洋部署)
- 协作待会签: 部署 @老吴, audit @老韩 + 老唐 + 老岳, trace 工具 @老徐, 告警路由 @老仓 (SRE) + 小尤 (UX)

---

## 0. TL;DR (老雷 + 老吴看这一段)

- **栈**: Prometheus (metric) + Grafana (可视化) + Loki (结构化日志) + Tempo (trace) + Alertmanager (告警). 4 件套, 自部署, 不引外部 SaaS.
- **位置**: 独立 obs 节点 (与主交易物理隔离, 同区域同机房), pull-based, obs 挂掉**不影响**主交易 hot path.
- **指标总数 (v0.1)**: **78 个 metric** (L1=14, L2=18, L3=16, L4=15, L5=15) + **6 个 SLI**.
- **第一个该上线的 dashboard**: **数据健康大盘 (小余视角)** — 跨洋链路 + WSS heartbeat + ETL 延迟最先暴露问题, 老姜 §3 跨洋 RTT 实测就需要它兜底.
- **采样率**: trace 默认 1% + 异常 100% + 拒单 100%; metric 全采.
- **存储**: Prometheus 15d local + remote_write 到 Thanos / VictoriaMetrics 半年 (老吴定); audit log 是独立体系 (老韩 / 老唐), 我不碰.

---

## 1. 总体架构 + 部署

### 1.1 栈选型

| 层 | 选型 | 替代品 | 不选的理由 |
|---|---|---|---|
| Metric 收集 | **Prometheus 2.x** | VictoriaMetrics, InfluxDB | Prom 是 C++ 生态事实标准, prometheus-cpp 成熟, pull model 自带可用性探测 |
| Metric 长存 | **VictoriaMetrics** (单机) | Thanos, M3, Cortex | 单二进制零依赖, 写吞吐高 4-5x Prom, MVP 阶段不上分布式 |
| 可视化 | **Grafana OSS** | 无 | 行业标准, 与 Prom/Loki/Tempo 都是同家 (Grafana Labs) |
| 日志 | **Loki** | ELK | ELK 太重 + JVM; Loki label-based 与 Prom 模型一致, 跨洋带宽紧也省 |
| Trace | **Tempo** + OpenTelemetry SDK | Jaeger | Tempo 与 Loki/Grafana 同家, 联动 metric→trace→log 一键跳; Jaeger 单独索引 ES 成本高 |
| 告警 | **Alertmanager** | PagerDuty (商用) | 与 Prom 同家, 路由 + 静默 + dedup 够用; 通知通道可外挂 PagerDuty/Webhook |
| C++ Metric Client | **prometheus-cpp** (jupp0r/prometheus-cpp) | civetweb-only | 成熟, header-only push gateway 模式可选, 老姜 RDTSC 埋点接口可包到 Histogram |
| C++ Trace Client | **OpenTelemetry C++ SDK** | Jaeger native client | OTel 是 CNCF 标准, exporter 可换 Tempo/Jaeger/OTLP, 不锁库 |

**为什么不上 SaaS (Datadog / NewRelic):**
- 跨洋链路, metric/log 流量出境带宽紧
- 体育博弈合规敏感 (老黄 S1-006), 数据出境额外法务负担
- 成本: 78 metric × scrape 15s × 长存半年, SaaS 月费 ≥ $2k, 自建一个 4c8g VM 一年 $500 搞定

### 1.2 部署拓扑 (待 @老吴 确认)

```
   美东 obs 节点 (独立 VM, 4c8g, 与主交易同区域)
   ┌──────────────────────────────────────────┐
   │  Prometheus  (scrape 15s, retain 15d)    │
   │  VictoriaMetrics (remote_write, retain   │
   │                   180d)                  │
   │  Grafana                                 │
   │  Loki + promtail (受控 agent 拉日志)      │
   │  Tempo + OTel collector                  │
   │  Alertmanager                            │
   └─────────────────▲────────────────────────┘
                     │ pull /metrics (15s)
                     │ push OTLP (trace)
                     │ push Loki (log, 异步批)
   ┌─────────────────┴────────────────────────┐
   │  主交易节点 (美东, 老吴 S1-010)            │
   │                                          │
   │  stcpp-trader     :9090 /metrics         │
   │  stcpp-recorder   :9091 /metrics         │
   │  stcpp-recon      :9092 /metrics         │
   │  stcpp-metrics-agent (sidecar)           │
   │     - 兜底 push gateway (短任务)          │
   │     - OTel collector agent (trace 转发)   │
   │     - promtail (日志 tail)                │
   └──────────────────────────────────────────┘
```

**部署要点:**
- obs 与 trader **不同节点**, 同 VPC, 内网 < 1ms RTT
- Prom scrape 走内网, 不出公网
- Alertmanager → 告警通道 (Slack / 电话 / PagerDuty) 走 obs 节点出口, 与主交易隔离
- 国内 on-call 看 Grafana 走 VPN bastion (老沈 TB-D), 不直暴 Grafana
- **obs 节点宕** ≠ 主交易宕. trader 继续跑, prometheus-cpp 是 in-process 端点, scrape 拉不到就拉不到, 不阻塞 hot path

### 1.3 与主交易的隔离 (obs 故障不影响交易)

| 故障 | 影响主交易吗 | 兜底 |
|---|---|---|
| obs 节点宕 | 否 | Alertmanager dead-man switch (见 §4.3) 触发反向告警 |
| Prom scrape 失败 | 否 | metric in-process ring buffer, scrape 恢复后下一轮拿到 |
| Loki 收日志卡 | 否 | promtail 本地 buffer, 满则丢老的 + bump counter |
| Tempo OTLP 端不通 | 否 | OTel collector agent buffer + retry; hot path 永远是 fire-and-forget, span 满则丢, 不阻塞 |
| Grafana 宕 | 否 | 仅影响人看面板, 不影响告警 (Alertmanager 独立) |
| Alertmanager 宕 | **是** (失去告警) | dead-man switch: 24h 没收到 heartbeat alert → on-call 电话, 见 §4.3 |

**红线: 任何 metric / trace / log API 在 hot path 上必须 lazy + zero-syscall + zero-malloc.** 见 §8.2.

---

## 2. 关键指标清单 (按 5 层)

**Naming 约定 (Prometheus best practice):**
- 前缀: `stcpp_` (sports-trader-cpp 缩写)
- 层标识: `stcpp_{l1|l2|l3|l4|l5}_<module>_<metric>_<unit>`
- 单位后缀: `_seconds` / `_bytes` / `_total` (counter) / `_ratio`
- **label 高基数禁区**: `market_id` 不当 label (有几千个), 走 exemplar 或 trace; `intent_id` / `audit_id` 同理
- 允许的 label: `strategy_tag` (~10 个), `market_type` (8 个 enum), `decision` (3 个 enum), `reject_code` (13 个 enum), `source` (poly/goalserve/chain), `endpoint` (≤20 个 URL group)

### 2.1 L1 INFRA (14 个)

| Metric | Type | Label | 单位 | Owner |
|---|---|---|---|---|
| `stcpp_l1_cpu_seconds_total` | counter | `core`, `mode` | s | 老周 |
| `stcpp_l1_mem_rss_bytes` | gauge | `proc` | byte | 老周 |
| `stcpp_l1_mem_arena_used_bytes` | gauge | `arena` | byte | 老周 |
| `stcpp_l1_disk_used_bytes` | gauge | `mount` | byte | 老吴 |
| `stcpp_l1_fd_open_count` | gauge | `proc` | int | 老周 |
| `stcpp_l1_net_rx_bytes_total` | counter | `iface` | byte | 老陈 |
| `stcpp_l1_net_tx_bytes_total` | counter | `iface` | byte | 老陈 |
| `stcpp_l1_net_rtt_seconds` | histogram | `peer` (poly/goalserve/rpc) | s | 老陈 |
| `stcpp_l1_tls_handshake_seconds` | histogram | `peer` | s | 老陈 |
| `stcpp_l1_thread_jitter_seconds` | histogram | `core`, `thread` | s | 老姜 |
| `stcpp_l1_gc_like_pause_seconds` | histogram | `cause` (malloc/pagefault) | s | 老姜 |
| `stcpp_l1_log_dropped_total` | counter | `level` | int | 小郑 |
| `stcpp_l1_clock_ntp_offset_seconds` | gauge | — | s | 老姜 |
| `stcpp_l1_uptime_seconds` | gauge | `proc` | s | 老周 |

### 2.2 L2 DATA (18 个)

| Metric | Type | Label | 单位 | Owner |
|---|---|---|---|---|
| `stcpp_l2_ingest_rate_total` | counter | `source`, `endpoint` | msg | 老李 / 小董 |
| `stcpp_l2_ingest_bytes_total` | counter | `source` | byte | 老李 |
| `stcpp_l2_wss_connected` | gauge | `source` | 0/1 | 老李 |
| `stcpp_l2_wss_heartbeat_age_seconds` | gauge | `source` | s | 老李 |
| `stcpp_l2_wss_reconnect_total` | counter | `source`, `reason` | int | 老李 |
| `stcpp_l2_wss_reconnect_seconds` | histogram | `source` | s | 老李 |
| `stcpp_l2_rest_rate_limit_remaining` | gauge | `endpoint` | int | 老李 |
| `stcpp_l2_rest_request_seconds` | histogram | `endpoint`, `status` | s | 老李 |
| `stcpp_l2_parse_seconds` | histogram | `source`, `schema` | s | 小田 |
| `stcpp_l2_parse_error_total` | counter | `source`, `error` | int | 小田 |
| `stcpp_l2_field_missing_total` | counter | `source`, `field` | int | 小余 |
| `stcpp_l2_normalize_seconds` | histogram | `source` | s | 小余 |
| `stcpp_l2_book_update_seconds` | histogram | `market_type` | s | 小田 |
| `stcpp_l2_book_depth_levels` | gauge | `market_type` | int | 小田 |
| `stcpp_l2_etl_e2e_seconds` | histogram | `source` | s | 小余 |
| `stcpp_l2_stale_source_seconds` | gauge | `source` | s | 小余 |
| `stcpp_l2_feature_snapshot_seq` | gauge | — | int | 小余 |
| `stcpp_l2_replay_lag_seconds` | gauge | — | s | 小段 |

### 2.3 L3 STRATEGY (16 个)

| Metric | Type | Label | 单位 | Owner |
|---|---|---|---|---|
| `stcpp_l3_signal_fire_total` | counter | `strategy_tag`, `signal_type` | int | 小程 |
| `stcpp_l3_signal_suppressed_total` | counter | `strategy_tag`, `reason` | int | 小程 |
| `stcpp_l3_pricing_seconds` | histogram | `strategy_tag`, `market_type` | s | 小梁 |
| `stcpp_l3_mm_quote_total` | counter | `strategy_tag` | int | 小蒋 |
| `stcpp_l3_mm_quote_spread_bps` | histogram | `strategy_tag` | bps | 小蒋 |
| `stcpp_l3_mm_inventory_usdc` | gauge | `strategy_tag` | USDC | 小蒋 |
| `stcpp_l3_edge_bps` | histogram | `strategy_tag`, `market_type` | bps | 小梁 |
| `stcpp_l3_edge_ci_low_bps` | histogram | `strategy_tag` | bps | 小梁 |
| `stcpp_l3_alpha_decay_seconds` | histogram | `strategy_tag` | s | 小程 |
| `stcpp_l3_intent_emit_total` | counter | `strategy_tag`, `urgency` | int | 小梁 |
| `stcpp_l3_intent_dropped_total` | counter | `strategy_tag`, `reason` (mpsc_full) | int | 小梁 |
| `stcpp_l3_strategy_enabled` | gauge | `strategy_tag` | 0/1 | 小梁 |
| `stcpp_l3_feature_read_seconds` | histogram | — | s | 小余 |
| `stcpp_l3_hedge_emit_total` | counter | `strategy_tag` | int | 小袁 |
| `stcpp_l3_direction_pnl_unreal_usdc` | gauge | `strategy_tag` | USDC | 小袁 |
| `stcpp_l3_signal_to_intent_seconds` | histogram | `strategy_tag` | s | 小梁 |

### 2.4 L4 RISK (15 个)

| Metric | Type | Label | 单位 | Owner |
|---|---|---|---|---|
| `stcpp_l4_evaluate_total` | counter | `decision`, `market_type` | int | 老韩 |
| `stcpp_l4_reject_total` | counter | `reject_code` | int | 老韩 |
| `stcpp_l4_evaluate_seconds` | histogram | `decision` | s | 老韩 |
| `stcpp_l4_state` | gauge | `state` (RUNNING/WARNING/HALTED/DRAIN) | 0/1 | 老韩 |
| `stcpp_l4_state_transition_total` | counter | `from`, `to`, `trigger` | int | 老韩 |
| `stcpp_l4_bankroll_usdc` | gauge | — | USDC | 老韩 |
| `stcpp_l4_market_exposure_usdc` | gauge | `market_type` | USDC | 老韩 |
| `stcpp_l4_market_exposure_ratio` | gauge | `market_type` | ratio | 老韩 |
| `stcpp_l4_daily_pnl_usdc` | gauge | — | USDC | 老韩 |
| `stcpp_l4_daily_loss_ratio` | gauge | — | ratio | 老韩 |
| `stcpp_l4_consec_loss_count` | gauge | — | int | 老韩 |
| `stcpp_l4_idempotency_hit_total` | counter | — | int | 老韩 |
| `stcpp_l4_audit_write_seconds` | histogram | — | s | 老韩 |
| `stcpp_l4_audit_write_error_total` | counter | `error` | int | 老韩 + 小郑 |
| `stcpp_l4_recon_drift_usdc` | gauge | `kind` (ledger_vs_chain) | USDC | 老彭 |

### 2.5 L5 EXECUTION (15 个)

| Metric | Type | Label | 单位 | Owner |
|---|---|---|---|---|
| `stcpp_l5_order_submit_total` | counter | `market_type`, `side` | int | 老李 |
| `stcpp_l5_order_submit_seconds` | histogram | `endpoint` | s | 老李 |
| `stcpp_l5_order_fill_total` | counter | `market_type`, `fill_kind` (full/partial) | int | 小肖 |
| `stcpp_l5_order_fill_ratio` | histogram | `market_type` | ratio | 小肖 |
| `stcpp_l5_order_reject_total` | counter | `reject_reason` (clob_side) | int | 老李 |
| `stcpp_l5_order_open_count` | gauge | `market_type` | int | 小肖 |
| `stcpp_l5_order_cancel_total` | counter | `reason` | int | 老李 |
| `stcpp_l5_sign_seconds` | histogram | `kind` (eip712) | s | 老孙 |
| `stcpp_l5_sign_error_total` | counter | `error` | int | 老孙 |
| `stcpp_l5_nonce_current` | gauge | `wallet` | int | 老孙 |
| `stcpp_l5_nonce_gap_total` | counter | — | int | 老孙 |
| `stcpp_l5_gas_price_gwei` | gauge | `provider` | gwei | 老叶 |
| `stcpp_l5_rpc_request_seconds` | histogram | `provider`, `method` | s | 老叶 |
| `stcpp_l5_rpc_error_total` | counter | `provider`, `error` | int | 老叶 |
| `stcpp_l5_wallet_balance_usdc` | gauge | `wallet` | USDC | 老孙 + 老彭 |

**合计: 14+18+16+15+15 = 78 个 metric**.

### 2.6 SLI (6 个核心 indicator, 滚动 30d 计算 SLO)

| SLI | 定义 | 来源 metric |
|---|---|---|
| `sli_hotpath_p99` | signal→order intent p99 | `stcpp_l3_signal_to_intent_seconds` + `stcpp_l4_evaluate_seconds` + `stcpp_l5_order_submit_seconds` |
| `sli_etl_p99` | ingest→feature p99 | `stcpp_l2_etl_e2e_seconds` |
| `sli_wss_reconnect_p99` | WSS 重连 p99 | `stcpp_l2_wss_reconnect_seconds` |
| `sli_uptime` | 主进程在线率 | `up{job="stcpp-trader"}` (Prom 内建) |
| `sli_risk_audit_success` | audit 写成功率 | 1 - `stcpp_l4_audit_write_error_total` / `stcpp_l4_evaluate_total` |
| `sli_data_freshness` | 各源 freshness p99 | `stcpp_l2_stale_source_seconds` |

---

## 3. SLO + 告警阈值 (与老姜延迟预算对齐)

| SLO | 目标 (滚动 30d) | 告警阈值 | 触发后行为 |
|---|---|---|---|
| **热路径 p99 < 500us** | 99.9% 窗口达成 | p99 > 500us 持续 5min → P1; p99 > 1ms 持续 1min → P0 | P0 触发自动 DRAIN (老韩) |
| **数据摄入→策略 p99 < 20ms** | 99% 窗口达成 | p99 > 20ms 持续 5min → P1; p99 > 50ms → P0 | P0 触发 RM WARNING |
| **WSS 重连 < 3s** | 99% 重连达成 | p99 > 3s → P2; 重连失败 3 次 → P1 | P1 升级到全源 STALE |
| **WSS heartbeat < 30s** (D-06) | 100% (硬约束) | age > 25s → P1; > 30s → P0 自动 HALT | 直接调 RM halt (老韩 R0) |
| **系统在线率 99.9%** | up time ≥ 99.9% | up == 0 持续 1min → P0 | systemd 拉起 + 告警 |
| **audit 写成功率 100%** | 100% (硬) | 任意失败 → P0 | RM fail-closed REJECT (老韩 §5.3) |
| **bankroll drift < 1%** | 99% 对账一致 | 任意 > 1% → P1; > 5% → P0 | 进 SAFE_MODE 仅撤不开 (老彭) |
| **nonce gap == 0** | 永远 0 | 任意 gap → P0 | 立即 HALT, 等老孙 + 老叶介入 |

**对齐老姜预算 (laojiang v1 §1):** 我的 `sli_hotpath_p99` = 老姜内环 budget. 老姜定 ~310us p99 + 100us 余量, 我设 SLO 500us, 留 ~190us 安全垫.

**对齐老姜预算 (laojiang v1 §2):** 我的 `sli_etl_p99` = 老姜外环 budget 20ms (含调度抖动, 不含跨洋 RTT). 跨洋 RTT 用 `stcpp_l1_net_rtt_seconds{peer=polymarket}` 单独 track, **不进 SLO** (老姜原则: 物理常数不放预算).

---

## 4. 告警分级 + 路由 (对齐小尤 UX 框架)

### 4.1 分级 (4 档, 与小尤将定的 UX 框架预对齐)

| 级别 | 响应 | 通道 | 例子 | 自动动作 |
|---|---|---|---|---|
| **P0 (灾难)** | 全自动熔断, 立刻电话 on-call | 电话 + Slack + Webhook to RM | 私钥异常 / nonce gap / bankroll 异常 / audit 写失败 / RM HALT | RM 状态机自动 HALT / SAFE_MODE |
| **P1 (严重)** | 5min 内人工介入 | Slack 高优 + 电话 (on-call 不响应升级) | hot path p99 > 500us, WSS 全断, RPC 全错 | RM 进 WARNING; 不自动停盘 |
| **P2 (警告)** | 1h 内 review | Slack 普通 | 单源 STALE_THRESHOLD 命中, gas 飙高, 拒单率异常 | 仅记录 |
| **P3 (信息)** | 每日 digest 邮件 | 邮件 | 单源短抖动, 单笔策略 edge_ci 穿 0 | 仅记录 |

### 4.2 路由表 (Alertmanager `route` 配置, 待 @老仓 @小尤 final)

```yaml
route:
  receiver: default-slack
  group_by: [alertname, severity]
  routes:
    - matchers: [severity="P0"]
      receiver: oncall-phone+slack-critical+webhook-rm
      group_wait: 0s     # P0 不聚合, 立即触发
      repeat_interval: 5m
    - matchers: [severity="P1"]
      receiver: slack-high
      group_wait: 30s
      repeat_interval: 30m
    - matchers: [severity="P2"]
      receiver: slack-normal
      group_wait: 5m
      repeat_interval: 4h
    - matchers: [severity="P3"]
      receiver: email-daily-digest
      group_wait: 1h
      repeat_interval: 24h
```

**与小尤 UX 对齐点 (待 @小尤 sprint-2 给出 UX 框架):**
- P0 在前端操盘 UI **红色 banner + 强制 modal**, 不让操作员误点掉
- P1 黄色 toast, 顶部常驻
- P2 侧栏 list, 可手动 dismiss
- P3 不进前端, 只在邮件

### 4.3 Dead-man switch (obs 自身故障兜底)

- trader 每 60s 推一条 `stcpp_meta_heartbeat_total` (push gateway)
- Alertmanager 配 `absent_over_time(stcpp_meta_heartbeat_total[5m])` → P0
- **关键**: 这条 alert 的 receiver 必须**不**经过 Alertmanager 本身 (鸡生蛋), 走 Grafana 直接告警 OnCall 或独立 cron + curl PagerDuty (待 @老仓 定)

---

## 5. Dashboard 清单 (Grafana)

**6 个核心 dashboard, 按"谁的视角"组织:**

| # | 名字 | 主用户 | 关键 panel | 上线优先级 |
|---|---|---|---|---|
| D1 | **交易大盘** | 操作员 (老雷 + on-call) | 当前 P&L / open orders / RM state / 告警计数 / 最近 10 笔 audit | P1 (M+2) |
| D2 | **数据健康大盘** | 小余 (data) | 各源 heartbeat age + reconnect / parse error / field missing / ETL p99 / 跨洋 RTT | **P0 (M+1, 第一个上)** |
| D3 | **信号监控** | 小梁 (quant) | signal fire rate / edge 分布 / alpha decay / strategy enabled 矩阵 | P2 (M+3) |
| D4 | **风控大盘** | 老韩 | 状态机 / 拒单热力图 (reject_code × strategy_tag) / bankroll / exposure / 日内 PnL | P1 (M+2) |
| D5 | **链上状态** | 老叶 + 老孙 | gas / nonce / RPC latency / wallet balance / fill latency | P1 (M+2) |
| D6 | **系统性能** | 老姜 + 老周 | per-core CPU / thread jitter / hot path p99 阶段拆解 / mem / fd | P1 (M+2) |

**为什么 D2 先上:**
1. 老姜 §3 跨洋 RTT 实测 (S1-021) **必须**有 D2 兜底数据
2. 老李 / 小董 WSS 接入 (S1-002/003) 上线时 D2 是唯一验收手段
3. 30s STALE_THRESHOLD (D-06) 在 D2 一眼能看
4. D2 不依赖策略 / 风控 / 执行任何上层, 是 ROI 最高的"先看到问题"面板

**Dashboard 共同约定:**
- 每个 panel 右上角带 `query inspector` 链接 (便于 SRE debug)
- 时间范围默认 last 1h, 关键 panel 提供 "click → 跳转到对应 trace" (Grafana exemplar)
- Variable 化: `strategy_tag` / `market_type` / `source` 用 Grafana variable, 不写死

---

## 6. Tracing 策略

### 6.1 链路定义 (端到端 trace)

**主 trace: signal → order submit**

```
[span] signal.fire (strategy/signal)
  └─ [span] pricing.compute (strategy/pricing)
       └─ [span] intent.emit (strategy/portfolio)
            └─ [span] risk.evaluate (risk/manager)     <- 跨线程, MPSC 传播
                 └─ [span] router.select (exec/router)
                      └─ [span] signer.sign (exec/signer) <- 跨进程, UDS 传播
                           └─ [span] clob.submit (exec/clob)
                                └─ [span] net.send (infra/net)
```

**辅 trace: ingest → feature** (数据外环)

```
[span] wss.frame (data/ingest/poly_wss)
  └─ [span] parse.json (infra/serde + simdjson)
       └─ [span] normalize (data/normalize)
            └─ [span] book.apply (data/book)
                 └─ [span] feature.publish (data/feature, RCU swap)
```

### 6.2 跨进程 trace 传播 (signer 独立进程)

- trader → signer 用 UDS, payload header 携带 W3C `traceparent` (16 byte trace_id + 8 byte span_id)
- signer 在 OTel SDK 里 `start_as_current_span` 接续, exporter 同 OTel collector agent
- **签名请求 payload 不进 trace** (含订单细节, audit 走老韩)

### 6.3 采样策略

| 场景 | 采样率 | 理由 |
|---|---|---|
| 常规下单 | **1%** (head-based) | hot path span 上千 QPS 全采会撑爆 Tempo |
| RM REJECT 决策 | **100%** | 拒单是低频高价值事件, 全采便于复盘 |
| 异常 / fail-fast | **100%** | err span 强制采 (OTel `sampling.priority=1`) |
| WSS reconnect | **100%** | 低频, 复盘必备 |
| 跨洋 RTT 超 200ms | **100%** | tail-based sampling, OTel collector 端配 |
| L1/L2 高频 span | **0.1%** | 拿趋势就够 |

**实现要点:**
- head-based 在 trader 端用 OTel `ParentBased(TraceIdRatioBased(0.01))`
- tail-based (异常 / 高延迟) 在 OTel collector 端用 `tail_sampling` processor, latency_threshold + error 双触发
- 采样决策**不影响 metric**, metric 永远全采

### 6.4 Exemplar (metric → trace 一键跳)

- 用 prometheus-cpp histogram 的 `ExemplarObserve(value, trace_id)` 接口
- Grafana 在 histogram panel 上自动渲染 exemplar 点, 点击跳 Tempo
- 关键 histogram 必带 exemplar: `stcpp_l3_signal_to_intent_seconds`, `stcpp_l4_evaluate_seconds`, `stcpp_l5_order_submit_seconds`, `stcpp_l5_sign_seconds`

---

## 7. 与 audit 分离 (老韩 / 老唐 vs 我)

**核心原则: 两套体系, 互不依赖, 互不替代.**

| 维度 | Audit (老韩 §5 + 老唐 + 老岳) | Metric/Trace (我) |
|---|---|---|
| 目的 | 取证 + 复盘 + 合规 | 监控 + 告警 + SLO |
| 完整性 | 不丢一条, fail-closed | 允许丢 (采样, ring drop) |
| 写入 | sync fsync, 阻塞决策 | async, 不阻塞 hot path |
| 存储 | append-only WAL + sqlite 索引, 本地 + 异地备份 | Prom TSDB + Tempo blob, 本地保留 |
| 字段 | 业务字段全 (bankroll, exposure, rule_trace) | 聚合 + 低基数 label |
| 保留 | ≥ 7y (合规) | 15d local + 180d longterm |
| 查询 | 按 audit_id / intent_id / 时间 | 按 metric name + label + 时间 |
| 故障 | audit 写失败 → 整个 evaluate 失败 (老韩 §5.3) | metric 写失败 → 记 counter, 继续 |

**禁止的反模式:**
- **不能**用 metric 反推 audit (label 维度被刻意压缩, 信息有损)
- **不能**用 audit 当 metric 源 (audit 是 sync 路径, 不允许跨进程聚合)
- **不能**把 audit_id 当 metric label (高基数, 走 trace_id 关联)

**允许的关联:**
- audit 记录里写入 `trace_id` 字段 (16 byte), 老韩 §5.2 audit schema 加这一列 → 待 @老韩 v0.2 加
- Grafana 看到异常 trace → 拿 trace_id → 在 audit log 里 grep → 拿到完整决策上下文
- Tempo 看到 trace → span attribute 带 `audit_id` (低基数环境下安全)

**职责分工:**
- 老韩 拥有 audit schema 业务字段
- 老唐 / 老岳 拥有 audit log 落盘 + 取证流程
- **我**拥有 metric + trace 体系, audit 落盘的 `trace_id` 字段是我们的接口契约 (待 @老韩 + @老唐 v0.2 联签)

---

## 8. 与 C++ 集成 (库选型)

### 8.1 库选型

| 用途 | 库 | 版本 | 集成方式 |
|---|---|---|---|
| Prometheus client | **prometheus-cpp** (jupp0r) | v1.2.x | vcpkg / Conan, header + lib, 静态链接 |
| OpenTelemetry | **opentelemetry-cpp** | v1.16+ | Conan, only SDK + OTLP exporter, 不要 jaeger exporter |
| Loki client | 不直接集成 | — | 走 promtail 读 stcpp 自家 log 文件 (Logger ring sink flush 到文件) |
| Logger | 老周 §2.1 `infra/log` 自研 | — | ring buffer + 后台 fsync, 我只定 format (JSON + W3C traceparent) |

**不引入的库:**
- `opentelemetry-cpp` 的 metric SDK — Prom client 已经够, 不双写
- StatsD / Datadog client — SaaS 路线砍掉
- Sentry C++ SDK — 老徐 §0 说先等 Sentry 项目, 后置

### 8.2 hot path 守门 (老姜 §5.3 接口契约)

**全 metric 调用必须满足:**
1. 单次 observe / inc < 50ns (老姜定 mark < 5ns, 我宽松到 50ns 因为含原子操作)
2. 不 syscall, 不 malloc, 不 lock
3. label 全部启动期 bind (`Family<>::Add(labels)` 在 ctor 里调一次, hot path 只 increment counter pointer)
4. Histogram bucket 静态化 (启动期定 buckets, 不接受运行时改)

**lazy 守门示例 (接口契约, 不写实现):**
```
// hot path 只调这个, 内部是 cached pointer + atomic inc
LATENCY_OBSERVE(kHotPathSignalToIntent, ns);  // <50ns
COUNTER_INC(kL4EvaluateApproved);             // <30ns
```

**禁区:**
- hot path 上 `Family<>::Add(labels)` (查 map 路径, ~ μs 级)
- hot path 上 string concat / format
- hot path 上 OTel `start_span` (不 lazy 不允许, 见 §6.3 采样)

### 8.3 Exporter 端点

每个进程暴露 3 个内置 HTTP endpoint (`infra/metrics` 启动 civetweb 单线程):

| Path | 用途 | 谁 scrape |
|---|---|---|
| `/metrics` | Prometheus pull | Prom server |
| `/health` | liveness | Alertmanager + systemd |
| `/ready` | readiness (启动期 self-check 完成 + WSS first heartbeat 收到) | Prom rule + 部署 gating |

OTel trace 不暴露 HTTP, 由 SDK 直接 OTLP push 到 OTel collector agent (localhost:4317).

---

## 9. 开放问题

| # | 议题 | 待定 | 负责跟进 |
|---|---|---|---|
| OQ-1 | obs 节点机型与成本 | 4c8g 假设, 实际 78 metric × 15s scrape 估算 ~50k samples/s, 待 @老吴 部署 review | @老吴 |
| OQ-2 | Tempo vs Jaeger 长期选型 | 倾向 Tempo (与 Grafana 同家), 但 Jaeger UI 更熟 | @老徐 (tools) |
| OQ-3 | trace 采样率 1% 是否够 | 1000 QPS × 1% = 10/s, 复盘 cover 度待评估 | @老姜 + @老韩 |
| OQ-4 | audit log schema 加 trace_id 字段 | 老韩 §5.2 schema_version bump 到 v0.2 | @老韩 + @老唐 |
| OQ-5 | dead-man switch 走哪条独立通道 | Alertmanager 自身故障兜底, 选 Grafana OnCall 还是独立 cron + PagerDuty | @老仓 |
| OQ-6 | 跨洋 metric remote_write 带宽预算 | 主→obs 同区域内网 OK; 但 obs → 国内 on-call dashboard 需 VPN 带宽预估 | @老陈 + @老吴 |
| OQ-7 | 操盘 UI 数据流 (小尤 UX 框架) | metric/SLI 喂给 UI 用 Grafana iframe 还是自家 React 接 Prom HTTP API | @小尤 + @小苏 |
| OQ-8 | exemplar 在 prometheus-cpp 是否原生支持 | 需要 review 库版本, 不支持则走自定义 _bucket 标签 (workaround) | @我 |
| OQ-9 | 78 metric 是否过多/过少 | 业内做市系统 100-200 metric 是常态, MVP 78 偏保守, 留扩展 | @老雷 review |
| OQ-10 | high-cardinality label 防爆 (e.g. 拒单代码 × strategy_tag × market_type 笛卡尔积) | 上 Prom `series limit` + 启动期 schema check | @我 + @老仓 |

---

## 10. 验收标准

- [ ] §1 obs 部署拓扑 @老吴 review 通过
- [ ] §2 78 个 metric 清单各 owner 在自己 ticket 里 ack 埋点
- [ ] §3 SLO 与老姜 (S1-011) 延迟预算口径对齐
- [ ] §4 告警分级与小尤 (UX 框架) 联签
- [ ] §5 D2 数据健康大盘 M+1 上线 (Sprint-2 内)
- [ ] §6 trace 采样策略 @老姜 + @老韩 ack
- [ ] §7 audit 接口契约 @老韩 + @老唐 联签 (trace_id 字段)
- [ ] §8 prometheus-cpp + opentelemetry-cpp 进 Conan 锁版本 (与 老何 C++ 选型对齐)

---

## 附 A — 与 ticket 的对应

- S1-001 (老周架构): 我的 5 层指标清单完全贴合 §2 分层
- S1-004 (老韩 RM): §2.4 L4 + §7 audit 分离
- S1-005 (老孙 私钥): §2.5 L5 + §6.2 跨进程 trace
- S1-009 (老叶 RPC): §2.5 L5 链上指标
- S1-010 (老吴 部署): §1.2 obs 节点位置依赖
- S1-011 (老姜 perf): §3 SLO + §8.2 hot path 守门
- S1-016 (老沈 安全): §1.3 obs 与 trader 隔离边界 (TB-B 进程隔离 + TB-D 运维边界)
- S1-018 (本文)
- S1-019 (协同规范): §4 告警通道与 on-call 流程
- S1-021 (跨洋实测): §3 `sli_etl_p99` + D2 数据健康大盘是其验收手段

---

**END v0.1.** 等老吴 (部署) + 老韩 (audit) + 小尤 (UX) 过一遍, 我再 bump v0.2.
