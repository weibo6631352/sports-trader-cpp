# src/stcpp/strategy/CMakeLists.txt — Strategy / Signal v0.2 (W6 Wave 28)
#
# Owner: 小程 (signal-research-cpp spec) + 小卢 (IC pool 实施)
# 落:
#   - 小程 P0-01 spec v0.1 (no-vig + 5 条件 + Kelly·0.25)
#   - 老李 GM LiveSection 5 enum
#   - ADR-008 multiplicative de-vig (Wave 28 小卢, Goalserve 8-9 家 bookmaker)
#
# 红线:
#   R-20   SignalContext 4 ts + feature_snapshot_id 必带 (lib 内 fail-closed → nullopt)
#   ML-R5  rule-based, 不调 ML
#   ADR-008 multiplicative de-vig, 不上 Shin (Shin 触发条件 = 老彭 vig 实证 W6 EOW)
#   老周 ABI lock: SignalContext / SignalOutput struct 不动
#
# W4 p0_01_pinnacle_no_vig.cpp 保留 (W4 25 测试兼容; W7 deprecation review)
# W6 p0_01_goalserve_devig.cpp 新增 (ADR-008 实施)

add_library(stcpp_strategy STATIC
    live_section_classifier.cpp
    p0_01_pinnacle_no_vig.cpp
    p0_01_goalserve_devig.cpp
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
