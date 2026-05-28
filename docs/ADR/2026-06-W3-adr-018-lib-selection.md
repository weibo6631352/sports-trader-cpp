# ADR-018: HTTP/WSS/JSON Lib 选型决议

- **ID:** ADR-018
- **Date:** 2026-06-W3 (W6 Wave 30)
- **Status:** Accepted
- **Owner:** 老周 (cpp-chief-architect)
- **签字:** 老周 (A 主管拍板) — 老郭 架构评审 forward (M1 前)
- **波及:** 老李 (polymarket-client), 小冯 (pm-wss-subscriber), 老唐 (BLAKE3 FetchContent 模板参照)
- **关联:**
  - ADR-006 (W5 末 GM: HTTP client = cpp-httplib)
  - ADR-012 (WSS 拓扑 E 方案, paper 1×8 不动)
  - ADR-017 (SPSC ring framework, R-12 严守)
  - `src/stcpp/polymarket/wss/CMakeLists.txt` — BoostBeastTransport 占位 hook
  - `src/stcpp/observability/CMakeLists.txt` — BLAKE3 FetchContent 模板

---

## 0. 背景与冲突

| 冲突源 | 内容 |
|---|---|
| ADR-006 (W5 末 GM 拍) | HTTP client = **cpp-httplib** (header-only, 编译快) |
| 小冯 W5 Wave 24 WSS v0.1 | `ISpscEventSink` 抽象 + `BoostBeastTransport` 占位 hook, 待老周 ack |
| 老李 W6 Wave 29 handshake v1 | `SubscribeSportsWss` (F-14) 接口锁定, 传输层独立于接口 |
| 老姜 W6 Wave 28 bench_e2e | MockSPSC 待真 SPSC + 真 WSS transport 接入后才能给出跨洋延迟基线 |

三 lib 需要在同一 ADR 决议, 避免 W7 接入时出现重复讨论。

---

## 1. 决议

### 1.1 HTTP REST — cpp-httplib (ADR-006 确认沿用)

**决议: 沿用 ADR-006, cpp-httplib header-only。**

理由:
- 跨洋 REST 调用 (gamma/clob/data) 均在 worker pool 线程执行, 不在 WSS event loop (R-12 合规)。
- header-only 零编译单元增量, 与项目 `-Werror` 体系兼容 (SYSTEM include 隔离第三方 warning)。
- paper 阶段 REST 请求量低 (listing + orderbook poll), 性能无压力。
- TLS: OpenSSL/LibreSSL native 支持, macOS Security.framework 可用。
- 老李 polymarket-client v0.1 REST path 已在 `src/stcpp/polymarket/` 落地, W6 W3 直接用 FetchContent 拉入即可, 无 API 破坏。

**不用替代方案理由:**
- libcurl: 状态机 API 复杂, callback 风格与项目 C++20 同步接口不统一。
- Boost.Asio HTTP: 引入 boost 大依赖, 与下方 boost.beast WSS 不同 — REST 不值得引。

### 1.2 WSS — Boost.Beast

**决议: WSS transport = Boost.Beast。**

理由:
- 小冯 v0.1 `BoostBeastTransport` 占位 hook 已在 `src/stcpp/polymarket/wss/CMakeLists.txt` 预留, 接入成本最低。
- 老李 v3 ack + 老周 Wave 26 自承认: beast 是 WSS 最成熟的 C++ 选项 (header-heavy, 但功能完备)。
- ADR-012 paper 阶段 1 conn × 8 sub — beast 单 io_context + strand 完全满足。
- R-12 合规路径: beast async read → `SpscEventSink<WssEvent, 65536>::try_push` → 立即返回, event loop 无阻塞 IO。
- 跨洋 reconnect backoff (200ms RTT, 老陈实测) 需要 beast `async_connect` 支持, 同步库无法满足。

**不用替代方案理由:**
- cpp-httplib WebSocket: 同步模式, 无法满足 R-12 (WSS event loop 禁同步 IO); 异步模式尚在 experimental 状态。
- websocketpp: 已 7 年无重大维护 (last release 0.8.2, 2019); Boost.Asio 依赖版本与 beast 相同但 API 更旧。

**版本约束:** Boost ≥ 1.82 (beast coroutine 稳定 + ASIO standalone 可选)。使用 standalone Asio 模式避免拉入全量 boost。

### 1.3 JSON Parser — simdjson

**决议: JSON parser = simdjson。**

理由:
- Polymarket WSS payload + REST response 均为 JSON; simdjson 6 GB/s parse 速率在 vCPU0 Ingest Reactor 不构成 R-12 瓶颈。
- On-demand API (simdjson ≥ 3.x) 支持零拷贝惰性解析, 与 `WssEvent` 结构体填充模式匹配。
- `GIT_SHALLOW TRUE` FetchContent 拉 header + 少量 cpp, 与 rigtorp 模式一致。
- 线程安全: simdjson parser 实例 thread-local 或 per-call 构造均可, 不持全局状态。

