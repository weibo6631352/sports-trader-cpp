# src/stcpp/backtest/CMakeLists.txt — Backtest framework v0.2
#
# Owner: 小蒋 (quant-backtest, C 量化研究部 IC #20)
# Wave: backtest framework cpp v0.2 skeleton
# Last review: 2026-05-29
#
# 落: docs/RESEARCH/xiaojiang-backtest-framework-v0.2-cpp.md (设计 SSOT)
#     docs/RESEARCH/xiaocheng-p0-02-alpha-v2-backtest-spec-v0.2.md (spec v0.2)
#
# 红线:
#   BR-1: 回测/paper/实盘共用 FillRateModel + SlippageModel (C++ 直接 link)
#   BR-5: 复用小肖 slippage model v1 (stcpp_microstructure + numerical 头文件)
#   R-7:  ExecutionMode-agnostic (paper/backtest 共用)
#   R-11: 不写 ledger (纯函数)
#   R-20: 4 ts PIT 强制校验
#
# 模块依赖:
#   - stcpp_microstructure (FillRateModel::compute_from_clob_book)
#   - 无其他 .cpp 依赖 (headers only for types/walk_forward/metrics/stats/event_replayer)
#
# ADR-010 §4 grandfather:
#   -Wno-double-promotion — <cmath> 浮点运算内联展开 (erfc / exp / sqrt)
#   -Wno-conversion       — 统计计算 size_t → double 转换

add_library(stcpp_backtest STATIC
    backtest_metrics.cpp
    replay_driver.cpp
)

target_include_directories(stcpp_backtest
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_backtest PUBLIC cxx_std_20)

target_compile_definitions(stcpp_backtest PUBLIC
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)

# BR-1 + BR-5: 链接 microstructure (FillRateModel) 共用
# ReplayDriver: 链接 orderbook_snapshot_hub (double-buffer 发布器)
target_link_libraries(stcpp_backtest
    PUBLIC stcpp_microstructure
    PUBLIC stcpp_orderbook_snapshot_hub
)

# ADR-010 §2.1: 生产代码 (src/+include/) 严禁 -Wno-* 抑制。
# 数值转换 (size_t→double / float→double) 一律在源码用显式 static_cast 表达, 不靠 -Wno 蒙混。
