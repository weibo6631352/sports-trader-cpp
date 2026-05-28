# REST API Skeleton 实施 Spec v1

- Owner: 小卢 (senior-ic-pool)
- Date: 2026-05-29
- Status: DRAFT v1 — 待老周 ADR-027 layer-1 review + 老雷 GM ack
- 关联:
  - `laozhou-w8-debug-rest-api-spec-v1.md` (老周 架构 spec, SSOT)
  - `CMakeLists.txt` (ExternalProject + ADR-018 build switch)
  - `ADR/2026-06-W3-adr-018-lib-selection.md` (cpp-httplib v0.16.3 已落地)
  - `ADR/2026-06-01-adr-015-vcpu-pin.md` (vCPU 分工)
  - `ADR/2026-05-28-gm-redline-websocket-non-blocking.md` (R-12)
  - `ADR/2026-05-28-gm-redline-data-source-timestamping.md` (R-20)

---

## §1 实施 Scope (W9 W2-W3)

### 1.1 第 1 阶段 — W9 W2 (小卢 IC 实施, ≤ 600 行 C++)

目标: HTTP server skeleton 可独立启动, 3 read-only endpoint 通过 curl 冒烟.

| 交付物 | 内容 |
|---|---|
| `src/stcpp/debug_api/` 目录 | CMakeLists.txt + server.hpp/cpp + 3 endpoint cpp |
| `HttpServer` class | cpp-httplib FetchContent v0.16.3, blocking model, 独立 `std::thread` |
| GET /healthz | 200 OK + JSON `{"ok":true,...}` |
| GET /version | 200 OK + JSON `{"version":"0.1.0","mode":"paper","build_ts":"..."}` |
| GET /status | 200 OK + JSON `{"state":"RUNNING","mode":"paper","wss_connected":{...},"uptime_sec":N}` |
| glaze JSON 序列化 | 出站 JSON 走 glaze (公司栈, 老周 spec §2.2 glaze 优先); 若 glaze 未落地降级 nlohmann/json |
| ctest integration T1-T5 | 见 §6; W9 W2 末 ctest 期望 458 + 5 = **463** |

约束:
- server 独立 vCPU 启动, 不占 vCPU0/1/2 hot path (ADR-015)
- 所有响应体含 `as_of_ts` (R-20; 值用 `std::chrono::system_clock::now()` epoch ns, 非数据源 ts — debug endpoint 无上游 ts, 符合 R-20 语义: as_of_ts 是读取快照的本地时刻)
- /version 的 `build_ts` 来自 CMake 编译期 `__DATE__`/`__TIME__` 注入, 非 `now()` (不违反 R-20: build_ts 是构建常量, 不是数据时间戳)

### 1.2 第 2 阶段 — W9 W3 (小卢 扩展)

目标: 接入跨模块 read API, 完成 6 endpoint 全集.

| 交付物 | 依赖方 | 数据来源 |
|---|---|---|
| GET /positions | 老韩 PositionLedger read API v0.5 | PositionLedger RCU snapshot |
| GET /risk/rejects | 老韩 RM ring buffer | `SpscRingSnapshot<RejectEvent, 512>` tail copy |
| POST /drain | 老韩 SystemState CAS | `std::atomic<SystemState>` CAS transition |
| POST /resume | 老韩 SystemState CAS (前置条件 DRAIN → RUNNING) | 同上 |
| GET /signals/active | 小卢 Signal Engine atomic snapshot | `std::atomic<SignalSnapshotPtr>` RCU swap |
| GET /signals/history | 小卢 Signal history SPSC ring | `SpscRingSnapshot<SignalEvent, 1024>` tail copy |

注: /signals 两个 endpoint 由小卢自己实施 (Signal Engine 是小卢 W8 交付物, 接口在本人掌控中). /positions /risk/rejects /drain /resume 依赖老韩接口 — W9 W3 Mon 前向老韩确认 `RmDebugSnapshot` + PositionLedger 读 API (见 §9 OQ-6).

---

## §2 文件结构

