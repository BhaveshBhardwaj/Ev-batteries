#pragma once

/// @file gpr_completion.hpp
/// @brief M10: Gaussian Process Regression (GPR) Curve Reconstruction Engine.
/// Reconstructs missing or partial constant-current charging curves via non-parametric
/// Gaussian Process Regression, enabling robust Incremental Capacity Analysis (ICA)
/// even under partial opportunity charging (Delta SoC < 20%).

#include <ev/battery/common/types.hpp>

#include <string>
#include <vector>

namespace ev::battery::gpr_completion {

/**
 * @brief Kernel hyperparameters and grid configuration for GPR curve reconstruction.
 */
struct GprConfig {
    double length_scale = 0.05;          ///< RBF length scale in Volts (default 50 mV)
    double signal_variance = 1.0;        ///< RBF signal variance sigma_f^2
    double noise_variance = 1e-4;        ///< Measurement noise variance / diagonal jitter sigma_n^2
    double min_extrapolation_v = 3.00;   ///< Lower target voltage boundary (V)
    double max_extrapolation_v = 3.65;   ///< Upper target voltage boundary (V)
    double voltage_step_v = 0.005;       ///< Target uniform voltage spacing (5 mV default)
    size_t max_support_points = 150;     ///< Maximum training points to downsample for O(N^3) speed
};

/**
 * @brief Complete GPR reconstruction result containing mean trajectory and epistemic uncertainty.
 */
struct GprResult {
    std::vector<double> voltage_grid_v;   ///< Uniform target voltage grid
    std::vector<double> mean_charge_ah;   ///< Predictive mean cumulative charge Q(V) (Ah)
    std::vector<double> variance_ah2;     ///< Epistemic uncertainty Var(Q(V))
    std::vector<double> lower_bound_ah;   ///< 95% confidence lower bound
    std::vector<double> upper_bound_ah;   ///< 95% confidence upper bound
    double max_uncertainty_ah = 0.0;     ///< Peak standard deviation across grid
    bool is_reconstructed = false;       ///< True if GPR reconstruction was executed
};

/**
 * @brief Reconstruct missing or truncated intervals in a Constant-Current charge segment via GPR.
 *
 * Employs a Gaussian Process with a Squared Exponential (RBF) covariance kernel:
 * k(V_i, V_j) = sigma_f^2 * exp(-0.5 * (V_i - V_j)^2 / l^2) + sigma_n^2 * delta_ij
 *
 * Inverts the covariance matrix via Cholesky decomposition (Eigen::LLT) to infer
 * the predictive mean Q*(V*) and epistemic variance Var(Q*(V*)) across the target grid.
 *
 * @param cc_segment Partial or complete CC charge time series.
 * @param config Kernel hyperparameters and grid boundaries.
 * @return GprResult or PipelineError on numerical/matrix factorization failure.
 */
[[nodiscard]] common::Result<GprResult> reconstructChargeCurve(
    const common::InternalTimeSeries& cc_segment,
    const GprConfig& config = {}
);

/**
 * @brief Synthesize a completed InternalTimeSeries from a GPR reconstruction.
 *
 * Produces a virtual CC charge segment with timestamps, voltages, and constant current
 * aligned to the reconstructed Q(V) trajectory, ready for direct ingestion into M05: ica_dva.
 *
 * @param gpr Completed GPR prediction report.
 * @param charge_current_a Charging current in Amperes (default 15.0 A).
 * @param nominal_temp_c Nominal operating temperature in °C.
 * @return InternalTimeSeries formatted for downstream electrochemical pipelines.
 */
[[nodiscard]] common::InternalTimeSeries synthesizeCompletedTimeSeries(
    const GprResult& gpr,
    double charge_current_a = 15.0,
    double nominal_temp_c = 25.0
);

} // namespace ev::battery::gpr_completion
