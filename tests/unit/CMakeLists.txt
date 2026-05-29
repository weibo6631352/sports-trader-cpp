# tests/unit/CMakeLists.txt — gtest 单测
# Sprint-2 W3 Wave 18:
#   - 小肖 SlippageModel 7 case
#   - 小宋 RM 21 reject enum coverage placeholder
#   - 老王 WAL framework skeleton (path prefix + PIT + header layout + sync p99)
#
# GoogleTest / GoogleTest FetchContent + GoogleTest module 由顶层 tests/CMakeLists.txt 提供
# (避免子目录重复 FetchContent 拉 git)
#
# ADR-010 §4 W7 清理 (小宋 2026-06-01):
#   每 target 删除: -Wno-sign-conversion / -Wno-shadow / -Wno-conversion
#   理由: 测试代码本身经手工验证无这些 warning (见 xiaosong-grandfather-cleanup-v1.md)
#   保留 grandfather (ADR-010 §2.2 批准):
#     -Wno-double-promotion  — gtest ASSERT_NEAR / EXPECT_DOUBLE_EQ 浮点宏触发
#     -Wno-old-style-cast    — gtest ASSERT_* / EXPECT_* 宏展开触发
#     -Wno-cast-align        — gtest mock 对象转型触发
#     -Wno-invalid-offsetof  — WAL/audit/ml/position TU #include .cpp 模板实例化触发 (仅相关 target)

add_executable(test_slippage_model
    test_slippage_model.cpp
)
target_link_libraries(test_slippage_model PRIVATE GTest::gtest_main)
target_compile_options(test_slippage_model PRIVATE
    -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
    -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
    -Wno-cast-align)        # ADR-010 grandfather: gtest mock 转型
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
    -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
    -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
    -Wno-cast-align)        # ADR-010 grandfather: gtest mock 转型
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
    -Wno-invalid-offsetof   # ADR-010 grandfather: TU #include .cpp 模板实例化
    -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
    -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
    -Wno-cast-align         # ADR-010 grandfather: gtest mock 转型
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
# GM 错 #11 hotfix (W6 W2): test_audit_emitter 需链 stcpp_observability_audit 才能拿 blake3.h include 路径
target_link_libraries(test_audit_emitter PRIVATE GTest::gtest_main stcpp_observability_audit)
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
    -Wno-invalid-offsetof   # ADR-010 grandfather: TU #include .cpp 模板实例化
    -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
    -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
    -Wno-cast-align         # ADR-010 grandfather: gtest mock 转型
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
    -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
    -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
    -Wno-cast-align         # ADR-010 grandfather: gtest mock 转型
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
    -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
    -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
    -Wno-cast-align         # ADR-010 grandfather: gtest mock 转型
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
    -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
    -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
    -Wno-cast-align)        # ADR-010 grandfather: gtest mock 转型
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
    -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
    -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
    -Wno-cast-align)        # ADR-010 grandfather: gtest mock 转型
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
    -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
    -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
    -Wno-cast-align)        # ADR-010 grandfather: gtest mock 转型
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

# 小卢 W6 Wave 28: P0-01 Goalserve multiplicative de-vig (ADR-008 实施)
#   T1: 公式 paper case (3 家 mock odds)
#   T2: 8-9 家完整 case (与 Pinnacle no-vig 结果差异 < 2%)
#   T3: < 3 家 fallback (valid=false, 信号不触发)
#   T4: 5 触发条件不变 (R-20 + 5 cond + size clip, 与 W4 25 测试逻辑等价)
#   T5: feature_snapshot_id ABI 不动 (小邓 ML column index 4/5 锁死)
add_executable(test_p0_01_goalserve_devig
    test_p0_01_goalserve_devig.cpp
)
target_link_libraries(test_p0_01_goalserve_devig PRIVATE
    GTest::gtest_main
    stcpp_strategy
)
target_compile_options(test_p0_01_goalserve_devig PRIVATE
    -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
    -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
    -Wno-cast-align)        # ADR-010 grandfather: gtest mock 转型
