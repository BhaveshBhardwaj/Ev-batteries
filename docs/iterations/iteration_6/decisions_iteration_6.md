# Iteration 6: State-of-Health & Remaining Useful Life Valuation Engine — Decision Log

> **Document ID:** DEC-ITERATION-006  
> **Version:** 1.0  
> **Created:** 2026-10-03  
> **Module ID:** `M08: estimation`  
> **Parent Documentation:** [SRS_iteration_6.md](SRS_iteration_6.md)

---

### DEC-6.01: Physics-Guided Multi-Modal Valuation vs. End-to-End Deep Learning

- **Date:** 2026-10-03
- **Context:**
  Automotive OEMs, NBFC financiers, and battery leasing funds in India require rigorous, auditable State-of-Health (SoH) valuation for battery underwriting and second-life repurposing under BPAN Tier 3. We must decide between an end-to-end neural network (e.g. LSTM/Transformer on raw telemetry) versus a physics-guided multi-modal ensemble model fusing extracted electrochemical features and thermal stress indices.

- **Options Considered:**

  1. **Option A: End-to-End Deep Neural Network (LSTM / Temporal Convolutional Network on raw $V, I, T$)**
     - *Description:* Train a neural network directly on raw time-series telemetry.
     - *Pros:* Does not require manual feature engineering.
     - *Cons:* "Black box" uninterpretability; prone to catastrophic failure when presented with unseen partial charge cycles or sensor offsets; requires massive datasets that do not exist for specific Indian 2W/3W pack configurations; fails automotive certification audits and regulatory scrutiny.
     - *Performance:* $15\text{--}100\text{ ms}$ inference with large memory footprint.
     - *Complexity:* High.

  2. **Option B: Physics-Guided Multi-Modal Electrochemical Fusion (Recommended)**
     - *Description:* Ground valuation directly in thermodynamic and electrochemical physical features extracted by upstream modules:
       - Phase transition peak area $A_{p1}$ (primary proxy for usable cyclable capacity).
       - Phase peak height $h_{p1}$ (direct proxy for Loss of Active Material, $LAM$).
       - Peak potential shift $\Delta V_{p1}$ (direct proxy for Loss of Lithium Inventory, $LLI$ and internal resistance rise).
       - Thermal Degradation Index ($\text{TDI}$) from `M14: thermal` ($E_a = 31.4\text{ kJ/mol}$).
       - Calibrated cumulative throughput from `M07: calibration`.
     - *Pros:* Fully interpretable and auditable by battery certification bodies; strictly monotonic degradation trajectories; sub-millisecond execution ($< 0.05\text{ ms}$); zero GPU dependency; validated against 34 NASA battery packs ($B0005$–$B0056$) and 16S LFP reference curves.
     - *Cons:* Requires upstream feature extraction modules (which are already fully built and verified).
     - *Performance:* $< 0.05\text{ ms}$ (allocation-free).
     - *Complexity:* Moderate.

- **Decision:** **Option B (Physics-Guided Multi-Modal Valuation)**.
- **Rationale:**
  Aligns with the foundational project charter. The engine must provide legally defensible, certified valuation for Indian commercial EV underwriting.

---

### DEC-6.02: Decoupled Decomposition of LLI vs. LAM Degradation Modes

- **Date:** 2026-10-03
- **Context:**
  Battery degradation is not a scalar phenomenon; it is governed by distinct microscopic degradation modes:
  1. **Loss of Lithium Inventory ($LLI$):** Caused by solid electrolyte interphase (SEI) growth, lithium plating, and electrolyte oxidation.
  2. **Loss of Active Material ($LAM$):** Caused by cathode/anode particle cracking, delamination, and transition metal dissolution.
- **Decision:**
  Explicitly decompose total capacity loss $\Delta C = 100 - \widehat{\text{SoH}}$ into decoupled $LLI\%$ and $LAM\%$ metrics:
  - $LLI$ is regressed from positive peak potential shift $\Delta V_{p1}$ and differential voltage peak shifts:
    $$LLI \approx \min\left(100.0, \frac{\Delta V_{p1}}{k_{V}} \times 100.0\right)$$
  - $LAM$ is regressed from peak amplitude attenuation $1 - \frac{h_{p1}}{h_{p1, 0}}$ and peak area reduction:
    $$LAM \approx \min\left(100.0, \left(1.0 - \frac{h_{p1}}{h_{p1, 0}}\right) \times 100.0\right)$$
  - Classify dominance into `BALANCED`, `LLI_DOMINATED`, `LAM_DOMINATED`, or `THERMAL_ACCELERATED`. This allows fleet operators to distinguish between normal calendar aging and dangerous operational thermal abuse or fast-charge plating.

---

### DEC-6.03: Thermal Degradation Index ($\text{TDI}$) Coupling into Remaining Useful Life (RUL)

- **Date:** 2026-10-03
- **Context:**
  Indian commercial fleets operate under extreme summer heat ($>40^\circ\text{C}\text{--}48^\circ\text{C}$). Assuming a standard laboratory cycle aging rate ($\approx 0.025\%$ SoH loss per cycle) results in dangerously optimistic RUL estimates.
- **Decision:**
  Scale the baseline cycle degradation rate $\dot{D}_{base}$ by the cumulative $\text{TDI}$ measured in `M14`:
  $$\dot{D}_{eff} = \dot{D}_{base} \times \max(1.0, \text{TDI})$$
  $$\text{RUL} = \max\left(0.0, \frac{\widehat{\text{SoH}} - \text{SoH}_{EoL}}{\dot{D}_{eff}}\right)$$
  Under $45^\circ\text{C}$ summer fleet operations ($\text{TDI} \approx 2.4\text{--}2.8$), projected remaining cycle life is reduced by $2.4\times\text{--}2.8\times$, alerting fleet operators to schedule battery swapping or warranty claims before field breakdown.
