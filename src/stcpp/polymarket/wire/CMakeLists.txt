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
# 2026-06-12 治理: HMAC-SHA256 libsodium→OpenSSL (砍掉 libsodium autoconf 外部构建;
#   OpenSSL 本就是 WSS 依赖)。homebrew 手动回退与 app/CMakeLists 同款。
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
    target_link_libraries(stcpp_polymarket_clob_wire PRIVATE OpenSSL::Crypto)
else()
    find_library(OPENSSL_CRYPTO_LIB crypto PATHS /opt/homebrew/opt/openssl@3/lib NO_DEFAULT_PATH)
    find_path(OPENSSL_INCLUDE_DIR openssl/ssl.h PATHS /opt/homebrew/opt/openssl@3/include NO_DEFAULT_PATH)
    if(NOT OPENSSL_CRYPTO_LIB OR NOT OPENSSL_INCLUDE_DIR)
        message(FATAL_ERROR "clob_wire: OpenSSL 未找到 (HMAC-SHA256 需要). brew install openssl@3")
    endif()
    target_link_libraries(stcpp_polymarket_clob_wire PRIVATE ${OPENSSL_CRYPTO_LIB})
    target_include_directories(stcpp_polymarket_clob_wire SYSTEM PRIVATE ${OPENSSL_INCLUDE_DIR})
endif()
target_link_libraries(stcpp_polymarket_clob_wire
    PUBLIC  stcpp_crypto_eip712     # keccak256 / U256 / AddressFromHex
)
message(STATUS "stcpp_polymarket_clob_wire: V2 wire body + L2 HMAC + L1 ClobAuth")
