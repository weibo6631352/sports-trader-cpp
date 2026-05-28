# src/stcpp/signer/v52/CMakeLists.txt — SignerV52 (C++20, libsodium ExternalProject_Add)
#
# Owner: 老孙 (#06, crypto-signing-expert)
# Wave 34 W8: libsodium FetchContent ExternalProject_Add cpp v0.2
#             撤 W6 W3 brew find_library 兼容性 hack (老周 C-02 review)
#
# 落:
#   laoshan-sop-v6.md §6 (Option A 实施: FetchContent + ExternalProject_Add)
#   老沈 W7 ack: stcpp_crypto_ed25519 INTERFACE target 共用 (signer v52 + CLI)
#   ADR-018 §X target 命名规范
#
# 变更说明 (W8 vs W6):
#   - 撤 W6 find_library SODIUM_LIBRARY / find_path SODIUM_INCLUDE_DIR 全路径 hack
#   - 撤 STCPP_SIGNER_V52_LIBSODIUM=0/1 双路径 (mock + brew)
#   - 改单一路径: 依赖 stcpp_crypto_ed25519 (ExternalProject_Add libsodium-1.0.20)
#   - stcpp_signer_v52_sodium INTERFACE wrapper 已由 stcpp_crypto_ed25519 替代, 废弃
#
# 红线:
#   R-7  paper → stcpp_signer_v52_paper 链 stcpp_crypto_ed25519
#   R-11 paper mode: secret_key 用固定 mock (0x42 seed), 非真私钥
#   R-12 不在 hot path (启动期签名)
#   HMAC bug 4 (老孙 v3 §A) — 永久 enforcement (grep 见 hmac_4bug.py)
#   BUG-W5-001 防御 (shift safety, 不复用)

option(STCPP_BUILD_SIGNER_V52 "Build signer v5.2 (ExternalProject_Add libsodium)" ON)

if(NOT STCPP_BUILD_SIGNER_V52)
    return()
endif()

# ---------- stcpp_signer_v52_paper ----------
#
# paper mode: Ed25519 mock (secret_key = 0x42 seed, 非真私钥 — R-11)
# 依赖 stcpp_crypto_ed25519 (ExternalProject_Add libsodium-1.0.20-RELEASE)
#
# live / backtest: stub (M5+ secp256k1 真切)

if(STCPP_EXEC_MODE STREQUAL "paper")
    add_library(stcpp_signer_v52_paper STATIC
        signer_v52.cpp
    )
    target_include_directories(stcpp_signer_v52_paper
        PUBLIC ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_features(stcpp_signer_v52_paper PUBLIC cxx_std_20)
    target_compile_definitions(stcpp_signer_v52_paper
        PUBLIC STCPP_EXEC_MODE_paper=1
    )
    target_link_libraries(stcpp_signer_v52_paper
        PUBLIC  stcpp_infra_wal
        PUBLIC  stcpp_crypto_ed25519
    )
    message(STATUS "stcpp_signer_v52: paper mode — stcpp_crypto_ed25519 (ExternalProject_Add libsodium)")
endif()

# live / backtest stub (M5+ secp256k1 真切, W8 只 stub)
# if(STCPP_EXEC_MODE STREQUAL "live")
#     add_library(stcpp_signer_v52_live STATIC signer_v52.cpp)
#     target_link_libraries(stcpp_signer_v52_live
#         PUBLIC stcpp_crypto_ed25519  # M5+ 替换为 secp256k1 EIP-712 实现
#     )
# endif()
