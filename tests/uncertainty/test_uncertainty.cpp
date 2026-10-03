#include <gtest/gtest.h>
#include <ev/battery/uncertainty/uncertainty.hpp>
#include <ev/battery/estimation/estimation.hpp>

#include <cmath>
#include <vector>

using namespace ev::battery;
using namespace ev::battery::uncertainty;

// ─── Test 1: Conformal Coverage Probability Verification ─────────────────────

TEST(UncertaintyTest, ConformalCoverage_EnclosesGroundTruthAtTargetConfidence) {
    // Generate 50 holdout validation samples with realistic ground truth SoH and residuals
    constexpr size_t kSamples = 50;
    size_t covered_count_90 = 0;
    size_t covered_count_95 = 0;

    UncertaintyConfig cfg_90;
    cfg_90.confidence_level = 0.90;

    UncertaintyConfig cfg_95;
    cfg_95.confidence_level = 0.95;

    for (size_t i = 0; i < kSamples; ++i) {
        // True SoH uniformly distributed in [70%, 98%]
        double true_soh = 70.0 + (static_cast<double>(i) / static_cast<double>(kSamples)) * 28.0;

        // Injected estimation residual with empirical standard deviation ~ 1.0%
        double angle = static_cast<double>(i) * 1.7;
        double residual = 1.0 * std::sin(angle); // bounded within +/- 1.0%
        double est_soh = true_soh + residual;

        estimation::EstimationReport est_rep;
        est_rep.soh_percent = est_soh;
        est_rep.confidence_score = 0.95;

        estimation::EstimationInput in;
        in.thermal_report.thermal_degradation_index = 1.0;
        in.calibration_report.dual_anchors_found = true;
        in.calibration_report.sensor_fault_detected = false;

        auto res_90 = quantifyUncertainty(est_rep, in, cfg_90);
        ASSERT_TRUE(res_90.has_value());
        if (true_soh >= res_90->soh_lower_bound_pct && true_soh <= res_90->soh_upper_bound_pct) {
            ++covered_count_90;
        }

        auto res_95 = quantifyUncertainty(est_rep, in, cfg_95);
        ASSERT_TRUE(res_95.has_value());
        if (true_soh >= res_95->soh_lower_bound_pct && true_soh <= res_95->soh_upper_bound_pct) {
            ++covered_count_95;
        }
    }

    // Mathematical guarantee: empirical coverage >= nominal coverage (90% and 95%)
    double coverage_90 = static_cast<double>(covered_count_90) / static_cast<double>(kSamples);
    double coverage_95 = static_cast<double>(covered_count_95) / static_cast<double>(kSamples);

    EXPECT_GE(coverage_90, 0.90);
    EXPECT_GE(coverage_95, 0.94);
}

// ─── Test 2: Heteroscedastic Local Difficulty Scaling ────────────────────────

TEST(UncertaintyTest, HeteroscedasticScaling_WidensUnderSummerThermalAbuse) {
    estimation::EstimationReport est;
    est.soh_percent = 84.0;
    est.confidence_score = 0.95;

    // Case 2A: Temperate baseline (25°C, TDI = 1.0, dual anchors found)
    estimation::EstimationInput in_temperate;
    in_temperate.thermal_report.thermal_degradation_index = 1.0;
    in_temperate.calibration_report.dual_anchors_found = true;
    in_temperate.calibration_report.sensor_fault_detected = false;

    auto res_temperate = quantifyUncertainty(est, in_temperate);
    ASSERT_TRUE(res_temperate.has_value());
    EXPECT_DOUBLE_EQ(res_temperate->local_difficulty_multiplier, 1.0);
    EXPECT_LE(res_temperate->interval_width_pct, 5.0);
    EXPECT_EQ(res_temperate->decision, CertificationDecision::CERTIFIED);

    // Case 2B: Indian summer fleet mission (45°C, TDI = 2.80, missing dual anchors)
    estimation::EstimationInput in_summer;
    in_summer.thermal_report.thermal_degradation_index = 2.80;
    in_summer.calibration_report.dual_anchors_found = false;
    in_summer.calibration_report.sensor_fault_detected = false;

    auto res_summer = quantifyUncertainty(est, in_summer);
    ASSERT_TRUE(res_summer.has_value());
    EXPECT_GT(res_summer->local_difficulty_multiplier, 1.50);
    EXPECT_GT(res_summer->interval_width_pct, res_temperate->interval_width_pct);

    // Interval width exceeds Gate 3 commercial threshold (5%), triggering AUDIT_REQUIRED
    EXPECT_GT(res_summer->interval_width_pct, 5.0);
    EXPECT_EQ(res_summer->decision, CertificationDecision::AUDIT_REQUIRED);
    EXPECT_NE(res_summer->decision_rationale.find("AUDIT_REQUIRED"), std::string::npos);
}

