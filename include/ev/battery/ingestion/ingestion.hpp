#pragma once

/// @file ingestion.hpp
/// @brief M01: Telemetry Data Ingestion & Schema Normalization Engine.
/// Parses CSV, JSON, and OEM telematics payloads, normalizes raw sensor units (mV, mA, deci-degC),
/// and constructs contiguous InternalTimeSeries structures with zero runtime heap fragmentation.

#include <ev/battery/common/types.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace ev::battery::ingestion {

/**
 * @brief Column mapping and scaling parameters for CSV ingestion.
 */
struct CsvColumnMapping {
    std::string timestamp_col = "timestamp";
    std::string voltage_col = "voltage";
    std::string current_col = "current";
    std::string temp_col = "temperature";
    std::string soc_col = "soc";
    double voltage_scale = 1.0;          ///< E.g. 0.001 if source is in millivolts (mV)
    double current_scale = 1.0;          ///< E.g. 0.001 if source is in milliamperes (mA)
    double temp_scale = 1.0;             ///< E.g. 0.1 if source is in deci-degrees Celsius (d°C)
    bool timestamp_is_millis = false;    ///< If true, timestamp is divided by 1000.0s
    char delimiter = ',';                ///< Field delimiter: ',' or ';'
};

/**
 * @brief Parse CSV telemetry content into a standardized InternalTimeSeries.
 *
 * Inspects header row for matching column names (case-insensitive, trims quotes)
 * and normalizes rows into standardized SI float samples.
 *
 * @param csv_content Raw CSV string buffer.
 * @param mapping Column names, delimiter, and scaling factors.
 * @return InternalTimeSeries or PipelineError on syntax/format errors.
 */
[[nodiscard]] common::Result<common::InternalTimeSeries> parseCsv(
    std::string_view csv_content,
    const CsvColumnMapping& mapping = {}
);

/**
 * @brief Parse JSON telemetry payload into a standardized InternalTimeSeries.
 *
 * Supports both JSON arrays of objects and single telemetry records.
 * Recognizes common OEM field aliases:
 * - Timestamp: "timestamp", "timestamp_s", "time", "t", "epoch_ms"
 * - Voltage: "voltage", "voltage_v", "v", "volt"
 * - Current: "current", "current_a", "i", "curr"
 * - Temperature: "temperature", "temperature_c", "temp", "temp_c", "t_c"
 * - SoC: "soc", "state_of_charge"
 *
 * @param json_content Raw JSON string buffer.
 * @return InternalTimeSeries or PipelineError on syntax/format errors.
 */
[[nodiscard]] common::Result<common::InternalTimeSeries> parseJson(
    std::string_view json_content
);

} // namespace ev::battery::ingestion
