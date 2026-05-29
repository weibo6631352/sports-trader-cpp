# src/stcpp/polymarket/clob_wss/CMakeLists.txt
#
# Owner: 小冯 (#34)  spec: 老李 (#07) W9 W2  Wave 79
# PolymarketCLOBSubscriber — CLOB WSS market + user channel
#
# ADR-027 cite: laoli-w9-wss-subscriber-impl-spec-v1.md (老李 spec)
# ADR-027 Enforce-1
#
# transport-agnostic (IWssTransport 抽象, boost.beast 真实现 W10+ 老周 ack)
# depends on: stcpp_polymarket_wss (IWssTransport / ISpscEventSink / WssEvent 接口)

add_library(stcpp_polymarket_clob_wss STATIC
    polymarket_clob_subscriber.cpp
    orderbook_adapter.cpp
)

# ---------------------------------------------------------------------------
# stcpp_orderbook_snapshot_hub — per-token double-buffer 只读快照发布器
#
# Owner: 小冯 (#34)
# 独立 target: 仅依赖 stcpp_polymarket_wss (WssEvent POD) + 标准库
# 零反向依赖 (R-12): 不 link RM / signer / exec
# 供 debug_api RealBookStateProvider 消费 (集成步骤后续独立做)
#
# root add_subdirectory 集成说明 (供 GM 合并时参考):
#   本 target 已在 src/stcpp/polymarket/clob_wss/CMakeLists.txt 中定义.
#   src/stcpp/polymarket/CMakeLists.txt 已 add_subdirectory(clob_wss).
#   root CMakeLists.txt 已 add_subdirectory(src/stcpp/polymarket).
#   → 无需修改 root CMakeLists.txt, 直接可用 stcpp_orderbook_snapshot_hub target.
# ---------------------------------------------------------------------------
add_library(stcpp_orderbook_snapshot_hub STATIC
    orderbook_snapshot_hub.cpp
)

target_include_directories(stcpp_orderbook_snapshot_hub
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_orderbook_snapshot_hub PUBLIC cxx_std_20)

# 仅依赖 microstructure (OrderBookLevel / kBookDepthLevels) — 零热路径反向依赖
target_link_libraries(stcpp_orderbook_snapshot_hub
    PUBLIC stcpp_polymarket_wss
)

target_include_directories(stcpp_polymarket_clob_wss
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_polymarket_clob_wss PUBLIC cxx_std_20)

# Depends on stcpp_polymarket_wss for IWssTransport / ISpscEventSink / WssEvent
target_link_libraries(stcpp_polymarket_clob_wss PUBLIC stcpp_polymarket_wss)

# 三方库豁免: Boost.Beast / simdjson (cpp-httplib 头文件引入的三方代码触发)
# ADR-010 §2.1 豁免标注 — 非业务代码, 三方库 include 引起, 清理跟踪: xiaosong-wno-cleanup-extension-w7-plan.md
target_compile_options(stcpp_polymarket_clob_wss PRIVATE
    -Wno-double-promotion   # 三方库豁免: Boost.Beast
    -Wno-old-style-cast     # 三方库豁免: Boost.Beast
    -Wno-cast-align         # 三方库豁免: Boost.Beast
)
