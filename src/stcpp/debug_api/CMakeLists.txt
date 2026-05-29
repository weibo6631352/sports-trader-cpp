# src/stcpp/debug_api/CMakeLists.txt — Debug REST API server (W9 W2)
#
# Owner: 小卢 (senior-ic-pool)  Sprint-3 W9 W2
# 关联:
#   docs/RESEARCH/xiaolu-w9-rest-api-skeleton-implementation-spec-v1.md
#   docs/RESEARCH/laozhou-w8-debug-rest-api-spec-v1.md
#   ADR-018 (cpp-httplib v0.16.3 FetchContent)
#   ADR-015 (vCPU 分工; API server 独立 vCPU6)
#   ADR-011 (paper/live 隔离; STCPP_EXEC_MODE 编译期常量)
#   R-12 (API server 独立线程, 不阻塞 hot path)
#   R-20 (as_of_ts epoch ns; build_ts = 编译期常量, 非数据 ts)
#
# 编译期常量注入 (version_generated.hpp):
#   - STCPP_GIT_HASH   — git rev-parse --short HEAD (configure-time)
#   - STCPP_VERSION    — ${PROJECT_VERSION} = 0.1.0
#   - STCPP_BUILD_TIME — 编译期 __DATE__ __TIME__ concat (不违反 R-20: 不是数据时间戳)
#   - STCPP_EXEC_MODE_STR — 字符串化 STCPP_EXEC_MODE

cmake_minimum_required(VERSION 3.27)

include(FetchContent)

# ---------- cpp-httplib v0.16.3 (ADR-018 已定) ----------
# header-only, single-file httplib.h; 内置 HTTP server + client
# blocking socket I/O model: 在独立 std::thread 运行, 不影响 hot path (R-12)

FetchContent_Declare(
    cpp_httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.16.3
    GIT_SHALLOW    TRUE
    SYSTEM
)
# cpp-httplib 带 CMakeLists.txt, FetchContent_MakeAvailable 即可获得 httplib::httplib target
set(HTTPLIB_COMPILE OFF CACHE BOOL "" FORCE)    # header-only, 不编译独立 lib
set(HTTPLIB_INSTALL OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)  # W9 不需 HTTPS
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE    OFF CACHE BOOL "" FORCE)  # W9 不压缩
FetchContent_MakeAvailable(cpp_httplib)

# ---------- 编译期常量生成 (version_generated.hpp) ----------

# git hash (configure-time)
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE STCPP_GIT_HASH_RAW
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT STCPP_GIT_HASH_RAW OR STCPP_GIT_HASH_RAW STREQUAL "")
    set(STCPP_GIT_HASH_RAW "unknown")
endif()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/version_generated.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/version_generated.hpp
    @ONLY
)

# ---------- stcpp_debug_api STATIC target ----------

add_library(stcpp_debug_api STATIC
    server.cpp
    endpoint_healthz.cpp
    endpoint_version.cpp
    endpoint_status.cpp
    # ADR-038 MVP read-only endpoints (小卢):
    endpoint_positions.cpp
    endpoint_pnl.cpp
    endpoint_risk.cpp
    endpoint_gate.cpp
    endpoint_metrics.cpp
    endpoint_market.cpp
    # 前端 v3 盯盘新增 (ADR-038 增量, 2026-05-29)
    endpoint_score.cpp
    endpoint_quote.cpp
    # ADR-040: book_pair + book/token/{token_id} (市场结构修正, 老周决议)
    endpoint_book_pair.cpp
)

target_include_directories(stcpp_debug_api
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}    # version_generated.hpp
)

# cpp-httplib: SYSTEM include (不受公司 -Werror 约束)
target_include_directories(stcpp_debug_api
    SYSTEM PUBLIC
        ${cpp_httplib_SOURCE_DIR}
)

target_compile_features(stcpp_debug_api PUBLIC cxx_std_20)

target_compile_definitions(stcpp_debug_api PRIVATE
    STCPP_EXEC_MODE_STR="${STCPP_EXEC_MODE}"
)

# 三方库豁免: cpp-httplib (ADR-010 §3.2 — header-only v0.16.3 宏展开 / vsnprintf 格式 / cast 警告)
# 三方库豁免: boost (ADR-010 §3.2 — Asio 可选依赖, shadow 警告)
target_compile_options(stcpp_debug_api PRIVATE
    -Wno-old-style-cast
    -Wno-cast-align
    -Wno-sign-conversion
    -Wno-conversion
    -Wno-shadow
    -Wno-double-promotion
    -Wno-format-nonliteral
)

find_package(Threads REQUIRED)
target_link_libraries(stcpp_debug_api PUBLIC Threads::Threads)

