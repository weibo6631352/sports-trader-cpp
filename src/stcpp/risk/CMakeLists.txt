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
)

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
