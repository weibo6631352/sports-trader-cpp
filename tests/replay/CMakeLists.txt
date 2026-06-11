# tests/replay/CMakeLists.txt — E-04 replay test framework 顶层入口
#
# Owner: 小宋 (test-replay-engineer)  E-04 replay framework v1
# 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §2 §4.3
#
# 仅在 paper build 启用 (replay 是 paper runtime version upgrade gate)
# spec §5.4: ReplayDriver::Init() 强制校验 execution::ExecutionContext::Mode() == Paper
#
# CTest 标签:
#   replay_gate         → R-R11-01~04 + R-R20-01~07 + R-chain-01~05
#   paper_runtime_gate  → chaos_smoke + replay_gate (两者 PASS 才放行)

if(NOT STCPP_EXEC_MODE STREQUAL "paper")
    message(STATUS "tests/replay: skip (STCPP_EXEC_MODE=${STCPP_EXEC_MODE}, paper-only)")
    return()
endif()

add_subdirectory(paper)   # header-only assertion 库 (无 binary target)
add_subdirectory(r11)
add_subdirectory(r20)
add_subdirectory(audit_chain)
