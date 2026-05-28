# tests/sim/r12_sim/CMakeLists.txt — R-12 4 场景 placeholder
# Owner: 小宋  Sprint-2 W3 Wave 18

add_executable(stcpp_test_sim_r12
    s1_rest_slow_wss_unblock_test.cpp
    s2_single_flight_test.cpp
    s3_wss_disconnect_batch_test.cpp
    s4_vcpu0_burst_test.cpp
)

target_link_libraries(stcpp_test_sim_r12 PRIVATE GTest::gtest_main)
target_compile_options(stcpp_test_sim_r12 PRIVATE
    -Wno-double-promotion -Wno-sign-conversion -Wno-conversion
    -Wno-shadow -Wno-old-style-cast -Wno-cast-align)
target_include_directories(stcpp_test_sim_r12 PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(stcpp_test_sim_r12 PRIVATE cxx_std_20)
target_compile_definitions(stcpp_test_sim_r12 PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)
gtest_discover_tests(stcpp_test_sim_r12
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "sim;r12;w3-placeholder"
)