**不用替代方案理由:**
- nlohmann/json: DOM 模式堆分配密集, R-12 热路径禁堆分配原则下不适用。
- rapidjson: API 陈旧, C++20 concepts/views 集成度低; simdjson 性能优势 3-5×。
- 小冯 v0.1 minimal parser: 只覆盖 sports channel 基础字段, 无法复用于 gamma/clob 多端 REST; 升 simdjson 是正确方向。

---

## 2. FetchContent 配置规范

三个 lib 均走 `FetchContent_Declare` + `FetchContent_MakeAvailable`, 与 rigtorp/BLAKE3 模式一致。无 vcpkg 依赖。

### 2.1 cpp-httplib (老李 W6 W3 接入)

落地位置: `src/stcpp/polymarket/CMakeLists.txt`

```cmake
include(FetchContent)
FetchContent_Declare(
    cpp_httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.16.3        # 稳定 tag, TLS 支持
    GIT_SHALLOW    TRUE
    SYSTEM
)
set(HTTPLIB_REQUIRE_OPENSSL OFF)   # macOS Security.framework 兜底
FetchContent_MakeAvailable(cpp_httplib)
# target: httplib::httplib (INTERFACE)
target_link_libraries(stcpp_polymarket_paper PRIVATE httplib::httplib)
target_link_libraries(stcpp_polymarket_live  PRIVATE httplib::httplib)
```

### 2.2 Boost.Beast (小冯 W7 接入)

落地位置: `src/stcpp/polymarket/wss/CMakeLists.txt` (替换占位 hook)

```cmake
include(FetchContent)
# Standalone Asio (不带完整 Boost, 减少依赖体积)
FetchContent_Declare(
    asio_standalone
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG        asio-1-30-2
    GIT_SHALLOW    TRUE
    SYSTEM
)
FetchContent_MakeAvailable(asio_standalone)

FetchContent_Declare(
    boost_beast
    GIT_REPOSITORY https://github.com/boostorg/beast.git
    GIT_TAG        boost-1.86.0
    GIT_SHALLOW    TRUE
    SYSTEM
)
FetchContent_MakeAvailable(boost_beast)

add_library(stcpp_polymarket_wss_beast STATIC
    beast_wss_transport.cpp   # BoostBeastTransport 真实现
)
target_compile_definitions(stcpp_polymarket_wss_beast PRIVATE
    ASIO_STANDALONE=1
    ASIO_NO_DEPRECATED=1
    BOOST_BEAST_USE_STD_STRING_VIEW=1
)
target_include_directories(stcpp_polymarket_wss_beast SYSTEM PRIVATE
    ${asio_standalone_SOURCE_DIR}/asio/include
    ${boost_beast_SOURCE_DIR}/include
)
target_link_libraries(stcpp_polymarket_wss_beast
    PUBLIC stcpp_polymarket_wss   # IWssTransport 抽象
    PUBLIC stcpp_infra_spsc       # SpscEventSink
)
```

### 2.3 simdjson (老李 + 小冯 W7 EOW 接入)

落地位置: `src/stcpp/infra/json/CMakeLists.txt` (新建 INTERFACE target)

```cmake
include(FetchContent)
FetchContent_Declare(
    simdjson
    GIT_REPOSITORY https://github.com/simdjson/simdjson.git
    GIT_TAG        v3.11.0       # 稳定 tag
    GIT_SHALLOW    TRUE
    SYSTEM
)
FetchContent_MakeAvailable(simdjson)
# simdjson 提供 simdjson::simdjson target
# 下游 link: target_link_libraries(... PUBLIC simdjson::simdjson)
```

顶层 `CMakeLists.txt` 加:
```cmake
# Sprint-2 W7: simdjson JSON parser (老李 REST + 小冯 WSS payload)
add_subdirectory(src/stcpp/infra/json)
```

---

## 3. 编译时间影响估算

| 库 | 模式 | 首次 FetchContent (git clone + cmake) | 增量编译 (cpp 有改动时) |
|---|---|---|---|
| cpp-httplib | header-only INTERFACE | ~10s (网络) + 0s cmake | ~0s (仅下游 TU 重编) |
| Boost.Beast | header-only INTERFACE (beast 本身无 cpp) | ~25s (git shallow) + ~5s cmake | ~0s |
| standalone Asio | header-only INTERFACE | ~8s (git shallow) | ~0s |
| simdjson | 2 cpp (simdjson.cpp amalgamation) | ~15s (网络) + ~12s 编译 | ~12s (simdjson.cpp 自身变更极少) |
| **合计新增** | — | **~60s 首次** | **~12s 增量 (simdjson only)** |

参照基线: rigtorp SPSC + BLAKE3 FetchContent 首次 ~45s。三 lib 合计首次约 +60s, 即全量冷构建从 ~4min 升至 ~5min (+25%)。增量构建对日常开发影响极小 (< 15s)。

**评估结论:** 在接受范围内。cpp-httplib 和 beast 均 header-only, 对每日增量 build 零影响。simdjson amalgamation 单 cpp 仅在库自身升级时重编。

---

## 4. 接入路径

