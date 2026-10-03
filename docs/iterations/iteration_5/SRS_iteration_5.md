# Iteration 5: Current-Sensor Self-Calibration — M07
## System Requirements Specification (SRS)

> **Document ID:** SRS-ITERATION-005  
> **Version:** 1.0  
> **Created:** 2026-10-03  
> **Module ID:** `M07: calibration`  
> **Target Architecture:** Indian 2W/3W Passive-Cooled Fleet Ecosystem & BPAN Tier 3  
> **Parent Architecture:** [FLOW-001 (docs/flow.md)](../../flow.md)

---

## 1. Scope

### 1.1 In Scope
- **M07 Rest-Anchor Zero-Current Offset Regression:**
  Extract resting intervals ($|I| \le I_{rest}$, dwell $\ge t_{min}$) from segmented telemetry and compute the true sensor zero-current bias:
  $$\hat{\alpha} = \frac{1}{N_{rest}} \sum_{k \in \text{rest}} I_k$$
- **M07 Dual OCV Anchor Matching & Gain Error Regression:**
  Identify resting equilibrium voltage states before and after operational charge/discharge intervals. Convert equilibrium OCV to reference SoC ($\text{SoC} = \text{OCV}^{-1}(V_{rest})$). Compare theoretical delivered charge $\Delta Q_{true} = Q_{nom} \cdot \Delta \text{SoC}$ against measured integral to regress gain error $\hat{\beta}$:
  $$\hat{\beta} = \frac{\int I_{meas} \mathrm{d}t - \hat{\alpha} \cdot \Delta t}{\Delta Q_{true}}$$
- **M07 Closed-Loop Telemetry Correction:**
  Apply calibrated correction to time series:
  $$I_{corr}(t) = \frac{I_{meas}(t) - \hat{\alpha}}{\hat{\beta}}$$
  $$Q_{corr}(t) = \int_{t_0}^t I_{corr}(\tau) \mathrm{d}\tau$$
- **Hardware Fault & Degradation Diagnostics (Gate 2 Enforcement):**
  - Trigger `ALERT_SENSOR_OFFSET_EXCESSIVE` if $|\hat{\alpha}| > 1.5\text{ A}$.
  - Trigger `ALERT_SENSOR_GAIN_EXCESSIVE` if $|\hat{\beta} - 1.0| > 0.08$.
  - Trigger `ALERT_SENSOR_DEGRADED` if residual calibration error $> 2.0\%$.
- **Physics-Based Sensor Degradation Simulation for Testing:**
  Test using a physical sensor error injection model ($I_{meas} = \beta_{true} \cdot I_{true} + \alpha_{true} + \mathcal{N}(0, \sigma^2)$) on synthetic vehicle sessions.

### 1.2 Out of Scope
- Machine learning regression for SoH — deferred to **M08: estimation** (Iteration 6).
- Real-time on-chip EEPROM calibration write-back — M07 executes as a cloud SaaS telemetry correction layer.

---

## 2. Gate 0: Prerequisite & Requirement Verification Audit

| Category | Prerequisite Check | Status | Evidence / Reference |
| :--- | :--- | :---: | :--- |
| **Input Telemetry** | Segmented session with labeled `REST` segments | **SATISFIED** | Delivered by `M03: segmentation` with verified dwell debouncing. |
| **Equilibrium OCV Model** | Monotonic OCV-to-SoC lookup table or analytic curve | **SATISFIED** | High-resolution LFP reference curve in `data/reference/lfp/lfp_16s_reference_ocv.csv`. |
| **BMS Sensor Tolerances** | Realistic hardware limits for shunt/Hall sensors | **SATISFIED** | Offset tolerance $\pm 1.5\text{ A}$, gain error $\pm 5\%$, noise $\sigma \approx 0.15\text{ A}$. |
| **Physics Ground Truth** | Controlled synthetic test bench with known injected errors | **SATISFIED** | Ground-truth verification against pre-injected $\alpha = 0.65\text{ A}$, $\beta = 1.015$. |

---

## 3. Requirements

### 3.1 Functional Requirements

