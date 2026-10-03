#include <gtest/gtest.h>
#include <ev/battery/ingestion/ingestion.hpp>

using namespace ev::battery;

TEST(IngestionTest, ParseStandardCsv) {
    std::string csv_data =
        "timestamp,voltage,current,temperature\n"
        "0.0,51.2,15.0,28.5\n"
        "1.0,51.25,15.1,28.6\n"
        "2.0,51.30,15.0,28.7\n";

    auto res = ingestion::parseCsv(csv_data);
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res->samples.size(), 3);
    EXPECT_DOUBLE_EQ(res->samples[0].timestamp_s, 0.0);
    EXPECT_DOUBLE_EQ(res->samples[0].voltage_v, 51.2);
    EXPECT_DOUBLE_EQ(res->samples[0].current_a, 15.0);
    EXPECT_DOUBLE_EQ(res->samples[0].temperature_c, 28.5);

    EXPECT_DOUBLE_EQ(res->samples[2].timestamp_s, 2.0);
    EXPECT_DOUBLE_EQ(res->samples[2].voltage_v, 51.30);
}

TEST(IngestionTest, ParseSemicolonCsvWithScaledUnits) {
    std::string csv_data =
        "time;volt_mv;curr_ma;t_dc\n"
        "1000;51200;15000;290\n"
        "2000;51250;15050;291\n";

    ingestion::CsvColumnMapping mapping;
    mapping.delimiter = ';';
    mapping.timestamp_col = "time";
    mapping.voltage_col = "volt_mv";
    mapping.current_col = "curr_ma";
    mapping.temp_col = "t_dc";
    mapping.voltage_scale = 0.001; // mV to V
    mapping.current_scale = 0.001; // mA to A
    mapping.temp_scale = 0.1;      // deci-degC to degC
    mapping.timestamp_is_millis = true; // ms to s

    auto res = ingestion::parseCsv(csv_data, mapping);
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res->samples.size(), 2);
    EXPECT_DOUBLE_EQ(res->samples[0].timestamp_s, 1.0);
    EXPECT_DOUBLE_EQ(res->samples[0].voltage_v, 51.2);
    EXPECT_DOUBLE_EQ(res->samples[0].current_a, 15.0);
    EXPECT_DOUBLE_EQ(res->samples[0].temperature_c, 29.0);
}

TEST(IngestionTest, MissingMandatoryColumnsReturnsError) {
    std::string csv_data =
        "timestamp,temperature\n"
        "0.0,28.5\n";

    auto res = ingestion::parseCsv(csv_data);
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(res.error().code, "MISSING_MANDATORY_COLUMNS");
}

TEST(IngestionTest, EmptyCsvReturnsError) {
    auto res = ingestion::parseCsv("");
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(res.error().code, "EMPTY_CSV");
}

TEST(IngestionTest, ParseJsonArrayOfSamples) {
    std::string json_data = R"([
        {"timestamp_s": 0.0, "voltage_v": 51.2, "current_a": 12.5, "temp_c": 32.0},
        {"timestamp_s": 1.0, "voltage_v": 51.3, "current_a": 12.5, "temp_c": 32.1},
        {"timestamp_s": 2.0, "voltage_v": 51.4, "current_a": 12.5, "temp_c": 32.2}
    ])";

    auto res = ingestion::parseJson(json_data);
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res->samples.size(), 3);
    EXPECT_DOUBLE_EQ(res->samples[1].timestamp_s, 1.0);
    EXPECT_DOUBLE_EQ(res->samples[1].voltage_v, 51.3);
    EXPECT_DOUBLE_EQ(res->samples[1].current_a, 12.5);
    EXPECT_DOUBLE_EQ(res->samples[1].temperature_c, 32.1);
}

TEST(IngestionTest, ParseJsonOemAliasesAndUnits) {
    std::string json_data = R"(
        {"t": 10.0, "v_mv": 51200.0, "i_ma": 15000.0, "temp": 35.0}
    )";

    auto res = ingestion::parseJson(json_data);
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res->samples.size(), 1);
    EXPECT_DOUBLE_EQ(res->samples[0].timestamp_s, 10.0);
    EXPECT_DOUBLE_EQ(res->samples[0].voltage_v, 51.2);
    EXPECT_DOUBLE_EQ(res->samples[0].current_a, 15.0);
    EXPECT_DOUBLE_EQ(res->samples[0].temperature_c, 35.0);
}

TEST(IngestionTest, EmptyJsonReturnsError) {
    auto res = ingestion::parseJson("");
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(res.error().code, "EMPTY_JSON");
}
