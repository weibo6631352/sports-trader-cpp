# 后端观测/调试 API — 架构决议 v1

- Owner: 老周 (#02, A 系统工程部主管, 架构主权)
- Last review: 2026-05-29
- 视角: 仅 A 单元架构骨架 (主管拍架构, 不写代码)
- 整合既有资产 (不另起炉灶):
  - `src/stcpp/debug_api/` (小卢 W9 已落: server + healthz/version/status + endpoint_version)
  - `docs/RESEARCH/laozhou-w8-debug-rest-api-spec-v1.md` (我 W8 spec, 接口分级 + 热路径隔离)
  - `docs/RESEARCH/xiaolu-w9-rest-api-skeleton-implementation-spec-v1.md`
  - `docs/RESEARCH/xiaosu-backend-api-requirements-v1.md` (24 REST + 10 WSS hard ask)
  - `docs/RESEARCH/xiaozheng-observability-v0.1.md` (Prom/Grafana/Loki/Tempo)
- 红线: R-12 (event loop 禁同步 IO/锁 > 100us) / R-11 (paper 不污染真账本) / 本地优先

---

## 0. 决议 TL;DR

**单进程内嵌 debug_api (cpp-httplib, vCPU6 独立线程) 演进为 `stcpp-ops-gateway` 独立进程**
(小苏架构图)。两阶段:

- **阶段 A (现在 → Sprint-3)**: 沿用小卢 W9 in-process debug_api,扩 read-only endpoint。本地开发友好,零部署依赖。
- **阶段 B (Sprint-3+)**: gateway 拆独立进程,经 WAL/IPC 读 trader 状态,物理隔离。gateway 挂 ≠ trader 挂。

阶段 A 的 endpoint schema 即为阶段 B 契约,**接口冻结一次,实现迁移透明**。

---

## 1. REST vs WSS 分工

| 通道 | 语义 | 内容 | 频率约束 |
|---|---|---|---|
| **REST `/api/*`** | 状态快照 / 历史拉取 / 字典 / replay | status / positions / pnl summary / reject 字典 / audit replay (cursor) / market 盘口 / m45 gate / heartbeat | UI poll 1~60s,绝不 us 级 (小苏: UI 100ms 够) |
| **WSS `/feed/*`** | 实时观测推送流 | rm_state 迁移 / risk_events / paper_fills / signal_triggers / market_data (throttled) / data_health / alerts | peak 10~100 msg/s,server 端 throttle (market 10Hz/position 1Hz) |

**R-12 不阻塞热路径 (不可妥协):**
- 观测 server 跑在**独立 std::thread + 独立 vCPU6** (ADR-015),vCPU0/1/2 (Ingest/Signal/Risk) 绝不调用任何 server 方法。
- WSS push **不在热路径回调里直接 write socket**。热路径只做一件事: 向 lock-free SPSC ring (ADR-017) push 一条 event,**wait-free,≤ 100us**。gateway 线程消费 ring → 序列化 → 推 WSS。ring 满则 drop 最旧观测帧 (观测可丢,交易不可阻)。
- REST handler 全在 gateway 线程,任何慢查询 (audit verify < 2s) 隔离在此,与热路径无共享锁。

---

## 2. 只读 vs 控制面 (严格隔离)

- 这套**默认纯只读观测面** (`/api/*` GET + `/feed/*` push)。观测 API 任何 bug 不得改变交易行为。
- **控制面 (POST halt / drain / resume)** 物理隔离:
  - 独立路由前缀 `/control/*`,独立 audit (`AET_OPERATOR_ACTION`),双确认 + ack_token。
  - `/control/halt` 经 RM 单向 SAFE_MODE 触发器 (老韩主权,我只提供传输),**绝不绕 RiskManager** (红线)。
  - mode 切换 = build-time 锁 (R-11/ADR-011),`/api/operator/mode` 仅 GET 暴露 `locked:true` + binary_sha256,**运行时不可切**。
- **MVP 决议: 控制面暂只做 `/control/halt` (紧急停),启停/下单不做。** UI 显示 read-only badge。

---

## 3. 内部状态安全暴露 (零耦合 / 零锁竞争)

热路径模块 (RM / Matcher / SignalEngine / PositionLedger) **不得为观测加锁**。三种读出机制,按更新频率选:

1. **原子标量** (state / counts / uptime / wss_connected): 各模块发布 `std::atomic<T>`,gateway `memory_order_acquire` wait-free 读。现有 `/status` stub 已是此模式 (HttpServer::is_running)。
2. **double-buffer snapshot** (RM 5 态 + bankroll + m45 gate + signal stats): 热路径写 back buffer,原子翻转 `active_idx`,gateway 读 front buffer。无锁,读永不阻塞写。每个模块暴露一个 `XxxDebugSnapshot` POD struct (老韩 `RmDebugSnapshot` 已规划)。
3. **lock-free SPSC ring → WAL** (fills / signal_triggers / audit events / market_data): 热路径单生产者 push,gateway 单消费者 pop。阶段 B 落 WAL,UI 经 gateway replay,**UI 永不直读 binary WAL** (小苏 ask #1)。

原则: 观测侧只持有指向各模块发布结构的 `const` 指针/句柄,**反向依赖为零**,模块不知道观测存在。

---

## 4. Endpoint 命名 / 版本 / schema 规范 (vendor-agnostic)

- **前缀**: 只读 `/api/v1/*`,推送 `/feed/v1/*`,控制 `/control/v1/*`,运维 `/healthz` `/version` `/metrics` (无版本,惯例)。
- **版本**: path 内嵌 `v1`;break change 升 `v2` 并保留 `v1` 一个 sprint。
- **schema vendor-agnostic**: endpoint 与字段名用**内部领域语义** (`market_id` / `book_l1` / `live_section` / `time_status`),**不泄露 vendor** (不出现 polymarket/goalserve/clob 作字段名);vendor 来源降为 payload 内 `source` 标签。对齐内部统一数据模型。
- **4 时间戳契约 (R-20)**: 每个 payload 带 `as_of_ts` (快照时刻);事件类带完整 `{event_ts, data_source_ts, ingestion_ts, as_of_ts}`,禁本地 now() 替上游 ts。
- **格式**: JSON (UI/调试) + Prometheus text exposition (`/metrics`,给小郑 Prom pull,与 obs 栈对齐)。cursor 翻页统一 `since=` / `limit=`。

---

## 5. 与现有 debug_api + 小卢 REST 整合 (不另起炉灶)

- **以 `src/stcpp/debug_api/` 为唯一落点**,小卢 W9 的 `HttpServer` + endpoint 注册模式 (`register_xxx(svr, hs)`) 即骨架。新 endpoint 按同模式扩 `endpoint_*.cpp`,**逐个把 W9 stub 字段接真 snapshot** (status 已留 W9 W3 接 RmSnapshot/SignalEngine/PositionLedger 的占位)。
- 小苏 24 REST / 10 WSS 需求 = 阶段 A endpoint backlog,按 owner 分派 (RM→老韩,audit→老唐,paper→小蒋,signal→小卢/小程,market→老李/小冯,data→小余)。**契约由各 owner 提 snapshot struct,我定传输与命名规范**。
- 阶段 B 拆进程时,endpoint handler 实现从"直读 atomic"换成"读 WAL/IPC",**对外 schema 不变**。

---

## 6. 本地优先 (不依赖上云)

- in-process debug_api 默认 `listen("127.0.0.1", 8080)` (本地 only),`curl localhost:8080/api/v1/status` 即可调试,零外部依赖。
- `/metrics` 本地 Prom 可 pull;obs 4 件套 (小郑) 本地 docker-compose 起,**不强制**,缺席也能 curl 看状态。
- 不引入云 SaaS / 托管网关 (与 obs 栈"不出境"一致)。阶段 B gateway 仍部署在同机房 obs 节点,本地 dev 单进程跑通即可。

---

## 7. 交接 / 待会签

- **传输层实现**: 小卢 (debug_api 扩 endpoint) + 小冯/老李 (market) — A 单元 IC,我派单。
- **snapshot struct 契约**: 各模块 owner 提 POD (老韩/老唐/小蒋/小卢/小董/小余),我 review 命名 + 4ts。
- **WSS push 框架**: 复用 SPSC ring (ADR-017),老姜/小郑会签延迟预算。
- **R-12 验收**: 任何 push 路径进热路径回调 = P0,CI 静态检查 (gateway 线程不得 link 热路径锁)。
- 控制面 `/control/halt` 经 RM SAFE_MODE: **老韩主权**,我仅提供传输,跨单元会签。
