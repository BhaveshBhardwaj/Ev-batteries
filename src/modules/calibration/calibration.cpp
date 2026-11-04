#include <ev/battery/calibration/calibration.hpp>
#include <ev/battery/math_utils/calculus.hpp>
#include <ev/battery/math_utils/statistics.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace ev::battery::calibration {

namespace {

// Helper: Compute analytical cell OCV for LFP (Safari & Delacourt 2011)
[[nodiscard]] double analyticalLfpCellOcv(double soc) noexcept {
    const double z = std::clamp(soc, 0.001, 0.999);
    const double base = 3.25;
    const double slope = 0.12 * z;
    const double lower_knee = -0.08 * std::exp(-35.0 * z);
    const double upper_knee = 0.28 * std::exp(45.0 * (z - 1.0));
    const double phase1 = 0.035 * std::tanh(25.0 * (z - 0.22));
    const double phase2 = 0.025 * std::tanh(30.0 * (z - 0.55));
    const double phase3 = 0.020 * std::tanh(35.0 * (z - 0.78));
    return base + slope + lower_knee + upper_knee + phase1 + phase2 + phase3;
}

// Helper: Compute analytical cell OCV for NMC (3.0V - 4.2V)
[[nodiscard]] double analyticalNmcCellOcv(double soc) noexcept {
    const double z = std::clamp(soc, 0.001, 0.999);
    return 3.0 + 1.15 * z + 0.05 * std::log(z + 0.01) - 0.05 * std::exp(25.0 * (z - 1.0));
}

struct RestInterval {
    size_t start_idx = 0;
    size_t end_idx = 0;
    double start_time_s = 0.0;
    double end_time_s = 0.0;
    double duration_s = 0.0;
    double settled_voltage_v = 0.0;
    double estimated_soc = 0.0;
};

} // namespace

std::vector<OcvPoint> getDefaultLfp16sOcvTable() {
    return getScaledOcvTable("LFP", 16);
}

std::vector<OcvPoint> getScaledOcvTable(std::string_view chemistry, int series_cells) {
    constexpr size_t kPoints = 250;
    const double n_cells = (series_cells > 0) ? static_cast<double>(series_cells) : (chemistry == "NMC" ? 14.0 : 16.0);
    std::vector<OcvPoint> table;
    table.reserve(kPoints);

    double prev_v = 0.0;
    for (size_t i = 0; i < kPoints; ++i) {
        double soc = 0.001 + (static_cast<double>(i) / static_cast<double>(kPoints - 1)) * (0.999 - 0.001);
        double cell_v = (chemistry == "NMC") ? analyticalNmcCellOcv(soc) : analyticalLfpCellOcv(soc);
        double v = cell_v * n_cells;
        if (i > 0 && v <= prev_v) {
            v = prev_v + 1e-5; // Enforce strict monotonicity
        }
        prev_v = v;
        table.push_back(OcvPoint{.soc = soc, .voltage_v = v});
    }
    return table;
}

