#include <ev/battery/ingestion/ingestion.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace ev::battery::ingestion {

namespace {

std::string toLowerTrimmed(std::string_view sv) {
    while (!sv.empty() && (std::isspace(static_cast<unsigned char>(sv.front())) || sv.front() == '"' || sv.front() == '\'')) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && (std::isspace(static_cast<unsigned char>(sv.back())) || sv.back() == '"' || sv.back() == '\'')) {
        sv.remove_suffix(1);
    }
    std::string res;
    res.reserve(sv.size());
    for (char c : sv) {
        res.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return res;
}

std::vector<std::string_view> splitLine(std::string_view line, char delimiter) {
    std::vector<std::string_view> tokens;
    size_t start = 0;
    while (start < line.size()) {
        size_t end = line.find(delimiter, start);
        if (end == std::string_view::npos) {
            tokens.push_back(line.substr(start));
            break;
        }
        tokens.push_back(line.substr(start, end - start));
        start = end + 1;
    }
    if (!line.empty() && line.back() == delimiter) {
        tokens.push_back("");
    }
    return tokens;
}

double parseDouble(std::string_view sv, double default_val = 0.0) {
    while (!sv.empty() && (std::isspace(static_cast<unsigned char>(sv.front())) || sv.front() == '"')) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && (std::isspace(static_cast<unsigned char>(sv.back())) || sv.back() == '"')) {
        sv.remove_suffix(1);
    }
    if (sv.empty()) return default_val;

    std::string s(sv);
    char* end = nullptr;
    double val = std::strtod(s.c_str(), &end);
    if (end == s.c_str()) return default_val;
    return val;
}

} // namespace

common::Result<common::InternalTimeSeries> parseCsv(
    std::string_view csv_content,
    const CsvColumnMapping& mapping
) {
    if (csv_content.empty()) {
        return std::unexpected(common::PipelineError{
            .module = "ingestion",
            .code = "EMPTY_CSV",
            .message = "CSV content is empty",
            .context = ""
        });
    }

    common::InternalTimeSeries series;

    size_t pos = 0;
    bool header_processed = false;

    int idx_time = -1;
    int idx_volt = -1;
    int idx_curr = -1;
    int idx_temp = -1;
    int idx_soc = -1;

    std::string target_time = toLowerTrimmed(mapping.timestamp_col);
    std::string target_volt = toLowerTrimmed(mapping.voltage_col);
    std::string target_curr = toLowerTrimmed(mapping.current_col);
    std::string target_temp = toLowerTrimmed(mapping.temp_col);
    std::string target_soc  = toLowerTrimmed(mapping.soc_col);

    while (pos < csv_content.size()) {
        size_t next_line = csv_content.find('\n', pos);
        std::string_view raw_line = (next_line == std::string_view::npos)
                                    ? csv_content.substr(pos)
                                    : csv_content.substr(pos, next_line - pos);
        pos = (next_line == std::string_view::npos) ? csv_content.size() : next_line + 1;

        if (!raw_line.empty() && raw_line.back() == '\r') {
            raw_line.remove_suffix(1);
        }

        // Skip completely empty lines
        if (raw_line.empty() || std::all_of(raw_line.begin(), raw_line.end(), [](char c){ return std::isspace(static_cast<unsigned char>(c)); })) {
            continue;
        }

        auto tokens = splitLine(raw_line, mapping.delimiter);

        if (!header_processed) {
            for (size_t i = 0; i < tokens.size(); ++i) {
                std::string col = toLowerTrimmed(tokens[i]);
                if (col == target_time || col == "timestamp" || col == "time" || col == "t" || col == "epoch") {
                    idx_time = static_cast<int>(i);
                } else if (col == target_volt || col == "voltage" || col == "v" || col == "volt" || col == "voltage_v") {
                    idx_volt = static_cast<int>(i);
                } else if (col == target_curr || col == "current" || col == "i" || col == "curr" || col == "current_a") {
                    idx_curr = static_cast<int>(i);
                } else if (col == target_temp || col == "temperature" || col == "temp" || col == "t_c" || col == "temp_c") {
                    idx_temp = static_cast<int>(i);
                } else if (col == target_soc || col == "soc") {
                    idx_soc = static_cast<int>(i);
                }
            }

            if (idx_time == -1 || idx_volt == -1 || idx_curr == -1) {
                return std::unexpected(common::PipelineError{
                    .module = "ingestion",
                    .code = "MISSING_MANDATORY_COLUMNS",
                    .message = "CSV header missing required columns (timestamp, voltage, or current)",
                    .context = std::string(raw_line)
                });
            }

            header_processed = true;
            continue;
        }

        // Data row
        int max_needed = std::max({idx_time, idx_volt, idx_curr});
        if (static_cast<int>(tokens.size()) <= max_needed) {
            continue; // Skip truncated rows
        }

        double raw_t = parseDouble(tokens[idx_time]);
        double raw_v = parseDouble(tokens[idx_volt]);
        double raw_i = parseDouble(tokens[idx_curr]);
        double raw_temp = (idx_temp >= 0 && idx_temp < static_cast<int>(tokens.size()))
                          ? parseDouble(tokens[idx_temp], 25.0)
                          : 25.0;

        double final_t = mapping.timestamp_is_millis ? (raw_t / 1000.0) : raw_t;
        double final_v = raw_v * mapping.voltage_scale;
        double final_i = raw_i * mapping.current_scale;
        double final_temp = raw_temp * mapping.temp_scale;

        common::Sample sample{
            .timestamp_s = final_t,
            .voltage_v = final_v,
            .current_a = final_i,
            .temperature_c = final_temp
        };
        series.samples.push_back(sample);
    }

    if (series.samples.empty()) {
        return std::unexpected(common::PipelineError{
            .module = "ingestion",
            .code = "NO_VALID_SAMPLES",
            .message = "No valid data rows found in CSV content",
            .context = ""
        });
    }

    return series;
}

