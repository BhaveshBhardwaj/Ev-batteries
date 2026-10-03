#pragma once

/// @file quality_score.hpp
/// @brief M12: Telemetry Quality Scoring (TQS) & Gate 1 Pre-Qualification Engine.
/// Quantifies sampling regularity, packet transmission continuity, physical sensor
/// plausibility, and SNR, enforcing Gate 1 admission thresholds (TQS >= 30.0).

#include <ev/battery/common/types.hpp>

#include <string>

namespace ev::battery::quality_score {

/**
 * @brief Convert QualityLabel to string.
 */
[[nodiscard]] constexpr const char* qualityLabelToString(common::QualityLabel q) noexcept {
    switch (q) {
        case common::QualityLabel::EXCELLENT:  return "EXCELLENT";
        case common::QualityLabel::ACCEPTABLE: return "ACCEPTABLE";
        case common::QualityLabel::MARGINAL:   return "MARGINAL";
        case common::QualityLabel::REJECT:     return "REJECT";
    }
    return "UNKNOWN";
}

/**
 * @brief Configuration parameters for Telemetry Quality Scoring (TQS).
 */
struct QualityConfig {
    double nominal_dt_s = 1.0;                 ///< Expected nominal sampling period (s)
    double min_plausible_voltage_v = 30.0;     ///< Minimum allowable pack voltage (V)
    double max_plausible_voltage_v = 65.0;     ///< Maximum allowable pack voltage (V)
    double max_plausible_current_a = 250.0;    ///< Maximum allowable peak current magnitude (A)
    double min_plausible_temp_c = -10.0;       ///< Minimum operational temperature (°C)
    double max_plausible_temp_c = 75.0;        ///< Maximum operational temperature (°C)
    double gate1_min_score = 30.0;             ///< Gate 1 automated admission cutoff (30.0 / 100)
};

/**
 * @brief Detailed breakdown of the multi-dimensional Telemetry Quality Score.
 */
struct QualityReport {
    double total_score = 0.0;                  ///< Composite TQS in [0.0, 100.0]
    double jitter_score = 0.0;                 ///< Sampling jitter score S_jitter in [0.0, 25.0]
    double dropout_score = 0.0;                ///< Packet dropout score S_drop in [0.0, 30.0]
    double plausibility_score = 0.0;           ///< Range compliance score S_plaus in [0.0, 25.0]
    double snr_score = 0.0;                    ///< High-frequency noise score S_snr in [0.0, 20.0]

    double sampling_jitter_cv = 0.0;           ///< Coefficient of variation of dt (sigma_dt / mu_dt)
    double dropout_fraction = 0.0;             ///< Ratio of missing duration to total duration
    double plausible_fraction = 1.0;           ///< Fraction of samples within physical bounds
    double voltage_noise_std = 0.0;            ///< High-frequency second-difference roughness

    common::QualityLabel quality_label = common::QualityLabel::REJECT;
    bool gate1_passed = false;                 ///< True if total_score >= gate1_min_score
    std::string diagnostic_summary;            ///< Diagnostic reason for operator/auditor
};

/**
 * @brief Compute Telemetry Quality Score and enforce Gate 1 pre-qualification.
 *
 * Evaluates telemetry health along 4 physical dimensions:
 * 1. Jitter (25 pts): Uniformity of sample timestamps.
 * 2. Dropout (30 pts): Percentage of missing time in transmission gaps.
 * 3. Plausibility (25 pts): Range compliance of voltage, current, and temperature.
 * 4. SNR / Noise (20 pts): Second-difference roughness of voltage signal.
 *
 * @param series Ingested raw telemetry series.
 * @param config Evaluation thresholds and parameters.
 * @return QualityReport detailing scores, Gate 1 status, and diagnostic notes.
 */
[[nodiscard]] QualityReport computeQualityScore(
    const common::InternalTimeSeries& series,
    const QualityConfig& config = {}
);

} // namespace ev::battery::quality_score
