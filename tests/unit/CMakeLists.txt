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
    -Wno-double-promotion -Wno-sign-conversion -Wno-conversion
    -Wno-shadow -Wno-old-style-cast -Wno-cast-align)
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
    -Wno-double-promotion -Wno-sign-conversion -Wno-conversion
    -Wno-shadow -Wno-old-style-cast -Wno-cast-align)
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
    -Wno-double-promotion -Wno-sign-conversion -Wno-conversion
    -Wno-shadow -Wno-old-style-cast -Wno-cast-align)
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
        -Wno-double-promotion -Wno-sign-conversion -Wno-conversion
        -Wno-shadow -Wno-old-style-cast -Wno-cast-align)
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
        -Wno-invalid-offsetof
        -Wno-double-promotion -Wno-sign-conversion -Wno-conversion
        -Wno-shadow -Wno-old-style-cast -Wno-cast-align
    )
    gtest_discover_tests(test_ml_hook
        DISCOVERY_TIMEOUT 30
        PROPERTIES LABELS "unit;ml;w4"
    )
endif()

# 小卢 W5 Wave 25: SingleInstanceLock 7 用例 (laozhou-single-instance-spec-v1.md §3.1)
#   T1 acquire→release→reacquire  T2 双进程 child 失败  T3 kill-9 后 reacquire
#   T4 paper/live/backtest 物理隔离  T5 PID file 内容格式  T6 SIGTERM unlink  T7 race 1-wins
add_executable(test_single_instance
    test_single_instance.cpp
)
target_link_libraries(test_single_instance PRIVATE
    GTest::gtest_main
    stcpp_process_lock
)
target_compile_options(test_single_instance PRIVATE
    -Wno-double-promotion -Wno-sign-conversion -Wno-conversion
    -Wno-shadow -Wno-old-style-cast -Wno-cast-align)
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
    PROPERTIES LABELS "unit;process;single-instance;w5"
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
    stcpp_process_lock
)
target_compile_options(test_position_ledger PRIVATE
    -Wno-double-promotion -Wno-sign-conversion -Wno-conversion
    -Wno-shadow -Wno-old-style-cast -Wno-cast-align
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
    -Wno-double-promotion -Wno-sign-conversion -Wno-conversion
    -Wno-shadow -Wno-old-style-cast -Wno-cast-align)
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
    -Wno-double-promotion -Wno-sign-conversion -Wno-conversion
    -Wno-shadow -Wno-old-style-cast -Wno-cast-align
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
