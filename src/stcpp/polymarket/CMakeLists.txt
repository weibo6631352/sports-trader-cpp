# src/stcpp/polymarket/CMakeLists.txt — PolymarketClient module (R-7 mode-isolated)
#
# Owner: 老李 (#07) — Sprint-2 W5 Wave 24 (W5-A-01)
# 落: docs/RESEARCH/laoli-polymarket-backend-requirements-v1.md §1.2 (paper/live 物理隔离)
#     docs/RESEARCH/laozhou-manager-mandate-v1.md §4.1 W5-01-A
# 红线 R-7: 顶层 CMake 按 STCPP_EXEC_MODE 选 paper / live, 二选一 link.
#   - paper:  stcpp_polymarket_paper  (in-memory mock, 不签真签名, OrderAck → PaperAudit)
#   - live:   stcpp_polymarket_live   (stub only v0.1; M5+ 接入真 PM endpoint + HMAC + EIP-712)
#   - 不允许 paper symbol 进 live binary, 反之亦然 (CI 隔离校验由 W5-T6 老吴 nm grep 补).
#
# 不耻下问:
#   - HMAC L2 实现 → @老孙 signer M5+
#   - WSS sports subscriber 接入 → @小冯 D 单元 W5-02bis
#   - paper VirtualMatcher 联调 → @小蒋 W5+

if(STCPP_EXEC_MODE STREQUAL "paper")
    add_library(stcpp_polymarket_paper STATIC
        paper/paper_pm_client.cpp
    )
    target_include_directories(stcpp_polymarket_paper
        PUBLIC ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_features(stcpp_polymarket_paper PUBLIC cxx_std_20)
    target_compile_definitions(stcpp_polymarket_paper PRIVATE
        STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
    )
    # paper PM client 依赖 WAL framework (PIT + WalKind tag, R-11 + R-20)
    target_link_libraries(stcpp_polymarket_paper PUBLIC stcpp_infra_wal)
endif()

if(STCPP_EXEC_MODE STREQUAL "live")
    add_library(stcpp_polymarket_live STATIC
        live/live_pm_client.cpp
    )
    target_include_directories(stcpp_polymarket_live
        PUBLIC ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_features(stcpp_polymarket_live PUBLIC cxx_std_20)
    target_compile_definitions(stcpp_polymarket_live PRIVATE
        STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
    )
    target_link_libraries(stcpp_polymarket_live PUBLIC stcpp_infra_wal)
    # M5+ 接入: libcurl (REST) + libwebsockets (WSS) + libsecp256k1 (EIP-712).
    # 当前 stub 不引入任何第三方依赖, link 顺利.
endif()

# 小冯 W5 Wave 24 (W5-D-02): PM sports-api WSS subscriber v0.1 — paper + live 共享 (read-only)
#   第 5 host wss://sports-api.polymarket.com/ws 公开免鉴权 (R-33 流程红线触发的新发现).
#   transport-agnostic — IWssTransport 抽象, 真 boost.beast 实现 W6 老周 ack 后接入.
add_subdirectory(wss)

# 小冯 Wave 79 (W9 W3): CLOB WSS subscriber — market + user channel
#   endpoint: wss://ws-subscriptions-clob.polymarket.com/ws/{market,user}
#   spec: 老李 W9 W2 laoli-w9-wss-subscriber-impl-spec-v1.md
#   ADR-027 Enforce-1
add_subdirectory(clob_wss)

