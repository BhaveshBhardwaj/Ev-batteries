#include <gtest/gtest.h>
#include <ev/battery/segmentation/segmentation.hpp>
#include "../synthetic/telemetry_generator.hpp"
#include <fstream>
#include <sstream>

using namespace ev::battery;
using namespace ev::battery::segmentation;

TEST(SegmentationTest, EmptyInput_ReturnsError) {
    common::InternalTimeSeries empty_ts;
    auto res = segmentRegimes(empty_ts);
    EXPECT_FALSE(res.has_value());
}

TEST(SegmentationTest, InvalidThresholds_ReturnsError) {
    common::InternalTimeSeries ts;
    ts.samples.push_back({.timestamp_s = 0.0, .voltage_v = 3.0, .current_a = 0.0, .temperature_c = 25.0});
    ts.samples.push_back({.timestamp_s = 1.0, .voltage_v = 3.0, .current_a = 0.0, .temperature_c = 25.0});

    SegmentationConfig bad_cfg;
    bad_cfg.charge_current_thresh_a = 0.1;
    bad_cfg.rest_current_thresh_a = 0.2; // charge threshold <= rest threshold
    auto res = segmentRegimes(ts, bad_cfg);
    EXPECT_FALSE(res.has_value());
}

TEST(SegmentationTest, SyntheticMultiStageSession_CorrectRegimes) {
    common::InternalTimeSeries ts;
    double t = 0.0;

    // Stage 1: Rest 30s (0A)
    for (int i = 0; i < 30; ++i, t += 1.0) {
        ts.samples.push_back({.timestamp_s = t, .voltage_v = 51.2, .current_a = 0.0, .temperature_c = 25.0});
    }
    // Stage 2: Discharge 60s (-10A)
    for (int i = 0; i < 60; ++i, t += 1.0) {
        ts.samples.push_back({.timestamp_s = t, .voltage_v = 49.0, .current_a = -10.0, .temperature_c = 26.0});
    }
    // Stage 3: Rest 20s (0A)
    for (int i = 0; i < 20; ++i, t += 1.0) {
        ts.samples.push_back({.timestamp_s = t, .voltage_v = 50.5, .current_a = 0.0, .temperature_c = 25.5});
    }
    // Stage 4: CC Charge 100s (+15A)
    for (int i = 0; i < 100; ++i, t += 1.0) {
        ts.samples.push_back({.timestamp_s = t, .voltage_v = 53.0, .current_a = 15.0, .temperature_c = 27.0});
    }

    auto segs_res = segmentRegimes(ts);
    ASSERT_TRUE(segs_res.has_value());
    const auto& segs = *segs_res;

    ASSERT_EQ(segs.size(), 4);
    EXPECT_EQ(segs[0].type, common::SegmentType::REST);
    EXPECT_EQ(segs[1].type, common::SegmentType::DISCHARGE);
    EXPECT_EQ(segs[2].type, common::SegmentType::REST);
    EXPECT_EQ(segs[3].type, common::SegmentType::CHARGE);

    EXPECT_NEAR(segs[1].mean_current_a, -10.0, 1e-6);
    EXPECT_NEAR(segs[3].mean_current_a, 15.0, 1e-6);
    EXPECT_GT(segs[3].throughput_ah, 0.4); // 15A * 99s / 3600 = ~0.4125 Ah
}

TEST(SegmentationTest, ExtractConstantCurrentWindow_LocatesLongestCharge) {
    ev::battery::testing::GeneratorConfig cfg;
    ev::battery::testing::TelemetryGenerator gen(cfg);

    auto gt = gen.generateFullSession(123, 0.45);
    ASSERT_FALSE(gt.telemetry.samples.empty());

    // Configure pack-level thresholds (charging at 15A with 0.65A shunt drift)
    SegmentationConfig pack_cfg;
    pack_cfg.rest_current_thresh_a = 1.0;
    pack_cfg.charge_current_thresh_a = 2.0;
    pack_cfg.discharge_current_thresh_a = -2.0;

    // Should locate the 1800s charge segment
    auto cc_res = extractConstantCurrentWindow(gt.telemetry, 300.0, 0.5, pack_cfg);
    ASSERT_TRUE(cc_res.has_value());

    EXPECT_EQ(cc_res->type, common::SegmentType::CHARGE);
    EXPECT_GT(cc_res->duration_s, 1500.0);
    EXPECT_NEAR(cc_res->mean_current_a, 15.0 * cfg.current_gain_error + cfg.current_offset_a, 0.5);
}

TEST(SegmentationTest, RealNasaDataGrounding_ExtractsCCWindow) {
    std::ifstream file("data/reference/nasa/B0005_charge_cycle_001.csv");
    if (!file.is_open()) {
        GTEST_SKIP() << "NASA reference dataset file not found, skipping real-data grounding test";
    }

    common::InternalTimeSeries nasa_ts;
    nasa_ts.session_id = "nasa_b0005_cycle001";
    nasa_ts.nominal_capacity_ah = 2.0;

    std::string line;
    std::getline(file, line); // header

    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string s_t, s_v, s_i, s_temp;
        if (std::getline(ss, s_t, ',') &&
            std::getline(ss, s_v, ',') &&
            std::getline(ss, s_i, ',') &&
            std::getline(ss, s_temp, ',')) {
            nasa_ts.samples.push_back({
                .timestamp_s = std::stod(s_t),
                .voltage_v = std::stod(s_v),
                .current_a = std::stod(s_i),
                .temperature_c = std::stod(s_temp)
            });
        }
    }

    ASSERT_GT(nasa_ts.samples.size(), 100);

    // NASA B0005 charge is ~1.5A CC charge (duration ~760s) followed by CV decay
    auto cc_res = extractConstantCurrentWindow(nasa_ts, 300.0, 0.1);
    ASSERT_TRUE(cc_res.has_value());
    EXPECT_EQ(cc_res->type, common::SegmentType::CHARGE);
    EXPECT_NEAR(cc_res->mean_current_a, 1.5, 0.1); // NASA CC charge is nominal 1.51A
    EXPECT_GT(cc_res->duration_s, 600.0);
}