```
src/stcpp/debug_api/
  CMakeLists.txt                   — debug_api 子模块构建定义
  server.hpp                       — HttpServer class 声明
  server.cpp                       — HttpServer class 实现 (start/stop/注册 handler)
  endpoint_healthz.cpp             — GET /healthz handler 实现
  endpoint_version.cpp             — GET /version handler 实现 (含 STCPP_EXEC_MODE)
  endpoint_status.cpp              — GET /status handler 实现
  endpoint_positions.cpp           — GET /positions handler (W9 W3, 依赖老韩接口)
  endpoint_signals.cpp             — GET /signals/active + /signals/history (W9 W3)
  endpoint_drain.cpp               — POST /drain + POST /resume (W9 W3, CAS 逻辑)

tests/integration/
  debug_api/
    test_debug_api_integration.cpp — curl + ctest 5 case (W9 W2)
    CMakeLists.txt                 — 新增子目录, 接入 tests/integration/CMakeLists.txt

根 CMakeLists.txt 新增:
  option(STCPP_BUILD_DEBUG_API ...) — ADR-018 build switch (§5)
```

注: endpoint_signals.cpp 合并 /signals/active 和 /signals/history 两个 handler, 因为两者共享 Signal Engine 依赖, 单文件避免跨文件重复 include.

---

## §3 HttpServer Class

### 3.1 声明 (server.hpp)

```cpp
// src/stcpp/debug_api/server.hpp
// Owner: 小卢  W9 W2
// 关联: laozhou-w8-debug-rest-api-spec-v1.md §3 + §5

#pragma once
#include <httplib.h>
#include <atomic>
#include <cstdint>
#include <thread>

namespace stcpp::debug_api {

class HttpServer {
public:
    explicit HttpServer(uint16_t port);
    ~HttpServer();

    // 在独立 std::thread 启动 httplib::Server::listen() (blocking).
    // 调用方不阻塞: 方法返回时 server 线程已 detach/join 策略见 stop().
    // 必须在 hot-path 线程 (vCPU0/1/2) 之外调用.
    void start();

    // 触发 httplib::Server::stop(), join server_thread_.
    // 析构时自动调用 (RAII).
    void stop();

    bool is_running() const noexcept;

private:
    void register_handlers();

    httplib::Server    server_;
    std::thread        server_thread_;
    std::atomic<bool>  running_{false};
    uint16_t           port_;
};

} // namespace stcpp::debug_api
```

关键设计决策:
- `httplib::Server` 是 blocking model (官方 README 注明 "uses blocking socket I/O"). 运行在独立 `std::thread`, 不阻塞 hot path.
- `running_` 用 `std::atomic<bool>`, `is_running()` 供 watchdog 和 /healthz 使用, wait-free read.
- 析构函数调用 `stop()` — RAII, 防止 server 线程泄漏 (ADR R-12: 线程孤立 = P0).
- `register_handlers()` 私有, 在 `start()` 内调用, 绑定所有 endpoint lambda.

### 3.2 线程安全约定

server 线程与 hot path 的唯一共享点是 atomic snapshot 指针或 atomic 值 (见 §7). server.cpp 内部不持有任何锁. 读 PositionLedger / RM state 通过模块暴露的 atomic snapshot 接口完成.

---

## §4 Endpoint 实现细节

### 4.1 GET /healthz

**文件**: `endpoint_healthz.cpp`

响应体 (老周 spec §6):
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

实现要点:
- `uptime_sec` 从 `HttpServer` 构造时记录的 `start_time_` 计算 (非运行时 REST 查询, 不依赖外部).
- thread heartbeat 机制: W9 W2 先用 stub `"alive"` 字符串; W10+ 接各模块 watchdog atomic. 字段名与老周 spec §6 精确匹配 (下游 curl 脚本硬编码 `.ok == true`).
- HTTP status: 200 OK.
- Content-Type: `application/json; charset=utf-8`.

### 4.2 GET /version

**文件**: `endpoint_version.cpp`

响应体:
```json
{
  "version": "0.1.0",
  "git_hash": "ebb25e8",
  "build_mode": "paper",
  "build_time": "2026-05-29T00:00:00Z",
  "cpp_standard": "C++20",
  "as_of_ts": 1748476800000000000
}
```

