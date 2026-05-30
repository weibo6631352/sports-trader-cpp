# tests/unit/CMakeLists.txt — gtest 单测
#
# ADR-039 §9 F2: CMake 测试自动发现重构 (老吴, 2026-05-29)
#   - file(GLOB CONFIGURE_DEPENDS "test_*.cpp") + foreach 循环统一注册
#   - per-test 特殊 link 依赖 / 编译宏 / 超时 / label 通过映射表注入
#   - 新增测试只需新建 test_xxx.cpp，不需要修改本文件
#   - 并行 worktree 不再在此冲突 (ADR-039 §10 §13 F2)
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

# ============================================================================
# §1  公共 -Wno grandfather (ADR-010 §2.2, 全 unit test target 适用)
# ============================================================================
set(_STCPP_UNIT_COMMON_OPTS
    -Wno-double-promotion   # ADR-010 grandfather: gtest 浮点宏
    -Wno-old-style-cast     # ADR-010 grandfather: gtest 宏展开
    -Wno-cast-align         # ADR-010 grandfather: gtest mock 转型
)

# ============================================================================
# §2  映射表: per-test 特殊属性
#
#   EXTRA_LIBS_<target>   — 追加链接库 (空 = 仅 GTest::gtest_main)
#   EXTRA_DEFS_<target>   — 追加 compile_definitions (STCPP_EXEC_MODE_* 已在循环中统一注入)
#   EXTRA_OPTS_<target>   — 追加 compile_options (在公共 grandfather 之外)
#   TIMEOUT_<target>      — gtest_discover_tests DISCOVERY_TIMEOUT (默认 30)
#   LABELS_<target>       — gtest_discover_tests PROPERTIES LABELS (默认 "unit")
# ============================================================================

