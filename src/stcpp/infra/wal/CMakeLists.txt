# src/stcpp/infra/wal/CMakeLists.txt — WAL framework (W3) + Position WAL (W6 P0) + IngestRaw WAL (W6 Wave 29)
#
# 落:
#   laowang-wal-framework-v0.2.md §10 (W3 落地)
#   W6-A-position-wal P0 (老韩 Smell-3 升级, GM Wave 27 拍板)
#   小冯 W6 Wave 29 DS-01: IngestRaw WAL 第 5 WAL
#
# stcpp_infra_wal:          header-only + wal_writer.cpp (模板实例化 + path 硬校验)
# stcpp_position_ledger:    PositionRecord + PositionLedger (R-1/7/11/20)
# stcpp_ingest_raw_writer:  IngestRawWriter 2-tier WAL (DS-01)
#
# R-7 paper/live 物理隔离:
#   CMake 通过 STCPP_EXEC_MODE 选择各 WAL 路径:
#     paper  → STCPP_POSITION_WAL_PREFIX=/var/lib/stcpp/engine/position
#              STCPP_INGEST_RAW_WAL_PREFIX=/var/lib/stcpp/ingest/paper
#     live   → STCPP_POSITION_WAL_PREFIX=/var/lib/stcpp/live/position
#              STCPP_INGEST_RAW_WAL_PREFIX=/var/lib/stcpp/ingest/live
#   build-time 宏注入, runtime 不可覆盖 (R-7).
#
# R-11 guard:
#   paper build: position/ingest path 含 "paper", live build 含 "live"

# ---------------------------------------------------------------------------
# 0. STCPP_EXEC_MODE 全局默认
# ---------------------------------------------------------------------------
# (2026-06-12 运行时 mode: STCPP_EXEC_MODE CMake 变量已废。)

# ---------------------------------------------------------------------------
# 1. stcpp_infra_wal — WAL framework (header-only + wal_writer.cpp)
# ---------------------------------------------------------------------------
add_library(stcpp_infra_wal STATIC
    wal_writer.cpp
)

target_include_directories(stcpp_infra_wal
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_infra_wal PUBLIC cxx_std_20)

# WAL framework 不依赖任何第三方 (BLAKE3 在 payload owner 算, SPSC W4 接 @小石).

# ---------------------------------------------------------------------------
# 2. stcpp_position_ledger — PositionLedger static lib (W6 P0)
#
# 依赖: stcpp_infra_wal (WalWriter framework)
# R-7 build-time guard: 根据 STCPP_EXEC_MODE 选择 WAL 路径前缀
# ---------------------------------------------------------------------------

# (STCPP_POSITION_WAL_PREFIX CMake 块 2026-06-12 删: 零代码消费者的死配置。)

add_library(stcpp_position_ledger STATIC
    position_ledger.cpp
)

target_include_directories(stcpp_position_ledger
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(stcpp_position_ledger
    PUBLIC stcpp_infra_wal
)

target_compile_features(stcpp_position_ledger PUBLIC cxx_std_20)


# ---------------------------------------------------------------------------
# 3. stcpp_ingest_raw_writer — IngestRaw WAL writer (小冯 W6 Wave 29, DS-01)
#
# 依赖: stcpp_infra_wal (WalWriter framework)
# R-7 build-time guard: 根据 STCPP_EXEC_MODE 选择 IngestRaw WAL 路径前缀
# R-11 path guard: 路径含 "ingest"; paper 含 "paper", live 含 "live"
# ---------------------------------------------------------------------------

# (ingest prefix 2026-06-12 运行时化: 默认 paper 在 header, live 由装配方按运行时 mode 选。)
set(STCPP_INGEST_RAW_WAL_PREFIX "/var/lib/stcpp/ingest/paper")

# R-11 硬校验: 路径必须含 "ingest"
string(FIND "${STCPP_INGEST_RAW_WAL_PREFIX}" "ingest" _ingest_idx)
if(_ingest_idx EQUAL -1)
    message(FATAL_ERROR
        "R-11 GUARD: IngestRaw WAL prefix must contain 'ingest'. "
        "Got: ${STCPP_INGEST_RAW_WAL_PREFIX}")
endif()

# R-7 硬校验: paper/live 互斥
if(STCPP_EXEC_MODE STREQUAL "live")
    string(FIND "${STCPP_INGEST_RAW_WAL_PREFIX}" "live" _ir_live_idx)
    if(_ir_live_idx EQUAL -1)
        message(FATAL_ERROR
            "R-7 GUARD: live build IngestRaw WAL prefix must contain 'live'. "
            "Got: ${STCPP_INGEST_RAW_WAL_PREFIX}")
    endif()
    string(FIND "${STCPP_INGEST_RAW_WAL_PREFIX}" "paper" _ir_paper_idx)
    if(NOT _ir_paper_idx EQUAL -1)
        message(FATAL_ERROR
            "R-7 GUARD: live build IngestRaw WAL prefix must NOT contain 'paper'. "
            "Got: ${STCPP_INGEST_RAW_WAL_PREFIX}")
    endif()
else()
    # paper / backtest
    string(FIND "${STCPP_INGEST_RAW_WAL_PREFIX}" "paper" _ir_paper_idx2)
    if(_ir_paper_idx2 EQUAL -1)
        message(FATAL_ERROR
            "R-7 GUARD: paper/backtest build IngestRaw WAL prefix must contain 'paper'. "
            "Got: ${STCPP_INGEST_RAW_WAL_PREFIX}")
    endif()
endif()

add_library(stcpp_ingest_raw_writer STATIC
    ingest_raw_writer.cpp
)

target_include_directories(stcpp_ingest_raw_writer
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(stcpp_ingest_raw_writer
    PUBLIC stcpp_infra_wal
)

target_compile_features(stcpp_ingest_raw_writer PUBLIC cxx_std_20)

# R-7 / R-11 build-time 路径宏注入
target_compile_definitions(stcpp_ingest_raw_writer PUBLIC
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
    STCPP_INGEST_RAW_WAL_PREFIX="${STCPP_INGEST_RAW_WAL_PREFIX}"
)
