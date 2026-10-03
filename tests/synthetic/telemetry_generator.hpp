#pragma once

/// @file telemetry_generator.hpp
/// @brief Physics-based battery telemetry synthesizer grounded in real LFP/NMC
/// electrochemical curves, simulating Indian 2W/3W commercial operational regimes.

#include <ev/battery/common/types.hpp>
#include <span>
#include <string>
#include <vector>

namespace ev::battery::testing {

/**
 * @brief Configuration parameters for synthetic telemetry generation.
 */
struct GeneratorConfig {
    std::string chemistry = "LFP";     ///< "LFP" or "NMC"
    double nominal_capacity_ah = 30.0; ///< Rated pack capacity (Ah)
    int series_cells = 16;             ///< 16S pack (approx 48V/51.2V nominal for LFP)
    double sampling_interval_s = 1.0;  ///< Nominal sampling period (1 Hz)

    // Environmental / Thermal (Indian commercial conditions)
    double ambient_temp_c = 42.0;      ///< High ambient temperature (°C)
    double thermal_resistance_k_w = 0.5; ///< Passive air-cooling thermal resistance

    // Sensor Imperfections & Hardware Drift
    double current_offset_a = 0.65;    ///< Shunt sensor zero-offset drift (A)
    double current_gain_error = 1.015; ///< 1.5% gain scaling error
    double voltage_noise_std_v = 0.05; ///< Terminal voltage noise std dev (V)
    double current_noise_std_a = 0.15; ///< Current sensor noise std dev (A)

    // Anomalies & Data Corruption
    double outlier_probability = 0.01; ///< Probability of random impulse spikes
    double outlier_magnitude_v = 0.6;  ///< Amplitude of voltage spike (V)
    double packet_drop_prob = 0.02;    ///< Probability of dropped packets (timestamp gaps)
};

/**
 * @brief Ground truth telemetry attributes for objective algorithmic evaluation.
 */
struct GroundTruthSession {
    common::InternalTimeSeries telemetry; ///< Corrupted / noisy telemetry as seen by pipeline
    std::vector<double> true_voltage_v;   ///< Ideal uncorrupted terminal voltage
    std::vector<double> true_current_a;   ///< Ideal uncorrupted current
    std::vector<double> true_soc;         ///< True State of Charge in [0.0, 1.0]
    std::vector<common::SegmentType> true_regimes; ///< True operating regime for each sample
    std::vector<bool> is_outlier;         ///< True if sample contains injected spike
    std::vector<bool> is_interpolated;    ///< True if sample was affected by packet drop
};

/**
 * @brief Physics-based battery telemetry synthesizer.
 */
class TelemetryGenerator {
public:
    explicit TelemetryGenerator(GeneratorConfig config = {});

    /**
     * @brief Synthesize a complete realistic operational cycle:
     * Rest (10 min) -> Discharge/Drive (40 min) -> Rest (15 min) -> CC-CV Charge (60 min) -> Rest (15 min).
     *
     * @param seed Random seed for deterministic reproducibility.
     * @param initial_soc Initial pack SoC in [0.0, 1.0].
     * @return GroundTruthSession containing both noisy stream and ground-truth validation arrays.
     */
    GroundTruthSession generateFullSession(uint32_t seed = 42, double initial_soc = 0.40);

    /**
     * @brief Synthesize a clean Constant-Current (CC) charge segment for ICA/DVA testing.
     *
     * @param charge_current_a Charging current in Amperes.
     * @param start_soc Starting SoC.
     * @param end_soc Ending SoC.
     * @param add_noise If false, produces pristine electrochemical curve.
     * @param seed Random seed.
     */
    GroundTruthSession generateChargeSegment(
        double charge_current_a = 15.0,
        double start_soc = 0.10,
        double end_soc = 0.95,
        bool add_noise = true,
        uint32_t seed = 42
    );

    /**
     * @brief Compute real electrochemical Open Circuit Voltage (OCV) for a single cell.
     * Grounded in laboratory LFP/NMC equilibrium potential data.
     *
     * @param soc State of Charge in [0.0, 1.0].
     * @return Single cell equilibrium OCV in Volts.
     */
    double cellOcv(double soc) const;

private:
    GeneratorConfig config_;
};

} // namespace ev::battery::testing
