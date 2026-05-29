# src/stcpp/observability/CMakeLists.txt — AuditEmitter v1.1 (W6 Wave 29)
#
# Owner: 老唐 (audit-expert, #38)
# 落:
#   laotang-audit-schema-v1.1.md §5
#   laowang-wal-framework-cpp-interface-v1.md §8
#   老周 Smell #A: AuditEmitterPool 5 上游
#   老韩 Smell #2: BLAKE3_REAL paper/live 都启用 (M1-A06 前置)
#
# W6 变更:
#   1. FetchContent BLAKE3 官方 C 实现 (portable, header-only-ish wrapper)
#   2. -DBLAKE3_REAL=1 注入 paper/live build (M1-A06 R-7)
#   3. stcpp_blake3 STATIC target (blake3.c + blake3_portable.c + blake3_dispatch.c)
#   4. stcpp_observability_audit 链 stcpp_blake3 + stcpp_infra_wal
#
# CI grep enforce (老高 PR v1.2):
#   grep -r "BLAKE3_STUB" src/ include/ → 禁止出现在 live build
#   grep "BLAKE3_REAL=1" 验证 paper/live build 均注入

cmake_minimum_required(VERSION 3.27)
include(FetchContent)

# ---------- BLAKE3 官方 C 实现 (FetchContent) ---------------------------------
#
# 官方 repo: https://github.com/BLAKE3-team/BLAKE3
# 使用 portable C 实现 (不依赖 AVX-512/AVX2/NEON 汇编), 跨平台兼容.
# M5 后可切汇编优化版本 (老高 PR v1.3 评审).

FetchContent_Declare(
    blake3_official
    GIT_REPOSITORY https://github.com/BLAKE3-team/BLAKE3.git
    GIT_TAG        1.5.4       # 稳定 tag, 2024-06 release
    GIT_SHALLOW    TRUE
    SYSTEM
)
FetchContent_MakeAvailable(blake3_official)

# BLAKE3 官方 C 源文件在 c/ 子目录
set(BLAKE3_SRC_DIR "${blake3_official_SOURCE_DIR}/c")

# stcpp_blake3: portable C 实现 (不含汇编 — 兼容 macOS arm64 + Linux x86_64)
# blake3_dispatch.c 选择运行时最优实现; portable fallback 全平台可用.
add_library(stcpp_blake3 STATIC
    ${BLAKE3_SRC_DIR}/blake3.c
    ${BLAKE3_SRC_DIR}/blake3_portable.c
    ${BLAKE3_SRC_DIR}/blake3_dispatch.c
)

target_include_directories(stcpp_blake3
    PUBLIC ${BLAKE3_SRC_DIR}
)

# 三方库豁免: BLAKE3 C 源码 — ADR-010 §3.2
# GM 错 #11 hotfix (W6 W2 push 前): blake3_dispatch.c 含 get_cpu_features unused-function
# 当 BLAKE3_NO_AVX512/AVX2/SSE41/SSE2 全关时, get_cpu_features 永不被调, AppleClang 21 触发 -Wunused-function
target_compile_options(stcpp_blake3 PRIVATE
    -Wno-cast-align            # 三方库豁免: BLAKE3
    -Wno-conversion            # 三方库豁免: BLAKE3
    -Wno-sign-conversion       # 三方库豁免: BLAKE3
    -Wno-shadow                # 三方库豁免: BLAKE3
    -Wno-old-style-cast        # 三方库豁免: BLAKE3
    -Wno-unused-function       # 三方库豁免: BLAKE3
    -Wno-unused-but-set-variable  # 三方库豁免: BLAKE3
    -Wno-unused-variable       # 三方库豁免: BLAKE3
)

# macOS / Linux 平台差异: blake3_dispatch.c 检测 SIMD 特性
# 关闭 AVX-512/AVX2 汇编路径 (portable 已足够, M5 后升级)
target_compile_definitions(stcpp_blake3 PRIVATE
    BLAKE3_NO_AVX512=1
    BLAKE3_NO_AVX2=1
    BLAKE3_NO_SSE41=1
    BLAKE3_NO_SSE2=1
    BLAKE3_USE_NEON=0   # GM 错 #11 hotfix: arm64 (Apple Silicon) 不编译 NEON 源文件, 强禁用 NEON path
)

# ---------- stcpp_observability_audit ----------------------------------------

add_library(stcpp_observability_audit STATIC
    audit_emitter.cpp
)

target_include_directories(stcpp_observability_audit
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

# wal_writer.cpp 单 TU 模板实例化 — include 路径需要 src 根
target_include_directories(stcpp_observability_audit
    PRIVATE ${CMAKE_SOURCE_DIR}
)

target_compile_features(stcpp_observability_audit PUBLIC cxx_std_20)

# R-11 物理隔离 macro (live → RiskAudit, paper/backtest → PaperAudit)
# R-7 (M1-A06): paper/live build 都注入 BLAKE3_REAL=1 (老韩 Smell #2)
# CI grep: 禁 BLAKE3_STUB 出现在 live/paper build (老高 PR v1.2)
target_compile_definitions(stcpp_observability_audit PUBLIC
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
    BLAKE3_REAL=1
)

# 链路: AuditEmitter → WAL framework + BLAKE3 C 实现
target_link_libraries(stcpp_observability_audit
    PUBLIC stcpp_infra_wal
    PUBLIC stcpp_blake3
)
