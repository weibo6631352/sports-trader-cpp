# tests/chaos/CMakeLists.txt — E-03 chaos test framework 顶层入口
#
# Owner: 小宋 (test-replay-engineer)  E-03 chaos v1
# 关联: docs/RESEARCH/xiaosong-chaos-replay-framework-spec-v1.md §1 §4.3
#
# 仅在 paper build 启用 (chaos 是 paper runtime 安全网)
# spec §5.4: paper mode 防串 — ChaosE2EFixture::SetUp() 强制 assert
#
# CTest 标签:
#   chaos_smoke    → C-PAPER-01~06 + C-WSS-01 + C-REST-01 + C-FILL-04 (gate 核心)
#   chaos_full     → 全部 chaos
#   paper_runtime_gate → chaos_smoke + replay_gate (两者 PASS 才放行)

if(NOT STCPP_EXEC_MODE STREQUAL "paper")
    message(STATUS "tests/chaos: skip (STCPP_EXEC_MODE=${STCPP_EXEC_MODE}, paper-only)")
    return()
endif()

add_subdirectory(wss)
add_subdirectory(ooo)
add_subdirectory(latency)
add_subdirectory(rest_timeout)
add_subdirectory(partial_fill)
add_subdirectory(paper)
