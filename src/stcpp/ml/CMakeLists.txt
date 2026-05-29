# src/stcpp/ml/CMakeLists.txt — MLDataHook v0.1 (W4 Wave 20)
#
# Owner: 小邓 (ml-engineer) — ML 数据钩子, paper / live 旁路抓拍
# 落: xiaodeng-ml-data-pipeline-v0.1.md  laowang-wal-framework-cpp-interface-v1.md §8
#
# 红线 R-7: hook v0.1 包含 VirtualFill (paper-only), 因此 ml lib 跟随 paper / backtest 模式编译.
#           W5+ live 模式接 RealFill 后再放开. 当前 live 模式不建 stcpp_ml.

if(STCPP_EXEC_MODE STREQUAL "paper" OR STCPP_EXEC_MODE STREQUAL "backtest")
    add_library(stcpp_ml STATIC
        hook.cpp
    )

    target_include_directories(stcpp_ml
        PUBLIC ${CMAKE_SOURCE_DIR}/include
    )

    # wal_writer.cpp 单 TU 模板定义 — include 路径需要 src 根
    target_include_directories(stcpp_ml
        PRIVATE ${CMAKE_SOURCE_DIR}
    )

    target_compile_features(stcpp_ml PUBLIC cxx_std_20)

    target_compile_definitions(stcpp_ml PUBLIC
        STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
    )

    # 关 hook.cpp 内必要的 cast warning
    # 三方库豁免: nlohmann_json (ADR-010 §3.2 — header-only JSON 宏展开 old-style-cast 警告)
    target_compile_options(stcpp_ml PRIVATE
        -Wno-old-style-cast
    )

    # 链路: WAL framework + risk + execution_paper + strategy
    target_link_libraries(stcpp_ml
        PUBLIC
            stcpp_infra_wal
            stcpp_risk
            stcpp_execution_paper
            stcpp_strategy
    )
endif()

# ---------------------------------------------------------------------------
# stcpp_ml_inference — FairValueModel 推理骨架 (ADR-037, 小邓 #31)
#
# 与 stcpp_ml (hook, R-7 paper-only) 解耦:
#   推理接口 + feature 抽取仅依赖 feature_store_contract.hpp (header-only 中间表示),
#   不含 VirtualFill, 三 mode (live/paper/backtest) 均可编译.
#   ONNXRuntime 当前未链 (W11+ 接); make_onnx_fair_value_model() 返回 nullptr.
# ---------------------------------------------------------------------------
add_library(stcpp_ml_inference STATIC
    fair_value_model.cpp
)

target_include_directories(stcpp_ml_inference
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_ml_inference PUBLIC cxx_std_20)

target_compile_definitions(stcpp_ml_inference PUBLIC
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)
