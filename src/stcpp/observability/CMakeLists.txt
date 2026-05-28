# src/stcpp/observability/CMakeLists.txt — AuditEmitter v0.1 (W4 Wave 19)
#
# Owner: 老唐 (audit-owner) + 老韩 RM 上游 + 老王 WAL 下游
# 落: laotang-audit-schema-v1.1.md §5  laowang-wal-framework-cpp-interface-v1.md §8

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

# 通过 build-time 注入 R-11 物理隔离 macro (live → RiskAudit, paper/backtest → PaperAudit)
target_compile_definitions(stcpp_observability_audit PUBLIC
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)

# 链路: AuditEmitter 实例化 WalWriter<AuditRecord> — 链 framework
target_link_libraries(stcpp_observability_audit
    PUBLIC stcpp_infra_wal
)

# BLAKE3 真上线在 Sprint-3 (@老孙). 此处 stub, 不依赖第三方.