实现要点:
- `build_mode` 来自编译期 `STCPP_EXEC_MODE` CMake define → C++ 宏 `STCPP_EXEC_MODE_STR` (字符串化). 三值: `"paper"` / `"live"` / `"backtest"`.
- `git_hash` 来自 CMake configure-time `git rev-parse --short HEAD` 写入编译期常量 (小卢在 `debug_api/CMakeLists.txt` 中加 `execute_process` + `configure_file` 生成 `version_generated.hpp`).
- `build_time` 来自 `__DATE__` + `__TIME__` 拼接 (CMake 注入或 C++ 宏直接使用), 编译期常量, 不违反 R-20 (不是数据时间戳).
- `version` 来自顶层 `project(stcpp VERSION 0.1.0)`, CMake 变量 `${PROJECT_VERSION}` 注入.

### 4.3 GET /status

**文件**: `endpoint_status.cpp`

响应体:
```json
{
  "state": "RUNNING",
  "mode": "paper",
  "wss_connected": {
    "sports_api": true,
    "clob": true,
    "user_channel": false
  },
  "signals_active_count": 0,
  "positions_count": 0,
  "rm_rejects_last_60s": 0,
  "uptime_sec": 3612,
  "as_of_ts": 1748476800000000000
}
```

实现要点:
- W9 W2 阶段: `state` = `"RUNNING"` (stub, 无 RM SystemState atomic 接入). `wss_connected` 全 false. `signals_active_count` / `positions_count` / `rm_rejects_last_60s` = 0. stub 字段在 W9 W3 接老韩 `RmDebugSnapshot`.
- 字段 `state` 枚举值精确匹配老周 spec §6: `"RUNNING"` / `"DRAIN"` / `"HALTED"`.
- `mode` 字段同 /version, 来自 `STCPP_EXEC_MODE` 编译期常量.
- `as_of_ts` = `now()` epoch ns (本地时刻, 不是数据源 ts — 状态快照的读取时刻, 符合 R-20).

### 4.4 GET /positions (W9 W3)

**文件**: `endpoint_positions.cpp`

响应体结构见老周 spec §6 /positions 示例.

实现要点:
- 依赖老韩 PositionLedger RCU snapshot 接口 (`PositionLedger::snapshot()` 或等价 API).
- 响应体含 `mode` 字段 (paper/live 标注, ADR-011).
- 每条 position 含 `as_of_ts` (来自 snapshot 的 `as_of_ts`, 透传, 不用 `now()` 替代 — 满足 R-20).
- 若 PositionLedger 接口 W9 W3 前未就绪: 返回空数组 + `"count": 0` + 当前 `as_of_ts` stub.

### 4.5 GET /signals/active + GET /signals/history (W9 W3)

**文件**: `endpoint_signals.cpp`

实现要点:
- `/signals/active`: 读 `std::atomic<SignalSnapshotPtr>` RCU swap (小卢 Signal Engine 接口, W8 已交付).
- `/signals/history?limit=N`: 读 `SpscRingSnapshot<SignalEvent, 1024>` tail copy. 参数 `limit` 上限 clamp 至 200 (老周 spec §4 + ctest T5).
- 两 handler 共享 `SignalEngine` 依赖指针, 由 `HttpServer` 构造时注入 (`HttpServer(uint16_t port, SignalEngine* se, ...)`). W9 W3 前 se 可为 nullptr — 此时返回 `{"count": 0, "signals": [], "as_of_ts": ...}`.
- 4-ts 字段全部透传 Signal Engine 的原始 ts, 不用 `now()` 替代 (R-20).

### 4.6 POST /drain + POST /resume (W9 W3)

**文件**: `endpoint_drain.cpp`

实现要点:
- `POST /drain`: CAS `SystemState::RUNNING → SystemState::DRAIN`. 成功 200; 已在 DRAIN/HALTED 返回 409 + 当前 state.
- `POST /resume`: CAS `SystemState::DRAIN → SystemState::RUNNING`. 成功 200; 不在 DRAIN 返回 409.
- body 解析: `POST /drain` 期望 `{"reason": "string"}`. 用 simdjson on-demand 解析 (ADR-018, 老周 spec §2.2). 解析失败不拒绝请求 — reason 默认空串 (debug 接口, 无严格 schema 验证).
- `SystemState` atomic 由老韩模块暴露, 以引用/指针注入 `HttpServer`. W9 W3 前若接口未就绪: drain 返回 200 stub (操作无实际效果), 但 dry-run 在日志打印 reason.
- `as_of_ts` = `now()` epoch ns (状态机写入时刻, 符合 R-20).

