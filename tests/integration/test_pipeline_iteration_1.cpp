#include <gtest/gtest.h>
#include <ev/battery/cleaning/cleaning.hpp>
#include <ev/battery/segmentation/segmentation.hpp>
#include <ev/battery/denoising/denoising.hpp>
#include "../synthetic/telemetry_generator.hpp"
#include <chrono>

using namespace ev::battery;
namespace synth = ev::battery::testing;

TEST(PipelineIteration1Test, SimToRealFullPipelineEndToEnd) {
    // 1. Synthesize realistic Indian 2W/3W fleet session
    synth::GeneratorConfig cfg;
    cfg.chemistry = "LFP";
    cfg.nominal_capacity_ah = 30.0;
    cfg.series_cells = 16;
    cfg.ambient_temp_c = 42.0;
    cfg.current_offset_a = 0.65;
    cfg.current_gain_error = 1.015;
    cfg.voltage_noise_std_v = 0.05;
    cfg.current_noise_std_a = 0.15;
    cfg.outlier_probability = 0.02; // 2% impulse noise
    cfg.packet_drop_prob = 0.02;    // 2% packet dropout

    synth::TelemetryGenerator gen(cfg);
    auto gt = gen.generateFullSession(42, 0.40);

    ASSERT_FALSE(gt.telemetry.samples.empty());
    const size_t raw_sample_count = gt.telemetry.samples.size();

    auto start_time = std::chrono::steady_clock::now();

    // 2. Stage 1: Data Cleaning (Hampel spike rejection + gap repair)
    auto clean_res = cleaning::cleanTimeSeries(gt.telemetry, 1.0, 10.0, 3, 3.0);
    ASSERT_TRUE(clean_res.has_value());
    const auto& [cleaned_series, clean_report] = *clean_res;

    EXPECT_GT(clean_report.voltage_spikes_removed, 0);
    EXPECT_GT(clean_report.samples_interpolated, 0);
    EXPECT_GE(cleaned_series.samples.size(), raw_sample_count);

    // Verify time grid is strictly monotonic
    for (size_t i = 1; i < cleaned_series.samples.size(); ++i) {
        EXPECT_GT(cleaned_series.samples[i].timestamp_s, cleaned_series.samples[i - 1].timestamp_s);
    }

    // 3. Stage 2: Operational Regime Segmentation
    segmentation::SegmentationConfig seg_cfg;
    seg_cfg.rest_current_thresh_a = 1.0;
    seg_cfg.charge_current_thresh_a = 2.0;
    seg_cfg.discharge_current_thresh_a = -2.0;

    auto segs_res = segmentation::segmentRegimes(cleaned_series, seg_cfg);
    ASSERT_TRUE(segs_res.has_value());
    EXPECT_GE(segs_res->size(), 4); // Expect REST, DISCHARGE, REST, CHARGE, REST

    // Extract Constant Current (CC) charging segment for ICA/DVA
    auto cc_res = segmentation::extractConstantCurrentWindow(cleaned_series, 300.0, 0.5, seg_cfg);
    ASSERT_TRUE(cc_res.has_value());
    EXPECT_EQ(cc_res->type, common::SegmentType::CHARGE);
    EXPECT_GT(cc_res->duration_s, 1500.0);

    // Slice CC window
    auto cc_series = segmentation::sliceSegment(cleaned_series, *cc_res);
    ASSERT_GT(cc_series.samples.size(), 1000);

    // 4. Stage 3: Adaptive Kalman + Savitzky-Golay Denoising
    denoising::DenoisingConfig denoise_cfg;
    denoise_cfg.savgol_window = 21;
    denoise_cfg.savgol_order = 2;

    auto denoise_res = denoising::denoiseTimeSeries(cc_series, denoise_cfg);
    ASSERT_TRUE(denoise_res.has_value());
    const auto& final_denoised = *denoise_res;

    auto end_time = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    // 5. Performance and Accuracy Validation
    EXPECT_LT(elapsed_ms, 150); // Entire pipeline must execute in under 150 ms

    // Denoised voltage must be smoother than raw
    double raw_roughness = 0.0;
    double clean_roughness = 0.0;
    for (size_t i = 1; i < final_denoised.samples.size(); ++i) {
        double d_raw = cc_series.samples[i].voltage_v - cc_series.samples[i - 1].voltage_v;
        double d_clean = final_denoised.samples[i].voltage_v - final_denoised.samples[i - 1].voltage_v;
        raw_roughness += d_raw * d_raw;
        clean_roughness += d_clean * d_clean;
    }

    EXPECT_LT(clean_roughness, 0.20 * raw_roughness); // At least 80% roughness reduction
}
