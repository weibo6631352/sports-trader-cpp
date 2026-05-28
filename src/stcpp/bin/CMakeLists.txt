# src/stcpp/bin/CMakeLists.txt — 主二进制 (build-time mode switch)
#
# 落:
#   xiaojiang-paper-engine-skeleton-v1.md §5 (paper.cpp main)
#   ADR gm-signoff-paper-trade R-7 (三 target 物理隔离)
#
# 红线: 一次 build 只产一个 binary. STCPP_EXEC_MODE = paper / live / backtest.
# paper signer .o 不进 live binary.

if(STCPP_EXEC_MODE STREQUAL "paper")
    add_executable(stcpp_paper paper.cpp)
    target_compile_definitions(stcpp_paper PRIVATE STCPP_EXEC_MODE_paper=1)
    target_link_libraries(stcpp_paper PRIVATE
        stcpp_process_lock      # W5 Wave 25: 防多开 (R-7/R-11/R-12)
        stcpp_signer_paper
        stcpp_execution_paper
        stcpp_infra_wal
    )
    target_include_directories(stcpp_paper PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_features(stcpp_paper PRIVATE cxx_std_20)
endif()

# Live / Backtest deferred (派单约束; M4.5+).
# if(STCPP_EXEC_MODE STREQUAL "live")
#     add_executable(stcpp_live live.cpp)
#     ...
# endif()