# --- 链接库映射 ---
set(EXTRA_LIBS_test_audit_emitter       stcpp_observability_audit)
set(EXTRA_LIBS_test_risk_gateway        stcpp_risk)
set(EXTRA_LIBS_test_risk_gateway_wave3  stcpp_risk)
# Phase B buy-NO: RM token-agnostic + per-condition cap 合并专项 (老韩 checklist T-NO-1..12)
set(EXTRA_LIBS_test_risk_gateway_buy_no stcpp_risk)
# 老沈 rm_debug_snapshot D1: lock-free double-buffer ring (feat/laoshen-rm-debug-snapshot)
set(EXTRA_LIBS_test_rm_debug_snapshot   stcpp_risk)
set(EXTRA_LIBS_test_goalserve_client    stcpp_data_goalserve)
set(EXTRA_LIBS_test_paper_signer        stcpp_signer_paper stcpp_infra_wal)
set(EXTRA_LIBS_test_virtual_matcher     stcpp_execution_paper stcpp_infra_wal)
set(EXTRA_LIBS_test_p0_01_signal        stcpp_strategy)
set(EXTRA_LIBS_test_p0_01_goalserve_devig stcpp_strategy)
set(EXTRA_LIBS_test_gate_evaluator      stcpp_stats)
set(EXTRA_LIBS_test_fill_rate_model     stcpp_microstructure stcpp_data_goalserve)
set(EXTRA_LIBS_test_live_section_classifier stcpp_strategy)
set(EXTRA_LIBS_test_pm_client_abi       stcpp_infra_wal)
# V2 EIP-712 digest 编码锁定 (GM 2026-05-31 固化, 实盘验证值防回归)
set(EXTRA_LIBS_test_eip712_v2           stcpp_crypto_eip712)
# secp256k1 签名/recover/地址推导 (GM 2026-05-31, 公开测试向量)
set(EXTRA_LIBS_test_secp256k1_signer    stcpp_crypto_secp256k1)
set(EXTRA_LIBS_test_fair_value_model    stcpp_ml_inference)
set(EXTRA_LIBS_test_single_instance     stcpp_process_lock_test)
set(EXTRA_LIBS_test_position_ledger     stcpp_infra_wal stcpp_process_lock_test)
set(EXTRA_LIBS_test_pm_wss_subscriber   stcpp_polymarket_wss)
set(EXTRA_LIBS_test_clob_subscriber     stcpp_polymarket_clob_wss)
set(EXTRA_LIBS_test_ingest_raw          stcpp_ingest_raw_writer stcpp_infra_wal)
set(EXTRA_LIBS_test_spsc_queue          stcpp_infra_spsc)
set(EXTRA_LIBS_test_goalserve_schema    stcpp_data_contract)
set(EXTRA_LIBS_test_blake3_audit        stcpp_observability_audit)
set(EXTRA_LIBS_test_audit_schema_v13    stcpp_observability_audit)
set(EXTRA_LIBS_test_audit_schema_v14    stcpp_observability_audit)
set(EXTRA_LIBS_test_position_ledger_w76 stcpp_risk)
set(EXTRA_LIBS_test_outbound_serializer stcpp_net_outbound stcpp_infra_wal stcpp_infra_spsc)
set(EXTRA_LIBS_test_goalserve_adapter   stcpp_data_contract)
set(EXTRA_LIBS_test_fair_value_estimator stcpp_pricing)
set(EXTRA_LIBS_test_backtest_types      stcpp_backtest)
set(EXTRA_LIBS_test_backtest_walk_forward stcpp_backtest)
set(EXTRA_LIBS_test_backtest_metrics    stcpp_backtest)
set(EXTRA_LIBS_test_backtest_stats      stcpp_backtest)
set(EXTRA_LIBS_test_backtest_event_replayer stcpp_backtest stcpp_microstructure)
set(EXTRA_LIBS_test_feature_store_contract stcpp_data_contract)
set(EXTRA_LIBS_test_orderbook_adapter   stcpp_polymarket_clob_wss)
set(EXTRA_LIBS_test_orderbook_snapshot_hub stcpp_orderbook_snapshot_hub)
set(EXTRA_LIBS_test_score_snapshot_store  stcpp_data_inplay_parser stcpp_data_score_store)
# 小卢 2026-05-30: 覆盖率/识别率 metric 单测 (ADR-038 §4.4; market_type/discovered/score_matched)
set(EXTRA_LIBS_test_coverage_metrics
    stcpp_orderbook_snapshot_hub
    stcpp_data_score_store
    stcpp_risk
    stcpp_ledger_snapshot_hub
    stcpp_quote_snapshot_hub
    stcpp_sizing
)
# 小段 2026-05-30: Goalserve inplay parser 安全加固单测 (gzip-bomb/max-depth/escape/log-injection)
set(EXTRA_LIBS_test_inplay_parser_harden  stcpp_data_inplay_parser)
set(EXTRA_LIBS_test_sizing_calculator     stcpp_sizing stcpp_risk)
set(EXTRA_LIBS_test_sizing_rm_consistency stcpp_sizing stcpp_risk)
# 小肖 ReplayDriver: replay→hub→Read 端对端路径验证
set(EXTRA_LIBS_test_replay_driver         stcpp_backtest stcpp_orderbook_snapshot_hub)
# 小石 LedgerSnapshotHub: per-market double-buffer 持仓/PnL 只读快照 (R-11/R-12/R-20)
set(EXTRA_LIBS_test_ledger_snapshot_hub   stcpp_ledger_snapshot_hub)
# 小石 QuoteSnapshotHub: per-market double-buffer 量化报价只读快照 (R-12/R-20 + ML provenance)
set(EXTRA_LIBS_test_quote_snapshot_hub    stcpp_quote_snapshot_hub)
# 小肖 PaperLoop: 最小 paper 交易循环 (R-11/R-12/R-20; feat/xiaoxiao-paper-loop)
set(EXTRA_LIBS_test_paper_loop
    stcpp_paper_loop
    stcpp_orderbook_snapshot_hub
    stcpp_risk
    stcpp_ledger_snapshot_hub
    stcpp_quote_snapshot_hub
    stcpp_pricing
    stcpp_sizing
    stcpp_signer_paper
    stcpp_execution_paper
    stcpp_infra_wal
)

