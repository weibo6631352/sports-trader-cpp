# tests/replay/r20/CMakeLists.txt — R-R20 replay R-20 4 ts tests
# Owner: 小宋  E-04 replay v1

add_executable(stcpp_replay_r20
    r_r20_01_normal_7d.cpp
    r_r20_violations.cpp
    r_r20_06_wal_header_payload.cpp
    r_r20_07_paper_30d_attestation.cpp
)

target_link_libraries(stcpp_replay_r20 PRIVATE
    GTest::gtest_main
    stcpp_observability_audit
    stcpp_infra_wal
)

target_include_directories(stcpp_replay_r20 PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_replay_r20 PRIVATE cxx_std_20)

target_compile_options(stcpp_replay_r20 PRIVATE
    -Wno-double-promotion
    -Wno-old-style-cast
    -Wno-cast-align
)

target_compile_definitions(stcpp_replay_r20 PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)

gtest_discover_tests(stcpp_replay_r20
    DISCOVERY_TIMEOUT 120
    PROPERTIES LABELS "replay;replay_gate;r20;e04"
)
