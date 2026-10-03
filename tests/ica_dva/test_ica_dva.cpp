#include <gtest/gtest.h>
#include <ev/battery/ica_dva/ica_dva.hpp>
#include <ev/battery/cleaning/cleaning.hpp>
#include <ev/battery/denoising/denoising.hpp>
#include <fstream>
#include <sstream>
#include <cmath>

using namespace ev::battery;

namespace {

// Helper to load real NASA charge cycle CSV
common::InternalTimeSeries loadNasaChargeCsv(const std::string& filepath, const std::string& id) {
    common::InternalTimeSeries ts;
    ts.session_id = id;
    ts.chemistry = "NMC";
    ts.nominal_capacity_ah = 2.0;

    std::ifstream file(filepath);
    if (!file.is_open()) return ts;

    std::string line;
    // Skip header: timestamp_s,voltage_v,current_a,temperature_c
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

// Generate ideal synthetic CC charge segment with an electrochemical plateau (creates an ICA peak)
common::InternalTimeSeries generateSyntheticCcCharge(
    size_t n_samples = 1000,
    double start_v = 3.3,
    double end_v = 4.1,
    double current_a = 2.0,
    double dt_s = 1.0)
{
    common::InternalTimeSeries ts;
    ts.session_id = "synth_cc";
    ts.chemistry = "LFP";
    ts.nominal_capacity_ah = 2.0;

    ts.samples.reserve(n_samples);
    constexpr double kPi = 3.14159265358979323846;
    for (size_t i = 0; i < n_samples; ++i) {
        double t = static_cast<double>(i) * dt_s;
        double progress = static_cast<double>(i) / static_cast<double>(n_samples - 1);
        // Base linear ramp + smooth sine inflection creates a plateau at progress = 0.5 (V ~ 3.7V)
        double v = start_v + (end_v - start_v) * progress + 0.08 * std::sin(2.0 * kPi * progress);
        ts.samples.push_back(common::Sample{
            .timestamp_s = t,
            .voltage_v = v,
            .current_a = current_a,
            .temperature_c = 25.0
        });
    }
    return ts;
}

} // namespace

TEST(IcaDvaTest, SyntheticCcSegmentProducesSmoothIcaPeak) {
    auto cc_series = generateSyntheticCcCharge(800, 3.3, 4.1, 1.5, 1.0);

    ica_dva::IcaConfig cfg;
    cfg.voltage_step_v = 0.005; // 5 mV
    cfg.savgol_window = 11;
    cfg.savgol_poly_order = 2;

    auto ica_res = ica_dva::computeIca(cc_series, cfg);
    ASSERT_TRUE(ica_res.has_value());
    const auto& ica = *ica_res;

    EXPECT_FALSE(ica.voltage_v.empty());
    EXPECT_EQ(ica.voltage_v.size(), ica.charge_ah.size());
    EXPECT_EQ(ica.voltage_v.size(), ica.dq_dv.size());

    // Verify grid spacing is strictly uniform
    for (size_t i = 1; i < ica.voltage_v.size(); ++i) {
        EXPECT_NEAR(ica.voltage_v[i] - ica.voltage_v[i - 1], 0.005, 1e-6);
        // Cumulative charge Q(V) must be monotonically non-decreasing
        EXPECT_GE(ica.charge_ah[i], ica.charge_ah[i - 1] - 1e-9);
    }

    // Verify peak detection around the 3.7V plateau
    EXPECT_GT(ica.max_dq_dv, 0.0);
    EXPECT_NEAR(ica.peak_voltage_v, 3.70, 0.05);
}

TEST(IcaDvaTest, RealWorldNasaCycle1VsCycle100PeakAttenuation) {
    std::string csv_c10 = "data/reference/nasa/B0005_charge_cycle_010.csv";
    std::string csv_c100 = "data/reference/nasa/B0005_charge_cycle_100.csv";

    auto ts_c10 = loadNasaChargeCsv(csv_c10, "NASA_B0005_C10");
    auto ts_c100 = loadNasaChargeCsv(csv_c100, "NASA_B0005_C100");

    if (ts_c10.samples.empty() || ts_c100.samples.empty()) {
        GTEST_SKIP() << "NASA reference CSV files not found at " << csv_c10;
    }

    // Filter to the CC window (current >= 1.2A for B0005 1.5A CC charge)
    common::InternalTimeSeries cc_c10;
    cc_c10.session_id = "B0005_CC_C10";
    for (const auto& s : ts_c10.samples) {
        if (s.current_a >= 1.2) cc_c10.samples.push_back(s);
    }

    common::InternalTimeSeries cc_c100;
    cc_c100.session_id = "B0005_CC_C100";
    for (const auto& s : ts_c100.samples) {
        if (s.current_a >= 1.2) cc_c100.samples.push_back(s);
    }

    ASSERT_GT(cc_c10.samples.size(), 50);
    ASSERT_GT(cc_c100.samples.size(), 50);

    ica_dva::IcaConfig cfg;
    cfg.voltage_step_v = 0.005; // 5 mV
    cfg.savgol_window = 15;
    cfg.savgol_poly_order = 2;

    auto ica_c10 = ica_dva::computeIca(cc_c10, cfg);
    auto ica_c100 = ica_dva::computeIca(cc_c100, cfg);

    ASSERT_TRUE(ica_c10.has_value());
    ASSERT_TRUE(ica_c100.has_value());

    // Characteristic electrochemical phenomenon:
    // Peak capacity fade: Cycle 10 peak amplitude > Cycle 100 peak amplitude
    EXPECT_GT(ica_c10->max_dq_dv, ica_c100->max_dq_dv);

    // Peak voltage shift towards higher potential due to internal resistance increase
    EXPECT_GE(ica_c100->peak_voltage_v, ica_c10->peak_voltage_v);

    // Total delivered charge during CC window must also decrease with aging
    EXPECT_GT(ica_c10->charge_ah.back(), ica_c100->charge_ah.back());
}

TEST(IcaDvaTest, DvaCurveGenerationOnSyntheticCc) {
    auto cc_series = generateSyntheticCcCharge(800, 3.2, 4.2, 2.0, 1.0);

    ica_dva::DvaConfig cfg;
    cfg.capacity_step_ah = 0.005; // 5 mAh
    cfg.savgol_window = 11;

    auto dva_res = ica_dva::computeDva(cc_series, cfg);
    ASSERT_TRUE(dva_res.has_value());
    const auto& dva = *dva_res;

    EXPECT_FALSE(dva.capacity_ah.empty());
    EXPECT_EQ(dva.capacity_ah.size(), dva.voltage_v.size());
    EXPECT_EQ(dva.capacity_ah.size(), dva.dv_dq.size());

    // Uniform capacity grid check
    for (size_t i = 1; i < dva.capacity_ah.size(); ++i) {
        EXPECT_NEAR(dva.capacity_ah[i] - dva.capacity_ah[i - 1], 0.005, 1e-6);
        // Voltage must increase during charge
        EXPECT_GE(dva.voltage_v[i], dva.voltage_v[i - 1] - 1e-6);
    }
}

TEST(IcaDvaTest, ErrorHandlingInsufficientDataAndSmallSpan) {
    common::InternalTimeSeries empty_ts;
    auto res_empty = ica_dva::computeIca(empty_ts);
    EXPECT_FALSE(res_empty.has_value());
    EXPECT_EQ(res_empty.error().code, "INSUFFICIENT_DATA");

    // Telemetry with tiny voltage span (e.g. 0.05V < 0.20V min span)
    common::InternalTimeSeries flat_ts;
    flat_ts.session_id = "flat";
    for (size_t i = 0; i < 50; ++i) {
        flat_ts.samples.push_back(common::Sample{
            .timestamp_s = static_cast<double>(i),
            .voltage_v = 3.30 + 0.001 * static_cast<double>(i),
            .current_a = 1.0,
            .temperature_c = 25.0
        });
    }

    auto res_span = ica_dva::computeIca(flat_ts);
    EXPECT_FALSE(res_span.has_value());
    EXPECT_EQ(res_span.error().code, "VOLTAGE_SPAN_TOO_SMALL");
}