common::Result<std::vector<OcvPoint>> loadOcvTableFromCsv(
    const std::string& csv_path,
    bool is_pack_voltage,
    int series_cells
) {
    std::ifstream file(csv_path);
    if (!file.is_open()) {
        return std::unexpected(common::PipelineError{
            .module = "calibration",
            .code = "FILE_NOT_FOUND",
            .message = "Failed to open reference OCV CSV file: " + csv_path,
            .context = csv_path
        });
    }

    std::vector<OcvPoint> table;
    std::string line;
    bool has_header = false;
    int soc_col = 0;
    int volt_col = is_pack_voltage ? 2 : 1;

    // Check header
    if (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string token;
        std::vector<std::string> headers;
        while (std::getline(ss, token, ',')) {
            // Trim whitespace
            size_t first = token.find_first_not_of(" \t\r\n");
            size_t last = token.find_last_not_of(" \t\r\n");
            if (first != std::string::npos && last != std::string::npos) {
                headers.push_back(token.substr(first, last - first + 1));
            } else {
                headers.push_back("");
            }
        }

        bool contains_text = false;
        for (const auto& h : headers) {
            for (char c : h) {
                if (std::isalpha(static_cast<unsigned char>(c))) {
                    contains_text = true;
                    break;
                }
            }
        }

        if (contains_text) {
            has_header = true;
            for (size_t i = 0; i < headers.size(); ++i) {
                if (headers[i] == "soc") soc_col = static_cast<int>(i);
                if (is_pack_voltage && (headers[i] == "pack_16s_ocv_v" || headers[i] == "pack_ocv_v" || headers[i] == "voltage_v")) {
                    volt_col = static_cast<int>(i);
                } else if (!is_pack_voltage && (headers[i] == "cell_ocv_v" || headers[i] == "voltage_v")) {
                    volt_col = static_cast<int>(i);
                }
            }
        } else {
            // No header, rewind
            file.clear();
            file.seekg(0);
        }
    }

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string token;
        std::vector<double> values;
        while (std::getline(ss, token, ',')) {
            try {
                values.push_back(std::stod(token));
            } catch (...) {
                // Ignore parse errors on malformed lines
            }
        }
        if (values.size() > static_cast<size_t>(std::max(soc_col, volt_col))) {
            double soc = values[soc_col];
            double volt = values[volt_col];
            if (!is_pack_voltage) {
                volt *= static_cast<double>(series_cells);
            }
            table.push_back(OcvPoint{.soc = soc, .voltage_v = volt});
        }
    }

    if (table.size() < 2) {
        return std::unexpected(common::PipelineError{
            .module = "calibration",
            .code = "INSUFFICIENT_DATA",
            .message = "OCV CSV contains fewer than 2 valid points",
            .context = csv_path
        });
    }

    // Sort table by voltage
    std::sort(table.begin(), table.end(), [](const OcvPoint& a, const OcvPoint& b) {
        return a.voltage_v < b.voltage_v;
    });

    return table;
}

double lookupSocFromOcv(double voltage_v, const std::vector<OcvPoint>& ocv_table) noexcept {
    if (ocv_table.empty()) return 0.5;
    if (voltage_v <= ocv_table.front().voltage_v) return ocv_table.front().soc;
    if (voltage_v >= ocv_table.back().voltage_v) return ocv_table.back().soc;

    auto it = std::lower_bound(
        ocv_table.begin(), ocv_table.end(), voltage_v,
        [](const OcvPoint& pt, double val) {
            return pt.voltage_v < val;
        }
    );

    if (it == ocv_table.begin()) return it->soc;
    if (it == ocv_table.end()) return ocv_table.back().soc;

    auto prev = it - 1;
    const double dv = it->voltage_v - prev->voltage_v;
    if (std::abs(dv) < 1e-9) return prev->soc;

    const double frac = (voltage_v - prev->voltage_v) / dv;
    return prev->soc + frac * (it->soc - prev->soc);
}

double lookupOcvFromSoc(double soc, const std::vector<OcvPoint>& ocv_table) noexcept {
    if (ocv_table.empty()) return 0.0;
    if (soc <= ocv_table.front().soc) return ocv_table.front().voltage_v;
    if (soc >= ocv_table.back().soc) return ocv_table.back().voltage_v;

    auto it = std::lower_bound(
        ocv_table.begin(), ocv_table.end(), soc,
        [](const OcvPoint& pt, double val) {
            return pt.soc < val;
        }
    );

    if (it == ocv_table.begin()) return it->voltage_v;
    if (it == ocv_table.end()) return ocv_table.back().voltage_v;

    auto prev = it - 1;
    const double d_soc = it->soc - prev->soc;
    if (std::abs(d_soc) < 1e-9) return prev->voltage_v;

    const double frac = (soc - prev->soc) / d_soc;
    return prev->voltage_v + frac * (it->voltage_v - prev->voltage_v);
}

