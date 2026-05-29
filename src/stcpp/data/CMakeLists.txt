# src/stcpp/data/CMakeLists.txt — Goalserve client + schema + data-contract
#
# Owner: 小段 (goalserve-specialist, #37)
# Sprint-2 W6 Wave 29 — D-W5-03/05 data-contract 落 src/stcpp/data/
# 小段 W9+ — inplay score parser + ScoreSnapshotStore
#
# Targets:
#   stcpp_data_goalserve       — W4 stub (goalserve_stub.cpp), URL builder + client
#   stcpp_data_schema          — header-only schema lib (goalserve_schema + data_contract + odds_record)
#   stcpp_data_contract        — static lib alias (links stcpp_data_schema + stcpp_data_goalserve)
#   stcpp_data_inplay_parser   — InplayScoreParser (soccer/basketball/tennis, 零外部依赖)
#   stcpp_data_score_store     — ScoreSnapshotStore (R-12 RCU-lite in-mem store)

# ---------------------------------------------------------------------------
# stcpp_data_goalserve: GoalserveClient stub (W4, W5 接 cpp-httplib)
# ---------------------------------------------------------------------------
add_library(stcpp_data_goalserve STATIC
    goalserve_stub.cpp
)

target_include_directories(stcpp_data_goalserve
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_data_goalserve PUBLIC cxx_std_20)

# 不依赖第三方 (W5 真接时 PRIVATE link cpp-httplib + zlib)

# ---------------------------------------------------------------------------
# stcpp_data_schema: header-only schema layer
#   goalserve_schema.hpp / data_contract.hpp / odds_record.hpp
#   这 3 个头文件均 header-only; INTERFACE target 提供 include path.
# ---------------------------------------------------------------------------
add_library(stcpp_data_schema INTERFACE)

target_include_directories(stcpp_data_schema
    INTERFACE ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_data_schema INTERFACE cxx_std_20)

# schema 依赖 goalserve_client (枚举 + URL builder SSOT)
target_link_libraries(stcpp_data_schema
    INTERFACE stcpp_data_goalserve
)

# ---------------------------------------------------------------------------
# stcpp_data_contract: 聚合 alias (供下游 link 时一步到位)
#   link stcpp_data_contract → 同时获得 schema + client
# ---------------------------------------------------------------------------
add_library(stcpp_data_contract INTERFACE)

target_link_libraries(stcpp_data_contract
    INTERFACE stcpp_data_schema
)

# ---------------------------------------------------------------------------
# stcpp_data_inplay_parser: InplayScoreParser (小段 W9+)
#
# 解析 inplay.goalserve.com/inplay-<sport>.gz JSON → GameScoreRecord[]
# MVP sport: Soccer / Basketball / Tennis
# 零外部依赖 (手写极简 JSON key 提取, 无 nlohmann/simdjson/glaze)
# R-20: data_source_ts = updated_ts (ms); 禁本地 now() 替代 data_source_ts
# R-12: 纯 CPU-bound, 无 IO, 可在采集线程调用
#
# 真实 HTTP 接线 (W5 TODO):
#   src/stcpp/data/inplay_feed_thread.cpp — GoalserveClient::Fetch → gzip decompress
#   → InplayScoreParser::Parse → ScoreSnapshotStore::Publish
# ---------------------------------------------------------------------------
add_library(stcpp_data_inplay_parser STATIC
    inplay_score_parser.cpp
)

target_include_directories(stcpp_data_inplay_parser
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_data_inplay_parser PUBLIC cxx_std_20)

target_link_libraries(stcpp_data_inplay_parser
    PUBLIC stcpp_data_contract  # GameScoreRecord, FourTs, GoalserveSport, TimeStatus
)

# 零外部依赖 (手写 JSON 提取, std::from_chars / std::string_view)
# 不需要 nlohmann_json / simdjson (W10+ 升级再引入)

# ---------------------------------------------------------------------------
# stcpp_data_score_store: ScoreSnapshotStore (小段 W9+)
#
# C++20 atomic<shared_ptr> RCU-lite in-mem store
# 采集线程 Publish(map) → 读取线程 Get(event_id) O(1) 无锁
# R-12: Get() 无 IO, 无锁 > 100us, 纳秒级
# R-20: EventScore.ts.data_source_ts_ns 透传 Goalserve updated_ts
#
# 依赖 state_provider.hpp (EventScore, FourTs) — debug_api 契约
# 注意: 不 include debug_api 实现, 仅 include 头文件 (零反向依赖, R-12)
# ---------------------------------------------------------------------------
add_library(stcpp_data_score_store STATIC
    score_snapshot_store.cpp
)

target_include_directories(stcpp_data_score_store
    PUBLIC ${CMAKE_SOURCE_DIR}/include
           ${CMAKE_SOURCE_DIR}/src   # state_provider.hpp 路径 (src/stcpp/debug_api/)
)

target_compile_features(stcpp_data_score_store PUBLIC cxx_std_20)

find_package(Threads REQUIRED)
target_link_libraries(stcpp_data_score_store
    PUBLIC Threads::Threads
)
