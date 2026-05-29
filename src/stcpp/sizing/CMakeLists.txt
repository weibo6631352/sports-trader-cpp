# src/stcpp/sizing/CMakeLists.txt — SizingCalculator static lib (小袁 v0.1)
#
# Owner: 小袁 (quant-microstructure, C 量化研究部)
# last_review: 2026-05-29
#
# SSOT: docs/RESEARCH/xiaoliang-kelly-sizing-spec-v1.md
#       docs/RESEARCH/laohan-kelly-cap-cosign-v1.md (联签)
#
# 红线:
#   R-1  sizing 绝不绕 RM; suggested_notional 仅建议值
#   R-7  ExecutionMode-agnostic (paper/live/backtest 共用)
#   R-11 纯函数, 不写 ledger / 不调 audit
#   老高 grep 守护: stcpp_sizing TU 内无 cap 字面量 (10000/25000/50000 来自 RiskConfig 注入)
#
# root 集成说明 (给集成方 — 不编辑 root CMakeLists.txt):
#   在 root CMakeLists.txt 的 add_subdirectory(src/stcpp/pricing) 之后加入:
#     add_subdirectory(src/stcpp/sizing)
#   测试自动发现: tests/unit/CMakeLists.txt 的 EXTRA_LIBS 映射表补:
#     set(EXTRA_LIBS_test_sizing_calculator  stcpp_sizing stcpp_risk)
#     set(EXTRA_LIBS_test_sizing_rm_consistency stcpp_sizing stcpp_risk)
#   LABELS 映射:
#     set(LABELS_test_sizing_calculator  "unit;sizing;kelly;c1-c7;xiaoyuan")
#     set(LABELS_test_sizing_rm_consistency "unit;sizing;rm-consistency;xiaoyuan")

add_library(stcpp_sizing STATIC
    sizing_calculator.cpp
)

target_include_directories(stcpp_sizing
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_sizing PUBLIC cxx_std_20)

# ExecutionMode build-time define (R-7 — 与 stcpp_risk 对称)
target_compile_definitions(stcpp_sizing PUBLIC
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)

# 依赖:
#   stcpp_risk    — RiskConfig / OrderIntent / RiskGateway (cap 注入 + RM 一致性单测用)
#   stcpp_infra_wal — pit.hpp header-only (R-20 chain, 与 stcpp_risk 同)
target_link_libraries(stcpp_sizing
    PUBLIC stcpp_risk
    PUBLIC stcpp_infra_wal
)
