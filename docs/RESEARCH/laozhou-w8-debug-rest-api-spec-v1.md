# 后端 Debug REST API — 设计 Spec v1

- Owner: 老周 (cpp-chief-architect)
- Date: 2026-05-29 UTC
- Status: DRAFT v1 — 待老雷 GM ack + 老高 ABI lock + 小郑 Prometheus 对齐
- 关联:
  - `laozhou-architecture-v0.6-e2e.md` (vCPU 分工 + 链路图)
  - `ADR/2026-06-W3-adr-018-lib-selection.md` (cpp-httplib / simdjson 决议)
  - paper/live 单 binary 架构 (原 ADR-011, 已删; paper/live 隔离仍是现行红线 R-11)
  - `ADR/2026-06-01-adr-015-vcpu-pin.md` (vCPU 分工, API server 独立 vCPU)
  - `ADR/2026-05-28-gm-redline-websocket-non-blocking.md` (R-12)
  - `ADR/2026-05-28-gm-redline-data-source-timestamping.md` (R-20, 4-ts)
  - `laohan-riskmanager-design-v0.3.1.md` (RejectCode 12 枚举)
  - `laotang-audit-schema-v1.1.md` (AuditEvent envelope + 4-ts)

---

## §1 老板 Verbatim 约束

> "后端 api 也可以开始做了, 初期就围绕功能调试开发一些接口, 使我们能通过 curl 访问我们的 api 来进行功能检查."
>
> — 老板, 2026-05-29

诉求拆解:

| # | 原话片段 | 工程语义 |
|---|---|---|
| 1 | "后端 api" | 公司自建 HTTP server, 暴露内部 state, 不是第三方 Polymarket / Goalserve |
| 2 | "初期围绕功能调试" | debug-first, 不是生产级 auth/rate-limit; W8-W9 MVP, W12 再评 hardening |
| 3 | "通过 curl 访问" | HTTP/1.1 REST + JSON 出站; curl 可直接请求, 无 session/cookie |
| 4 | "功能检查" | 老板 + 工程师可 query: WSS 是否连上? RM 在不在 DRAIN? 信号有没有? 持仓多少? 拒单率? |

---

## §2 设计原则

### 2.1 接口分级

| 级别 | 约束 | W8-W9 状态 |
|---|---|---|
| **read-only debug** | 主体 (10/12); GET 仅读 state snapshot, 不写任何 position/ledger | MVP 全实施 |
| **运维写接口** | 仅 2 个 (DRAIN / resume); 通过 state machine atomic transition | MVP 实施, 无 auth |
| **auth** | W8 W4 仅本地 loopback; 公网部署 (Frankfurt) 再加 bearer token | 暂不实施 |

### 2.2 输出格式

- JSON 出站, 与公司栈保持一致
- 解析输入 (POST body) 用 simdjson on-demand (ADR-018 已定)
- 序列化出站 JSON 用 **nlohmann/json** (non-hot-path, debug server 可接受 DOM 模式)
  - 注: 小赵 (cpp-serialization-engineer) 维护 simdjson / glaze 两用选项; 出站 debug JSON 不在热路径, 选 nlohmann/json 或 glaze 均可, Sprint-3 W9 小赵 confirm; 本 spec 写 "nlohmann/json" 作为安全默认, 不依赖 glaze 是否已落地
  - 任务 prompt 写 "glaze 序列化" = 老板期望; 落地时小赵评估 glaze vs nlohmann/json; **若 glaze 选型已定则优先 glaze, 否则 nlohmann/json**
- Content-Type: `application/json; charset=utf-8`
- 时间戳字段全部走 4-ts 契约 (R-20): `event_ts / data_source_ts / ingestion_ts / as_of_ts`, 单位 epoch nanoseconds (int64)

### 2.3 热路径隔离 (不可妥协)

- API server 运行在独立 vCPU (vCPU6 或空闲核; 不占 vCPU0 Ingest / vCPU1 Signal / vCPU2 RM)
- 读 state 用 atomic snapshot: `std::atomic<StateSnapshot*>` + RCU, 或与 PositionLedger 相同的 SPSC ring 读端
- 绝对不锁 hot path 数据结构 (lock > 100us = P0, R-12)
- 写接口 (DRAIN / resume) 通过 `std::atomic<SystemState>` CAS transition, 不进 hot path

