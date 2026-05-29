# tests/integration/debug_api/CMakeLists.txt — debug API integration test (W9 W2)
#
# Owner: 小卢 (senior-ic-pool)  Sprint-3 W9 W2
# 关联:
#   xiaolu-w9-rest-api-skeleton-implementation-spec-v1.md §6.1
#   laozhou-w8-debug-rest-api-spec-v1.md §7.2
#
# 5 ctest case: T1-T5 (见 test_debug_api_integration.cpp)
#
# 红线:
#   R-7: paper-only (integration test 仅 paper mode 启用)
#   R-12: T4 验证 server stop 不影响主线程
#   ADR-018: cpp-httplib 已通过 stcpp_debug_api 链接获得

if(NOT STCPP_EXEC_MODE STREQUAL "paper")
    message(STATUS "tests/integration/debug_api: skip (STCPP_EXEC_MODE=${STCPP_EXEC_MODE}, paper-only)")
    return()
endif()

if(NOT STCPP_BUILD_DEBUG_API)
    message(STATUS "tests/integration/debug_api: skip (STCPP_BUILD_DEBUG_API=OFF)")
    return()
endif()

add_executable(stcpp_test_integration_debug_api
    test_debug_api_integration.cpp
)

target_link_libraries(stcpp_test_integration_debug_api PRIVATE
    GTest::gtest_main
    stcpp_debug_api
)

target_compile_features(stcpp_test_integration_debug_api PRIVATE cxx_std_20)

target_compile_options(stcpp_test_integration_debug_api PRIVATE
    -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
    -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
    -Wno-cast-align         # ADR-010 grandfather: gtest mock 转型
)

target_include_directories(stcpp_test_integration_debug_api PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_definitions(stcpp_test_integration_debug_api PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)

gtest_discover_tests(stcpp_test_integration_debug_api
    DISCOVERY_TIMEOUT 120
    PROPERTIES LABELS "integration;debug-api;w9"
)

# ---------- ADR-038 MVP observability endpoints (小卢) ----------
add_executable(stcpp_test_observability_endpoints
    test_observability_endpoints.cpp
)

target_link_libraries(stcpp_test_observability_endpoints PRIVATE
    GTest::gtest_main
    stcpp_debug_api
)

target_compile_features(stcpp_test_observability_endpoints PRIVATE cxx_std_20)

target_compile_options(stcpp_test_observability_endpoints PRIVATE
    -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
    -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
    -Wno-cast-align         # ADR-010 grandfather: gtest mock 转型
)

target_include_directories(stcpp_test_observability_endpoints PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_definitions(stcpp_test_observability_endpoints PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)

gtest_discover_tests(stcpp_test_observability_endpoints
    DISCOVERY_TIMEOUT 120
    PROPERTIES LABELS "integration;debug-api;adr-038"
)

# ---------- P1 dogfood 整改验证 (小卢 feat/xiaolu-market-metrics-fix) ----------
# P1-1: market() catalog found=true
# P1-2: metrics() uptime/rm_reject/fill/wss 非零
# P1-3: book wss_state 与 /status 一致
add_executable(stcpp_test_p1_market_metrics_wss
    test_p1_market_metrics_wss.cpp
)

target_link_libraries(stcpp_test_p1_market_metrics_wss PRIVATE
    GTest::gtest_main
    stcpp_debug_api
    stcpp_data_score_store
    stcpp_orderbook_snapshot_hub
    stcpp_ledger_snapshot_hub
    stcpp_quote_snapshot_hub
    stcpp_risk
    stcpp_polymarket_wss
)

target_compile_features(stcpp_test_p1_market_metrics_wss PRIVATE cxx_std_20)

target_compile_options(stcpp_test_p1_market_metrics_wss PRIVATE
    -Wno-double-promotion
    -Wno-old-style-cast
    -Wno-cast-align
)

target_include_directories(stcpp_test_p1_market_metrics_wss PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_definitions(stcpp_test_p1_market_metrics_wss PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)

gtest_discover_tests(stcpp_test_p1_market_metrics_wss
    DISCOVERY_TIMEOUT 120
    PROPERTIES LABELS "integration;debug-api;p1-dogfood"
)
