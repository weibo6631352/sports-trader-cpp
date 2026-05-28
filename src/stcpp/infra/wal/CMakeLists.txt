# src/stcpp/infra/wal/CMakeLists.txt — WAL framework skeleton (W3) + Position WAL (W6 P0)
#
# 落:
#   laowang-wal-framework-v0.2.md §10 (W3 落地)
#   W6-A-position-wal P0 (老韩 Smell-3 升级, GM Wave 27 拍板)
#
# stcpp_infra_wal:     header-only + wal_writer.cpp (模板实例化 + path 硬校验)
# stcpp_position_ledger: PositionRecord + PositionLedger (R-1/7/11/20)
#
# R-7 paper/live 物理隔离:
#   CMake 通过 STCPP_EXEC_MODE 选择 position WAL 路径:
#     paper  → STCPP_POSITION_WAL_PREFIX=/var/lib/stcpp/paper/position
#     live   → STCPP_POSITION_WAL_PREFIX=/var/lib/stcpp/live/position
#   build-time 宏注入, runtime 不可覆盖 (R-7).
#
# R-11 guard:
#   paper build: STCPP_POSITION_WAL_PREFIX 必须含 "paper" (CMake assert)
#   live  build: STCPP_POSITION_WAL_PREFIX 必须含 "live"  (CMake assert)

add_library(stcpp_infra_wal STATIC
    wal_writer.cpp
)

target_include_directories(stcpp_infra_wal
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_infra_wal PUBLIC cxx_std_20)

# WAL framework 不依赖任何第三方 (BLAKE3 在 payload owner 算, SPSC W4 接 @小石).

# ---------------------------------------------------------------------------
# stcpp_position_ledger — PositionLedger static lib (W6 P0)
#
# 依赖: stcpp_infra_wal (WalWriter framework)
# R-7 build-time guard: 根据 STCPP_EXEC_MODE 选择 WAL 路径前缀
# ---------------------------------------------------------------------------

# R-7: 根据 STCPP_EXEC_MODE 确定 position WAL 路径前缀
if(NOT DEFINED STCPP_EXEC_MODE)
    set(STCPP_EXEC_MODE "paper")
endif()

if(STCPP_EXEC_MODE STREQUAL "paper")
    set(STCPP_POSITION_WAL_PREFIX "/var/lib/stcpp/paper/position")
elseif(STCPP_EXEC_MODE STREQUAL "live")
    set(STCPP_POSITION_WAL_PREFIX "/var/lib/stcpp/live/position")
elseif(STCPP_EXEC_MODE STREQUAL "backtest")
    set(STCPP_POSITION_WAL_PREFIX "/var/lib/stcpp/paper/position")
else()
    message(FATAL_ERROR
        "stcpp_position_ledger: unknown STCPP_EXEC_MODE='${STCPP_EXEC_MODE}'. "
        "Must be paper / live / backtest.")
endif()

# R-11 build-time 硬校验:
#   paper/backtest build: path 必须含 "paper" (不能指向 live)
#   live build:           path 必须含 "live"  (不能指向 paper)
if(STCPP_EXEC_MODE STREQUAL "live")
    string(FIND "${STCPP_POSITION_WAL_PREFIX}" "live" _pos_live_idx)
    if(_pos_live_idx EQUAL -1)
        message(FATAL_ERROR
            "R-11 GUARD: live build position WAL prefix must contain 'live'. "
            "Got: ${STCPP_POSITION_WAL_PREFIX}")
    endif()
    string(FIND "${STCPP_POSITION_WAL_PREFIX}" "paper" _pos_paper_idx)
    if(NOT _pos_paper_idx EQUAL -1)
        message(FATAL_ERROR
            "R-11 GUARD: live build position WAL prefix must NOT contain 'paper'. "
            "Got: ${STCPP_POSITION_WAL_PREFIX}")
    endif()
else()
    # paper / backtest
    string(FIND "${STCPP_POSITION_WAL_PREFIX}" "paper" _pos_paper_idx2)
    if(_pos_paper_idx2 EQUAL -1)
        message(FATAL_ERROR
            "R-11 GUARD: paper/backtest build position WAL prefix must contain 'paper'. "
            "Got: ${STCPP_POSITION_WAL_PREFIX}")
    endif()
endif()

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

# R-7 build-time 路径宏注入
target_compile_definitions(stcpp_position_ledger PUBLIC
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
    STCPP_POSITION_WAL_PREFIX="${STCPP_POSITION_WAL_PREFIX}"
)
