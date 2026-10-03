# Iteration 9: Telemetry Ingestion & Quality Scoring (TQS) Gate 1 Engine — M01 & M12
## System Requirements Specification (SRS)

> **Document ID:** SRS-ITERATION-009  
> **Version:** 1.0  
> **Created:** 2026-10-03  
> **Module IDs:** `M01: ingestion`, `M12: quality_score`  
> **Target Architecture:** Indian 2W/3W Passive-Cooled Fleet Ecosystem & BPAN Tier 3  
> **Parent Architecture:** [FLOW-001 (docs/flow.md)](../../flow.md)

---

## 1. Scope

### 1.1 In Scope
- **M01 Multi-Format Ingestion & Schema Normalization:**
  - Robust parser for common Indian OEM fleet telematics: CSV files and JSON payload streams.
  - Multi-unit conversion handling:
    - Voltage: Volts, milliVolts (mV), centiVolts (cV).
    - Current: Amperes (A), milliAmperes (mA).
    - Temperature: Celsius (°C), deci-Celsius (0.1 °C / d°C).
    - Timestamps: Unix epoch seconds, Unix epoch milliseconds, ISO-8601 strings.
  - Generates standardized, memory-contiguous `common::InternalTimeSeries`.
  - Zero allocation on per-sample parsing loops.
- **M12 Telemetry Quality Scoring (TQS) Engine & Gate 1 Enforcement:**
  - Evaluates continuous telemetry health across 4 physical dimensions:
    1. **Sampling Regularity ($S_{jitter} \in [0, 25]$):** Measures timestamp interval coefficient of variation ($CV_{\Delta t}$).
    2. **Packet Completeness ($S_{drop} \in [0, 30]$):** Quantifies transmission dropouts and missing telemetry packets.
    3. **Physical Plausibility ($S_{plaus} \in [0, 25]$):** Validates voltage ($[30\text{ V}, 65\text{ V}]$), current, and temperature ($[-10^\circ\text{C}, 75^\circ\text{C}]$).
    4. **Signal Stability & SNR ($S_{snr} \in [0, 20]$):** Measures high-frequency noise variance relative to signal dynamic range.
  - Computes composite $\text{TQS} \in [0, 100]$ and categorical ranking:
    - $\ge 85$: `EXCELLENT`
    - $[65, 85)$: `ACCEPTABLE`
    - $[30, 65)$: `MARGINAL`
    - $< 30$: `REJECT` (Gate 1 Failure: automatic rejection before entering downstream pipeline).
- **Integration with Orchestrator:**
  - Enhances `generateHealthCertificate` with front-of-pipe ingestion and Gate 1 pre-qualification.

### 1.2 Out of Scope
- Hardware BLE/CAN transceiver hardware firmware (ESP32/STM32) — this engine acts on the decoded data frames.

---

## 2. Gate 0: Prerequisite & Requirement Verification Audit

| Category | Prerequisite Check | Status | Evidence / Reference |
| :--- | :--- | :---: | :--- |
| **Common Types** | `Sample`, `InternalTimeSeries`, `QualityLabel`, `PipelineError` | **SATISFIED** | Defined in [include/ev/battery/common/types.hpp](../../include/ev/battery/common/types.hpp). |
| **Math Kernel** | Monotonic statistics, Kahan variance, robust dispersion | **SATISFIED** | Implemented in `M15: math_utils`. |
| **Downstream Pipelines** | Cleaning (M02), Segmentation (M03), Calibration (M07), Output (M13) | **SATISFIED** | 116 passing tests across all modules. |

---

## 3. Requirements

### 3.1 Functional Requirements

- **FR-9.01 (CSV Ingestion):** Parse standard comma-separated and semicolon-separated CSV telemetry files with customizable or auto-detected header mapping (`timestamp`, `voltage`, `current`, `temperature`).
- **FR-9.02 (JSON Ingestion):** Parse JSON telemetry payloads (both single records and batch arrays) handling standard OEM field aliases (`v`/`volt`, `i`/`curr`, `temp`/`t`).
- **FR-9.03 (Unit Standardization):** Normalize raw readings (mV, mA, deci-°C) into standard SI/metric float units ($V, A, ^\circ\text{C}, s$).
- **FR-9.04 (Composite TQS Formulation):** Compute composite Telemetry Quality Score $\text{TQS} = S_{jitter} + S_{drop} + S_{plaus} + S_{snr} \in [0, 100]$.
- **FR-9.05 (Gate 1 Safety Enforcement):** If $\text{TQS} < 30$, reject session with `QualityLabel::REJECT` and emit diagnostic reasons.

### 3.2 Non-Functional Requirements

- **NFR-9.01 (Ingestion Throughput):** CSV/JSON parsing must process at least **$100,000\text{ samples/second}$** on a single thread.
- **NFR-9.02 (TQS Latency):** Scoring a 10,000-sample session must take **$< 1.0\text{ ms}$**.
- **NFR-9.03 (Allocation Safety):** Ingestion must pre-reserve memory buffers when row count is known or estimated, minimizing memory fragmentation.

---

## 4. Interfaces & Data Structures

```cpp
namespace ev::battery::ingestion {

struct CsvColumnMapping {
    std::string timestamp_col = "timestamp";
    std::string voltage_col = "voltage";
    std::string current_col = "current";
    std::string temp_col = "temperature";
    std::string soc_col = "soc";
    double voltage_scale = 1.0;     // 0.001 if mV
    double current_scale = 1.0;     // 0.001 if mA
    double temp_scale = 1.0;        // 0.1 if deci-degC
    bool timestamp_is_millis = false;
};

[[nodiscard]] common::Result<common::InternalTimeSeries> parseCsv(
    std::string_view csv_content,
    const CsvColumnMapping& mapping = {}
);

[[nodiscard]] common::Result<common::InternalTimeSeries> parseJson(
    std::string_view json_content
);

} // namespace ev::battery::ingestion

namespace ev::battery::quality_score {

struct QualityReport {
    double total_score = 0.0;            ///< Composite TQS in [0, 100]
    double jitter_score = 0.0;           ///< S_jitter in [0, 25]
    double dropout_score = 0.0;          ///< S_drop in [0, 30]
    double plausibility_score = 0.0;     ///< S_plaus in [0, 25]
    double snr_score = 0.0;              ///< S_snr in [0, 20]
    double dropout_fraction = 0.0;       ///< Ratio of missing interval duration
    double sampling_jitter_cv = 0.0;     ///< Coefficient of variation of dt
    common::QualityLabel quality_label = common::QualityLabel::REJECT;
    bool gate1_passed = false;           ///< True if total_score >= 30.0
    std::string diagnostic_summary;
};

[[nodiscard]] QualityReport computeQualityScore(
    const common::InternalTimeSeries& series,
    double nominal_dt_s = 1.0
);

} // namespace ev::battery::quality_score
```

---

## 5. Acceptance Criteria

1. **Format Versatility:** Accurately parse standard CSV and JSON with various unit scales ($mV, mA, d^\circ\text{C}, ms$).
2. **Deterministic TQS:** Pristine synthetic sessions score $\ge 90$ (`EXCELLENT`), sessions with severe dropouts and jitter score $< 30$ (`REJECT`).
3. **Gate 1 Protection:** Sessions with $\text{TQS} < 30$ are blocked from downstream valuation.
4. **Latency:** Parse $10,000$ samples in $< 20\text{ ms}$; score $10,000$ samples in $< 1\text{ ms}$.
5. **100% test pass rate across all suites.**
