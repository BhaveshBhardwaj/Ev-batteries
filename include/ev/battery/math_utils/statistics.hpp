#pragma once

/// @file statistics.hpp
/// @brief Basic statistical functions for signal analysis.
///
/// All functions accept std::span<const double> for zero-copy operation.
/// Functions that return NaN for degenerate inputs (empty data) rather
/// than using std::expected, since NaN propagation is the natural
/// IEEE-754 behaviour and avoids branching in hot loops.

#include <cstddef>
#include <span>

namespace ev::battery::math_utils {

/**
 * @brief Arithmetic mean using Kahan compensated summation.
 * @param data Input data.
 * @return Mean value. Returns NaN if data is empty.
 */
double mean(std::span<const double> data);

/**
 * @brief Population standard deviation (ddof=0).
 *
 * Uses two-pass algorithm (compute mean, then variance) for
 * numerical stability.
 *
 * @param data Input data.
 * @return Population standard deviation. Returns NaN if data is empty.
 */
double standardDeviation(std::span<const double> data);

/**
 * @brief Median value.
 * @param data Input data.
 * @return Median. Returns NaN if data is empty.
 * @note Allocates an internal copy for partial sorting (O(n) memory).
 */
double median(std::span<const double> data);

/**
 * @brief Median Absolute Deviation (MAD).
 *
 * MAD = median(|x_i - median(x)|). A robust measure of dispersion.
 *
 * @param data Input data.
 * @return MAD value. Returns NaN if data is empty.
 * @note Allocates internal copies for sorting (O(n) memory, called twice).
 */
double medianAbsoluteDeviation(std::span<const double> data);

/**
 * @brief Compute the p-th percentile using linear interpolation.
 *
 * Matches NumPy's default interpolation method.
 *
 * @param data Input data.
 * @param p Percentile in [0, 100].
 * @return Percentile value. Returns NaN if data is empty or p is out of range.
 * @note Allocates an internal copy for sorting (O(n log n)).
 */
double percentile(std::span<const double> data, double p);

} // namespace ev::battery::math_utils
