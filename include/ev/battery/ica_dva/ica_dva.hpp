#pragma once

/// @file ica_dva.hpp
/// @brief M05: Incremental Capacity Analysis (ICA) and Differential Voltage Analysis (DVA).
/// Transforms continuous CC battery charge segments into uniform dQ/dV(V) and dV/dQ(Q) curves.

#include <ev/battery/common/types.hpp>
#include <ev/battery/math_utils/calculus.hpp>
#include <ev/battery/math_utils/filters.hpp>
#include <vector>

namespace ev::battery::ica_dva {

/**
 * @brief Configuration parameters for Incremental Capacity Analysis (dQ/dV).
 */
struct IcaConfig {
    double voltage_step_v = 0.005;      ///< Resampling grid spacing: 5 mV default
    int savgol_window = 11;             ///< Savitzky-Golay window size (odd integer >= 5)
    int savgol_poly_order = 2;          ///< Savitzky-Golay polynomial order (2 or 3)
    double min_voltage_span_v = 0.15;   ///< Minimum required voltage span for valid ICA (V)
};

/**
 * @brief Configuration parameters for Differential Voltage Analysis (dV/dQ).
 */
struct DvaConfig {
    double capacity_step_ah = 0.01;     ///< Resampling grid spacing: 10 mAh default
    int savgol_window = 11;             ///< Savitzky-Golay window size
    int savgol_poly_order = 2;          ///< Savitzky-Golay polynomial order
    double min_capacity_span_ah = 0.10; ///< Minimum required capacity span for valid DVA (Ah)
};

/**
 * @brief Output curve representation for Incremental Capacity Analysis.
 */
struct IcaCurve {
    std::vector<double> voltage_v;      ///< Uniform voltage grid (V)
    std::vector<double> charge_ah;      ///< Resampled cumulative charge Q(V) (Ah)
    std::vector<double> dq_dv;          ///< Differential capacity dQ/dV (Ah/V)
    double peak_voltage_v = 0.0;        ///< Voltage corresponding to primary global peak
    double max_dq_dv = 0.0;             ///< Amplitude of dominant peak (Ah/V)
};

/**
 * @brief Output curve representation for Differential Voltage Analysis.
 */
struct DvaCurve {
    std::vector<double> capacity_ah;    ///< Uniform capacity grid (Ah)
    std::vector<double> voltage_v;      ///< Resampled terminal voltage V(Q) (V)
    std::vector<double> dv_dq;          ///< Differential voltage dV/dQ (V/Ah)
};

/**
 * @brief Compute Incremental Capacity curve (dQ/dV vs V) from a CC charging segment.
 *
 * Steps:
 * 1. Validates CC segment integrity and voltage range.
 * 2. Cumulative trapezoidal integration: Q(t) = ∫ I dt (in Ah).
 * 3. Enforces monotonic filtering on (V, Q) pairs.
 * 4. Builds uniform voltage grid: V_start to V_end with step voltage_step_v.
 * 5. Resamples Q onto V_grid using Steffen's monotone cubic spline interpolation.
 * 6. Applies Savitzky-Golay 1st-derivative kernel scaled by voltage_step_v.
 * 7. Identifies dominant global peak coordinates.
 *
 * @param cc_segment Cleaned, denoised constant-current charge segment.
 * @param config Calculation configuration.
 * @return IcaCurve on success, or PipelineError on failure.
 */
[[nodiscard]] common::Result<IcaCurve> computeIca(
    const common::InternalTimeSeries& cc_segment,
    const IcaConfig& config = {}
);

/**
 * @brief Compute Differential Voltage curve (dV/dQ vs Q) from a CC charging segment.
 *
 * Steps:
 * 1. Validates CC segment integrity.
 * 2. Cumulative integration: Q(t) = ∫ I dt (in Ah).
 * 3. Builds uniform capacity grid: Q_start to Q_end with step capacity_step_ah.
 * 4. Resamples V onto Q_grid using Steffen's monotone cubic spline interpolation.
 * 5. Applies Savitzky-Golay 1st-derivative kernel scaled by capacity_step_ah.
 *
 * @param cc_segment Cleaned, denoised constant-current charge segment.
 * @param config Calculation configuration.
 * @return DvaCurve on success, or PipelineError on failure.
 */
[[nodiscard]] common::Result<DvaCurve> computeDva(
    const common::InternalTimeSeries& cc_segment,
    const DvaConfig& config = {}
);

} // namespace ev::battery::ica_dva
