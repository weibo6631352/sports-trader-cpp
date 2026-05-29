# ADR-038: 后端观测/调试 API

- **ID:** ADR-038
- **Date:** 2026-05-29
- **Owner:** 老雷 (GM) 拍板; 老周 (架构主权) 实施架构; 老郭 (架构评审) 放行
- **Status:** Accepted (MVP 实施启动)
- **触发 (老板 verbatim):** "后端 api 也可以设计展开工作了, 讨论需要哪些接口, 主要用于我们观测、开发调试用的, 可获取信息尽可能详细。找人讨论一下开始实施吧, 前端人员和 polymarket 专家, 架构师, 顾问团也讨论一下。" + "性能专家呢 也需要讨论"
- **讨论参与 (7 方):** 小苏 (前端消费) / 老李 (Polymarket 协议) / 老周 (API 架构) / 小郑 (可观测性) / 小白 (安全) / 老郭 (架构评审+顾问协调) / 老姜 (性能预算)
- **输入文档:** docs/RESEARCH/{xiaosu-frontend-api-needs, laoli-polymarket-api-expose, laozhou-observability-api-arch, xiaozheng-observability-endpoints, xiaobai-observability-api-security, laoguo-observability-api-review, laojiang-observability-api-perf-budget}-v1.md

---

## 1. 定位 (scope)

**只读观测/调试 API**, 服务于人 (老板/开发) 观测系统 + 调试。信息尽可能详细, 但**详细 ≠ 泄密**、**详细 ≠ 拖累热路径**。

- ✅ 做: 观测系统状态 / PnL / 风控决策 / 信号 / 订单生命周期 / 行情质量 / GM-PAPER-G 门禁仪表 / 全链调试 trace
- ⛔ 不做 (MVP): 交易控制面 (启停/下单)。唯一例外 `/control/halt` 经 RM SAFE_MODE (老韩主权会签), 物理隔离 + 独立 audit
- 本地优先 (对齐 ADR-037 暂不上云): 默认 `127.0.0.1`, curl 即调

## 2. 架构 (老周 + 老姜, 不另起炉灶)

沿用 `src/stcpp/debug_api/` (cpp-httplib, vCPU6 独立线程, 小卢 W9 已落 skeleton), 两阶段:
- **阶段 A (现在→Sprint-3):** in-process 扩 read-only endpoint, 零依赖本地友好
- **阶段 B (Sprint-3+):** 拆 `stcpp-ops-gateway` 独立进程经 WAL/IPC 读状态。**阶段 A schema 冻结即阶段 B 契约, 迁移对外透明**

**热路径零拖累 (R-12 + 老姜预算, 不可妥协):**
- 热路径唯一动作 = 向**独立观测 SPSC ring** (ADR-017, 独立于 5 业务 ring) `try_push` 一帧 POD; 预算 p99 ≤ 80ns; ring 满 drop 最旧观测帧 (观测可丢/交易不可阻)
- 状态读出: atomic 标量 / double-buffer snapshot (POD, 原子翻 idx) / SPSC ring→WAL; 观测侧只持 const 句柄, 模块零反向依赖
- 详细信息分层: 常驻 WSS 只推摘要帧; full orderbook / 全链 trace / log 检索一律 **REST 按需拉取**, 详细成本由请求方承担, 不摊热路径
- **回归门禁: e2e bench 观测开/关双跑, delta p99 ≤ 2% 否则拦 PR**; 热路径 TU 禁 link 观测锁/JSON/堆 (CI 静态 grep)

## 3. 传输分工 + 命名

| 前缀 | 传输 | 用途 |
|---|---|---|
| `/api/v1/*` | REST (poll 1~60s) | 状态快照 / 历史聚合 / 字典 / replay |
| `/feed/v1/*` | WSS 推送 | 实时观测流 (单连接多 topic, 每消息带 seq + as_of_ts, 重连先发 snapshot 再续增量) |
| `/control/v1/*` | REST + 鉴权 + audit | 控制面 (MVP 仅 halt, 物理隔离) |
| `/metrics` `/healthz` | 裸前缀 | 运维 (Prometheus text) |

**schema 不可逆铁律 (MVP 就定对):** ① 4 时间戳字段名 + `epoch_ns int64` (禁 ISO 字符串); ② vendor-agnostic 字段语义 (禁 `goalserve_`/`pm_` 原始字段进 response, vendor 降为 payload `source` 标签, CI grep 守, 对齐 ADR-037); ③ Prometheus metric 名 + label key 一次定对 (低基数, 禁 market_id/intent_id 当 label)。

## 4. Endpoint 目录 (合并 小苏 + 老李 + 小郑)

### 4.1 PnL / 持仓 (O2 盈利可证)
| endpoint | 传输 | 内容 |
|---|---|---|
| `GET /api/v1/positions` | REST 3s | per market/outcome 持仓 + pnl_realized/unrealized + mark_price |
| `GET /api/v1/pnl/timeseries?window=&bucket=` | REST 10s | cum_net_pnl/realized/unrealized/fee/gas/n_trades 分桶序列 |
| `GET /api/v1/pnl/attribution` | REST 10s | gross→fee→gas→slippage→spread→net 瀑布 + per_market |

