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
# paper signer 不能出现在 live binary; live signer 不能出现在 paper binary.

if(STCPP_EXEC_MODE STREQUAL "paper")
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
