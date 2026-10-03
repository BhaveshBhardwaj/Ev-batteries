#include <gtest/gtest.h>
#include <ev/battery/gpr_completion/gpr_completion.hpp>
#include <ev/battery/ica_dva/ica_dva.hpp>
#include "../synthetic/telemetry_generator.hpp"

using namespace ev::battery;
namespace synth = ev::battery::testing;

TEST(GprCompletionTest, FullChargeReconstructionAccuracy) {
    // Generate clean synthetic charge segment (10% to 95% SoC)
    synth::GeneratorConfig cfg;
    synth::TelemetryGenerator gen(cfg);
    auto gt = gen.generateChargeSegment(15.0, 0.10, 0.95, false, 42);

    gpr_completion::GprConfig gpr_cfg;
    auto res = gpr_completion::reconstructChargeCurve(gt.telemetry, gpr_cfg);
    ASSERT_TRUE(res.has_value());

    const auto& gpr = *res;
    EXPECT_TRUE(gpr.is_reconstructed);
    EXPECT_FALSE(gpr.voltage_grid_v.empty());
    EXPECT_EQ(gpr.voltage_grid_v.size(), gpr.mean_charge_ah.size());

    // Monotonicity of charge trajectory: charge must generally increase with voltage
    for (size_t i = 1; i < gpr.mean_charge_ah.size(); ++i) {
        EXPECT_GE(gpr.mean_charge_ah[i], gpr.mean_charge_ah[i - 1] - 0.01);
    }
}

TEST(GprCompletionTest, PartialChargeUncertaintyExpansionInExtrapolationWings) {
    // Generate partial charge segment (only middle 40% to 70% SoC)
    synth::GeneratorConfig cfg;
    synth::TelemetryGenerator gen(cfg);
    auto gt = gen.generateChargeSegment(15.0, 0.40, 0.70, true, 42);

    gpr_completion::GprConfig gpr_cfg;
    auto res = gpr_completion::reconstructChargeCurve(gt.telemetry, gpr_cfg);
    ASSERT_TRUE(res.has_value());

    const auto& gpr = *res;
    EXPECT_GT(gpr.voltage_grid_v.size(), 20);

    // Observed voltages are around the middle of the grid.
    // In unobserved extrapolation regions (e.g. index 0 vs index N/2), variance must expand.
    size_t mid_idx = gpr.variance_ah2.size() / 2;
    double var_observed = gpr.variance_ah2[mid_idx];
    double var_extrapolated = gpr.variance_ah2.front();

    EXPECT_GT(var_extrapolated, var_observed);
    EXPECT_GT(gpr.max_uncertainty_ah, 0.01);
}

TEST(GprCompletionTest, SynthesizedSeriesDownstreamIcaCompatibility) {
    synth::GeneratorConfig cfg;
    synth::TelemetryGenerator gen(cfg);
    auto gt = gen.generateChargeSegment(15.0, 0.20, 0.85, false, 42);

    gpr_completion::GprConfig gpr_cfg;
    auto gpr_res = gpr_completion::reconstructChargeCurve(gt.telemetry, gpr_cfg);
    ASSERT_TRUE(gpr_res.has_value());

    // Synthesize completed series for ICA
    auto synth_series = gpr_completion::synthesizeCompletedTimeSeries(*gpr_res, 15.0, 30.0);
    EXPECT_EQ(synth_series.samples.size(), gpr_res->voltage_grid_v.size());

    // Run ICA transformation on synthesized series
    ica_dva::IcaConfig ica_cfg;
    ica_cfg.voltage_step_v = 0.05; // Pack-scale grid
    auto ica_res = ica_dva::computeIca(synth_series, ica_cfg);
    ASSERT_TRUE(ica_res.has_value());
    EXPECT_GT(ica_res->max_dq_dv, 0.0);
}

TEST(GprCompletionTest, DegenerateAndEmptyInputHandling) {
    common::InternalTimeSeries empty_series;
    auto res1 = gpr_completion::reconstructChargeCurve(empty_series);
    EXPECT_FALSE(res1.has_value());
    EXPECT_EQ(res1.error().code, "EMPTY_INPUT");

    // Series with identical voltage (zero variation)
    common::InternalTimeSeries flat_series;
    flat_series.samples.push_back({.timestamp_s = 0.0, .voltage_v = 51.2, .current_a = 15.0, .temperature_c = 25.0});
    flat_series.samples.push_back({.timestamp_s = 1.0, .voltage_v = 51.2, .current_a = 15.0, .temperature_c = 25.0});
    auto res2 = gpr_completion::reconstructChargeCurve(flat_series);
    EXPECT_FALSE(res2.has_value());
    EXPECT_EQ(res2.error().code, "INSUFFICIENT_DISTINCT_VOLTAGES");
}
