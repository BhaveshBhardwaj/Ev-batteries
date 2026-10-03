# Iteration 7: Conformal Prediction & Safety Abstention Engine — M09
## System Requirements Specification (SRS)

> **Document ID:** SRS-ITERATION-007  
> **Version:** 1.0  
> **Created:** 2026-10-03  
> **Module ID:** `M09: uncertainty`  
> **Target Architecture:** Indian 2W/3W Passive-Cooled Fleet Ecosystem & BPAN Tier 3  
> **Parent Architecture:** [FLOW-001 (docs/flow.md)](../../flow.md)

---

## 1. Scope

### 1.1 In Scope
- **M09 Split Conformal Prediction Intervals:**
  Compute mathematically guaranteed $90\%$ and $95\%$ coverage prediction intervals:
  $$[\text{SoH}_{low}, \text{SoH}_{high}]$$
  such that finite-sample marginal coverage is rigorously preserved:
  $$P(\text{SoH}_{true} \in [\text{SoH}_{low}, \text{SoH}_{high}]) \ge 1 - \alpha$$
- **M09 Heteroscedastic Local Difficulty Scaling:**
  Scale non-conformity scores by physical operational covariates:
  - Thermal acceleration factor ($\mu_{thermal} = 1.0 + 0.25 \max(0, \text{TDI} - 1.0)$).
  - Sensor calibration reliability ($\mu_{calib} = \text{dual\_anchors\_found} ? 1.0 : 1.30$, increased if sensor fault flagged).
  - Feature quality and prominence.
- **M09 Out-of-Distribution (OOD) Detection:**
  Detect atypical operational telemetry or unexpected electrochemical behavior via Mahalanobis/Z-score distance against reference fleet distributions.
- **M09 Automated Certification Decision & Abstention (Gate 3 Enforcement):**
  - If confidence interval band $\Delta \text{SoH} = \text{SoH}_{high} - \text{SoH}_{low} \le 5.0\%$ and $\text{OOD} = \text{false}$:
    Issue `CERTIFIED` status.
  - If $\Delta \text{SoH} > 5.0\%$ or $\text{OOD} = \text{true}$:
    Issue `AUDIT_REQUIRED` status, preventing uncertified automated risk underwriting.
- **Physical Validation against Sim-to-Real Datasets:**
  Verify that empirical coverage on holdout NASA packs ($B0005$–$B0056$) and synthetic fleet sessions meets or exceeds nominal $90\%$ and $95\%$ targets.

### 1.2 Out of Scope
- Final JSON serialization and SHA-256 cryptographic signing — formalized in **M13: output** (Iteration 8).

---

## 2. Gate 0: Prerequisite & Requirement Verification Audit

| Category | Prerequisite Check | Status | Evidence / Reference |
| :--- | :--- | :---: | :--- |
| **Point Valuation** | Certified point estimate $\widehat{\text{SoH}}$ available | **SATISFIED** | Delivered by `M08: estimation` with validated NASA/LFP grounding. |
| **Operational Covariates** | TDI, dwell fraction, and sensor calibration metrics | **SATISFIED** | Delivered by `M14: thermal` and `M07: calibration`. |
| **Calibration Set** | Pre-computed empirical non-conformity residuals | **SATISFIED** | Grounded in multi-battery NASA aging holdout errors ($\text{MAE} \approx 1.2\%$). |

---

## 3. Requirements

### 3.1 Functional Requirements

- **FR-7.01 (Coverage Guarantee):** On holdout verification datasets, the conformal prediction interval shall contain the ground-truth capacity SoH with empirical coverage $\ge 90\%$ for $\alpha = 0.10$ and $\ge 95\%$ for $\alpha = 0.05$.
- **FR-7.02 (Heteroscedastic Scaling):** Under severe summer thermal abuse ($\text{TDI} > 2.5$) or missing OCV anchors, the interval width shall widen dynamically to reflect heightened epistemic uncertainty.
- **FR-7.03 (Safety Abstention):** When the $90\%$ confidence interval width exceeds $5.0\%$ or OOD distance $z > 3.0$, the module shall set `decision = CertificationDecision::AUDIT_REQUIRED`.
- **FR-7.04 (Bounded Intervals):** Prediction intervals shall be strictly clamped within physical bounds $[0.0, 100.0]\%$.

### 3.2 Non-Functional Requirements

- **NFR-7.01 (Execution Latency):** Uncertainty quantification and decision issuance must execute in **$< 0.1\text{ ms}$** (allocation-free).
- **NFR-7.02 (Deterministic Reproducibility):** Output intervals and decisions must be strictly bit-identical across runs.

---

## 4. Interfaces & Data Structures

```cpp
namespace ev::battery::uncertainty {

enum class CertificationDecision : uint8_t {
    CERTIFIED,      ///< High confidence, tight bounds (<= 5% interval width): eligible for automated BPAN Tier 3 certification
    AUDIT_REQUIRED  ///< High uncertainty (> 5% interval width) or OOD: manual inspection / depot recalibration required
};

[[nodiscard]] constexpr const char* certificationDecisionToString(CertificationDecision d) noexcept;

struct ConformalCalibrationData {
    std::vector<double> non_conformity_scores; ///< Pre-computed normalized residuals on holdout fleet packs
};

struct UncertaintyConfig {
    double confidence_level = 0.90;             ///< Target coverage (0.90 = 90%, 0.95 = 95%)
    double max_allowed_interval_width_pct = 5.0;///< Gate 3 maximum allowed interval width for auto-certification (%)
    double ood_threshold_z = 3.0;               ///< Z-score threshold for out-of-distribution detection
    ConformalCalibrationData calibration_data;  ///< Empirical calibration distribution
};

struct UncertaintyReport {
    double soh_point_estimate_pct = 100.0;     ///< Original point estimate from M08
    double soh_lower_bound_pct = 0.0;          ///< Lower confidence bound at target coverage
    double soh_upper_bound_pct = 100.0;        ///< Upper confidence bound at target coverage
    double interval_width_pct = 0.0;           ///< Upper - Lower width (%)
    double coverage_level = 0.90;              ///< Configured coverage (e.g. 0.90)
    double local_difficulty_multiplier = 1.0;  ///< Heteroscedastic scaling factor
    double ood_score = 0.0;                    ///< Out-of-distribution distance
    bool is_out_of_distribution = false;       ///< True if ood_score > ood_threshold_z
    CertificationDecision decision = CertificationDecision::AUDIT_REQUIRED;
    std::string decision_rationale;            ///< Technical explanation for underwriters
};

[[nodiscard]] ConformalCalibrationData getDefaultCalibrationData();

[[nodiscard]] common::Result<UncertaintyReport> quantifyUncertainty(
    const estimation::EstimationReport& estimation,
    const estimation::EstimationInput& input,
    const UncertaintyConfig& config = {}
);

} // namespace ev::battery::uncertainty
```

---

## 5. Acceptance Criteria

1. **Empirical Coverage Verification:** Holdout test residuals are enclosed within computed prediction intervals with $\ge 90\%$ frequency.
2. **Heteroscedastic Adaptivity:** Severe thermal stress ($\text{TDI} = 2.8$) widens the interval by $\ge 35\%$ compared to baseline rest.
3. **Automated Abstention Trigger:** Injected out-of-distribution or noisy telemetry producing $> 5.0\%$ interval width correctly sets `AUDIT_REQUIRED`.
4. **Execution Latency:** $< 0.1\text{ ms}$.
5. **100% test pass rate.**
