# src/stcpp/bin/CMakeLists.txt — 主二进制 (build-time mode switch)
#
# 落:
#   xiaojiang-paper-engine-skeleton-v1.md §5 (paper.cpp main)
#   ADR gm-signoff-paper-trade R-7 (三 target 物理隔离)
#
# 红线: 一次 build 只产一个 binary. STCPP_EXEC_MODE = paper / live / backtest.
# paper signer .o 不进 live binary.

if(STCPP_EXEC_MODE STREQUAL "paper")
    add_executable(stcpp_paper paper.cpp)
    target_compile_definitions(stcpp_paper PRIVATE STCPP_EXEC_MODE_paper=1)
    target_link_libraries(stcpp_paper PRIVATE
        stcpp_process_lock      # W5 Wave 25: 防多开 (R-7/R-11/R-12)
        stcpp_signer_paper
        stcpp_execution_paper
        stcpp_infra_wal
    )
    target_include_directories(stcpp_paper PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_features(stcpp_paper PRIVATE cxx_std_20)
endif()

# Live / Backtest deferred (派单约束; M4.5+).
# if(STCPP_EXEC_MODE STREQUAL "live")
#     add_executable(stcpp_live live.cpp)
#     ...
# endif()

# -------------------------------------------------------------------------
# 老沈 W6 Wave 29: stcpp-strategy-unlock CLI (Smell #4, ADR-009 v2)
# W8 Wave 34: 撤 unofficial-sodium::sodium (vcpkg), 改链 stcpp_crypto_ed25519
#             (FetchContent + ExternalProject_Add libsodium-1.0.20, 老孙 W8 N-A)
#
# R-2: paper / live 共享 binary (exec-mode 由 CLI --exec-mode 参数决定, 不 ifdef)
# R-7: paper / live 三签可独立
# R-11: audit WAL 物理隔离 (AuditEmitter path 由 exec-mode 决定)
# 私钥: 不入 binary, 运行时从 .env 读 RM_UNLOCK_PUBKEY_{LAOHAN,LAOTANG,LAOLEI}
#
# libsodium 依赖 (W8 变更):
#   - 撤 find_package(unofficial-sodium CONFIG REQUIRED) — vcpkg 路径已废弃
#   - 改链 stcpp_crypto_ed25519 (ExternalProject_Add libsodium-1.0.20-RELEASE 静态库)
#   - stcpp_cli_three_sig 与 stcpp_signer_v52_paper 共用 stcpp_crypto_ed25519 (老沈 W7 ack)
#
# W7 修 (GM 错 #11 沿用):
#   STCPP_BUILD_CLI 默认 OFF, 不阻主项目 build
# -------------------------------------------------------------------------

option(STCPP_BUILD_CLI "Build stcpp-strategy-unlock CLI (requires stcpp_crypto_ed25519)" OFF)

if(STCPP_BUILD_CLI)
    # 三签验证静态库 (被 CLI binary + test_strategy_unlock 共享)
    add_library(stcpp_cli_three_sig STATIC
        cli/three_signature.cpp
    )
    target_include_directories(stcpp_cli_three_sig PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_features(stcpp_cli_three_sig PUBLIC cxx_std_20)
    # ADR-010 §4: 禁止新增 -Wno-* 抑制。已删 -Wno-sign-conversion -Wno-conversion -Wno-shadow (W7 Wave 33 小宋).
    # 若 cli source 仍有上述 warning 请 @老沈 在独立 PR 修真 source.
    # grandfather 保留 (ADR-010 §2.2): double-promotion / old-style-cast / cast-align
    target_compile_options(stcpp_cli_three_sig PRIVATE
        -Wno-double-promotion -Wno-old-style-cast -Wno-cast-align
    )
    # libsodium (W8: 改链 stcpp_crypto_ed25519; 撤 unofficial-sodium::sodium vcpkg)
    # stcpp_crypto_ed25519 由 src/stcpp/crypto/CMakeLists.txt 定义
    # (ExternalProject_Add libsodium-1.0.20-RELEASE 静态库 + include)
    target_link_libraries(stcpp_cli_three_sig PUBLIC stcpp_crypto_ed25519)

    # CLI 主 binary (stcpp-strategy-unlock)
    add_executable(stcpp_cli_strategy_unlock
        cli/strategy_unlock_cli.cpp
    )
    target_link_libraries(stcpp_cli_strategy_unlock PRIVATE
        stcpp_cli_three_sig
        stcpp_risk
    )
    target_include_directories(stcpp_cli_strategy_unlock PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_features(stcpp_cli_strategy_unlock PRIVATE cxx_std_20)
    # ADR-010 §4: 禁止新增 -Wno-* 抑制。已删 -Wno-sign-conversion -Wno-conversion -Wno-shadow (W7 Wave 33 小宋).
    # 若 cli source 仍有上述 warning 请 @老沈 在独立 PR 修真 source.
    # grandfather 保留 (ADR-010 §2.2): double-promotion / old-style-cast / cast-align
    target_compile_options(stcpp_cli_strategy_unlock PRIVATE
        -Wno-double-promotion -Wno-old-style-cast -Wno-cast-align
    )
    set_target_properties(stcpp_cli_strategy_unlock PROPERTIES
        OUTPUT_NAME "stcpp-strategy-unlock"
    )
endif()
