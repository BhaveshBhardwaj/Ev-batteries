# Iteration 8: BPAN Tier 3 Health Certification & Cryptographic Signing Engine — M13
## System Requirements Specification (SRS)

> **Document ID:** SRS-ITERATION-008  
> **Version:** 1.0  
> **Created:** 2026-10-03  
> **Module ID:** `M13: output`  
> **Target Architecture:** Indian 2W/3W Passive-Cooled Fleet Ecosystem & BPAN Tier 3  
> **Parent Architecture:** [FLOW-001 (docs/flow.md)](../../flow.md)

---

## 1. Scope

### 1.1 In Scope
- **M13 BPAN Tier 3 Standardized Payload Structure:**
  Format dynamic battery passport data compliant with India's NITI Aayog / MoRTH Battery Pack Aadhaar Number (BPAN) Tier 3 specification for electric 2-wheelers and 3-wheelers:
  - Header: BPAN ID, Pack Serial, Chemistry (LFP/NMC), Form Factor, Nominal Capacity/Voltage.
  - Health & Capacity: Certified SoH (%), Usable Capacity (Ah), Equivalent Full Cycles (EFC).
  - Conformal Bounds: Guaranteed $90\%$ and $95\%$ Confidence Interval $[SoH_{low}, SoH_{high}]$, Interval Width ($\le 5.0\%$).
  - Degradation Analytics: Decoupled $LLI\%$, $LAM\%$, Dominance mode (`BALANCED`, `LLI_DOMINATED`, `LAM_DOMINATED`, `THERMAL_ACCELERATED`).
  - Thermal Exposure History: Lifetime $\text{TDI}$, Dwell time above $40^\circ\text{C}$ and $45^\circ\text{C}$, Peak heating rate.
  - Hardware Diagnostics: Sensor offset $\hat{\alpha}$, Gain factor $\hat{\beta}$, Gate 2 safety status.
  - Gate 1–3 Certification Decision: `CERTIFIED` or `AUDIT_REQUIRED`.
- **M13 Tamper-Evident SHA-256 Cryptographic Signing:**
  Generate a FIPS 180-2 / RFC 6234 compliant SHA-256 cryptographic hash over the canonical certificate fields to guarantee audit immutability across banks, insurers, and fleet operators.
- **M13 Multi-Format Serialization:**
  - Standardized JSON serialization for cloud API dispatch.
  - Formatted text/markdown inspection certificate with cryptographic seal for printing and regulatory filing.
- **M13 Unified End-to-End Pipeline Orchestrator:**
  Provide a top-level single-call API `generateHealthCertificate(series, config)` that executes the complete 9-module chain:
  `Raw Telemetry` $\to$ `Cleaning` $\to$ `Segmentation` $\to$ `Denoising` $\to$ `Calibration` $\to$ `Thermal` $\to$ `ICA/DVA` $\to$ `Features` $\to$ `Estimation` $\to$ `Uncertainty` $\to$ `Signed BPAN Certificate`.

### 1.2 Out of Scope
- Direct hardware BLE/CAN bus flashing — this engine executes as the cloud/edge certification authority.

---

## 2. Gate 0: Prerequisite & Requirement Verification Audit

| Category | Prerequisite Check | Status | Evidence / Reference |
| :--- | :--- | :---: | :--- |
| **All Upstream Modules** | Cleaning, Segmentation, Denoising, Calibration, ICA, Features, Thermal, Estimation, Uncertainty | **SATISFIED** | All 9 upstream modules complete with 111 passing tests and verified benchmarks. |
| **BPAN Tier 3 Spec** | NITI Aayog digital battery passport schema requirements | **SATISFIED** | Formulated in `docs/flow.md` and `context.txt`. |
| **Crypto Standards** | Self-contained SHA-256 hash algorithm | **SATISFIED** | Native C++23 zero-dependency FIPS 180-2 implementation. |

---

