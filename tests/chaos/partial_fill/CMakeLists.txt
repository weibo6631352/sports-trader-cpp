# tests/chaos/partial_fill/CMakeLists.txt — C-FILL chaos partial fill tests
# Owner: 小宋  E-03 chaos v1

add_executable(stcpp_chaos_partial_fill
    c_fill_01_50pct.cpp
    c_fill_02_5pct_many.cpp
    c_fill_03_price_move.cpp
    c_fill_04_paper_wal.cpp
)

target_link_libraries(stcpp_chaos_partial_fill PRIVATE
    GTest::gtest_main
    stcpp_risk
    stcpp_observability_audit
    stcpp_signer_paper
    stcpp_execution_paper
)

target_include_directories(stcpp_chaos_partial_fill PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_chaos_partial_fill PRIVATE cxx_std_20)

target_compile_options(stcpp_chaos_partial_fill PRIVATE
    -Wno-double-promotion
    -Wno-old-style-cast
    -Wno-cast-align
)

target_compile_definitions(stcpp_chaos_partial_fill PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)

gtest_discover_tests(stcpp_chaos_partial_fill
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "chaos;chaos_smoke;partial_fill;e03"
)
