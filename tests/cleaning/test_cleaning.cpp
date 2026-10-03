#include <gtest/gtest.h>
#include <ev/battery/cleaning/cleaning.hpp>
#include "../synthetic/telemetry_generator.hpp"

using namespace ev::battery;
using namespace ev::battery::cleaning;

TEST(CleaningTest, HampelFilter_NoOutliers_PreservesInput) {
    std::vector<double> input = {3.30, 3.31, 3.32, 3.31, 3.30, 3.32, 3.31};
    std::vector<double> output(input.size());

    auto res = hampelFilter(input, output, 2, 3.0);
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(*res, 0);

    for (size_t i = 0; i < input.size(); ++i) {
        EXPECT_DOUBLE_EQ(output[i], input[i]);
    }
}

TEST(CleaningTest, HampelFilter_SingleSpike_ReplacedByMedian) {
    std::vector<double> input = {3.30, 3.30, 3.31, 4.50 /* spike */, 3.30, 3.31, 3.30};
    std::vector<double> output(input.size());

    auto res = hampelFilter(input, output, 2, 3.0);
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(*res, 1);

    // Spike at index 3 should be replaced by median of {3.30, 3.31, 4.50, 3.30, 3.31} -> 3.31
    EXPECT_NEAR(output[3], 3.31, 0.02);
    // Non-spikes preserved
    EXPECT_DOUBLE_EQ(output[0], input[0]);
    EXPECT_DOUBLE_EQ(output[1], input[1]);
    EXPECT_DOUBLE_EQ(output[2], input[2]);
    EXPECT_DOUBLE_EQ(output[4], input[4]);
}

TEST(CleaningTest, HampelFilter_InvalidArgs_ReturnsErrors) {
    std::vector<double> in = {1.0, 2.0};
    std::vector<double> out_wrong_size(3);
    std::vector<double> out_correct(2);

    // Empty input
    auto res_empty = hampelFilter(std::span<const double>{}, out_correct, 1);
    EXPECT_FALSE(res_empty.has_value());

    // Mismatched sizes
    auto res_size = hampelFilter(in, out_wrong_size, 1);
    EXPECT_FALSE(res_size.has_value());

    // Half window zero
    auto res_win = hampelFilter(in, out_correct, 0);
    EXPECT_FALSE(res_win.has_value());
}

TEST(CleaningTest, DetectStuckSensor_DetectsFlatline) {
    std::vector<double> vals;
    std::vector<double> times;
    
    // 10 active samples
    for (int i = 0; i < 10; ++i) {
        vals.push_back(3.0 + 0.01 * i);
        times.push_back(static_cast<double>(i));
    }
    // 35 flatline samples
    for (int i = 10; i < 45; ++i) {
        vals.push_back(3.10);
        times.push_back(static_cast<double>(i));
    }
    // 10 active samples
    for (int i = 45; i < 55; ++i) {
        vals.push_back(3.15 + 0.01 * (i - 45));
        times.push_back(static_cast<double>(i));
    }

    auto res = detectStuckSensor(vals, times, 30);
    ASSERT_TRUE(res.has_value());
    ASSERT_EQ(res->size(), 1);
    EXPECT_EQ((*res)[0].start_index, 10);
    EXPECT_EQ((*res)[0].end_index, 44);
    EXPECT_DOUBLE_EQ((*res)[0].stuck_value, 3.10);
    EXPECT_DOUBLE_EQ((*res)[0].duration_s, 34.0);
}

TEST(CleaningTest, RepairTimestampGaps_InterpolatesMissingSamples) {
    common::InternalTimeSeries ts;
    // Sample 0: t=0, v=3.0, i=10.0
    ts.samples.push_back({.timestamp_s = 0.0, .voltage_v = 3.0, .current_a = 10.0, .temperature_c = 25.0});
    // Gap: Next sample at t=3.0 (missing t=1.0 and t=2.0)
    ts.samples.push_back({.timestamp_s = 3.0, .voltage_v = 3.3, .current_a = 10.0, .temperature_c = 25.0});

    auto res = repairTimestampGaps(ts, 1.0, 5.0);
    ASSERT_TRUE(res.has_value());
    const auto& [repaired, interpolated_count] = *res;

    EXPECT_EQ(interpolated_count, 2);
    ASSERT_EQ(repaired.samples.size(), 4);

    EXPECT_DOUBLE_EQ(repaired.samples[0].timestamp_s, 0.0);
    EXPECT_DOUBLE_EQ(repaired.samples[1].timestamp_s, 1.0);
    EXPECT_DOUBLE_EQ(repaired.samples[2].timestamp_s, 2.0);
    EXPECT_DOUBLE_EQ(repaired.samples[3].timestamp_s, 3.0);

    // Linear interpolation of voltage: 3.0, 3.1, 3.2, 3.3
    EXPECT_NEAR(repaired.samples[1].voltage_v, 3.1, 1e-6);
    EXPECT_NEAR(repaired.samples[2].voltage_v, 3.2, 1e-6);
}

TEST(CleaningTest, CleanTimeSeries_SimToRealTelemetryEndToEnd) {
    ev::battery::testing::GeneratorConfig cfg;
    cfg.outlier_probability = 0.03;
    cfg.packet_drop_prob = 0.02;
    ev::battery::testing::TelemetryGenerator gen(cfg);

    auto gt = gen.generateFullSession(42, 0.50);
    ASSERT_FALSE(gt.telemetry.samples.empty());

    auto clean_res = cleanTimeSeries(gt.telemetry, 1.0, 10.0, 3, 3.0);
    ASSERT_TRUE(clean_res.has_value());

    const auto& [cleaned, report] = *clean_res;
    EXPECT_GT(report.voltage_spikes_removed, 0);
    EXPECT_GT(report.samples_interpolated, 0);
    EXPECT_GE(cleaned.samples.size(), report.original_sample_count);

    // Verify all timestamps in cleaned are strictly monotonic
    for (size_t i = 1; i < cleaned.samples.size(); ++i) {
        EXPECT_GT(cleaned.samples[i].timestamp_s, cleaned.samples[i - 1].timestamp_s);
    }
}
