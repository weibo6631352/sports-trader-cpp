# tests/chaos/wss/CMakeLists.txt — C-WSS chaos WSS disconnect tests
# Owner: 小宋  E-03 chaos v1

add_executable(stcpp_chaos_wss
    c_wss_01_disconnect_15s.cpp
    c_wss_02_stale_halt.cpp
    c_wss_03_reconnect_fail.cpp
    c_wss_04_paper_startup.cpp
)

target_link_libraries(stcpp_chaos_wss PRIVATE
    GTest::gtest_main
    stcpp_risk
    stcpp_observability_audit
    stcpp_signer_paper
    stcpp_execution_paper
)

target_include_directories(stcpp_chaos_wss PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_chaos_wss PRIVATE cxx_std_20)

target_compile_options(stcpp_chaos_wss PRIVATE
    -Wno-double-promotion
    -Wno-old-style-cast
    -Wno-cast-align
)

target_compile_definitions(stcpp_chaos_wss PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)

gtest_discover_tests(stcpp_chaos_wss
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "chaos;chaos_smoke;wss;e03"
)
