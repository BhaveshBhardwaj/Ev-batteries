# Iteration 4: Thermal Stress Modeling — M14
## System Requirements Specification (SRS)

> **Document ID:** SRS-ITERATION-004  
> **Version:** 1.0  
> **Created:** 2026-10-03  
> **Module ID:** `M14: thermal`  
> **Target Chemistry:** LFP & NMC (Indian 2W/3W Passive-Cooled Pack Ecosystem)  
> **Parent Architecture:** [FLOW-001 (docs/flow.md)](../../flow.md)

---

## 1. Scope

### 1.1 In Scope
- **M14 Arrhenius Cumulative Thermal Aging Integration:**
  Compute temperature-dependent reaction rate acceleration relative to $25^\circ\text{C}$ (298.15 K) baseline using the electrochemical Arrhenius relation:
  $$k(T) = \exp\left(-\frac{E_a}{R} \cdot \left(\frac{1}{T + 273.15} - \frac{1}{298.15}\right)\right)$$
  where $E_a \approx 31.4\text{ kJ/mol}$ for SEI growth on the graphite anode and $R = 8.314\text{ J/(mol}\cdot\text{K)}$.
- **High-Temperature Dwell Time Tracking:**
  Quantify cumulative duration and percentage of operational time spent in critical thermal degradation zones:
  - Warning zone: $T \ge 40^\circ\text{C}$
  - Severe thermal abuse zone: $T \ge 45^\circ\text{C}$
  - Extreme runaway risk zone: $T \ge 55^\circ\text{C}$
- **Thermal Degradation Index (TDI):**
  Integrate electro-thermal stress combining Arrhenius aging multiplier with normalized C-rate current weighting:
  $$\text{TDI} = \frac{1}{\Delta t} \int_{t_0}^{t_1} k(T(t)) \cdot \left(1 + \gamma \cdot \frac{|I(t)|}{I_{1C}}\right) \mathrm{d}t$$
- **Thermal Rise Rate ($\frac{\mathrm{d}T}{\mathrm{d}t}$) Detection:**
  Estimate maximum thermal rate-of-rise ($^\circ\text{C/min}$) during high-current discharging and CC charging to diagnose internal resistance ($DCIR$) growth and electrolyte conductivity loss.
- **Physics-Based Thermal Simulation for Testing:**
  Develop a lumped-parameter thermal model in tests:
  $$C_{th} \frac{\mathrm{d}T}{\mathrm{d}t} = I(t)^2 R_{int}(T, \text{SoH}) + I(t) T \frac{\partial U_{ocv}}{\partial T} - h_{conv} A (T - T_{amb})$$
  simulating Indian summer conditions ($T_{amb} = 42\text{–}48^\circ\text{C}$) with passive air cooling to validate M14 metrics.

### 1.2 Out of Scope
- Direct machine learning SoH weighting — deferred to **M08: estimation** (Iteration 5).
- Multi-node 3D thermal finite element analysis — out of scope for lightweight BMS cloud telemetry analytics.

---

## 2. Gate 0: Prerequisite & Requirement Verification Audit

| Category | Prerequisite Check | Status | Evidence / Reference |
| :--- | :--- | :---: | :--- |
| **Input Data** | Cleaned time series containing $t$ (s), $T$ ($^\circ\text{C}$), and $I$ (A) | **SATISFIED** | Delivered by `M02: cleaning` with repaired timestamps and outlier-free temperature. |
| **Physical Constants** | Universal gas constant $R$, Arrhenius activation energy $E_a$, reference temperature | **SATISFIED** | $R = 8.314462\text{ J/(mol}\cdot\text{K)}$, $E_{a,sei} = 31.4\text{ kJ/mol}$, $T_{ref} = 298.15\text{ K}$. |
| **Thermal Zones** | Indian automotive operating thresholds | **SATISFIED** | $40^\circ\text{C}$ (summer fleet baseline), $45^\circ\text{C}$ (accelerated SEI decomposition), $55^\circ\text{C}$ (safety alert). |
| **Physics Ground Truth** | Lumped electro-thermal energy balance equation | **SATISFIED** | Verified 1-state ODE heat generation ($Q_{joule} + Q_{rev} - Q_{diss}$). |

---

## 3. Functional & Non-Functional Requirements