# --- 追加 compile_definitions 映射 ---
# (仅列出 STCPP_EXEC_MODE_* 以外的额外 define)
set(EXTRA_DEFS_test_blake3_audit        BLAKE3_REAL=1)
set(EXTRA_DEFS_test_audit_schema_v13    BLAKE3_REAL=1)
set(EXTRA_DEFS_test_audit_schema_v14    BLAKE3_REAL=1)
set(EXTRA_DEFS_test_single_instance
    STCPP_PID_DIR="/tmp/stcpp_test"
    STCPP_BUILD_COMMIT="test-build"
)
set(EXTRA_DEFS_test_position_ledger
    STCPP_POSITION_WAL_PREFIX="/var/lib/stcpp/paper/position"
    STCPP_PID_DIR="/tmp/stcpp_test"
    STCPP_BUILD_COMMIT="test-build"
)
set(EXTRA_DEFS_test_ingest_raw          STCPP_INGEST_RAW_WAL_PREFIX="/var/lib/stcpp/ingest/paper")

# --- 追加 compile_options 映射 ---
# (在公共 grandfather 3 项之外追加)
set(EXTRA_OPTS_test_wal_writer          -Wno-invalid-offsetof)
set(EXTRA_OPTS_test_audit_emitter       -Wno-invalid-offsetof)
set(EXTRA_OPTS_test_ml_hook             -Wno-invalid-offsetof)
set(EXTRA_OPTS_test_pm_client_abi       -Wno-invalid-offsetof)
set(EXTRA_OPTS_test_position_ledger     -Wno-invalid-offsetof)
set(EXTRA_OPTS_test_blake3_audit        -Wno-invalid-offsetof)
set(EXTRA_OPTS_test_audit_schema_v13    -Wno-invalid-offsetof)
set(EXTRA_OPTS_test_audit_schema_v14    -Wno-invalid-offsetof)
# backtest group: -Wno-conversion (size_t → double 统计计算)
set(EXTRA_OPTS_test_backtest_types          -Wno-conversion)
set(EXTRA_OPTS_test_backtest_walk_forward   -Wno-conversion)
set(EXTRA_OPTS_test_backtest_metrics        -Wno-conversion)
set(EXTRA_OPTS_test_backtest_stats          -Wno-conversion)
set(EXTRA_OPTS_test_backtest_event_replayer -Wno-conversion)

# --- DISCOVERY_TIMEOUT 映射 (默认 30) ---
set(TIMEOUT_test_spsc_queue         60)
set(TIMEOUT_test_signer_v53_pit     60)
set(TIMEOUT_test_signer_v62         60)
set(TIMEOUT_test_backtest_metrics   60)
set(TIMEOUT_test_replay_driver      60)
set(TIMEOUT_test_paper_loop         60)

