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
# ADR-010 §2.2 grandfather list: 5 项已验证来源 (gtest/gmock 框架触发, 非测试代码问题)
# W81 cherry-pick (57f545b): gmock 在 Linux GCC 13 下 -Wsign-conversion 触发于内部 matcher 宏;
#   SYSTEM include 处理不覆盖 gmock_main 源码 (FetchContent 源码而非 header-only);
#   ADR-010 §3.2 三方库豁免条款适用: 仅加 gmock/gmock_main, 不加 gtest/gtest_main.
foreach(_gt_target gtest gtest_main)
    if(TARGET ${_gt_target})
        target_compile_options(${_gt_target} PRIVATE
            -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
            -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
            -Wno-cast-align         # ADR-010 grandfather: gtest mock 转型
            -Wno-character-conversion  # GM 错 #11 配套 W6 W2 reinstate: gtest-printers.h:502 char8_t→char32_t (AppleClang 21)
        )
    endif()
endforeach()

foreach(_gm_target gmock gmock_main)
    if(TARGET ${_gm_target})
        # 三方库豁免: google-mock (ADR-010 §3.2)
        # -Wsign-conversion: gmock internal matcher 宏在 Linux GCC 13 下触发 (W81 cherry-pick 57f545b)
        target_compile_options(${_gm_target} PRIVATE
            -Wno-double-promotion   # ADR-010 grandfather: gmock 浮点宏
            -Wno-old-style-cast     # ADR-010 grandfather: gmock 宏展开
            -Wno-cast-align         # ADR-010 grandfather: gmock mock 转型
            -Wno-character-conversion  # gtest-printers.h char8_t→char32_t (AppleClang 21)
            -Wno-sign-conversion    # 三方库豁免: gmock internal matcher 宏 (ADR-010 §3.2, Linux GCC 13)
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
