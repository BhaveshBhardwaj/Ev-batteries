#include <gtest/gtest.h>
#include "telemetry_generator.hpp"

using namespace ev::battery;
using namespace ev::battery::testing;

TEST(TelemetryGeneratorTest, OcvMonotonicityAndRanges) {
    GeneratorConfig cfg;
    cfg.chemistry = "LFP";
    cfg.series_cells = 16;
    TelemetryGenerator gen(cfg);

    // Single-cell OCV should be around 2.5V - 3.65V for LFP
    double v_low = gen.cellOcv(0.05);
    double v_mid = gen.cellOcv(0.50);
    double v_high = gen.cellOcv(0.95);

    EXPECT_GT(v_low, 2.5);
    EXPECT_LT(v_low, 3.2);
    EXPECT_NEAR(v_mid, 3.3, 0.1); // LFP 3.3V plateau
    EXPECT_GT(v_high, 3.35);
    EXPECT_LT(v_high, 3.65);
    EXPECT_GT(v_mid, v_low);
    EXPECT_GT(v_high, v_mid);
}

TEST(TelemetryGeneratorTest, PristineChargeSegment) {
    GeneratorConfig cfg;
    cfg.chemistry = "LFP";
    cfg.nominal_capacity_ah = 30.0;
    cfg.series_cells = 16;
    TelemetryGenerator gen(cfg);

    // Pristine generation (add_noise = false)
    auto session = gen.generateChargeSegment(15.0, 0.20, 0.80, false, 123);

    ASSERT_FALSE(session.telemetry.samples.empty());
    EXPECT_EQ(session.telemetry.samples.size(), session.true_voltage_v.size());
    EXPECT_EQ(session.telemetry.samples.size(), session.true_current_a.size());

    // Without noise, true current equals measured current
    for (size_t i = 0; i < session.telemetry.samples.size(); ++i) {
        EXPECT_DOUBLE_EQ(session.telemetry.samples[i].current_a, 15.0);
        EXPECT_DOUBLE_EQ(session.telemetry.samples[i].voltage_v, session.true_voltage_v[i]);
        EXPECT_FALSE(session.is_outlier[i]);
    }
}

TEST(TelemetryGeneratorTest, FullRealisticSessionSynthesis) {
    GeneratorConfig cfg;
    cfg.chemistry = "LFP";
    cfg.outlier_probability = 0.05;
    cfg.packet_drop_prob = 0.05;
    TelemetryGenerator gen(cfg);

    auto session = gen.generateFullSession(42, 0.50);

    // Total duration should be 300 + 1200 + 400 + 1800 + 300 = 4000 steps
    EXPECT_EQ(session.true_voltage_v.size(), 4000);
    EXPECT_EQ(session.true_current_a.size(), 4000);
    EXPECT_EQ(session.true_soc.size(), 4000);

    // Due to packet drops, telemetry samples should be slightly fewer than 4000
    EXPECT_LT(session.telemetry.samples.size(), 4000);
    EXPECT_GT(session.telemetry.samples.size(), 3500);

    // Ground truth outliers exist
    bool has_outliers = false;
    for (bool spk : session.is_outlier) {
        if (spk) {
            has_outliers = true;
            break;
        }
    }
    EXPECT_TRUE(has_outliers);
}