---

## §5 ADR-018 Build Switch

顶层 `CMakeLists.txt` 新增 (在现有 `add_subdirectory` 末尾, `message(STATUS ...)` 之前):

```cmake
# Sprint-3 W9: Debug REST API server (老周 spec, 小卢 IC 实施)
# ADR-018 + ADR-027 layer 1 approve 后生效
option(STCPP_BUILD_DEBUG_API "Build debug REST API server (cpp-httplib, read-only debug)" ON)
if(STCPP_BUILD_DEBUG_API)
    add_subdirectory(src/stcpp/debug_api)
endif()
```

语义:
- 默认 `ON` — paper / live / backtest 三 mode 均可启用 (老周 spec §8 ADR-018).
- production hardened 部署可 `cmake -DSTCPP_BUILD_DEBUG_API=OFF` 关闭 (无 auth 的 debug 接口在公网不应暴露).
- 不影响现有 23 个 `add_subdirectory` 顺序和依赖.

`src/stcpp/debug_api/CMakeLists.txt` 结构:
```cmake
# debug_api 子模块 CMakeLists.txt
# FetchContent cpp-httplib v0.16.3 — 已在 ADR-018 落地, 此处直接 find/link
# (若 FetchContent 在顶层 polymarket 子模块已声明, 直接 FetchContent_MakeAvailable 不重复拉取)

add_library(stcpp_debug_api STATIC
    server.cpp
    endpoint_healthz.cpp
    endpoint_version.cpp
    endpoint_status.cpp
    # W9 W3 加入:
    # endpoint_positions.cpp
    # endpoint_signals.cpp
    # endpoint_drain.cpp
)

target_include_directories(stcpp_debug_api PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}
)

# cpp-httplib: header-only, FetchContent 已在 ADR-018 配置
# 链接方式: INTERFACE target httplib::httplib (或等价名称, 确认老李 polymarket 子模块已声明)
target_link_libraries(stcpp_debug_api PUBLIC httplib::httplib)

target_compile_features(stcpp_debug_api PUBLIC cxx_std_20)
target_compile_definitions(stcpp_debug_api PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)
```

注意: cpp-httplib `httplib::Server` blocking model 在 debug_api 独立线程内运行, 不影响 CMake 静态库的 thread-safety 约束.

---

## §6 测试 Spec

### 6.1 W9 W2 ctest Integration 5 Case

**文件**: `tests/integration/debug_api/test_debug_api_integration.cpp`

| ID | 测试名 | 内容 | 期望 |
|---|---|---|---|
| T1 | `DebugApiServer_Healthz_Returns200` | 在测试 binary 内启动 HttpServer(18080) + curl /healthz | HTTP 200 + `ok == true` + `as_of_ts > 0` |
| T2 | `DebugApiServer_Version_Fields` | curl /version | HTTP 200 + `version` 字段存在 + `build_mode` in {"paper","live","backtest"} |
| T3 | `DebugApiServer_Status_PaperMode` | curl /status (paper build) | HTTP 200 + `state` in {"RUNNING","DRAIN","HALTED"} + `mode == "paper"` |
| T4 | `DebugApiServer_HotpathIsolation_ServerStop` | stop HttpServer → 主线程继续执行 100ms 无 hang | 主线程存活, 无 deadlock (hot path 不受 server 停止影响) |
| T5 | `DebugApiServer_Concurrent100_NocrashNoHang` | 100 个 std::thread 并发 GET /healthz | 全部请求有响应 (200 or connection reset), server 不 crash, 进程不 OOM |

curl 调用方式: 测试内用 `popen("curl -sf http://localhost:18080/healthz", "r")` 或 cpp-httplib Client 发起 (client 已在项目中, ADR-018). 两种方式均可, 优先 httplib::Client (避免 shell 外部依赖).

