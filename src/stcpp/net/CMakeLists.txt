# src/stcpp/net/CMakeLists.txt — 出站序列化 + 背压 (A-NET-01)
#
# Owner: 老陈 (A-NET-01, #03)  Last review: 2026-05-29
#
# Target: stcpp_net_outbound
#   - OutboundBuffer     (header-only, 随 include 路径提供)
#   - OutboundSerializer (outbound_serializer.cpp)
#   - OutboundSubmitQueue (header-only, 随 include 路径提供)
#
# 依赖: stcpp_infra_wal (pit.hpp), stcpp_infra_spsc (queue_capacities.hpp)
# 不依赖: glaze (Sprint-4 再引入), nghttp2 (Http2Client 在后续 Wave)
# 不碰: stcpp_risk / stcpp_observability_audit (老沈在改)
#
# paper + live 双 mode 均编译 (序列化逻辑与 mode 无关)

add_library(stcpp_net_outbound STATIC
    outbound_serializer.cpp
)

target_include_directories(stcpp_net_outbound
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_net_outbound PUBLIC cxx_std_20)

target_compile_definitions(stcpp_net_outbound PRIVATE
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)

# stcpp_infra_wal: OutboundQueue 头文件间接依赖 queue_capacities.hpp (infra/spsc)
# stcpp_infra_spsc: IS_POWER_OF_TWO / queue_capacities.hpp
target_link_libraries(stcpp_net_outbound
    PUBLIC stcpp_infra_wal
    PUBLIC stcpp_infra_spsc
)
