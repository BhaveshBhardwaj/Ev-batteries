# Iteration 2: Incremental Capacity Analysis (ICA) & Differential Voltage Analysis (DVA) — M05
## System Requirements Specification (SRS)

> **Document ID:** SRS-ITERATION-002  
> **Version:** 1.0  
> **Created:** 2026-10-03  
> **Module ID:** `M05: ica_dva`  
> **Target Chemistry:** LFP & NMC (Indian 2W/3W Passive-Cooled Pack Ecosystem)  
> **Parent Architecture:** [FLOW-001 (docs/flow.md)](../../flow.md)

---

## 1. Scope

### 1.1 In Scope
- **M05 Incremental Capacity Analysis (ICA):** Transform constant-current (CC) charge segment telemetry $(V(t), I(t))$ into uniform differential capacity curves $\frac{\mathrm{d}Q}{\mathrm{d}V}(V)$.
- **M05 Differential Voltage Analysis (DVA):** Transform constant-current segment telemetry into uniform differential voltage curves $\frac{\mathrm{d}V}{\mathrm{d}Q}(Q)$.
- **Monotone Interpolation & Uniform Resampling:** Resample non-uniform $V(t)$ telemetry onto an equidistant voltage grid $\Delta V$ using Steffen's monotone cubic spline (`math_utils::monotoneCubicInterp`) to eliminate interpolation overshoot and spurious oscillations.
- **Noise-Robust Numerical Differentiation:** Compute $\frac{\mathrm{d}Q}{\mathrm{d}V}$ and $\frac{\mathrm{d}V}{\mathrm{d}Q}$ using calibrated Savitzky-Golay derivative filters (`math_utils::savgolDerivative`).
- **Validation against Real-World Datasets:** Validate against real NASA Li-ion battery charge cycles (`B0005`, `B0006`, `B0018`) across fresh (Cycle 001) and degraded states (Cycle 050, Cycle 100), observing characteristic peak shrinkage and potential shift.

### 1.2 Out of Scope
- Peak picking and feature extraction ($V_p, h_p, A_p$) — deferred to **M06: features** (Iteration 3).
- Machine learning regression / ONNX inference for SoH/RUL — deferred to **M08: estimation** (Iteration 4).
- Gaussian Process Regression (GPR) missing-data extrapolation for <20% SoC fragments — deferred to offline expansion.

---

## 2. Gate 0: Prerequisite & Requirement Verification Audit

| Category | Prerequisite Check | Status | Evidence / Reference |
| :--- | :--- | :---: | :--- |
| **Input Data** | Real-world continuous CC charge telemetry logs | **SATISFIED** | NASA PCoE `B0005`, `B0006`, `B0007`, `B0018` cycle CSVs in `data/reference/nasa/`. |
| **Signal Quality** | Outlier-cleaned, segmented, and denoised CC segments | **SATISFIED** | `M02: cleaning`, `M03: segmentation`, `M04: denoising` pass integration test pipeline with latency < 8 ms. |
| **Mathematical Primitives** | Monotone interpolation, cumulative integration, SavGol derivatives | **SATISFIED** | `ev::battery::math_utils` kernel verified with unit tests & benchmarks. |
| **Domain Parameters** | Cell voltage operating limits, resolution $\Delta V$ | **SATISFIED** | NMC: 3.40V to 4.20V; LFP: 3.00V to 3.50V; standard grid spacing $\Delta V = 5\text{ mV}$. |
| **Validation Reference** | Ground-truth capacity fade and degradation trajectory | **SATISFIED** | `B0005_capacity_fade.csv` through `B0056_capacity_fade.csv` available. |

---

## 3. Requirements

### 3.1 Functional Requirements

