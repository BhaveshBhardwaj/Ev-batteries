#pragma once

/// @file filters.hpp
/// @brief Time-series signal filtering and smoothing: Savitzky-Golay polynomial
/// filters, 1D discrete convolutions, and moving window smoothers.

#include <ev/battery/math_utils/types.hpp>
#include <span>
#include <vector>

namespace ev::battery::math_utils {

// ─── Savitzky-Golay Smoothing & Differentiation ──────────────────

/**
 * @brief Compute Savitzky-Golay filter coefficients via Vandermonde QR/LDLT solve.
 *
 * @param window_size Odd integer >= 3 and <= 51.
 * @param poly_order  Polynomial order, must satisfy 0 <= poly_order < window_size.
 * @param deriv_order Derivative order (0 = smoothing, 1 = 1st derivative, etc.).
 * @return Vector of coefficients (length = window_size), or error.
 */
MathResult<std::vector<double>> savgolCoefficients(
    int window_size,
    int poly_order,
    int deriv_order = 0
);

/**
 * @brief Apply Savitzky-Golay smoothing filter with symmetric mirror boundary handling.
 *
 * Preserves underlying polynomial peak shapes without damping peak heights.
 *
 * @param input       Input telemetry sequence.
 * @param output      Output smoothed buffer (same size as input).
 * @param window_size Odd integer >= 3.
 * @param poly_order  Polynomial order.
 * @return Success, or error.
 */
MathResult<void> savgolFilter(
    std::span<const double> input,
    std::span<double> output,
    int window_size,
    int poly_order
);

/**
 * @brief Apply Savitzky-Golay derivative filter with physical sample spacing scaling.
 *
 * @param input          Input telemetry sequence.
 * @param output         Output derivative buffer (same size as input).
 * @param window_size    Odd integer >= 3.
 * @param poly_order     Polynomial order.
 * @param deriv_order    Derivative order (>= 1).
 * @param sample_spacing Spacing between consecutive samples (h > 0).
 * @return Success, or error.
 */
MathResult<void> savgolDerivative(
    std::span<const double> input,
    std::span<double> output,
    int window_size,
    int poly_order,
    int deriv_order,
    double sample_spacing = 1.0
);

// ─── Discrete 1D Convolutions ────────────────────────────────────

/**
 * @brief 1D discrete convolution in "valid" mode (no padding).
 *
 * Output size must be exactly signal.size() - kernel.size() + 1.
 */
MathResult<void> convolve1d(
    std::span<const double> signal,
    std::span<const double> kernel,
    std::span<double> output
);

/**
 * @brief 1D discrete convolution in "same" mode with mirror boundary extension.
 *
 * Output size matches signal size exactly. Kernel must have odd length.
 */
MathResult<void> convolve1dSame(
    std::span<const double> signal,
    std::span<const double> kernel,
    std::span<double> output
);

// ─── Moving Window Smoothers ─────────────────────────────────────

/**
 * @brief Centered moving average filter with boundary shrinking.
 *
 * @param input       Input data.
 * @param output      Output buffer (same size as input).
 * @param window_size Odd integer >= 1.
 */
MathResult<void> movingAverage(
    std::span<const double> input,
    std::span<double> output,
    int window_size
);

/**
 * @brief Centered moving median filter for spike / impulsive noise rejection.
 *
 * @param input       Input data.
 * @param output      Output buffer (same size as input).
 * @param window_size Odd integer >= 1.
 */
MathResult<void> movingMedian(
    std::span<const double> input,
    std::span<double> output,
    int window_size
);

} // namespace ev::battery::math_utils
