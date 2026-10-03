#include <gtest/gtest.h>
#include <ev/battery/calibration/calibration.hpp>
#include <ev/battery/segmentation/segmentation.hpp>
#include "../synthetic/telemetry_generator.hpp"

#include <cmath>
#include <numeric>
#include <vector>

using namespace ev::battery;
using namespace ev::battery::calibration;
namespace synth = ev::battery::testing;

// ─── Test 1: Physics Simulation & Synthetic Ground-Truth Recovery ────────────

TEST(CalibrationTest, PhysicsSimulation_FullSessionParameterRecovery) {
    // Grounded in realistic 16S LFP 48V pack electro-thermal equivalent circuit
    synth::GeneratorConfig gen_cfg;
    gen_cfg.chemistry = "LFP";
    gen_cfg.nominal_capacity_ah = 30.0;
    gen_cfg.series_cells = 16;
    gen_cfg.sampling_interval_s = 1.0;
    gen_cfg.ambient_temp_c = 42.0;

    // Injected hardware imperfections: 0.65A shunt bias, 1.5% gain scaling, noise
    const double true_alpha = 0.65;
    const double true_beta = 1.015;
    gen_cfg.current_offset_a = true_alpha;
    gen_cfg.current_gain_error = true_beta;
    gen_cfg.current_noise_std_a = 0.15;
    gen_cfg.voltage_noise_std_v = 0.03;
    gen_cfg.outlier_probability = 0.0;
    gen_cfg.packet_drop_prob = 0.0;

    synth::TelemetryGenerator generator(gen_cfg);
    auto session = generator.generateFullSession(12345, 0.40);
    ASSERT_GT(session.telemetry.samples.size(), 3000);

    // Build operational segments corresponding to the physics session stages
    // Stage 0: Rest 300s (0..299)
    // Stage 1: Discharge 1200s (300..1499)
    // Stage 2: Rest 400s (1500..1899)
    // Stage 3: Charge 1800s (1900..3699)
    // Stage 4: Rest 300s (3700..3999)
    std::vector<segmentation::Segment> segs = {
        segmentation::Segment{
            .type = common::SegmentType::REST,
            .start_index = 0,
            .end_index = 299,
            .start_timestamp_s = session.telemetry.samples[0].timestamp_s,
            .end_timestamp_s = session.telemetry.samples[299].timestamp_s,
            .duration_s = 300.0,
            .mean_current_a = true_alpha
        },
        segmentation::Segment{
            .type = common::SegmentType::DISCHARGE,
            .start_index = 300,
            .end_index = 1499,
            .start_timestamp_s = session.telemetry.samples[300].timestamp_s,
            .end_timestamp_s = session.telemetry.samples[1499].timestamp_s,
            .duration_s = 1200.0,
            .mean_current_a = -12.0
        },
        segmentation::Segment{
            .type = common::SegmentType::REST,
            .start_index = 1500,
            .end_index = 1899,
            .start_timestamp_s = session.telemetry.samples[1500].timestamp_s,
            .end_timestamp_s = session.telemetry.samples[1899].timestamp_s,
            .duration_s = 400.0,
            .mean_current_a = true_alpha
        },
        segmentation::Segment{
            .type = common::SegmentType::CHARGE,
            .start_index = 1900,
            .end_index = 3699,
            .start_timestamp_s = session.telemetry.samples[1900].timestamp_s,
            .end_timestamp_s = session.telemetry.samples[3699].timestamp_s,
            .duration_s = 1800.0,
            .mean_current_a = 15.0
        },
        segmentation::Segment{
            .type = common::SegmentType::REST,
            .start_index = 3700,
            .end_index = 3999,
            .start_timestamp_s = session.telemetry.samples[3700].timestamp_s,
            .end_timestamp_s = session.telemetry.samples[3999].timestamp_s,
            .duration_s = 300.0,
            .mean_current_a = true_alpha
        }
    };

    CalibrationConfig config;
    config.nominal_capacity_ah = 30.0;

    auto result = calibrateTimeSeries(session.telemetry, segs, config);
    ASSERT_TRUE(result.has_value());

    const auto& [cal_series, report] = *result;

    // FR-5.01: Offset recovered within 0.05A
    EXPECT_NEAR(report.estimated_offset_a, true_alpha, 0.05);

    // FR-5.02 & FR-5.03: Dual anchors found and gain recovered within 0.015
    EXPECT_TRUE(report.dual_anchors_found);
    EXPECT_NEAR(report.estimated_gain, true_beta, 0.015);

    // Gate 2: Sensor is within normal limits
    EXPECT_FALSE(report.sensor_fault_detected);

    // Compute ground-truth charge integral
    double q_true = 0.0;
    for (size_t i = 1; i < session.true_current_a.size(); ++i) {
        double dt_h = (session.telemetry.samples[i].timestamp_s - session.telemetry.samples[i - 1].timestamp_s) / 3600.0;
        q_true += 0.5 * (session.true_current_a[i - 1] + session.true_current_a[i]) * dt_h;
    }

    double raw_err = std::abs(report.raw_coulomb_ah - q_true);
    double cal_err = std::abs(report.calibrated_coulomb_ah - q_true);

    // Raw coulomb error is substantial due to 4000s * 0.65A drift (~0.72 Ah)
    EXPECT_GT(raw_err, 0.50);

    // Acceptance Criterion 2: Calibration eliminates >= 90% of cumulative integration drift
    EXPECT_LT(cal_err, 0.10 * raw_err);
}

