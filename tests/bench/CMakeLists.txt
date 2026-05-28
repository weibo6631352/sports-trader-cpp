# tests/bench/CMakeLists.txt — Google Benchmark perf 回归
# Sprint-2 W3 Wave 18: 小肖 SlippageModel 3 bench

include(FetchContent)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.8.3
)
FetchContent_MakeAvailable(benchmark)

add_executable(bench_slippage_model
    bench_slippage_model.cpp
)
target_link_libraries(bench_slippage_model PRIVATE benchmark::benchmark)
target_include_directories(bench_slippage_model PRIVATE ${CMAKE_SOURCE_DIR}/include)
# 性能 build: -O2 -DNDEBUG (sanitizer 不开)
target_compile_options(bench_slippage_model PRIVATE -O2 -DNDEBUG)