# --- LABELS 映射 (默认 "unit") ---
set(LABELS_test_slippage_model             "unit")
set(LABELS_test_wal_writer                 "unit;wal;w3")
set(LABELS_test_audit_emitter              "unit;audit;w4")
set(LABELS_test_risk_gateway               "unit;risk;w4")
set(LABELS_test_risk_gateway_wave3         "unit;risk;wave3;dd-soft;ts-v2;fee-estimate;audit-v14;p0")
# 老沈 rm_debug_snapshot D1
set(LABELS_test_rm_debug_snapshot          "unit;risk;rm-debug-snapshot;lock-free;tsan;blacklist;d1")
set(LABELS_test_goalserve_client           "unit;goalserve;w4")
set(LABELS_test_paper_signer               "unit;paper;signer;w4")
set(LABELS_test_virtual_matcher            "unit;paper;matcher;w4")
set(LABELS_test_p0_01_signal               "unit;signal;strategy;w4")
set(LABELS_test_p0_01_goalserve_devig      "unit;signal;strategy;adr-008;w6")
set(LABELS_test_gate_evaluator             "unit;stats;gate;w4")
set(LABELS_test_fill_rate_model            "unit;microstructure;w4")
set(LABELS_test_fill_rate_model_v2         "unit;microstructure;clob;mode-a;wave3")
set(LABELS_test_live_section_classifier    "unit;signal;strategy;w4")
set(LABELS_test_pm_client_abi              "unit;polymarket;abi-lock;w6")
set(LABELS_test_paper_pm_client            "unit;polymarket;paper;w5")
set(LABELS_test_ml_hook                    "unit;ml;w4")
set(LABELS_test_fair_value_model           "unit;ml;adr037")
set(LABELS_test_single_instance            "unit;process;single-instance;w5;w6")
set(LABELS_test_position_ledger            "unit;wal;position;w6;p0")
set(LABELS_test_pm_wss_subscriber          "unit;polymarket;wss;w5")
set(LABELS_test_clob_subscriber            "unit;polymarket;clob-wss;adr027;wave79;w9;security;overflow-safe;xiaobai-audit")
set(LABELS_test_ingest_raw                 "unit;wal;ingest-raw;ds-01;w6")
set(LABELS_test_spsc_queue                 "unit;spsc;lock-free;w6")
set(LABELS_test_goalserve_schema           "unit;goalserve;schema;data-contract;adr-008;w6")
set(LABELS_test_strategy_unlock            "unit;risk;three-sig;strategy-unlock;w6;smell-4")
set(LABELS_test_blake3_audit               "unit;audit;blake3;w6;m1-a06;smell-a;smell-2")
set(LABELS_test_audit_schema_v13           "unit;audit;schema-v13;token_id;outcome;side;migration;adr027;w9")
set(LABELS_test_audit_schema_v14           "unit;audit;schema-v14;timestamp_ms;metadata;builder;pUSD;migration;adr027;wave98;w10")
set(LABELS_test_signer_v52                 "unit;signer;v52;ed25519;ipc;w6")
set(LABELS_test_signer_v53_pit             "unit;signer;v53;token_id;side;adr027;w9")
set(LABELS_test_signer_v62                 "unit;signer;v62;transformer;adr027;wave104;w10")
set(LABELS_test_position_ledger_w76        "unit;risk;position-ledger;state-machine;adr027;wave76;w9")
set(LABELS_test_outbound_serializer        "unit;net;outbound;serializer;backpressure;a-net-01;wave3")
set(LABELS_test_goalserve_adapter          "unit;goalserve;adapter;vendor-agnostic;adr-037")
set(LABELS_test_fair_value_estimator       "unit;pricing;fair-value;numerical;adr-037")
set(LABELS_test_backtest_types             "unit;backtest;types;r20;pit;backtest-v0.2")
set(LABELS_test_backtest_walk_forward      "unit;backtest;walk-forward;guard;purged-kfold;backtest-v0.2")
set(LABELS_test_backtest_metrics           "unit;backtest;metrics;sharpe;mdd;bootstrap;backtest-v0.2")
set(LABELS_test_backtest_stats             "unit;backtest;stats;bonferroni;dsr;overfitting;backtest-v0.2")
set(LABELS_test_backtest_event_replayer    "unit;backtest;event-replayer;ledger;fill-rate;br1;br5;backtest-v0.2")
set(LABELS_test_feature_store_contract     "unit;feature-store;data-contract;adr-037;vendor-agnostic;pit")
set(LABELS_test_orderbook_adapter          "unit;polymarket;clob-wss;orderbook-adapter;r-20;r-12;v0.1")
set(LABELS_test_orderbook_snapshot_hub     "unit;polymarket;clob-wss;snapshot-hub;double-buffer;r-12;r-20;v0.1")
# 小肖 ReplayDriver v0.1
set(LABELS_test_replay_driver              "unit;backtest;replay-driver;r-12;r-20;synthetic;historical;e2e")
# 小肖 PaperLoop v0.1 (feat/xiaoxiao-paper-loop, 2026-05-30)
set(LABELS_test_paper_loop                 "unit;paper;paper-loop;r-11;r-12;r-20;ci-gating;kelly;xiaoxiao")
# 小石 LedgerSnapshotHub v0.1
set(LABELS_test_ledger_snapshot_hub        "unit;risk;ledger;double-buffer;r-11;r-12;r-20;xiaoshi")
# 小石 QuoteSnapshotHub v0.1
set(LABELS_test_quote_snapshot_hub         "unit;sizing;quote;double-buffer;r-12;r-20;ml-provenance;xiaoshi")
set(LABELS_test_inplay_parser_harden       "unit;goalserve;inplay;security;harden;gzip-bomb;max-depth;log-injection;xiaobai-audit")
# 小卢 2026-05-30: 覆盖率/识别率 metric 单测
set(LABELS_test_coverage_metrics           "unit;debug-api;metrics;coverage;market-type;score-matched;adr-038;xiaolu")

