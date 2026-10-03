#pragma once

/// @file thermal.hpp
/// @brief M14: Thermal Stress Modeling & Arrhenius Degradation Index.
/// Evaluates cumulative thermal exposure, temperature rate-of-rise (dT/dt),
/// high-temperature dwell statistics, and electro-thermal stress for Indian 2W/3W fleets.

#include <ev/battery/common/types.hpp>
#include <cstdint>
#include <cmath>

namespace ev::battery::thermal {

/**
 * @brief Categorical severity ranking for cumulative thermal stress.
 */
enum class ThermalSeverity : uint8_t {
    NORMAL,    ///< TDI < 1.5 (temperate / low stress)
    ELEVATED,  ///< 1.5 <= TDI < 2.5 (warm operating conditions)
    SEVERE,    ///< 2.5 <= TDI < 4.0 (Indian summer >40°C / high torque)
    CRITICAL   ///< TDI >= 4.0 (sustained extreme thermal abuse >45°C)
};

/**
 * @brief Convert ThermalSeverity to human-readable string.
 */
[[nodiscard]] constexpr const char* thermalSeverityToString(ThermalSeverity s) noexcept {
    switch (s) {
        case ThermalSeverity::NORMAL:   return "NORMAL";
        case ThermalSeverity::ELEVATED: return "ELEVATED";
        case ThermalSeverity::SEVERE:   return "SEVERE";
        case ThermalSeverity::CRITICAL: return "CRITICAL";
    }
    return "UNKNOWN";
}

/**
 * @brief Configuration parameters for thermal stress analysis.
 */
struct ThermalConfig {
    double activation_energy_j_mol = 31400.0; ///< Arrhenius Ea: 31.4 kJ/mol for graphite anode SEI growth
    double reference_temp_c = 25.0;           ///< Reference baseline temperature (298.15 K)
    double current_weight_gamma = 0.50;       ///< Weighting for normalized C-rate electro-thermal coupling
    double nominal_capacity_ah = 30.0;        ///< Nominal capacity for 1C normalization (A)
    double dt_window_s = 60.0;                ///< Time window for dT/dt slope estimation (seconds)
};

/**
 * @brief Detailed report summarizing session thermal characteristics.
 */
struct ThermalReport {
    double mean_temperature_c = 0.0;
    double max_temperature_c = 0.0;
    double min_temperature_c = 0.0;
    double dwell_time_above_40c_s = 0.0;
    double dwell_time_above_45c_s = 0.0;
    double dwell_time_above_55c_s = 0.0;
    double dwell_fraction_above_40c = 0.0;    ///< Fraction of total session time >= 40°C
    double dwell_fraction_above_45c = 0.0;    ///< Fraction of total session time >= 45°C
    double thermal_degradation_index = 1.0;   ///< Cumulative TDI multiplier relative to 25°C rest
    double max_heating_rate_c_per_min = 0.0;  ///< Peak heating rate dT/dt (°C/min)
    ThermalSeverity severity = ThermalSeverity::NORMAL;
};

/**
 * @brief Calculate the instantaneous Arrhenius thermal acceleration factor k(T).
 *
 * k(T) = exp(-Ea/R * (1/(T + 273.15) - 1/(T_ref + 273.15)))
 *
 * @param temp_c Temperature in degrees Celsius.
 * @param ea_j_mol Activation energy in Joules/mole (default 31.4 kJ/mol).
 * @param t_ref_c Reference baseline temperature in Celsius (default 25°C).
 * @return Dimensionless acceleration factor (k = 1.0 at 25°C, ~2.18 at 45°C).
 */
[[nodiscard]] inline double arrheniusAcceleration(
    double temp_c,
    double ea_j_mol = 31400.0,
    double t_ref_c = 25.0) noexcept
{
    constexpr double kGasConstantR = 8.314462618; // J/(mol*K)
    double t_k = temp_c + 273.15;
    double t_ref_k = t_ref_c + 273.15;
    if (t_k <= 1.0) t_k = 1.0; // Prevent division by zero
    double exponent = -(ea_j_mol / kGasConstantR) * (1.0 / t_k - 1.0 / t_ref_k);
    return std::exp(exponent);
}

/**
 * @brief Analyze thermal degradation stress, dwell statistics, and heating rates.
 *
 * @param series Cleaned battery telemetry series.
 * @param config Configuration parameters.
 * @return ThermalReport or PipelineError on invalid/empty input.
 */
[[nodiscard]] common::Result<ThermalReport> analyzeThermalStress(
    const common::InternalTimeSeries& series,
    const ThermalConfig& config = {}
);

} // namespace ev::battery::thermal
