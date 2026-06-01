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
    # 小冯 schema append: EventInfo 列表 (G-FREEZE-W 只增, 2026-05-29)
    endpoint_events.cpp
    # 老雷 2026-06-01 可观测: 特征健康 + 映射状态
    endpoint_features.cpp
    endpoint_mapping.cpp
    # 老雷 2026-06-01 凯利评审: 账户级现金/估值
    endpoint_account.cpp
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

# 注: stcpp_paper_server / stcpp_paper_runtime 可运行入口已迁至 src/stcpp/app/
#     (PaperDaemon 重构, 老郭 §A.1 / 老周架构裁定 B1). debug_api 仅保留契约库,
#     绝不含 paper_daemon.cpp — 库依赖保持 Threads only, 防 §A.2 "什么都连" 膨胀.

message(STATUS "stcpp_debug_api: cpp-httplib v0.16.3 (header-only) | mode=${STCPP_EXEC_MODE} | git=${STCPP_GIT_HASH_RAW}")
