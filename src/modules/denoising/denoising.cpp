#include <ev/battery/denoising/denoising.hpp>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace ev::battery::denoising {

common::Result<void> adaptiveKalmanFilter(
    std::span<const double> raw_voltage,
    std::span<const double> current,
    std::span<double> filtered_voltage,
    const DenoisingConfig& config
) {
    if (raw_voltage.empty()) {
        return std::unexpected(common::PipelineError{
            .module = "denoising",
            .code = "INVALID_ARGUMENT",
            .message = denoisingErrorToString(DenoisingError::EMPTY_INPUT)
        });
    }

    if (raw_voltage.size() != current.size() || raw_voltage.size() != filtered_voltage.size()) {
        return std::unexpected(common::PipelineError{
            .module = "denoising",
            .code = "INVALID_ARGUMENT",
            .message = denoisingErrorToString(DenoisingError::SIZE_MISMATCH)
        });
    }

    if (config.process_noise_q <= 0.0 || config.measurement_noise_r0 <= 0.0) {
        return std::unexpected(common::PipelineError{
            .module = "denoising",
            .code = "INVALID_ARGUMENT",
            .message = denoisingErrorToString(DenoisingError::INVALID_KALMAN_PARAMS)
        });
    }

    const size_t n = raw_voltage.size();
    double x_est = raw_voltage[0];
    double p_cov = 1.0;

    const double q = config.process_noise_q;
    const double r0 = config.measurement_noise_r0;
    const double gamma = config.current_adaptation_gamma;

    for (size_t k = 0; k < n; ++k) {
        // Time update (prediction)
        const double x_pred = x_est;
        const double p_pred = p_cov + q;

        // Measurement noise covariance adaptation based on current delta
        const double di = (k > 0) ? std::abs(current[k] - current[k - 1]) : 0.0;
        const double r_k = r0 * (1.0 + gamma * di);

        // Measurement update (correction)
        const double k_gain = p_pred / (p_pred + r_k);
        x_est = x_pred + k_gain * (raw_voltage[k] - x_pred);
        p_cov = (1.0 - k_gain) * p_pred;

        filtered_voltage[k] = x_est;
    }

    return {};
}

common::Result<void> denoiseVoltage(
    std::span<const double> raw_voltage,
    std::span<const double> current,
    std::span<double> denoised_voltage,
    const DenoisingConfig& config
) {
    if (raw_voltage.empty()) {
        return std::unexpected(common::PipelineError{
            .module = "denoising",
            .code = "INVALID_ARGUMENT",
            .message = denoisingErrorToString(DenoisingError::EMPTY_INPUT)
        });
    }

    if (raw_voltage.size() != current.size() || raw_voltage.size() != denoised_voltage.size()) {
        return std::unexpected(common::PipelineError{
            .module = "denoising",
            .code = "INVALID_ARGUMENT",
            .message = denoisingErrorToString(DenoisingError::SIZE_MISMATCH)
        });
    }

    // Stage 1: Adaptive Kalman Filtering
    std::vector<double> stage1(raw_voltage.size());
    auto kalman_res = adaptiveKalmanFilter(raw_voltage, current, stage1, config);
    if (!kalman_res) {
        return std::unexpected(kalman_res.error());
    }

    // Stage 2: Optional Savitzky-Golay Polynomial Smoothing
    if (config.enable_savgol_stage && raw_voltage.size() >= config.savgol_window) {
        auto sg_res = math_utils::savgolFilter(
            stage1,
            denoised_voltage,
            static_cast<int>(config.savgol_window),
            static_cast<int>(config.savgol_order)
        );
        if (!sg_res) {
            // Fallback to Stage 1 output if Savitzky-Golay fails
            std::copy(stage1.begin(), stage1.end(), denoised_voltage.begin());
        }
    } else {
        std::copy(stage1.begin(), stage1.end(), denoised_voltage.begin());
    }

    return {};
}

common::Result<common::InternalTimeSeries> denoiseTimeSeries(
    const common::InternalTimeSeries& series,
    const DenoisingConfig& config
) {
    if (series.samples.empty()) {
        return std::unexpected(common::PipelineError{
            .module = "denoising",
            .code = "INVALID_ARGUMENT",
            .message = denoisingErrorToString(DenoisingError::EMPTY_INPUT)
        });
    }

    const size_t n = series.samples.size();
    std::vector<double> raw_v(n);
    std::vector<double> curr_i(n);
    std::vector<double> clean_v(n);

    for (size_t i = 0; i < n; ++i) {
        raw_v[i] = series.samples[i].voltage_v;
        curr_i[i] = series.samples[i].current_a;
    }

    auto res = denoiseVoltage(raw_v, curr_i, clean_v, config);
    if (!res) {
        return std::unexpected(res.error());
    }

    common::InternalTimeSeries denoised = series;
    for (size_t i = 0; i < n; ++i) {
        denoised.samples[i].voltage_v = clean_v[i];
    }

    return denoised;
}

double calculateSnrDb(
    std::span<const double> ground_truth,
    std::span<const double> evaluated_signal
) {
    if (ground_truth.empty() || ground_truth.size() != evaluated_signal.size()) {
        return 0.0;
    }

    double signal_power = 0.0;
    double noise_power = 0.0;

    for (size_t i = 0; i < ground_truth.size(); ++i) {
        signal_power += ground_truth[i] * ground_truth[i];
        const double err = ground_truth[i] - evaluated_signal[i];
        noise_power += err * err;
    }

    if (noise_power < 1e-15) {
        return 100.0; // Virtually infinite SNR
    }

    return 10.0 * std::log10(signal_power / noise_power);
}

} // namespace ev::battery::denoising
