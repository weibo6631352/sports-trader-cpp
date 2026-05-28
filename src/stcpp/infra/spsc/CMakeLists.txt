# src/stcpp/infra/spsc/CMakeLists.txt — SPSC/MPMC ring framework
#
# Owner: 小石 (data-structures-expert, #41)  W6 Wave 28 P0
# 落: laozhou-architecture-v0.6-e2e.md §4  5 queue 拓扑
#
# 依赖:
#   rigtorp/SPSCQueue  — header-only, MIT
#   rigtorp/MPMCQueue  — header-only, MIT
#
# INTERFACE target: stcpp_infra_spsc
#   下游 link 此 target 即可获得 rigtorp include path + 本项目 spsc include path
#   无 .cpp 需要编译 — 全 header-only

cmake_minimum_required(VERSION 3.27)

include(FetchContent)

# ---------- rigtorp/SPSCQueue -------------------------------------------------

FetchContent_Declare(
    rigtorp_spsc
    GIT_REPOSITORY https://github.com/rigtorp/SPSCQueue.git
    GIT_TAG        v1.1        # 2021-04 tag, API 稳定
    GIT_SHALLOW    TRUE
    SYSTEM
)
FetchContent_MakeAvailable(rigtorp_spsc)

# ---------- rigtorp/MPMCQueue -------------------------------------------------

FetchContent_Declare(
    rigtorp_mpmc
    GIT_REPOSITORY https://github.com/rigtorp/MPMCQueue.git
    GIT_TAG        master      # header-only, master = default branch
    GIT_SHALLOW    TRUE
    SYSTEM
)
FetchContent_MakeAvailable(rigtorp_mpmc)

# ---------- stcpp_infra_spsc — INTERFACE target ------------------------------

add_library(stcpp_infra_spsc INTERFACE)

target_include_directories(stcpp_infra_spsc
    INTERFACE
        ${CMAKE_SOURCE_DIR}/include
        # rigtorp SPSCQueue: src/ 目录含 SPSCQueue.h
        ${rigtorp_spsc_SOURCE_DIR}/include
        # rigtorp MPMCQueue: src/ 含 MPMCQueue.h
        ${rigtorp_mpmc_SOURCE_DIR}/include
)

target_compile_features(stcpp_infra_spsc INTERFACE cxx_std_20)

# rigtorp headers 是 SYSTEM include, 不受公司 -Werror 约束 (FetchContent SYSTEM 关键字已处理)
# 但 target_include_directories SYSTEM 模式需手动给 INTERFACE 加:
target_include_directories(stcpp_infra_spsc
    SYSTEM INTERFACE
        ${rigtorp_spsc_SOURCE_DIR}/include
        ${rigtorp_mpmc_SOURCE_DIR}/include
)

message(STATUS "stcpp_infra_spsc: rigtorp SPSCQueue @ ${rigtorp_spsc_SOURCE_DIR}/include")
message(STATUS "stcpp_infra_spsc: rigtorp MPMCQueue @ ${rigtorp_mpmc_SOURCE_DIR}/include")