### 3.1 Functional Requirements
- **FR-4.01 (Arrhenius Acceleration Factor):** For every sample, calculate instantaneous thermal acceleration factor $k(T) \ge 0$. At $25^\circ\text{C}$, $k(25) = 1.0$; at $45^\circ\text{C}$, $k(45) \approx 2.15\text{--}2.25$.
- **FR-4.02 (Cumulative Dwell Statistics):** Compute cumulative exposure time (seconds) and percentage of session spent above $40^\circ\text{C}$, $45^\circ\text{C}$, and $55^\circ\text{C}$.
- **FR-4.03 (TDI Computation):** Numerically integrate the coupled electro-thermal stress function over the entire session using trapezoidal quadrature, yielding a normalized dimensionless Thermal Degradation Index ($\text{TDI} \ge 1.0$).
- **FR-4.04 (Peak dT/dt Tracking):** Compute maximum temperature gradient $\left(\frac{\mathrm{d}T}{\mathrm{d}t}\right)_{max}$ ($^\circ\text{C/min}$) across a centered sliding window.
- **FR-4.05 (Thermal Severity Grading):** Categorize overall thermal stress into `NORMAL` ($\text{TDI} < 1.5$), `ELEVATED` ($1.5 \le \text{TDI} < 2.5$), `SEVERE` ($2.5 \le \text{TDI} < 4.0$), or `CRITICAL` ($\text{TDI} \ge 4.0$).

### 3.2 Non-Functional Requirements
- **NFR-4.01 (Latency):** Complete thermal evaluation of a 10,000-sample session in **< 5 ms** on a single core.
- **NFR-4.02 (Numerical Stability):** Handle negative temperatures and sub-zero Celsius gracefully without numeric overflow or NaN.

---

## 4. Interfaces & Data Structures

```cpp
namespace ev::battery::thermal {

enum class ThermalSeverity : uint8_t {
    NORMAL,    ///< TDI < 1.5 (moderate climate / gentle driving)
    ELEVATED,  ///< 1.5 <= TDI < 2.5 (warm ambient / standard fleet duty)
    SEVERE,    ///< 2.5 <= TDI < 4.0 (Indian summer >40°C / high torque)
    CRITICAL   ///< TDI >= 4.0 (sustained >45°C thermal abuse)
};

struct ThermalConfig {
    double activation_energy_j_mol = 31400.0; ///< Arrhenius activation energy (31.4 kJ/mol for SEI)
    double reference_temp_c = 25.0;           ///< Reference baseline (298.15 K)
    double current_weight_gamma = 0.50;       ///< Electro-thermal coupling factor
    double nominal_capacity_ah = 30.0;        ///< 1C reference current
    double dt_window_s = 60.0;                ///< Smoothing window for dT/dt gradient estimation
};

struct ThermalReport {
    double mean_temperature_c = 0.0;
    double max_temperature_c = 0.0;
    double min_temperature_c = 0.0;
    double dwell_time_above_40c_s = 0.0;
    double dwell_time_above_45c_s = 0.0;
    double dwell_time_above_55c_s = 0.0;
    double dwell_fraction_above_40c = 0.0;    ///< Fraction of total session time >= 40°C
    double dwell_fraction_above_45c = 0.0;    ///< Fraction of total session time >= 45°C
    double thermal_degradation_index = 1.0;   ///< TDI (dimensionless multiplier)
    double max_heating_rate_c_per_min = 0.0;  ///< Max dT/dt (°C/min)
    ThermalSeverity severity = ThermalSeverity::NORMAL;
};

[[nodiscard]] common::Result<ThermalReport> analyzeThermalStress(
    const common::InternalTimeSeries& series,
    const ThermalConfig& config = {}
);

} // namespace ev::battery::thermal
```

---

## 5. Acceptance Criteria

1. **Arrhenius Exactness:** At $T = 25.0^\circ\text{C}$, $k(T) = 1.000 \pm 10^{-4}$. At $T = 45.0^\circ\text{C}$, $k(T) \approx 2.18 \pm 0.05$.
2. **Physics Simulation Grounding:** Simulated Indian summer drive cycle ($T_{amb} = 45^\circ\text{C}$, passive cooling, $1.5\text{C}$ pulses) must produce $\text{TDI} \ge 2.5$ and `SEVERE` or `CRITICAL` severity rating.
3. **Execution Performance:** Analyzes 10,000 samples in $< 2.0\text{ ms}$.
4. **All unit and integration tests pass.**