target_include_directories(test_p0_01_goalserve_devig PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_p0_01_goalserve_devig PRIVATE cxx_std_20)
target_compile_definitions(test_p0_01_goalserve_devig PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)
gtest_discover_tests(test_p0_01_goalserve_devig
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "unit;signal;strategy;adr-008;w6"
)

# 小董 W4 Wave 20: M4.5 7 hard gate statistical framework v1
#   G1-G7 × pass/fail (14 核心) + boundary (50/49, 8.0/8.01%) + NaN/Inf + bootstrap 确定性 + PIT
add_executable(test_gate_evaluator
    test_gate_evaluator.cpp
)
target_link_libraries(test_gate_evaluator PRIVATE
    GTest::gtest_main
    stcpp_stats
)
target_compile_options(test_gate_evaluator PRIVATE
    -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
    -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
    -Wno-cast-align)        # ADR-010 grandfather: gtest mock 转型
target_include_directories(test_gate_evaluator PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_gate_evaluator PRIVATE cxx_std_20)
target_compile_definitions(test_gate_evaluator PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)
gtest_discover_tests(test_gate_evaluator
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "unit;stats;gate;w4"
)

# 小袁 W4 Wave 20: FillRateModel v0.1 + 8 sport profile + Microprobe 20 测试
add_executable(test_fill_rate_model
    test_fill_rate_model.cpp
)
target_link_libraries(test_fill_rate_model PRIVATE
    GTest::gtest_main
    stcpp_microstructure
    stcpp_data_goalserve
)
target_compile_options(test_fill_rate_model PRIVATE
    -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
    -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
    -Wno-cast-align)        # ADR-010 grandfather: gtest mock 转型
target_include_directories(test_fill_rate_model PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_fill_rate_model PRIVATE cxx_std_20)
target_compile_definitions(test_fill_rate_model PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)
gtest_discover_tests(test_fill_rate_model
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "unit;microstructure;w4"
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
    -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
    -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
    -Wno-cast-align)        # ADR-010 grandfather: gtest mock 转型
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

# 老李 W6 Wave 29: IPolymarketClient ABI lock (Smell #C handshake)
#   T1: 14 接口 ABI hash 表 (与 handshake 文档 §2 一致)
#   T2: TimestampQuad sizeof=40 + 5 offsetof; SignedOrder/OrderAck 字段存在性
#   T3: PMErrorKind 10 值 + OrderStatus 7 值不变
#   T4: TimestampQuad 4 ts PIT chain 语义 + DataSourceTsSource 4 值锁定
# paper + live 双 mode 都链 (ABI lock 是共享 core, R-7 不影响本测试)
add_executable(test_pm_client_abi
    test_pm_client_abi.cpp
)
target_link_libraries(test_pm_client_abi PRIVATE
    GTest::gtest_main
    stcpp_infra_wal
)
target_compile_options(test_pm_client_abi PRIVATE
    -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
    -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
    -Wno-cast-align         # ADR-010 grandfather: gtest mock 转型
    -Wno-invalid-offsetof
)
target_include_directories(test_pm_client_abi PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_pm_client_abi PRIVATE cxx_std_20)
target_compile_definitions(test_pm_client_abi PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)
gtest_discover_tests(test_pm_client_abi
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "unit;polymarket;abi-lock;w6"
)

