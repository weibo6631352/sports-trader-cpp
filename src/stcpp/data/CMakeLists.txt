# src/stcpp/data/CMakeLists.txt — Goalserve client + schema + data-contract
#
# Owner: 小段 (goalserve-specialist, #37)
# Sprint-2 W6 Wave 29 — D-W5-03/05 data-contract 落 src/stcpp/data/
#
# Targets:
#   stcpp_data_goalserve   — W4 stub (goalserve_stub.cpp), URL builder + client
#   stcpp_data_schema      — header-only schema lib (goalserve_schema + data_contract + odds_record)
#   stcpp_data_contract    — static lib alias (links stcpp_data_schema + stcpp_data_goalserve)

# ---------------------------------------------------------------------------
# stcpp_data_goalserve: GoalserveClient stub (W4, W5 接 cpp-httplib)
# ---------------------------------------------------------------------------
add_library(stcpp_data_goalserve STATIC
    goalserve_stub.cpp
)

target_include_directories(stcpp_data_goalserve
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_data_goalserve PUBLIC cxx_std_20)

# 不依赖第三方 (W5 真接时 PRIVATE link cpp-httplib + zlib)

# ---------------------------------------------------------------------------
# stcpp_data_schema: header-only schema layer
#   goalserve_schema.hpp / data_contract.hpp / odds_record.hpp
#   这 3 个头文件均 header-only; INTERFACE target 提供 include path.
# ---------------------------------------------------------------------------
add_library(stcpp_data_schema INTERFACE)

target_include_directories(stcpp_data_schema
    INTERFACE ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_data_schema INTERFACE cxx_std_20)

# schema 依赖 goalserve_client (枚举 + URL builder SSOT)
target_link_libraries(stcpp_data_schema
    INTERFACE stcpp_data_goalserve
)

# ---------------------------------------------------------------------------
# stcpp_data_contract: 聚合 alias (供下游 link 时一步到位)
#   link stcpp_data_contract → 同时获得 schema + client
# ---------------------------------------------------------------------------
add_library(stcpp_data_contract INTERFACE)

target_link_libraries(stcpp_data_contract
    INTERFACE stcpp_data_schema
)
