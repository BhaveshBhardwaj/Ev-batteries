# Iteration 6: State-of-Health & Remaining Useful Life Valuation Engine — M08
## System Requirements Specification (SRS)

> **Document ID:** SRS-ITERATION-006  
> **Version:** 1.0  
> **Created:** 2026-10-03  
> **Module ID:** `M08: estimation`  
> **Target Architecture:** Indian 2W/3W Passive-Cooled Fleet Ecosystem & BPAN Tier 3  
> **Parent Architecture:** [FLOW-001 (docs/flow.md)](../../flow.md)

---

## 1. Scope

### 1.1 In Scope
- **M08 Multi-Modal State-of-Health (SoH) Valuation:**
  Fuse extracted differential capacity features (`M06: features`), thermal degradation indices (`M14: thermal`), and drift-corrected cumulative throughput (`M07: calibration`) to produce certified battery pack State-of-Health percentage:
  $$\widehat{\text{SoH}} \in [0.0, 100.0]\%$$
- **M08 Electrochemical Degradation Mode Decomposition:**
  Quantify and decouple the dual microscopic aging mechanisms:
  - **Loss of Lithium Inventory ($LLI$):** Tracked via primary peak potential shift ($\Delta V_{p1}$) and phase 1 charge area attenuation ($A_{p1}$).
  - **Loss of Active Material ($LAM$):** Tracked via primary peak amplitude reduction ($h_{p1}$) and peak width broadening.
- **M08 Remaining Useful Life (RUL) Projection under Indian Thermal Stress:**
  Project remaining operational cycles until commercial End-of-Life ($\text{SoH}_{EoL} = 70.0\%$ for Indian 2W/3W commercial duty, $80.0\%$ for standard automotive):
  $$\text{RUL} = \frac{\widehat{\text{SoH}} - \text{SoH}_{EoL}}{\dot{D}_{eff}}$$
  where effective degradation rate $\dot{D}_{eff} = \dot{D}_{base} \cdot \text{TDI}$ incorporates the Arrhenius thermal acceleration index from M14.
- **M08 Chemistry Adaptation (LFP vs. NMC):**
  Support parameterizations for both 16S LFP (characterized by flat 3.32V plateau and multi-phase graphite intercalation) and standard NMC (sloped 3.0V–4.2V curve).
- **Physical Validation against Sim-to-Real Datasets:**
  - NASA battery packs $B0005$–$B0056$ (Cycles 1 through 100+).
  - Physics-based electro-thermal synthetic sessions under $42^\circ\text{C}$ Indian summer ambient.

### 1.2 Out of Scope
- Conformal prediction confidence bands and safety abstention thresholds — formalized in **M09: uncertainty** (Iteration 7).
- BPAN Tier 3 JSON serialization and SHA-256 signing — formalized in **M13: output** (Iteration 8).

---

## 2. Gate 0: Prerequisite & Requirement Verification Audit

| Category | Prerequisite Check | Status | Evidence / Reference |
| :--- | :--- | :---: | :--- |
| **Electrochemical Features** | Prominence-filtered $V_p, h_p, A_p, V_v$ extracted | **SATISFIED** | Delivered by `M06: features` with 8D flat vector. |
| **Thermal Degradation Index** | Arrhenius TDI & high-temperature dwell metrics | **SATISFIED** | Delivered by `M14: thermal` ($E_a = 31.4\text{ kJ/mol}$). |
| **Calibrated Telemetry** | Drift-free cumulative throughput & zero-bias correction | **SATISFIED** | Delivered by `M07: calibration` ($93\%$ drift elimination). |
| **Ground-Truth Datasets** | Experimental NASA cycles + reference LFP OCV curves | **SATISFIED** | 34 NASA packs ($B0005$–$B0056$) and `lfp_16s_reference_ocv.csv`. |

---

## 3. Requirements

### 3.1 Functional Requirements

- **FR-6.01 (SoH Estimation Accuracy):** Given valid electrochemical features and nominal baseline, the module shall estimate pack SoH with Mean Absolute Error $\text{MAE} \le 1.5\%$ on reference validation cycles.
- **FR-6.02 (Degradation Mode Decomposition):** The module shall isolate $LLI\%$ and $LAM\%$ such that:
  $$\Delta \text{SoH}_{total} \approx w_{LLI} \cdot LLI + w_{LAM} \cdot LAM$$
