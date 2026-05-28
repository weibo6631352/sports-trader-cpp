# tests/unit/CMakeLists.txt — gtest 单测
# Sprint-2 W3 Wave 18:
#   - 小肖 SlippageModel 7 case
#   - 小宋 RM 21 reject enum coverage placeholder
#   - 老王 WAL framework skeleton (path prefix + PIT + header layout + sync p99)
#
# GoogleTest / GoogleTest FetchContent + GoogleTest module 由顶层 tests/CMakeLists.txt 提供
# (避免子目录重复 FetchContent 拉 git)

add_executable(test_slippage_model
    test_slippage_model.cpp
)
target_link_libraries(test_slippage_model PRIVATE GTest::gtest_main)
target_compile_options(test_slippage_model PRIVATE
    -Wno-double-promotion -Wno-sign-conversion -Wno-conversion
    -Wno-shadow -Wno-old-style-cast -Wno-cast-align)
target_include_directories(test_slippage_model PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_slippage_model PRIVATE cxx_std_20)
target_compile_definitions(test_slippage_model PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)
gtest_discover_tests(test_slippage_model
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "unit"
)

# 小宋: RM 21 reject enum coverage (W3 placeholder, W4+ owner 切真 RM)
add_executable(stcpp_test_unit_risk
    risk_enum_coverage_test.cpp
)
target_link_libraries(stcpp_test_unit_risk PRIVATE GTest::gtest_main)
target_compile_options(stcpp_test_unit_risk PRIVATE
    -Wno-double-promotion -Wno-sign-conversion -Wno-conversion
    -Wno-shadow -Wno-old-style-cast -Wno-cast-align)
