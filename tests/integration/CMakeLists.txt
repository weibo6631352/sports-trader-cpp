# tests/integration/CMakeLists.txt — integration test target (W5 Wave 24)
#
# Owner: 小宋 (test-replay-engineer)  Sprint-2 W5 Wave 24 (W5-E-03 老胡 spec)
# 关联: docs/RESEARCH/laozhou-architecture-v0.6-e2e.md §5 M1 10 hard gate
#       docs/RESEARCH/xiaoying-acceptance-spec-v1.md §1.A/D/E/F (M1-A04/D01-D04/E03/F04)
#       docs/RESEARCH/laojiang-latency-budget-v1.md  (端到端 < 50ms p99)
#       docs/RESEARCH/xiaosong-test-framework-cpp-skeleton-v1.md §2.2 SimHarnessFixture
#
# 红线: R-1 (RM evaluate 必经) / R-7 (paper-only) / R-11 (4 wal 物理隔离)
#       R-12 (event loop 不阻塞) / R-20 (4 ts PIT 全链路)
#
# W5 Wave 24 占位策略: 老李 PM REST client / 小冯 PM WSS subscriber / 老沈 12 reject fixture
#   并行交付; 任何依赖未就绪用 in-process MockPmWss + MockChainRpc 占位, W5 末替换.
#
# 仅在 paper build 启用 (R-7 物理隔离); live binary 不进 integration target.

if(NOT STCPP_EXEC_MODE STREQUAL "paper")
    message(STATUS "tests/integration: skip (STCPP_EXEC_MODE=${STCPP_EXEC_MODE}, paper-only)")
    return()
endif()

# ---- E2E smoke test (M1-G1 / D01 / F04 / A04) ----
add_executable(stcpp_test_integration_paper_e2e
    paper_e2e_smoke_test.cpp
)
target_link_libraries(stcpp_test_integration_paper_e2e PRIVATE
    GTest::gtest_main
    stcpp_risk
    stcpp_observability_audit
    stcpp_signer_paper
    stcpp_execution_paper
)
target_include_directories(stcpp_test_integration_paper_e2e PRIVATE
    ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/include)
target_compile_features(stcpp_test_integration_paper_e2e PRIVATE cxx_std_20)
target_compile_options(stcpp_test_integration_paper_e2e PRIVATE
    -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
    -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
    -Wno-cast-align)        # ADR-010 grandfather: gtest mock 转型
target_compile_definitions(stcpp_test_integration_paper_e2e PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1)
gtest_discover_tests(stcpp_test_integration_paper_e2e
    DISCOVERY_TIMEOUT 120
    PROPERTIES LABELS "integration;e2e;w5;m1")

# ---- audit chain verify (M1-A06 / E03) ----
add_executable(stcpp_test_integration_audit_chain
    audit_chain_verify_test.cpp
)
target_link_libraries(stcpp_test_integration_audit_chain PRIVATE
    GTest::gtest_main stcpp_observability_audit)
target_include_directories(stcpp_test_integration_audit_chain PRIVATE
    ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/include)
target_compile_features(stcpp_test_integration_audit_chain PRIVATE cxx_std_20)
target_compile_options(stcpp_test_integration_audit_chain PRIVATE
    -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
    -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
    -Wno-cast-align)        # ADR-010 grandfather: gtest mock 转型
target_compile_definitions(stcpp_test_integration_audit_chain PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1)
gtest_discover_tests(stcpp_test_integration_audit_chain
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "integration;audit;w5;m1")

# ---- R-11 paper pollution guard (M1-D02/D03/G9) ----
add_executable(stcpp_test_integration_r11_pollution
    r11_paper_pollution_test.cpp
)
target_link_libraries(stcpp_test_integration_r11_pollution PRIVATE
    GTest::gtest_main
    stcpp_risk stcpp_observability_audit stcpp_signer_paper stcpp_execution_paper)
target_include_directories(stcpp_test_integration_r11_pollution PRIVATE
    ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/include)
target_compile_features(stcpp_test_integration_r11_pollution PRIVATE cxx_std_20)
target_compile_options(stcpp_test_integration_r11_pollution PRIVATE
    -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
    -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
    -Wno-cast-align)        # ADR-010 grandfather: gtest mock 转型
target_compile_definitions(stcpp_test_integration_r11_pollution PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1)
gtest_discover_tests(stcpp_test_integration_r11_pollution
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "integration;r11;w5;m1")