- **FR-6.03 (Thermal-Coupled RUL):** The module shall scale projected cycle consumption rate by the session $\text{TDI}$. Under $45^\circ\text{C}$ summer conditions ($\text{TDI} \approx 2.5$), the projected remaining life in calendar days/shifts shall decrease accordingly.
- **FR-6.04 (Safety & End-of-Life Flagging):** When $\widehat{\text{SoH}} \le \text{SoH}_{EoL}$ ($70\%$ for commercial, $80\%$ for passenger), `end_of_life_reached` shall be flagged true.
- **FR-6.05 (Physical Plausibility Bounds):** The module shall clamp $\widehat{\text{SoH}}$ to $[0.0, 105.0]\%$ and flag anomalous non-monotonic increases exceeding physical healing limits ($< 2\%$ rebound).

### 3.2 Non-Functional Requirements

- **NFR-6.01 (Execution Latency):** Valuation inference must execute in **$< 0.5\text{ ms}$** per session (allocation-free, purely analytic/ensemble math).
- **NFR-6.02 (Deterministic Execution):** The valuation output must be strictly bit-identical across repeated runs given the same input vector.

---

## 4. Interfaces & Data Structures

```cpp
namespace ev::battery::estimation {

enum class DegradationDominance : uint8_t {
    BALANCED,
    LLI_DOMINATED,
    LAM_DOMINATED,
    THERMAL_ACCELERATED
};

[[nodiscard]] constexpr const char* degradationDominanceToString(DegradationDominance d) noexcept;

struct BaselineCalibration {
    double initial_peak_height = 5.0;     ///< Pristine Cycle 1 primary peak height (Ah/V)
    double initial_peak_voltage_v = 3.97; ///< Pristine Cycle 1 primary peak potential (V)
    double initial_peak_area_ah = 1.25;   ///< Pristine Cycle 1 primary peak area (Ah)
    double nominal_capacity_ah = 30.0;    ///< Rated capacity (Ah)
};

struct EstimationConfig {
    std::string chemistry = "LFP";          ///< "LFP" or "NMC"
    double eol_soh_threshold_pct = 70.0;    ///< Commercial fleet End-of-Life threshold (70% for BPAN Tier 3)
    double weight_peak_area = 0.50;         ///< Weight for peak area in SoH regression
    double weight_peak_height = 0.35;       ///< Weight for peak height (LAM proxy)
    double weight_voltage_shift = 0.15;     ///< Weight for voltage shift (LLI proxy)
    BaselineCalibration baseline;           ///< Pristine battery baseline references
};

struct EstimationInput {
    features::DifferentialCapacityFeatures ica_features;
    thermal::ThermalReport thermal_report;
    calibration::CalibrationReport calibration_report;
    double cumulative_throughput_ah = 0.0;
    double equivalent_full_cycles = 0.0;
};

struct EstimationReport {
    double soh_percent = 100.0;             ///< Certified State of Health (0.0 to 100.0%)
    double estimated_capacity_ah = 0.0;     ///< Absolute usable capacity (Ah)
    double remaining_useful_life_cycles = 0.0; ///< Projected remaining cycles until EoL
    double lli_percent = 0.0;               ///< Loss of Lithium Inventory (%)
    double lam_percent = 0.0;               ///< Loss of Active Material (%)
    double confidence_score = 0.95;         ///< Valuation confidence [0.0, 1.0]
    bool end_of_life_reached = false;       ///< True if SoH <= eol_soh_threshold_pct
    DegradationDominance dominance = DegradationDominance::BALANCED;
    std::string diagnostics_summary;
};

[[nodiscard]] common::Result<EstimationReport> estimateBatteryHealth(
    const EstimationInput& input,
    const EstimationConfig& config = {}
);

} // namespace ev::battery::estimation
```

---

## 5. Acceptance Criteria

1. **NASA Battery Pack Ground Truth ($B0005$):**
   - Cycle 10: $\widehat{\text{SoH}} \approx 98\%\text{--}100\%$.
   - Cycle 100: $\widehat{\text{SoH}} \approx 71\%\text{--}73\%$ ($\text{MAE} \le 1.5\%$ vs. experimental measured capacity).
2. **Degradation Mode Identification:** Correctly flags $LAM$ dominance when peak height attenuates faster than peak voltage shifts, and $LLI$ dominance when peak shifts $> 30\text{ mV}$.
3. **Thermal RUL Coupling:** High $\text{TDI} = 2.8$ under $45^\circ\text{C}$ reduces projected cycle life by $\approx 2.8\times$ compared to $25^\circ\text{C}$ baseline.
4. **Sub-millisecond Latency:** Valuation execution $< 0.1\text{ ms}$.
5. **100% test pass rate.**
