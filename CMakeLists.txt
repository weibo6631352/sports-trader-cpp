cmake_minimum_required(VERSION 3.27)
project(stcpp
    VERSION 0.1.0
    DESCRIPTION "Polymarket sports market quant trading system"
    LANGUAGES CXX C
)
# C language enable: BLAKE3 官方实现是 C (老唐 W6 Wave 29 FetchContent), 必须 enable C

# C++ 版本锁 (ADR-001 W-1 + 老何 v1 选型)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ExecutionMode build-time switch (ADR gm-signoff-paper-trade R-7 + R-11)
# 三 mode 同进程不允许动态切换 — build-time 二选一
# (backtest 模式 2026-06-12 老板裁决删: 模型假设源自同批历史数据, 回测=in-sample 假象;
#  验证 = 脚本验数据 + paper walk-forward [生产二进制跑实时数据, 天然 out-of-sample]。)
set(STCPP_EXEC_MODE "paper" CACHE STRING "Execution mode: live / paper")
set_property(CACHE STCPP_EXEC_MODE PROPERTY STRINGS live paper)

if(NOT STCPP_EXEC_MODE MATCHES "^(live|paper)$")
    message(FATAL_ERROR "STCPP_EXEC_MODE must be one of: live / paper (got: ${STCPP_EXEC_MODE})")
endif()

message(STATUS "stcpp build: STCPP_EXEC_MODE = ${STCPP_EXEC_MODE}")

# 编译选项 (老高 v1 §8 + 老何 footgun)
# Wave 75 fix: -Wnon-virtual-dtor / -Wold-style-cast / -Wcast-align 仅 C++ 有效
# 用 generator expression 限制到 C++ 编译单元, 避免 BLAKE3 C 源文件报错 (Linux GCC cc1 -Werror)
add_compile_options(
    -Wall -Wextra -Werror
    $<$<COMPILE_LANGUAGE:CXX>:-Wshadow>
    $<$<COMPILE_LANGUAGE:CXX>:-Wnon-virtual-dtor>
    $<$<COMPILE_LANGUAGE:CXX>:-Wcast-align>
    $<$<COMPILE_LANGUAGE:CXX>:-Wold-style-cast>
    $<$<COMPILE_LANGUAGE:CXX>:-Wconversion>
    $<$<COMPILE_LANGUAGE:CXX>:-Wsign-conversion>
    $<$<COMPILE_LANGUAGE:CXX>:-Wnull-dereference>
    $<$<COMPILE_LANGUAGE:CXX>:-Wdouble-promotion>
    $<$<COMPILE_LANGUAGE:CXX>:-Wformat=2>
    -fno-omit-frame-pointer
)

