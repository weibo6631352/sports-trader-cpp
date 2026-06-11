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

if(STCPP_EXEC_MODE STREQUAL "paper" OR STCPP_EXEC_MODE STREQUAL "live")
    add_library(stcpp_signer_paper STATIC
        paper/paper_signer.cpp
    )
    target_include_directories(stcpp_signer_paper
        PUBLIC ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_features(stcpp_signer_paper PUBLIC cxx_std_20)
    target_compile_definitions(stcpp_signer_paper PRIVATE
        STCPP_EXEC_MODE_paper=1
    )
    # paper signer 只依赖 WAL framework (拿 WalKind 标签) + numerical (header-only Slippage)
    target_link_libraries(stcpp_signer_paper PUBLIC stcpp_infra_wal)
endif()

# Live / Backtest deferred (M4.5+, 当前不实现 — 派单约束)
# if(STCPP_EXEC_MODE STREQUAL "live")
#     add_library(stcpp_signer_live STATIC live/live_signer.cpp)
#     ...
# endif()

# Sprint-2 W6 Wave 30: 老孙 signer v5.2 cpp v0.1 (撤 Rust 后 C++ 重写)
add_subdirectory(v52)

# Wave 104 P0: 老孙 signer v6.2 cpp (V2 ABI, ADR-018 build switch)
add_subdirectory(v62)
