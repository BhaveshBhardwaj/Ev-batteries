#pragma once

/// @file estimation.hpp
/// @brief M08: State-of-Health (SoH) & Remaining Useful Life (RUL) Valuation Engine.
/// Fuses electrochemical differential capacity features (M06), thermal degradation
/// stress indices (M14), and calibrated throughput (M07) to evaluate certified SoH,
/// decouple LLI/LAM degradation modes, and project remaining operational cycles.

#include <ev/battery/calibration/calibration.hpp>
#include <ev/battery/common/types.hpp>
#include <ev/battery/features/features.hpp>
#include <ev/battery/thermal/thermal.hpp>

#include <cstdint>
#include <string>

namespace ev::battery::estimation {

/**
 * @brief Categorical classification of the primary degradation mechanism.
 */
enum class DegradationDominance : uint8_t {
    BALANCED,             ///< Uniform normal aging with proportional LLI and LAM
    LLI_DOMINATED,        ///< Accelerated Loss of Lithium Inventory (anode SEI growth / plating)
    LAM_DOMINATED,        ///< Accelerated Loss of Active Material (cathode particle cracking / dissolution)
    THERMAL_ACCELERATED   ///< Extreme thermal abuse (high TDI / dwell > 45°C) accelerating cell wear
};

/**
 * @brief Convert DegradationDominance to human-readable string.
 */
[[nodiscard]] constexpr const char* degradationDominanceToString(DegradationDominance d) noexcept {
    switch (d) {
        case DegradationDominance::BALANCED:            return "BALANCED";
        case DegradationDominance::LLI_DOMINATED:       return "LLI_DOMINATED";
        case DegradationDominance::LAM_DOMINATED:       return "LAM_DOMINATED";
        case DegradationDominance::THERMAL_ACCELERATED: return "THERMAL_ACCELERATED";
    }
    return "UNKNOWN";
}

/**
 * @brief Pristine initial state references for a fresh (Cycle 1 / 100% SoH) battery pack.
 */
struct BaselineCalibration {
    double initial_peak_height = 5.0;      ///< Pristine primary peak height (Ah/V)
    double initial_peak_voltage_v = 3.97;  ///< Pristine primary peak potential (V)
    double initial_peak_area_ah = 1.25;    ///< Pristine primary peak area (Ah)
    double nominal_capacity_ah = 30.0;     ///< Rated nameplate pack capacity (Ah)
};

/**
 * @brief Configuration parameters for multi-modal battery health estimation.
 */
struct EstimationConfig {
    std::string chemistry = "LFP";         ///< "LFP" or "NMC"
    double eol_soh_threshold_pct = 70.0;   ///< Commercial fleet End-of-Life threshold (70% for BPAN Tier 3)
    double weight_peak_area = 0.50;        ///< Weight for peak area in SoH regression
    double weight_peak_height = 0.35;      ///< Weight for peak height (LAM proxy)
    double weight_voltage_shift = 0.15;    ///< Weight for peak potential shift (LLI proxy)
    double base_degradation_pct_per_cycle = 0.025; ///< Nominal aging rate (% SoH / cycle at 25°C)
    BaselineCalibration baseline;          ///< Pristine baseline reference parameters
};

/**
 * @brief Input payload feeding into the multi-modal valuation engine.
 */
struct EstimationInput {
    features::ElectrochemicalFeatures ica_features; ///< Extracted ICA peaks and valleys from M06
    thermal::ThermalReport thermal_report;          ///< Thermal stress metrics and TDI from M14
    calibration::CalibrationReport calibration_report; ///< Calibrated throughput and sensor drift from M07
    double cumulative_throughput_ah = 0.0;          ///< Lifetime cumulative Ah throughput
    double equivalent_full_cycles = 0.0;            ///< Lifetime Equivalent Full Cycles (EFC)
    double nominal_capacity_ah = 30.0;              ///< Rated pack capacity (Ah)
};

/**
 * @brief Comprehensive valuation report detailing health metrics, RUL, and diagnostics.
 */
struct EstimationReport {
    double soh_percent = 100.0;              ///< Certified State of Health in [0.0, 100.0]%
    double estimated_capacity_ah = 0.0;      ///< Certified usable capacity (Ah)
    double remaining_useful_life_cycles = 0.0; ///< Projected remaining cycles until EoL
    double lli_percent = 0.0;                ///< Estimated Loss of Lithium Inventory (%)
    double lam_percent = 0.0;                ///< Estimated Loss of Active Material (%)
    double confidence_score = 0.95;          ///< Valuation algorithmic confidence in [0.0, 1.0]
    bool end_of_life_reached = false;        ///< True if SoH <= eol_soh_threshold_pct
    DegradationDominance dominance = DegradationDominance::BALANCED;
    std::string diagnostics_summary;         ///< Detailed technical explanation of valuation
};

/**
 * @brief Evaluate certified battery State-of-Health (SoH) and Remaining Useful Life (RUL).
 *
 * Fuses electrochemical differential capacity features (M06), thermal Arrhenius
 * degradation index (M14), and calibrated Coulomb metrics (M07) into an auditable,
 * deterministic valuation report compliant with BPAN Tier 3 requirements.
 *
 * @param input Multi-modal telemetry features and sensor calibration reports.
 * @param config Configuration parameters, baseline calibration, and EoL thresholds.
 * @return EstimationReport or PipelineError if input data is invalid.
 */
[[nodiscard]] common::Result<EstimationReport> estimateBatteryHealth(
    const EstimationInput& input,
    const EstimationConfig& config = {}
);

} // namespace ev::battery::estimation
