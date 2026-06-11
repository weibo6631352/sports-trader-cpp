# src/stcpp/app/CMakeLists.txt — paper daemon 编排层 (老郭 §A.1 / 老周架构裁定 B1)
#
# Owner: 老雷 (GM)  last_review: 2026-05-30
#
# 老周 B1: PaperDaemon + market_discovery 不进 stcpp_debug_api 契约库 (该库仅 link
#   Threads). 装进去会把 11 个重依赖灌进契约库, 坐实老郭 §A.2 膨胀. 故新建 thin
#   app 编排层 stcpp_paper_app, 依赖方向 app → debug_api 单向.
#
# 产出:
#   stcpp_paper_app       STATIC — PaperDaemon + market_discovery (可测)
#   stcpp_paper_server    EXE    — RunMode::PaperDaemon (带 HTTP; 原 debug_api 定义, 现迁此)
#   stcpp_paper_runtime   EXE    — RunMode::Headless (systemd /opt/stcpp/bin/paper_runtime)
#
# 关联: ADR-011 (paper/live binary), R-11 (paper 隔离), R-12 (后台线程独立)

# ---------------------------------------------------------------------------
# OpenSSL (LiveWssTransport POSIX+OpenSSL TLS 1.3; header-only, 经 paper_daemon.cpp 实体化)
# ---------------------------------------------------------------------------
find_package(OpenSSL QUIET)
if(NOT OpenSSL_FOUND)
    find_library(OPENSSL_SSL_LIB ssl PATHS /opt/homebrew/opt/openssl@3/lib NO_DEFAULT_PATH)
    find_library(OPENSSL_CRYPTO_LIB crypto PATHS /opt/homebrew/opt/openssl@3/lib NO_DEFAULT_PATH)
    find_path(OPENSSL_INCLUDE_DIR openssl/ssl.h PATHS /opt/homebrew/opt/openssl@3/include NO_DEFAULT_PATH)
    if(OPENSSL_SSL_LIB AND OPENSSL_CRYPTO_LIB AND OPENSSL_INCLUDE_DIR)
        set(OPENSSL_FOUND_MANUAL TRUE)
        message(STATUS "stcpp_paper_app: OpenSSL found via homebrew: ${OPENSSL_SSL_LIB}")
    else()
        message(WARNING "stcpp_paper_app: OpenSSL not found — live WSS will fail at link. brew install openssl@3")
    endif()
endif()

