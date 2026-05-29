# tests/unit/sizing/CMakeLists.txt — SizingCalculator unit + RM-consistency 测试 (小袁 v0.1)
#
# Owner: 小袁 (quant-microstructure)
# last_review: 2026-05-29
#
# 验收门槛 (老韩 §5):
#   test_sizing_calculator:     C1-C7 全绿 + capped_by 命中 + λ 折扣 + fill_rate 折扣 + NaN 健壮性
#   test_sizing_rm_consistency: N=10,000 零 sizing 维度 reject + net edge ±1e-9 + bankroll 动态
#
# GoogleTest 由顶层 tests/CMakeLists.txt 提供 (FetchContent + find_package(GTest))
# 顶层 enable_testing() 已在 root CMakeLists.txt 的 include(CTest) 后执行

cmake_minimum_required(VERSION 3.27)

# 引入 GTest 模块 (由顶层 tests/CMakeLists.txt 拉取; 此子目录单独构建时可用 find_package)
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    # 单独构建场景: 尝试 FetchContent
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    include(GoogleTest)
endif()

# ============================================================================
# test_sizing_calculator — C1-C7 闭合性单测
# ============================================================================

add_executable(test_sizing_calculator
    test_sizing_calculator.cpp
)

target_include_directories(test_sizing_calculator PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}
)

target_compile_features(test_sizing_calculator PRIVATE cxx_std_20)

target_compile_definitions(test_sizing_calculator PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)

target_compile_options(test_sizing_calculator PRIVATE
    -Wno-double-promotion    # ADR-010 grandfather: gtest 浮点宏
    -Wno-old-style-cast      # ADR-010 grandfather: gtest 宏展开
    -Wno-cast-align          # ADR-010 grandfather: gtest mock
)

target_link_libraries(test_sizing_calculator PRIVATE
    GTest::gtest_main
    stcpp_sizing
    stcpp_risk
)

include(GoogleTest)
gtest_discover_tests(test_sizing_calculator
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "unit;sizing;kelly;c1-c7;xiaoyuan"
)

# ============================================================================
# test_sizing_rm_consistency — N=10,000 随机 RM 一致性验证
# ============================================================================

add_executable(test_sizing_rm_consistency
    test_sizing_rm_consistency.cpp
)

target_include_directories(test_sizing_rm_consistency PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}
)

target_compile_features(test_sizing_rm_consistency PRIVATE cxx_std_20)

target_compile_definitions(test_sizing_rm_consistency PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)

target_compile_options(test_sizing_rm_consistency PRIVATE
    -Wno-double-promotion
    -Wno-old-style-cast
    -Wno-cast-align
)

target_link_libraries(test_sizing_rm_consistency PRIVATE
    GTest::gtest_main
    stcpp_sizing
    stcpp_risk
)

gtest_discover_tests(test_sizing_rm_consistency
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit;sizing;rm-consistency;n10000;xiaoyuan"
)
