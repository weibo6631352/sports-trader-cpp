cmake_minimum_required(VERSION 3.27)
project(stcpp
    VERSION 0.1.0
    DESCRIPTION "Polymarket sports market quant trading system"
    LANGUAGES CXX
)

# C++ 版本锁 (ADR-001 W-1 + 老何 v1 选型)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ExecutionMode build-time switch (ADR gm-signoff-paper-trade R-7 + R-11)
# 三 mode 同进程不允许动态切换 — build-time 二选一
set(STCPP_EXEC_MODE "paper" CACHE STRING "Execution mode: live / paper / backtest")
set_property(CACHE STCPP_EXEC_MODE PROPERTY STRINGS live paper backtest)

if(NOT STCPP_EXEC_MODE MATCHES "^(live|paper|backtest)$")
    message(FATAL_ERROR "STCPP_EXEC_MODE must be one of: live / paper / backtest (got: ${STCPP_EXEC_MODE})")
endif()

message(STATUS "stcpp build: STCPP_EXEC_MODE = ${STCPP_EXEC_MODE}")

# 编译选项 (老高 v1 §8 + 老何 footgun)
add_compile_options(
    -Wall -Wextra -Werror
    -Wshadow -Wnon-virtual-dtor -Wcast-align -Wold-style-cast
    -Wconversion -Wsign-conversion -Wnull-dereference -Wdouble-promotion
    -Wformat=2
    -fno-omit-frame-pointer
)

# Sanitizers (开发期, 不进 release)
option(STCPP_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(STCPP_ENABLE_UBSAN "Enable UBSan" OFF)
option(STCPP_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(STCPP_ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-sanitize-recover=address)
    add_link_options(-fsanitize=address)
endif()
if(STCPP_ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined -fno-sanitize-recover=undefined)
    add_link_options(-fsanitize=undefined)
endif()
if(STCPP_ENABLE_TSAN AND NOT STCPP_ENABLE_ASAN)
    add_compile_options(-fsanitize=thread)
    add_link_options(-fsanitize=thread)
endif()

# 头文件路径
include_directories(${CMAKE_SOURCE_DIR}/include)

# 测试 (Sprint-2 W3 Wave 18 启用)
option(STCPP_BUILD_BENCH "Build Google Benchmark perf 回归" OFF)

include(CTest)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
    # W4+: replay / chaos / fuzz / perf 逐步加
endif()

if(STCPP_BUILD_BENCH)
    add_subdirectory(tests/bench)
endif()

# 子模块 (Sprint-2 W3 Wave 18: WAL framework skeleton 起步)
add_subdirectory(src/stcpp/infra/wal)
# Sprint-2 W4 Wave 19: 老韩 RiskGateway v0.1 (paper mode 走通)
add_subdirectory(src/stcpp/risk)
# Sprint-2 W4 Wave 19: 老唐 AuditEmitter v0.1 (接老王 WAL framework)
add_subdirectory(src/stcpp/observability)
# Sprint-2 W4 Wave 19: 小段 Goalserve client adapter v0.1 (stub)
add_subdirectory(src/stcpp/data)
# Sprint-2 W4 Wave 19: 小蒋 paper engine v0.1 (signer + virtual matcher + paper main)
add_subdirectory(src/stcpp/signer)
add_subdirectory(src/stcpp/execution)
add_subdirectory(src/stcpp/bin)
# Sprint-2 W4 Wave 19: P0-01 Pinnacle no-vig + LiveSection (小程 spec + 小卢 IC 实施)
add_subdirectory(src/stcpp/strategy)

message(STATUS "stcpp project scaffold (Sprint-2 W4 Wave 19). paper engine v0.1 + 3 virtual mock.")