target_include_directories(stcpp_test_unit_risk PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(stcpp_test_unit_risk PRIVATE cxx_std_20)
target_compile_definitions(stcpp_test_unit_risk PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)
gtest_discover_tests(stcpp_test_unit_risk
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "unit;risk;w3-placeholder"
)

# 老王: WAL framework skeleton (派单 §7)
#   T1 R-11 path prefix abort (death test)
#   T2 R-20 PIT assert chain (5 violation case)
#   T3 同步 Append p99 ≤ 6us (gtest 兜底, GoogleBench W4)
#   T4 header v2 64B + offset (老唐 v1.1 §2.x 对齐)
add_executable(test_wal_writer
    test_wal_writer.cpp
)
target_link_libraries(test_wal_writer PRIVATE GTest::gtest_main)
target_include_directories(test_wal_writer PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_wal_writer PRIVATE cxx_std_20)
target_compile_definitions(test_wal_writer PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)
# 测试 TU 自带 #include wal_writer.cpp 做模板实例化 — 关 offsetof 警告 + 重复 macro 警告
target_compile_options(test_wal_writer PRIVATE
    -Wno-invalid-offsetof
    -Wno-double-promotion -Wno-sign-conversion -Wno-conversion
    -Wno-shadow -Wno-old-style-cast -Wno-cast-align
)
gtest_discover_tests(test_wal_writer
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "unit;wal;w3"
)

# 老唐: AuditEmitter v0.1 (派单 W4 Wave 19)
#   A1 12 AET emit  A2 hash chain 3-record  A3 4 ts PIT  A4 reject invariant
#   A5 concept + 实例化  A6 misuse 拒绝  A7 decision_ts < as_of 拒绝
add_executable(test_audit_emitter
    test_audit_emitter.cpp
)
target_link_libraries(test_audit_emitter PRIVATE GTest::gtest_main)
target_include_directories(test_audit_emitter PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_audit_emitter PRIVATE cxx_std_20)
target_compile_definitions(test_audit_emitter PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)
# TU 内 #include audit_emitter.cpp 做模板实例化 — 关 offsetof 警告
target_compile_options(test_audit_emitter PRIVATE
    -Wno-invalid-offsetof
    -Wno-double-promotion -Wno-sign-conversion -Wno-conversion
    -Wno-shadow -Wno-old-style-cast -Wno-cast-align
)
gtest_discover_tests(test_audit_emitter
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "unit;audit;w4"
)

# 老韩 Sprint-2 W4 Wave 19: RiskGateway v0.1 真实现单测
#   - 21 reject enum × ≥ 1 case (跑真 RM, 不走 mock fixture)
#   - INVALID_INTENT × 9 sub_reason (R-20 4 ts PIT + 字段)
#   - 状态机 5 态转移 (SAFE_MODE / DRAIN / HALTED / RUNNING / WARNING)
#   - SlippageModel 联动 (depth / fill_rate / slippage_bps)
#   - WAL emit 联动 (InMemoryEmitter mock; W5 接老唐 obs::AuditEmitter)
add_executable(test_risk_gateway
    test_risk_gateway.cpp
)
target_link_libraries(test_risk_gateway PRIVATE
    GTest::gtest_main
    stcpp_risk
)
target_compile_options(test_risk_gateway PRIVATE
    -Wno-double-promotion -Wno-sign-conversion -Wno-conversion
    -Wno-shadow -Wno-old-style-cast -Wno-cast-align
)
target_include_directories(test_risk_gateway PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_risk_gateway PRIVATE cxx_std_20)
target_compile_definitions(test_risk_gateway PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)
gtest_discover_tests(test_risk_gateway
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "unit;risk;w4"
)

# 小段 Sprint-2 W4 Wave 19: Goalserve client v0.1 stub
#   T1 URL builder 5 host x 8 sport 40 combos
#   T2 TimeStatus 11 enum 闭合 (0-9 + 99)
#   T3 GameRecord/OddsRecord 4 ts 不等式 (R-20)
#   T4 ts 增量 round-trip
#   T5 sport 命名陷阱 (basket vs bsktbl, amfootball, tennis_scores, soccernew)
#   T6 ParseLastUpdateNs 两种格式 ("dd.MM.yyyy HH:mm" + ISO 8601)
#   T7 NowIngestionNs 单调 + TsMsToNs 精度
#   T8 Stub Fetch 4-ts 自然成立
add_executable(test_goalserve_client
    test_goalserve_client.cpp
)
target_link_libraries(test_goalserve_client PRIVATE
    GTest::gtest_main
    stcpp_data_goalserve
)
target_include_directories(test_goalserve_client PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_goalserve_client PRIVATE cxx_std_20)
target_compile_options(test_goalserve_client PRIVATE
    -Wno-double-promotion -Wno-sign-conversion -Wno-conversion
    -Wno-shadow -Wno-old-style-cast -Wno-cast-align
)
target_compile_definitions(test_goalserve_client PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)
gtest_discover_tests(test_goalserve_client
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "unit;goalserve;w4"
)

# 小蒋 W4 Wave 19: paper engine signer 3 mock + ExecutionContext (R-7 双调 abort)
add_executable(test_paper_signer
    test_paper_signer.cpp
)
target_link_libraries(test_paper_signer PRIVATE
    GTest::gtest_main
    stcpp_signer_paper
    stcpp_infra_wal
)
target_compile_options(test_paper_signer PRIVATE
    -Wno-double-promotion -Wno-sign-conversion -Wno-conversion
    -Wno-shadow -Wno-old-style-cast -Wno-cast-align)
target_include_directories(test_paper_signer PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_paper_signer PRIVATE cxx_std_20)
target_compile_definitions(test_paper_signer PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)
gtest_discover_tests(test_paper_signer
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "unit;paper;signer;w4"
)

# 小蒋 W4 Wave 19: VirtualMatcher Mode A++ Bernoulli + R-11 PaperAudit 硬绑
add_executable(test_virtual_matcher
    test_virtual_matcher.cpp
)
target_link_libraries(test_virtual_matcher PRIVATE
    GTest::gtest_main
    stcpp_execution_paper
    stcpp_infra_wal
)
target_compile_options(test_virtual_matcher PRIVATE
    -Wno-double-promotion -Wno-sign-conversion -Wno-conversion
    -Wno-shadow -Wno-old-style-cast -Wno-cast-align)
target_include_directories(test_virtual_matcher PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_virtual_matcher PRIVATE cxx_std_20)
target_compile_definitions(test_virtual_matcher PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)
gtest_discover_tests(test_virtual_matcher
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "unit;paper;matcher;w4"
)

# 小卢 W4 Wave 19: P0-01 Pinnacle no-vig signal (小程 spec v0.1 实施)
add_executable(test_p0_01_signal
    test_p0_01_signal.cpp
)
target_link_libraries(test_p0_01_signal PRIVATE
    GTest::gtest_main
    stcpp_strategy
)
target_compile_options(test_p0_01_signal PRIVATE
    -Wno-double-promotion -Wno-sign-conversion -Wno-conversion
    -Wno-shadow -Wno-old-style-cast -Wno-cast-align)
target_include_directories(test_p0_01_signal PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_p0_01_signal PRIVATE cxx_std_20)
target_compile_definitions(test_p0_01_signal PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)
gtest_discover_tests(test_p0_01_signal
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "unit;signal;strategy;w4"
)

# 小卢 W4 Wave 19: LiveSection 5 enum 分类 (老李 GM 逆向反推)
add_executable(test_live_section_classifier
    test_live_section_classifier.cpp
)
target_link_libraries(test_live_section_classifier PRIVATE
    GTest::gtest_main
    stcpp_strategy
)
target_compile_options(test_live_section_classifier PRIVATE
    -Wno-double-promotion -Wno-sign-conversion -Wno-conversion
    -Wno-shadow -Wno-old-style-cast -Wno-cast-align)
target_include_directories(test_live_section_classifier PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_live_section_classifier PRIVATE cxx_std_20)
target_compile_definitions(test_live_section_classifier PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)
gtest_discover_tests(test_live_section_classifier
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "unit;signal;strategy;w4"
)