- **FR-5.01 (Rest Offset Regression):** When one or more valid `REST` segments ($\ge 30\text{ s}$ duration) exist, the module shall estimate sensor offset $\hat{\alpha}$ with accuracy $|\hat{\alpha} - \alpha_{true}| \le 0.05\text{ A}$.
- **FR-5.02 (Dual OCV Anchor Identification):** The module shall scan the session for initial and final resting segments separated by a significant charge/discharge throughput ($|\Delta \text{SoC}| \ge 10\%$).
- **FR-5.03 (Gain Error Estimation):** When dual OCV anchors are present, the module shall compute gain factor $\hat{\beta}$ with relative error $|\hat{\beta} - \beta_{true}| \le 0.015$.
- **FR-5.04 (Calibrated Series Generation):** The module shall output a corrected `InternalTimeSeries` where current samples are mapped via $I_{corr} = (I_{raw} - \hat{\alpha}) / \hat{\beta}$ and cumulative throughput is drift-free.
- **FR-5.05 (Fault Flagging):** If estimated offset $|\hat{\alpha}| > 1.5\text{ A}$ or estimated gain $|\hat{\beta} - 1.0| > 0.08$, the module shall flag a sensor hardware fault in the calibration report.

### 3.2 Non-Functional Requirements

- **NFR-5.01 (Latency):** Complete self-calibration of a 10,000-sample session in **< 10 ms** on a single thread.
- **NFR-5.02 (Robust Fallback):** If no dual OCV anchors exist (e.g., continuous driving with no post-drive rest), the module shall fall back to offset-only correction ($\hat{\beta} = 1.0$) without erroring.

---

## 4. Interfaces & Data Structures

```cpp
namespace ev::battery::calibration {

struct OcvPoint {
    double soc;       ///< State of Charge (0.0 to 1.0)
    double voltage_v; ///< Equilibrium OCV (V)
};

struct CalibrationConfig {
    double max_rest_current_a = 0.50;      ///< Maximum threshold to qualify as rest (A)
    double min_rest_duration_s = 30.0;     ///< Minimum duration to eliminate capacitive transient (s)
    double max_allowed_offset_a = 1.50;    ///< Hardware fault limit for offset (A)
    double max_allowed_gain_error = 0.08;  ///< Hardware fault limit for gain error (|beta - 1.0|)
    double min_soc_delta_for_gain = 0.08;  ///< Minimum SoC change required to regress gain beta
    std::vector<OcvPoint> ocv_table;       ///< Monotonic OCV-to-SoC lookup table
};

struct CalibrationReport {
    double estimated_offset_a = 0.0;       ///< Estimated sensor zero-bias alpha (A)
    double estimated_gain = 1.0;           ///< Estimated sensor gain beta
    double initial_soc = 0.0;              ///< Pre-charge/discharge SoC from OCV anchor
    double final_soc = 0.0;                ///< Post-charge/discharge SoC from OCV anchor
    double raw_coulomb_ah = 0.0;           ///< Uncalibrated integral
    double calibrated_coulomb_ah = 0.0;    ///< Drift-corrected integral
    bool dual_anchors_found = false;       ///< True if both initial and final OCV anchors were identified
    bool sensor_fault_detected = false;    ///< True if offset or gain exceeds physical fault limits
    std::string fault_message;
};

[[nodiscard]] common::Result<std::pair<common::InternalTimeSeries, CalibrationReport>> calibrateTimeSeries(
    const common::InternalTimeSeries& series,
    const std::vector<segmentation::Segment>& segments,
    const CalibrationConfig& config = {}
);

} // namespace ev::battery::calibration
```

---

## 5. Acceptance Criteria

1. **Synthetic Sensor Calibration Ground Truth:** Injected errors ($\alpha = 0.65\text{ A}$, $\beta = 1.015$) are recovered within $|\hat{\alpha} - 0.65| \le 0.05\text{ A}$ and $|\hat{\beta} - 1.015| \le 0.015$.
2. **Drift Elimination:** The cumulative integration error over a 1-hour session with 0.65A drift is reduced by $\ge 90\%$.
3. **Execution Latency:** < 10 ms for 10,000 samples.
4. **All unit and integration tests pass.**
