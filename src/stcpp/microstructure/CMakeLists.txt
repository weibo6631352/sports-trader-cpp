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

# fill_rate_model.cpp 内部使用 OrderBookLevel 等 POD 算数; 关闭 double-promotion 警告.
# 主仓 -Werror 保留, 仅本模块的旧式 cast / 隐式转换 (depth_ratio division) 放宽.
target_compile_options(stcpp_microstructure PRIVATE
    -Wno-double-promotion
)
