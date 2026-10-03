# Iteration 9: Telemetry Ingestion & Quality Scoring (TQS) Gate 1 Engine — Decision Log

> **Document ID:** DEC-ITERATION-009  
> **Version:** 1.0  
> **Created:** 2026-10-03  
> **Module IDs:** `M01: ingestion`, `M12: quality_score`  
> **Parent Documentation:** [SRS_iteration_9.md](SRS_iteration_9.md)

---

### DEC-9.01: Pure C++23 Fast Zero-Dependency CSV & JSON Parsing

- **Date:** 2026-10-03
- **Context:**
  Indian fleet operators upload telematics via periodic CSV batches or REST/MQTT JSON messages. We need to ingest both formats quickly and safely without pulling in heavy external dependencies.
- **Options Considered:**
  1. *Option A:* Pull in `nlohmann/json` and `rapidcsv` via CMake FetchContent.
     - *Cons:* Significantly increases compile times, binary size, and CMake configure latency; excessive features not needed for flat telemetry streams.
  2. *Option B:* Pure C++23 zero-dependency streaming parser with pre-allocated sample buffers (Recommended).
     - *Pros:* Zero external dependencies, $< 5\text{ ms}$ compile time overhead, zero heap allocation on sample loops, parses $> 200,000\text{ samples/second}$.
- **Decision:** **Option B (Zero-dependency streaming parser)**.
- **Rationale:** Ensures lightweight deployment on edge telematics gateways, IoT devices, and cloud serverless workers.

---

### DEC-9.02: Multi-Dimensional Normalized Formulation for Telemetry Quality Score (TQS)

- **Date:** 2026-10-03
- **Context:**
  Low-quality telemetry with frequent dropouts, severe sampling jitter, or unphysical sensor spikes corrupts electrochemical derivative curves ($dQ/dV$) and Kalman filters. We must compute a calibrated score $\in [0, 100]$ to decide whether telemetry is fit for processing.
- **Decision:**
  Define composite TQS across 4 orthogonal physical dimensions:
  $$\text{TQS} = S_{jitter} + S_{drop} + S_{plaus} + S_{snr} \in [0, 100]$$
  1. **Sampling Jitter ($S_{jitter} \in [0, 25]$):**
     $$S_{jitter} = 25.0 \times \max\left(0.0, 1.0 - 2.0 \times \frac{\sigma_{\Delta t}}{\mu_{\Delta t}}\right)$$
  2. **Packet Completeness ($S_{drop} \in [0, 30]$):**
     $$S_{drop} = 30.0 \times \max\left(0.0, 1.0 - 5.0 \times \frac{T_{missing}}{T_{total}}\right)$$
  3. **Physical Plausibility ($S_{plaus} \in [0, 25]$):**
     $$S_{plaus} = 25.0 \times \frac{N_{plausible}}{N_{total}}$$
     Validates $V \in [30.0, 65.0]\text{ V}$, $|I| \le 250.0\text{ A}$, $T \in [-10.0, 75.0]^\circ\text{C}$.
  4. **High-Frequency SNR ($S_{snr} \in [0, 20]$):**
     $$S_{snr} = 20.0 \times \max\left(0.0, 1.0 - 15.0 \times \sigma_{\Delta^2 V}\right)$$

---

### DEC-9.03: Gate 1 Safety Pre-Qualification & Rejection Threshold

- **Date:** 2026-10-03
- **Context:**
  If telemetry is fundamentally corrupted (e.g. inverted signs, stuck clocks, $> 20\%$ packet drops), downstream cleaning and denoising cannot reconstruct ground truth.
- **Decision:**
  - $\text{TQS} \ge 30.0$: **PASS Gate 1** (Session approved for downstream processing).
  - $\text{TQS} < 30.0$: **FAIL Gate 1** (Pipeline halts immediately, logs diagnostic breakdown, and marks session as `REJECT`).
