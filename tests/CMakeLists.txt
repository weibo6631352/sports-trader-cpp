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
    SYSTEM   # CMake 3.25+: gtest include 视为 SYSTEM, 不受公司 -Wdouble-promotion / -Wsign-conversion 等 -Werror 约束
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# gtest / gmock 第三方源码本身也 inherit 顶层 -Werror -Wdouble-promotion 等严格 flag,
# 关掉这些 warning 让 gtest 能 build (生产代码 src/ + include/ 保持严格)
foreach(_gt_target gtest gtest_main gmock gmock_main)
    if(TARGET ${_gt_target})
        target_compile_options(${_gt_target} PRIVATE
            -Wno-double-promotion -Wno-sign-conversion -Wno-conversion
            -Wno-shadow -Wno-old-style-cast -Wno-cast-align
            -Wno-character-conversion
        )
    endif()
endforeach()

# tests/ 子树关掉对第三方 gtest header 触发的严格 warning
# (生产代码 src/ + include/ 保持顶层 -Werror 严格, 仅 tests/ 放宽以兼容 gtest)
add_compile_options(
    -Wno-double-promotion
    -Wno-sign-conversion
    -Wno-conversion
    -Wno-shadow
    -Wno-old-style-cast
)

# include path: 让 tests 内 #include "tests/unit/..." 工作
include_directories(${CMAKE_SOURCE_DIR})

include(GoogleTest)

add_subdirectory(unit)
add_subdirectory(sim)