common::Result<common::InternalTimeSeries> parseJson(
    std::string_view json_content
) {
    if (json_content.empty()) {
        return std::unexpected(common::PipelineError{
            .module = "ingestion",
            .code = "EMPTY_JSON",
            .message = "JSON content is empty",
            .context = ""
        });
    }

    common::InternalTimeSeries series;

    // Fast zero-dependency scanner for JSON objects { ... }
    size_t pos = 0;
    while (pos < json_content.size()) {
        size_t obj_start = json_content.find('{', pos);
        if (obj_start == std::string_view::npos) break;

        size_t obj_end = json_content.find('}', obj_start);
        if (obj_end == std::string_view::npos) break;

        std::string_view obj_str = json_content.substr(obj_start + 1, obj_end - obj_start - 1);
        pos = obj_end + 1;

        // Parse key-value pairs in obj_str
        double t_val = 0.0;
        double v_val = 0.0;
        double i_val = 0.0;
        double temp_val = 25.0;
        bool has_t = false;
        bool has_v = false;
        bool has_i = false;

        size_t kv_pos = 0;
        while (kv_pos < obj_str.size()) {
            size_t k_start = obj_str.find('"', kv_pos);
            if (k_start == std::string_view::npos) break;
            size_t k_end = obj_str.find('"', k_start + 1);
            if (k_end == std::string_view::npos) break;

            std::string_view key = obj_str.substr(k_start + 1, k_end - k_start - 1);
            size_t colon = obj_str.find(':', k_end + 1);
            if (colon == std::string_view::npos) break;

            // Find end of value (comma or end of obj)
            size_t val_start = colon + 1;
            while (val_start < obj_str.size() && std::isspace(static_cast<unsigned char>(obj_str[val_start]))) {
                val_start++;
            }
            size_t val_end = obj_str.find(',', val_start);
            if (val_end == std::string_view::npos) {
                val_end = obj_str.size();
            }

            std::string_view val_sv = obj_str.substr(val_start, val_end - val_start);
            kv_pos = val_end + 1;

            double num_val = parseDouble(val_sv);

            std::string lk = toLowerTrimmed(key);
            if (lk == "timestamp" || lk == "timestamp_s" || lk == "time" || lk == "t" || lk == "epoch") {
                t_val = num_val;
                has_t = true;
            } else if (lk == "epoch_ms" || lk == "time_ms") {
                t_val = num_val / 1000.0;
                has_t = true;
            } else if (lk == "voltage" || lk == "voltage_v" || lk == "v" || lk == "volt") {
                v_val = num_val;
                has_v = true;
            } else if (lk == "voltage_mv" || lk == "v_mv") {
                v_val = num_val * 0.001;
                has_v = true;
            } else if (lk == "current" || lk == "current_a" || lk == "i" || lk == "curr") {
                i_val = num_val;
                has_i = true;
            } else if (lk == "current_ma" || lk == "i_ma") {
                i_val = num_val * 0.001;
                has_i = true;
            } else if (lk == "temperature" || lk == "temperature_c" || lk == "temp" || lk == "temp_c" || lk == "t_c") {
                temp_val = num_val;
            } else if (lk == "temp_dc") {
                temp_val = num_val * 0.1;
            }
        }

        if (has_t && has_v && has_i) {
            series.samples.push_back({
                .timestamp_s = t_val,
                .voltage_v = v_val,
                .current_a = i_val,
                .temperature_c = temp_val
            });
        }
    }

    if (series.samples.empty()) {
        return std::unexpected(common::PipelineError{
            .module = "ingestion",
            .code = "NO_VALID_JSON_SAMPLES",
            .message = "No valid telemetry objects found in JSON content",
            .context = ""
        });
    }

    return series;
}

} // namespace ev::battery::ingestion
