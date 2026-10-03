#include <gtest/gtest.h>
#include <ev/battery/features/features.hpp>
#include <ev/battery/ica_dva/ica_dva.hpp>
#include <fstream>
#include <sstream>
#include <cmath>

using namespace ev::battery;

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

// Generate ideal synthetic CC charge segment with a plateau at progress = 0.5 (V ~ 3.7V)
common::InternalTimeSeries generateSyntheticCc(double plateau_v = 3.70) {
    common::InternalTimeSeries ts;
    ts.session_id = "synth_cc";
    ts.nominal_capacity_ah = 2.0;
    size_t n = 800;
    constexpr double kPi = 3.14159265358979323846;

    for (size_t i = 0; i < n; ++i) {
        double progress = static_cast<double>(i) / static_cast<double>(n - 1);
        double v = 3.3 + 0.8 * progress + 0.08 * std::sin(2.0 * kPi * progress);
        ts.samples.push_back(common::Sample{
            .timestamp_s = static_cast<double>(i),
            .voltage_v = v,
            .current_a = 2.0,
            .temperature_c = 25.0
        });
    }
    return ts;
}

// Synthetic IcaCurve with two distinct Gaussian-shaped peaks and a valley
ica_dva::IcaCurve generateSyntheticTwoPeakCurve() {
    ica_dva::IcaCurve ica;
    size_t n = 160;
    ica.voltage_v.resize(n);
    ica.charge_ah.resize(n);
    ica.dq_dv.resize(n);

    double v_start = 3.30;
    double dv = 0.005;

    for (size_t i = 0; i < n; ++i) {
        double v = v_start + static_cast<double>(i) * dv;
        ica.voltage_v[i] = v;
        ica.charge_ah[i] = 0.01 * static_cast<double>(i);

        // Peak 1 at 3.50V (width 0.03V, height 4.0)
        double g1 = 4.0 * std::exp(-0.5 * std::pow((v - 3.50) / 0.03, 2.0));
        // Peak 2 at 3.90V (width 0.04V, height 6.0)
        double g2 = 6.0 * std::exp(-0.5 * std::pow((v - 3.90) / 0.04, 2.0));
        // Baseline 0.5
        ica.dq_dv[i] = 0.5 + g1 + g2;
    }
    return ica;
}

} // namespace

TEST(FeaturesTest, SyntheticSinglePeakExtraction) {
    auto cc = generateSyntheticCc();
    auto ica_res = ica_dva::computeIca(cc);
    ASSERT_TRUE(ica_res.has_value());

    auto feat_res = features::extractIcaFeatures(*ica_res);
    ASSERT_TRUE(feat_res.has_value());
    const auto& feat = *feat_res;

    // Peak should be detected near 3.70 V
    EXPECT_NEAR(feat.primary_peak_voltage_v, 3.70, 0.05);
    EXPECT_GT(feat.primary_peak_height, 0.0);
    EXPECT_GT(feat.primary_peak_area_ah, 0.0);
    EXPECT_GT(feat.total_cc_charge_ah, 0.0);
    EXPECT_NEAR(feat.voltage_span_v, 0.8, 0.05);

    auto vec = feat.toVector();
    EXPECT_EQ(vec.size(), 8);
}

TEST(FeaturesTest, SyntheticTwoPeaksAndValleyExtraction) {
    auto ica = generateSyntheticTwoPeakCurve();

    features::FeatureConfig cfg;
    cfg.min_prominence_ratio = 0.10;
    cfg.peak_area_half_width_v = 0.040;

    auto feat_res = features::extractIcaFeatures(ica, cfg);
    ASSERT_TRUE(feat_res.has_value());
    const auto& feat = *feat_res;

    // Primary peak is the taller one at 3.90V
    EXPECT_NEAR(feat.primary_peak_voltage_v, 3.90, 0.02);
    EXPECT_NEAR(feat.primary_peak_height, 6.5, 0.3);

    // Secondary peak is the smaller one at 3.50V
    EXPECT_TRUE(feat.has_secondary_peak);
    EXPECT_NEAR(feat.secondary_peak_voltage_v, 3.50, 0.02);
    EXPECT_NEAR(feat.secondary_peak_height, 4.5, 0.3);

    // Valley must lie between the two peaks
    EXPECT_GT(feat.valley_voltage_v, 3.50);
    EXPECT_LT(feat.valley_voltage_v, 3.90);
    EXPECT_LT(feat.valley_height, feat.secondary_peak_height);
}

TEST(FeaturesTest, RealWorldNasaCycle10VsCycle100DegradationTracking) {
    std::string csv_c10 = "data/reference/nasa/B0005_charge_cycle_010.csv";
    std::string csv_c100 = "data/reference/nasa/B0005_charge_cycle_100.csv";

    auto ts_c10 = loadNasaChargeCsv(csv_c10, "NASA_B0005_C10");
    auto ts_c100 = loadNasaChargeCsv(csv_c100, "NASA_B0005_C100");

    if (ts_c10.samples.empty() || ts_c100.samples.empty()) {
        GTEST_SKIP() << "NASA reference CSV files not found at " << csv_c10;
    }

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

    // 1. Peak height reduction (LAM aging indicator)
    EXPECT_GT(feat_c10->primary_peak_height, feat_c100->primary_peak_height);
    double height_loss_pct = (feat_c10->primary_peak_height - feat_c100->primary_peak_height) /
                             feat_c10->primary_peak_height * 100.0;
    EXPECT_GE(height_loss_pct, 15.0); // At least 15% peak drop

    // 2. Integrated peak area reduction (Charge capacity loss)
    EXPECT_GT(feat_c10->primary_peak_area_ah, feat_c100->primary_peak_area_ah);

    // 3. Peak voltage shift to higher potential (LLI & Resistance rise)
    EXPECT_GE(feat_c100->primary_peak_voltage_v, feat_c10->primary_peak_voltage_v);

    // 4. Feature vector output
    auto vec_c10 = feat_c10->toVector();
    auto vec_c100 = feat_c100->toVector();
    EXPECT_EQ(vec_c10.size(), 8);
    EXPECT_EQ(vec_c100.size(), 8);
}

TEST(FeaturesTest, ErrorHandlingInsufficientPoints) {
    ica_dva::IcaCurve short_ica;
    short_ica.voltage_v = {3.7, 3.8, 3.9};
    short_ica.dq_dv = {1.0, 2.0, 1.0};

    auto res = features::extractIcaFeatures(short_ica);
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(res.error().code, "INSUFFICIENT_DATA");
}
