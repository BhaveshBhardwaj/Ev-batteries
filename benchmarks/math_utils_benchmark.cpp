#include <benchmark/benchmark.h>
#include <ev/battery/math_utils/math_utils.hpp>

#include <random>
#include <vector>

using namespace ev::battery::math_utils;

static void BM_SavgolFilter(benchmark::State& state) {
    const size_t n = static_cast<size_t>(state.range(0));
    std::vector<double> input(n, 3.3);
    std::vector<double> output(n, 0.0);

    for (auto _ : state) {
        auto res = savgolFilter(input, output, 15, 3);
        benchmark::DoNotOptimize(output.data());
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_SavgolFilter)->Range(1000, 100000);

static void BM_CumulativeTrapz(benchmark::State& state) {
    const size_t n = static_cast<size_t>(state.range(0));
    std::vector<double> x(n);
    std::vector<double> y(n);
    std::vector<double> integral(n);

    for (size_t i = 0; i < n; ++i) {
        x[i] = static_cast<double>(i) * 0.1;
        y[i] = 10.0 + static_cast<double>(i % 5);
    }

    for (auto _ : state) {
        auto res = cumulativeTrapz(x, y, integral);
        benchmark::DoNotOptimize(integral.data());
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_CumulativeTrapz)->Range(1000, 100000);

static void BM_Mean(benchmark::State& state) {
    const size_t n = static_cast<size_t>(state.range(0));
    std::vector<double> input(n, 3.14159);

    for (auto _ : state) {
        double m = mean(input);
        benchmark::DoNotOptimize(m);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_Mean)->Range(1000, 100000);

static void BM_MonotoneCubicInterp(benchmark::State& state) {
    const size_t n = 1000;
    std::vector<double> x_data(n);
    std::vector<double> y_data(n);
    for (size_t i = 0; i < n; ++i) {
        x_data[i] = static_cast<double>(i);
        y_data[i] = std::sqrt(static_cast<double>(i));
    }

    const size_t q_len = static_cast<size_t>(state.range(0));
    std::vector<double> x_query(q_len);
    std::vector<double> y_query(q_len);
    for (size_t i = 0; i < q_len; ++i) {
        x_query[i] = static_cast<double>(i) * (static_cast<double>(n) / static_cast<double>(q_len));
    }

    for (auto _ : state) {
        auto res = monotoneCubicInterp(x_data, y_data, x_query, y_query);
        benchmark::DoNotOptimize(y_query.data());
    }
    state.SetItemsProcessed(state.iterations() * q_len);
}
BENCHMARK(BM_MonotoneCubicInterp)->Range(100, 10000);

BENCHMARK_MAIN();
