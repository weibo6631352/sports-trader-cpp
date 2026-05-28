# src/stcpp/stats/CMakeLists.txt — M4.5 7 hard gate statistical framework v1
#
# Owner: 小董 (stats-inference-advisor, W4 Wave 20)
# 落:
#   include/stcpp/stats/gate_evaluator.hpp
#   docs/RESEARCH/xiaodong-m45-gate-framework-v1.md
#
# 红线:
#   R-7  paper / live 共用同 evaluator (build-time mode 不分叉统计)
#   R-11 不写 ledger (纯函数)
#   R-20 PIT pre-check (CheckPit) 走 GateMetrics 自带 4 ts
#
# 本 lib 无外部第三方依赖, 纯 STL + <cmath> + <random>.

add_library(stcpp_stats STATIC
    gate_evaluator.cpp
)

target_include_directories(stcpp_stats
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_stats PUBLIC cxx_std_20)

# R-7 build-time ExecutionMode 注入 (paper / live 走同 evaluator, 仅切数据源)
target_compile_definitions(stcpp_stats PUBLIC
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)

# 不依赖第三方 (W5 接 paper_audit.wal 读时 link stcpp_infra_wal + stcpp_observability_audit).
