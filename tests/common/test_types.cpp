#include <gtest/gtest.h>
#include <ev/battery/common/types.hpp>

using namespace ev::battery::common;

TEST(CommonTypes_Sample, Construction_DefaultAndCustomValues) {
    Sample sample{
        .timestamp_s = 1700000000.0,
        .voltage_v = 3.65,
        .current_a = 15.0,
        .temperature_c = 28.5,
        .cell_voltages_v = std::vector<double>{3.65, 3.64, 3.66}
    };

    EXPECT_DOUBLE_EQ(sample.timestamp_s, 1700000000.0);
    EXPECT_DOUBLE_EQ(sample.voltage_v, 3.65);
    EXPECT_DOUBLE_EQ(sample.current_a, 15.0);
    EXPECT_DOUBLE_EQ(sample.temperature_c, 28.5);
    ASSERT_TRUE(sample.cell_voltages_v.has_value());
    EXPECT_EQ(sample.cell_voltages_v->size(), 3);
}

TEST(CommonTypes_InternalTimeSeries, PopulateAndInspectMetadata) {
    InternalTimeSeries ts;
    ts.session_id = "sess_001";
    ts.pack_id = "pack_LFP_48V_30Ah";
    ts.chemistry = "LFP";
    ts.nominal_capacity_ah = 30.0;
    ts.metadata["fleet_id"] = "delhi_logistics_north";

    Sample s1{100.0, 3.2, 10.0, 35.0, std::nullopt};
    Sample s2{101.0, 3.25, 10.0, 35.2, std::nullopt};
    ts.samples.push_back(s1);
    ts.samples.push_back(s2);

    EXPECT_EQ(ts.samples.size(), 2);
    EXPECT_EQ(ts.chemistry, "LFP");
    EXPECT_DOUBLE_EQ(ts.nominal_capacity_ah, 30.0);
    EXPECT_EQ(ts.metadata.at("fleet_id"), "delhi_logistics_north");
}

TEST(CommonTypes_Result, SuccessAndFailureHandling) {
    auto ok_func = []() -> Result<int> {
        return 42;
    };
    auto err_func = []() -> Result<int> {
        return std::unexpected(PipelineError{
            .module = "ingestion",
            .code = "INVALID_PAYLOAD",
            .message = "Payload is corrupted",
            .context = "packet_id=12"
        });
    };

    auto res_ok = ok_func();
    ASSERT_TRUE(res_ok.has_value());
    EXPECT_EQ(*res_ok, 42);

    auto res_err = err_func();
    ASSERT_FALSE(res_err.has_value());
    EXPECT_EQ(res_err.error().module, "ingestion");
    EXPECT_EQ(res_err.error().code, "INVALID_PAYLOAD");
}