**新增 CMakeLists.txt** (`tests/integration/debug_api/CMakeLists.txt`):
```cmake
# tests/integration/debug_api — debug API integration test (W9 W2)
# 仅 paper mode (R-7 物理隔离; debug API 在 live hardened 部署可 OFF)
if(NOT STCPP_EXEC_MODE STREQUAL "paper")
    return()
endif()
if(NOT STCPP_BUILD_DEBUG_API)
    return()
endif()

add_executable(stcpp_test_integration_debug_api
    test_debug_api_integration.cpp
)
target_link_libraries(stcpp_test_integration_debug_api PRIVATE
    GTest::gtest_main
    stcpp_debug_api
)
target_compile_features(stcpp_test_integration_debug_api PRIVATE cxx_std_20)
target_compile_options(stcpp_test_integration_debug_api PRIVATE
    -Wno-double-promotion
    -Wno-old-style-cast
    -Wno-cast-align
)
target_include_directories(stcpp_test_integration_debug_api PRIVATE
    ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/include
)
target_compile_definitions(stcpp_test_integration_debug_api PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)
gtest_discover_tests(stcpp_test_integration_debug_api
    DISCOVERY_TIMEOUT 120
    PROPERTIES LABELS "integration;debug-api;w9"
)
```

`tests/integration/CMakeLists.txt` 末尾追加:
```cmake
# W9 W2: debug REST API integration (小卢)
if(STCPP_BUILD_DEBUG_API)
    add_subdirectory(debug_api)
endif()
```

### 6.2 ctest 数量基线

W9 W2 末 ctest 期望: **458 (存量) + 5 (新增 T1-T5) = 463**

存量 458 来源 (paper mode gtest_discover_tests 注册 case 数, 非 executable 数):
- unit/ 各 executable gtest case 累计
- sim/ r12_sim
- integration/ paper_e2e / audit_chain / r11_pollution

5 个新 case 对应 T1-T5, 每个 TEST() 宏对应 1 个 ctest case.

### 6.3 后续 Chaos Test (W9 W3 末 + W10 集成测试)

| 场景 | 验证方式 | owner |
|---|---|---|
| API server 线程强制 stop | hot path 继续运行 100ms; RM evaluate p99 不变化 | 小卢 |
| 100 rps GET /healthz 连续 10s | vCPU0/1/2 CPU 占用不升; 无 lock contention (用 TSAN build 验证) | 小卢 |
| POST /drain 后立即 10 个并发信号 | RM 正确拒单; 无 race on SystemState atomic | 老韩 |
| API server OOM (限制 malloc) | fallback HTTP 503; hot path 不受影响 | 老周 review |

Chaos test 不计入 W9 W2 ctest 基线 (W10 W3 老周 integration test 专项).

---

## §7 Hot Path 安全约束

### 7.1 线程隔离

- `HttpServer::start()` 在调用方 main() 或 paper main() 中调用一次, 在独立 `std::thread server_thread_` 运行 `server_.listen("0.0.0.0", port)`.
- vCPU0 (Ingest) / vCPU1 (Signal) / vCPU2 (Risk+Audit) 绝对不调用 `HttpServer` 任何接口.
- paper 阶段不强 pin vCPU; M5+ live 前老姜 7-vCPU pin 压测后固定 API server 到 vCPU6 (ADR-015).

### 7.2 读 State 机制 (W9 W3 接入时)

每个 GET endpoint 读对应模块的 atomic snapshot, 不持锁进入热路径:

| Endpoint | 读机制 | 禁止 |
|---|---|---|
| /status | `std::atomic<SystemStateSnapshot>` (16B 内联) | 禁止调用 RM::evaluate() |
| /signals/active | `std::atomic<SignalSnapshotPtr>` RCU swap | 禁止持 SignalEngine mutex |
| /signals/history | `SpscRingSnapshot<SignalEvent, 1024>` tail copy | 禁止阻塞 push 端 |
| /positions | PositionLedger RCU snapshot (老韩接口) | 禁止写 position |
| /risk/rejects | `SpscRingSnapshot<RejectEvent, 512>` tail copy | 禁止调用 evaluate() |

读机制由各模块 owner 实现并暴露; `HttpServer` 仅读 atomic 指针或 copy snapshot, 不持任何锁.

