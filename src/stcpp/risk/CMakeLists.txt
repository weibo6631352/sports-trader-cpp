# src/stcpp/risk/CMakeLists.txt — RiskGateway static lib (老韩 W4 Wave 19)
#
# 落: docs/RESEARCH/laohan-riskmanager-design-v0.3.1.md
#
# 红线:
#   R-1   任何 reject 必经此 lib (绕过 = P0)
#   R-7   ExecutionMode build-time — paper / live / backtest 用 STCPP_EXEC_MODE_* define 编译期分流
#   R-11  paper 走 PaperAudit WAL, live 走 RiskAudit (audit emitter 在 ctor 注入, 此 lib 不直接 link wal)
#   R-20  PIT assert chain 调用 stcpp_infra_wal 的 pit.hpp (header-only)

add_library(stcpp_risk STATIC
    risk_gateway.cpp
    position_ledger.cpp
)

# ---------------------------------------------------------------------------
# stcpp_ledger_snapshot_hub — per-market double-buffer 持仓/PnL 只读快照发布器
#
# Owner: 小石 (#41, data-structures-expert, G-LEDGER-OWNER)
# 独立 target: 仅依赖标准库
# 零反向依赖 (R-12): 不 link RM / signer / exec / debug_api
# R-11: LedgerFeatures.mode 区分 paper/live/backtest
# R-20: 4-ts 透传 (来自 VirtualFill.as_of_ts_ns)
#
# root 集成说明 (给集成方 — 不编辑 root CMakeLists.txt):
#   本 target 已在 src/stcpp/risk/CMakeLists.txt 中定义.
#   root CMakeLists.txt 已 add_subdirectory(src/stcpp/risk).
#   → 无需修改 root CMakeLists.txt, 直接可用 stcpp_ledger_snapshot_hub target.
#   tests/unit/CMakeLists.txt EXTRA_LIBS 映射表补:
#     set(EXTRA_LIBS_test_ledger_snapshot_hub  stcpp_ledger_snapshot_hub)
#     set(EXTRA_LIBS_test_quote_snapshot_hub   stcpp_quote_snapshot_hub)
#   LABELS 映射:
#     set(LABELS_test_ledger_snapshot_hub  "unit;risk;ledger;double-buffer;r-11;r-12;r-20;xiaoshi")
#     set(LABELS_test_quote_snapshot_hub   "unit;sizing;quote;double-buffer;r-12;r-20;xiaoshi")
# ---------------------------------------------------------------------------
add_library(stcpp_ledger_snapshot_hub STATIC
    ledger_snapshot_hub.cpp
)

target_include_directories(stcpp_ledger_snapshot_hub
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_ledger_snapshot_hub PUBLIC cxx_std_20)

# 零反向依赖: 纯头文件 + 标准库 (R-12: 不 link RM / signer / exec / debug_api)
# target_link_libraries 省略: 无外部依赖 (纯标准库)

target_include_directories(stcpp_risk
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_risk PUBLIC cxx_std_20)

# ExecutionMode build-time define (R-7)
target_compile_definitions(stcpp_risk PUBLIC
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)

# pit.hpp 在 stcpp_infra_wal 提供 (header-only 引用, 不强制 link)
# 真上线 (W5) 接 WalWriter<PaperAuditRecord> / <RiskAuditRecord> 时再 link.
target_link_libraries(stcpp_risk
    PUBLIC stcpp_infra_wal
)
