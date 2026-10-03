#include <ev/battery/estimation/estimation.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace ev::battery::estimation {

common::Result<EstimationReport> estimateBatteryHealth(
    const EstimationInput& input,
    const EstimationConfig& config
) {
    EstimationReport report;

    // Nominal capacity resolution
    double nominal_cap_ah = input.nominal_capacity_ah;
    if (nominal_cap_ah <= 0.0) {
        nominal_cap_ah = config.baseline.nominal_capacity_ah;
    }
    if (nominal_cap_ah <= 0.0) {
        nominal_cap_ah = 30.0; // Standard commercial 16S pack baseline
    }

    const auto& baseline = config.baseline;
    const auto& ica = input.ica_features;
    const auto& thermal = input.thermal_report;
    const auto& calib = input.calibration_report;

    // ─────────────────────────────────────────────────────────────
    // 1. Electrochemical Peak Ratio Analysis
    // ─────────────────────────────────────────────────────────────
    double r_area = 1.0;
    double r_height = 1.0;
    double r_voltage = 1.0;
    double v_shift = 0.0;
    bool has_valid_peaks = false;

    if (ica.primary_peak_height > 1e-4) {
        has_valid_peaks = true;

        // Peak area ratio
        if (baseline.initial_peak_area_ah > 1e-4) {
            r_area = ica.primary_peak_area_ah / baseline.initial_peak_area_ah;
        } else {
            r_area = 1.0;
        }

        // Peak height ratio (LAM proxy)
        if (baseline.initial_peak_height > 1e-4) {
            r_height = ica.primary_peak_height / baseline.initial_peak_height;
        } else {
            r_height = 1.0;
        }

        // Peak potential shift (LLI & DCIR proxy)
        v_shift = ica.primary_peak_voltage_v - baseline.initial_peak_voltage_v;
        // In charge ICA, 0.15V shift corresponds to complete end-of-life
        constexpr double kMaxVoltageShiftV = 0.15;
        double shift_penalty = std::max(0.0, v_shift) / kMaxVoltageShiftV;
        r_voltage = 1.0 - std::min(0.50, shift_penalty);
    } else if (calib.calibrated_coulomb_ah > 0.0 && nominal_cap_ah > 0.0) {
        // Fallback: estimate directly from calibrated throughput if no ICA peak available
        double raw_ratio = std::clamp(calib.calibrated_coulomb_ah / nominal_cap_ah, 0.0, 1.05);
        report.soh_percent = std::clamp(raw_ratio * 100.0, 0.0, 100.0);
        report.estimated_capacity_ah = nominal_cap_ah * (report.soh_percent / 100.0);
        report.lam_percent = (1.0 - raw_ratio) * 100.0;
        report.lli_percent = report.lam_percent;
        report.confidence_score = 0.70;
        report.dominance = DegradationDominance::BALANCED;

        if (report.soh_percent <= config.eol_soh_threshold_pct) {
            report.end_of_life_reached = true;
            report.remaining_useful_life_cycles = 0.0;
        } else {
            double rem = report.soh_percent - config.eol_soh_threshold_pct;
            double rate = config.base_degradation_pct_per_cycle;
            report.remaining_useful_life_cycles = rem / rate;
        }

        std::stringstream ss;
        ss << std::fixed << std::setprecision(2);
        ss << "Fallback SoH: " << report.soh_percent << "% (" << report.estimated_capacity_ah
           << " Ah) from calibrated throughput. ICA peaks unobserved.";
        report.diagnostics_summary = ss.str();
        return report;
    }

    // Clamp ratios to physically plausible bounds
    r_area = std::clamp(r_area, 0.0, 1.05);
    r_height = std::clamp(r_height, 0.0, 1.05);
    r_voltage = std::clamp(r_voltage, 0.50, 1.0);

    // ─────────────────────────────────────────────────────────────
    // 2. Multi-Modal SoH Regression
    // ─────────────────────────────────────────────────────────────
    double w_area = config.weight_peak_area;
    double w_height = config.weight_peak_height;
    double w_voltage = config.weight_voltage_shift;
    double w_sum = w_area + w_height + w_voltage;
    if (w_sum <= 1e-6) w_sum = 1.0;

    double weighted_soh_fraction = (w_area * r_area + w_height * r_height + w_voltage * r_voltage) / w_sum;
    report.soh_percent = std::clamp(weighted_soh_fraction * 100.0, 0.0, 100.0);
    report.estimated_capacity_ah = nominal_cap_ah * (report.soh_percent / 100.0);

    // ─────────────────────────────────────────────────────────────
    // 3. Degradation Mode Decomposition (LLI vs. LAM)
    // ─────────────────────────────────────────────────────────────
    // LAM: directly proportional to active material peak amplitude loss
    report.lam_percent = std::clamp((1.0 - r_height) * 100.0, 0.0, 100.0);

    // LLI: tracked by positive peak shift and phase area attenuation
    constexpr double kMaxLliShiftV = 0.15;
    double lli_from_shift = (std::max(0.0, v_shift) / kMaxLliShiftV) * 100.0;
    double lli_from_area = (1.0 - r_area) * 100.0;
    report.lli_percent = std::clamp(0.60 * lli_from_area + 0.40 * lli_from_shift, 0.0, 100.0);

    // Categorize dominance
    if (thermal.thermal_degradation_index >= 2.50 || thermal.dwell_fraction_above_45c > 0.40) {
        report.dominance = DegradationDominance::THERMAL_ACCELERATED;
    } else if (report.lli_percent > report.lam_percent + 8.0) {
        report.dominance = DegradationDominance::LLI_DOMINATED;
    } else if (report.lam_percent > report.lli_percent + 8.0) {
        report.dominance = DegradationDominance::LAM_DOMINATED;
    } else {
        report.dominance = DegradationDominance::BALANCED;
    }

    // ─────────────────────────────────────────────────────────────
    // 4. Remaining Useful Life (RUL) with Thermal Acceleration
    // ─────────────────────────────────────────────────────────────
    if (report.soh_percent <= config.eol_soh_threshold_pct) {
        report.remaining_useful_life_cycles = 0.0;
        report.end_of_life_reached = true;
    } else {
        report.end_of_life_reached = false;
        double remaining_soh_pct = report.soh_percent - config.eol_soh_threshold_pct;

        // Baseline degradation velocity (% SoH loss per cycle)
        double degradation_rate_pct_per_cycle = config.base_degradation_pct_per_cycle;
        if (input.equivalent_full_cycles >= 15.0) {
            double observed_rate = (100.0 - report.soh_percent) / input.equivalent_full_cycles;
            if (observed_rate > 0.005) {
                degradation_rate_pct_per_cycle = observed_rate;
            }
        }

        // Modulate degradation rate by Arrhenius thermal degradation index (TDI)
        double thermal_multiplier = std::max(1.0, thermal.thermal_degradation_index);
        double effective_rate = degradation_rate_pct_per_cycle * thermal_multiplier;

        if (effective_rate > 1e-6) {
            report.remaining_useful_life_cycles = std::max(0.0, remaining_soh_pct / effective_rate);
        } else {
            report.remaining_useful_life_cycles = 1000.0;
        }
    }

    // ─────────────────────────────────────────────────────────────
    // 5. Algorithmic Confidence & Diagnostics Summary
    // ─────────────────────────────────────────────────────────────
    double conf = 0.95;
    if (!has_valid_peaks) conf -= 0.15;
    if (calib.sensor_fault_detected) conf -= 0.20;
    if (!calib.dual_anchors_found) conf -= 0.05;
    if (thermal.thermal_degradation_index > 3.0) conf -= 0.05;
    report.confidence_score = std::clamp(conf, 0.20, 1.0);

    std::stringstream ss;
    ss << std::fixed << std::setprecision(2);
    ss << "Certified SoH: " << report.soh_percent << "% (" << report.estimated_capacity_ah << " Ah). ";
    ss << "Primary Mechanism: " << degradationDominanceToString(report.dominance) << " ";
    ss << "(LLI=" << report.lli_percent << "%, LAM=" << report.lam_percent << "%). ";
    ss << "Thermal Stress Multiplier: " << std::max(1.0, thermal.thermal_degradation_index) << "x. ";
    ss << "Projected RUL: " << static_cast<int>(report.remaining_useful_life_cycles) << " cycles ";
    ss << "to " << config.eol_soh_threshold_pct << "% EoL.";

    report.diagnostics_summary = ss.str();
    return report;
}

} // namespace ev::battery::estimation
