# src/stcpp/signer/v62/CMakeLists.txt — SignerV62 v6.2 (C++20, libsodium)
#
# Owner: 老孙 (#06, crypto-signing-expert)
# Wave 104 P0: V2 ABI cpp 实施
#
# 落:
#   laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md §7.2 ADR-018 build switch
#   ADR-018: paper + live 必 V2; backtest 可 V1 for 历史
#
# 红线 R-7:
#   paper  → stcpp_signer_v62_paper (V2 EIP-712, Ed25519 mock)
#   live   → stub (M5+ secp256k1 真切; 当前 not implemented)
#   backtest → stub (不允许 live binary 链接 V1 signer, CI grep 拦截)
#
# ADR-018 build switch:
#   STCPP_CLOB_V2 (paper + live 必须; CI abi_lock grep 拦截 V1 link in live binary)
#   STCPP_CLOB_V1 (backtest 历史回测专用, 不允许 paper/live)

option(STCPP_BUILD_SIGNER_V62 "Build signer v6.2 (V2 ABI, ExternalProject_Add libsodium)" ON)

if(NOT STCPP_BUILD_SIGNER_V62)
    return()
endif()

# ---------- stcpp_signer_v62_paper ----------
#
# paper mode: Ed25519 mock (V2 EIP-712 proxy message; 非真私钥 — R-11)
# 依赖 stcpp_crypto_ed25519 (ExternalProject_Add libsodium-1.0.20, 与 v52 共用)
#
# ADR-018: paper binary 必须 STCPP_CLOB_V2 (CI abi_lock grep 拦截 V1 link)

if(STCPP_EXEC_MODE STREQUAL "paper")
    add_library(stcpp_signer_v62_paper STATIC
        signer_v62.cpp
    )
    target_include_directories(stcpp_signer_v62_paper
        PUBLIC ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_features(stcpp_signer_v62_paper PUBLIC cxx_std_20)
    target_compile_definitions(stcpp_signer_v62_paper
        PUBLIC STCPP_EXEC_MODE_paper=1
        PUBLIC STCPP_CLOB_V2=1
    )
    target_link_libraries(stcpp_signer_v62_paper
        PUBLIC  stcpp_infra_wal
        PUBLIC  stcpp_crypto_ed25519
    )
    message(STATUS "stcpp_signer_v62: paper mode — stcpp_crypto_ed25519 (V2 ABI)")
endif()

# live / backtest stub (M5+ secp256k1 真切)
# if(STCPP_EXEC_MODE STREQUAL "live")
#     add_library(stcpp_signer_v62_live STATIC signer_v62.cpp)
#     target_compile_definitions(stcpp_signer_v62_live PUBLIC STCPP_CLOB_V2=1)
#     target_link_libraries(stcpp_signer_v62_live PUBLIC stcpp_crypto_ed25519)
# endif()
