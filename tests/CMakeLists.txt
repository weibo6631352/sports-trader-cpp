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
# ADR-010 §2.2 grandfather list: 仅保留 4 项已验证来源 (gtest 框架触发, 非测试代码问题)
# 已删除: -Wno-sign-conversion / -Wno-shadow / -Wno-conversion / -Wno-character-conversion
#   (gtest 以 SYSTEM include 处理, 这些 warning 不从 system header 传播; 测试代码本身无此问题)
foreach(_gt_target gtest gtest_main gmock gmock_main)
    if(TARGET ${_gt_target})
        target_compile_options(${_gt_target} PRIVATE
            -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
            -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
            -Wno-cast-align         # ADR-010 grandfather: gtest mock 转型
            -Wno-character-conversion  # GM 错 #11 配套 W6 W2 reinstate: gtest-printers.h:502 char8_t→char32_t (AppleClang 21)
        )
    endif()
endforeach()

# tests/ 子树: ADR-010 §2.2 grandfather list (仅保留 gtest 框架真实触发项)
# 已删除: -Wno-sign-conversion / -Wno-shadow / -Wno-conversion
#   测试代码自身已无这些 warning; gtest SYSTEM include 已通过 CMake SYSTEM 关键字处理
add_compile_options(
    -Wno-double-promotion   # ADR-010 grandfather: gtest ASSERT_NEAR / EXPECT_DOUBLE_EQ 浮点宏
    -Wno-old-style-cast     # ADR-010 grandfather: gtest ASSERT_* / EXPECT_* 宏展开
)

# include path: 让 tests 内 #include "tests/unit/..." 工作
include_directories(${CMAKE_SOURCE_DIR})

include(GoogleTest)

add_subdirectory(unit)
add_subdirectory(sim)
# W5 Wave 24 (小宋): integration test framework v0.1 (paper E2E + audit chain + R-11 pollution)
add_subdirectory(integration)
