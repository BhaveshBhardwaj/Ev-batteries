#include <gtest/gtest.h>
#include <ev/battery/estimation/estimation.hpp>
#include <ev/battery/features/features.hpp>
#include <ev/battery/ica_dva/ica_dva.hpp>

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

using namespace ev::battery;
using namespace ev::battery::estimation;

namespace {

common::InternalTimeSeries loadNasaChargeCsv(const std::string& filepath, const std::string& id) {
    common::InternalTimeSeries ts;
    ts.session_id = id;
    ts.chemistry = "NMC";
    ts.nominal_capacity_ah = 2.0;

    std::ifstream file(filepath);
    if (!file.is_open()) return ts;

    std::string line;
    if (!std::getline(file, line)) return ts;

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string t_str, v_str, i_str, temp_str;
        if (std::getline(ss, t_str, ',') &&
            std::getline(ss, v_str, ',') &&
            std::getline(ss, i_str, ',') &&
            std::getline(ss, temp_str, ',')) {
            common::Sample s;
            s.timestamp_s = std::stod(t_str);
            s.voltage_v = std::stod(v_str);
            s.current_a = std::stod(i_str);
            s.temperature_c = std::stod(temp_str);
            ts.samples.push_back(s);
        }
    }
    return ts;
}

} // namespace

// ─── Test 1: Real NASA Ground Truth Degradation Tracking ─────────────────────

TEST(EstimationTest, RealWorldNasaCycle10VsCycle100DegradationTracking) {
    std::string csv_c10 = "data/reference/nasa/B0005_charge_cycle_010.csv";
    std::string csv_c100 = "data/reference/nasa/B0005_charge_cycle_100.csv";

    auto ts_c10 = loadNasaChargeCsv(csv_c10, "NASA_B0005_C10");
    auto ts_c100 = loadNasaChargeCsv(csv_c100, "NASA_B0005_C100");

    if (ts_c10.samples.empty() || ts_c100.samples.empty()) {
        GTEST_SKIP() << "NASA reference CSV files not found at " << csv_c10;
    }

    // Filter Constant Current (CC) region (> 1.2A)
    common::InternalTimeSeries cc_c10;
    for (const auto& s : ts_c10.samples) {
        if (s.current_a >= 1.2) cc_c10.samples.push_back(s);
    }
    common::InternalTimeSeries cc_c100;
    for (const auto& s : ts_c100.samples) {
        if (s.current_a >= 1.2) cc_c100.samples.push_back(s);
    }

    ica_dva::IcaConfig ica_cfg;
    ica_cfg.voltage_step_v = 0.005;
    ica_cfg.savgol_window = 15;

    auto ica_c10 = ica_dva::computeIca(cc_c10, ica_cfg);
    auto ica_c100 = ica_dva::computeIca(cc_c100, ica_cfg);
    ASSERT_TRUE(ica_c10.has_value());
    ASSERT_TRUE(ica_c100.has_value());

    auto feat_c10 = features::extractIcaFeatures(*ica_c10);
    auto feat_c100 = features::extractIcaFeatures(*ica_c100);
    ASSERT_TRUE(feat_c10.has_value());
    ASSERT_TRUE(feat_c100.has_value());

    // Configure baseline calibration based on pristine early cycle (Cycle 10)
    EstimationConfig config;
    config.chemistry = "NMC";
    config.baseline.initial_peak_height = feat_c10->primary_peak_height;
    config.baseline.initial_peak_voltage_v = feat_c10->primary_peak_voltage_v;
    config.baseline.initial_peak_area_ah = feat_c10->primary_peak_area_ah;
    config.baseline.nominal_capacity_ah = 2.0;

    // Evaluate Cycle 10
    EstimationInput in_c10;
    in_c10.ica_features = *feat_c10;
    in_c10.nominal_capacity_ah = 2.0;
    in_c10.equivalent_full_cycles = 10.0;
    in_c10.thermal_report.thermal_degradation_index = 1.0;

    auto res_c10 = estimateBatteryHealth(in_c10, config);
    ASSERT_TRUE(res_c10.has_value());
    const auto& report_c10 = *res_c10;

    // Cycle 10 represents healthy pack (> 95% SoH)
    EXPECT_GT(report_c10.soh_percent, 95.0);
    EXPECT_GT(report_c10.estimated_capacity_ah, 1.90);
    EXPECT_FALSE(report_c10.end_of_life_reached);

    // Evaluate Cycle 100
    EstimationInput in_c100;
    in_c100.ica_features = *feat_c100;
    in_c100.nominal_capacity_ah = 2.0;
    in_c100.equivalent_full_cycles = 100.0;
    in_c100.thermal_report.thermal_degradation_index = 1.0;

    auto res_c100 = estimateBatteryHealth(in_c100, config);
    ASSERT_TRUE(res_c100.has_value());
    const auto& report_c100 = *res_c100;

    // NASA B0005 ground truth capacity at Cycle 100 is ~1.40 Ah (~70% SoH)
    // Verify our multi-modal estimate captures this physical drop
    EXPECT_LT(report_c100.soh_percent, 76.0);
    EXPECT_GT(report_c100.soh_percent, 68.0);
    EXPECT_NEAR(report_c100.estimated_capacity_ah, 1.44, 0.10);

    // Verify significant active material degradation is identified
    EXPECT_GT(report_c100.lam_percent, 20.0);
    EXPECT_GT(report_c100.lli_percent, 20.0);
}

// ─── Test 2: Degradation Dominance Classification ────────────────────────────

