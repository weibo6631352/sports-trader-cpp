# src/stcpp/data/CMakeLists.txt — Goalserve client adapter (W4 stub, W5 真接)
#
# Owner: 小段 (goalserve-specialist)
# Sprint-2 W4 Wave 19 — header + stub. W5 接 cpp-httplib 时新增 goalserve_http.cpp.

add_library(stcpp_data_goalserve STATIC
    goalserve_stub.cpp
)

target_include_directories(stcpp_data_goalserve
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_data_goalserve PUBLIC cxx_std_20)

# 不依赖第三方 (W5 真接时 PRIVATE link cpp-httplib + zlib)
