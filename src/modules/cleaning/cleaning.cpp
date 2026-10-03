#include <ev/battery/cleaning/cleaning.hpp>
#include <algorithm>
#include <cmath>

namespace ev::battery::cleaning {

common::Result<size_t> hampelFilter(
    std::span<const double> input,
    std::span<double> output,
    size_t half_window,
    double n_sigma
) {
    if (input.empty()) {
        return std::unexpected(common::PipelineError{
            .module = "cleaning",
            .code = "INVALID_ARGUMENT",
            .message = cleaningErrorToString(CleaningError::EMPTY_INPUT)
        });
    }

    if (output.size() != input.size()) {
        return std::unexpected(common::PipelineError{
            .module = "cleaning",
            .code = "INVALID_ARGUMENT",
            .message = cleaningErrorToString(CleaningError::SIZE_MISMATCH)
        });
    }

    if (half_window == 0) {
        return std::unexpected(common::PipelineError{
            .module = "cleaning",
            .code = "INVALID_ARGUMENT",
            .message = cleaningErrorToString(CleaningError::INVALID_WINDOW_SIZE)
        });
    }

    const size_t n = input.size();
    size_t outliers_replaced = 0;
    const double mad_scale = 1.4826;

    // Buffer to avoid reallocating during window calculations
    std::vector<double> window_buf;
    window_buf.reserve(2 * half_window + 1);

    for (size_t i = 0; i < n; ++i) {
        const size_t start = (i >= half_window) ? (i - half_window) : 0;
        const size_t end = std::min(n, i + half_window + 1);

        window_buf.assign(input.begin() + start, input.begin() + end);

        const double med = math_utils::median(window_buf);
        const double mad = math_utils::medianAbsoluteDeviation(window_buf);
        const double threshold = n_sigma * mad_scale * mad;

        const double diff = std::abs(input[i] - med);
        if (mad > 1e-12 && diff > threshold) {
            output[i] = med;
            ++outliers_replaced;
        } else {
            output[i] = input[i];
        }
    }

    return outliers_replaced;
}

common::Result<std::vector<StuckSensorRegion>> detectStuckSensor(
    std::span<const double> values,
    std::span<const double> timestamps_s,
    size_t min_duration_samples,
    double tolerance
) {
    if (values.size() != timestamps_s.size()) {
        return std::unexpected(common::PipelineError{
            .module = "cleaning",
            .code = "INVALID_ARGUMENT",
            .message = cleaningErrorToString(CleaningError::SIZE_MISMATCH)
        });
    }

    if (values.empty()) {
        return std::vector<StuckSensorRegion>{};
    }

    if (min_duration_samples < 2) {
        return std::unexpected(common::PipelineError{
            .module = "cleaning",
            .code = "INVALID_ARGUMENT",
            .message = cleaningErrorToString(CleaningError::INVALID_WINDOW_SIZE)
        });
    }

    std::vector<StuckSensorRegion> regions;
    const size_t n = values.size();
    size_t run_start = 0;

    for (size_t i = 1; i <= n; ++i) {
        bool is_break = false;
        if (i == n) {
            is_break = true;
        } else if (std::abs(values[i] - values[run_start]) > tolerance) {
            is_break = true;
        }

        if (is_break) {
            const size_t run_len = i - run_start;
            if (run_len >= min_duration_samples) {
                regions.push_back(StuckSensorRegion{
                    .start_index = run_start,
                    .end_index = i - 1,
                    .stuck_value = values[run_start],
                    .duration_s = timestamps_s[i - 1] - timestamps_s[run_start]
                });
            }
            run_start = i;
        }
    }

    return regions;
}

common::Result<std::pair<common::InternalTimeSeries, size_t>> repairTimestampGaps(
    const common::InternalTimeSeries& series,
    double nominal_dt_s,
    double max_gap_s
) {
    if (nominal_dt_s <= 0.0) {
        return std::unexpected(common::PipelineError{
            .module = "cleaning",
            .code = "INVALID_ARGUMENT",
            .message = cleaningErrorToString(CleaningError::INVALID_SAMPLING_INTERVAL)
        });
    }

    if (series.samples.size() < 2) {
        return std::make_pair(series, 0);
    }

    // Verify monotonicity
    for (size_t i = 1; i < series.samples.size(); ++i) {
        if (series.samples[i].timestamp_s <= series.samples[i - 1].timestamp_s) {
            return std::unexpected(common::PipelineError{
                .module = "cleaning",
                .code = "DATA_CORRUPTION",
                .message = cleaningErrorToString(CleaningError::NON_MONOTONIC_TIMESTAMPS)
            });
        }
    }

    common::InternalTimeSeries repaired = series;
    repaired.samples.clear();
    repaired.samples.reserve(series.samples.size() * 11 / 10);

    size_t interpolated_count = 0;
    repaired.samples.push_back(series.samples[0]);

    for (size_t i = 1; i < series.samples.size(); ++i) {
        const auto& prev = series.samples[i - 1];
        const auto& curr = series.samples[i];
        const double dt = curr.timestamp_s - prev.timestamp_s;

        if (dt > 1.5 * nominal_dt_s && dt <= max_gap_s) {
            const auto missing_pts = static_cast<size_t>(std::round(dt / nominal_dt_s)) - 1;
            const double sub_dt = dt / static_cast<double>(missing_pts + 1);

            for (size_t k = 1; k <= missing_pts; ++k) {
                const double alpha = static_cast<double>(k) / static_cast<double>(missing_pts + 1);
                common::Sample syn{
                    .timestamp_s = prev.timestamp_s + k * sub_dt,
                    .voltage_v = prev.voltage_v + alpha * (curr.voltage_v - prev.voltage_v),
                    .current_a = prev.current_a + alpha * (curr.current_a - prev.current_a),
                    .temperature_c = prev.temperature_c + alpha * (curr.temperature_c - prev.temperature_c),
                    .cell_voltages_v = std::nullopt
                };
                repaired.samples.push_back(syn);
                ++interpolated_count;
            }
        }

        repaired.samples.push_back(curr);
    }

    return std::make_pair(repaired, interpolated_count);
}

common::Result<std::pair<common::InternalTimeSeries, CleaningReport>> cleanTimeSeries(
    const common::InternalTimeSeries& raw_series,
    double nominal_dt_s,
    double max_repairable_gap_s,
    size_t half_window,
    double n_sigma
) {
    if (raw_series.samples.empty()) {
        return std::unexpected(common::PipelineError{
            .module = "cleaning",
            .code = "INVALID_ARGUMENT",
            .message = cleaningErrorToString(CleaningError::EMPTY_INPUT)
        });
    }

    CleaningReport report;
    report.original_sample_count = raw_series.samples.size();

    // 1. Repair timestamp dropouts first to establish uniform grid
    auto gap_res = repairTimestampGaps(raw_series, nominal_dt_s, max_repairable_gap_s);
    if (!gap_res) {
        return std::unexpected(gap_res.error());
    }

    auto [repaired_series, interpolated_pts] = std::move(*gap_res);
    report.samples_interpolated = interpolated_pts;
    report.gaps_repaired = (interpolated_pts > 0) ? 1 : 0;

    const size_t n = repaired_series.samples.size();

    // Extract channels for contiguous processing
    std::vector<double> voltages(n);
    std::vector<double> currents(n);
    std::vector<double> timestamps(n);

    for (size_t i = 0; i < n; ++i) {
        voltages[i] = repaired_series.samples[i].voltage_v;
        currents[i] = repaired_series.samples[i].current_a;
        timestamps[i] = repaired_series.samples[i].timestamp_s;
    }

    // 2. Detect stuck sensors
    auto stuck_v_res = detectStuckSensor(voltages, timestamps, 30);
    if (stuck_v_res) {
        report.stuck_voltage_regions = std::move(*stuck_v_res);
    }
    auto stuck_i_res = detectStuckSensor(currents, timestamps, 30);
    if (stuck_i_res) {
        report.stuck_current_regions = std::move(*stuck_i_res);
    }

    // 3. Hampel filtering for spike removal on voltage
    std::vector<double> clean_voltages(n);
    auto h_v_res = hampelFilter(voltages, clean_voltages, half_window, n_sigma);
    if (!h_v_res) {
        return std::unexpected(h_v_res.error());
    }
    report.voltage_spikes_removed = *h_v_res;

    // 4. Hampel filtering for spike removal on current
    std::vector<double> clean_currents(n);
    auto h_i_res = hampelFilter(currents, clean_currents, half_window, n_sigma);
    if (!h_i_res) {
        return std::unexpected(h_i_res.error());
    }
    report.current_spikes_removed = *h_i_res;

    // 5. Reassemble cleaned time series
    for (size_t i = 0; i < n; ++i) {
        repaired_series.samples[i].voltage_v = clean_voltages[i];
        repaired_series.samples[i].current_a = clean_currents[i];
    }

    report.cleaned_sample_count = n;

    return std::make_pair(std::move(repaired_series), std::move(report));
}

} // namespace ev::battery::cleaning
