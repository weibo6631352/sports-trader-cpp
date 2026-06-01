// tests/perf/bench_onnx_predict.cpp — ONNX fair-value predict 单次延迟 (老姜性能评审 P1)
//
// 三方评审的【最后一块天花板数据】: bench_tick_one 已证非 ONNX 决策路径 per-market ≈380ns (几乎免费),
//   唯一瓶颈是 ONNX predict (占 per-condition 99%, 还跑两遍 D1)。本 bench 测真 ONNX 单次 predict 延迟,
//   据此 + bench_tick_one 才能定真天花板 (N market × tick 频率)。
//
// 模型: models/bench_fair.onnx (100 树 × 110 特征 → 1 输出 LightGBM regressor, 代表生产架构;
//   由 .venv gen_bench_onnx.py 生成)。从 repo 根跑: ./build-bench/tests/perf/bench_onnx_predict
//
// 跑: cd <repo>; ./build-bench/tests/perf/bench_onnx_predict --benchmark_min_time=0.5s
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include "stcpp/ml/fair_value_model.hpp"
#include "stcpp/ml/model_feature_spec.hpp"

using namespace stcpp::ml;

namespace {

void BM_OnnxPredict(benchmark::State& state) {
    OnnxModelConfig cfg;
    cfg.onnx_path = "models/bench_fair.onnx";  // 从 repo 根跑
    cfg.expected_feature_count = kMlFeatureCount;  // 110
    cfg.output_outcome_count = 2;
    cfg.model_id = "bench-fair-100t";
    cfg.intra_op_threads = 1;

    auto model = make_onnx_fair_value_model(cfg);
    if (model == nullptr || !model->ready()) {
        state.SkipWithError("ONNX 模型加载失败 — 确认从 repo 根跑 + models/bench_fair.onnx 存在 + STCPP_ONNX_ENABLED");
        return;
    }

    // 代表性 110 维特征 (非全零, 走真树路径)。
    FeatureVector fv;
    fv.values.resize(kMlFeatureCount);
    for (std::size_t i = 0; i < kMlFeatureCount; ++i) {
        fv.values[i] = static_cast<float>((i % 7) - 3) * 0.3f;
    }

    for (auto _ : state) {
        auto p = model->predict(fv);
        benchmark::DoNotOptimize(p.probs[0]);
    }
    state.SetItemsProcessed(state.iterations());
}
// 单线程 (生产 intra_op_threads=1, 热路径旁路不抢 CPU)。
BENCHMARK(BM_OnnxPredict);

}  // namespace

BENCHMARK_MAIN();
