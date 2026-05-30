# src/stcpp/crypto/CMakeLists.txt — stcpp_crypto_ed25519 (libsodium ExternalProject_Add)
#
# Owner: 老孙 (#06, crypto-signing-expert)
# Wave 34 W8: libsodium FetchContent ExternalProject_Add cpp v0.2
#             老沈 W7 ack Option A — 撤 brew find_library hack (老周 C-02 review)
#
# 落:
#   laoshan-sop-v6.md §6 (Option A: FetchContent_Declare + ExternalProject_Add)
#   老沈 W7 ack: signer v52 + STRATEGY_DECAYED CLI 共用 stcpp_crypto_ed25519 INTERFACE
#   BLAKE3 / rigtorp FetchContent 模式一致 (observability/CMakeLists.txt 参考)
#
# 架构:
#   1. ExternalProject_Add 下载 + 构建 libsodium tar (autoconf 官方推荐路径)
#      (不走 FetchContent_Populate deprecated API, CMake 4.3.3 禁)
#   2. IMPORTED STATIC target `libsodium_external` 暴露 .a + include
#   3. STATIC target `stcpp_crypto_ed25519` 封装 ed25519.cpp + libsodium_external
#
# ExternalProject vs FetchContent:
#   libsodium 无 CMakeLists.txt (autoconf 构建), 不能走 FetchContent_MakeAvailable.
#   FetchContent_Populate deprecated (CMake 4.3.3 禁用).
#   正确路径: ExternalProject_Add URL 直接下载 tar + autoconf configure.
#   FetchContent_Declare 用于纯 CMake 项目; libsodium 走 ExternalProject_Add.
#
# 跨平台:
#   macOS arm64: ./configure --prefix=... && make -j<N> install
#   Linux x86_64: 同 configure 路径 (CI Ubuntu)
#
# 红线:
#   R-7  paper/live/backtest 共享 stcpp_crypto_ed25519
#   R-11 paper mode 不真签 — 由 signer_v52 传 mock secret_key (本 CMake 无 mode 判断)
#   R-12 不在 hot path
#   ADR-018 §X target 命名: stcpp_crypto_ed25519 / libsodium_external (IMPORTED)

cmake_minimum_required(VERSION 3.27)

include(ExternalProject)

# ---------- libsodium 版本 / URL ----------

set(LIBSODIUM_VERSION    "1.0.20")
set(LIBSODIUM_TAG        "1.0.20-RELEASE")
# GitHub release tarball (稳定, 含 configure; 不走 git clone 避免 submodule 问题)
set(LIBSODIUM_URL
    "https://github.com/jedisct1/libsodium/releases/download/${LIBSODIUM_TAG}/libsodium-${LIBSODIUM_VERSION}.tar.gz"
)
# SHA256: libsodium-1.0.20.tar.gz (官方 GitHub release)
set(LIBSODIUM_URL_HASH
    "SHA256=ebb65ef6ca439333c2bb41a0c1990587288da07f6c7fd07cb3a18cc18d30ce19"
)

# ---------- 构建路径 ----------
# ExternalProject 下载、构建、安装均在 build/_deps/libsodium-*
# (与 FetchContent _deps/ 子目录对齐, 不污染系统路径)

set(LIBSODIUM_PREFIX     "${CMAKE_BINARY_DIR}/_deps/libsodium-install")
set(LIBSODIUM_INCLUDE    "${LIBSODIUM_PREFIX}/include")
set(LIBSODIUM_STATIC_LIB "${LIBSODIUM_PREFIX}/lib/libsodium.a")

