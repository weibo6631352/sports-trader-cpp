# src/stcpp/microstructure/CMakeLists.txt — Microstructure module (小袁 W4 Wave 20)
#
# 落: docs/RESEARCH/xiaoyuan-fill-rate-model-v0.1.md
# 红线: R-7 mode-agnostic (live / paper / backtest 共用), R-11 不写 ledger

add_library(stcpp_microstructure STATIC
    fill_rate_model.cpp
)

target_include_directories(stcpp_microstructure
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_microstructure PUBLIC cxx_std_20)

# ADR-010 §2.1: 生产代码严禁 -Wno-* 抑制 (Wave 74 fix)
# fill_rate_model.cpp 使用 double 算数无 double-promotion; 已确认 -Wno-double-promotion 多余, 移除.
