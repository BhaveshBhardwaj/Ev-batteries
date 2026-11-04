#pragma once

/// @file calibration.hpp
/// @brief M07: Current-Sensor Closed-Loop Self-Calibration.
/// Regresses zero-bias offset error (alpha) from electrochemical rest anchors
/// and gain scaling error (beta) from dual OCV equilibrium states, generating
/// drift-free calibrated telemetry and enforcing Gate 2 sensor fault diagnostics.

#include <ev/battery/common/types.hpp>
#include <ev/battery/segmentation/segmentation.hpp>
#include <string>
#include <utility>
#include <vector>

namespace ev::battery::calibration {

/**
 * @brief Discrete point in an Open Circuit Voltage (OCV) vs. SoC relationship.
 */
struct OcvPoint {
    double soc = 0.0;        ///< State of Charge in [0.0, 1.0]
    double voltage_v = 0.0;  ///< Equilibrium Open Circuit Voltage (V)
};

/**
 * @brief Configuration parameters for current sensor self-calibration.
 */
struct CalibrationConfig {
    double max_rest_current_a = 0.80;           ///< Maximum current magnitude to qualify as rest (A)
    double max_candidate_rest_current_a = 2.50; ///< Upper bound to capture uncalibrated/faulty rest intervals (A)
    double min_rest_duration_s = 30.0;          ///< Minimum duration to eliminate capacitive/RC transient (s)
    double max_allowed_offset_a = 1.50;         ///< Hardware fault threshold for sensor zero offset (A)
    double max_allowed_gain_error = 0.08;       ///< Hardware fault threshold for sensor gain error (|beta - 1.0|)
    double min_soc_delta_for_gain = 0.08;  ///< Minimum SoC difference required to regress gain beta
    double nominal_capacity_ah = 30.0;     ///< Fallback pack nominal capacity if unspecified in series (Ah)
    std::vector<OcvPoint> ocv_table;       ///< Monotonic OCV-to-SoC lookup table (defaults to 16S LFP)
};

/**
 * @brief Comprehensive report summarizing the current-sensor calibration results.
 */
struct CalibrationReport {
    double estimated_offset_a = 0.0;       ///< Regressed sensor zero-bias offset alpha (A)
    double estimated_gain = 1.0;           ///< Regressed sensor gain scaling factor beta
    double initial_soc = 0.0;              ///< Equilibrium SoC from initial OCV anchor
    double final_soc = 0.0;                ///< Equilibrium SoC from final OCV anchor
    double raw_coulomb_ah = 0.0;           ///< Uncalibrated raw Coulomb-counted charge integral (Ah)
    double calibrated_coulomb_ah = 0.0;    ///< Calibrated, drift-corrected charge integral (Ah)
    bool dual_anchors_found = false;       ///< True if dual OCV anchors satisfied |Delta SoC| >= threshold
    bool sensor_fault_detected = false;    ///< True if offset or gain exceeded Gate 2 physical limits
    std::string fault_message;             ///< Diagnostic alert details for operator inspection
};

/**
 * @brief Invert equilibrium OCV to estimate State of Charge (SoC).
 * Performs O(log M) binary search and piecewise linear interpolation.
 *
 * @param voltage_v Equilibrium terminal voltage (V).
 * @param ocv_table Monotonic OCV-SoC lookup table.
 * @return Estimated SoC in [0.0, 1.0].
 */
[[nodiscard]] double lookupSocFromOcv(
    double voltage_v,
    const std::vector<OcvPoint>& ocv_table
) noexcept;

/**
 * @brief Evaluate equilibrium OCV for a given State of Charge (SoC).
 * Performs O(log M) binary search and piecewise linear interpolation.
 *
 * @param soc State of Charge in [0.0, 1.0].
 * @param ocv_table Monotonic OCV-SoC lookup table.
 * @return Equilibrium voltage in Volts.
 */
[[nodiscard]] double lookupOcvFromSoc(
    double soc,
    const std::vector<OcvPoint>& ocv_table
) noexcept;

/**
 * @brief Generate default high-resolution 16S LFP Open Circuit Voltage table.
 * Synthesized from the Safari & Delacourt (2011) electrochemical LFP model.
 *
 * @return Vector of OcvPoint pairs sorted monotonically by voltage and SoC.
 */
[[nodiscard]] std::vector<OcvPoint> getDefaultLfp16sOcvTable();

/**
 * @brief Generate scaled Open Circuit Voltage table for any cell chemistry and series cell count.
 *
 * @param chemistry "LFP" or "NMC".
 * @param series_cells Number of cells in series (e.g. 16 for 16S LFP, 14 for 14S NMC, 1 for cell).
 * @return Vector of OcvPoint pairs sorted monotonically by voltage and SoC.
 */
[[nodiscard]] std::vector<OcvPoint> getScaledOcvTable(std::string_view chemistry, int series_cells);

/**
 * @brief Load Open Circuit Voltage table from a reference CSV file.
 * Expects CSV format with columns: soc, cell_ocv_v [, pack_16s_ocv_v].
 *
 * @param csv_path Absolute or relative path to CSV file.
 * @param is_pack_voltage If true, parses pack voltage column; if false, scales cell voltage by series_cells.
 * @param series_cells Number of cells in series if scaling cell voltage (default 16).
 * @return Vector of OcvPoint or PipelineError.
 */
[[nodiscard]] common::Result<std::vector<OcvPoint>> loadOcvTableFromCsv(
    const std::string& csv_path,
    bool is_pack_voltage = true,
    int series_cells = 16
);

/**
 * @brief Perform closed-loop current sensor self-calibration on a telemetry session.
 *
 * 1. Rest-Anchor Offset Regression:
 *    Identifies resting intervals and calculates mean current:
 *    alpha_hat = 1/N_rest * sum(I_k)
 *
 * 2. Dual OCV Anchor Matching & Gain Error Regression:
 *    Identifies equilibrium voltage states before and after active operation:
 *    Delta Q_true = Q_nom * (SoC_2 - SoC_1)
 *    beta_hat = (Q_meas - alpha_hat * Delta t) / Delta Q_true
 *
 * 3. Gate 2 Sensor Diagnostic Fault Thresholds:
 *    Flags ALERT_SENSOR_OFFSET_EXCESSIVE if |alpha_hat| > max_allowed_offset_a.
 *    Flags ALERT_SENSOR_GAIN_EXCESSIVE if |beta_hat - 1.0| > max_allowed_gain_error.
 *
 * 4. Corrected Series Generation:
 *    I_corr(t) = (I_meas(t) - alpha_hat) / beta_hat
 *
 * @param series Ingested/cleaned battery telemetry series.
 * @param segments Pre-computed operational segments (from M03: segmentation).
 *                 If empty, resting segments are detected internally.
 * @param config Calibration configuration parameters.
 * @return Pair of calibrated InternalTimeSeries and detailed CalibrationReport.
 */
[[nodiscard]] common::Result<std::pair<common::InternalTimeSeries, CalibrationReport>> calibrateTimeSeries(
    const common::InternalTimeSeries& series,
    const std::vector<segmentation::Segment>& segments = {},
    const CalibrationConfig& config = {}
);

} // namespace ev::battery::calibration
