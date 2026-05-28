# src/stcpp/infra/process/CMakeLists.txt — SingleInstanceLock static lib (W5 Wave 25)
#
# 落:
#   laozhou-single-instance-spec-v1.md §3 (集成点 2: CMakeLists)
#   ADR R-7: PID file path build-time define (STCPP_PID_DIR, default /tmp/stcpp)
#   ADR R-11: paper.pid ≠ live.pid 物理隔离
#
# 依赖: 仅 POSIX (flock / unistd / signal) + stcpp execution (ExecutionMode enum)
# M5+ 切生产路径: -DSTCPP_PID_DIR=/var/run/stcpp (仅重编, 不改代码)

add_library(stcpp_process_lock STATIC
    single_instance.cpp
)

target_include_directories(stcpp_process_lock
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_process_lock PUBLIC cxx_std_20)

# R-7: build-time PID dir inject.  默认 /tmp/stcpp (dev + CI 友好).
# 覆盖: cmake -DSTCPP_PID_DIR=/var/run/stcpp (M5+ 生产 Linux)
if(NOT DEFINED STCPP_PID_DIR)
    set(STCPP_PID_DIR "/tmp/stcpp")
endif()

# git commit hash 注入 (诊断用; CI 由 cmake -DSTCPP_BUILD_COMMIT=$(git rev-parse HEAD) 传入)
if(NOT DEFINED STCPP_BUILD_COMMIT)
    set(STCPP_BUILD_COMMIT "unknown")
endif()

# STCPP_PID_DIR と STCPP_BUILD_COMMIT は PRIVATE:
# - .cpp はこのマクロで実際パスを使う
# - .hpp は #ifndef ガードで再定義しない (下流 target は自前 define 可能)
# - テスト target は PRIVATE STCPP_PID_DIR="/tmp/stcpp_test" を追加可能
target_compile_definitions(stcpp_process_lock
    PRIVATE
        STCPP_PID_DIR="${STCPP_PID_DIR}"
        STCPP_BUILD_COMMIT="${STCPP_BUILD_COMMIT}"
)

# 无外部库依赖 (纯 POSIX)

# ---------------------------------------------------------------------------
# stcpp_process_lock_test — 测试专用 variant (W6 Wave 30 小卢)
#   STCPP_TEST_BUILD=1: 允许 STCPP_TEST_PID_DIR 环境变量 runtime override,
#   让并发 ctest 进程获得 per-process PID dir 隔离, 不影响 R-7 (生产代码无此 macro).
# ---------------------------------------------------------------------------
add_library(stcpp_process_lock_test STATIC
    single_instance.cpp
)

target_include_directories(stcpp_process_lock_test
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_process_lock_test PUBLIC cxx_std_20)

target_compile_definitions(stcpp_process_lock_test
    PRIVATE
        STCPP_PID_DIR="/tmp/stcpp_test"
        STCPP_BUILD_COMMIT="test-build"
        STCPP_TEST_BUILD=1
)