### 4.2 订单 / 撮合 / 风控 / 信号
| endpoint | 传输 | 内容 |
|---|---|---|
| `GET /api/v1/orders/open` | REST 2s | 在挂单 |
| `GET /api/v1/risk/rejects` | REST 5s | RM 拒单列表 (含 reason_code) |
| `GET /api/v1/signals/edge_vs_fill` | REST 5s | expected_edge_bps vs realized_edge_bps + slippage |
| `WS /feed/v1/fills` | WSS | 成交事件 |
| `WS /feed/v1/risk_events` | WSS | 拒单/风控事件实时 (schema 与 audit WAL 对齐, 老唐+老沈会签) |

### 4.3 Polymarket 协议 (老李)
| endpoint | 内容 |
|---|---|
| `GET /api/v1/market/{condition_id}` | MarketInfo: token_id/outcome/tick_size/fee_rate/neg_risk/accepting_orders + active/closed/resolved 三态分开 |
| `GET /api/v1/book/{condition_id}` | OrderBookSnapshot full depth + microprice/spread/imbalance (后端算好) + sequence_no + gap 计数 + WSS state |
| `GET /api/v1/order/{client_order_id}` | 订单生命周期 trace: Intent→签名(sig redact)→提交→ack→fill/reject/cancel, OrderStatus 7 态机每转移留 ts, fill 含 fee 实收, mode (R-11) |
| `GET /api/v1/settlement/{condition_id}` | 名义 fee_rate vs 实收 fee + redeemable/settled 状态 |

### 4.4 调试 trace + 可观测性 (小郑)
| endpoint | 内容 |
|---|---|
| `GET /api/v1/trace/decision/{audit_id}` | 单笔决策全链 span: signal→rm→signer→exec (trace_id=audit_id ULID, 从 audit WAL 重建); **放行单也带 verdict trace 不只拒单** |
| `GET /api/v1/gate/paper` | GM-PAPER-G 30 日滚动: n_trades/正收益日占比/Sharpe+se/p_value/hit_rate/mdd/prelim_pass/confirm_pass |
| `GET /metrics` | Prometheus: 健康(uptime/wss/reconnect/rtt/loop p99) + 业务(rm_decision/fill/net_edge/pnl) + 数据质量(staleness/gap/drift) |
| `GET /api/v1/logs?trace_id=` | 结构化日志检索 |
| `GET /api/v1/data/latency/{market_id}` | R-20 4 时间戳延迟瀑布 (倒挂告警) |
| `WS /feed/v1/health_alert` | halt/异常实时推送 |

## 5. 安全 (小白, 上线前置)

- **黑名单字段 (allowlist 默认拒, 永不出 API):** 私钥/mnemonic/shamir 分片 / 原始签名字节 r,s,v / 待签 digest / Polymarket API_KEY·SECRET·PASSPHRASE / Goalserve key / DB password / 任意 HMAC key
- **访问控制:** 默认 bind `127.0.0.1`, 远程走 SSH 隧道; 短 TTL 只读 token, 与下单/signer 权限物理隔离; **signer 进程永不开任何 HTTP/观测口**
- **R-11 API 层:** response 必带顶层 `mode: paper|live`; paper 查 paper_audit / live 查 risk_audit, **API 层禁 join 合表**
- **trace 脱敏:** 决策 trace 可全量详细 (rule_id/阈值/输入快照/4ts), 但永不带签名/私钥字节; KEY_ROTATION 只回元数据

## 6. 实施分期

- **MVP (现在, W9-W10):** PnL 看板 3 口 + 订单/风控/信号 + 调试 trace + gate/paper + market/book + /metrics + /healthz ≈ 12-14 read-only endpoint + WSS fills/risk_events; loopback 无 auth 合理; control 仅 halt
- **完整版 (M5+ live 前):** orderbook/audit 全量 + 鉴权 + signer 隔离审计 + 脱敏 + reverse proxy

## 7. 跨单元会签 (放行前置)

| 项 | 会签人 |
|---|---|
| `/control/halt` 经 RM SAFE_MODE 不绕 RiskManager | 老韩 (RM 主权) |
| WSS push 延迟预算 + 观测 ring 接入 re-baseline | 老姜 + 小郑 + 小石 |
| WSS/事件 schema 与 audit WAL 对齐 | 老唐 + 老沈 |
| R-12 验收 CI 静态检查 (gateway 线程不 link 热路径锁) | 老郭 + 老姜 |
| 老何 RCU footgun: 裸指针 swap 改 shared_ptr 原子 load / hazard pointer; tail_copy 返 span | 老何 review |

## 8. 落地

- [x] ADR-038 立 (本文件, 7 方讨论综合)
- [ ] MVP 实施: 小卢 扩 debug_api read-only endpoint (worktree+PR)
- [ ] 观测 SPSC ring + double-buffer snapshot 接入 (小石 + 小卢)
- [ ] 前端接 MVP endpoint (小苏)
- [ ] 会签项 §7 逐项关闭

---

**最后更新:** 2026-05-29 by 老雷 (GM) — 7 方讨论综合
