# Iteration 10: Gaussian Process Regression (GPR) Curve Reconstruction — M10
## System Requirements Specification (SRS)

> **Document ID:** SRS-ITERATION-010  
> **Version:** 1.0  
> **Created:** 2026-10-03  
> **Module ID:** `M10: gpr_completion`  
> **Target Architecture:** Indian 2W/3W Passive-Cooled Fleet Ecosystem & Partial Charging Regimes  
> **Parent Architecture:** [FLOW-001 (docs/flow.md)](../../flow.md)

---

## 1. Scope

### 1.1 In Scope
- **M10 Partial Charge Recovery via Gaussian Process Regression:**
  - Non-parametric reconstruction of truncated or fragmented Constant-Current charging curves:
    $$\bar{Q}_* = \mathbf{k}_*^T (\mathbf{K} + \sigma_n^2 \mathbf{I})^{-1} \mathbf{Q}_{obs}$$
    $$\text{Var}(Q_*) = k(x_*, x_*) - \mathbf{k}_*^T (\mathbf{K} + \sigma_n^2 \mathbf{I})^{-1} \mathbf{k}_*$$
  - Radial Basis Function (RBF / Squared Exponential) covariance kernel with Cholesky decomposition ($\mathbf{L}\mathbf{L}^T$) via Eigen 3.4.
  - Epistemic uncertainty quantification: Outputs $\pm 1.96\sigma$ confidence bands across extrapolated voltage intervals.
  - Generates synthetic, complete `InternalTimeSeries` allowing downstream `M05: ica_dva` and `M06: features` to extract peak signatures even from partial opportunity charging ($\Delta \text{SoC} < 20\%$).
- **M10 Latency & Numerical Stability:**
  - Downsampling observed telemetry to $N \le 200$ anchor points to bound Cholesky factor complexity to $\mathcal{O}(N^3) < 1\text{ ms}$.
  - Jitter regularization ($\sigma_n^2 \mathbf{I}$) ensuring positive-definiteness on flat plateaus (e.g. 3.32V - 3.35V LFP plateau).

### 1.2 Out of Scope
- Online recursive GPR updating — batch Cholesky on extracted CC segments is deterministic and sufficient.

---

## 2. Gate 0: Prerequisite & Requirement Verification Audit

| Category | Prerequisite Check | Status | Evidence / Reference |
| :--- | :--- | :---: | :--- |
| **Linear Algebra Engine** | Eigen 3.4 Dense Cholesky Solver (`Eigen::LLT`) | **SATISFIED** | Integrated in root CMakeLists via FetchContent. |
| **Common Types** | `Sample`, `InternalTimeSeries`, `Result` | **SATISFIED** | Defined in `common/types.hpp`. |
| **Downstream Pipelines** | `M05: ica_dva`, `M06: features`, `M08: estimation` | **SATISFIED** | All passing unit and benchmark tests. |

---

## 3. Requirements

### 3.1 Functional Requirements

- **FR-10.01 (Partial CC Detection):** Detect if a CC charging segment covers insufficient voltage span ($< 0.15\text{ V}$) or has internal gap dropouts.
- **FR-10.02 (GPR Predictive Trajectory):** Train an RBF Gaussian Process on observed $(V, Q)$ coordinates and predict the mean charge trajectory across the full operational range $[V_{min}, V_{max}]$.
- **FR-10.03 (Epistemic Confidence Bands):** Compute predictive variance $\text{Var}(Q_*)$ at each target grid point. In unobserved extrapolation regions, variance must smoothly expand towards prior signal variance $\sigma_f^2$.
- **FR-10.04 (Synthesize Complete Series):** Provide `synthesizeCompletedTimeSeries(gpr, current_a)` to feed downstream `M05: ica_dva`.

### 3.2 Non-Functional Requirements

- **NFR-10.01 (Execution Latency):** GPR training and predictive inference over 100 target grid points must execute in **$< 5.0\text{ ms}$**.
- **NFR-10.02 (Numerical Condition Number):** Condition number must be maintained via diagonal jitter regularizer $\sigma_n^2 \ge 10^{-6}$ to prevent ill-conditioned Cholesky factorization.

---

## 4. Interfaces & Data Structures

```cpp
namespace ev::battery::gpr_completion {

struct GprConfig {
    double length_scale = 0.05;        ///< Length scale in Volts
    double signal_variance = 1.0;      ///< Kernel signal variance
    double noise_variance = 1e-4;      ///< Noise variance / diagonal jitter
    double min_extrapolation_v = 3.00;
    double max_extrapolation_v = 3.65;
    double voltage_step_v = 0.005;     ///< Uniform 5 mV grid spacing
};

struct GprResult {
    std::vector<double> voltage_grid_v;
    std::vector<double> mean_charge_ah;
    std::vector<double> variance_ah2;
    std::vector<double> lower_bound_ah;
    std::vector<double> upper_bound_ah;
    double max_uncertainty_ah = 0.0;
    bool is_reconstructed = false;
};

[[nodiscard]] common::Result<GprResult> reconstructChargeCurve(
    const common::InternalTimeSeries& cc_segment,
    const GprConfig& config = {}
);

[[nodiscard]] common::InternalTimeSeries synthesizeCompletedTimeSeries(
    const GprResult& gpr,
    double charge_current_a = 15.0,
    double nominal_temp_c = 25.0
);

} // namespace ev::battery::gpr_completion
```

---

## 5. Acceptance Criteria

1. **Reconstruction Accuracy:** In a simulated partial charging session (e.g. 50% of the CC window missing), reconstructed $Q(V)$ tracks the true ground-truth trajectory with $\text{MAE} < 0.10\text{ Ah}$.
2. **Uncertainty Expansion:** In unobserved extrapolation regions, predictive variance is $> 5\times$ higher than in observed training regions.
3. **Downstream ICA Compatibility:** Passing the synthesized completed series into `ica_dva::computeIca` succeeds and yields identifiable peak signatures.
4. **Latency:** Complete GPR computation runs in $< 5.0\text{ ms}$.
5. **100% test pass rate across all suites.**