// ─── Test 2: Auto-Detection of Rest Intervals without Pre-Segmentation ────────

TEST(CalibrationTest, AutoRestDetection_RecoversOffsetAndGain) {
    synth::GeneratorConfig gen_cfg;
    const double true_alpha = 0.50;
    const double true_beta = 1.020;
    gen_cfg.current_offset_a = true_alpha;
    gen_cfg.current_gain_error = true_beta;
    gen_cfg.current_noise_std_a = 0.10;
    gen_cfg.voltage_noise_std_v = 0.02;
    gen_cfg.outlier_probability = 0.0;
    gen_cfg.packet_drop_prob = 0.0;

    synth::TelemetryGenerator generator(gen_cfg);
    auto session = generator.generateFullSession(42, 0.35);

    CalibrationConfig config;
    config.nominal_capacity_ah = 30.0;

    // Passing empty segments triggers autonomous rest scan
    auto result = calibrateTimeSeries(session.telemetry, {}, config);
    ASSERT_TRUE(result.has_value());

    const auto& [cal_series, report] = *result;
    EXPECT_NEAR(report.estimated_offset_a, true_alpha, 0.05);
    EXPECT_TRUE(report.dual_anchors_found);
    EXPECT_NEAR(report.estimated_gain, true_beta, 0.02);
}

// ─── Test 3: Idle / Long Parking Drift Elimination ───────────────────────────

TEST(CalibrationTest, IdleParking_EliminatesPhantomEnergyAccumulation) {
    // 2 hours of commercial depot parking (7200 seconds)
    // Sensor has +0.80 A zero-offset drift
    common::InternalTimeSeries ts;
    ts.session_id = "depot_parking_2hr";
    ts.nominal_capacity_ah = 30.0;
    const double true_alpha = 0.80;
    const size_t n_samples = 7200;
    ts.samples.reserve(n_samples);

    for (size_t i = 0; i < n_samples; ++i) {
        ts.samples.push_back(common::Sample{
            .timestamp_s = static_cast<double>(i),
            .voltage_v = 52.8,
            .current_a = true_alpha, // Zero true current + offset
            .temperature_c = 35.0,
            .cell_voltages_v = std::nullopt
        });
    }

    auto result = calibrateTimeSeries(ts);
    ASSERT_TRUE(result.has_value());

    const auto& [cal_series, report] = *result;

    // Offset exactly recovered
    EXPECT_NEAR(report.estimated_offset_a, true_alpha, 1e-4);

    // Raw coulomb counting accumulated 0.80A * 2h = 1.60 Ah phantom energy!
    EXPECT_NEAR(report.raw_coulomb_ah, 1.60, 0.05);

    // Calibrated coulomb counting is virtually 0.0 Ah (>99% drift reduction)
    EXPECT_NEAR(report.calibrated_coulomb_ah, 0.0, 0.01);
}

// ─── Test 4: Single Rest Segment Fallback (Offset-Only) ───────────────────────

TEST(CalibrationTest, SingleRestSegment_FallsBackToGainOne) {
    common::InternalTimeSeries ts;
    ts.session_id = "single_rest_drive";
    ts.nominal_capacity_ah = 30.0;

    // Rest 60s at 0.40A offset
    double t = 0.0;
    for (int i = 0; i < 60; ++i, t += 1.0) {
        ts.samples.push_back(common::Sample{
            .timestamp_s = t,
            .voltage_v = 51.5,
            .current_a = 0.40,
            .temperature_c = 28.0
        });
    }
    // Drive 120s at -15.0A
    for (int i = 0; i < 120; ++i, t += 1.0) {
        ts.samples.push_back(common::Sample{
            .timestamp_s = t,
            .voltage_v = 48.0,
            .current_a = -15.0,
            .temperature_c = 30.0
        });
    }

    auto result = calibrateTimeSeries(ts);
    ASSERT_TRUE(result.has_value());

    const auto& [cal_series, report] = *result;
    EXPECT_NEAR(report.estimated_offset_a, 0.40, 1e-3);
    EXPECT_FALSE(report.dual_anchors_found); // Only 1 rest anchor
    EXPECT_DOUBLE_EQ(report.estimated_gain, 1.0); // Fallback to 1.0
    EXPECT_FALSE(report.sensor_fault_detected);
}

