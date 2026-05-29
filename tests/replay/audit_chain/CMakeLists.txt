# tests/replay/audit_chain/CMakeLists.txt — R-chain audit chain replay tests
# Owner: 小宋  E-04 replay v1

add_executable(stcpp_replay_audit_chain
    r_chain_01_20_records.cpp
    r_chain_02_tamper.cpp
    r_chain_03_04_strategy_events.cpp
    r_chain_05_golden_head_match.cpp
)

target_link_libraries(stcpp_replay_audit_chain PRIVATE
    GTest::gtest_main
    stcpp_observability_audit
    stcpp_infra_wal
    stcpp_risk
)

target_include_directories(stcpp_replay_audit_chain PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_replay_audit_chain PRIVATE cxx_std_20)

target_compile_options(stcpp_replay_audit_chain PRIVATE
    -Wno-double-promotion
    -Wno-old-style-cast
    -Wno-cast-align
)

target_compile_definitions(stcpp_replay_audit_chain PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)

gtest_discover_tests(stcpp_replay_audit_chain
    DISCOVERY_TIMEOUT 120
    PROPERTIES LABELS "replay;replay_gate;audit_chain;e04"
)