# ---------- stcpp_debug_server 可运行入口 (前端集成落地点) ----------
# Owner: 老雷 (GM) 2026-05-29 — 让 frontend/ 看板连真后端 (非 ?stub=1 mock)。
# 默认 DemoStateProvider (代表性数据, 全面板可渲染); --empty 用 StubStateProvider;
# --real 用 RealStateProvider (book/rejects 接真实快照, 其余委托 Demo)。
# 只读 server, 127.0.0.1 默认 (ADR-038 §5)。在 debug_api/ 定义以保证 target 顺序正确
# (root 在 bin/ 之后才 add_subdirectory(debug_api), bin/ 取不到本 target)。
#
# --real 依赖:
#   stcpp_orderbook_snapshot_hub — per-token double-buffer (小冯, 零反向依赖)
#   stcpp_risk                   — RmDebugSnapshot + attach/detach free functions (老沈)
#   stcpp_ledger_snapshot_hub    — LedgerSnapshotHub positions/pnl hub (小石, 集成 ④)
#   stcpp_sizing                 — SizingCalculator 真实 Kelly 计算 (小袁, ADR-042, 集成 ③)
#   stcpp_quote_snapshot_hub     — QuoteSnapshotHub quote hub (小石, 集成 ④)
#   stcpp_data_score_store       — ScoreSnapshotStore live feed 比分快照 (小段, 集成 ③)
#
# --live 额外依赖 (小冯 2026-05-29):
#   OpenSSL (libssl + libcrypto) — LiveWssTransport POSIX+OpenSSL TLS 1.3
#   live_wss_transport.hpp + live_book_publisher.hpp — header-only, 在 src/stcpp/debug_api/
#   POSIX socket (系统库): getaddrinfo / connect / recv / send — 无需额外 link (macOS/Linux 内置)

# 查找 OpenSSL (--live 模式需要)
find_package(OpenSSL QUIET)
if(NOT OpenSSL_FOUND)
    # macOS homebrew fallback
    find_library(OPENSSL_SSL_LIB ssl PATHS /opt/homebrew/opt/openssl@3/lib NO_DEFAULT_PATH)
    find_library(OPENSSL_CRYPTO_LIB crypto PATHS /opt/homebrew/opt/openssl@3/lib NO_DEFAULT_PATH)
    find_path(OPENSSL_INCLUDE_DIR openssl/ssl.h PATHS /opt/homebrew/opt/openssl@3/include NO_DEFAULT_PATH)
    if(OPENSSL_SSL_LIB AND OPENSSL_CRYPTO_LIB AND OPENSSL_INCLUDE_DIR)
        set(OPENSSL_FOUND_MANUAL TRUE)
        message(STATUS "OpenSSL found via homebrew: ${OPENSSL_SSL_LIB}")
    else()
        message(WARNING "OpenSSL not found — --live mode will fail at runtime (link error). Install: brew install openssl@3")
    endif()
endif()

add_executable(stcpp_debug_server debug_server_main.cpp)
target_link_libraries(stcpp_debug_server PRIVATE
    stcpp_debug_api
    stcpp_orderbook_snapshot_hub
    stcpp_risk
    stcpp_ledger_snapshot_hub   # 集成 ④: positions/pnl hub (小石)
    stcpp_sizing
    stcpp_quote_snapshot_hub    # 集成 ④: quote hub (小石)
    stcpp_data_score_store
    stcpp_backtest              # ReplayDriver (--replay 模式, kSynthetic hub feed)
)

# OpenSSL link (--live 模式 LiveWssTransport)
if(OpenSSL_FOUND)
    target_link_libraries(stcpp_debug_server PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(stcpp_debug_server PRIVATE STCPP_LIVE_WSS_OPENSSL=1)
    message(STATUS "stcpp_debug_server: OpenSSL found via find_package (--live 模式启用)")
elseif(OPENSSL_FOUND_MANUAL)
    target_link_libraries(stcpp_debug_server PRIVATE ${OPENSSL_SSL_LIB} ${OPENSSL_CRYPTO_LIB})
    target_include_directories(stcpp_debug_server SYSTEM PRIVATE ${OPENSSL_INCLUDE_DIR})
    target_compile_definitions(stcpp_debug_server PRIVATE STCPP_LIVE_WSS_OPENSSL=1)
    message(STATUS "stcpp_debug_server: OpenSSL found via homebrew fallback (--live 模式启用)")
else()
    message(WARNING "stcpp_debug_server: OpenSSL NOT found — --live 模式将编译报错; 安装: brew install openssl@3")
endif()

target_include_directories(stcpp_debug_server PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(stcpp_debug_server PRIVATE cxx_std_20)
target_compile_definitions(stcpp_debug_server PRIVATE
    STCPP_EXEC_MODE_STR="${STCPP_EXEC_MODE}"
)
# 三方库豁免: cpp-httplib (经 stcpp_debug_api PUBLIC SYSTEM include 传入)
# OpenSSL: SYSTEM include 豁免 (系统头, 非公司 -Werror 范围)
target_compile_options(stcpp_debug_server PRIVATE
    -Wno-old-style-cast
    -Wno-cast-align
    -Wno-sign-conversion
    -Wno-conversion
    -Wno-shadow
    -Wno-double-promotion
    -Wno-format-nonliteral
)

message(STATUS "stcpp_debug_api: cpp-httplib v0.16.3 (header-only) | mode=${STCPP_EXEC_MODE} | git=${STCPP_GIT_HASH_RAW}")
