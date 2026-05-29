# tests/chaos/paper/CMakeLists.txt — C-PAPER chaos paper-specific tests
# Owner: 小宋  E-03 chaos v1

add_executable(stcpp_chaos_paper
    c_paper_02_ooo_5intents.cpp
    c_paper_06_multi_fault.cpp
)

target_link_libraries(stcpp_chaos_paper PRIVATE
    GTest::gtest_main
    stcpp_risk
    stcpp_observability_audit
    stcpp_signer_paper
    stcpp_execution_paper
)

target_include_directories(stcpp_chaos_paper PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_chaos_paper PRIVATE cxx_std_20)

target_compile_options(stcpp_chaos_paper PRIVATE
    -Wno-double-promotion
    -Wno-old-style-cast
    -Wno-cast-align
)

target_compile_definitions(stcpp_chaos_paper PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)

gtest_discover_tests(stcpp_chaos_paper
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "chaos;chaos_smoke;paper;e03;paper_runtime_gate"
)