- **FR-2.01 (Coulomb Integration):** The module shall integrate current $I(t)$ over time for the CC segment using cumulative trapezoidal integration to compute delivered charge $Q(t) = \int_{t_0}^t I(\tau) \mathrm{d}\tau$ in Ampere-hours (Ah).
- **FR-2.02 (Voltage Monotonicity Verification & Grid Generation):** The module shall construct a uniformly spaced voltage grid $V_{grid} = [V_{start}, V_{start} + \Delta V, \dots, V_{end}]$ across the monotonic voltage range of the CC charging window.
- **FR-2.03 (Steffen Monotone Resampling):** The module shall resample cumulative charge $Q$ onto $V_{grid}$ using Steffen's monotone cubic spline interpolation, guaranteeing that no false oscillations or unphysical negative slopes are introduced into $Q(V)$.
- **FR-2.04 (Savitzky-Golay Differential Capacity):** The module shall compute $\frac{\mathrm{d}Q}{\mathrm{d}V}$ on $V_{grid}$ using a Savitzky-Golay 1st-derivative kernel with configurable window length ($W \in [5, 31]$, odd) and polynomial degree ($P \in [2, 4]$).
- **FR-2.05 (Differential Voltage Transformation):** The module shall resample terminal voltage $V$ onto a uniform charge grid $Q_{grid}$ ($\Delta Q \in [0.005, 0.02]\text{ Ah}$) and compute $\frac{\mathrm{d}V}{\mathrm{d}Q}$ using Savitzky-Golay differentiation.
- **FR-2.06 (Degradation Tracking Feasibility):** When evaluated on Cycle 1 vs. Cycle 100 of the NASA reference dataset, the calculated ICA curve shall exhibit clear primary peak attenuation (decrease in peak height $\ge 15\%$) reflecting capacity loss.

### 3.2 Non-Functional Requirements

- **NFR-2.01 (Execution Latency):** Full ICA transformation of a 1-hour charge window (3600 samples) onto a 5 mV grid (approx. 160 grid points) shall execute in **< 15 ms** on a single thread.
- **NFR-2.02 (Memory Safety):** All arrays used in intermediate transformations shall be pre-allocated; zero heap allocations shall occur within inner differentiation loops.
- **NFR-2.03 (Numerical Stability):** Derivative calculation shall produce strictly finite values (no `NaN` or `Inf`). Voltage reversals or non-monotonic transients in raw inputs shall be sanitized without throwing exceptions.

---

## 4. Interfaces & Data Structures

```cpp
namespace ev::battery::ica_dva {

struct IcaConfig {
    double voltage_step_v = 0.005;    ///< Grid spacing: 5 mV (default)
    int savgol_window = 11;           ///< SG filter window (odd integer >= 5)
    int savgol_poly_order = 2;        ///< SG polynomial degree (2 or 3)
    double min_voltage_span_v = 0.20; ///< Minimum voltage span required for valid ICA (V)
};

struct DvaConfig {
    double capacity_step_ah = 0.01;   ///< Grid spacing: 10 mAh (default)
    int savgol_window = 11;           ///< SG filter window
    int savgol_poly_order = 2;        ///< SG polynomial degree
};

struct IcaCurve {
    std::vector<double> voltage_v;    ///< Uniform voltage grid (V)
    std::vector<double> charge_ah;    ///< Resampled cumulative charge Q(V)
    std::vector<double> dq_dv;        ///< Incremental capacity dQ/dV (Ah/V)
    double peak_voltage_v = 0.0;      ///< Voltage corresponding to maximum dQ/dV
    double max_dq_dv = 0.0;           ///< Amplitude of dominant peak
};

struct DvaCurve {
    std::vector<double> capacity_ah;  ///< Uniform capacity grid (Ah)
    std::vector<double> voltage_v;    ///< Resampled terminal voltage V(Q)
    std::vector<double> dv_dq;        ///< Differential voltage dV/dQ (V/Ah)
};

[[nodiscard]] common::Result<IcaCurve> computeIca(
    const common::InternalTimeSeries& cc_segment,
    const IcaConfig& config = {}
);

[[nodiscard]] common::Result<DvaCurve> computeDva(
    const common::InternalTimeSeries& cc_segment,
    const DvaConfig& config = {}
);

} // namespace ev::battery::ica_dva
```

---

## 5. Acceptance Criteria

1. **Monotone Resampling Integrity:** $Q(V)$ array resampled via Steffen spline must be monotonically non-decreasing for charging segments ($Q[i+1] \ge Q[i]$ everywhere).
2. **Smooth Derivative:** The resultant $\frac{\mathrm{d}Q}{\mathrm{d}V}$ curve on NASA B0005 Cycle 1 must exhibit smooth, well-defined electrochemical phase peaks without high-frequency ringing.
3. **Aging Trend Verification:** Unit/Integration test on NASA B0005 Cycle 1 vs. Cycle 100 must verify that the peak height of the primary phase transition peak decreases monotonically with aging.
4. **Isolated Compilation:** `ev_battery_ica_dva` builds, tests, and benchmarks independently.
