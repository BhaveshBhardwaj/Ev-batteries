# Iteration 1 — Architecture & Technical Decisions

> **Document ID:** DEC-IT1  
> **Version:** 1.0  
> **Created:** 2026-10-03  
> **Status:** Approved & Active  
> **Iteration:** Iteration 1 (Core Signal Processing: M02, M03, M04)

---

## 1. Context & Motivation

Iteration 1 establishes the core signal processing pipeline that transforms raw, noisy, hardware-corrupted battery telemetry from low-cost Indian 2W/3W BMS units into clean, segmented, high-SNR analytical streams ready for SOH estimation.

---

## 2. Key Decisions

### DEC-1.01: Sim-to-Real Hybrid Grounding Strategy (Gate 0 Compliance)
- **Problem:** Real Indian 2W/3W commercial fleet telemetry contains proprietary noise patterns, but official benchmark data (NASA PCoE, Oxford, BatteryArchive) lacks the specific hardware quirks of Indian low-cost BMS (0.65A zero-drift, 42°C ambient, sporadic packet drops, ±0.6V impulse spikes).
- **Decision:** Adopt a **Sim-to-Real Hybrid Grounding Strategy**:
  1. *Real Data Anchor:* Download and parse official NASA Ames PCoE Li-ion cycling data (B0005) and laboratory LFP equilibrium OCV data into `data/reference/`.
  2. *Physics Simulation:* Implement `TelemetryGenerator` (`tests/synthetic/telemetry_generator.hpp/cpp`) that models 1st-order Equivalent Circuit Model (ECM) dynamics ($V_{RC}$ polarization, Joule heating, shunt offset/gain drift, packet drops, and impulse spikes).
- **Consequence:** All algorithms have 100% verifiable ground-truth for quantitative unit tests (SNR $\ge 15\text{ dB}$, 100% spike rejection) while matching real lab battery curves.

---

### DEC-1.02: Module M02 (Cleaning) Architecture
- **Problem:** Raw BMS streams exhibit two primary data corruptions:
  1. Isolated impulse noise / spikes from motor inverter EMI or ADC bitflips.
  2. Missing packets from cellular/GSM dropouts causing timestamp gaps.
- **Decision:**
  - **Hampel Filter:** Use a sliding window median filter with $3\times 1.4826 \times \text{MAD}$ threshold. Spikes exceeding this threshold are replaced with the local median, preventing distortion of adjacent data.
  - **Timestamp Gap Repair:** Gaps between $\Delta t_{nominal}$ and $10\text{ s}$ are repaired using monotonic linear interpolation. Gaps $> 10\text{ s}$ are flagged as session breaks (discontinuous).
  - **Stuck Sensor Detection:** Flag channels where value variance over a sliding $30\text{ s}$ window is zero (flatlining sensor).

---

### DEC-1.03: Module M03 (Segmentation) Architecture
- **Problem:** Drive cycles fluctuate wildly between acceleration (-30A), regenerative braking (+10A), idle (0A), and depot charging (+15A). Simple zero-crossing triggers chatter at the boundaries.
- **Decision:**
  - Implement a finite state machine (FSM) with dual-threshold hysteresis:
    - `CHARGE`: $I > +0.5\text{ A}$ for at least $T_{min} = 10\text{ s}$.
    - `DISCHARGE`: $I < -0.5\text{ A}$ for at least $T_{min} = 10\text{ s}$.
    - `REST`: $|I| \le 0.1\text{ A}$ for at least $T_{rest\_min} = 60\text{ s}$.
  - Provide `extractConstantCurrentWindow` which locates contiguous sub-segments where current standard deviation $\sigma_I < 0.5\text{ A}$ for $\ge 300\text{ s}$, which is strictly required for differential voltage ($dQ/dV$) analysis.

---

### DEC-1.04: Module M04 (Denoising) Architecture
- **Problem:** Terminal voltage noise obscures subtle electrochemical phase transitions in LFP cells (where plateau slope is very low, $< 1\text{ mV}/\% \text{SoC}$). Filtering must not distort phase peak positions.
- **Decision:**
  - Use a two-stage cascaded smoothing pipeline:
    1. *Stage 1 (Time Domain):* Adaptive 1D Kalman Filter with dynamic measurement variance $R_k = R_0 \cdot (1 + \gamma |I_k|)$.
    2. *Stage 2 (Polynomial Smooth):* Savitzky-Golay smoothing (order 2 or 3, window 15–31 points) to preserve true peaks and zero-phase distortion.

---

### DEC-1.05: Semantic Modularity Enforcement
- **Decision:** Strict adherence to semantic modularity rules:
  - `include/ev/battery/cleaning/cleaning.hpp` + `src/modules/cleaning/cleaning.cpp`
  - `include/ev/battery/segmentation/segmentation.hpp` + `src/modules/segmentation/segmentation.cpp`
  - `include/ev/battery/denoising/denoising.hpp` + `src/modules/denoising/denoising.cpp`
  - Each with a clean, focused public interface, `std::span` arguments, and zero unnecessary heap allocations in inner loops.
