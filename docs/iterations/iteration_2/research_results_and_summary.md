# Iteration 2: Incremental Capacity & Differential Voltage Analysis (ICA/DVA) — Research & Results Summary

> **Document ID:** SUMMARY-ITERATION-002  
> **Module ID:** `M05: ica_dva`  
> **Status:** Completed & Validated  
> **Parent Documentation:** [SRS_iteration_2.md](SRS_iteration_2.md), [decisions_iteration_2.md](decisions_iteration_2.md)

---

## 1. Executive Summary

Iteration 2 delivered the core scientific engine of the EV Battery Health Certification platform: **Module M05 (ICA/DVA Transformation)**. This module translates noisy, irregular constant-current (CC) charge telemetry into uniform Incremental Capacity ($\frac{\mathrm{d}Q}{\mathrm{d}V}(V)$) and Differential Voltage ($\frac{\mathrm{d}V}{\mathrm{d}Q}(Q)$) curves.

By utilizing Steffen's monotone cubic spline interpolation and Savitzky-Golay numerical derivatives on a standardized 5 mV voltage grid, the module eliminates division-by-zero artifacts and non-monotonic sensor noise, exposing the fundamental electrochemical phase transitions without damping peak heights.

---

## 2. Research Findings & Electrochemical Grounding

### 2.1 Theoretical Validation against Real-World NASA Aging Telemetry
We benchmarked the ICA engine across cycles of the real-world NASA Li-ion battery aging dataset (`B0005` in `data/reference/nasa/`):

| Cycle Number | State of Health Context | CC Delivered Charge | Peak Amplitude ($\frac{\mathrm{d}Q}{\mathrm{d}V}_{max}$) | Peak Voltage ($V_p$) | Observable Electrochemical Degradation |
| :---: | :---: | :---: | :---: | :---: | :--- |
| **Cycle 010** | Fresh / Baseline | $1.260\text{ Ah}$ | **$4.950\text{ Ah/V}$** | $3.970\text{ V}$ | Sharp structural phase peak at cathode plateau. |
| **Cycle 050** | Mid-life Degradation | $1.152\text{ Ah}$ | **$4.312\text{ Ah/V}$** | $3.985\text{ V}$ | Attenuation of phase peak; subtle upward potential shift. |
| **Cycle 100** | Aged Cell | $0.903\text{ Ah}$ | **$3.567\text{ Ah/V}$** | $4.010\text{ V}$ | **28% decrease** in peak height ($LAM$); **+40 mV shift** ($LLI$ & internal resistance rise). |

**Conclusion:** The M05 transformation reliably isolates the aging indicators ($LAM$ and $LLI$) described in the project charter without requiring external laboratory testing hardware.

### 2.2 Mathematical Insights
- **Steffen Monotone Spline vs Standard Cubic Splines:** Standard cubic splines introduce Runge-phenomenon overshoots when interpolating steep voltage steps, generating artificial false derivative peaks. Steffen's method mathematically clamps the first derivatives to ensure strict monotonicity ($Q[i+1] \ge Q[i]$), guaranteeing clean derivatives.
- **Savitzky-Golay Scaling:** Differentiating on an equidistant grid with physical step scaling ($\Delta V = 5\text{ mV}$) avoids point-by-point division by small $\Delta V$, reducing derivative variance by $> 95\%$ compared to raw finite differences.
- **Edge Effect Rejection:** Boundary margins ($W \approx 11\text{ to }15$) are necessary to prevent boundary extrapolation curl from being misinterpreted as phase peaks.

---

## 3. Performance & Benchmark Verification

Benchmarks were evaluated on `ev_battery_bench` (`BM_ComputeIca`):

| Benchmark Workload | Telemetry Sample Count | Latency (Single Core, Debug) | Throughput | Target Requirement | Status |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **Short Partial Charge** | 1,000 samples | **0.51 ms** | $1.93\times 10^6\text{ samples/s}$ | $< 10\text{ ms}$ | **PASSED ($19\times$ margin)** |
| **Standard 1-Hour CC** | 4,096 samples | **1.95 ms** | $2.10\times 10^6\text{ samples/s}$ | $< 15\text{ ms}$ | **PASSED ($7.7\times$ margin)** |
| **Heavy Long-Duration CC** | 10,000 samples | **2.72 ms** | $3.67\times 10^6\text{ samples/s}$ | $< 50\text{ ms}$ | **PASSED ($18\times$ margin)** |

---

## 4. Test Suite Integrity

All **84 unit and integration tests** in the test suite pass with zero errors:
- `test_calculus.cpp`: Monotone cubic splines, trapezoidal integration.
- `test_filters.cpp`: Savitzky-Golay coefficients, derivative scaling.
- `test_cleaning.cpp`: Hampel outlier rejection, stuck sensor detection.
- `test_segmentation.cpp`: Dual-threshold FSM regime classification, CC window extraction.
- `test_denoising.cpp`: Adaptive Kalman filter, cascaded Savitzky-Golay.
- `test_ica_dva.cpp`: Synthetic plateau peak identification, real-world NASA Cycle 10 vs 100 peak attenuation and voltage shift validation.
- `test_pipeline_iteration_1.cpp`: End-to-end integration pipeline.
