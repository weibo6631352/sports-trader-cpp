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
)

target_include_directories(stcpp_polymarket_clob_wss
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_polymarket_clob_wss PUBLIC cxx_std_20)

# Depends on stcpp_polymarket_wss for IWssTransport / ISpscEventSink / WssEvent
target_link_libraries(stcpp_polymarket_clob_wss PUBLIC stcpp_polymarket_wss)

# ADR-010 grandfather list only
target_compile_options(stcpp_polymarket_clob_wss PRIVATE
    -Wno-double-promotion
    -Wno-old-style-cast
    -Wno-cast-align
)
