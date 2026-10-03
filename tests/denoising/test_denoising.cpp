#include <gtest/gtest.h>
#include <ev/battery/denoising/denoising.hpp>
#include "../synthetic/telemetry_generator.hpp"
#include <cmath>
#include <random>

using namespace ev::battery;
using namespace ev::battery::denoising;

TEST(DenoisingTest, EmptyInput_ReturnsError) {
    std::vector<double> empty_v;
    std::vector<double> out_v;
    auto res = denoiseVoltage(empty_v, empty_v, out_v);
    EXPECT_FALSE(res.has_value());
}

TEST(DenoisingTest, SizeMismatch_ReturnsError) {
    std::vector<double> v = {3.0, 3.1};
    std::vector<double> i = {10.0};
    std::vector<double> out(2);
    auto res = denoiseVoltage(v, i, out);
    EXPECT_FALSE(res.has_value());
}

TEST(DenoisingTest, AdaptiveKalman_ConstantSignal_RejectsNoise) {
    const size_t n = 200;
    const double true_v = 3.30;
    std::vector<double> raw_v(n);
    std::vector<double> curr_i(n, 10.0);
    std::vector<double> filtered_v(n);

    std::mt19937 rng(42);
    std::normal_distribution<double> noise(0.0, 0.05);

    for (size_t k = 0; k < n; ++k) {
        raw_v[k] = true_v + noise(rng);
    }

    DenoisingConfig cfg;
    cfg.process_noise_q = 1e-5;
    cfg.measurement_noise_r0 = 2.5e-3; // (0.05)^2

    auto res = adaptiveKalmanFilter(raw_v, curr_i, filtered_v, cfg);
    ASSERT_TRUE(res.has_value());

    // Calculate variance of raw vs filtered for the steady-state portion (k > 50)
    double raw_var = 0.0;
    double filt_var = 0.0;
    for (size_t k = 50; k < n; ++k) {
        raw_var += (raw_v[k] - true_v) * (raw_v[k] - true_v);
        filt_var += (filtered_v[k] - true_v) * (filtered_v[k] - true_v);
    }

    EXPECT_LT(filt_var, 0.25 * raw_var);
}

TEST(DenoisingTest, CascadedDenoising_SnrImprovementTarget) {
    ev::battery::testing::GeneratorConfig cfg;
    cfg.voltage_noise_std_v = 0.06;
    cfg.outlier_probability = 0.0; // test pure Gaussian noise reduction
    ev::battery::testing::TelemetryGenerator gen(cfg);

    auto gt = gen.generateChargeSegment(15.0, 0.20, 0.70, true, 999);
    ASSERT_GT(gt.telemetry.samples.size(), 200);

    const size_t n = gt.telemetry.samples.size();
    std::vector<double> raw_v(n);
    std::vector<double> curr_i(n);
    std::vector<double> clean_v(n);

    for (size_t k = 0; k < n; ++k) {
        raw_v[k] = gt.telemetry.samples[k].voltage_v;
        curr_i[k] = gt.telemetry.samples[k].current_a;
    }

    DenoisingConfig denoise_cfg;
    denoise_cfg.savgol_window = 21;
    denoise_cfg.savgol_order = 2;

    auto res = denoiseVoltage(raw_v, curr_i, clean_v, denoise_cfg);
    ASSERT_TRUE(res.has_value());

    const double raw_snr = calculateSnrDb(gt.true_voltage_v, raw_v);
    const double filtered_snr = calculateSnrDb(gt.true_voltage_v, clean_v);

    // Expect at least 10 dB SNR improvement (>90% noise energy reduction)
    EXPECT_GT(filtered_snr, raw_snr + 10.0);

    // Check mean absolute error (MAE) across steady-state portion
    double mae = 0.0;
    size_t count = 0;
    for (size_t k = 50; k < n - 50; ++k) {
        mae += std::abs(clean_v[k] - gt.true_voltage_v[k]);
        ++count;
    }
    mae /= static_cast<double>(count);
    EXPECT_LT(mae, 0.025); // Mean error across pack < 25 mV (1.5 mV / cell)
}
