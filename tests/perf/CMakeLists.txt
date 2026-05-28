# tests/perf/CMakeLists.txt — Sprint-2 W4 Wave 21 (老姜)
#
# Google Benchmark 性能回归 framework. 与 tests/bench/ (W3 小肖 slippage) 共存:
#   - tests/bench/   : 小肖 W3 slippage 旧版 (保持不动, 通过)
#   - tests/perf/    : 老姜 W4 Wave 21 (6 模块 + JSON output + CI gate)
#
# 启用: cmake -S . -B build -DSTCPP_BUILD_BENCH=ON -DCMAKE_BUILD_TYPE=Release
# 跑:   cd build && ./tests/perf/bench_<module> --benchmark_format=json \
#                       --benchmark_out=perf-results/<module>.json
# 比对: python scripts/perf_compare.py build/perf-results tests/perf/baselines/main.json

include(FetchContent)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

# 与 tests/bench/ 同 tag, 避免 FetchContent 拉两次
FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.8.3
    GIT_SHALLOW    TRUE
    SYSTEM
)
FetchContent_MakeAvailable(benchmark)

# 第三方 benchmark 源码不接受公司 -Werror 严格 flag (与 tests/CMakeLists.txt 同处理)
# 注: 这里也救济 tests/bench (W3 小肖 bench_slippage_model) 的同款问题 —
#     顶层 -Werror -Wformat=2 -Wformat-nonliteral 会卡 benchmark/src/colorprint.cc vsnprintf.
foreach(_bm_target benchmark benchmark_main)
    if(TARGET ${_bm_target})
        target_compile_options(${_bm_target} PRIVATE
            -Wno-double-promotion -Wno-sign-conversion -Wno-conversion
            -Wno-shadow -Wno-old-style-cast -Wno-cast-align
            -Wno-format-nonliteral
            -Wno-error
        )
    endif()
endforeach()

# 统一编译选项 (与生产 Release 对齐)
function(stcpp_add_perf_bench TGT)
    add_executable(${TGT} ${ARGN})
    target_link_libraries(${TGT} PRIVATE benchmark::benchmark)
    target_include_directories(${TGT} PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_compile_features(${TGT} PRIVATE cxx_std_20)
    # -O2 与生产对齐, NDEBUG 关 assert, STCPP_PERF_BENCH 触发 noinline hint (§6 Ask 1)
    target_compile_options(${TGT} PRIVATE
        -O2 -DNDEBUG
        -Wno-double-promotion -Wno-sign-conversion -Wno-conversion
        -Wno-shadow -Wno-old-style-cast -Wno-cast-align
    )
    target_compile_definitions(${TGT} PRIVATE
        STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
        STCPP_PERF_BENCH=1
    )
    # CTest label 让 CI 单跑 perf: `ctest -L perf`
    add_test(NAME ${TGT}
             COMMAND ${TGT}
                 --benchmark_min_time=0.5s
                 --benchmark_format=console
                 --benchmark_color=false)
    set_tests_properties(${TGT} PROPERTIES LABELS "perf")
endfunction()

# ----- M1 SlippageModel (复测, 与 tests/bench/bench_slippage_model.cpp 用同公式) -----
stcpp_add_perf_bench(bench_slippage          bench_slippage.cpp)

# ----- M2 RiskGateway (21 reject + approved + 4 ts PIT 不同 sub_reason) -----
stcpp_add_perf_bench(bench_risk_gateway      bench_risk_gateway.cpp)
target_link_libraries(bench_risk_gateway     PRIVATE stcpp_risk)

# ----- M3 AuditEmitter (12 AET + hash chain stub + WalAppend) -----
stcpp_add_perf_bench(bench_audit_emitter     bench_audit_emitter.cpp)
target_link_libraries(bench_audit_emitter    PRIVATE stcpp_observability_audit stcpp_infra_wal)

# ----- M4 PaperSigner (Sign-only, 不跑 2s confirm wait; §6 Ask 3 改异步前临时) -----
stcpp_add_perf_bench(bench_paper_signer      bench_paper_signer.cpp)
target_link_libraries(bench_paper_signer     PRIVATE stcpp_signer_paper stcpp_infra_wal)

# ----- M5 VirtualMatcher (Match: Slippage + Bernoulli draw + R-20 透传) -----
stcpp_add_perf_bench(bench_virtual_matcher   bench_virtual_matcher.cpp)
target_link_libraries(bench_virtual_matcher  PRIVATE stcpp_execution_paper stcpp_infra_wal)

# ----- M6 Goalserve client (URL build + ParseLastUpdateNs + TsMsToNs) -----
stcpp_add_perf_bench(bench_goalserve_parse   bench_goalserve_parse.cpp)
target_link_libraries(bench_goalserve_parse  PRIVATE stcpp_data_goalserve)

message(STATUS "stcpp perf bench: 6 target enabled (M1..M6); M7-M9 留 W5 wave 22")