### 7.3 写接口安全 (POST /drain + /resume)

通过 `std::atomic<SystemState>` CAS, 不进入 hot path 数据结构:

```
// 伪代码 (老韩 W9 实施, 此处仅 spec 描述)
SystemState expected = SystemState::RUNNING;
bool ok = system_state_.compare_exchange_strong(
    expected, SystemState::DRAIN,
    std::memory_order_acq_rel
);
// ok == false → 409 + current state (expected 已被 CAS 更新为实际值)
// ok == true  → 200 + prev_state=RUNNING, new_state=DRAIN
```

CAS 失败路径: 返回 409 Conflict + `{"ok":false,"error":"ALREADY_DRAIN","current_state":"DRAIN","as_of_ts":...}`.

---

## §8 Timeline

| 时间 | 里程碑 | 交付物 | owner |
|---|---|---|---|
| W9 W2 Mon | spec final + 老周 ADR-027 layer-1 review | 本文档 v1 → v1.1 (review 意见吸收) | 小卢 + 老周 |
| W9 W2 Tue | `src/stcpp/debug_api/` 目录 + CMakeLists.txt + server.hpp/cpp 骨架 | 能 cmake build, server 启动不 crash | 小卢 |
| W9 W2 Wed | endpoint_healthz + endpoint_version + endpoint_status | 3 endpoint curl 冒烟 pass | 小卢 |
| W9 W2 Thu | test_debug_api_integration.cpp T1-T5 + CMakeLists | ctest -R debug_api 5 passed | 小卢 |
| W9 W2 Fri | ctest paper mode 全量跑 | 463 passed, 0 failed | 小卢 |
| W9 W3 Mon | 向老韩确认 RmDebugSnapshot + PositionLedger 读 API (OQ-6) | @老韩 接口签名确认 | 小卢 → 老韩 |
| W9 W3 Tue | endpoint_positions + endpoint_drain (接老韩接口) | /positions curl + /drain round-trip pass | 小卢 |
| W9 W3 Wed | endpoint_signals (接小卢 Signal Engine W8 接口) | /signals/active + /signals/history curl pass | 小卢 |
| W9 W3 Thu | integration test 全量 + curl 实测脚本 | 老周 curl 脚本全 pass | 小卢 |
| W9 W3 Fri | 老胡 PM ack + worktree PR merge | debug API debug 可用确认 | 小卢 → 老胡 |

---

## §9 不耻下问 (Open Questions)

| # | 问谁 | 问题 | 截止 |
|---|---|---|---|
| OQ-1 | @老周 | ADR-027 layer-1 架构一致性 review: HttpServer class 设计 + 文件结构与 v0.6-e2e 架构是否对齐 | W9 W2 Mon EOD |
| OQ-2 | @老韩 | PositionLedger read API 签名 + RmDebugSnapshot struct 字段列表; /drain 的 resume 前置条件 (需满足什么才能从 DRAIN 转回 RUNNING?) | W9 W3 Mon 前 |
| OQ-3 | @老唐 | /audit/recent endpoint (W10 W1 老唐交付); AuditEmitter ring `tail_copy(N)` 返回 `std::vector<AuditEvent>` 还是 span? | W10 W1 前 |
| OQ-4 | @小赵 | glaze 序列化落地状态? debug_api 出站 JSON 是否已可直接用 glaze? 若未落地小卢 W9 W2 用 nlohmann/json 先行 | W9 W2 Mon 前 |
| OQ-5 | @老高 | ABI lock check: debug_api 新增 `HttpServer` + 5 endpoint 路径是否与现有模块 ABI 字段名冲突 (老周 spec §10 OQ-2 对应) | W9 W2 前 |
| OQ-6 | @小郑 | /metrics Prometheus 格式 W9 W3 是否可提前接入? 已有 Prometheus exposer 框架; 若未就绪 W10 W2 小郑负责接 | W9 W3 前 |
| OQ-7 | @老雷 | GM ack: HTTP server 选型 (cpp-httplib 已 ADR-018 落地) + W9 W2-W3 debug_api 里程碑 | W9 W2 Mon |

---

**最后更新:** 2026-05-29 by 小卢
