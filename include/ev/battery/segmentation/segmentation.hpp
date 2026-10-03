#pragma once

/// @file segmentation.hpp
/// @brief M03: Operational Regime Segmentation & Constant Current Window Extraction.
/// Segments continuous battery telemetry into REST, CHARGE, DISCHARGE, and IDLE regimes
/// using dual-threshold hysteresis and debounce filtering.

#include <ev/battery/common/types.hpp>
#include <ev/battery/math_utils/calculus.hpp>
#include <ev/battery/math_utils/statistics.hpp>
#include <optional>
#include <vector>

namespace ev::battery::segmentation {

/**
 * @brief Segmentation error codes.
 */
enum class SegmentationError {
    EMPTY_INPUT,
    INVALID_THRESHOLDS,
    INSUFFICIENT_DATA,
    NO_MATCHING_SEGMENT
};

/**
 * @brief Convert SegmentationError to string.
 */
[[nodiscard]] constexpr const char* segmentationErrorToString(SegmentationError err) noexcept {
    switch (err) {
        case SegmentationError::EMPTY_INPUT:
            return "Input time series is empty";
        case SegmentationError::INVALID_THRESHOLDS:
            return "Invalid segmentation thresholds";
        case SegmentationError::INSUFFICIENT_DATA:
            return "Insufficient data points for segmentation";
        case SegmentationError::NO_MATCHING_SEGMENT:
            return "No segment matching requested criteria found";
    }
    return "Unknown segmentation error";
}

/**
 * @brief Parameters for operational regime segmentation.
 */
struct SegmentationConfig {
    double rest_current_thresh_a = 0.2;       ///< |I| <= this is considered rest/idle (A)
    double charge_current_thresh_a = 0.5;     ///< I > this enters charge state (A)
    double discharge_current_thresh_a = -0.5; ///< I < this enters discharge state (A)
    double min_dwell_time_s = 10.0;           ///< Minimum state duration to debounce transients (s)
    double min_rest_duration_s = 60.0;        ///< Minimum rest duration to qualify as electrochemical rest (s)
};

/**
 * @brief Represents a segmented operational interval with extracted metrics.
 */
struct Segment {
    common::SegmentType type;
    size_t start_index = 0;
    size_t end_index = 0;
    double start_timestamp_s = 0.0;
    double end_timestamp_s = 0.0;
    double duration_s = 0.0;
    double mean_current_a = 0.0;
    double std_current_a = 0.0;
    double mean_voltage_v = 0.0;
    double min_voltage_v = 0.0;
    double max_voltage_v = 0.0;
    double throughput_ah = 0.0; ///< Coulomb-counted charge throughput during segment
};

/**
 * @brief Segment a continuous time series into discrete operational regimes.
 *
 * Employs a finite state machine with hysteresis and minimum dwell-time debounce
 * to classify samples into REST, CHARGE, and DISCHARGE segments.
 *
 * @param series Cleaned battery telemetry series.
 * @param config Configuration parameters.
 * @return Vector of identified Segments or SegmentationError.
 */
[[nodiscard]] common::Result<std::vector<Segment>> segmentRegimes(
    const common::InternalTimeSeries& series,
    const SegmentationConfig& config = {}
);

/**
 * @brief Extract the primary Constant-Current (CC) charging segment for ICA/DVA analysis.
 *
 * Scans for charge segments where current variation std_current <= max_current_ripple_a
 * and duration >= min_duration_s.
 *
 * @param series Cleaned battery telemetry series.
 * @param min_duration_s Minimum duration in seconds (default 300.0s = 5 min).
 * @param max_current_ripple_a Maximum current standard deviation (default 0.5A).
 * @return Longest matching Segment, or error if none exists.
 */
[[nodiscard]] common::Result<Segment> extractConstantCurrentWindow(
    const common::InternalTimeSeries& series,
    double min_duration_s = 300.0,
    double max_current_ripple_a = 0.5,
    const SegmentationConfig& config = {}
);

/**
 * @brief Extract a sub-timeseries corresponding to a specific Segment.
 *
 * @param series Parent series.
 * @param segment Target segment descriptor.
 * @return Sliced InternalTimeSeries.
 */
[[nodiscard]] common::InternalTimeSeries sliceSegment(
    const common::InternalTimeSeries& series,
    const Segment& segment
);

} // namespace ev::battery::segmentation