# ============================================================================
# §3  核心辅助宏: stcpp_add_unit_test(<target> <source>)
#     注册一个 unit test target, 应用公共配置 + 从映射表注入特殊属性
# ============================================================================
macro(stcpp_add_unit_test _target _source)
    add_executable(${_target} ${_source})

    # 公共链接: GTest::gtest_main + 特殊库
    target_link_libraries(${_target} PRIVATE
        GTest::gtest_main
        ${EXTRA_LIBS_${_target}}
    )

    # 公共 include 路径
    target_include_directories(${_target} PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )

    # C++20 标准
    target_compile_features(${_target} PRIVATE cxx_std_20)

    # 公共 define + 特殊 define
    target_compile_definitions(${_target} PRIVATE
        STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
        ${EXTRA_DEFS_${_target}}
    )

    # 公共 grandfather 选项 + 特殊选项
    target_compile_options(${_target} PRIVATE
        ${_STCPP_UNIT_COMMON_OPTS}
        ${EXTRA_OPTS_${_target}}
    )

    # DISCOVERY_TIMEOUT: 优先映射表, 否则默认 30
    if(DEFINED TIMEOUT_${_target})
        set(_timeout ${TIMEOUT_${_target}})
    else()
        set(_timeout 30)
    endif()

    # LABELS: 优先映射表, 否则默认 "unit"
    if(DEFINED LABELS_${_target})
        set(_labels "${LABELS_${_target}}")
    else()
        set(_labels "unit")
    endif()

    gtest_discover_tests(${_target}
        DISCOVERY_TIMEOUT ${_timeout}
        PROPERTIES LABELS "${_labels}"
    )
endmacro()

# ============================================================================
# §4  legacy 异常 target: stcpp_test_unit_risk
#     target 名与 source 名均非 test_*.cpp 规范, 单独注册
#     标签 "unit;risk;w3-placeholder" 同原来
# ============================================================================
set(LABELS_stcpp_test_unit_risk "unit;risk;w3-placeholder")
stcpp_add_unit_test(stcpp_test_unit_risk risk_enum_coverage_test.cpp)

# ============================================================================
# §5  无条件 test_*.cpp 自动发现 + 注册
#     (条件编译 target 排除在此列表之外, 在 §6 各条件块中单独处理)
# ============================================================================

# 条件编译 target 文件名集合 (用于从 GLOB 中排除, 避免在不满足条件时尝试注册)
set(_STCPP_CONDITIONAL_SRCS
    test_fill_rate_model_v2.cpp     # paper only
    test_paper_pm_client.cpp        # paper only
    test_ml_hook.cpp                # paper|backtest only
    test_signer_v52.cpp             # paper only
    test_signer_v53_pit.cpp         # paper only
    test_signer_v62.cpp             # paper only
    test_paper_daemon.cpp           # paper only (links stcpp_paper_app)
    test_market_discovery.cpp       # paper only (links stcpp_paper_app; 小宋 MD-01..47)
    test_event_matcher.cpp          # paper only (links stcpp_paper_app; A0 映射桥)
    test_strategy_unlock.cpp        # STCPP_BUILD_CLI only
    # deferred / not-yet-buildable stubs (target library not yet implemented):
    test_parquet_writer_stub.cpp    # W7+ HC-06 deferred; stcpp_data_parquet not yet built
)

