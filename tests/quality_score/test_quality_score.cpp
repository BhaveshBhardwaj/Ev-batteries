#include <gtest/gtest.h>
#include <ev/battery/quality_score/quality_score.hpp>
#include "../synthetic/telemetry_generator.hpp"

using namespace ev::battery;
namespace synth = ev::battery::testing;

TEST(QualityScoreTest, PristineSessionScoresHighAndPassesGate1) {
    // Generate clean synthetic session with 1 Hz regular sampling
    synth::GeneratorConfig cfg;
    cfg.sampling_interval_s = 1.0;
    cfg.voltage_noise_std_v = 0.01;
    cfg.current_noise_std_a = 0.05;
    cfg.outlier_probability = 0.0;
    cfg.packet_drop_prob = 0.0;

    synth::TelemetryGenerator gen(cfg);
    auto gt = gen.generateFullSession(42, 0.50);

    quality_score::QualityConfig q_cfg;
    auto report = quality_score::computeQualityScore(gt.telemetry, q_cfg);

    EXPECT_TRUE(report.gate1_passed);
    EXPECT_GE(report.total_score, 85.0);
    EXPECT_EQ(report.quality_label, common::QualityLabel::EXCELLENT);
    EXPECT_GE(report.jitter_score, 24.0);
    EXPECT_GE(report.dropout_score, 29.0);
    EXPECT_GE(report.plausibility_score, 24.0);
}

TEST(QualityScoreTest, HeavyDropoutsAndJitterFailsGate1) {
    // Construct series with severe timestamp jitter and massive packet dropouts (> 25% missing)
    common::InternalTimeSeries ts;
    double t = 0.0;
    for (size_t i = 0; i < 200; ++i) {
        if (i % 10 == 0) {
            t += 45.0; // 45-second dropout gap
        } else {
            t += (i % 2 == 0) ? 0.2 : 2.8; // High jitter (CV > 0.8)
        }
        ts.samples.push_back({
            .timestamp_s = t,
            .voltage_v = 51.2,
            .current_a = 15.0,
            .temperature_c = 35.0
        });
    }

    quality_score::QualityConfig q_cfg;
    auto report = quality_score::computeQualityScore(ts, q_cfg);

    EXPECT_FALSE(report.gate1_passed);
    EXPECT_LT(report.total_score, 30.0);
    EXPECT_EQ(report.quality_label, common::QualityLabel::REJECT);
    EXPECT_LT(report.dropout_score, 10.0);
    EXPECT_NE(report.diagnostic_summary.find("GATE 1 REJECT"), std::string::npos);
}

TEST(QualityScoreTest, UnphysicalVoltagePenalizesPlausibility) {
    // Construct series where half the voltage readings are unphysical (< 30 V or > 65 V)
    common::InternalTimeSeries ts;
    for (size_t i = 0; i < 100; ++i) {
        double v = (i % 2 == 0) ? 51.2 : 12.0; // 12V is unphysical for 16S pack
        ts.samples.push_back({
            .timestamp_s = static_cast<double>(i),
            .voltage_v = v,
            .current_a = 10.0,
            .temperature_c = 30.0
        });
    }

    quality_score::QualityConfig q_cfg;
    auto report = quality_score::computeQualityScore(ts, q_cfg);

    // Plausibility score must be halved (~12.5 / 25)
    EXPECT_LT(report.plausibility_score, 15.0);
    EXPECT_LT(report.plausible_fraction, 0.60);
}

TEST(QualityScoreTest, InsufficientSamplesReturnsReject) {
    common::InternalTimeSeries ts;
    ts.samples.push_back({.timestamp_s = 0.0, .voltage_v = 51.2, .current_a = 0.0, .temperature_c = 25.0});

    quality_score::QualityConfig q_cfg;
    auto report = quality_score::computeQualityScore(ts, q_cfg);

    EXPECT_FALSE(report.gate1_passed);
    EXPECT_EQ(report.quality_label, common::QualityLabel::REJECT);
    EXPECT_DOUBLE_EQ(report.total_score, 0.0);
}