# Sanitizers (开发期, 不进 release)
option(STCPP_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(STCPP_ENABLE_UBSAN "Enable UBSan" OFF)
option(STCPP_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

if(STCPP_ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-sanitize-recover=address)
    add_link_options(-fsanitize=address)
endif()
if(STCPP_ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined -fno-sanitize-recover=undefined)
    add_link_options(-fsanitize=undefined)
endif()
if(STCPP_ENABLE_TSAN AND NOT STCPP_ENABLE_ASAN)
    add_compile_options(-fsanitize=thread)
    add_link_options(-fsanitize=thread)
endif()

# 头文件路径
include_directories(${CMAKE_SOURCE_DIR}/include)

# 测试 (Sprint-2 W3 Wave 18 启用)
option(STCPP_BUILD_BENCH "Build Google Benchmark perf 回归" OFF)

include(CTest)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
    # W4+: replay / chaos / fuzz / perf 逐步加
endif()

if(STCPP_BUILD_BENCH)
    add_subdirectory(tests/bench)
    # 老姜 W4 Wave 21: perf 回归 framework (6 模块 + JSON + CI gate)
    add_subdirectory(tests/perf)
endif()

# 子模块 (Sprint-2 W3 Wave 18: WAL framework skeleton 起步)
add_subdirectory(src/stcpp/infra/wal)
# (infra/spsc 2026-06-12 治理删: 唯一消费者 net_outbound 已删, 生产零引用。)
# Sprint-2 W5 Wave 25: 小卢 SingleInstanceLock (防多开 R-7/R-11/R-12)
add_subdirectory(src/stcpp/infra/process)
# Sprint-2 W4 Wave 19: 老韩 RiskGateway v0.1 (paper mode 走通)
add_subdirectory(src/stcpp/risk)
# Sprint-2 W4 Wave 19: 老唐 AuditEmitter v0.1 (接老王 WAL framework)
add_subdirectory(src/stcpp/observability)
# Sprint-2 W4 Wave 19: 小段 Goalserve client adapter v0.1 (stub)
add_subdirectory(src/stcpp/data)
# crypto: eip712_v2 + secp256k1 (live 签名) + blake3 (WAL)。
#   (ed25519+libsodium / signer v52/v62 / bin 三签 CLI 2026-06-12 治理删: 全是被
#    live_order_submitter 直签路径超越的旧架构层 + 已废会签机器, 零生产消费者。)
add_subdirectory(src/stcpp/crypto)
# Sprint-2 W4 Wave 19: 小蒋 paper engine v0.1 (signer + virtual matcher + paper main)
add_subdirectory(src/stcpp/signer)
add_subdirectory(src/stcpp/execution)
# Sprint-2 W4 Wave 19: P0-01 Pinnacle no-vig + LiveSection (小程 spec + 小卢 IC 实施)
add_subdirectory(src/stcpp/strategy)
# Sprint-2 W4 Wave 20: 小董 M4.5 7 hard gate statistical framework v1
add_subdirectory(src/stcpp/stats)
# Sprint-2 W4 Wave 20: 小袁 microstructure C++ lib v0.1 (FillRateModel + 8 sport profile)
add_subdirectory(src/stcpp/microstructure)
# (src/stcpp/ml [MLDataHook 训练数据捕获] 已砍 2026-06-05「砍掉大模型训练功能」;
#  feature_snapshot/training_label header 留 include/stcpp/ml/ — feature_snapshot_id 是 live 信号 ABI)
# Sprint-2 W5 Wave 24: 老李 PolymarketClient v0.1 (paper mock + live stub, R-7 物理隔离)
add_subdirectory(src/stcpp/polymarket)

# 小肖 ADR-037 配套: FairValueEstimator v0.1 — 自估 fair-value 定价模型
# 消费 feature_store_contract.hpp FeatureStoreGameRow/BookRow
# 接口留给小邓 ML 模型替换 (IFairValueModel)
add_subdirectory(src/stcpp/pricing)

# C-SIZE-01 (小袁 v0.1, ADR-042): Kelly SizingCalculator — edge_ci_lower 净 edge + 5 级 cap 链
# R-1 绝不绕 RM (suggested_notional 仅建议); cap 注入 RiskConfig const& 禁字面量 (老高 grep 守护)
add_subdirectory(src/stcpp/sizing)

# (src/stcpp/net [A-NET-01 出站序列化+背压队列] 2026-06-12 治理删: Wave3 旧架构层,
#  被 PersistentHttps/live_order_submitter 直连路径超越, 仅自测试消费。)

# Sprint-3 W9 W2: Debug REST API server (老周 spec, 小卢 IC 实施)
# ADR-018 (cpp-httplib v0.16.3) + ADR-015 (vCPU 分工) + ADR-011 (paper/live 隔离)
# 默认 ON — paper/live/backtest 三 mode 均可启用
# production hardened 部署可 cmake -DSTCPP_BUILD_DEBUG_API=OFF 关闭 (无 auth 的 debug 接口)
option(STCPP_BUILD_DEBUG_API "Build debug REST API server (cpp-httplib, read-only debug)" ON)
if(STCPP_BUILD_DEBUG_API)
    add_subdirectory(src/stcpp/debug_api)
endif()

# (src/stcpp/backtest 2026-06-12 删, 理由见顶部 STCPP_EXEC_MODE 注)
# BR-1: 复用 stcpp_microstructure (FillRateModel::compute_from_clob_book)
# BR-5: 复用小肖 slippage model v1 (header-only, 无额外 link)

# feat/xiaoxiao-paper-loop (小肖 paper runtime integration, 2026-05-30)
# 最小 paper 交易循环: OrderBookSnapshotHub → FairValue → Sizing → RM → PaperSigner
#   → VirtualMatcher → PositionLedger → LedgerSnapshotHub + QuoteSnapshotHub
# R-11: paper 不污染真账本; R-12: 独立线程; R-20: 4 ts 全链路透传
add_subdirectory(src/stcpp/paper)

# PaperDaemon 编排层 (老郭 §A.1 / 老周架构裁定 B1)
# stcpp_paper_app (PaperDaemon + market_discovery) + stcpp_paper_server(http) + stcpp_paper_runtime(headless)
# 必须在 debug_api + paper 之后 (app → debug_api/paper 单向依赖).
if(STCPP_BUILD_DEBUG_API)
    add_subdirectory(src/stcpp/app)
endif()

message(STATUS "stcpp project scaffold (Sprint-3 W9 W2). paper engine v0.1 + 7-gate evaluator v1 + microstructure v0.1 + ml data hook v0.1 + polymarket client v0.1 + single-instance-lock v0.1 + debug-api v0.1 + fair-value-estimator v0.1.")
