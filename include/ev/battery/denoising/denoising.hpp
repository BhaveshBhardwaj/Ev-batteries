#pragma once

/// @file denoising.hpp
/// @brief M04: Telemetry Denoising & Electrochemical Peak Preserving Smoothing.
/// Provides an Adaptive Kalman Filter and cascaded Savitzky-Golay smoothing
/// designed to achieve >= 15 dB SNR improvement without shifting peak locations.

#include <ev/battery/common/types.hpp>
#include <ev/battery/math_utils/filters.hpp>
#include <span>
#include <vector>

namespace ev::battery::denoising {

/**
 * @brief Denoising error codes.
 */
enum class DenoisingError {
    EMPTY_INPUT,
    INVALID_WINDOW_SIZE,
    SIZE_MISMATCH,
    INVALID_KALMAN_PARAMS
};

/**
 * @brief Convert DenoisingError to human-readable string.
 */
[[nodiscard]] constexpr const char* denoisingErrorToString(DenoisingError err) noexcept {
    switch (err) {
        case DenoisingError::EMPTY_INPUT:
            return "Input measurement stream is empty";
        case DenoisingError::INVALID_WINDOW_SIZE:
            return "Window size must be odd and greater than polynomial order";
        case DenoisingError::SIZE_MISMATCH:
            return "Input and output span sizes do not match";
        case DenoisingError::INVALID_KALMAN_PARAMS:
            return "Kalman filter noise parameters must be strictly positive";
    }
    return "Unknown denoising error";
}

/**
 * @brief Configuration parameters for adaptive cascaded denoising.
 */
struct DenoisingConfig {
    // Kalman parameters
    double process_noise_q = 1e-4;       ///< State transition covariance Q
    double measurement_noise_r0 = 1e-2;  ///< Baseline measurement noise covariance R0
    double current_adaptation_gamma = 0.05; ///< Scaling of R with current deviation: R = R0*(1 + gamma*|dI|)

    // Savitzky-Golay polynomial parameters
    size_t savgol_window = 15;           ///< Smoothing window (must be odd)
    size_t savgol_order = 2;             ///< Polynomial order (2 or 3)
    bool enable_savgol_stage = true;     ///< If true, executes cascaded Savitzky-Golay pass
};

/**
 * @brief 1D Adaptive Kalman Filter for battery terminal voltage.
 *
 * Implements a dynamic state estimator where measurement variance R is adapted
 * based on current stability to prevent phase-lag during load steps while maximizing
 * noise rejection during constant-current intervals.
 *
 * @param raw_voltage Raw terminal voltage measurements.
 * @param current Current measurements associated with each sample.
 * @param filtered_voltage Output destination for filtered voltages.
 * @param config Configuration parameters.
 * @return void on success, or DenoisingError.
 */
[[nodiscard]] common::Result<void> adaptiveKalmanFilter(
    std::span<const double> raw_voltage,
    std::span<const double> current,
    std::span<double> filtered_voltage,
    const DenoisingConfig& config = {}
);

/**
 * @brief Cascaded Denoising Pipeline (Kalman + Savitzky-Golay).
 *
 * Applies Stage 1 Adaptive Kalman Filtering followed by Stage 2 zero-phase
 * Savitzky-Golay smoothing to achieve superior SNR improvement while preserving
 * dQ/dV and dV/dQ electrochemical peak positions.
 *
 * @param raw_voltage Raw terminal voltages.
 * @param current Current measurements.
 * @param denoised_voltage Output array (same size as raw_voltage).
 * @param config Configuration parameters.
 * @return void on success, or DenoisingError.
 */
[[nodiscard]] common::Result<void> denoiseVoltage(
    std::span<const double> raw_voltage,
    std::span<const double> current,
    std::span<double> denoised_voltage,
    const DenoisingConfig& config = {}
);

/**
 * @brief Denoise an entire InternalTimeSeries in-place or returning a cleaned copy.
 *
 * @param series Input series.
 * @param config Configuration parameters.
 * @return Denoised InternalTimeSeries or PipelineError.
 */
[[nodiscard]] common::Result<common::InternalTimeSeries> denoiseTimeSeries(
    const common::InternalTimeSeries& series,
    const DenoisingConfig& config = {}
);

/**
 * @brief Compute Signal-to-Noise Ratio (SNR) in decibels (dB) between a ground-truth signal
 * and a noisy or filtered signal.
 *
 * SNR = 10 * log10( sum(s^2) / sum((s - s_noisy)^2) )
 *
 * @param ground_truth Clean reference signal.
 * @param evaluated_signal Noisy or filtered signal.
 * @return SNR in dB.
 */
[[nodiscard]] double calculateSnrDb(
    std::span<const double> ground_truth,
    std::span<const double> evaluated_signal
);

} // namespace ev::battery::denoising
