# src/stcpp/strategy/CMakeLists.txt — Strategy / Signal v0.1 (W4 Wave 19)
#
# Owner: 小程 (signal-research-cpp spec) + 小卢 (IC pool 实施)
# 落:
#   - 小程 P0-01 spec v0.1 (no-vig + 5 条件 + Kelly·0.25)
#   - 老李 GM LiveSection 5 enum
#
# 红线:
#   R-20  SignalContext 4 ts + feature_snapshot_id 必带 (lib 内 fail-closed → nullopt)
#   ML-R5 rule-based, 不调 ML
#   R-11  Pinnacle mock 数据 paper_audit (W5 接老彭 CSV)

add_library(stcpp_strategy STATIC
    live_section_classifier.cpp
    p0_01_pinnacle_no_vig.cpp
)

target_include_directories(stcpp_strategy
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_strategy PUBLIC cxx_std_20)

# build-time ExecutionMode 注入 (R-7)
target_compile_definitions(stcpp_strategy PUBLIC
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)

# 当前 v0.1 不直接链 RM / SlippageModel — 通过 forward decl + 数值 input.
# W5 接 SlippageModel / Goalserve / RM 时在此 target_link_libraries 加.
