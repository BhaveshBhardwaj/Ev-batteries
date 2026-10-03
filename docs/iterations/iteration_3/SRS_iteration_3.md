# Iteration 3: Electrochemical Feature Extraction — M06
## System Requirements Specification (SRS)

> **Document ID:** SRS-ITERATION-003  
> **Version:** 1.0  
> **Created:** 2026-10-03  
> **Module ID:** `M06: features`  
> **Target Chemistry:** LFP & NMC (Indian 2W/3W Passive-Cooled Fleet Ecosystem)  
> **Parent Architecture:** [FLOW-001 (docs/flow.md)](../../flow.md)

---

## 1. Scope

### 1.1 In Scope
- **M06 Peak and Valley Detection:** Detect local maxima (peaks) and local minima (valleys) in smooth $\frac{\mathrm{d}Q}{\mathrm{d}V}(V)$ and $\frac{\mathrm{d}V}{\mathrm{d}Q}(Q)$ curves via sign changes in the 1st numerical difference and prominence filtering.
- **Electrochemical Feature Vector Assembly:** Compute quantitative degradation indicators:
  - Primary peak voltage $V_{p1}$ and height $h_{p1}$.
  - Secondary peak voltage $V_{p2}$ and height $h_{p2}$ (when distinct).
  - Inter-peak valley voltage $V_{v1}$ and valley depth $h_{v1}$.
  - Peak area $A_{p1} = \int_{V_{p1} - \delta}^{V_{p1} + \delta} \frac{\mathrm{d}Q}{\mathrm{d}V} \mathrm{d}V$ (in Ah) via trapezoidal integration.
  - Total CC throughput $Q_{cc}$ and charging voltage range $\Delta V_{cc}$.
- **Degradation Mode Diagnostic Flags:**
  - $LAM$ flag: Triggered when primary peak height $h_{p1}$ attenuates significantly relative to baseline.
  - $LLI$ flag: Triggered when primary peak voltage $V_{p1}$ shifts rightward by $> 30\text{ mV}$.
- **Performance & Integration:** Latency < 5 ms; standalone compilation and test suite integration.

### 1.2 Out of Scope
- Machine learning regression (ONNX inference for SoH/RUL) — deferred to **M08: estimation** (Iteration 4).
- Conformal prediction intervals — deferred to **M09: uncertainty** (Iteration 5).

---

## 2. Gate 0: Prerequisite & Requirement Verification Audit

| Category | Prerequisite Check | Status | Evidence / Reference |
| :--- | :--- | :---: | :--- |
| **Input Data** | Smooth, uniform $\frac{\mathrm{d}Q}{\mathrm{d}V}$ curves with known grid step | **SATISFIED** | `M05: ica_dva` delivers `IcaCurve` with verified 5 mV resolution. |
| **Ground Truth Data** | Multi-cycle real-world NASA aging curves | **SATISFIED** | NASA B0005, B0006, B0018 cycles 10, 50, 100 available in `data/reference/nasa/`. |
| **Mathematical Primitives** | Peak detection, prominence calculation, trapz integration | **SATISFIED** | `math_utils::trapz` and differential calculus kernels available. |
| **Domain Thresholds** | Prominence limits and peak integration window bounds | **SATISFIED** | Prominence $\ge 0.10 \cdot \max(dQ/dV)$; integration window $\delta = 40\text{ mV}$. |

---

## 3. Requirements

### 3.1 Functional Requirements

- **FR-3.01 (Peak Picking via Prominence):** The module shall identify local maxima in $\frac{\mathrm{d}Q}{\mathrm{d}V}$ where the peak prominence exceeds a configurable threshold (default: 10% of maximum peak height), effectively ignoring minor numerical ripples.
- **FR-3.02 (Primary & Secondary Peak Ordering):** The module shall sort detected peaks in descending order of prominence, designating the highest prominence peak as primary ($V_{p1}, h_{p1}$) and the next prominent peak as secondary ($V_{p2}, h_{p2}$).
- **FR-3.03 (Inter-Peak Valley Detection):** The module shall identify the lowest local minimum between $V_{p1}$ and $V_{p2}$ to characterize the phase transition valley ($V_{v1}, h_{v1}$).
- **FR-3.04 (Peak Area Integration):** The module shall compute the integrated peak area $A_{p1}$ around $V_{p1}$ over a window of $[V_{p1} - \text{half\_window}, V_{p1} + \text{half\_window}]$ using numerical trapezoidal integration.
- **FR-3.05 (Feature Vector Assembly):** The module shall assemble all extracted parameters into a flat, normalized vector of `double` values suitable for direct input into downstream ML estimators (`M08`).
- **FR-3.06 (Degradation Monotonicity Validation):** When evaluated on NASA B0005 Cycle 10 vs Cycle 100, the extracted feature vector shall demonstrate monotonic reduction in both peak height $h_{p1}$ and peak area $A_{p1}$, along with a positive voltage shift in $V_{p1}$.

### 3.2 Non-Functional Requirements

- **NFR-3.01 (Execution Latency):** Feature extraction over an `IcaCurve` of up to 500 grid points shall execute in **< 5 ms** on a single thread.
- **NFR-3.02 (Memory Safety):** Extraction shall operate with bounded memory allocation; zero dynamic memory allocations in peak detection loops.

---

## 4. Interfaces & Data Structures

```cpp
namespace ev::battery::features {

struct FeatureConfig {
    double min_prominence_ratio = 0.10;   ///< Minimum prominence as fraction of max peak (default 10%)
    double peak_area_half_width_v = 0.040; ///< Integration half-window around Vp: +/-40 mV
};

struct ElectrochemicalFeatures {
    double primary_peak_voltage_v = 0.0;    ///< V_p1 (V)
    double primary_peak_height = 0.0;       ///< h_p1 (Ah/V)
    double primary_peak_area_ah = 0.0;      ///< A_p1 (Ah)
    double secondary_peak_voltage_v = 0.0;  ///< V_p2 (V, 0 if not present)
    double secondary_peak_height = 0.0;     ///< h_p2 (Ah/V, 0 if not present)
    double valley_voltage_v = 0.0;          ///< V_v1 (V)
    double valley_height = 0.0;             ///< h_v1 (Ah/V)
    double total_cc_charge_ah = 0.0;        ///< Total charge delivered in CC window (Ah)
    double voltage_span_v = 0.0;            ///< V_end - V_start of CC window (V)
    bool has_secondary_peak = false;

    /// Convert to flat feature vector for ML input
    [[nodiscard]] std::vector<double> toVector() const;
};

[[nodiscard]] common::Result<ElectrochemicalFeatures> extractIcaFeatures(
    const ica_dva::IcaCurve& ica,
    const FeatureConfig& config = {}
);

} // namespace ev::battery::features
```

---

## 5. Acceptance Criteria

1. **Synthetic Peak Accuracy:** On a synthetic single-peak curve, $V_{p1}$ matches ground truth within $\pm 5\text{ mV}$ and $A_{p1}$ matches analytic integral within $2\%$.
2. **Real NASA Grounding:** Evaluated on NASA B0005, Cycle 10 vs Cycle 100 features demonstrate:
   - $h_{p1}(\text{C10}) > h_{p1}(\text{C100})$ with $\ge 20\%$ drop.
   - $A_{p1}(\text{C10}) > A_{p1}(\text{C100})$ with $\ge 20\%$ drop.
   - $V_{p1}(\text{C100}) \ge V_{p1}(\text{C10})$.
3. **Execution Latency:** < 5 ms for 500-point curve.
4. **All unit and integration tests pass.**
