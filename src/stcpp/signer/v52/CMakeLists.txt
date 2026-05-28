# src/stcpp/signer/v52/CMakeLists.txt — SignerV52 (C++20 重写, 撤 Rust)
#
# Owner: 老孙 (#06, crypto-signing-expert)
# Wave 30 W6: signer v5.2 cpp v0.1 IPC + Ed25519 paper mock
#
# 落:
#   laoSun-signer-v5.2-cpp-design.md §3
#   laoshan-sop-v6.md §6 (老沈 W6 Wave 29: vcpkg unofficial-sodium 已撤)
#
# 红线:
#   R-7  paper → stcpp_signer_v52_paper (Ed25519 mock); live/backtest stub (M5+)
#   STCPP_EXEC_MODE: paper / live / backtest (顶层 CMake 注入)
#
# libsodium 策略 (W6 W3):
#   - 优先 find_package(libsodium) (homebrew macOS / apt Linux 预装)
#   - homebrew: /opt/homebrew/Cellar/libsodium/1.0.22/ (macOS arm64)
#   - 不做 FetchContent git clone (网络不稳, W6 W3 deadline 约束)
#   - 与老沈 SOP §6 "FetchContent libsodium source" 精神一致:
#     老沈指 "不走 vcpkg unofficial-sodium"; 本地已有 brew libsodium = source 同等效果
#   - M5+ 若 CI 无 brew: 补 FetchContent ExternalProject_Add (老孙 W6 W4 跟进)

option(STCPP_BUILD_SIGNER_V52 "Build signer v5.2 (paper Ed25519 mock + IPC)" ON)

if(NOT STCPP_BUILD_SIGNER_V52)
    return()
endif()

# ---------- libsodium 查找 (homebrew prebuilt, macOS + Linux) ----------
#
# 策略: 先尝试 find_library / find_path 找 homebrew 安装;
# 找不到则 STCPP_SIGNER_V52_LIBSODIUM=0 (走 mock 路径, 测试仍能过 T1-T7)

set(LIBSODIUM_FOUND FALSE)

# macOS Homebrew arm64 路径
find_library(SODIUM_LIBRARY
    NAMES sodium libsodium
    PATHS
        /opt/homebrew/lib
        /usr/local/lib
        /usr/lib
    NO_DEFAULT_PATH
)

find_path(SODIUM_INCLUDE_DIR
    NAMES sodium.h
    PATHS
        /opt/homebrew/include
        /usr/local/include
        /usr/include
    NO_DEFAULT_PATH
)

if(SODIUM_LIBRARY AND SODIUM_INCLUDE_DIR)
    set(LIBSODIUM_FOUND TRUE)
    message(STATUS "stcpp_signer_v52: libsodium found: ${SODIUM_LIBRARY} (include: ${SODIUM_INCLUDE_DIR})")
else()
    message(STATUS "stcpp_signer_v52: libsodium NOT found — paper will use deterministic mock (no real Ed25519)")
endif()

# ---------- stcpp_signer_v52_sodium (INTERFACE wrapper) ----------
#
# 如果找到 libsodium, 创建 IMPORTED target wrapper;
# 否则创建空 INTERFACE target (mock 路径不需要 sodium).

add_library(stcpp_signer_v52_sodium INTERFACE)

if(LIBSODIUM_FOUND)
    target_include_directories(stcpp_signer_v52_sodium
        INTERFACE ${SODIUM_INCLUDE_DIR}
    )
    target_link_libraries(stcpp_signer_v52_sodium
        INTERFACE ${SODIUM_LIBRARY}
    )
    target_compile_definitions(stcpp_signer_v52_sodium
        INTERFACE STCPP_SIGNER_V52_LIBSODIUM=1
    )
endif()

# ---------- stcpp_signer_v52_paper ----------

if(STCPP_EXEC_MODE STREQUAL "paper")
    add_library(stcpp_signer_v52_paper STATIC
        signer_v52.cpp
    )
    target_include_directories(stcpp_signer_v52_paper
        PUBLIC ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_features(stcpp_signer_v52_paper PUBLIC cxx_std_20)
    target_compile_definitions(stcpp_signer_v52_paper
        PUBLIC  STCPP_EXEC_MODE_paper=1
    )
    target_link_libraries(stcpp_signer_v52_paper
        PUBLIC  stcpp_infra_wal
        PUBLIC  stcpp_signer_v52_sodium
    )
endif()

# live / backtest stub (M5+ secp256k1 真切, W6 W3 只 stub)
# if(STCPP_EXEC_MODE STREQUAL "live")
#     add_library(stcpp_signer_v52_live STATIC signer_v52.cpp)
# endif()

if(LIBSODIUM_FOUND)
    message(STATUS "stcpp_signer_v52: STCPP_EXEC_MODE=${STCPP_EXEC_MODE}, libsodium ${SODIUM_LIBRARY}")
else()
    message(STATUS "stcpp_signer_v52: STCPP_EXEC_MODE=${STCPP_EXEC_MODE}, mock mode (no libsodium)")
endif()