# 老李 W5 Wave 24: PaperPolymarketClient v0.1 (14 接口 paper mock, R-7/R-11/R-20 enforce)
# 仅 paper mode link (R-7 物理隔离, live mode 此 target 不存在)
if(STCPP_EXEC_MODE STREQUAL "paper")
    add_executable(test_paper_pm_client
        test_paper_pm_client.cpp
    )
    target_link_libraries(test_paper_pm_client PRIVATE
        GTest::gtest_main
        stcpp_polymarket_paper
        stcpp_infra_wal
    )
    target_compile_options(test_paper_pm_client PRIVATE
        -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
        -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
        -Wno-cast-align)        # ADR-010 grandfather: gtest mock 转型
    target_include_directories(test_paper_pm_client PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_features(test_paper_pm_client PRIVATE cxx_std_20)
    target_compile_definitions(test_paper_pm_client PRIVATE
        STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
    )
    gtest_discover_tests(test_paper_pm_client
        DISCOVERY_TIMEOUT 30
        PROPERTIES LABELS "unit;polymarket;paper;w5"
    )
endif()

# 小邓 W4 Wave 20: MLDataHook v0.1 (FeatureSnapshot + TrainingLabel + 4 stage join)
# stcpp_ml lib R-7 guard: 仅 paper / backtest 模式编译 (含 VirtualFill)
if(STCPP_EXEC_MODE STREQUAL "paper" OR STCPP_EXEC_MODE STREQUAL "backtest")
    add_executable(test_ml_hook
        test_ml_hook.cpp
    )
    target_link_libraries(test_ml_hook PRIVATE
        GTest::gtest_main
        stcpp_ml
        stcpp_infra_wal
        stcpp_risk
        stcpp_execution_paper
        stcpp_strategy
    )
    target_include_directories(test_ml_hook PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_features(test_ml_hook PRIVATE cxx_std_20)
    target_compile_definitions(test_ml_hook PRIVATE
        STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
    )
    # TU 内 #include hook.cpp 模板实例化 — 关 offsetof / cast 警告同 audit_emitter 模式
    target_compile_options(test_ml_hook PRIVATE
        -Wno-invalid-offsetof   # ADR-010 grandfather: TU #include .cpp 模板实例化
        -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
        -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
        -Wno-cast-align         # ADR-010 grandfather: gtest mock 转型
    )
    gtest_discover_tests(test_ml_hook
        DISCOVERY_TIMEOUT 30
        PROPERTIES LABELS "unit;ml;w4"
    )
endif()

# 小卢 W5 Wave 25: SingleInstanceLock 7 用例 (laozhou-single-instance-spec-v1.md §3.1)
#   T1 acquire→release→reacquire  T2 双进程 child 失败  T3 kill-9 后 reacquire
#   T4 paper/live/backtest 物理隔离  T5 PID file 内容格式  T6 SIGTERM unlink  T7 race 1-wins
# 小卢 W6 Wave 30: FdGuard T8 RAII lifecycle; T9 SIGTERM handler close fd (非死代码, P1-03)
#   stcpp_process_lock_test: STCPP_TEST_BUILD=1, path_for() 用 /tmp/stcpp_test_<PID>
#   保证 ctest -j 8 并发进程间 PID file 物理隔离 (R-7 仅约束生产 binary)
add_executable(test_single_instance
    test_single_instance.cpp
)
target_link_libraries(test_single_instance PRIVATE
    GTest::gtest_main
    stcpp_process_lock_test
)
target_compile_options(test_single_instance PRIVATE
    -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
    -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
    -Wno-cast-align         # ADR-010 grandfather: gtest mock 转型
    # Wave 81 fix: POSIX read/write in fork/pipe sync — GCC 13 warn_unused_result strict
    # even with explicit (void) cast. ADR-010 §2.2 grandfather: test-only POSIX IO.
    -Wno-unused-result)
target_include_directories(test_single_instance PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_single_instance PRIVATE cxx_std_20)
target_compile_definitions(test_single_instance PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
    STCPP_PID_DIR="/tmp/stcpp_test"
    STCPP_BUILD_COMMIT="test-build"
)
gtest_discover_tests(test_single_instance
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "unit;process;single-instance;w5;w6"
)

# 老王 W6 Wave 28: Position WAL P0 (老韩 Smell-3 升级, GM Wave 27 拍板)
#   T1 apply_fill → query_position 单笔正确
#   T2 多笔 fill → position_total 累加 + entry_avg_price 加权均值
#   T3 WAL 落盘 → restore_from_wal 状态完全恢复 (老韩 #3 P0 核心: 崩溃重启)
#   T4 R-20 4 ts 透传 + PIT chain assert + violation 拒收
#   T5 4 wal kind 物理隔离 (paper position.wal 不出现在 live, R-11)
#   T6 circuit_breaker_state 与 RM evaluate 联动 mock (R-1)
#   T7 跨进程 SingleInstanceLock 防多开协同 (小卢 W5-A-09)
#   + ABI layout 152B + WalRecord concept 满足 + serialize roundtrip
add_executable(test_position_ledger
    test_position_ledger.cpp
)
target_link_libraries(test_position_ledger PRIVATE
    GTest::gtest_main
    stcpp_infra_wal
    stcpp_process_lock_test
)
target_compile_options(test_position_ledger PRIVATE
    -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
    -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
    -Wno-cast-align         # ADR-010 grandfather: gtest mock 转型
    -Wno-invalid-offsetof
)
target_include_directories(test_position_ledger PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_position_ledger PRIVATE cxx_std_20)
target_compile_definitions(test_position_ledger PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
    STCPP_POSITION_WAL_PREFIX="/var/lib/stcpp/paper/position"
    STCPP_PID_DIR="/tmp/stcpp_test"
    STCPP_BUILD_COMMIT="test-build"
)
gtest_discover_tests(test_position_ledger
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "unit;wal;position;w6;p0"
)

# 小冯 W5 Wave 24: PM WSS subscriber v0.1 (8 sub topic + reconnect + heartbeat + back-pressure)
#   T1  8 sub topic × 各 1 case (parse 4 ts)
#   T2  reconnect (transport disconnect → AsyncConnect 再调)
#   T3  heartbeat 超时 (now - last_msg_ts > heartbeat_timeout → Close + reconnect)
#   T4  back-pressure (SPSC 满 → drop + metrics_.frames_dropped_total)
#   T5  R-20 4 ts 不等式 enforce (event ≤ ds ≤ ingest ≤ as_of)
#   T6  subscribe frame 构造校验 (公开免鉴权, action subscribe)
#   T7  by_topic counter 各 +1
#   T8  parse error counter (非 JSON / unknown topic)
#   +   R-33 默认 URL 第 5 host, PONG 不入队
add_executable(test_pm_wss_subscriber
    test_pm_wss_subscriber.cpp
)
target_link_libraries(test_pm_wss_subscriber PRIVATE
    GTest::gtest_main
    stcpp_polymarket_wss
)
target_compile_options(test_pm_wss_subscriber PRIVATE
    -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
    -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
    -Wno-cast-align)        # ADR-010 grandfather: gtest mock 转型
target_include_directories(test_pm_wss_subscriber PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_pm_wss_subscriber PRIVATE cxx_std_20)
target_compile_definitions(test_pm_wss_subscriber PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)
gtest_discover_tests(test_pm_wss_subscriber
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "unit;polymarket;wss;w5"
)

# 小冯 Wave 79 (W9 W3): PolymarketCLOBSubscriber 3 ctest
#   T1: subscribe payload 双 token (assets_ids array, "Market" 大写 M, P-01/P-02)
#   T2: book event parse + 4-ts 填充 (R-20 data_source_ts=timestamp×1e6, P-03, future-guard)
#   T3: reconnect on disconnect (P-08 重发订阅 + heartbeat timeout, §5.1/§5.2)
# ADR-027 cite: laoli-w9-wss-subscriber-impl-spec-v1.md / ADR-027 Enforce-1
add_executable(test_clob_subscriber
    test_clob_subscriber.cpp
)
target_link_libraries(test_clob_subscriber PRIVATE
    GTest::gtest_main
    stcpp_polymarket_clob_wss
)
target_compile_options(test_clob_subscriber PRIVATE
    -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
    -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
    -Wno-cast-align         # ADR-010 grandfather: gtest mock 转型
)
target_include_directories(test_clob_subscriber PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_clob_subscriber PRIVATE cxx_std_20)
target_compile_definitions(test_clob_subscriber PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)
gtest_discover_tests(test_clob_subscriber
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "unit;polymarket;clob-wss;adr027;wave79;w9"
)

# 小冯 W6 Wave 29: IngestRaw WAL 第 5 WAL (DS-01 解锁)
#   T1: PM_WSS frame 落 paper.ingest_raw.wal (Tier 1 Accepted)
#   T2: 5 source_kind 各 1 case (PM_WSS + 4 GS_*)
#   T3: ring 满 fallback Tier 2 (不丢 metadata, R-12 不阻塞)
#   T4: R-20 4 ts UPSTREAM_PAYLOAD 优先 (data_source_ts=0 fallback + PIT reject)
#   T5: paper/live build 物理隔离 (R-11 / R-7)
#   T6: payload_blob 16KB 边界 (truncate vs reject)
#   T7: ML 训练读 ingest_raw.wal — WalRecord concept + serialize 还原
add_executable(test_ingest_raw
    test_ingest_raw.cpp
)
target_link_libraries(test_ingest_raw PRIVATE
    GTest::gtest_main
    stcpp_ingest_raw_writer
    stcpp_infra_wal
)
target_compile_options(test_ingest_raw PRIVATE
    -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
    -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
    -Wno-cast-align         # ADR-010 grandfather: gtest mock 转型
)
target_include_directories(test_ingest_raw PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_ingest_raw PRIVATE cxx_std_20)
target_compile_definitions(test_ingest_raw PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
    STCPP_INGEST_RAW_WAL_PREFIX="/var/lib/stcpp/ingest/paper"
)
gtest_discover_tests(test_ingest_raw
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "unit;wal;ingest-raw;ds-01;w6"
)

# 小石 W6 Wave 28: SPSC/MPMC ring framework v0.1 (P0 偏离 #4 修复)
#   T1: 单线程 FIFO 顺序
#   T2: 跨线程 SPSC 1000 笔不丢
#   T3: queue 满 try_push 返 false (R-12 back-pressure)
#   T4: 2^n + cache-line align (static_assert + layout)
#   T5: FillQueue 双 SPSC fanout — Position + ML 2-consumer
#   T6: latency bench (单 push p99 < 100ns)
#   T7 (bonus): drop_count 累加准确性
add_executable(test_spsc_queue
    test_spsc_queue.cpp
)
target_link_libraries(test_spsc_queue PRIVATE
    GTest::gtest_main
    stcpp_infra_spsc
)
target_compile_options(test_spsc_queue PRIVATE
    -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
    -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
    -Wno-cast-align         # ADR-010 grandfather: gtest mock 转型
)
target_include_directories(test_spsc_queue PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_spsc_queue PRIVATE cxx_std_20)
target_compile_definitions(test_spsc_queue PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)
gtest_discover_tests(test_spsc_queue
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit;spsc;lock-free;w6"
)

# 小段 W6 Wave 29: Goalserve schema v0.1 + data-contract (D-W5-03/05 解锁)
#   T1: URL builder 8 sport × 5 host 40 combos (BuildUrlFromSchema 路径)
#   T2: TimeStatus 11 enum 闭合 (schema 层语义)
#   T3: validate_payload PM_WSS 字段校验
#   T4: validate_payload GS_OddsFeed 字段校验
#   T5: 8-9 家 bookmaker ABI 锁 (ADR-008)
#   T6: 4 ts 字段位置正确性 (各 payload type)
add_executable(test_goalserve_schema
    test_goalserve_schema.cpp
)
target_link_libraries(test_goalserve_schema PRIVATE
    GTest::gtest_main
    stcpp_data_contract
)
target_compile_options(test_goalserve_schema PRIVATE
    -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
    -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
    -Wno-cast-align         # ADR-010 grandfather: gtest mock 转型
)
target_include_directories(test_goalserve_schema PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_goalserve_schema PRIVATE cxx_std_20)
target_compile_definitions(test_goalserve_schema PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)
gtest_discover_tests(test_goalserve_schema
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "unit;goalserve;schema;data-contract;adr-008;w6"
)

# 老沈 W6 Wave 29: STRATEGY_DECAYED 三签解锁 (Smell #4 M2 前必交)
#   T1: 正确 3 签 → OK
#   T2: 单签 (normal mode) → LAOHAN_FAILED
#   T3: 2 签缺 laotang → LAOTANG_FAILED
#   T4: 错签 pubkey mismatch → BAD_SIGNATURE / LAOTANG_FAILED
#   T5: 过期签 timestamp > 1h → EXPIRED
#   T6: audit chain 3 audit_id 唯一性
#   T7: 观察期 STRATEGY_DECAYED re-trigger (RM evaluate mock)
#   T8: emergency override 单签 → OK
# GM 错 #11 修 (2026-06-01): STCPP_BUILD_CLI guard, 默认 OFF
# 老沈 W7 改 FetchContent libsodium source (不走 vcpkg)
if(STCPP_BUILD_CLI)
    add_executable(test_strategy_unlock
        test_strategy_unlock.cpp
    )
    target_link_libraries(test_strategy_unlock PRIVATE
        GTest::gtest_main
        stcpp_risk
        stcpp_cli_three_sig
    )
    target_compile_options(test_strategy_unlock PRIVATE
        -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
        -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
        -Wno-cast-align         # ADR-010 grandfather: gtest mock 转型
    )
    target_include_directories(test_strategy_unlock PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_features(test_strategy_unlock PRIVATE cxx_std_20)
    target_compile_definitions(test_strategy_unlock PRIVATE
        STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
    )
    gtest_discover_tests(test_strategy_unlock
        DISCOVERY_TIMEOUT 30
        PROPERTIES LABELS "unit;risk;three-sig;strategy-unlock;w6;smell-4"
    )
endif()

# 老唐 W6 Wave 29: BLAKE3 真实现 + AuditEmitter pool (老周 Smell #A + 老韩 Smell #2)
#   T1: BLAKE3 hash 一致性 (同输入同输出, 不同输入不同输出, 非零)
#   T2: hash_chain 串联 3 record (chain 单调推进, 本地重算验证)
#   T3: chain verify 篡改检测 (M1-A06 必过)
#   T4: 5 emitter pool 各自独立 ring + 全局 chain 串联 (老周 Smell #A)
#   T5: build-time switch (BLAKE3_REAL=1 必定义; BLAKE3_STUB 禁止)
add_executable(test_blake3_audit
    test_blake3_audit.cpp
)
target_link_libraries(test_blake3_audit PRIVATE
    GTest::gtest_main
    stcpp_observability_audit
)
target_compile_options(test_blake3_audit PRIVATE
    -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
    -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
    -Wno-cast-align         # ADR-010 grandfather: gtest mock 转型
    -Wno-invalid-offsetof
)
target_include_directories(test_blake3_audit PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_blake3_audit PRIVATE cxx_std_20)
# R-7: paper/live build 都注入 BLAKE3_REAL=1 (M1-A06 前置条件, 老韩 Smell #2)
target_compile_definitions(test_blake3_audit PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
    BLAKE3_REAL=1
)
gtest_discover_tests(test_blake3_audit
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "unit;audit;blake3;w6;m1-a06;smell-a;smell-2"
)

# 老唐 W9 Wave 65: audit schema v1.3 cpp — 5 ctest (ADR-027 Enforce-1)
#   T1: AuditEvent v1.3 含 token_id round-trip (serialize → deserialize)
#   T2: outcome enum 序列化 (0-6 全 enum, uint8 round-trip)
#   T3: side enum 序列化 (0/1 Buy/Sell round-trip, is_buy v1.2 compat)
#   T4: BLAKE3 chain 新字段不破 hash 链 (T2_Chain_RecomputeMatches 仍 PASS)
#   T5: schema v1.2 → v1.3 migration (旧 log read 加 default 字段)
# cite: ADR-027 Enforce-1 / polymarket_ssot §3 §6 / handshake §3
add_executable(test_audit_schema_v13
    test_audit_schema_v13.cpp
)
target_link_libraries(test_audit_schema_v13 PRIVATE
    GTest::gtest_main
    stcpp_observability_audit
)
target_compile_options(test_audit_schema_v13 PRIVATE
    -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
    -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
    -Wno-cast-align         # ADR-010 grandfather: gtest mock 转型
    -Wno-invalid-offsetof   # ADR-010 grandfather: TU #include .cpp 模板实例化
)
target_include_directories(test_audit_schema_v13 PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_audit_schema_v13 PRIVATE cxx_std_20)
# R-7: BLAKE3_REAL=1 (T4 chain 验证依赖真 BLAKE3)
target_compile_definitions(test_audit_schema_v13 PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
    BLAKE3_REAL=1
)
gtest_discover_tests(test_audit_schema_v13
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "unit;audit;schema-v13;token_id;outcome;side;migration;adr027;w9"
)

# 老孙 W6 Wave 30: signer v5.2 cpp v0.1 (撤 Rust 后 C++ 重写)
#   T1-T8 + Bonus_R11 + Bonus_R20: 10 test targets
# R-7 物理隔离: paper mode only; libsodium homebrew prebuilt
if(STCPP_EXEC_MODE STREQUAL "paper")
    add_executable(test_signer_v52
        test_signer_v52.cpp
    )
    # stcpp_signer_v52_paper PUBLIC-links stcpp_signer_v52_sodium (INTERFACE),
    # which carries sodium include dir + lib + STCPP_SIGNER_V52_LIBSODIUM define.
    target_link_libraries(test_signer_v52 PRIVATE
        GTest::gtest_main
        stcpp_signer_v52_paper
        stcpp_infra_wal
    )
    target_compile_options(test_signer_v52 PRIVATE
        -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
        -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
        -Wno-cast-align         # ADR-010 grandfather: gtest mock 转型
    )
    target_include_directories(test_signer_v52 PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_features(test_signer_v52 PRIVATE cxx_std_20)
    target_compile_definitions(test_signer_v52 PRIVATE
        STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
    )
    gtest_discover_tests(test_signer_v52
        DISCOVERY_TIMEOUT 30
        PROPERTIES LABELS "unit;signer;v52;ed25519;ipc;w6"
    )

    # 老孙 W9 Wave 58: signer v5.3 新增 7 ctest (T1-T7)
    #   T1: token_id ABI binding (EIP-712 Order.tokenId)
    #   T2: side 0/1 round-trip + side=2 拒签
    #   T3: HMAC bug 4 反陷阱 (sigType=1 accept; sigType=2 reject — v5.3 修正)
    #   T4: SecureBuffer<64> sodium_memzero 维持
    #   T5: paper/live 共用 stcpp_crypto_ed25519
    #   T6: v5.1 cases migration (旧行为不回退)
    #   T7: P99 < 8us hot path (100k 次 CLOCK_MONOTONIC_RAW)
    # ADR-027 Enforce-1 cite: SSOT §3.5 + handshake §84
    add_executable(test_signer_v53_pit
        test_signer_v53_pit.cpp
    )
    target_link_libraries(test_signer_v53_pit PRIVATE
        GTest::gtest_main
        stcpp_signer_v52_paper
        stcpp_infra_wal
    )
    target_compile_options(test_signer_v53_pit PRIVATE
        -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
        -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
        -Wno-cast-align         # ADR-010 grandfather: gtest mock 转型
    )
    target_include_directories(test_signer_v53_pit PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_features(test_signer_v53_pit PRIVATE cxx_std_20)
    target_compile_definitions(test_signer_v53_pit PRIVATE
        STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
    )
    gtest_discover_tests(test_signer_v53_pit
        DISCOVERY_TIMEOUT 60
        PROPERTIES LABELS "unit;signer;v53;token_id;side;adr027;w9"
    )
endif()

# 老沈 Wave 76 W9 W4: PositionLedger read API + StateMachine cpp (ADR-027 Enforce-1)
#   TC-01: 快照一致性 (read 不锁 hot path)
#   TC-02: CAS 幂等 (try_transition RUNNING->DRAIN 2次, 1成1fail)
#   TC-03: DRAIN 平仓放行 (is_close+Sell)
#   TC-04: reject ring tail_copy (HALTED reject, audit_id 非零)
# cite: ADR-027 Enforce-1 / polymarket_ssot §2.3 §3.3 / handshake §3
add_executable(test_position_ledger_w76
    test_position_ledger_w76.cpp
)
target_link_libraries(test_position_ledger_w76 PRIVATE
    GTest::gtest_main
    stcpp_risk
)
target_compile_options(test_position_ledger_w76 PRIVATE
    -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
    -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
    -Wno-cast-align         # ADR-010 grandfather: gtest mock 转型
)
target_include_directories(test_position_ledger_w76 PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(test_position_ledger_w76 PRIVATE cxx_std_20)
target_compile_definitions(test_position_ledger_w76 PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)
gtest_discover_tests(test_position_ledger_w76
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS "unit;risk;position-ledger;state-machine;adr027;wave76;w9"
)