# ---------- nproc 检测 (并行构建) ----------
# macOS: sysctl -n hw.logicalcpu; Linux: nproc
if(CMAKE_HOST_APPLE)
    execute_process(
        COMMAND sysctl -n hw.logicalcpu
        OUTPUT_VARIABLE _HOST_NPROC
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
else()
    execute_process(
        COMMAND nproc
        OUTPUT_VARIABLE _HOST_NPROC
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
if(NOT _HOST_NPROC OR _HOST_NPROC STREQUAL "")
    set(_HOST_NPROC 4)
endif()
message(STATUS "stcpp_crypto_ed25519: libsodium autoconf build with -j${_HOST_NPROC}")

# ---------- ExternalProject_Add — 下载 + autoconf 构建 libsodium ----------
#
# libsodium 官方推荐: ./configure --prefix=<dir> --enable-static --disable-shared
#   --enable-static: 生成 libsodium.a (静态链接, CI/生产无 .so 依赖)
#   --disable-shared: 不生成 .so (精简, 与 BLAKE3 静态模式一致)
#   --with-pic: Position Independent Code (生产 binary 需要)
#
# BUILD_IN_SOURCE 1: libsodium autoconf configure 必须在解压的源目录运行
#   (注: BUILD_IN_SOURCE 1 时不能同时设 BINARY_DIR)
# INSTALL_COMMAND: 安装到 LIBSODIUM_PREFIX
# BUILD_BYPRODUCTS: 告知 Ninja/Make libsodium.a 何时生成 (避免重新 configure)

ExternalProject_Add(
    libsodium_autoconf
    URL         ${LIBSODIUM_URL}
    URL_HASH    ${LIBSODIUM_URL_HASH}
    PREFIX      "${CMAKE_BINARY_DIR}/_deps/libsodium-ep"
    INSTALL_DIR ${LIBSODIUM_PREFIX}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    BUILD_IN_SOURCE 1
    CONFIGURE_COMMAND
        <SOURCE_DIR>/configure
        --prefix=<INSTALL_DIR>
        --enable-static
        --disable-shared
        --with-pic
        --quiet
    BUILD_COMMAND
        make -j${_HOST_NPROC}
    INSTALL_COMMAND
        make install
    BUILD_BYPRODUCTS
        ${LIBSODIUM_STATIC_LIB}
    # 状态输出 (便于 CI 日志)
    LOG_CONFIGURE TRUE
    LOG_BUILD     TRUE
    LOG_INSTALL   TRUE
    LOG_OUTPUT_ON_FAILURE TRUE
)

# ---------- IMPORTED STATIC target: libsodium_external ----------
#
# ADR-018 §X 命名: libsodium_external
# 下游 target 链接此 target 即可获得:
#   - libsodium.a 静态库 (ExternalProject_Add 构建产物)
#   - sodium.h include 路径

add_library(libsodium_external STATIC IMPORTED GLOBAL)

# CMake generate 阶段检查 INTERFACE_INCLUDE_DIRECTORIES 路径必须存在.
# ExternalProject_Add 构建前目录尚未创建 → 预建目录占位.
# (与 ExternalProject_Add 标准用法一致, 参考 CMake 文档 ExternalProject §INTERFACE)
file(MAKE_DIRECTORY ${LIBSODIUM_INCLUDE})

set_target_properties(libsodium_external PROPERTIES
    IMPORTED_LOCATION               ${LIBSODIUM_STATIC_LIB}
    INTERFACE_INCLUDE_DIRECTORIES   ${LIBSODIUM_INCLUDE}
)

# 确保 libsodium_external 依赖 ExternalProject 构建完成
add_dependencies(libsodium_external libsodium_autoconf)

# ---------- stcpp_crypto_ed25519 — STATIC target (ADR-018 §X) ----------
#
# 封装:
#   - ed25519.cpp (Ed25519 sign/verify wrapper)
#   - libsodium_external (STATIC IMPORTED)
#   - include/stcpp/crypto/ (Ed25519 wrapper header + SecureBuffer)
#   - C++20
#
# 下游: signer_v52 + stcpp_cli_three_sig 共用此 target (老沈 W7 ack)
# 不传播 -Wno-* (本 wrapper 是公司代码, 受 -Werror 约束)

add_library(stcpp_crypto_ed25519 STATIC
    ed25519.cpp
)

target_include_directories(stcpp_crypto_ed25519
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

# libsodium include (SYSTEM: sodium.h 不受公司 -Werror 约束)
target_include_directories(stcpp_crypto_ed25519
    SYSTEM PUBLIC
        ${LIBSODIUM_INCLUDE}
)

target_compile_features(stcpp_crypto_ed25519 PUBLIC cxx_std_20)

# 链接 libsodium_external (IMPORTED STATIC)
target_link_libraries(stcpp_crypto_ed25519
    PUBLIC libsodium_external
)

# ExternalProject 构建顺序依赖 (stcpp_crypto_ed25519 编译前 libsodium.a 必须存在)
add_dependencies(stcpp_crypto_ed25519 libsodium_autoconf)

# libsodium 需要 pthreads (某些 Linux 配置)
find_package(Threads REQUIRED)
target_link_libraries(stcpp_crypto_ed25519 PUBLIC Threads::Threads)

message(STATUS "stcpp_crypto_ed25519: libsodium-${LIBSODIUM_VERSION} ExternalProject_Add")
message(STATUS "stcpp_crypto_ed25519: install -> ${LIBSODIUM_PREFIX}")
message(STATUS "stcpp_crypto_ed25519: static lib -> ${LIBSODIUM_STATIC_LIB}")

# ---------- stcpp_crypto_eip712 — Polymarket CTF Exchange V2 EIP-712 digest 编码 ----------
#
# Owner: GM (老雷) 2026-05-31。固化自 experiments/laosun-laoli-live-order (首笔实盘成交验证)。
# 纯 C++ digest 编码 (keccak256 + EIP-712 struct/domain), 无外部依赖。
# 全 mode 构建 (paper/live/backtest 共用; 下游 signer_v62 live 分支 + live_pm_client)。
# secp256k1 签名 + L2 HMAC + POST 走后续 increment (那时再链 secp256k1/openssl)。
add_library(stcpp_crypto_eip712 STATIC
    eip712_v2.cpp
)
target_include_directories(stcpp_crypto_eip712
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(stcpp_crypto_eip712 PUBLIC cxx_std_20)
message(STATUS "stcpp_crypto_eip712: V2 EIP-712 digest 编码 (纯 C++, 无外部依赖)")
