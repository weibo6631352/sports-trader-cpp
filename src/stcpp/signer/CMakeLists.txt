# src/stcpp/signer/CMakeLists.txt — Signer module (mode-isolated targets)
#
# 落:
#   xiaojiang-paper-engine-skeleton-v1.md §3
#   ADR gm-signoff-paper-trade R-7 (Live / Paper / Backtest 三 target 物理隔离)
#
# 红线 R-7: 顶层 CMake 按 STCPP_EXEC_MODE 三选一 link.
#  - paper:    stcpp_signer_paper      (.o 含 mock signature + virtual confirm)
#  - live:     stcpp_signer_live       (M4.5 后实现, 当前 stub)
#  - backtest: stcpp_signer_backtest   (stub)
#
# 2026-06-12 单参数切换 (老板令): live binary 复用 paper_loop 决策链,
# PaperSigner 在 live 模式只做审计链 mock 签名 (从不上线),
# 真 EIP-712 签名在 LiveOrderSubmitter (signer v62)。
# 隔离从「物理不链接」改为「executor seam + R-11 mode_tag 运行时断言 +
# R-7 FATAL guard (STCPP_LIVE_INTENT_OK)」。backtest 仍不链接此库。

# (2026-06-12 运行时 mode: 无条件构建 — 单 binary。)
    add_library(stcpp_signer_paper STATIC
        paper/paper_signer.cpp
    )
    target_include_directories(stcpp_signer_paper
        PUBLIC ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_features(stcpp_signer_paper PUBLIC cxx_std_20)
    # paper signer 只依赖 WAL framework (拿 WalKind 标签) + numerical (header-only Slippage)
    target_link_libraries(stcpp_signer_paper PUBLIC stcpp_infra_wal)

# Live / Backtest deferred (M4.5+, 当前不实现 — 派单约束)
# if(STCPP_EXEC_MODE STREQUAL "live")
#     add_library(stcpp_signer_live STATIC live/live_signer.cpp)
#     ...
# endif()

# (signer v52 [ed25519 IPC] / v62 [V2 ABI transformer] 2026-06-12 治理删: 两代均零生产
#  消费者 — 真 live 签名路径 = live_order_submitter 直用 crypto/eip712_v2 + secp256k1。)
