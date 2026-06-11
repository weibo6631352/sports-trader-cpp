# src/stcpp/crypto/CMakeLists.txt — 加密原语 (live 签名路径)
#
# 2026-06-12 治理: ed25519 + libsodium ExternalProject 整段删除 —— 唯一消费者是
#   已删的 signer v52/v62 + 三签 CLI (废会签机器)。真 live 签名 = eip712_v2 +
#   secp256k1 (live_order_submitter 直用, 首笔实盘成交已验证)。git 史可考。

cmake_minimum_required(VERSION 3.27)

# ---------- stcpp_crypto_eip712 — Polymarket CTF Exchange V2 EIP-712 digest 编码 ----------
#
# Owner: GM (老雷) 2026-05-31。固化自 experiments/laosun-laoli-live-order (首笔实盘成交验证)。
# 纯 C++ digest 编码 (keccak256 + EIP-712 struct/domain), 无外部依赖。
# 全 mode 构建 (paper/live/backtest 共用; 下游 live_order_submitter)。
# secp256k1 签名 + L2 HMAC + POST 走后续 increment (那时再链 secp256k1/openssl)。
add_library(stcpp_crypto_eip712 STATIC
    eip712_v2.cpp
)
target_include_directories(stcpp_crypto_eip712
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(stcpp_crypto_eip712 PUBLIC cxx_std_20)
message(STATUS "stcpp_crypto_eip712: V2 EIP-712 digest 编码 (纯 C++, 无外部依赖)")

# ---------- secp256k1 (Bitcoin Core) — Ethereum ECDSA 签名 (Polymarket V2 EIP-712) ----------
#
# Owner: GM (老雷) 2026-05-31。固化下单签名 (首笔实盘成交验证)。
# secp256k1 v0.6.0 是 CMake 项目 → FetchContent_MakeAvailable (非 ExternalProject;
#   libsodium 走 ExternalProject 是因它 autoconf 无 CMakeLists, secp256k1 有)。
# 全 mode 构建 (单测在默认 paper ctest 跑; 下游 stcpp_crypto_secp256k1 + live SubmitOrder)。
include(FetchContent)
set(SECP256K1_ENABLE_MODULE_RECOVERY ON  CACHE BOOL "" FORCE)  # 可恢复签名 (v = recid)
set(SECP256K1_BUILD_BENCHMARK        OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_TESTS            OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_EXHAUSTIVE_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_CTIME_TESTS      OFF CACHE BOOL "" FORCE)
set(SECP256K1_INSTALL                OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS                OFF CACHE BOOL "" FORCE)
FetchContent_Declare(secp256k1
    URL      "https://github.com/bitcoin-core/secp256k1/archive/refs/tags/v0.6.0.tar.gz"
    URL_HASH SHA256=785bb98e7d6705c51c8dfa8ac3af6aa2ccfa3774714d51c0b9e28fac1146e9f1
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(secp256k1)

# ---------- stcpp_crypto_secp256k1 — Ethereum ECDSA 签名/recover/地址推导 ----------
add_library(stcpp_crypto_secp256k1 STATIC
    secp256k1_signer.cpp
)
target_include_directories(stcpp_crypto_secp256k1
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(stcpp_crypto_secp256k1 PUBLIC cxx_std_20)
target_link_libraries(stcpp_crypto_secp256k1
    PUBLIC  stcpp_crypto_eip712   # keccak256 (地址推导)
    PRIVATE secp256k1             # libsecp256k1
)
message(STATUS "stcpp_crypto_secp256k1: secp256k1 v0.6.0 (recovery module)")
