# tests/chaos/rest_timeout/CMakeLists.txt — C-REST chaos REST timeout tests
# Owner: 小宋  E-03 chaos v1

add_executable(stcpp_chaos_rest_timeout
    c_rest_01_429.cpp
    c_rest_02_502_gamma.cpp
    c_rest_03_timeout.cpp
    c_rest_04_paper_startup.cpp
)

target_link_libraries(stcpp_chaos_rest_timeout PRIVATE
    GTest::gtest_main
    stcpp_risk
    stcpp_observability_audit
    stcpp_signer_paper
    stcpp_execution_paper
)

target_include_directories(stcpp_chaos_rest_timeout PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_chaos_rest_timeout PRIVATE cxx_std_20)

target_compile_options(stcpp_chaos_rest_timeout PRIVATE
    -Wno-double-promotion
    -Wno-old-style-cast
    -Wno-cast-align
)

target_compile_definitions(stcpp_chaos_rest_timeout PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)

gtest_discover_tests(stcpp_chaos_rest_timeout
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "chaos;chaos_smoke;rest_timeout;e03"
)
