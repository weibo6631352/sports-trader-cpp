# tests/replay/r11/CMakeLists.txt — R-R11 replay R-11 tests
# Owner: 小宋  E-04 replay v1

add_executable(stcpp_replay_r11
    r_r11_01_50_paper_e2e_replay.cpp
    r_r11_02_live_mode_injection.cpp
    r_r11_03_full_day_replay.cpp
    r_r11_04_sign_resp_wal_kind.cpp
)

target_link_libraries(stcpp_replay_r11 PRIVATE
    GTest::gtest_main
    stcpp_risk
    stcpp_observability_audit
    stcpp_signer_paper
    stcpp_execution_paper
)

target_include_directories(stcpp_replay_r11 PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_replay_r11 PRIVATE cxx_std_20)

target_compile_options(stcpp_replay_r11 PRIVATE
    -Wno-double-promotion
    -Wno-old-style-cast
    -Wno-cast-align
)

target_compile_definitions(stcpp_replay_r11 PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)

gtest_discover_tests(stcpp_replay_r11
    DISCOVERY_TIMEOUT 120
    PROPERTIES LABELS "replay;replay_gate;r11;e04"
)
