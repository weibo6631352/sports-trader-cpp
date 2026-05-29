# src/stcpp/pricing/CMakeLists.txt — FairValueEstimator v0.1
#
# Owner: 小肖 (numerical-algorithms)
# 落: ADR-037 配套 自估 fair-value 定价模型 PoC
#
# 依赖:
#   stcpp/data/feature_store_contract.hpp  (FeatureStoreGameRow/BookRow)
#   stcpp/strategy/signal_iface.hpp        (Outcome enum)
#
# 无外部依赖 — header-only 依赖 stcpp_data_contract (已链 goalserve_record.hpp)

add_library(stcpp_pricing STATIC
    fair_value_estimator.cpp
)

target_include_directories(stcpp_pricing
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(stcpp_pricing PUBLIC cxx_std_20)

target_link_libraries(stcpp_pricing
    PUBLIC stcpp_data_contract
    PUBLIC stcpp_strategy
)

# build-time ExecutionMode 注入 (R-7)
target_compile_definitions(stcpp_pricing PUBLIC
    STCPP_EXEC_MODE_${STCPP_EXEC_MODE}=1
)
