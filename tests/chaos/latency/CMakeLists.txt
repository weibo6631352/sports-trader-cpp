# tests/chaos/latency/CMakeLists.txt — C-LAT chaos latency spike tests
# Owner: 小宋  E-03 chaos v1

add_executable(stcpp_chaos_latency
    c_lat_01_rest_5s.cpp
    c_lat_02_rest_sustained.cpp
    c_lat_03_rpc_spike.cpp
    c_lat_04_paper_e2e.cpp
)

target_link_libraries(stcpp_chaos_latency PRIVATE
    GTest::gtest_main
    stcpp_risk
    stcpp_observability_audit
    stcpp_signer_paper
    stcpp_execution_paper
)

target_include_directories(stcpp_chaos_latency PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_chaos_latency PRIVATE cxx_std_20)

target_compile_options(stcpp_chaos_latency PRIVATE
    -Wno-double-promotion
    -Wno-old-style-cast
    -Wno-cast-align
)

target_compile_definitions(stcpp_chaos_latency PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)

gtest_discover_tests(stcpp_chaos_latency
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "chaos;chaos_smoke;latency;e03"
)
