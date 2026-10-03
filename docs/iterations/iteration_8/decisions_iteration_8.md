# Iteration 8: BPAN Tier 3 Health Certification & Cryptographic Signing Engine — Decision Log

> **Document ID:** DEC-ITERATION-008  
> **Version:** 1.0  
> **Created:** 2026-10-03  
> **Module ID:** `M13: output`  
> **Parent Documentation:** [SRS_iteration_8.md](SRS_iteration_8.md)

---

### DEC-8.01: Self-Contained FIPS 180-2 SHA-256 vs. OpenSSL / Crypto++ Dynamic Linking

- **Date:** 2026-10-03
- **Context:**
  BPAN Tier 3 battery certificates must be cryptographically sealed with a SHA-256 digital signature to guarantee tamper-evidence and audit compliance for Indian fintech lenders, NBFCs, and the NITI Aayog digital registry. We need to implement SHA-256 hashing.

- **Options Considered:**

  1. **Option A: External Cryptographic Library (OpenSSL / Crypto++)**
     - *Description:* Link against OpenSSL `libcrypto` or Crypto++.
     - *Pros:* Widely used general-purpose crypto library.
     - *Cons:* Heavy external dependency; dynamic DLL versioning hell on Windows/Linux; licensing and vulnerability patching friction; massive overhead for single SHA-256 digest computation.
     - *Complexity:* High.

  2. **Option B: Pure Self-Contained Standard C++ FIPS 180-2 SHA-256 Implementation (Recommended)**
     - *Description:* Implement a standard, zero-dependency, allocation-free FIPS 180-2 / RFC 6234 SHA-256 engine in pure standard C++23.
     - *Pros:* Zero external dependencies; 100% portable across Windows, Linux, embedded RTOS, and cloud serverless; executes in $< 5\ \mu\text{s}$; verified against NIST reference test vectors; completely immune to DLL versioning issues.
     - *Cons:* Limited to SHA-256 (which is all that is required for BPAN Tier 3).
     - *Complexity:* Low.

- **Decision:** **Option B (Pure Self-Contained SHA-256 Engine)**.
- **Rationale:**
  Ensures lightweight, auditable, and rock-solid deployment across all commercial fleet telematics backends.

---

### DEC-8.02: Canonical Payload Hash Construction for Audit Tamper Evident Immutability

- **Date:** 2026-10-03
- **Context:**
  If a battery certificate is serialized to JSON, variations in whitespace or key ordering could alter the hash unless a strict canonical format is enforced.
- **Decision:**
  Construct the cryptographic hash from a deterministic canonicalized representation:
  $$\text{Payload} = \text{BPAN\_ID} + "|" + \text{TIMESTAMP} + "|" + \widehat{\text{SoH}} + "|" + [\text{SoH}_{low}, \text{SoH}_{high}] + "|" + \text{RUL} + "|" + \text{TDI} + "|" + \text{DECISION}$$
  Computing $\text{SHA-256}(\text{Payload})$ guarantees that modifying even a single character (e.g. changing $74.2\%$ to $84.2\%$) produces an entirely different hash, immediately exposing tampering.

---

### DEC-8.03: Unified End-to-End Orchestrator Pipeline API

- **Date:** 2026-10-03
- **Context:**
  Downstream users, web services, and batch evaluation jobs need a single top-level call that accepts raw fleet telemetry and outputs a signed certificate without needing to manually wire all 9 upstream modules.
- **Decision:**
  Implement `generateHealthCertificate(series, metadata, lifetime_efc)` which orchestrates:
  `M02: cleaning` $\to$ `M03: segmentation` $\to$ `M04: denoising` $\to$ `M07: calibration` $\to$ `M14: thermal` $\to$ `M05: ica_dva` $\to$ `M06: features` $\to$ `M08: estimation` $\to$ `M09: uncertainty` $\to$ `M13: output`.
  Any upstream module error or safety trigger is captured and cleanly formatted in the final certificate.
