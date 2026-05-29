# src/stcpp/execution/CMakeLists.txt — Execution module
#
# 落: xiaojiang-paper-engine-skeleton-v1.md §4 (VirtualMatcher)
#
# 红线 R-7: VirtualMatcher 只在 paper binary 里编译.
# (Live binary 不该有 "虚拟撮合"; backtest 走 SimMatcher 另外的 target, M5 后)

if(STCPP_EXEC_MODE STREQUAL "paper")
    add_library(stcpp_execution_paper STATIC
        virtual_matcher.cpp
    )
    target_include_directories(stcpp_execution_paper
        PUBLIC ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_features(stcpp_execution_paper PUBLIC cxx_std_20)
    target_compile_definitions(stcpp_execution_paper PRIVATE
        STCPP_EXEC_MODE_paper=1
    )
    # Wave 3: MatchWithBook() 依赖 stcpp_microstructure (FillRateModel::compute_from_clob_book)
    target_link_libraries(stcpp_execution_paper PUBLIC
        stcpp_infra_wal
        stcpp_microstructure
    )
    # Wave 3: compute_from_clob_book 内含 std::exp (-cmath), 关闭 double-promotion 同 microstructure
    target_compile_options(stcpp_execution_paper PRIVATE
        -Wno-double-promotion
    )
endif()
