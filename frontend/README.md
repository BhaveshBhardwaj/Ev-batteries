# Bharat Battery Passport Tier 3 — Interactive GUI & Valuator

A web-based graphical user interface for the **EV Battery Health Certification Engine and Digital Battery Passport Valuator** for Indian commercial 2W/3W fleets (LFP/NMC chemistries, passive air cooling, NITI Aayog BPAN Tier 3 compliance).

---

## 🌟 Key Features

1. **Light Theme & Executive Polish**:
   - Clean, high-contrast light theme engineered for readability and regulatory compliance audits.
   - Ashoka Pillar Government of India emblem, cryptographic immutability badges, and real-time safety gate status indicators.

2. **1-Click Indian Fleet Presets**:
   - **Ola 2W Delivery**: Nominal 16S 30Ah LFP, 120 EFC, 86.4% SoH, TDI 1.35x, Gate 1–3 PASS.
   - **Zypp 45°C Extreme Summer**: High thermal stress, TDI >2.8x, dwell >45°C.
   - **BluSmart 3W Fleet**: 450 EFC aged pack, 81.5% SoH, commercial transit duty cycle.
   - **Partial Opportunity Charge**: 30%–55% SoC short charge window; activates **M10 Gaussian Process Regression (GPR)** curve reconstruction with $\pm 1.96\sigma$ epistemic confidence bands.
   - **Shunt Sensor Fault**: Current zero-bias drift of 2.2A exceeding the 1.5A hardware threshold $\to$ **Gate 2 Sensor Fault REJECT**.
   - **Severe Packet Loss**: >25% packet dropouts and sensor jitter $\to$ **Gate 1 Data Quality REJECT** ($\text{TQS} < 30.0$).

3. **Interactive Sim-to-Real Tweakers**:
   - Adjust ambient temperature (20°C – 55°C), current sensor zero-bias (-2.5A to +2.5A), packet loss probability (0% – 40%), and voltage noise (0.00V – 0.25V) to immediately witness how safety gates respond.

4. **Deep-Dive Engineering Tabs**:
   - **BPAN Tier 3 Passport**: Official certificate layout, nameplate vs certified usable capacity, decoupled LLI/LAM degradation modes, and RUL to 70% fleet EoL.
   - **Electrochemical ICA / DVA (M05 & M06)**: High-resolution $dQ/dV$ and $dV/dQ$ curves showing primary peak tracking ($V_{p1}$), peak height ($h_{p1}$), and peak integration area ($A_{p1}$).
   - **Partial Charge GPR (M10)**: Non-parametric curve reconstruction across unobserved voltage wings with shaded 95% epistemic confidence intervals.
   - **Telemetry Quality Scoring (M12)**: Gate 1 4-dimensional breakdown (sampling regularity jitter, packet transmission completeness, physical plausibility, high-frequency SNR).
   - **Cleaning & Calibration (M02 & M07)**: Hampel filter impulse spike removal, monotonic gap repair, and Gate 2 current-sensor zero-bias self-calibration.
   - **Thermal Degradation (M14)**: Arrhenius Degradation Index ($TDI$) stress trajectory, dwell time above 40°C and 45°C, and heating rate $dT/dt$.

5. **Cryptographic Immutability & Interactive Tamper Test**:
   - Pure client-side **FIPS 180-2 SHA-256** hash generation matching the C++23 production core.
   - Interactive tampering simulator: modify certified SoH by even $0.01\%$ to witness the **cryptographic avalanche effect** (displaying exact hex difference and invalidating the certificate).

6. **Export & Printing**:
   - 1-click download of official **BPAN Tier 3 JSON** schema matching the C++ `ev_battery_cli` output.
   - Export structured **Inspection Report** in `.txt` format.
   - Browser print stylesheet for printing clean regulatory paper certificates.

---

## 🚀 How to Run the GUI

### Option 1: Direct File Open
Simply double-click `frontend/index.html` in Windows Explorer or open it in any modern browser (Chrome, Edge, Firefox, Brave):
```powershell
Start-Process "frontend\index.html"
```

### Option 2: Local Web Server (Recommended for Chart.js & Assets)
Run a local Python HTTP server from the project root:
```powershell
python -m http.server 8080
```
Then navigate to:
```
http://localhost:8080/frontend/
```
