# tests/perf/CMakeLists.txt — Sprint-2 W4 Wave 21 (老姜) + W6 Wave 28 v1.2
#
# Google Benchmark 性能回归 framework. 与 tests/bench/ (W3 小肖 slippage) 共存:
#   - tests/bench/   : 小肖 W3 slippage 旧版 (保持不动, 通过)
#   - tests/perf/    : 老姜 W4 Wave 21 (6 模块) + W6 Wave 28 (e2e_latency + spsc_latency)
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

# 三方库豁免: google-benchmark 源码 — ADR-010 §3.2
# 注: 顶层 -Werror -Wformat=2 -Wformat-nonliteral 会卡 benchmark/src/colorprint.cc vsnprintf.
foreach(_bm_target benchmark benchmark_main)
    if(TARGET ${_bm_target})
        target_compile_options(${_bm_target} PRIVATE
            -Wno-double-promotion   # 三方库豁免: google-benchmark
            -Wno-old-style-cast     # 三方库豁免: google-benchmark
            -Wno-cast-align         # 三方库豁免: google-benchmark
            -Wno-format-nonliteral  # 三方库豁免: google-benchmark (colorprint.cc vsnprintf)
            -Wno-error              # 三方库豁免: google-benchmark (第三方不受 -Werror 约束)
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
    # ADR-010 §4: 删 -Wno-sign-conversion / -Wno-shadow / -Wno-conversion; 保留 3 项 grandfather
    target_compile_options(${TGT} PRIVATE
        -O2 -DNDEBUG
        -Wno-double-promotion   # ADR-010 grandfather: benchmark 浮点宏
        -Wno-old-style-cast     # ADR-010 grandfather: benchmark 宏展开
        -Wno-cast-align         # ADR-010 grandfather: benchmark mock 转型
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

# ----- W6 Wave 28: E2E latency bench (ADR-015 配套 + Wave 26 决议 2) -----
# 链路: WSS recv mock → RM → PaperSigner → VirtualMatcher → Position
# guard: lower p99 >= 100 ns / upper p99 <= 50 ms
stcpp_add_perf_bench(bench_e2e_latency       bench_e2e_latency.cpp)
target_link_libraries(bench_e2e_latency      PRIVATE
    stcpp_risk
    stcpp_signer_paper
    stcpp_execution_paper
    stcpp_infra_wal)

# ----- W6 Wave 28: SPSC queue latency bench (小石 W6 SPSC framework 联调) -----
# 5 capacity: 64/256/1024/4096/16384; upper guard p99 < 100 ns
# 注: 小石 W6 SPSC 就位后替换 MockSPSC → stcpp::infra::spsc::SPSCQueue<T,N>
stcpp_add_perf_bench(bench_spsc_latency      bench_spsc_latency.cpp)
# bench_spsc_latency: 仅依赖 stcpp_infra_wal (pit::NowRealtimeNs); 无额外 lib
target_link_libraries(bench_spsc_latency     PRIVATE stcpp_infra_wal)

message(STATUS "stcpp perf bench: 8 target enabled (M1..M6 + e2e_latency + spsc_latency)")
