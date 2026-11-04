#include <ev/battery/uncertainty/uncertainty.hpp>
#include <ev/battery/estimation/trained_model.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <vector>

namespace ev::battery::uncertainty {

ConformalCalibrationData getDefaultCalibrationData() {
    // Empirical normalized absolute residuals from holdout NASA B0005-B0018 and LFP cycles
    const auto& empirical = estimation::getEmpiricalConformalResiduals();
    std::vector<double> scores(empirical.begin(), empirical.end());
    return ConformalCalibrationData{.non_conformity_scores = std::move(scores)};
}

common::Result<UncertaintyReport> quantifyUncertainty(
    const estimation::EstimationReport& estimation,
    const estimation::EstimationInput& input,
    const UncertaintyConfig& config
) {
    if (config.confidence_level <= 0.0 || config.confidence_level >= 1.0) {
        return std::unexpected(common::PipelineError{
            .module = "uncertainty",
            .code = "INVALID_CONFIG",
            .message = "Confidence level must be strictly between 0.0 and 1.0",
            .context = std::to_string(config.confidence_level)
        });
    }

    UncertaintyReport report;
    report.soh_point_estimate_pct = estimation.soh_percent;
    report.coverage_level = config.confidence_level;

    // ─────────────────────────────────────────────────────────────
    // 1. Resolve Conformal Calibration Distribution
    // ─────────────────────────────────────────────────────────────
    std::vector<double> scores = config.calibration_data.non_conformity_scores;
    if (scores.empty()) {
        scores = getDefaultCalibrationData().non_conformity_scores;
    }

    std::sort(scores.begin(), scores.end());
    const size_t n_calib = scores.size();

    // Standard finite-sample conformal quantile index: ceil((N + 1) * (1 - alpha))
    double alpha = 1.0 - config.confidence_level;
    size_t q_idx = static_cast<size_t>(std::ceil(static_cast<double>(n_calib + 1) * (1.0 - alpha))) - 1;
    if (q_idx >= n_calib) {
        q_idx = n_calib - 1;
    }
    double raw_quantile = scores[q_idx];

    // ─────────────────────────────────────────────────────────────
    // 2. Heteroscedastic Local Difficulty Multiplier
    // ─────────────────────────────────────────────────────────────
    double difficulty = 1.0;

    // Thermal stress scaling (M14)
    double tdi = input.thermal_report.thermal_degradation_index;
    if (tdi > 1.0) {
        difficulty += 0.25 * (tdi - 1.0);
    }
    if (input.thermal_report.dwell_fraction_above_45c > 0.30) {
        difficulty += 0.20;
    }

    // Sensor calibration reliability scaling (M07)
    if (!input.calibration_report.dual_anchors_found) {
        difficulty += 0.30; // Uncalibrated gain adds uncertainty
    }
    if (input.calibration_report.sensor_fault_detected) {
        difficulty += 0.50; // Sensor hardware fault severely elevates uncertainty
    }

    // Algorithmic valuation confidence scaling (M08)
    if (estimation.confidence_score < 0.90) {
        difficulty += 0.25 * (0.90 - estimation.confidence_score) / 0.10;
    }

    report.local_difficulty_multiplier = difficulty;

    // ─────────────────────────────────────────────────────────────
    // 3. Adaptive Conformal Prediction Interval Construction
    // ─────────────────────────────────────────────────────────────
    double half_width = raw_quantile * difficulty;

    report.soh_lower_bound_pct = std::clamp(estimation.soh_percent - half_width, 0.0, 100.0);
    report.soh_upper_bound_pct = std::clamp(estimation.soh_percent + half_width, 0.0, 100.0);
    report.interval_width_pct = report.soh_upper_bound_pct - report.soh_lower_bound_pct;

    // ─────────────────────────────────────────────────────────────
    // 4. Out-of-Distribution (OOD) Distance Check
    // ─────────────────────────────────────────────────────────────
    double max_z = 0.0;

    // Temperature plausibility
    if (input.thermal_report.max_temperature_c > 55.0) {
        max_z = std::max(max_z, (input.thermal_report.max_temperature_c - 55.0) / 3.0);
    }

    // Peak potential plausibility
    if (input.ica_features.primary_peak_voltage_v > 0.0) {
        double v = input.ica_features.primary_peak_voltage_v;
        // Check for 16S LFP pack (~52V-56V) or single NMC cell (~3.6V-4.15V)
        if (v > 5.0) {
            // 16S pack
            if (v < 50.0 || v > 57.5) {
                max_z = std::max(max_z, 3.5);
            }
        } else {
            // Single cell
            if (v < 3.30 || v > 4.25) {
                max_z = std::max(max_z, 3.5);
            }
        }
    }

    report.ood_score = max_z;
    report.is_out_of_distribution = (max_z >= config.ood_threshold_z);

    // ─────────────────────────────────────────────────────────────
    // 5. Gate 3 Automated Certification Decision & Abstention
    // ─────────────────────────────────────────────────────────────
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2);

    if (report.interval_width_pct <= config.max_allowed_interval_width_pct && !report.is_out_of_distribution) {
        report.decision = CertificationDecision::CERTIFIED;
        ss << "CERTIFIED: High certainty valuation. " << static_cast<int>(config.confidence_level * 100.0)
           << "% Conformal Interval [" << report.soh_lower_bound_pct << "%, "
           << report.soh_upper_bound_pct << "%] (width " << report.interval_width_pct
           << "%) meets Gate 3 commercial threshold (<= " << config.max_allowed_interval_width_pct << "%).";
    } else {
        report.decision = CertificationDecision::AUDIT_REQUIRED;
        ss << "AUDIT_REQUIRED: Automated certification blocked. ";
        if (report.interval_width_pct > config.max_allowed_interval_width_pct) {
            ss << "Confidence interval width (" << report.interval_width_pct << "%) exceeds maximum limit ("
               << config.max_allowed_interval_width_pct << "%). ";
        }
        if (report.is_out_of_distribution) {
            ss << "Telemetry flagged Out-of-Distribution (z=" << report.ood_score << " >= "
               << config.ood_threshold_z << "). ";
        }
        ss << "Depot physical inspection recommended.";
    }

    report.decision_rationale = ss.str();
    return report;
}

} // namespace ev::battery::uncertainty
