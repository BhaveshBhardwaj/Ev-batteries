# Iteration 1 — SRS: Core Signal Processing Pipeline

> **Document ID:** SRS-IT1  
> **Version:** 1.0  
> **Created:** 2026-10-03  
> **Parent:** SRS-000 (Initial SRS), rules.md (§2.4 Gate 0 Data Grounding)  
> **Status:** Active  
> **Modules Delivered:** M02 (cleaning), M03 (segmentation), M04 (denoising)  
> **Test Grounding:** Physics-Based Sim-to-Real Indian 2W/3W Battery Telemetry Generator (`tests/synthetic/telemetry_generator.hpp`)

---

## 1. Scope & Objectives

### 1.1 In Scope
- **Gate 0 Grounding:** Physics-based battery telemetry synthesizer simulating real Indian commercial operational profiles (42°C ambient, LFP 16S pack, 0.65A shunt zero-drift, 1.5% gain scaling, packet dropouts, and impulse noise).
- **M02 (Data Cleaning & Integrity Restoration):**
  - Outlier detection and spike removal via sliding-window Hampel filter ($3\sigma$ thresholding using Median Absolute Deviation).
  - Packet dropout recovery via monotonic linear timestamp gap interpolation (`repairTimestampGaps`).
  - Sensor stuck/flatline detection (`detectStuckSensor`).
- **M03 (Session & Regime Segmentation):**
  - Operating regime classification (`segmentRegimes`): REST, CHARGE, DISCHARGE, IDLE using current hysteresis and minimum dwell-time windowing.
  - Constant Current (CC) charging window extractor (`extractConstantCurrentWindow`): locates stable $I_{cc} \pm \Delta I$ charging intervals suitable for Incremental Capacity Analysis (ICA).
- **M04 (Signal Denoising & Smoothing):**
  - Cascaded smoothing: Adaptive 1D Kalman Filter (dynamic measurement variance $R$ based on current state) + Savitzky-Golay polynomial smoothing.
  - Signal-to-Noise Ratio (SNR) enhancement $\ge 15\text{ dB}$ on synthetic and real charging curves without shifting peak locations.
- **Full Semantic Modularity:** Single cohesive domain module per folder (`cleaning.hpp/cpp`, `segmentation.hpp/cpp`, `denoising.hpp/cpp`).

### 1.2 Out of Scope
- M05 (Feature Extraction / ICA-DVA peak fitting) — scheduled for Iteration 2.
- M06 (Capacity Estimation) and M07 (Resistance Estimation) — scheduled for Iteration 3.
- M08 (Sensor Self-Calibration) — scheduled for Iteration 4.

---

## 2. Functional Requirements

### 2.1 M02: Cleaning & Integrity Restoration (`ev::battery::cleaning`)

| ID | Requirement | Acceptance Criteria |
|----|------------|---------------------|
| FR-1.01 | The system SHALL detect and replace transient impulse spikes using a sliding Hampel Filter. | 100% of injected synthetic spikes ($> 0.5\text{ V}$) detected and replaced by local median; zero false positives on smooth transients. |
| FR-1.02 | The system SHALL detect timestamp gaps exceeding nominal sampling interval by $> 1.5\times$ and perform linear interpolation for gaps $< 10\text{ s}$. | Monotonic $1\text{ Hz}$ time grid restored; gap flag recorded in sample quality metadata. |
| FR-1.03 | The system SHALL flag sensors that report constant identical floating-point values for longer than threshold duration ($> 30\text{ s}$). | Returns `DataQualityAlert::STUCK_SENSOR` with affected channel and sample index range. |

### 2.2 M03: Regime Segmentation (`ev::battery::segmentation`)

| ID | Requirement | Acceptance Criteria |
|----|------------|---------------------|
| FR-1.04 | The system SHALL partition continuous telemetry into contiguous segments: `REST`, `CHARGE`, `DISCHARGE`. | Uses dual-threshold hysteresis ($I_{thresh} = \pm 0.5\text{ A}$) and debounce window ($T_{min} = 10\text{ s}$) to prevent chattering. |
| FR-1.05 | The system SHALL identify and extract the longest Constant-Current (CC) charge segment where current variance $\sigma_I \le 0.5\text{ A}$ and duration $\ge 300\text{ s}$. | CC segment start/end timestamps and mean current returned with $< 1\%$ error compared to ground truth. |

### 2.3 M04: Denoising (`ev::battery::denoising`)

| ID | Requirement | Acceptance Criteria |
|----|------------|---------------------|
| FR-1.06 | The system SHALL filter raw terminal voltage using an Adaptive Kalman Filter combined with Savitzky-Golay smoothing. | Achieves $\ge 15\text{ dB}$ SNR improvement; peak voltage error $\le 5\text{ mV}$ compared to uncorrupted ground truth. |
| FR-1.07 | The filter SHALL preserve inflection points and peak locations of the underlying OCV/terminal curve ($|\Delta t_{peak}| \le 2\text{ s}$). | Derivative $dV/dt$ or $dQ/dV$ peak alignment verified against pristine analytical curve. |

---

## 3. Non-Functional Requirements & Performance Budgets

| ID | Metric | Budget Target |
|----|--------|---------------|
| NFR-1.01 | Cleaning Throughput | $\ge 250,000\text{ samples/sec}$ |
| NFR-1.02 | Denoising Throughput | $\ge 100,000\text{ samples/sec}$ |
| NFR-1.03 | Memory Overhead | Zero heap allocations inside inner filter loops; strictly `std::span` and preallocated buffers. |
| NFR-1.04 | Test Coverage | Unit test coverage $\ge 90\%$ for all public functions in M02, M03, M04. |
