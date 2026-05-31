# src/stcpp/polymarket/gate/CMakeLists.txt — stcpp_polymarket_order_gate (mode-agnostic)
#
# Owner: GM (老雷) 2026-05-31 — Phase 4 D-A: 实盘下单强制风控门。
# 全 mode 构建 (单测在默认 paper ctest 跑)。链 stcpp_risk (RiskGateway/OrderIntent)。
# 下单 sink 注入: 生产绑 LiveOrderSubmitter, 测试注入 stub → gate 逻辑离线可测。
add_library(stcpp_polymarket_order_gate STATIC
    live_order_gate.cpp
)
target_include_directories(stcpp_polymarket_order_gate
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(stcpp_polymarket_order_gate PUBLIC cxx_std_20)
target_link_libraries(stcpp_polymarket_order_gate
    PUBLIC stcpp_risk
)
message(STATUS "stcpp_polymarket_order_gate: 实盘下单强制风控门 (Phase 4 D-A)")