## 3. Requirements

### 3.1 Functional Requirements

- **FR-8.01 (BPAN Schema Completeness):** The module shall serialize all mandatory Tier 3 parameters into valid JSON conforming to the BPAN schema.
- **FR-8.02 (SHA-256 Cryptographic Digest):** The module shall generate a deterministic 64-character hexadecimal SHA-256 digest over the canonicalized payload. Modifying any numerical value in the certificate must alter the resulting hash.
- **FR-8.03 (Certification Enforcement):** If Gate 2 (sensor hardware fault) or Gate 3 (interval width $> 5.0\%$ or OOD) fails, the certificate `certification_decision` shall be `AUDIT_REQUIRED`, and `is_certified` shall be `false`.
- **FR-8.04 (End-to-End Execution):** The top-level orchestrator shall successfully ingest raw `InternalTimeSeries` and output a fully formed, cryptographically sealed `HealthCertificate`.

### 3.2 Non-Functional Requirements

- **NFR-8.01 (Certificate Generation Latency):** Certificate formulation and SHA-256 hashing must execute in **$< 0.1\text{ ms}$**.
- **NFR-8.02 (End-to-End Pipeline Latency):** The complete 9-module pipeline from raw telemetry to signed certificate must execute in **$< 15.0\text{ ms}$** for a 10,000-sample session.
- **NFR-8.03 (Zero External Dependencies):** Cryptographic hashing must be 100% self-contained standard C++ without requiring external OpenSSL DLLs or third-party crypto packages.

---

## 4. Interfaces & Data Structures

```cpp
namespace ev::battery::output {

struct BpanMetadata {
    std::string bpan_id;             ///< Battery Pack Aadhaar Number (e.g., "IN-BPAN-2W-LFP-00941")
    std::string serial_number;       ///< Manufacturer pack serial
    std::string chemistry;           ///< "LFP" or "NMC"
    std::string vehicle_category;    ///< "2W", "3W", "SWAP_STATION"
    double nominal_capacity_ah = 30.0;
    double nominal_voltage_v = 51.2;
    int series_cells = 16;
};

struct HealthCertificate {
    BpanMetadata metadata;
    double certified_soh_pct = 100.0;
    double usable_capacity_ah = 30.0;
    double soh_lower_bound_pct = 0.0;
    double soh_upper_bound_pct = 100.0;
    double confidence_interval_width_pct = 0.0;
    double remaining_useful_life_cycles = 0.0;
    double lli_percent = 0.0;
    double lam_percent = 0.0;
    double thermal_degradation_index = 1.0;
    double sensor_offset_bias_a = 0.0;
    double sensor_gain_factor = 1.0;
    bool is_certified = false;
    std::string certification_decision; ///< "CERTIFIED" or "AUDIT_REQUIRED"
    std::string primary_degradation_mode;
    std::string evaluation_timestamp_utc;
    std::string sha256_digital_signature; ///< 64-char hex SHA-256 digest
};

[[nodiscard]] std::string computeSha256(std::string_view input);

[[nodiscard]] std::string serializeCertificateToJson(const HealthCertificate& cert);

[[nodiscard]] std::string generateHumanReadableReport(const HealthCertificate& cert);

[[nodiscard]] common::Result<HealthCertificate> generateHealthCertificate(
    const common::InternalTimeSeries& series,
    const BpanMetadata& metadata,
    double lifetime_efc = 0.0
);

} // namespace ev::battery::output
```

---

## 5. Acceptance Criteria

1. **SHA-256 Digest Correctness:** Hash matches standard reference vectors (FIPS 180-2 verification tests).
2. **Tamper Detection:** Modifying any single character or digit in the certificate changes the cryptographic digest.
3. **End-to-End Sim-to-Real Pipeline Execution:** Generates signed certificate from raw 10,000-sample session in $< 15\text{ ms}$.
4. **100% test pass rate across all suites.**