// ─── Test 3: Sensor Hardware Fault Escalates Uncertainty ─────────────────────

TEST(UncertaintyTest, SensorFault_EscalatesUncertaintyAndBlocksCertification) {
    estimation::EstimationReport est;
    est.soh_percent = 80.0;
    est.confidence_score = 0.75; // Low confidence due to faulty sensor

    estimation::EstimationInput in;
    in.thermal_report.thermal_degradation_index = 1.0;
    in.calibration_report.dual_anchors_found = false;
    in.calibration_report.sensor_fault_detected = true; // Gate 2 sensor fault!

    auto res = quantifyUncertainty(est, in);
    ASSERT_TRUE(res.has_value());
    EXPECT_GT(res->local_difficulty_multiplier, 1.80);
    EXPECT_GT(res->interval_width_pct, 5.0);
    EXPECT_EQ(res->decision, CertificationDecision::AUDIT_REQUIRED);
}

// ─── Test 4: Out-of-Distribution (OOD) Safety Circuit-Breaker ────────────────

TEST(UncertaintyTest, OutOfDistribution_TriggersSafetyAbstention) {
    estimation::EstimationReport est;
    est.soh_percent = 88.0;

    // Atypical thermal anomaly (core temperature 66°C > 55°C limit)
    estimation::EstimationInput in;
    in.thermal_report.max_temperature_c = 66.0;
    in.thermal_report.thermal_degradation_index = 1.5;
    in.calibration_report.dual_anchors_found = true;

    UncertaintyConfig cfg;
    cfg.ood_threshold_z = 3.0;

    auto res = quantifyUncertainty(est, in, cfg);
    ASSERT_TRUE(res.has_value());
    EXPECT_TRUE(res->is_out_of_distribution);
    EXPECT_GE(res->ood_score, 3.0);
    EXPECT_EQ(res->decision, CertificationDecision::AUDIT_REQUIRED);
    EXPECT_NE(res->decision_rationale.find("Out-of-Distribution"), std::string::npos);
}

// ─── Test 5: Physical Clamping to [0, 100]% ───────────────────────────────────

TEST(UncertaintyTest, PhysicalClamping_PreservesValidSoHRange) {
    // Pack near pristine (99% SoH)
    estimation::EstimationReport est_high;
    est_high.soh_percent = 99.2;
    estimation::EstimationInput in;
    in.calibration_report.dual_anchors_found = true;

    auto res_high = quantifyUncertainty(est_high, in);
    ASSERT_TRUE(res_high.has_value());
    EXPECT_LE(res_high->soh_upper_bound_pct, 100.0);

    // Pack near dead (1.0% SoH)
    estimation::EstimationReport est_low;
    est_low.soh_percent = 1.0;
    auto res_low = quantifyUncertainty(est_low, in);
    ASSERT_TRUE(res_low.has_value());
    EXPECT_GE(res_low->soh_lower_bound_pct, 0.0);
}

// ─── Test 6: Configuration Validation ────────────────────────────────────────

TEST(UncertaintyTest, InvalidConfidenceLevel_ReturnsError) {
    estimation::EstimationReport est;
    estimation::EstimationInput in;

    UncertaintyConfig bad_cfg_low;
    bad_cfg_low.confidence_level = -0.1;
    EXPECT_FALSE(quantifyUncertainty(est, in, bad_cfg_low).has_value());

    UncertaintyConfig bad_cfg_high;
    bad_cfg_high.confidence_level = 1.0;
    EXPECT_FALSE(quantifyUncertainty(est, in, bad_cfg_high).has_value());
}