### 2.4 不破坏现有 ADR

| ADR | 约束 | API Server 如何遵守 |
|---|---|---|
| ADR-004 RM 短路 | RiskGateway::evaluate 独立路径 | GET /risk 仅读 RM 暴露的 atomic ring, 不调用 evaluate |
| ADR-011 paper/live 隔离 | 同一 binary, transport 物理隔离 | API 响应体带 `"mode": "paper"/"live"/"backtest"` 字段, 不混用 |
| R-12 hot path | WSS event loop 无阻塞 IO | API server 独立线程 + io_context, 完全隔离 vCPU0/1/2 |
| R-20 4-ts | 所有数据时间戳完整 | 每个 endpoint snapshot 包含 `as_of_ts`; 来自数据源的 ts 透传不用 now() 替代 |
| ADR-018 cpp-httplib | HTTP REST client 已用 cpp-httplib | HTTP server 同样用 cpp-httplib (见 §3 选型) |

---

## §3 HTTP Server 选型

### 3.1 候选评估

WebFetch 来源 (GM 错 #23 enforce, 必 cite):

**cpp-httplib**
- WebFetch: https://github.com/yhirose/cpp-httplib @ 2026-05-29
- 官方描述: "A C++11 single-file header-only cross platform HTTP/HTTPS library"
- 关键特性: single-file header-only (`httplib.h`), built-in HTTP server + client, TLS (OpenSSL / MbedTLS / wolfSSL), HTTP/1.1 only (不支持 HTTP/2/3)
- 重要限制: "This library uses 'blocking' socket I/O." (官网 README Important 块)
- 现状: ADR-018 已选定 cpp-httplib v0.16.3 作为 HTTP REST client (老李 polymarket-client); FetchContent 已配置, tag `v0.16.3`
- debug API 场景: blocking 模式对 debug 接口无害 (老板 curl 测试, 低并发, 不在热路径)

**Boost.Beast**
- WebFetch: https://www.boost.org/doc/libs/release/libs/beast/ @ 2026-05-29 (canonical redirect to 1.91.0)
- 官方描述: "Beast is a C++ header-only library serving as a foundation for writing interoperable networking libraries by providing low-level HTTP/1, WebSocket"
- 关键特性: header-only, 基于 Boost.Asio async io_context, low-level HTTP/1 + WebSocket
- 现状: ADR-018 已选定 Boost.Beast 作为 WSS transport (小冯 BoostBeastTransport); 项目已引入 Standalone Asio + Beast
- debug API 场景: Beast 是 low-level toolkit, 需手写 HTTP router; 对 debug 接口 overkill; 复用 io_context 与 WSS 共享有线程安全风险 (R-12)
- 依赖: Boost ≥ 1.82 + OpenSSL

**Drogon**
- WebFetch: https://drogon.org/ @ 2026-05-29
- 官方描述: "Drogon, the fast C++ web framework" — fully asynchronous, multi-threaded, 150K+ req/s (single Ryzen 3700X core)
- 关键特性: async + coroutine, ORM (PostgreSQL/MySQL/Redis), WebSocket, plugin system, CMake 构建
- 依赖: 非 header-only; 需要 cmake 构建全量框架 (`BUILD_ORM ON` default); 依赖 jsoncpp / OpenSSL / zlib 等
- debug API 场景: 严重 overkill; ORM + DB + Redis 对 debug server 无用; 引入大框架与 "不引入大型框架" 原则相悖; 编译时间长

### 3.2 推荐决议

**推荐: cpp-httplib (沿用 ADR-018 + ADR-006 决议)**

理由:

| 维度 | cpp-httplib | Boost.Beast | Drogon |
|---|---|---|---|
| 依赖增量 | **0 (已有)** | 已有但需手写 router | 需新引大框架 |
| ADR-018 兼容 | **完全 (已落地)** | 部分 (WSS 已用, 但 HTTP server 需额外实现) | 新引入 |
| header-only | **yes** | yes | no |
| 内置 HTTP server | **yes** | 需手写 | yes |
| debug 场景适配 | **最佳** (blocking ok, API 简单) | over-engineered | over-engineered |
| R-12 隔离 | 独立线程即合规 | 需小心 io_context 隔离 | 独立线程即合规 |
| 编译增量 | **nil** | nil | 大 |

**结论**: cpp-httplib 已在 ADR-018 落地, `httplib::Server` API 极简, blocking-per-thread 模式对 debug 接口完全足够 (老板 curl, 非高并发). 零额外依赖引入. **老雷 final ack 后固化 ADR.**

---

## §4 MVP Endpoint 列表

共 12 个 endpoint: 10 GET (read-only) + 2 POST (运维写). W8 W5 spec 闭环, W9 实施.

| Method | Path | 含义 | Sprint-3 owner | 数据来源 SSOT |
|---|---|---|---|---|
| GET | `/healthz` | 健康检查 (进程存活 + vCPU 各线程 heartbeat) | 老周 | 内部 watchdog |
| GET | `/version` | 版本 + build mode (live/paper/backtest) + git hash | 老周 | CMake 编译期常量 |
| GET | `/status` | 系统状态 (RUNNING/DRAIN/HALTED) + WSS 连接状态 + 活跃信号数 + 持仓数 + uptime | 老韩 | RM state + SystemState atomic |
| GET | `/signals/active` | 当前活跃 signal + fair_value + token_id + 4-ts | 小卢 | Signal Engine atomic ring |
| GET | `/signals/history` | 最近 N 条 signal (query `?limit=N`, 默认 20, 上限 200) | 小卢 | Signal history SPSC ring snapshot |
| GET | `/positions` | 当前持仓 (`pos_yes` + `pos_no` + `pnl_unrealized`) — paper/live 分开标 | 老韩 | PositionLedger RCU snapshot |
| GET | `/orderbook/{token_id}` | 镜像 orderbook snapshot (bids/asks + 4-ts) | 小袁 | OrderBookMirror (小袁 microstructure-v1) |
| GET | `/risk/rejects` | RM 拒绝历史 + RejectCode 分布 (query `?limit=N`, 默认 50) | 老韩 | RM ring (老韩 v0.3.1 §3 RejectCode) |
| GET | `/audit/recent` | 最近 N 条 audit event (query `?limit=N`, 默认 50) | 老唐 | AuditEmitter ring (老唐 v1.1 schema) |
| GET | `/metrics` | Prometheus text format (兼容 Prometheus scrape) | 小郑 | Prometheus exposer (小郑 W10) |
| POST | `/drain` | 触发 DRAIN 状态 (运维; W8 MVP 无 auth; body: `{"reason": "string"}`) | 老韩 | SystemState CAS transition |
| POST | `/resume` | 退出 DRAIN, 恢复 RUNNING (运维; 需 RM state 满足前置条件) | 老韩 | SystemState CAS transition |

**endpoint 数据 schema SSOT 引用:**
- `token_id` / `outcome` / `condition_id`: 老李 `laoli-polymarket-api-spec-v1.md` + `laoli-polymarket-endpoint-matrix-v3.md`
- 4-ts 字段 (`event_ts` / `data_source_ts` / `ingestion_ts` / `as_of_ts`): R-20 契约, `laotang-audit-schema-v1.1.md` §2.x
- RejectCode 12 枚举: `laohan-riskmanager-design-v0.3.1.md` §3.10.x
- SystemState (RUNNING / DRAIN / HALTED): `laozhou-architecture-v0.6-e2e.md` §2 vCPU 模型

---

## §5 Hot Path 安全约束

### 5.1 vCPU 分工

```
vCPU0  Ingest Reactor (R-12 严守)    ← API server 绝对不占
vCPU1  Signal Engine                  ← API server 绝对不占
vCPU2  Risk + Audit                   ← API server 绝对不占
vCPU3  Paper Sign + Match + Ledger    ← API server 不占
vCPU4  ML Hook (小邓)                  ← API server 不占
vCPU5  Settle + Gate (低频)            ← API server 不占
vCPU6  API Server (debug-rest)        ← 独立; paper 阶段可与 vCPU5 合并
vCPU3* WAL group commit fsync pool    ← 已有 (老王 v0.2)
```

paper 阶段 (ADR-015 现状): main thread + 4 WAL bg fsync; API server 独立线程, 不强 pin; 总 ≤ 6 threads.

M5+ live 前: 老姜 7-vCPU pin 压测后, API server 固定 vCPU6.

### 5.2 读 State 机制

每个 GET endpoint 读对应模块的 atomic snapshot, 不持锁进入热路径:

| Endpoint | 读机制 | 不可用 |
|---|---|---|
| /status | `std::atomic<SystemStateSnapshot>` (16B 内联) | 禁止调用 RM::evaluate() |
| /signals/active | `std::atomic<SignalSnapshotPtr>` RCU swap (老周 PositionLedger 同思路) | 禁止持 SignalEngine mutex |
| /signals/history | `SpscRingSnapshot<SignalEvent, 1024>` tail copy (wait-free read end) | 禁止阻塞 push 端 |
| /positions | PositionLedger RCU snapshot (与 SettleWatcher 同路径) | 禁止写 position |
| /orderbook/{id} | OrderBookMirror::snapshot() (小袁 microstructure-v1 接口) | 禁止修改 book |
| /risk/rejects | `SpscRingSnapshot<RejectEvent, 512>` tail copy | 禁止调用 evaluate() |
| /audit/recent | AuditEmitter ring tail copy (老唐 v1.1 ring buffer) | 禁止 emit() |

### 5.3 写接口安全

POST /drain 和 /resume 通过 `std::atomic<SystemState>` CAS:

```cpp
// 伪代码 (老韩 W9 实施)
bool try_drain(std::string_view reason) {
    SystemState expected = SystemState::RUNNING;
    return system_state_.compare_exchange_strong(
        expected, SystemState::DRAIN,
        std::memory_order_acq_rel
    );
    // 若 CAS 成功, RM evaluate() 自动拒单 (R-12 路径不变)
}
```

CAS 失败 (已在 DRAIN/HALTED) 返回 409 Conflict + 当前 state.

---

## §6 JSON Schema 示例 (curl 输出样本)

### GET /healthz

```
curl http://localhost:8080/healthz
```

```json
{
  "ok": true,
  "threads": {
    "ingest_reactor": "alive",
    "signal_engine": "alive",
    "risk_manager": "alive",
    "paper_signer": "alive",
    "api_server": "alive"
  },
  "uptime_sec": 3612,
  "as_of_ts": 1748476800000000000
}
```

### GET /version

```
curl http://localhost:8080/version
```

```json
{
  "version": "0.1.0-sprint3",
  "git_hash": "ebb25e8",
  "build_mode": "paper",
  "build_time": "2026-05-29T00:00:00Z",
  "cpp_standard": "C++20",
  "as_of_ts": 1748476800000000000
}
```

### GET /status

```
curl http://localhost:8080/status
```

```json
{
  "state": "RUNNING",
  "mode": "paper",
  "wss_connected": {
    "sports_api": true,
    "clob": true,
    "user_channel": false
  },
  "signals_active_count": 12,
  "positions_count": 3,
  "rm_rejects_last_60s": 2,
  "uptime_sec": 3612,
  "as_of_ts": 1748476800000000000
}
```

### GET /signals/active

```
curl http://localhost:8080/signals/active
```

```json
{
  "count": 2,
  "signals": [
    {
      "signal_id": "sig_0001",
      "token_id": "79390...abc",
      "condition_id": "0xabc123...",
      "outcome": "Yes",
      "fair_value": "0.5820",
      "side": "BUY",
      "strategy": "PinnacleNoVig_v1",
      "event_ts": 1748476799000000000,
      "data_source_ts": 1748476799100000000,
      "ingestion_ts": 1748476799200000000,
      "as_of_ts": 1748476800000000000
    },
    {
      "signal_id": "sig_0002",
      "token_id": "84521...def",
      "condition_id": "0xdef456...",
      "outcome": "No",
      "fair_value": "0.4150",
      "side": "SELL",
      "strategy": "PinnacleNoVig_v1",
      "event_ts": 1748476798000000000,
      "data_source_ts": 1748476798200000000,
      "ingestion_ts": 1748476798300000000,
      "as_of_ts": 1748476800000000000
    }
  ],
  "as_of_ts": 1748476800000000000
}
```

### GET /signals/history?limit=3

```
curl "http://localhost:8080/signals/history?limit=3"
```

```json
{
  "limit": 3,
  "count": 3,
  "signals": [
    {
      "signal_id": "sig_0001",
      "token_id": "79390...abc",
      "outcome": "Yes",
      "fair_value": "0.5820",
      "side": "BUY",
      "rm_verdict": "PASS",
      "as_of_ts": 1748476800000000000
    },
    {
      "signal_id": "sig_0000",
      "token_id": "79390...abc",
      "outcome": "Yes",
      "fair_value": "0.5710",
      "side": "BUY",
      "rm_verdict": "REJECT:STALE_BOOK",
      "as_of_ts": 1748476740000000000
    },
    {
      "signal_id": "sig_9999",
      "token_id": "11223...ghi",
      "outcome": "No",
      "fair_value": "0.3900",
      "side": "BUY",
      "rm_verdict": "PASS",
      "as_of_ts": 1748476680000000000
    }
  ],
  "as_of_ts": 1748476800000000000
}
```

### GET /positions

```
curl http://localhost:8080/positions
```

```json
{
  "mode": "paper",
  "count": 2,
  "positions": [
    {
      "token_id": "79390...abc",
      "condition_id": "0xabc123...",
      "outcome": "Yes",
      "pos_yes": "250.00",
      "pos_no": "0.00",
      "avg_entry_price": "0.5750",
      "pnl_unrealized": "1.75",
      "as_of_ts": 1748476800000000000
    },
    {
      "token_id": "84521...def",
      "condition_id": "0xdef456...",
      "outcome": "No",
      "pos_yes": "0.00",
      "pos_no": "100.00",
      "avg_entry_price": "0.4200",
      "pnl_unrealized": "-0.50",
      "as_of_ts": 1748476800000000000
    }
  ],
  "total_pnl_unrealized": "1.25",
  "as_of_ts": 1748476800000000000
}
```

### GET /orderbook/{token_id}

```
curl http://localhost:8080/orderbook/79390...abc
```

```json
{
  "token_id": "79390...abc",
  "condition_id": "0xabc123...",
  "outcome": "Yes",
  "bids": [
    {"price": "0.5800", "size": "1000.00"},
    {"price": "0.5750", "size": "500.00"},
    {"price": "0.5700", "size": "2000.00"}
  ],
  "asks": [
    {"price": "0.5850", "size": "800.00"},
    {"price": "0.5900", "size": "1200.00"},
    {"price": "0.5950", "size": "600.00"}
  ],
  "mid_price": "0.5825",
  "spread": "0.0050",
  "event_ts": 1748476799000000000,
  "data_source_ts": 1748476799100000000,
  "ingestion_ts": 1748476799200000000,
  "as_of_ts": 1748476800000000000
}
```

### GET /risk/rejects?limit=3

```
curl "http://localhost:8080/risk/rejects?limit=3"
```

```json
{
  "limit": 3,
  "count": 3,
  "reject_code_distribution": {
    "STALE_BOOK": 5,
    "SIZE_EXCEEDS_CAP": 2,
    "INVALID_INTENT": 1
  },
  "rejects": [
    {
      "seq": 42,
      "reject_code": "STALE_BOOK",
      "token_id": "79390...abc",
      "sub_reason": null,
      "as_of_ts": 1748476798000000000
    },
    {
      "seq": 41,
      "reject_code": "INVALID_INTENT",
      "token_id": "84521...def",
      "sub_reason": "BOOK_SNAPSHOT_ZERO",
      "as_of_ts": 1748476797000000000
    },
    {
      "seq": 40,
      "reject_code": "SIZE_EXCEEDS_CAP",
      "token_id": "11223...ghi",
      "sub_reason": null,
      "as_of_ts": 1748476795000000000
    }
  ],
  "as_of_ts": 1748476800000000000
}
```

### GET /audit/recent?limit=2

```
curl "http://localhost:8080/audit/recent?limit=2"
```

```json
{
  "limit": 2,
  "count": 2,
  "events": [
    {
      "audit_id": "01HZX...",
      "event_type": "AET_ORDER_PLACED",
      "token_id": "79390...abc",
      "outcome": "Yes",
      "price": "0.5820",
      "size": "250.00",
      "event_ts": 1748476799000000000,
      "data_source_ts": 1748476799100000000,
      "ingestion_ts": 1748476799200000000,
      "as_of_ts": 1748476800000000000
    },
    {
      "audit_id": "01HZW...",
      "event_type": "AET_RISK_REJECT",
      "token_id": "84521...def",
      "reject_code": "STALE_BOOK",
      "event_ts": 1748476798000000000,
      "data_source_ts": 1748476798050000000,
      "ingestion_ts": 1748476798100000000,
      "as_of_ts": 1748476800000000000
    }
  ],
  "as_of_ts": 1748476800000000000
}
```

### GET /metrics

```
curl http://localhost:8080/metrics
```

```
# HELP stcpp_signals_active_total Current active signals count
# TYPE stcpp_signals_active_total gauge
stcpp_signals_active_total 12

# HELP stcpp_rm_rejects_total RM reject count by code
# TYPE stcpp_rm_rejects_total counter
stcpp_rm_rejects_total{code="STALE_BOOK"} 5
stcpp_rm_rejects_total{code="SIZE_EXCEEDS_CAP"} 2

# HELP stcpp_wss_connected WSS connection status (1=connected, 0=disconnected)
# TYPE stcpp_wss_connected gauge
stcpp_wss_connected{channel="sports_api"} 1
stcpp_wss_connected{channel="clob"} 1
stcpp_wss_connected{channel="user_channel"} 0

# HELP stcpp_uptime_seconds Process uptime in seconds
# TYPE stcpp_uptime_seconds counter
stcpp_uptime_seconds 3612
```

### POST /drain

```
curl -X POST http://localhost:8080/drain \
  -H "Content-Type: application/json" \
  -d '{"reason": "manual test 2026-05-29"}'
```

成功 (200):
```json
{
  "ok": true,
  "prev_state": "RUNNING",
  "new_state": "DRAIN",
  "reason": "manual test 2026-05-29",
  "as_of_ts": 1748476800000000000
}
```

冲突 (409):
```json
{
  "ok": false,
  "error": "ALREADY_DRAIN",
  "current_state": "DRAIN",
  "as_of_ts": 1748476800000000000
}
```

### POST /resume

```
curl -X POST http://localhost:8080/resume \
  -H "Content-Type: application/json" \
  -d '{}'
```

成功 (200):
```json
{
  "ok": true,
  "prev_state": "DRAIN",
  "new_state": "RUNNING",
  "as_of_ts": 1748476800000000000
}
```

---

## §7 测试 Spec

### 7.1 curl 冒烟测试 (每个 endpoint 一个)

每个 case 验证: HTTP 200 + Content-Type JSON + 关键字段存在.

```bash
BASE="http://localhost:8080"

# T-01 /healthz
curl -sf "$BASE/healthz" | jq '.ok == true'

# T-02 /version
curl -sf "$BASE/version" | jq '.build_mode | IN("paper","live","backtest")'

# T-03 /status
curl -sf "$BASE/status" | jq '.state | IN("RUNNING","DRAIN","HALTED")'

# T-04 /signals/active
curl -sf "$BASE/signals/active" | jq '.count >= 0'

# T-05 /signals/history limit
curl -sf "$BASE/signals/history?limit=5" | jq '.limit == 5'

# T-06 /positions
curl -sf "$BASE/positions" | jq '.mode | IN("paper","live","backtest")'

# T-07 /orderbook (需要已知 token_id, 集成测试传 mock)
curl -sf "$BASE/orderbook/mock_token_001" | jq '.token_id == "mock_token_001"'

# T-08 /risk/rejects
curl -sf "$BASE/risk/rejects?limit=10" | jq '.limit == 10'

# T-09 /audit/recent
curl -sf "$BASE/audit/recent?limit=5" | jq '.count >= 0'

# T-10 /metrics (Prometheus text format)
curl -sf "$BASE/metrics" | grep "stcpp_uptime_seconds"

# T-11 POST /drain → POST /resume round-trip
curl -sf -X POST "$BASE/drain" -H "Content-Type: application/json" \
  -d '{"reason":"test"}' | jq '.ok == true'
curl -sf -X POST "$BASE/resume" -H "Content-Type: application/json" \
  -d '{}' | jq '.ok == true'
curl -sf "$BASE/status" | jq '.state == "RUNNING"'
```

### 7.2 ctest Integration Test

集成测试模块: `tests/integration/debug_api/`

| 测试 case | 内容 | owner |
|---|---|---|
| `test_api_server_startup` | HTTP server 启动 + /healthz 200 < 500ms | 老周 |
| `test_all_endpoints_concurrent` | 12 endpoint 各 5 并发请求 → 无 5xx, 无 dead lock | 老周 |
| `test_drain_resume_idempotent` | drain × 2 → 第二次 409; resume → 200; resume × 2 → 第二次 409 | 老韩 |
| `test_4ts_monotonic` | 所有 endpoint 的 4-ts 字段满足 R-20 不等式 | 老唐 |
| `test_signals_limit_clamp` | `?limit=999` → 实际返回 ≤ 200 (上限 clamp) | 小卢 |
| `test_orderbook_not_found` | 未知 token_id → 404 + JSON error body | 小袁 |

### 7.3 Chaos Test

目标: API server 崩溃不影响 hot path (R-12 隔离验证)

| 场景 | 验证方式 |
|---|---|
| API server 线程 panic / kill | hot path 继续运行; RM::evaluate p99 不变化; WAL 不停写 |
| 高并发 GET /orderbook 100 rps | vCPU0/1/2 CPU 占用不升; 无 lock contention |
| POST /drain 后立即 high-freq signal | RM 正确拒单 (不因 DRAIN 导致 race); audit ring 正常 emit |
| API server OOM (模拟 JSON 响应堆分配失败) | fallback: HTTP 503; hot path 不受影响 |

---

## §8 与现有 ADR 联动

| ADR | 影响 | API server 落地方式 |
|---|---|---|
| ADR-004 RM 短路 | GET /status + /risk/rejects 读 RM state | RM 暴露 `RmDebugSnapshot` 结构体, `std::atomic<RmDebugSnapshot*>` RCU; API 读 snapshot, 绝不调用 evaluate() |
| ADR-011 paper/live 隔离 | API 响应带 `"mode"` 字段 | binary 编译期 `STCPP_MODE` 常量注入; 响应体不会混用 paper/live 数据 |
| R-12 hot path 100us | API server 独立线程 | `httplib::Server` 在独立 `std::thread` 运行; 与 vCPU0/1/2 共享的唯一数据 = atomic snapshot 指针 |
| R-20 4-ts | 所有数据时间戳 | 每个 snapshot struct 含 `as_of_ts`; 来自 orderbook/signal 的 `event_ts / data_source_ts / ingestion_ts` 透传, 不用 `now()` 替代 |
| ADR-013 v2 (us-east-1 部署) | API server 绑定地址 | `server.listen("0.0.0.0", 8080)` — paper 阶段本地 curl; 公网部署时加 bearer token + 443 reverse proxy (M5+ 前处理) |
| ADR-018 build switch | API server 是可选模块 | CMake option: `STCPP_BUILD_DEBUG_API=ON` (default ON in paper/backtest; OFF 可选在 live production hardened); paper/live/backtest 三 mode 均可开 |
| ADR-015 vCPU pin | API server vCPU 分配 | paper 阶段: 不强 pin, 独立 std::thread; M5+ 前: 老姜 7-vCPU pin 压测后固定 vCPU6 |

---

## §9 Sprint-3 排期

| 里程碑 | 时间 | 任务 | owner |
|---|---|---|---|
| **Spec 闭环** | W8 W5 (2026-05-30 EOD) | §3 HTTP server 选型 GM ack + §4 endpoint 列表老高 ABI lock + 小郑 Prometheus 格式对齐 | 老周 + 老高 + 小郑 |
| **HTTP skeleton** | W9 W1 | `src/stcpp/debug_api/` 目录建立; httplib::Server 骨架; /healthz /version /status 跑通 | 小卢 (老周 review) |
| **RM + signal endpoint** | W9 W2-W3 | /signals/active + /signals/history (小卢); /status + /risk/rejects + /drain + /resume (老韩) | 小卢 / 老韩 |
| **orderbook + audit** | W10 W1 | /orderbook/{token_id} (小袁); /audit/recent (老唐) | 小袁 / 老唐 |
| **Prometheus /metrics** | W10 W2 | /metrics Prometheus text format 接小郑已有 exposer | 小郑 |
| **Integration test** | W10 W3 | ctest 6 case + chaos test | 老周 + 老唐 |
| **与 paper runtime 同启** | W11 W1 | API server 作为 paper binary 启动流程的一部分; CMake BUILD_DEBUG_API=ON default | 老周 + 老吴 |

---

## §10 派单 Backlog (Spec ack 后)

以下派单需老雷 GM ack spec 后, 老周转各主管下发:

| 接收方 | 任务 | 截止 | 依赖 |
|---|---|---|---|
| **小卢 W9** | /signals/active + /signals/history 实施; Signal Engine 暴露 `SignalDebugSnapshot` atomic | W9 W3 | 小卢 Signal Engine W8 完成 |
| **老韩 W9** | /status + /risk/rejects + POST /drain + POST /resume; RM 暴露 `RmDebugSnapshot` RCU | W9 W3 | RM v0.3.1 稳定 |
| **小袁 W9** | /orderbook/{token_id}; OrderBookMirror 暴露 snapshot() 接口 | W9 W3 | 小袁 microstructure-v1 |
| **老唐 W10** | /audit/recent; AuditEmitter ring 暴露 tail_copy(N) 接口 | W10 W1 | 老唐 v1.1 ring 落码 |
| **小郑 W10** | /metrics Prometheus text format; 对齐已有 Prometheus exposer | W10 W2 | 小郑 Sprint-3 observability 立项 |
| **老高 W8 W5** | API ABI lock: endpoint + schema grep; 确认无破坏现有 ADR ABI | W8 W5 EOD | 本 spec v1 |
| **小苏 W10+** | 前端 UI 可视化 (调用本 API); /status /positions /signals 面板 | W10+ | 本 API W10 集成测试通过 |

---

## §11 不耻下问 (Open Questions + 确认项)

| # | 问谁 | 问题 | 截止 |
|---|---|---|---|
| OQ-1 | @老雷 GM | final ack: HTTP server 选型 (cpp-httplib 推荐) + endpoint 范围 (12 个) | W8 W5 EOD |
| OQ-2 | @老高 | W8 W5 ABI lock: 本 spec endpoint path + JSON schema 是否与现有模块 ABI 冲突; grep 已有 struct 字段名 | W8 W5 EOD |
| OQ-3 | @小郑 | W8 W5: Prometheus /metrics 已有 exposer 框架; API server 是复用 exposer 输出还是独立实现 text format? | W8 W5 EOD |
| OQ-4 | @小赵 | JSON 序列化: 出站 debug JSON 用 nlohmann/json 还是 glaze? glaze 落地状态? | W9 W1 前 |
| OQ-5 | @小卢 | W9: Signal Engine 暴露什么样的 snapshot 接口? `SignalDebugSnapshot` struct 字段列表确认 | W9 W1 前 |
| OQ-6 | @老韩 | W9: `RmDebugSnapshot` struct 字段列表 + DRAIN state machine 前置条件 (resume 需要什么条件才能成功?) | W9 W1 前 |
| OQ-7 | @小袁 | W9: `OrderBookMirror::snapshot()` 返回 struct 字段 — 与本 spec §6 /orderbook 示例是否对齐? | W9 W1 前 |
| OQ-8 | @老唐 | W10: AuditEmitter ring `tail_copy(N)` 接口设计; 返回 `std::vector<AuditEvent>` 还是 span? | W10 W1 前 |

---

**最后更新:** 2026-05-29 by 老周
