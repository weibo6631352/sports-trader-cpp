# tests/chaos/ooo/CMakeLists.txt — C-OOO chaos out-of-order tests
# Owner: 小宋  E-03 chaos v1

add_executable(stcpp_chaos_ooo
    c_ooo_01_seq_gap.cpp
    c_ooo_02_window_shuffle.cpp
    c_ooo_03_paper_stale_quote.cpp
)

target_link_libraries(stcpp_chaos_ooo PRIVATE
    GTest::gtest_main
    stcpp_risk
    stcpp_observability_audit
    stcpp_signer_paper
    stcpp_execution_paper
)

target_include_directories(stcpp_chaos_ooo PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_chaos_ooo PRIVATE cxx_std_20)

target_compile_options(stcpp_chaos_ooo PRIVATE
    -Wno-double-promotion
    -Wno-old-style-cast
    -Wno-cast-align
)

target_compile_definitions(stcpp_chaos_ooo PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)

gtest_discover_tests(stcpp_chaos_ooo
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "chaos;chaos_smoke;ooo;e03"
)
