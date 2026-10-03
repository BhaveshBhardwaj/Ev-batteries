#pragma once

/// @file uncertainty.hpp
/// @brief M09: Conformal Prediction & Safety Abstention Engine.
/// Computes distribution-free, finite-sample guaranteed confidence intervals
/// for State-of-Health valuations, applies heteroscedastic local difficulty scaling,
/// and enforces Gate 3 automated certification and safety abstention thresholds.

#include <ev/battery/common/types.hpp>
#include <ev/battery/estimation/estimation.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace ev::battery::uncertainty {

/**
 * @brief Certification outcome based on epistemic uncertainty and OOD status.
 */
enum class CertificationDecision : uint8_t {
    CERTIFIED,      ///< High certainty (interval <= 5.0% and within distribution): eligible for BPAN Tier 3 certification
    AUDIT_REQUIRED  ///< High uncertainty (interval > 5.0% or OOD): requires physical depot audit
};

/**
 * @brief Convert CertificationDecision to human-readable string.
 */
[[nodiscard]] constexpr const char* certificationDecisionToString(CertificationDecision d) noexcept {
    switch (d) {
        case CertificationDecision::CERTIFIED:      return "CERTIFIED";
        case CertificationDecision::AUDIT_REQUIRED: return "AUDIT_REQUIRED";
    }
    return "UNKNOWN";
}

/**
 * @brief Empirical calibration residuals for split conformal prediction.
 */
struct ConformalCalibrationData {
    std::vector<double> non_conformity_scores; ///< Pre-computed normalized absolute residuals |y - y_hat| / sigma
};

/**
 * @brief Configuration parameters for conformal prediction and safety abstention.
 */
struct UncertaintyConfig {
    double confidence_level = 0.90;              ///< Target coverage probability (0.90 = 90%, 0.95 = 95%)
    double max_allowed_interval_width_pct = 5.0; ///< Maximum allowed interval width for automated certification (%)
    double ood_threshold_z = 3.0;                ///< Z-score threshold for out-of-distribution detection
    ConformalCalibrationData calibration_data;   ///< Empirical holdout residuals (defaults to NASA/LFP calibration set)
};

/**
 * @brief Comprehensive uncertainty quantification report.
 */
struct UncertaintyReport {
    double soh_point_estimate_pct = 100.0;      ///< Original point estimate from M08
    double soh_lower_bound_pct = 0.0;           ///< Certified lower confidence bound (%)
    double soh_upper_bound_pct = 100.0;         ///< Certified upper confidence bound (%)
    double interval_width_pct = 0.0;            ///< Confidence interval width (upper - lower) (%)
    double coverage_level = 0.90;               ///< Target coverage probability
    double local_difficulty_multiplier = 1.0;   ///< Heteroscedastic operational scaling factor
    double ood_score = 0.0;                     ///< Out-of-distribution distance
    bool is_out_of_distribution = false;        ///< True if ood_score > ood_threshold_z
    CertificationDecision decision = CertificationDecision::AUDIT_REQUIRED;
    std::string decision_rationale;             ///< Technical explanation for underwriters and auditors
};

/**
 * @brief Generate default empirical calibration distribution grounded in NASA/LFP holdout data.
 *
 * @return ConformalCalibrationData populated with normalized residual scores.
 */
[[nodiscard]] ConformalCalibrationData getDefaultCalibrationData();

/**
 * @brief Quantify valuation uncertainty and issue certification decision.
 *
 * Computes split conformal prediction intervals with finite-sample coverage guarantees:
 * P(SoH_true in [SoH_low, SoH_high]) >= 1 - alpha
 *
 * Applies heteroscedastic difficulty scaling based on thermal stress (TDI) and sensor
 * calibration quality. If interval width exceeds max_allowed_interval_width_pct or OOD
 * is detected, automatically issues AUDIT_REQUIRED.
 *
 * @param estimation Point estimation report from M08.
 * @param input Multi-modal telemetry and operational features.
 * @param config Uncertainty configuration and calibration data.
 * @return UncertaintyReport or PipelineError on invalid configuration.
 */
[[nodiscard]] common::Result<UncertaintyReport> quantifyUncertainty(
    const estimation::EstimationReport& estimation,
    const estimation::EstimationInput& input,
    const UncertaintyConfig& config = {}
);

} // namespace ev::battery::uncertainty
