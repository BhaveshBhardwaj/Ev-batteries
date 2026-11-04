# Iteration 11: Real-World 3D Digital Twin, Dynamic Vehicle Kinematics & Unified Telematics Dashboard
## System Requirements Specification (SRS)

> **Document ID:** SRS-ITERATION-011  
> **Version:** 1.0  
> **Created:** 2026-11-04  
> **Module ID:** `M15: sim_to_real_digital_twin` & `M16: telematics_dashboard`  
> **Target Architecture:** Indian Commercial 2W/3W EV Ecosystem (Ola, Ather, Treo, HyperDrive)  
> **Parent Architecture:** [RULES-001 (docs/rules.md)](../../rules.md) & [FLOW-001 (docs/flow.md)](../../flow.md)

---

## 1. Scope

### 1.1 In Scope
- **Real-World Environmental & Powertrain Dynamics Physics Engine:**
  - **Aerodynamic Drag:** $F_{\text{aero}} = \frac{1}{2} \rho(T_{\text{ambient}}) C_d A v^2$ with temperature-dependent dynamic air density $\rho = \frac{P}{R_{\text{spec}} T}$ and live power dissipation ($W$).
  - **Vehicle Curb & Payload Mass Scaling ($F=ma$):** Accurate curb weights across Indian 2W/3W models (Ola S1 Pro $125\text{kg}$, Ather 450X $111\text{kg}$, Mahindra Treo $350\text{kg}$, HyperDrive EV $1650\text{kg}$) coupled with interactive Rider Weight ($45-130\text{kg}$) and Cargo Payload ($0-150\text{kg}$) sliders.
  - **Dynamic Cornering & Steering Kinematics:** Procedural road turns, manual steering rack inputs (`A` / `D` keys), centripetal lateral acceleration $a_y = \frac{v^2}{R}$, tire scrub drag $F_{\text{cornering}}$, front-wheel Ackermann steering geometry, and chassis roll/lean.
  - **Road Grade / Incline & Flyovers:** Gravitational load $F_{\text{grade}} = M_{\text{total}} g \sin(\theta)$ modeling Indian flyover climbs (+4% to +8%) and downhill regenerative energy recovery (-5%).
  - **Auxiliary Electrical Load:** Continuous cabin AC and BMS cooling fan load ($50-1200\text{W}$) drawing discharge current even while stationary at traffic signals.
  - **Road Potholes & Damped Suspension Kinematics:** Procedural 3D potholes with damped harmonic spring-mass bounce ($M \ddot{z} + C \dot{z} + K z = F(t)$), vertical accelerometer G-shock readout, and micro-impulse terminal resistance jitter.
- **Unified Side-by-Side Telemetry & Analytical Inspector Console:**
  - High-density responsive 2-column Metric Tile Grid (`metric-tiles-grid`) eliminating all horizontal and vertical text clipping.
  - Console deck tab navigation: `[ 📊 All Summary ]`, `[ 🌪️ Real Dynamics ]`, `[ 🛡️ Gate 1 & 2 ]`, `[ ⚡ SOH Valuation ]`, `[ 🏛️ BPAN Passport ]`.
  - Seamless 10 Hz telemetry synchronization with the compiled C++23 native valuation core via REST API `/api/certify`.

### 1.2 Out of Scope
- Direct CAN bus hardware sniffing over physical OBD-II dongles (covered via simulated 1 Hz telemetry buffer stream).

---

## 2. Gate 0: Prerequisite & Requirement Verification Audit

| Category | Prerequisite Check | Status | Evidence / Reference |
| :--- | :--- | :---: | :--- |
| **Physical Aerodynamic Parameters** | Drag coefficient $C_d$, frontal area $A$, air density $\rho(T)$ | **SATISFIED** | Calibrated to 2W/3W chassis standards ($C_d = 0.38$, $A = 0.72\text{ m}^2$). |
| **Mass & Powertrain Scaling** | Curb weight baselines + payload bounds | **SATISFIED** | Configured in `sim3d.js` matching Ola S1, Ather 450X, Mahindra Treo specs. |
| **C++ Valuation Core** | Compiled CLI binary `ev_battery_cli.exe` | **SATISFIED** | `build/src/apps/cli/Release/ev_battery_cli.exe` verified passing 131 tests. |
| **ML Inference Engine** | Trained peak ratio regression model in C++ | **SATISFIED** | `trained_model.hpp` verified bit-exact with NASA PCoE reference cycling. |

---

## 3. Requirements

### 3.1 Functional Requirements

- **FR-11.01 (Multi-Factor Traction Modeling):** Compute instantaneous traction force balancing inertial, aerodynamic, rolling, cornering, and gravitational forces:
  $$F_{\text{traction}} = M \cdot a + \frac{1}{2}\rho C_d A v^2 + C_{rr} M g + F_{\text{cornering}} + M g \sin(\theta)$$
- **FR-11.02 (Electrochemical & Auxiliary Current Coupling):** Integrate powertrain mechanical power and auxiliary loads to derive pack current:
  $$I_{\text{pack}} = -\frac{\frac{P_{\text{mech}}}{\eta} + P_{\text{aux}}}{V_{\text{pack}}}$$
  Under negative load ($P_{\text{mech}} < -50\text{W}$), trigger regenerative braking charging current ($I_{\text{pack}} > 0$).
- **FR-11.03 (Real-Time Hampel Filtering):** Filter raw voltage with moving-median window ($k=3$) to strip pothole contact resistance impulses while maintaining true electrochemical open-circuit voltage baselines.
- **FR-11.04 (Tile-Based Responsive Console):** Display all live analytical results in structured tile cards with title, prominent colored numeric value, and descriptive subtitle without text overflow or truncation.
- **FR-11.05 (End-to-End BPAN Certification):** Stream rolling telemetry to `/api/certify` on demand or dock, producing genuine FIPS 180-2 SHA-256 sealed digital battery certificates.

---

## 4. Test & Verification Plan

1. **Unit & Mathematical Integrity:** Execute full suite of 131 C++ tests verifying calculus, filtering, ICA peak extraction, calibration, estimation, and SHA-256 certificates.
2. **Visual & UI Layout Verification:** Inspect via Chrome DevTools across viewport sizes (from 1024px to 1920px) ensuring no clipped text, no horizontal scrollbars, and zero script errors.
3. **Physics Sanity Checks:**
   - At $v = 0\text{ km/h}$, $F_{\text{aero}} = 0\text{ N}$, $I_{\text{pack}} = \frac{P_{\text{aux}}}{V} \approx 4.7\text{ A}$ discharge.
   - At $v = 50\text{ km/h}$, $F_{\text{aero}} \approx 30-33\text{ N}$ ($420-460\text{ W}$ dissipation).
   - At $4\%$ flyover grade, uphill discharge current spikes to $> 80\text{ A}$.

