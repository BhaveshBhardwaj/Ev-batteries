#pragma once

/// @file cleaning.hpp
/// @brief M02: Telemetry Data Cleaning & Signal Integrity Restoration.
/// Provides sliding Hampel outlier rejection, timestamp gap repair,
/// and stuck sensor detection.

#include <ev/battery/common/types.hpp>
#include <ev/battery/math_utils/statistics.hpp>
#include <span>
#include <vector>

namespace ev::battery::cleaning {

/**
 * @brief Error codes specific to data cleaning operations.
 */
enum class CleaningError {
    INVALID_WINDOW_SIZE,
    SIZE_MISMATCH,
    EMPTY_INPUT,
    NON_MONOTONIC_TIMESTAMPS,
    INVALID_SAMPLING_INTERVAL
};

/**
 * @brief Convert CleaningError to human-readable string.
 */
[[nodiscard]] constexpr const char* cleaningErrorToString(CleaningError err) noexcept {
    switch (err) {
        case CleaningError::INVALID_WINDOW_SIZE:
            return "Invalid window size (must be >= 1)";
        case CleaningError::SIZE_MISMATCH:
            return "Input and output span sizes do not match";
        case CleaningError::EMPTY_INPUT:
            return "Input data is empty";
        case CleaningError::NON_MONOTONIC_TIMESTAMPS:
            return "Timestamps are not strictly monotonically increasing";
        case CleaningError::INVALID_SAMPLING_INTERVAL:
            return "Nominal sampling interval must be positive";
    }
    return "Unknown cleaning error";
}

/**
 * @brief Details about a detected stuck sensor interval.
 */
struct StuckSensorRegion {
    size_t start_index;
    size_t end_index;
    double stuck_value;
    double duration_s;
};

/**
 * @brief Summary statistics of a data cleaning pass.
 */
struct CleaningReport {
    size_t original_sample_count = 0;
    size_t cleaned_sample_count = 0;
    size_t voltage_spikes_removed = 0;
    size_t current_spikes_removed = 0;
    size_t samples_interpolated = 0;
    size_t gaps_repaired = 0;
    std::vector<StuckSensorRegion> stuck_voltage_regions;
    std::vector<StuckSensorRegion> stuck_current_regions;
};

/**
 * @brief Apply Hampel Filter to a 1D sequence of measurements.
 *
 * For each point, computes the median and MAD of a sliding window [i - half_window, i + half_window].
 * If |x_i - median| > n_sigma * 1.4826 * MAD, the point is treated as an outlier and replaced by median.
 *
 * @param input Raw measurement array.
 * @param output Destination array (must match input.size()).
 * @param half_window Number of samples on each side of the center point (effective window = 2*W + 1).
 * @param n_sigma Multiplier for MAD threshold (default 3.0, corresponding to 99.7% Gaussian confidence).
 * @return Number of replaced outliers on success, or CleaningError.
 */
[[nodiscard]] common::Result<size_t> hampelFilter(
    std::span<const double> input,
    std::span<double> output,
    size_t half_window = 3,
    double n_sigma = 3.0
);

/**
 * @brief Clean an entire InternalTimeSeries stream.
 *
 * Runs Hampel filtering on voltage and current channels, identifies stuck sensor flatlines,
 * and repairs packet dropouts via linear timestamp gap interpolation.
 *
 * @param raw_series The uncleaned telemetry series.
 * @param nominal_dt_s Expected nominal sampling period (e.g., 1.0s).
 * @param max_repairable_gap_s Maximum gap duration to fill via interpolation (default 10.0s).
 * @param half_window Hampel filter half-window.
 * @param n_sigma Hampel outlier threshold.
 * @return Pair of (Cleaned InternalTimeSeries, CleaningReport) or CleaningError.
 */
[[nodiscard]] common::Result<std::pair<common::InternalTimeSeries, CleaningReport>> cleanTimeSeries(
    const common::InternalTimeSeries& raw_series,
    double nominal_dt_s = 1.0,
    double max_repairable_gap_s = 10.0,
    size_t half_window = 3,
    double n_sigma = 3.0
);

/**
 * @brief Detect stuck / flatlined sensor readings in a measurement stream.
 *
 * Identifies contiguous sub-intervals of at least min_duration_samples where
 * max(values) - min(values) <= tolerance.
 *
 * @param values Measurement values.
 * @param timestamps_s Sample timestamps (must match values.size()).
 * @param min_duration_samples Minimum contiguous samples to trigger a stuck alert.
 * @param tolerance Max difference considered flatline (default 1e-9).
 * @return Vector of StuckSensorRegion structs.
 */
[[nodiscard]] common::Result<std::vector<StuckSensorRegion>> detectStuckSensor(
    std::span<const double> values,
    std::span<const double> timestamps_s,
    size_t min_duration_samples = 30,
    double tolerance = 1e-9
);

/**
 * @brief Repair timestamp gaps in an InternalTimeSeries via linear interpolation.
 *
 * Gaps where dt > 1.5 * nominal_dt_s and dt <= max_gap_s are filled with
 * uniformly spaced synthetic samples whose voltage, current, and temperature
 * are linearly interpolated. Samples are flagged with QualityLabel::INTERPOLATED.
 *
 * @param series Input series.
 * @param nominal_dt_s Expected sampling interval.
 * @param max_gap_s Maximum gap to interpolate.
 * @return Repaired series and number of filled samples.
 */
[[nodiscard]] common::Result<std::pair<common::InternalTimeSeries, size_t>> repairTimestampGaps(
    const common::InternalTimeSeries& series,
    double nominal_dt_s = 1.0,
    double max_gap_s = 10.0
);

} // namespace ev::battery::cleaning
