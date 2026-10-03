# EV Battery Telemetry Analyzer — Initial Software Requirements Specification

> **Document ID:** SRS-000  
> **Version:** 1.0  
> **Created:** 2026-10-03  
> **Author:** Development Team  
> **Status:** Baseline  
> **Classification:** Internal

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [System Overview](#2-system-overview)
3. [Stakeholders and Users](#3-stakeholders-and-users)
4. [System Architecture](#4-system-architecture)
5. [Module Specifications](#5-module-specifications)
6. [Functional Requirements](#6-functional-requirements)
7. [Non-Functional Requirements](#7-non-functional-requirements)
8. [Data Requirements](#8-data-requirements)
9. [Interface Specifications](#9-interface-specifications)
10. [Design Decisions and Alternatives](#10-design-decisions-and-alternatives)
11. [Risk Register](#11-risk-register)
12. [Iteration Roadmap](#12-iteration-roadmap)
13. [Glossary](#13-glossary)
14. [Appendices](#14-appendices)

---

## 1. Introduction

### 1.1 Purpose

This Software Requirements Specification (SRS) defines the complete requirements for the **EV Battery Telemetry Analyzer** — a high-performance, software-only engine that estimates the State-of-Health (SoH), Remaining Useful Life (RUL), and internal resistance of lithium-ion battery packs used in Indian 2W/3W electric vehicle fleets. The system ingests raw BMS telemetry (voltage, current, temperature, timestamps), processes it through a signal-processing and machine-learning pipeline, and outputs auditable health certificates.

This document serves as the **master SRS**. Each development iteration will produce a derivative SRS that references and refines the requirements herein.

### 1.2 Scope

**In scope:**
- Ingestion of BMS/telematics telemetry data via REST API and file upload (JSON/CSV).
- OEM schema normalization for diverse Indian 2W/3W BMS formats.
- Signal cleaning, segmentation, and denoising.
- Incremental Capacity Analysis (ICA) and Differential Voltage Analysis (DVA).
- Feature extraction from dQ/dV and dV/dQ curves.
- Current-sensor self-calibration for low-cost BMS units.
- SoH, RUL, and DCIR estimation via ML models (inference in C++).
- Calibrated uncertainty quantification with abstention.
- Physics-based synthetic training data generation.
- Fleet-level few-shot model adaptation.
- Telemetry Quality Score.
- Thermal stress modeling (Arrhenius-based).
- Output: JSON/PDF health certificates, BPAN Tier 3 payload.

**Out of scope (for this release cycle):**
- Real-time embedded BMS firmware.
- Hardware diagnostic devices (OBD dongles, CAN loggers for production use).
- Mobile applications.
- Financial valuation models (residual value in INR) — deferred to a future product layer.
- Direct BPAN registry submission (designed-to-spec, but registry API is not yet published).

### 1.3 Definitions and Conventions

- **SHALL** = mandatory requirement.
- **SHOULD** = strongly recommended, may be deferred with justification.
- **MAY** = optional enhancement.
- Requirements are tagged: `FR-XX.YY` (functional) and `NFR-XX.YY` (non-functional).
- Module IDs: `M01`–`M15` as defined in `rules.md`.

### 1.4 References

| Document | Description |
|----------|-------------|
| `docs/rules.md` | Development rules, coding standards, iteration workflow |
| NASA Battery Dataset | Public aging dataset (18650 cells) |
| CALCE Battery Dataset | University of Maryland cycling data |
| Oxford Battery Dataset | Degradation data with diverse protocols |
| MIT-Stanford-Toyota Dataset | Fast-charging degradation data |
| MoRTH BPAN Guidelines | Battery Pack Aadhaar Number framework (2026) |

---

## 2. System Overview

### 2.1 System Description

The EV Battery Telemetry Analyzer is a **C++-native, high-performance processing engine** that transforms raw battery telemetry into actionable health diagnostics. It is designed as a library-first architecture: the core engine is a static/shared C++ library that can be embedded in CLI tools, REST API servers, or linked from other services.

### 2.2 High-Level Data Flow

```
Raw Telemetry (JSON/CSV/Stream)
        │
        ▼
┌─────────────────┐
│   M01: Ingestion │ ──→ Schema normalization, OEM adapters
└────────┬────────┘
         │ InternalTimeSeries
         ▼
┌─────────────────┐
│   M12: Quality   │ ──→ Telemetry Quality Score (accept/warn/reject)
│        Score     │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│   M02: Cleaning  │ ──→ Outlier removal, gap handling, fault correction
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│   M03: Segment-  │ ──→ Charge / Discharge / Rest segments
│       ation      │     CC-phase windows identified
└────────┬────────┘
         │ ChargeSegment[]
         ▼
┌─────────────────┐
│   M07: Calibra-  │ ──→ Current-sensor offset/gain correction
│       tion       │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│   M04: Denoising │ ──→ Kalman filter + Savitzky-Golay smoothing
└────────┬────────┘
         │ SmoothedSegment[]
         ▼
┌─────────────────┐
│   M05: ICA/DVA   │ ──→ dQ/dV and dV/dQ computation (+ GPR option)
└────────┬────────┘
         │ ICACurve[]
         ▼
┌─────────────────┐
│   M06: Features  │ ──→ Peak height, position, area, width, shifts
└────────┬────────┘
         │ FeatureVector
         ▼
┌─────────────────┐
│   M08: Estimation│ ──→ SoH%, RUL, DCIR (ONNX Runtime inference)
│   M09: Uncertain-│ ──→ Confidence intervals, abstention flag
│        ty        │
│   M14: Thermal   │ ──→ Thermal stress score, Arrhenius aging
└────────┬────────┘
         │ HealthReport
         ▼
┌─────────────────┐
│   M13: Output    │ ──→ JSON certificate, PDF report, BPAN payload
└─────────────────┘
```

### 2.3 Deployment Model

| Deployment | Description |
|------------|-------------|
| **Library** | `libev_battery_core.a` / `.so` — embed in any C++ application |
| **CLI** | `ev_battery_cli` — batch process telemetry files |
| **REST API** | `ev_battery_api` — HTTP server for B2B SaaS integration |
| **Python bindings** | pybind11 wrapper for research and training workflows |

---

## 3. Stakeholders and Users

| Stakeholder | Role | Primary Concern |
|-------------|------|-----------------|
| **Fleet Operators** (Zypp, Yulu, Bounce) | Upload telemetry, receive fleet health dashboards | Accuracy, uptime, ease of integration |
| **NBFCs / Banks** | Consume SoH scores for loan underwriting | Calibrated uncertainty, auditability |
| **Insurance Providers** | Use risk scores for premium calculation | Reliability of thermal risk scores |
| **Used-EV Platforms** (Cars24, Spinny) | Generate health certificates for listings | Speed (< 2 min), trust |
| **OEMs / Battery Manufacturers** | BPAN compliance reporting | Schema compatibility, regulatory correctness |
| **Battery Recyclers** | Grade retired packs for second-life vs. recycling | Accuracy at end-of-life SoH ranges |
| **Internal ML Team** | Train and improve models | Data pipeline quality, reproducibility |

---

## 4. System Architecture

### 4.1 Architectural Style

**Modular monolith with library-first design.**

The system is composed of 15 independent modules compiled into a single core library. Modules are loosely coupled via abstract interfaces and can be tested, benchmarked, and potentially deployed independently.

**Rationale:** A microservice architecture is premature for a team of 1-3 engineers. A modular monolith provides the same logical separation with far less operational overhead. If scaling demands it later, modules can be extracted into services along their existing interface boundaries.

### 4.2 Technology Stack

| Layer | Technology | Justification |
|-------|-----------|---------------|
| Core computation | C++20 | Performance: SIMD, zero-copy, cache-friendly data layouts |
| Linear algebra | Eigen 3 | Industry standard, header-only, SIMD-optimized |
| ML inference | ONNX Runtime (C++ API) | Run PyTorch/sklearn models without Python dependency |
| JSON parsing | nlohmann/json | De facto standard, header-only, MIT license |
| HTTP server | cpp-httplib or Drogon | Lightweight, single-header (httplib) or high-perf (Drogon) |
| Logging | spdlog + fmt | Fast, structured logging |
| Build system | CMake 3.25+ | Cross-platform, industry standard |
| Package manager | vcpkg | Microsoft-backed, excellent Windows + Linux support |
| Testing | Google Test + Google Benchmark | Industry standard |
| ML training | Python (PyTorch, scikit-learn) | Ecosystem maturity for model development |
| Model export | ONNX | Bridge from Python training to C++ inference |

### 4.3 Directory Structure

```
ev-battery/
├── CMakeLists.txt                 # Top-level build
├── vcpkg.json                     # Dependency manifest
├── docs/
│   ├── rules.md
│   ├── SRS_initial.md             ← THIS FILE
│   ├── architecture/
│   └── iterations/
├── include/
│   ├── ev/battery/                # Public headers (namespace ev::battery)
│   │   ├── common/                # Shared types, error codes
│   │   ├── math_utils/            # M15
│   │   ├── ingestion/             # M01
│   │   ├── cleaning/              # M02
│   │   ├── segmentation/          # M03
│   │   ├── denoising/             # M04
│   │   ├── ica_dva/               # M05
│   │   ├── features/              # M06
│   │   ├── calibration/           # M07
│   │   ├── estimation/            # M08
│   │   ├── uncertainty/           # M09
│   │   ├── synthetic_data/        # M10
│   │   ├── fleet_adapt/           # M11
│   │   ├── quality_score/         # M12
│   │   ├── output/                # M13
│   │   └── thermal/               # M14
├── src/
│   └── modules/                   # Implementation files mirroring include/
│       ├── math_utils/
│       ├── ingestion/
│       └── ...
├── tests/
│   ├── data/                      # Test datasets with provenance
│   ├── math_utils/
│   ├── cleaning/
│   └── ...
├── benchmarks/
│   ├── math_utils_bench.cpp
│   └── ...
├── tools/
│   ├── cli/                       # CLI application
│   └── api/                       # REST API server
├── python/
│   ├── training/                  # ML model training scripts
│   ├── notebooks/                 # Research and demo notebooks
│   └── bindings/                  # pybind11 wrappers
└── scripts/
    ├── download_datasets.py       # Dataset download + hash verification
    └── export_model.py            # PyTorch/sklearn → ONNX export
```

---

## 5. Module Specifications

### 5.1 M15: Math Utilities (`math_utils`)

**Purpose:** Foundational numerical routines shared by all modules.

**Capabilities:**
- Numerical differentiation (finite differences, Savitzky-Golay coefficients)
- Interpolation (linear, cubic spline, monotone piecewise)
- Basic statistics (mean, std, median, MAD, percentiles)
- Moving window operations (moving average, moving median)
- Convolution (1D, with SIMD optimization)
- Eigenvalue/eigenvector wrappers (Eigen thin wrapper)

**Key Design Constraints:**
- Header-heavy for inlining critical functions.
- All functions operate on `std::span<const double>` inputs (zero-copy).
- No heap allocation in any function (caller provides output buffers).

---

### 5.2 M02: Cleaning (`cleaning`)

**Purpose:** Remove invalid data points and correct obvious sensor faults.

**Capabilities:**
- Timestamp monotonicity enforcement (drop or reorder out-of-sequence samples).
- Gap detection: identify intervals > configurable threshold (default: 5 s) and split.
- Outlier removal: statistical (z-score, IQR-based) and physical-limit (voltage outside [0, 100V], current outside [-500A, 500A], temperature outside [-40°C, 100°C]).
- Stuck-sensor detection: constant-value runs beyond a threshold duration.
- NaN/Inf handling.

**Input:** `InternalTimeSeries` (raw, normalized).  
**Output:** `CleanedTimeSeries` (same structure, with a cleaning report).

---

### 5.3 M03: Segmentation (`segmentation`)

**Purpose:** Split a cleaned time series into charge, discharge, and rest segments. Identify usable constant-current (CC) charge windows.

**Capabilities:**
- Classify each sample as CHARGE, DISCHARGE, or REST based on current sign and magnitude (configurable dead-band, e.g., |I| < 0.05A → REST).
- Merge short transient segments (< configurable minimum duration).
- Detect CC phases: segments where current is stable within a tolerance band (e.g., ±5% of median) for a minimum duration.
- Compute voltage span of each CC window.
- Filter: only CC windows with sufficient voltage span (configurable, default > 0.3V) are marked as "usable for ICA."

**Input:** `CleanedTimeSeries`.  
**Output:** `SegmentedLog` containing a vector of `Segment` structs with type, start/end indices, duration, current statistics, and voltage range.

---

### 5.4 M04: Denoising (`denoising`)

**Purpose:** Smooth voltage and current signals to enable reliable differentiation.

**Capabilities:**

#### 5.4.1 Savitzky-Golay Filter
- Configurable window length (odd integer) and polynomial order.
- Computes smoothed values and optionally the first derivative simultaneously.
- SIMD-optimized convolution kernel.

#### 5.4.2 Kalman Filter (1D and Multi-State)
- **1D Kalman smoother** for voltage and current channels independently.
- **Extended Kalman Filter (EKF)** option for joint V-I-T state estimation.
- Configurable process noise (Q) and measurement noise (R) covariance.
- Forward-backward (RTS) smoother for offline processing.

#### 5.4.3 Moving Average
- Simple, weighted, and exponential moving average.
- Configurable window size.

**Input:** `Segment` (charge segment with raw V, I, T arrays).  
**Output:** `SmoothedSegment` (same structure with smoothed arrays + smoothing metadata).

---

### 5.5 M05: ICA/DVA Engine (`ica_dva`)

**Purpose:** Compute dQ/dV (Incremental Capacity Analysis) and dV/dQ (Differential Voltage Analysis) curves from smoothed charge segments.

**Capabilities:**

#### 5.5.1 Direct Differentiation
- Compute cumulative charge Q by integrating current over time (trapezoidal rule).
- Compute dQ/dV via finite differences on the Q(V) curve.
- Apply additional smoothing to the derivative if needed.

#### 5.5.2 GPR-Based Differentiation
- Fit a Gaussian Process Regression model to Q(V).
- Compute the analytical derivative of the GP mean function.
- Extract uncertainty bands (±2σ) from the GP posterior.
- Kernel: squared exponential (RBF) with automatic hyperparameter optimization (marginal likelihood maximization).

#### 5.5.3 DVA
- Compute dV/dQ via finite differences on the V(Q) curve.
- Useful for LFP cells where dQ/dV peaks are subtle.

**Input:** `SmoothedSegment`.  
**Output:** `ICACurve` containing voltage axis, dQ/dV values, uncertainty bands (if GPR), and metadata.

**Key Design Decision: GPR in C++**

| Alternative | Pros | Cons |
|-------------|------|------|
| **A: Implement GPR in C++ with Eigen** | Full control, no Python dependency, fast inference, SIMD-friendly | Medium implementation complexity; kernel hyperparameter optimization requires gradient-based optimization |
| **B: Call Python GPR (scikit-learn) via subprocess** | Easy to implement, well-tested | Process spawn overhead (100+ ms), breaks C++-only deployment, hard to benchmark |
| **C: Use a lightweight C++ GP library (libgp, GPy C++ port)** | Less implementation work | Fewer maintained options, may not have RTS smoother integration |

**Decision:** Option A. GPR with Eigen. The RBF kernel and Cholesky-based inference are straightforward in Eigen. L-BFGS-B for hyperparameter optimization can use a header-only library (e.g., LBFGSpp). This keeps the entire pipeline C++-native.

---

### 5.6 M06: Feature Extraction (`features`)

**Purpose:** Extract quantitative features from ICA/DVA curves for downstream ML models.

**Features extracted per curve:**

| Feature | Description | Degradation Mode Indicated |
|---------|-------------|---------------------------|
| `peak_count` | Number of detected peaks | Structural change |
| `peak_voltage[i]` | Voltage position of peak i | LLI (shift), LAM (disappearance) |
| `peak_height[i]` | Height (dQ/dV value) of peak i | LAM, capacity fade |
| `peak_area[i]` | Integrated area under peak i | LAM |
| `peak_fwhm[i]` | Full width at half maximum of peak i | Impedance growth |
| `peak_shift[i]` | Voltage shift from fresh-cell reference | LLI |
| `total_area` | Total area under dQ/dV curve | Total capacity |
| `curve_rmse_vs_ref` | RMSE of curve vs. fresh-cell reference | Overall degradation |
| `voltage_span` | Voltage range of the input segment | Window quality |
| `mean_temperature` | Mean temperature during segment | Context feature |
| `mean_c_rate` | Mean C-rate during segment | Context feature |
| `segment_duration` | Duration of the segment | Context feature |

**Peak Detection Algorithm:**
- Find local maxima in dQ/dV using first-derivative sign changes.
- Filter by minimum prominence (configurable, chemistry-dependent).
- Refine peak position via parabolic interpolation.

**Reference Curve Management:**
- Store fresh-cell reference dQ/dV curves per chemistry (LFP, NMC).
- Features include shifts relative to the reference.
- References are loaded from configuration files, not hardcoded.

---

### 5.7 M07: Current-Sensor Self-Calibration (`calibration`)

**Purpose:** Estimate and correct current-sensor offset and gain errors from the data itself, without requiring a reference sensor.

**This is the primary proprietary technology module.**

**Problem Statement:**
Low-cost BMS current sensors (shunt resistors, Hall-effect) exhibit:
- **Offset error (bias):** A constant additive error (e.g., +0.15A) that causes Coulomb counting drift.
- **Gain error (scale factor):** A multiplicative error (e.g., 1.03×) that causes proportional capacity error.
- **Drift:** Both offset and gain change slowly over time due to temperature and aging.

These errors corrupt the charge integral Q = ∫I dt, which is fundamental to ICA.

**Calibration Method (Multi-Signal Consistency):**

1. **Rest-voltage anchoring:** During rest segments (I ≈ 0), the measured current should be zero. The residual is the offset estimate. Multiple rest segments give a robust offset estimate (median).

2. **Overlapping charge windows:** If two charge sessions cover the same voltage range [V₁, V₂], the computed ΔQ should be identical (for the same SoH). Discrepancies reveal gain error.

3. **Cross-session consistency:** Track the estimated offset and gain across multiple sessions to detect drift and apply time-varying corrections.

4. **Physics constraint:** During CC charging, dV/dt should be smooth. Sudden jumps in the corrected current signal indicate residual errors.

**Input:** Multiple `Segment`s from multiple sessions.  
**Output:** `CalibrationResult` containing offset, gain, confidence, and a corrected current array.

---

### 5.8 M08: Estimation (`estimation`)

**Purpose:** Predict SoH, RUL, and DCIR from the feature vector.

**Model Architecture:**

| Model | Input | Output | Training |
|-------|-------|--------|----------|
| **SoH Estimator** | Feature vector + context | SoH % (0–100) | Gradient-boosted trees (XGBoost/LightGBM), exported to ONNX |
| **DCIR Estimator** | Current-step voltage response features | Internal resistance (mΩ) | Same |
| **RUL Estimator** | Time series of SoH estimates + features | Remaining cycles / months | Sequence model (LSTM or Transformer), exported to ONNX |

**Per-Chemistry Models:**
- Separate model weights for LFP and NMC.
- Chemistry is either specified by the client or auto-detected from the voltage curve shape.

**Inference Runtime:**
- ONNX Runtime C++ API.
- Models loaded at startup, kept in memory.
- Batch inference for fleet processing.

---

### 5.9 M09: Uncertainty Quantification (`uncertainty`)

**Purpose:** Provide calibrated confidence intervals and an explicit "insufficient data" output.

**Method: Split Conformal Prediction.**

1. Hold out a calibration set (not used for training).
2. Compute nonconformity scores: |predicted SoH − true SoH| on the calibration set.
3. For a new prediction, construct a prediction interval using the empirical quantile of the nonconformity scores at the desired coverage level (e.g., 90%).
4. **Abstention rule:** If the prediction interval width exceeds a threshold (e.g., > 15%), or if the input is flagged by the quality score as low-confidence, return `INSUFFICIENT_DATA` instead of a point estimate.

**Output:** `UncertaintyResult` containing point estimate, lower bound, upper bound, coverage level, interval width, and abstention flag.

---

### 5.10 M10: Synthetic Data Generation (`synthetic_data`)

**Purpose:** Generate large, labeled training datasets from physics-based models when real field data is scarce.

**Approach:**

1. **Half-cell models:** Parameterize anode and cathode open-circuit voltage (OCV) curves for common cell types (NMC111, NMC622, NMC811, LFP).
2. **Degradation modes:** Simulate Loss of Lithium Inventory (LLI), Loss of Active Material (LAM-PE, LAM-NE), and resistance growth as scalar parameters.
3. **Full-cell synthesis:** Combine half-cell curves with degradation parameters to generate synthetic full-cell OCV and dQ/dV curves.
4. **Noise injection:** Add realistic sensor noise, quantization, sampling rate variation, and partial-window effects.
5. **Temperature variation:** Shift OCV curves by temperature (Nernst equation).

**Output:** Thousands of synthetic `(feature_vector, true_SoH)` pairs for training.

**Language:** The simulation kernel is C++. The training loop that consumes the data is Python (PyTorch).

---

### 5.11 M11: Fleet Adaptation (`fleet_adapt`)

**Purpose:** Adapt a pre-trained base model to a new fleet using only 3–5 reference capacity tests.

**Method:**
1. **Base model:** Trained on combined public + synthetic data.
2. **Calibration data:** 3–5 packs from the new fleet with known ground-truth SoH (from reference capacity tests).
3. **Adaptation:** Fine-tune the last layer(s) of the model, or apply a linear correction (bias + scale) to the base model's predictions.
4. **Validation:** Leave-one-out cross-validation on the calibration set.

---

### 5.12 M12: Telemetry Quality Score (`quality_score`)

**Purpose:** Assess whether a telemetry log is reliable enough for health estimation.

**Score Components:**

| Factor | Weight | Description |
|--------|--------|-------------|
| Sampling rate adequacy | 20% | Is the effective sampling rate ≥ 1 Hz? |
| Voltage resolution | 20% | Is the voltage quantization ≤ 5 mV? |
| Current sensor noise | 15% | Estimated noise level from rest segments |
| Timestamp quality | 15% | Monotonicity, gap fraction, jitter |
| Charge window quality | 15% | Voltage span, CC duration, stability |
| Completeness | 15% | Fraction of expected channels present |

**Output:** Score 0–100, plus categorical label: `EXCELLENT` (≥80), `ACCEPTABLE` (50–79), `MARGINAL` (30–49), `REJECT` (<30).

---

### 5.13 M13: Output (`output`)

**Purpose:** Format health estimates into deliverable outputs.

**Formats:**
1. **JSON certificate:** Machine-readable, includes all estimates, confidence intervals, model version, input hash, quality score, and timestamp.
2. **PDF report:** Human-readable summary with charts (dQ/dV curve, SoH trend if historical data available).
3. **BPAN payload:** Formatted per MoRTH Tier 3 BDD specifications (SoH, cycle count, thermal events, status).

---

### 5.14 M14: Thermal Model (`thermal`)

**Purpose:** Model temperature-dependent degradation for predictive analytics.

**Capabilities:**
- **Arrhenius aging model:** Calendar aging rate = A · exp(-Ea / (R · T)), calibrated to field data.
- **Thermal stress score:** Cumulative high-temperature exposure metric.
- **Predictive queries:** "How much additional capacity loss over the next N months at temperature profile T(t)?"

---

### 5.15 M01: Ingestion (`ingestion`)

**Purpose:** Accept raw telemetry data from external sources and normalize it to the internal format.

**Capabilities:**
- REST API endpoints (POST `/api/v1/analyze`, POST `/api/v1/upload`).
- JSON and CSV parsing with configurable column mapping.
- OEM adapter library: pluggable schema mappers for different BMS/telematics formats.
- Input validation: required fields, data type checks, range checks.
- Rate limiting and authentication (API key-based).

**Internal Format:**

```cpp
namespace ev::battery::common {

struct Sample {
    double timestamp_s;     // Unix timestamp (seconds, double for sub-second)
    double voltage_v;       // Terminal voltage (V)
    double current_a;       // Current (A), positive = charge
    double temperature_c;   // Temperature (°C)
    // Optional fields
    std::optional<std::vector<double>> cell_voltages_v; // Per-cell voltages
};

struct InternalTimeSeries {
    std::string session_id;
    std::string pack_id;
    std::string chemistry;           // "LFP", "NMC", "UNKNOWN"
    double nominal_capacity_ah;      // Rated capacity
    std::vector<Sample> samples;
    std::unordered_map<std::string, std::string> metadata;
};

} // namespace ev::battery::common
```

---

## 6. Functional Requirements

### 6.1 Data Ingestion (M01)

| ID | Requirement | Acceptance Criteria |
|----|------------|---------------------|
| FR-01.01 | The system SHALL accept telemetry data as JSON via HTTP POST. | POST to `/api/v1/analyze` with valid JSON returns 200 and a job ID. |
| FR-01.02 | The system SHALL accept telemetry data as CSV file upload. | POST to `/api/v1/upload` with multipart CSV returns 200 and a job ID. |
| FR-01.03 | The system SHALL normalize diverse OEM schemas to `InternalTimeSeries`. | At least 3 distinct OEM formats parse correctly in integration tests. |
| FR-01.04 | The system SHALL validate input data and return descriptive errors. | Missing required fields return 400 with field-level error messages. |
| FR-01.05 | The system SHALL support configurable column mapping via adapter config files. | A new OEM can be onboarded by adding a JSON adapter config, no code change. |

### 6.2 Cleaning (M02)

| ID | Requirement | Acceptance Criteria |
|----|------------|---------------------|
| FR-02.01 | The system SHALL remove samples with voltage, current, or temperature outside physical limits. | 100% of out-of-range samples removed in test data. |
| FR-02.02 | The system SHALL detect and handle timestamp gaps > 5 seconds. | Gaps are detected; segments are split at gap boundaries. |
| FR-02.03 | The system SHALL detect stuck sensors (constant value > 30 seconds). | Stuck runs are flagged and excluded from downstream processing. |
| FR-02.04 | The system SHALL produce a cleaning report listing all modifications. | Report includes count removed, reasons, and affected time ranges. |

### 6.3 Segmentation (M03)

| ID | Requirement | Acceptance Criteria |
|----|------------|---------------------|
| FR-03.01 | The system SHALL classify each sample as CHARGE, DISCHARGE, or REST. | Classification matches ground truth on labeled test data with > 99% accuracy. |
| FR-03.02 | The system SHALL identify constant-current (CC) charging phases. | CC phases with ≥ 60s duration and ±5% current stability are correctly identified. |
| FR-03.03 | The system SHALL compute voltage span and duration for each segment. | Values match manual computation within floating-point tolerance. |
| FR-03.04 | The system SHALL mark CC windows with ≥ 0.3V span as "usable for ICA." | Only windows meeting the threshold are passed to the ICA module. |

### 6.4 Denoising (M04)

| ID | Requirement | Acceptance Criteria |
|----|------------|---------------------|
| FR-04.01 | The system SHALL implement a Savitzky-Golay filter with configurable window and order. | Filter output matches reference implementation (SciPy) to within 1e-10 on identical input. |
| FR-04.02 | The system SHALL implement a Kalman smoother (forward-backward) for voltage. | Smoothed voltage SNR improvement ≥ 10 dB on synthetic noisy data. |
| FR-04.03 | The system SHALL preserve the shape of the underlying voltage curve. | Peak positions in dQ/dV from smoothed data match those from noise-free reference within ±2 mV. |

### 6.5 ICA/DVA (M05)

| ID | Requirement | Acceptance Criteria |
|----|------------|---------------------|
| FR-05.01 | The system SHALL compute dQ/dV via direct differentiation of smoothed Q(V). | Output matches reference computation on known dataset. |
| FR-05.02 | The system SHALL optionally compute dQ/dV via GPR with uncertainty bands. | GPR derivative within ±5% of direct differentiation; uncertainty bands cover true curve ≥ 90% of the time. |
| FR-05.03 | The system SHALL compute dV/dQ (DVA) for LFP applications. | DVA peaks are detectable on LFP test data. |
| FR-05.04 | The system SHALL handle partial charge windows (not full 0–100% SoC). | dQ/dV computed correctly on windows as narrow as 0.3V. |

### 6.6 Feature Extraction (M06)

| ID | Requirement | Acceptance Criteria |
|----|------------|---------------------|
| FR-06.01 | The system SHALL detect peaks in dQ/dV curves. | All peaks with prominence > configurable threshold are detected; no false peaks in noise floor. |
| FR-06.02 | The system SHALL extract peak voltage, height, area, and FWHM. | Values match manual measurement within 1% on reference curves. |
| FR-06.03 | The system SHALL compute feature shifts relative to a fresh-cell reference. | Shifts are monotonically increasing with cycle count on aging test data. |
| FR-06.04 | The system SHALL output a fixed-dimension feature vector regardless of peak count. | Zero-padding or max-peak selection ensures consistent dimensionality. |

### 6.7 Calibration (M07)

| ID | Requirement | Acceptance Criteria |
|----|------------|---------------------|
| FR-07.01 | The system SHALL estimate current-sensor offset from rest segments. | Offset estimated within ±10 mA of true offset on synthetic data. |
| FR-07.02 | The system SHALL estimate current-sensor gain error from overlapping charge windows. | Gain error estimated within ±0.5% of true gain on synthetic data. |
| FR-07.03 | The system SHALL track calibration parameters across sessions. | Drift > 50 mA/month triggers a warning. |
| FR-07.04 | The system SHALL apply corrections to the current signal before ICA computation. | Corrected ΔQ error < 0.5% on synthetic data with known offset/gain. |

### 6.8 Estimation (M08)

| ID | Requirement | Acceptance Criteria |
|----|------------|---------------------|
| FR-08.01 | The system SHALL estimate SoH (capacity ratio) as a percentage. | RMSE < 2.5% on held-out test set (cell-level split). |
| FR-08.02 | The system SHALL estimate DCIR from current-step features. | RMSE < 5 mΩ on test data with known DCIR. |
| FR-08.03 | The system SHALL provide separate models for LFP and NMC chemistries. | LFP model accuracy within 3% on LFP test data; NMC within 2.5%. |
| FR-08.04 | The system SHALL run inference using ONNX Runtime. | Model loads and produces output in < 10 ms per sample. |

### 6.9 Uncertainty (M09)

| ID | Requirement | Acceptance Criteria |
|----|------------|---------------------|
| FR-09.01 | The system SHALL produce prediction intervals with ≥ 90% empirical coverage. | On a calibration test set, ≥ 90% of true values fall within the interval. |
| FR-09.02 | The system SHALL abstain (return INSUFFICIENT_DATA) when interval width > 15%. | Abstention rate on low-quality inputs > 50%; abstention rate on high-quality inputs < 5%. |

### 6.10 Output (M13)

| ID | Requirement | Acceptance Criteria |
|----|------------|---------------------|
| FR-13.01 | The system SHALL generate a JSON health certificate. | JSON validates against the defined schema. |
| FR-13.02 | The system SHALL include model version and input data hash in every certificate. | Hash is SHA-256 of the input data; model version matches loaded model. |
| FR-13.03 | The system SHALL generate a BPAN-formatted payload. | Payload contains required Tier 3 BDD fields. |

---

## 7. Non-Functional Requirements

### 7.1 Performance

| ID | Requirement | Acceptance Criteria |
|----|------------|---------------------|
| NFR-01 | Full pipeline processing of a 1-hour charge log (3600 samples, 1 Hz) SHALL complete in < 500 ms on a single core. | Benchmark passes on reference hardware (4-core x86-64, 2.5 GHz). |
| NFR-02 | Batch processing of 100 logs SHALL complete in < 30 seconds using multi-threading. | Benchmark passes with 4 threads. |
| NFR-03 | Memory usage for a single log processing SHALL not exceed 50 MB. | Measured via profiler. |

### 7.2 Reliability

| ID | Requirement | Acceptance Criteria |
|----|------------|---------------------|
| NFR-04 | The system SHALL handle malformed input without crashing. | Fuzz testing with 10,000 random inputs produces zero crashes. |
| NFR-05 | The system SHALL log all errors with structured context (spdlog). | Error logs include input ID, module name, and error details. |

### 7.3 Portability

| ID | Requirement | Acceptance Criteria |
|----|------------|---------------------|
| NFR-06 | The core library SHALL compile on Windows (MSVC), Linux (GCC/Clang), and macOS (Clang). | CI builds pass on all three platforms. |
| NFR-07 | The core library SHALL have no dependency on Python at runtime. | `ldd` / `dumpbin` shows no Python shared library dependency. |

### 7.4 Maintainability

| ID | Requirement | Acceptance Criteria |
|----|------------|---------------------|
| NFR-08 | Code coverage SHALL be ≥ 80% on core modules. | Coverage report generated in CI. |
| NFR-09 | All public APIs SHALL have Doxygen documentation. | `doxygen` generates without warnings. |
| NFR-10 | Adding a new OEM adapter SHALL require no changes to core modules. | Demonstrated by adding a mock OEM adapter config. |

### 7.5 Security

| ID | Requirement | Acceptance Criteria |
|----|------------|---------------------|
| NFR-11 | The REST API SHALL require API key authentication. | Requests without a valid key return 401. |
| NFR-12 | Input data SHALL be validated before processing. | Buffer overflow and injection tests pass. |

---

## 8. Data Requirements

### 8.1 Training Datasets

| Dataset | Source | Chemistry | Use | License |
|---------|--------|-----------|-----|---------|
| NASA Battery Dataset | NASA PCoE | 18650 (LCO) | Baseline training, demo | Public domain |
| CALCE Battery Dataset | U. Maryland | Various | Validation | Academic use |
| Oxford Battery Degradation | U. Oxford | NMC | Training | CC-BY |
| MIT-Stanford-Toyota | MIT | LFP, NMC | Fast-charge degradation | Open |
| Sandia National Lab | SNL | LFP, NMC | Thermal abuse data | Public domain |
| **Synthetic (M10)** | Generated | LFP, NMC | Augmented training | Internal |
| **Field telemetry** | Fleet pilots | LFP (Indian 2W/3W) | Fine-tuning, validation | Under NDA |

### 8.2 Reference Data

| Data | Description | Format |
|------|-------------|--------|
| Fresh-cell dQ/dV references | Per-chemistry reference curves | JSON (voltage, dQ/dV arrays) |
| OCV-SoC lookup tables | Open-circuit voltage vs. SoC for each chemistry | JSON |
| Half-cell OCV curves | Anode and cathode half-cell potentials | CSV |

### 8.3 Data Provenance Requirements

Every dataset used must have a provenance record containing:
- Name and version
- Source URL
- Download date
- License
- SHA-256 hash of the downloaded file
- Any preprocessing applied

---

## 9. Interface Specifications

### 9.1 Core Pipeline Interface (C++)

```cpp
namespace ev::battery {

/**
 * @brief Configuration for the full analysis pipeline.
 */
struct PipelineConfig {
    // Cleaning
    double voltage_min_v = 0.0;
    double voltage_max_v = 100.0;
    double current_min_a = -500.0;
    double current_max_a = 500.0;
    double temperature_min_c = -40.0;
    double temperature_max_c = 100.0;
    double gap_threshold_s = 5.0;

    // Segmentation
    double rest_current_threshold_a = 0.05;
    double cc_current_tolerance = 0.05;   // fractional (5%)
    double cc_min_duration_s = 60.0;
    double min_voltage_span_v = 0.3;

    // Denoising
    int savgol_window = 21;
    int savgol_order = 3;
    double kalman_process_noise = 1e-5;
    double kalman_measurement_noise = 1e-3;

    // ICA
    bool use_gpr = false;
    double gpr_length_scale = 0.01;
    double gpr_noise_level = 1e-4;

    // Features
    double peak_min_prominence = 0.1;
    std::string chemistry = "NMC";       // "LFP" or "NMC"
    std::string reference_path = "";      // Path to fresh-cell reference

    // Estimation
    std::string model_path = "";          // Path to ONNX model
    double abstention_threshold = 0.15;   // 15% interval width
    double coverage_level = 0.90;

    // Calibration
    bool enable_calibration = true;
};

/**
 * @brief Full health report output.
 */
struct HealthReport {
    // Identity
    std::string session_id;
    std::string pack_id;
    std::string model_version;
    std::string input_hash_sha256;

    // Quality
    double quality_score;        // 0-100
    std::string quality_label;   // "EXCELLENT", "ACCEPTABLE", "MARGINAL", "REJECT"

    // Calibration
    double calibrated_offset_a;
    double calibrated_gain;

    // SoH
    double soh_percent;          // 0-100
    double soh_lower_bound;
    double soh_upper_bound;
    double soh_coverage;
    bool abstained;

    // DCIR
    double dcir_mohm;

    // RUL (if sufficient historical data)
    std::optional<double> rul_cycles;
    std::optional<double> rul_months;

    // Thermal
    double thermal_stress_score; // 0-100
    double cumulative_high_temp_hours;

    // ICA features (for transparency)
    std::vector<double> peak_voltages;
    std::vector<double> peak_heights;

    // Timestamps
    double analysis_timestamp_s;
    double data_start_timestamp_s;
    double data_end_timestamp_s;
};

/**
 * @brief Main pipeline interface.
 */
class Pipeline {
public:
    explicit Pipeline(const PipelineConfig& config);
    ~Pipeline();

    /**
     * @brief Analyze a telemetry time series and produce a health report.
     * @param data Normalized time series input.
     * @return HealthReport or an error.
     */
    std::expected<HealthReport, std::string> analyze(
        const common::InternalTimeSeries& data
    );

    /**
     * @brief Analyze multiple time series in batch (multi-threaded).
     * @param batch Vector of time series inputs.
     * @return Vector of results.
     */
    std::vector<std::expected<HealthReport, std::string>> analyzeBatch(
        const std::vector<common::InternalTimeSeries>& batch
    );
};

} // namespace ev::battery
```

### 9.2 REST API Interface

| Endpoint | Method | Body | Response |
|----------|--------|------|----------|
| `/api/v1/analyze` | POST | JSON telemetry | `HealthReport` (JSON) |
| `/api/v1/upload` | POST | Multipart CSV | Job ID → poll `/api/v1/status/{id}` |
| `/api/v1/status/{id}` | GET | — | Job status + result when complete |
| `/api/v1/health` | GET | — | Server health check |
| `/api/v1/version` | GET | — | API version, model version |

---

## 10. Design Decisions and Alternatives

### DEC-000.01: Primary Language — C++ vs. Python vs. Rust

**Context:** The system performs heavy numerical computation (signal processing, matrix operations, differentiation) and needs to be deployed as a high-performance service.

| Option | Pros | Cons |
|--------|------|------|
| **C++20** | Fastest execution, SIMD support, Eigen ecosystem, ONNX Runtime native API, mature for numerical computing | Longer development time, memory safety requires discipline, slower iteration |
| **Python** | Fastest development, rich ML ecosystem (PyTorch, scikit-learn, scipy), good for prototyping | 10-100× slower for numerical pipelines, GIL limits parallelism, dependency on Python runtime in production |
| **Rust** | Memory safety, good performance, growing ecosystem | Smaller numerical computing ecosystem, fewer ML inference options, steeper learning curve |

**Decision:** C++20 for all core computation. Python for ML training only, with ONNX export for C++ inference.

**Rationale:** The 500 ms processing target on a single core requires C++-level performance. Python would necessitate extensive C extensions anyway (which is what NumPy/SciPy already are). Rust lacks mature equivalents to Eigen and ONNX Runtime's C++ API. The team has C++ expertise.

**Consequences:** Slower initial development velocity, but production-ready performance from the start. No Python runtime dependency in deployment.

---

### DEC-000.02: Monolith vs. Microservices

**Context:** The system has 15 modules. Should they be separate services?

| Option | Pros | Cons |
|--------|------|------|
| **Modular monolith** | Simple deployment, in-process communication (zero overhead), easier debugging, suitable for small team | Scaling requires scaling everything; tighter coupling risk |
| **Microservices** | Independent scaling, polyglot flexibility | Massive operational overhead for 1-3 engineers, network latency between modules, distributed system complexity |

**Decision:** Modular monolith.

**Rationale:** The entire pipeline processes one log sequentially. There is no benefit to network-separating sequential stages. The team is 1-3 engineers. Microservices would consume more engineering time in infrastructure than in actual product development.

**Consequences:** Must maintain module boundaries via code discipline (separate CMake targets, interface-based coupling). Migration to services later is possible because modules already have clean interfaces.

---

### DEC-000.03: HTTP Framework — cpp-httplib vs. Drogon vs. Crow

**Context:** Need a C++ HTTP server for the REST API.

| Option | Pros | Cons |
|--------|------|------|
| **cpp-httplib** | Single header, trivial to integrate, sufficient for moderate load | Limited async support, no WebSocket, may bottleneck at high concurrency |
| **Drogon** | High performance (async, non-blocking), ORM, WebSocket, mature | Heavier dependency, more complex setup |
| **Crow** | Flask-like API, easy to learn | Less actively maintained, fewer features |

**Decision:** Deferred to Iteration 3 (when M01 Ingestion is built). Start with cpp-httplib for simplicity; migrate to Drogon if load testing reveals bottlenecks.

---

### DEC-000.04: Kalman Filter — Custom vs. Library

**Context:** Need a Kalman filter/smoother for voltage denoising.

| Option | Pros | Cons |
|--------|------|------|
| **Custom implementation with Eigen** | Full control, can optimize for our specific state model, no extra dependency | Development and testing time |
| **Use a library (e.g., kalman, tiny-ekf)** | Faster to integrate | Most C++ Kalman libraries are academic, poorly maintained, or don't include RTS smoother |

**Decision:** Custom implementation with Eigen. The 1D Kalman smoother is ~100 lines of code. The RTS backward pass is another ~50. This is more reliable than depending on a niche library.

---

### DEC-000.05: Conformal Prediction — Library vs. Custom

**Context:** Need split conformal prediction for uncertainty quantification.

| Option | Pros | Cons |
|--------|------|------|
| **Custom implementation** | Simple algorithm (~50 lines), no dependency, full control over calibration set management | Must implement and test from scratch |
| **Use MAPIE or similar Python library** | Well-tested, feature-rich | Python dependency, must bridge to C++ |

**Decision:** Custom implementation in C++. Split conformal prediction is algorithmically simple (sort nonconformity scores, take quantile). The complexity is in the calibration set management, which is application-specific anyway.

---

## 11. Risk Register

| ID | Risk | Likelihood | Impact | Mitigation |
|----|------|-----------|--------|------------|
| R-01 | LFP dQ/dV peaks are too subtle for reliable feature extraction from noisy field data. | High | High | Use DVA (dV/dQ) as fallback for LFP. Invest in GPR-based smoothing. Test LFP early (Iteration 2). |
| R-02 | Field telemetry sampling rate is too low (< 1 Hz) for ICA. | Medium | High | Telemetry Quality Score rejects low-rate data. Design features that degrade gracefully. |
| R-03 | Current-sensor errors are too large or nonlinear for the calibration method to correct. | Medium | Medium | Calibration module reports confidence; low-confidence calibrations trigger abstention. |
| R-04 | ONNX Runtime integration issues on Windows. | Low | Medium | ONNX Runtime has mature MSVC support. Test in Iteration 0 CI. |
| R-05 | Public datasets don't represent Indian 2W/3W conditions. | High | High | Synthetic data module (M10) bridges the gap. Field pilots provide real data. |
| R-06 | GPR in C++ is too slow for real-time processing. | Medium | Medium | Limit GPR to short segments (< 500 points). Pre-compute kernel matrices. Use Cholesky with Eigen. |
| R-07 | Scope creep from attempting all 15 modules simultaneously. | High | High | Strict iteration discipline. Each iteration delivers 1-3 modules. No parallel module development. |
| R-08 | Model overfits to public dataset cell types and doesn't generalize. | Medium | High | Per-chemistry models. Synthetic data augmentation. Fleet adaptation (M11) with few-shot calibration. |

---

## 12. Iteration Roadmap

### Iteration 0: Foundation (Target: 1-2 weeks)
**Delivers:** Project scaffolding, build system, CI pipeline, M15 (math_utils).
**Goal:** `cmake --build .` succeeds. `ctest` runs math utility tests. Benchmark harness works.

### Iteration 1: Signal Processing Core (Target: 2-3 weeks)
**Delivers:** M02 (cleaning), M03 (segmentation), M04 (denoising).
**Goal:** Can ingest a raw CSV charge log, clean it, segment it, and produce smoothed voltage/current arrays. Validated on NASA dataset.

### Iteration 2: ICA/DVA and Features (Target: 2-3 weeks)
**Delivers:** M05 (ica_dva), M06 (features).
**Goal:** dQ/dV peaks extracted from smoothed data. Feature vectors match expected degradation trends on CALCE aging data. LFP tested.

### Iteration 3: Ingestion API and Quality Score (Target: 2 weeks)
**Delivers:** M01 (ingestion), M12 (quality_score).
**Goal:** REST API accepts JSON/CSV. Quality score correctly classifies good vs. bad logs.

### Iteration 4: Sensor Calibration (Target: 2-3 weeks)
**Delivers:** M07 (calibration).
**Goal:** Offset and gain correction demonstrated on synthetic data with known errors.

### Iteration 5: SoH Estimation (Target: 2-3 weeks)
**Delivers:** M08 (estimation).
**Goal:** Trained model (XGBoost → ONNX) achieves < 2.5% RMSE on test set. Inference in C++ via ONNX Runtime.

### Iteration 6: Uncertainty (Target: 1-2 weeks)
**Delivers:** M09 (uncertainty).
**Goal:** Conformal prediction intervals with ≥ 90% coverage. Abstention on low-quality inputs.

### Iteration 7: Synthetic Data (Target: 2-3 weeks)
**Delivers:** M10 (synthetic_data).
**Goal:** Half-cell model generates synthetic dQ/dV curves. Training on synthetic + real data improves accuracy.

### Iteration 8: Thermal + Output (Target: 2 weeks)
**Delivers:** M14 (thermal), M13 (output).
**Goal:** Thermal stress score computed. JSON certificate and BPAN payload generated.

### Iteration 9: Fleet Adaptation (Target: 2 weeks)
**Delivers:** M11 (fleet_adapt).
**Goal:** Few-shot calibration on a held-out fleet subset reduces error vs. base model.

### Iteration 10: Integration and Hardening (Target: 2-3 weeks)
**Delivers:** Full pipeline integration, performance optimization, documentation.
**Goal:** End-to-end test: raw JSON → health certificate in < 500 ms. All quality gates pass.

---

## 13. Glossary

| Term | Definition |
|------|------------|
| **SoH** | State of Health. Ratio of current maximum capacity to rated capacity, expressed as percentage. |
| **RUL** | Remaining Useful Life. Estimated cycles or time until SoH drops below a threshold (typically 80%). |
| **DCIR** | DC Internal Resistance. Measured from voltage response to current steps. |
| **ICA** | Incremental Capacity Analysis. Plot of dQ/dV vs. V to reveal electrochemical phase transitions. |
| **DVA** | Differential Voltage Analysis. Plot of dV/dQ vs. Q, the reciprocal of ICA. |
| **LLI** | Loss of Lithium Inventory. Degradation mode where cyclable lithium is consumed (e.g., SEI growth). |
| **LAM** | Loss of Active Material. Degradation mode where electrode material becomes electrochemically inactive. |
| **CC** | Constant Current. Charging phase where current is held steady. |
| **GPR** | Gaussian Process Regression. A probabilistic, non-parametric regression method. |
| **BPAN** | Battery Pack Aadhaar Number. India's mandatory battery identity and lifecycle tracking system. |
| **BDD** | Battery Dynamic Data. BPAN Tier 3 data: SoH, cycle count, thermal events, status. |
| **LFP** | Lithium Iron Phosphate (LiFePO₄). Common cathode chemistry in Indian 2W/3W. |
| **NMC** | Nickel Manganese Cobalt (LiNiₓMnyCo₂O₂). Higher energy density cathode chemistry. |
| **OCV** | Open Circuit Voltage. Thermodynamic equilibrium voltage at a given SoC. |
| **SoC** | State of Charge. Current charge level as a fraction of maximum capacity. |
| **NBFC** | Non-Banking Financial Company. Indian financial institution type. |
| **OEM** | Original Equipment Manufacturer. Vehicle or battery manufacturer. |
| **ONNX** | Open Neural Network Exchange. Interoperable format for ML models. |

---

## 14. Appendices

### Appendix A: Chemistry-Specific Considerations

#### A.1 NMC (Nickel Manganese Cobalt)

- **Voltage range:** 2.8V – 4.2V (per cell).
- **dQ/dV characteristics:** Distinct peaks corresponding to phase transitions in layered cathode. Peaks are well-separated and relatively easy to track.
- **Degradation signatures:** Peak height loss (LAM), peak shift to higher voltage (LLI), broadening (impedance growth).
- **ICA suitability:** HIGH. NMC is the easier chemistry for ICA-based SoH estimation.

#### A.2 LFP (Lithium Iron Phosphate)

- **Voltage range:** 2.5V – 3.65V (per cell).
- **dQ/dV characteristics:** Very flat voltage plateau (~3.3V) produces a single dominant, sharp peak. Subtle secondary peaks exist but are easily lost in noise.
- **Degradation signatures:** Peak height loss, subtle peak splitting. Much less feature-rich than NMC.
- **ICA suitability:** MODERATE TO LOW. The flat voltage curve means dQ/dV has fewer and subtler features. DVA (dV/dQ) may be more informative. Higher SNR requirements.
- **Mitigation:** Use DVA as primary analysis for LFP. Require longer CC windows (> 0.5V span). Apply more aggressive smoothing. Consider full-curve shape matching instead of peak-based features.

### Appendix B: Sensor Error Models

#### B.1 Current Sensor Error Model

```
I_measured = gain × I_true + offset + noise

Where:
  gain    ~ 1.0 ± 0.05 (5% gain error, typical for low-cost shunt/Hall)
  offset  ~ 0.0 ± 0.3 A (300 mA offset, typical for uncalibrated Hall)
  noise   ~ N(0, σ²), σ ~ 0.05-0.2 A depending on sensor quality
```

#### B.2 Voltage Sensor Error Model

```
V_measured = V_true + quantization + noise

Where:
  quantization = round(V_true / resolution) × resolution
  resolution   ~ 1-10 mV (typical BMS ADC)
  noise        ~ N(0, σ²), σ ~ 1-5 mV
```

### Appendix C: Public Dataset Download Scripts

Scripts for downloading and verifying public datasets will be provided in `scripts/download_datasets.py` with SHA-256 hash verification.

---

## Revision History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2026-10-03 | Initial SRS baseline. |