file(GLOB _ALL_UNIT_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")

foreach(_src ${_ALL_UNIT_SRCS})
    get_filename_component(_fname "${_src}" NAME)
    get_filename_component(_stem  "${_src}" NAME_WE)  # stem = test_xxx

    # 跳过条件编译 target (由 §6 各条件块处理)
    if(_fname IN_LIST _STCPP_CONDITIONAL_SRCS)
        continue()
    endif()

    stcpp_add_unit_test(${_stem} ${_fname})
endforeach()

# ============================================================================
# §6  条件编译 target (EXEC_MODE guard / BUILD_CLI guard)
# ============================================================================

# --- paper only ---
if(STCPP_EXEC_MODE STREQUAL "paper")

    # 小袁 Wave 3: FillRateModel v0.2 (compute_from_clob_book) + VirtualMatcher Mode A
    set(EXTRA_LIBS_test_fill_rate_model_v2
        stcpp_execution_paper
        stcpp_microstructure
        stcpp_infra_wal
    )
    stcpp_add_unit_test(test_fill_rate_model_v2 test_fill_rate_model_v2.cpp)

    # 老李 W5 Wave 24: PaperPolymarketClient v0.1 (14 接口 paper mock, R-7/R-11/R-20 enforce)
    set(EXTRA_LIBS_test_paper_pm_client
        stcpp_polymarket_paper
        stcpp_infra_wal
    )
    stcpp_add_unit_test(test_paper_pm_client test_paper_pm_client.cpp)

    # 老雷 PaperDaemon 重构 (老郭 §A.1): 装配 + R-11 detach 生命周期回归 (老韩审计要求)
    set(EXTRA_LIBS_test_paper_daemon
        stcpp_paper_app
    )
    set(LABELS_test_paper_daemon "unit;paper;paper-daemon;r-11;assembly;lifecycle;laolei")
    stcpp_add_unit_test(test_paper_daemon test_paper_daemon.cpp)

    # 老孙 W6 Wave 30: signer v5.2 cpp v0.1 (撤 Rust 后 C++ 重写)
    set(EXTRA_LIBS_test_signer_v52
        stcpp_signer_v52_paper
        stcpp_infra_wal
    )
    stcpp_add_unit_test(test_signer_v52 test_signer_v52.cpp)

    # 老孙 W9 Wave 58: signer v5.3 新增 7 ctest
    set(EXTRA_LIBS_test_signer_v53_pit
        stcpp_signer_v52_paper
        stcpp_infra_wal
    )
    stcpp_add_unit_test(test_signer_v53_pit test_signer_v53_pit.cpp)

    # 老孙 Wave 104 P0: SignerV62 v6.2 + transformer_v62 5 ctest
    set(EXTRA_LIBS_test_signer_v62
        stcpp_signer_v62_paper
        stcpp_signer_v52_paper
        stcpp_infra_wal
    )
    # STCPP_CLOB_V2=1 追加 define
    list(APPEND EXTRA_DEFS_test_signer_v62 STCPP_CLOB_V2=1)
    stcpp_add_unit_test(test_signer_v62 test_signer_v62.cpp)

    # 小宋 market_discovery 单测草案 MD-01..47
    # discovery_detail 纯函数 + ParseSports* Parse 层 fixture 测试 (无网络)
    set(EXTRA_LIBS_test_market_discovery stcpp_paper_app)
    set(LABELS_test_market_discovery "unit;paper;market-discovery;gamma;parse;xiaosong")
    stcpp_add_unit_test(test_market_discovery test_market_discovery.cpp)

    # A0 映射桥 EventMatcher (condition_id↔goalserve event, fail-closed)
    set(EXTRA_LIBS_test_event_matcher stcpp_paper_app)
    set(LABELS_test_event_matcher "unit;paper;event-matcher;a0;mapping;fail-closed;laolei")
    stcpp_add_unit_test(test_event_matcher test_event_matcher.cpp)

endif()

# --- paper OR backtest ---
if(STCPP_EXEC_MODE STREQUAL "paper" OR STCPP_EXEC_MODE STREQUAL "backtest")

    # 小邓 W4 Wave 20: MLDataHook v0.1 (FeatureSnapshot + TrainingLabel + 4 stage join)
    set(EXTRA_LIBS_test_ml_hook
        stcpp_ml
        stcpp_infra_wal
        stcpp_risk
        stcpp_execution_paper
        stcpp_strategy
    )
    stcpp_add_unit_test(test_ml_hook test_ml_hook.cpp)

endif()

# --- STCPP_BUILD_CLI guard ---
if(STCPP_BUILD_CLI)

    # 老沈 W6 Wave 29: STRATEGY_DECAYED 三签解锁
    set(EXTRA_LIBS_test_strategy_unlock
        stcpp_risk
        stcpp_cli_three_sig
    )
    stcpp_add_unit_test(test_strategy_unlock test_strategy_unlock.cpp)

endif()
