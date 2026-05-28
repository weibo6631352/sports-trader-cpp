# src/stcpp/infra/wal/CMakeLists.txt — WAL framework skeleton (W3)
#
# 落: laowang-wal-framework-v0.2.md §10 (W3 落地)
#
# header-only + 一份 .cpp (模板实例化 + path 硬校验). W4 接 rigtorp SPSC + fsync.

add_library(stcpp_infra_wal STATIC
    wal_writer.cpp
)

target_include_directories(stcpp_infra_wal
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_infra_wal PUBLIC cxx_std_20)

# WAL framework 不依赖任何第三方 (BLAKE3 在 payload owner 算, SPSC W4 接 @小石).
