# src/stcpp/polymarket/wss/CMakeLists.txt — PM WSS subscriber v0.1
#
# Owner: 小冯 (api-watch-general, #34)  spec: 小余 W5-D-02 + 老周 W5-A-02
# W5 Wave 24 — subscriber library, transport-agnostic
#
# 真 transport (BoostBeastTransport) v0.1 不带 — 待 W6 老周 ack lib 选型后接入
# (boost.beast via FetchContent, 见 architecture v0.6 §17). v0.1 仅 mock transport.

add_library(stcpp_polymarket_wss STATIC
    pm_wss_subscriber.cpp
)

target_include_directories(stcpp_polymarket_wss
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_polymarket_wss PUBLIC cxx_std_20)

# Subscriber 不依赖第三方 transport — IWssTransport 是抽象, 真实现 (boost.beast)
# 在另一独立 target stcpp_polymarket_wss_beast 提供 (W6 ack 后落地).
target_compile_options(stcpp_polymarket_wss PRIVATE
    -Wno-double-promotion -Wno-sign-conversion -Wno-conversion
    -Wno-shadow -Wno-old-style-cast -Wno-cast-align
)