// ─── Test 5: Gate 2 Diagnostics — Hardware Fault Flagging ────────────────────

TEST(CalibrationTest, Gate2Faults_FlaggedCorrectly) {
    // Test 5A: Excessive Offset (> 1.5A)
    {
        common::InternalTimeSeries ts;
        ts.nominal_capacity_ah = 30.0;
        for (int i = 0; i < 100; ++i) {
            ts.samples.push_back(common::Sample{
                .timestamp_s = static_cast<double>(i),
                .voltage_v = 52.0,
                .current_a = 2.20, // Severe 2.2A offset fault
                .temperature_c = 30.0
            });
        }
        auto res = calibrateTimeSeries(ts);
        ASSERT_TRUE(res.has_value());
        EXPECT_TRUE(res->second.sensor_fault_detected);
        EXPECT_NE(res->second.fault_message.find("ALERT_SENSOR_OFFSET_EXCESSIVE"), std::string::npos);
    }

    // Test 5B: Excessive Gain Error (|beta - 1.0| > 0.08)
    {
        // 60s Rest 1 -> Charge 200s -> 60s Rest 2
        // True charge = 15A * (200s / 3600h) = 0.833 Ah (Delta SoC = 0.833 / 30 = 0.0278)
        // Set up OCV table and voltage steps to simulate a 12% gain error
        CalibrationConfig cfg;
        cfg.max_allowed_gain_error = 0.08;
        cfg.min_soc_delta_for_gain = 0.05;

        common::InternalTimeSeries ts;
        ts.nominal_capacity_ah = 10.0; // 10 Ah pack

        double t = 0.0;
        // Rest 1: 60s at 50.0V (low SoC)
        for (int i = 0; i < 60; ++i, t += 1.0) {
            ts.samples.push_back({.timestamp_s = t, .voltage_v = 50.0, .current_a = 0.0, .temperature_c = 25.0});
        }
        // Charge: 360s at 11.2A (measured 11.2A while true was 10.0A -> +12% gain error)
        for (int i = 0; i < 360; ++i, t += 1.0) {
            ts.samples.push_back({.timestamp_s = t, .voltage_v = 52.0, .current_a = 11.2, .temperature_c = 26.0});
        }
        // Rest 2: 60s at 53.5V (higher SoC)
        for (int i = 0; i < 60; ++i, t += 1.0) {
            ts.samples.push_back({.timestamp_s = t, .voltage_v = 53.5, .current_a = 0.0, .temperature_c = 25.0});
        }

        // Set OCV table such that 50.0V -> SoC 0.10, 53.5V -> SoC 0.20 (Delta SoC = 0.10, Delta Q_true = 1.0 Ah)
        cfg.ocv_table = {
            OcvPoint{.soc = 0.0, .voltage_v = 45.0},
            OcvPoint{.soc = 0.10, .voltage_v = 50.0},
            OcvPoint{.soc = 0.20, .voltage_v = 53.5},
            OcvPoint{.soc = 1.0, .voltage_v = 58.0}
        };

        auto res = calibrateTimeSeries(ts, {}, cfg);
        ASSERT_TRUE(res.has_value());
        const auto& report = res->second;

        EXPECT_TRUE(report.dual_anchors_found);
        // Net measured charge: 11.2A * 360s / 3600 = 1.12 Ah. True charge = 1.0 Ah -> beta = 1.12
        EXPECT_NEAR(report.estimated_gain, 1.12, 0.02);
        EXPECT_TRUE(report.sensor_fault_detected);
        EXPECT_NE(report.fault_message.find("ALERT_SENSOR_GAIN_EXCESSIVE"), std::string::npos);
    }
}

// ─── Test 6: OCV Lookup Table & Inversion ────────────────────────────────────

TEST(CalibrationTest, OcvLookup_RoundTripAndMonotonicity) {
    auto table = getDefaultLfp16sOcvTable();
    ASSERT_GE(table.size(), 100);

    // Verify strict monotonicity
    for (size_t i = 1; i < table.size(); ++i) {
        EXPECT_GT(table[i].voltage_v, table[i - 1].voltage_v);
        EXPECT_GT(table[i].soc, table[i - 1].soc);
    }

    // Verify lookup round-trip
    for (double test_soc = 0.05; test_soc <= 0.95; test_soc += 0.10) {
        double v = lookupOcvFromSoc(test_soc, table);
        double recovered_soc = lookupSocFromOcv(v, table);
        EXPECT_NEAR(recovered_soc, test_soc, 0.01);
    }
}

// ─── Test 7: Empty Input Returns Error ───────────────────────────────────────

TEST(CalibrationTest, EmptyInput_ReturnsError) {
    common::InternalTimeSeries empty_ts;
    auto res = calibrateTimeSeries(empty_ts);
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(res.error().code, "EMPTY_INPUT");
}