TEST(EstimationTest, DegradationDominance_ClassifiesCorrectly) {
    EstimationConfig config;
    config.baseline.initial_peak_height = 5.0;
    config.baseline.initial_peak_voltage_v = 3.97;
    config.baseline.initial_peak_area_ah = 1.25;

    // Case 2A: Large positive peak shift (LLI / SEI layer growth)
    {
        EstimationInput in;
        in.ica_features.primary_peak_voltage_v = 4.04;
        in.ica_features.primary_peak_height = 4.9;
        in.ica_features.primary_peak_area_ah = 1.10;
        in.thermal_report.thermal_degradation_index = 1.1;

        auto res = estimateBatteryHealth(in, config);
        ASSERT_TRUE(res.has_value());
        EXPECT_EQ(res->dominance, DegradationDominance::LLI_DOMINATED);
        EXPECT_GT(res->lli_percent, res->lam_percent);
    }

    // Case 2B: Severe peak height attenuation without voltage shift (LAM)
    {
        EstimationInput in;
        in.ica_features.primary_peak_voltage_v = 3.97;
        in.ica_features.primary_peak_height = 3.0;
        in.ica_features.primary_peak_area_ah = 0.85;
        in.thermal_report.thermal_degradation_index = 1.1;

        auto res = estimateBatteryHealth(in, config);
        ASSERT_TRUE(res.has_value());
        EXPECT_EQ(res->dominance, DegradationDominance::LAM_DOMINATED);
        EXPECT_GT(res->lam_percent, res->lli_percent);
    }

    // Case 2C: High thermal stress (TDI >= 2.50)
    {
        EstimationInput in;
        in.ica_features.primary_peak_voltage_v = 3.98;
        in.ica_features.primary_peak_height = 4.5;
        in.ica_features.primary_peak_area_ah = 1.15;
        in.thermal_report.thermal_degradation_index = 2.85; // Indian summer thermal abuse

        auto res = estimateBatteryHealth(in, config);
        ASSERT_TRUE(res.has_value());
        EXPECT_EQ(res->dominance, DegradationDominance::THERMAL_ACCELERATED);
    }
}

// ─── Test 3: Thermal-Accelerated Remaining Useful Life (RUL) ──────────────────

TEST(EstimationTest, RemainingUsefulLife_AcceleratedByThermalStress) {
    EstimationConfig config;
    config.eol_soh_threshold_pct = 70.0;
    config.baseline.initial_peak_height = 5.0;
    config.baseline.initial_peak_voltage_v = 3.97;
    config.baseline.initial_peak_area_ah = 1.25;

    // Battery currently at ~85% SoH after 100 cycles
    EstimationInput in_normal;
    in_normal.ica_features.primary_peak_voltage_v = 3.98;
    in_normal.ica_features.primary_peak_height = 4.25;
    in_normal.ica_features.primary_peak_area_ah = 1.06;
    in_normal.equivalent_full_cycles = 100.0;
    in_normal.thermal_report.thermal_degradation_index = 1.0; // 25°C temperate baseline

    auto res_normal = estimateBatteryHealth(in_normal, config);
    ASSERT_TRUE(res_normal.has_value());
    double rul_normal = res_normal->remaining_useful_life_cycles;
    EXPECT_GT(rul_normal, 0.0);

    // Same battery under 45°C Indian summer fleet mission (TDI = 2.50)
    EstimationInput in_hot = in_normal;
    in_hot.thermal_report.thermal_degradation_index = 2.50;

    auto res_hot = estimateBatteryHealth(in_hot, config);
    ASSERT_TRUE(res_hot.has_value());
    double rul_hot = res_hot->remaining_useful_life_cycles;

    // Projected RUL must be reduced by factor of ~2.5x under summer conditions
    EXPECT_NEAR(rul_normal / rul_hot, 2.50, 0.10);
}

// ─── Test 4: End-of-Life Flagging (BPAN Tier 3 Threshold) ─────────────────────

TEST(EstimationTest, EndOfLife_FlaggedCorrectly) {
    EstimationConfig config;
    config.eol_soh_threshold_pct = 70.0;
    config.baseline.initial_peak_height = 5.0;
    config.baseline.initial_peak_voltage_v = 3.97;
    config.baseline.initial_peak_area_ah = 1.25;

    // Severely degraded battery (SoH < 70%)
    EstimationInput in;
    in.ica_features.primary_peak_voltage_v = 4.05;
    in.ica_features.primary_peak_height = 2.5;
    in.ica_features.primary_peak_area_ah = 0.65;

    auto res = estimateBatteryHealth(in, config);
    ASSERT_TRUE(res.has_value());
    EXPECT_TRUE(res->end_of_life_reached);
    EXPECT_DOUBLE_EQ(res->remaining_useful_life_cycles, 0.0);
    EXPECT_LT(res->soh_percent, 70.0);
}

// ─── Test 5: Fallback to Calibrated Throughput when Peaks are Absent ──────────

TEST(EstimationTest, MissingPeaks_FallsBackGracefully) {
    EstimationConfig config;
    config.baseline.nominal_capacity_ah = 30.0;

    EstimationInput in;
    // No ICA peaks available (e.g. short 5% micro-charge session)
    in.nominal_capacity_ah = 30.0;
    in.calibration_report.calibrated_coulomb_ah = 25.5; // 85% of 30Ah
    in.calibration_report.dual_anchors_found = true;

    auto res = estimateBatteryHealth(in, config);
    ASSERT_TRUE(res.has_value());
    EXPECT_NEAR(res->soh_percent, 85.0, 1.0);
    EXPECT_NEAR(res->estimated_capacity_ah, 25.5, 0.3);
    EXPECT_LT(res->confidence_score, 0.90); // Confidence deducted due to missing peaks
}
