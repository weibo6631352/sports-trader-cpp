# tests/unit/CMakeLists.txt — gtest 单测
# Sprint-2 W3 Wave 18:
#   - 小肖 SlippageModel 7 case
#   - 小宋 RM 21 reject enum coverage placeholder
#   - 老王 WAL framework skeleton (path prefix + PIT + header layout + sync p99)
#
# GoogleTest / GoogleTest FetchContent + GoogleTest module 由顶层 tests/CMakeLists.txt 提供
# (避免子目录重复 FetchContent 拉 git)

add_executable(test_slippage_model
    test_slippage_model.cpp
)
target_link_libraries(test_slippage_model PRIVATE GTest::gtest_main)
target_include_directories(test_slippage_model PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_slippage_model PRIVATE cxx_std_20)
target_compile_definitions(test_slippage_model PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)
gtest_discover_tests(test_slippage_model
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "unit"
)

# 小宋: RM 21 reject enum coverage (W3 placeholder, W4+ owner 切真 RM)
add_executable(stcpp_test_unit_risk
    risk_enum_coverage_test.cpp
)
target_link_libraries(stcpp_test_unit_risk PRIVATE GTest::gtest_main)
target_include_directories(stcpp_test_unit_risk PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(stcpp_test_unit_risk PRIVATE cxx_std_20)
target_compile_definitions(stcpp_test_unit_risk PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)
gtest_discover_tests(stcpp_test_unit_risk
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "unit;risk;w3-placeholder"
)

# 老王: WAL framework skeleton (派单 §7)
#   T1 R-11 path prefix abort (death test)
#   T2 R-20 PIT assert chain (5 violation case)
#   T3 同步 Append p99 ≤ 6us (gtest 兜底, GoogleBench W4)
#   T4 header v2 64B + offset (老唐 v1.1 §2.x 对齐)
add_executable(test_wal_writer
    test_wal_writer.cpp
)
target_link_libraries(test_wal_writer PRIVATE GTest::gtest_main)
target_include_directories(test_wal_writer PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_wal_writer PRIVATE cxx_std_20)
target_compile_definitions(test_wal_writer PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)
# 测试 TU 自带 #include wal_writer.cpp 做模板实例化 — 关 offsetof 警告 + 重复 macro 警告
target_compile_options(test_wal_writer PRIVATE
    -Wno-invalid-offsetof
)
gtest_discover_tests(test_wal_writer
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "unit;wal;w3"
)
