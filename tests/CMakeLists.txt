# tests/CMakeLists.txt — 顶层测试入口 (Sprint-2 W3 Wave 18)
#
# Owner: 小宋 (test-replay-engineer)
# 关联: docs/RESEARCH/xiaosong-test-framework-cpp-skeleton-v1.md §1
#
# GoogleTest FetchContent (CI 离线: 优先 find_package, 失败回落 FetchContent)
# 三 mode 兼容: STCPP_EXEC_MODE = live/paper/backtest 均能 build

include(FetchContent)

set(STCPP_GTEST_TAG "v1.14.0" CACHE STRING "GoogleTest tag")

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        ${STCPP_GTEST_TAG}
    GIT_SHALLOW    TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# include path: 让 tests 内 #include "tests/unit/..." 工作
include_directories(${CMAKE_SOURCE_DIR})

include(GoogleTest)

add_subdirectory(unit)
add_subdirectory(sim)
