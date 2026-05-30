# src/stcpp/polymarket/wire/CMakeLists.txt — stcpp_polymarket_clob_wire (mode-agnostic)
#
# Owner: GM (老雷) 2026-05-31。CLOB V2 wire body + L2 HMAC + L1 ClobAuth 纯函数。
# 全 mode 构建 (单测在默认 paper ctest 跑; 下游 live LiveOrderSubmitter)。
add_library(stcpp_polymarket_clob_wire STATIC
    clob_wire.cpp
)
target_include_directories(stcpp_polymarket_clob_wire
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(stcpp_polymarket_clob_wire PUBLIC cxx_std_20)
target_link_libraries(stcpp_polymarket_clob_wire
    PUBLIC  stcpp_crypto_eip712     # keccak256 / U256 / AddressFromHex
    PRIVATE stcpp_crypto_ed25519    # libsodium HMAC-SHA256 (crypto_auth_hmacsha256)
)
message(STATUS "stcpp_polymarket_clob_wire: V2 wire body + L2 HMAC + L1 ClobAuth")