# ---------------------------------------------------------------------------
# stcpp_paper_app STATIC — PaperDaemon + market_discovery
# ---------------------------------------------------------------------------
add_library(stcpp_paper_app STATIC
    market_discovery.cpp
    event_matcher.cpp
    team_alias.cpp
    paper_daemon.cpp
)
target_include_directories(stcpp_paper_app PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_features(stcpp_paper_app PUBLIC cxx_std_20)
# per-mode 宏 (kCompiledMode 用; paper_runtime G1 static_assert 依赖) + EXEC_MODE_STR
target_compile_definitions(stcpp_paper_app PUBLIC
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
    STCPP_EXEC_MODE_STR="${STCPP_EXEC_MODE}"
)
target_link_libraries(stcpp_paper_app PUBLIC
    stcpp_debug_api            # app → debug_api 单向 (RealStateProvider/HttpServer/live_*)
    stcpp_orderbook_snapshot_hub
    stcpp_risk
    stcpp_ledger_snapshot_hub
    stcpp_sizing
    stcpp_quote_snapshot_hub
    stcpp_data_score_store
    stcpp_data_inplay_feed
    stcpp_data_live_stats_parser  # live_stats: CommentariesParser→LiveStatsParser::Parse (commentaries Feed)
    stcpp_paper_loop
    stcpp_pricing
    stcpp_signer_paper
    stcpp_execution_paper
    stcpp_process_lock         # 程序级防多开 (SingleInstanceLock; paper_server/runtime main 用)
)
# 2026-06-12 单参数切换: live 模式装配 LiveCredentials→Submitter→Gate→Executor→Adapter
# (paper_daemon.cpp `if constexpr (kCompiledMode==Live)` 块)。gate 库本身 mode-agnostic,
# 但 LiveOrderSubmitter 实现只在 live 模式定义 (stcpp_polymarket_live), 故条件链接。
if(STCPP_EXEC_MODE STREQUAL "live")
    target_link_libraries(stcpp_paper_app PUBLIC
        stcpp_polymarket_live
        stcpp_polymarket_order_gate
    )
endif()
# OpenSSL link (LiveWssTransport)
if(OpenSSL_FOUND)
    target_link_libraries(stcpp_paper_app PUBLIC OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(stcpp_paper_app PUBLIC STCPP_LIVE_WSS_OPENSSL=1)
elseif(OPENSSL_FOUND_MANUAL)
    target_link_libraries(stcpp_paper_app PUBLIC ${OPENSSL_SSL_LIB} ${OPENSSL_CRYPTO_LIB})
    target_include_directories(stcpp_paper_app SYSTEM PUBLIC ${OPENSSL_INCLUDE_DIR})
    target_compile_definitions(stcpp_paper_app PUBLIC STCPP_LIVE_WSS_OPENSSL=1)
endif()
# 三方库豁免: cpp-httplib (经 stcpp_debug_api PUBLIC SYSTEM include 传入, 宏展开/cast/format 警告)
# 三方库豁免: OpenSSL (LiveWssTransport TLS, SYSTEM 头) — ADR-010 §3.2
target_compile_options(stcpp_paper_app PRIVATE
    -Wno-old-style-cast
    -Wno-cast-align
    -Wno-sign-conversion
    -Wno-conversion
    -Wno-shadow
    -Wno-double-promotion
    -Wno-format-nonliteral
)

# ---------------------------------------------------------------------------
# stcpp_paper_server EXE — RunMode::PaperDaemon (带 HTTP 运营观测端)
#   (原 stcpp_paper_server / debug_server_main.cpp — W9 骨架遗留名, 2026-05-30 改名;
#    它是 paper daemon 的运营观测 HTTP 服务, 非调试工具. OUTPUT_NAME=paper_server,
#    与 headless 的 paper_runtime 对齐.)
# ---------------------------------------------------------------------------
add_executable(stcpp_paper_server paper_server_main.cpp)
set_target_properties(stcpp_paper_server PROPERTIES OUTPUT_NAME paper_server)
target_link_libraries(stcpp_paper_server PRIVATE stcpp_paper_app)
target_compile_features(stcpp_paper_server PRIVATE cxx_std_20)
# 三方库豁免: cpp-httplib (经 stcpp_paper_app→stcpp_debug_api SYSTEM include) + OpenSSL — ADR-010 §3.2
target_compile_options(stcpp_paper_server PRIVATE
    -Wno-old-style-cast -Wno-cast-align -Wno-sign-conversion -Wno-conversion
    -Wno-shadow -Wno-double-promotion -Wno-format-nonliteral
)

# ---------------------------------------------------------------------------
# stcpp_paper_runtime EXE — RunMode::Headless (systemd)
# ---------------------------------------------------------------------------
add_executable(stcpp_paper_runtime paper_runtime_main.cpp)
set_target_properties(stcpp_paper_runtime PROPERTIES OUTPUT_NAME paper_runtime)
target_link_libraries(stcpp_paper_runtime PRIVATE stcpp_paper_app)
target_compile_features(stcpp_paper_runtime PRIVATE cxx_std_20)
# 三方库豁免: cpp-httplib (经 stcpp_paper_app→stcpp_debug_api SYSTEM include) + OpenSSL — ADR-010 §3.2
target_compile_options(stcpp_paper_runtime PRIVATE
    -Wno-old-style-cast -Wno-cast-align -Wno-sign-conversion -Wno-conversion
    -Wno-shadow -Wno-double-promotion -Wno-format-nonliteral
)
# 部署: install 到 bin/ → provision.sh 用 cmake --install (老吴对齐 §2, T4)
install(TARGETS stcpp_paper_runtime RUNTIME DESTINATION bin)

message(STATUS "stcpp_paper_app: PaperDaemon 编排层 | paper_server(http) + paper_runtime(headless) | mode=${STCPP_EXEC_MODE}")