| 时间节点 | 任务 | Owner | 依赖 |
|---|---|---|---|
| W6 W3 (本周) | cpp-httplib FetchContent 进 `src/stcpp/polymarket/CMakeLists.txt`; paper_pm_client REST path 换真接 | 老李 | ADR-018 本文 |
| W7 (下周) | boost.beast FetchContent; `BoostBeastTransport` 真实现替换占位; R-12 async read → SPSC try_push 落地 | 小冯 | 老周 ack (本文) |
| W7 EOW | simdjson FetchContent; WSS payload + REST response 解析替换 minimal parser; `WssEvent` struct 适配 | 老李 + 小冯 | beast 接入后联调 |
| M5 前 | cpp-httplib TLS 路径验证 (live 真 HMAC 签名头, 老孙 signer v5.1 F-02 路径) | 老李 + 老孙 | live binary 接入 |

---

## 5. 风险登记

| 风险 | 缓解 |
|---|---|
| beast header-only 拖慢 IDE 索引 | 用 SYSTEM include 隔离, 编辑器 PCH 处理; 不影响 ninja 增量 |
| boost standalone vs full boost 版本冲突 | 明确 standalone Asio, 不引入 `find_package(Boost)` |
| simdjson On-demand API 学习曲线 | 小冯 + 老李 W7 联合落地, 共享 demo TU |
| cpp-httplib TLS OpenSSL 依赖 | macOS: Security.framework; Linux CI: libssl-dev apt; cmake SYSTEM 变量已有处理模式 |

---

## §X build switch 规范 (W6 W3 老郭仲裁立, 2026-06-W3)

### 背景

W6 W3 累积 5+1 build switch:
- `STCPP_EXEC_MODE` (paper/live/backtest)
- `STCPP_BUILD_BENCH`
- `STCPP_BUILD_CLI` (老沈 W6 Wave 29)
- `STCPP_BUILD_SIGNER_V52` (老孙 W6 Wave 30)
- `STCPP_TEST_BUILD` (小卢 W6 Wave 30, 隐式 compile define 而非 CMake option)

老周建议立 ADR-019 build switch 规范. 老高 + 老胡建议 ADR-018 §X 补条款. 老郭仲裁: ADR-018 §X 补.

### 规则

1. **命名规范**: 所有 CMake option 命名 `STCPP_<DOMAIN>_<NAME>` 或 `STCPP_BUILD_<MODULE>`; test-only 隐式 define 命名 `STCPP_TEST_*`
2. **上限**: CMake `option()` ≤ 8 个 (隐式 compile define 也算 1 个, 含 `STCPP_TEST_BUILD`)
3. **触发架构评审**:
   - **第 5 个 CMake option 引入时**: 老郭过目 + 新 switch 须老郭 ack 后方可 merge (W7 起执行)
   - **第 6 个 CMake option 引入时**: 必须升级立 ADR-019 正式立项 (ADR-019 当前为候选预留, 与老周提案对齐)
4. **隐式 PRIVATE define (非 option) 文档化**: 每个隐式 define 必须在对应 CMakeLists.txt 顶部注释列出 (默认值 / 含义 / 跨平台影响)
5. **CI 覆盖**: `EXEC_MODE(paper/live/backtest)` × default options 主要组合, paper mode 为必测; W7 老高 PR v1.4 补 CI 矩阵覆盖

### W6 W3 现状审查

| switch | 类型 | 默认值 | 文档化状态 |
|---|---|---|---|
| `STCPP_EXEC_MODE` | CMake option (string) | paper | ADR-011 §1 |
| `STCPP_BUILD_BENCH` | CMake option (bool) | OFF | ADR-017 注 |
| `STCPP_BUILD_CLI` | CMake option (bool) | OFF | 老沈 W6 W2 派单 prompt |
| `STCPP_BUILD_SIGNER_V52` | CMake option (bool) | ON | **未文档化** — W7 补 |
| `STCPP_TEST_BUILD` | 隐式 PRIVATE define | undefined | **未文档化, 老高 H-09 指出** — W7 补 |

5 switch 未超 8 上限. 但 `STCPP_TEST_BUILD` 隐式 define 未在 CMake option 列表中, 难以发现. CI 当前只测 paper mode (1/8 矩阵) — 风险高.

### W7 落地

- 老周 + 老郭 (W7 W4): 5 switch 文档化 (本 ADR §X)
- 老高 PR v1.4 (W7 W3): build switch grep (隐式 define 漏文档化 fail) + CI 矩阵覆盖 enforce
- 第 6 个 CMake option 触发立 ADR-019 (前置条件, ADR-019 候选编号已预留)
- `STCPP_BUILD_SIGNER_V52` 默认 ON 与 `STCPP_EXEC_MODE` 隐式关联 (老高 H-09) — 老周 W7 补显式 guard

---

**老周 (cpp-chief-architect):** ADR-018 决议锁定。老郭 架构评审 M1 前 forward 一次。

**§X 仲裁:** 老郭 (chief-architecture-reviewer), 2026-06-W3. 老周 24h 申辩窗口截止: W6 W3 EOD (6/2). 无异议视为接受.

**last_review:** 2026-06-W3 by 老郭 (§X 补); 老周 (§0-§5)