common::Result<std::pair<common::InternalTimeSeries, CalibrationReport>> calibrateTimeSeries(
    const common::InternalTimeSeries& series,
    const std::vector<segmentation::Segment>& segments,
    const CalibrationConfig& config
) {
    if (series.samples.empty()) {
        return std::unexpected(common::PipelineError{
            .module = "calibration",
            .code = "EMPTY_INPUT",
            .message = "Telemetry series is empty",
            .context = series.session_id
        });
    }

    CalibrationReport report;
    const auto& samples = series.samples;
    const size_t n_samples = samples.size();

    // Nominal capacity resolution
    double nominal_cap_ah = config.nominal_capacity_ah;
    if (series.nominal_capacity_ah > 0.0) {
        nominal_cap_ah = series.nominal_capacity_ah;
    }

    // Reference OCV table resolution
    std::vector<OcvPoint> ocv_table = config.ocv_table;
    if (ocv_table.empty()) {
        std::string chem = series.chemistry.empty() ? "LFP" : series.chemistry;
        int n_cells = (chem == "NMC") ? 14 : 16;
        if (!series.samples.empty()) {
            double v_sample = series.samples.front().voltage_v;
            if (v_sample < 5.0) {
                n_cells = 1; // Single cell laboratory test
            }
        }
        ocv_table = getScaledOcvTable(chem, n_cells);
    }

    // ─────────────────────────────────────────────────────────────
    // 1. Identify Valid Resting Intervals
    // ─────────────────────────────────────────────────────────────
    std::vector<RestInterval> rest_intervals;

    // Check if user-provided segments contain REST regimes
    for (const auto& seg : segments) {
        bool is_rest_candidate = false;
        if (seg.type == common::SegmentType::REST) {
            is_rest_candidate = true;
        } else if (std::abs(seg.mean_current_a) <= config.max_candidate_rest_current_a &&
                   seg.std_current_a <= 0.35) {
            // Relaxed check if segmentation misclassified rest due to large sensor offset
            is_rest_candidate = true;
        }

        if (is_rest_candidate && seg.duration_s >= config.min_rest_duration_s) {
            if (seg.start_index < n_samples && seg.end_index < n_samples && seg.start_index <= seg.end_index) {
                // Calculate settled tail voltage (last 15 samples or 25% of rest segment)
                size_t tail_len = std::min<size_t>(15, seg.end_index - seg.start_index + 1);
                size_t tail_start = seg.end_index - tail_len + 1;
                double v_sum = 0.0;
                for (size_t k = tail_start; k <= seg.end_index; ++k) {
                    v_sum += samples[k].voltage_v;
                }
                double v_settled = v_sum / static_cast<double>(tail_len);
                double soc = lookupSocFromOcv(v_settled, ocv_table);

                rest_intervals.push_back(RestInterval{
                    .start_idx = seg.start_index,
                    .end_idx = seg.end_index,
                    .start_time_s = samples[seg.start_index].timestamp_s,
                    .end_time_s = samples[seg.end_index].timestamp_s,
                    .duration_s = seg.duration_s,
                    .settled_voltage_v = v_settled,
                    .estimated_soc = soc
                });
            }
        }
    }

    // Fallback: If no segments were provided or none met rest criteria, scan series directly
    if (rest_intervals.empty() && n_samples >= 2) {
        size_t run_start = 0;
        bool in_run = false;

        for (size_t i = 0; i < n_samples; ++i) {
            bool matches_rest = (std::abs(samples[i].current_a) <= config.max_candidate_rest_current_a);

            if (matches_rest) {
                if (!in_run) {
                    run_start = i;
                    in_run = true;
                }
            } else {
                if (in_run) {
                    double dur_s = samples[i - 1].timestamp_s - samples[run_start].timestamp_s;
                    if (dur_s >= config.min_rest_duration_s) {
                        size_t tail_len = std::min<size_t>(15, (i - 1) - run_start + 1);
                        size_t tail_start = (i - 1) - tail_len + 1;
                        double v_sum = 0.0;
                        for (size_t k = tail_start; k <= i - 1; ++k) {
                            v_sum += samples[k].voltage_v;
                        }
                        double v_settled = v_sum / static_cast<double>(tail_len);
                        double soc = lookupSocFromOcv(v_settled, ocv_table);

                        rest_intervals.push_back(RestInterval{
                            .start_idx = run_start,
                            .end_idx = i - 1,
                            .start_time_s = samples[run_start].timestamp_s,
                            .end_time_s = samples[i - 1].timestamp_s,
                            .duration_s = dur_s,
                            .settled_voltage_v = v_settled,
                            .estimated_soc = soc
                        });
                    }
                    in_run = false;
                }
            }
        }

        // Close trailing run
        if (in_run) {
            double dur_s = samples.back().timestamp_s - samples[run_start].timestamp_s;
            if (dur_s >= config.min_rest_duration_s) {
                size_t tail_len = std::min<size_t>(15, (n_samples - 1) - run_start + 1);
                size_t tail_start = (n_samples - 1) - tail_len + 1;
                double v_sum = 0.0;
                for (size_t k = tail_start; k < n_samples; ++k) {
                    v_sum += samples[k].voltage_v;
                }
                double v_settled = v_sum / static_cast<double>(tail_len);
                double soc = lookupSocFromOcv(v_settled, ocv_table);

                rest_intervals.push_back(RestInterval{
                    .start_idx = run_start,
                    .end_idx = n_samples - 1,
                    .start_time_s = samples[run_start].timestamp_s,
                    .end_time_s = samples.back().timestamp_s,
                    .duration_s = dur_s,
                    .settled_voltage_v = v_settled,
                    .estimated_soc = soc
                });
            }
        }
    }

    // ─────────────────────────────────────────────────────────────
    // 2. Regress Zero-Current Bias (alpha_hat)
    // ─────────────────────────────────────────────────────────────
    double total_rest_current_sum = 0.0;
    size_t total_rest_sample_count = 0;

    for (const auto& interval : rest_intervals) {
        for (size_t k = interval.start_idx; k <= interval.end_idx; ++k) {
            total_rest_current_sum += samples[k].current_a;
            ++total_rest_sample_count;
        }
    }

    if (total_rest_sample_count > 0) {
        report.estimated_offset_a = total_rest_current_sum / static_cast<double>(total_rest_sample_count);
    } else {
        report.estimated_offset_a = 0.0;
    }

    // ─────────────────────────────────────────────────────────────
    // 3. Regress Gain Scaling Factor (beta_hat) via Dual OCV Anchors
    // ─────────────────────────────────────────────────────────────
    double max_soc_delta = 0.0;
    bool found_valid_pair = false;
    double best_beta = 1.0;
    double best_initial_soc = 0.0;
    double best_final_soc = 0.0;

    if (rest_intervals.size() >= 2) {
        for (size_t i = 0; i < rest_intervals.size(); ++i) {
            for (size_t j = i + 1; j < rest_intervals.size(); ++j) {
                const auto& anchor1 = rest_intervals[i];
                const auto& anchor2 = rest_intervals[j];

                double delta_soc = anchor2.estimated_soc - anchor1.estimated_soc;
                double abs_delta_soc = std::abs(delta_soc);

                if (abs_delta_soc >= config.min_soc_delta_for_gain) {
                    // Integrate measured current between end of anchor 1 and end of anchor 2
                    size_t idx1 = anchor1.end_idx;
                    size_t idx2 = anchor2.end_idx;
                    if (idx2 <= idx1) continue;

                    double q_meas_as = 0.0;
                    for (size_t k = idx1; k < idx2; ++k) {
                        double dt = samples[k + 1].timestamp_s - samples[k].timestamp_s;
                        q_meas_as += 0.5 * (samples[k].current_a + samples[k + 1].current_a) * dt;
                    }
                    double q_meas_ah = q_meas_as / 3600.0;
                    double delta_t_h = (samples[idx2].timestamp_s - samples[idx1].timestamp_s) / 3600.0;

                    // Subtract offset contribution
                    double q_net_ah = q_meas_ah - report.estimated_offset_a * delta_t_h;
                    double q_true_ah = nominal_cap_ah * delta_soc;

                    if (std::abs(q_true_ah) > 1e-4) {
                        double beta_candidate = q_net_ah / q_true_ah;
                        if (beta_candidate > 0.0) {
                            if (abs_delta_soc > max_soc_delta) {
                                max_soc_delta = abs_delta_soc;
                                best_beta = beta_candidate;
                                best_initial_soc = anchor1.estimated_soc;
                                best_final_soc = anchor2.estimated_soc;
                                found_valid_pair = true;
                            }
                        }
                    }
                }
            }
        }
    }

    if (found_valid_pair) {
        report.dual_anchors_found = true;
        report.estimated_gain = best_beta;
        report.initial_soc = best_initial_soc;
        report.final_soc = best_final_soc;
    } else {
        report.dual_anchors_found = false;
        report.estimated_gain = 1.0;
        if (!rest_intervals.empty()) {
            report.initial_soc = rest_intervals.front().estimated_soc;
            report.final_soc = rest_intervals.back().estimated_soc;
        }
    }

    // ─────────────────────────────────────────────────────────────
    // 4. Enforce Gate 2 Diagnostics & Hardware Fault Limits
    // ─────────────────────────────────────────────────────────────
    if (std::abs(report.estimated_offset_a) > config.max_allowed_offset_a) {
        report.sensor_fault_detected = true;
        report.fault_message += "ALERT_SENSOR_OFFSET_EXCESSIVE: Offset magnitude " +
            std::to_string(std::abs(report.estimated_offset_a)) + " A exceeds limit " +
            std::to_string(config.max_allowed_offset_a) + " A. ";
    }

    if (std::abs(report.estimated_gain - 1.0) > config.max_allowed_gain_error) {
        report.sensor_fault_detected = true;
        report.fault_message += "ALERT_SENSOR_GAIN_EXCESSIVE: Gain error magnitude " +
            std::to_string(std::abs(report.estimated_gain - 1.0)) + " exceeds limit " +
            std::to_string(config.max_allowed_gain_error) + ". ";
    }

    // ─────────────────────────────────────────────────────────────
    // 5. Generate Calibrated Series & Coulomb Integration
    // ─────────────────────────────────────────────────────────────
    common::InternalTimeSeries calibrated_series = series;
    double raw_coulomb = 0.0;
    double cal_coulomb = 0.0;

    const double alpha = report.estimated_offset_a;
    const double beta = (report.estimated_gain > 1e-4) ? report.estimated_gain : 1.0;

    for (size_t i = 0; i < n_samples; ++i) {
        double i_meas = samples[i].current_a;
        double i_corr = (i_meas - alpha) / beta;
        calibrated_series.samples[i].current_a = i_corr;

        if (i > 0) {
            double dt_h = (samples[i].timestamp_s - samples[i - 1].timestamp_s) / 3600.0;
            double prev_meas = samples[i - 1].current_a;
            double prev_corr = calibrated_series.samples[i - 1].current_a;

            raw_coulomb += 0.5 * (prev_meas + i_meas) * dt_h;
            cal_coulomb += 0.5 * (prev_corr + i_corr) * dt_h;
        }
    }

    report.raw_coulomb_ah = raw_coulomb;
    report.calibrated_coulomb_ah = cal_coulomb;

    return std::make_pair(std::move(calibrated_series), report);
}

} // namespace ev::battery::calibration
