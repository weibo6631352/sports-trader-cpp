# src/stcpp/engine/CMakeLists.txt — TradingLoop 静态库
#
# Owner: 小肖 (numerical-algorithms, A 系统工程部)
# last_review: 2026-05-30
#
# 功能: 最小 paper 交易循环 (TradingLoop)
#   消费 OrderBookSnapshotHub → FairValueEstimator → SizingCalculator →
#   RiskGateway → PaperSigner → VirtualMatcher → PositionLedger →
#   LedgerSnapshotHub + QuoteSnapshotHub
#
# 红线:
#   R-11: paper 不污染真账本 (VirtualFill.mode_tag==0 + PaperSigner PaperAudit)
#   R-12: 独立线程 (std::jthread), 不进 WSS event loop
#   R-20: 4 ts 全链路透传
#
# 依赖:
#   stcpp_orderbook_snapshot_hub  — hub_.Read() 只读
#   stcpp_pricing                 — FairValueEstimator
#   stcpp_sizing                  — SizingCalculator + QuoteSnapshotHub
#   stcpp_risk                    — RiskGateway + PositionLedger + LedgerSnapshotHub + RmDebugSnapshot
#   stcpp_ledger_snapshot_hub     — LedgerSnapshotHub::Publish
#   stcpp_quote_snapshot_hub      — QuoteSnapshotHub::Publish
#   stcpp_signer_paper            — PaperSigner + VirtualNonce + VirtualGas + VirtualConfirm
#   stcpp_execution_paper         — VirtualMatcher

add_library(stcpp_trading_loop STATIC
    trading_loop.cpp
)

target_include_directories(stcpp_trading_loop
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_trading_loop PUBLIC cxx_std_20)

# git commit 注入 (2026-06-14 review #2): version 标签行带 commit, 离线按"代码版本"切段 (同 config 不同 build
#   也能分)。CI 可 -DSTCPP_BUILD_COMMIT 传; 否则 configure 时自动探 git HEAD (start 脚本每次跑 cmake → 当前)。
if(NOT DEFINED STCPP_BUILD_COMMIT)
    execute_process(COMMAND git rev-parse --short HEAD
                    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                    OUTPUT_VARIABLE STCPP_BUILD_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT STCPP_BUILD_COMMIT)
        set(STCPP_BUILD_COMMIT "unknown")
    endif()
endif()
target_compile_definitions(stcpp_trading_loop PRIVATE STCPP_BUILD_COMMIT="${STCPP_BUILD_COMMIT}")

# ExecutionMode (R-7: paper loop 仅 paper binary 使用; 此 target 不强制 mode,
# 由使用方 binary 决定; 这里不加 EXEC_MODE define 以便测试用 paper mode 编译)

target_link_libraries(stcpp_trading_loop PUBLIC
    stcpp_orderbook_snapshot_hub
    stcpp_pricing
    stcpp_sizing
    stcpp_risk
    stcpp_ledger_snapshot_hub
    stcpp_quote_snapshot_hub
    stcpp_signer_paper
    stcpp_execution_paper
    stcpp_infra_wal
    stcpp_data_score_store  # A1: ScoreSnapshotStore::Get(inplay_match_id) → 真实 Goalserve 比分
    stcpp_stats             # 2026-06-12 治理: GateEvaluator G1-G7 记分牌接 daily-close
)

# ADR-010: 不添加 -Wno-* 抑制 (Rule 1: 生产代码禁新增 -Wno-conversion/-Wno-sign-conversion)
# trading_loop.cpp 源码已通过显式强转修正所有 conversion 警告
