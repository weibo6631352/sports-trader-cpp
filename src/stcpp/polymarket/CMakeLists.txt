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

# (stcpp_polymarket_paper [paper_pm_client mock] + live_pm_client stub + pm_client.hpp 接口
#  2026-06-12 治理删: W5 旧规划层, 零链接消费者。paper 撮合走 execution/VirtualMatcher,
#  live 下单走 live_order_submitter (直连 CLOB), IPmClient 抽象从未落地。)

# 2026-06-12 运行时 mode: live 下单器全 mode 构建 (单 binary; 装配由运行时 mode 决定)。
find_package(CURL REQUIRED)
add_library(stcpp_polymarket_live STATIC
    live/live_order_submitter.cpp   # GM 2026-05-31 Phase 3: 实盘下单器 (实盘验证)
    live/onchain_balance.cpp        # GM 2026-06-12: 链上 pUSD 余额读取 (live 真实本金)
)
target_include_directories(stcpp_polymarket_live
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(stcpp_polymarket_live PUBLIC cxx_std_20)
# Phase 3: 签名(eip712+secp256k1) + wire(body/HMAC) + libcurl POST 串成下单器。
target_link_libraries(stcpp_polymarket_live
    PUBLIC  stcpp_infra_wal
            stcpp_crypto_eip712
            stcpp_crypto_secp256k1
            stcpp_polymarket_clob_wire
    PRIVATE CURL::libcurl
)
# Phase 3 真网验证 CLI
add_executable(stcpp_live_order_cli live/live_order_cli.cpp)
target_link_libraries(stcpp_live_order_cli PRIVATE stcpp_polymarket_live CURL::libcurl)

# 小冯 W5 Wave 24 (W5-D-02): PM sports-api WSS subscriber v0.1 — paper + live 共享 (read-only)
#   第 5 host wss://sports-api.polymarket.com/ws 公开免鉴权 (R-33 流程红线触发的新发现).
#   transport-agnostic — IWssTransport 抽象, 真 boost.beast 实现 W6 老周 ack 后接入.
add_subdirectory(gate)
add_subdirectory(wire)
add_subdirectory(wss)

# GM 2026-06-13 一次性诊断: daemon live 执行链断点定位探针 (gate→executor→adapter→apply_fill 单发)
add_executable(stcpp_live_chain_probe live/live_chain_probe.cpp)
target_link_libraries(stcpp_live_chain_probe
    PRIVATE stcpp_polymarket_live stcpp_polymarket_order_gate stcpp_risk stcpp_execution_paper CURL::libcurl)

# 小冯 Wave 79 (W9 W3): CLOB WSS subscriber — market + user channel
#   endpoint: wss://ws-subscriptions-clob.polymarket.com/ws/{market,user}
#   spec: 老李 W9 W2 laoli-w9-wss-subscriber-impl-spec-v1.md
#   ADR-027 Enforce-1
add_subdirectory(clob_wss)

