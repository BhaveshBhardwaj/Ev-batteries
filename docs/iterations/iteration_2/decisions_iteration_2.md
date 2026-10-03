# Iteration 2: Incremental Capacity Analysis (ICA/DVA) — Decision Log

> **Document ID:** DEC-ITERATION-002  
> **Version:** 1.0  
> **Created:** 2026-10-03  
> **Module ID:** `M05: ica_dva`

---

### DEC-2.01: Mathematical Strategy for Noise-Tolerant dQ/dV Differentiation

- **Date:** 2026-10-03
- **Context:**
  Raw battery telemetry from low-cost BMS contains voltage quantization noise (typically 1–5 mV steps) and sampling jitter. Computing the numerical derivative $\frac{\mathrm{d}Q}{\mathrm{d}V} = \frac{\Delta Q}{\Delta V}$ directly on raw data causes severe noise amplification (division by tiny $\Delta V \approx 0$), producing uninterpretable chaotic spikes that obscure the true electrochemical phase transition peaks. We need a robust mathematical approach to extract smooth, physically meaningful ICA peaks.

- **Options Considered:**

  1. **Option A: Direct Finite Difference with Heavy Pre-Smoothing (Moving Average / Gaussian)**
     - *Description:* Smooth the raw $V(t)$ signal aggressively with a wide Gaussian or moving average filter, then compute simple central difference $\frac{\Delta Q}{\Delta V}$.
     - *Pros:* Extremely simple to implement; low computational overhead ($O(N)$).
     - *Cons:* Heavy pre-smoothing flattens and distorts the true peak amplitude and shifts peak locations; fails to resolve non-uniform $\Delta V$ spacing; does not guarantee monotonicity of $V(t)$.
     - *Performance:* Very fast (< 1 ms), but poor scientific fidelity.
     - *Complexity:* Low.
     - *Impact on Other Modules:* Downstream feature extraction (M06) will receive inaccurate peak locations and damped peak heights.

  2. **Option B: Uniform Voltage Resampling via Steffen Spline + Savitzky-Golay QR Derivative (Recommended)**
     - *Description:* Invert the signal by generating a strictly uniform voltage grid ($V_{grid}$ with fixed $\Delta V$, e.g. 5 mV). Resample cumulative charge $Q$ onto $V_{grid}$ using Steffen's monotone cubic interpolation (which mathematically guarantees monotonicity and completely prevents Runge-type oscillations). Then, apply a calibrated Savitzky-Golay derivative filter directly on the uniform grid using physical step scaling: $\left(\frac{\mathrm{d}Q}{\mathrm{d}V}\right)_k = \frac{1}{\Delta V} \sum c_j Q_{k+j}$.
     - *Pros:* Fully eliminates division-by-zero or small $\Delta V$ noise; Steffen spline preserves exact monotonicity ($Q[i+1] \ge Q[i]$); Savitzky-Golay polynomial fitting preserves peak heights and shapes without attenuation; deterministic, zero-allocation runtime.
     - *Cons:* Requires monotonically increasing voltage segment; requires two passes (resample + SG derivative).
     - *Performance:* ~2–5 ms for 3600 samples onto a 5 mV grid (well within the 15 ms target).
     - *Complexity:* Medium.
     - *Impact on Other Modules:* Provides clean, standard-format $dQ/dV(V)$ vectors with known grid resolution for M06 peak tracking.

  3. **Option C: Gaussian Process Regression (GPR) Analytic Derivative**
     - *Description:* Fit a Gaussian Process with Matérn or RBF kernel to $(V, Q)$ pairs, and compute the analytic derivative of the GP mean function.
     - *Pros:* Provides built-in variance/uncertainty bounds on the derivative; naturally handles arbitrary non-uniform gaps.
     - *Cons:* $O(N^3)$ computational complexity for kernel inversion; requires Eigen matrix solver or Cholesky decomposition; too slow for real-time edge processing (hundreds of ms to seconds).
     - *Performance:* Slow (100–500 ms for $N \approx 500$).
     - *Complexity:* High.
     - *Impact on Other Modules:* Too heavy for the hot path; best reserved for offline partial-charge reconstruction.

- **Decision:** **Option B (Steffen Monotone Resampling + Savitzky-Golay Derivative)**.
- **Rationale:**
  Option B delivers laboratory-grade electrochemical derivative accuracy without the heavy computational burden of GPR or the peak-damping flaws of simple pre-smoothing. Our `math_utils` kernel already contains pre-tested, zero-allocation implementations of `monotoneCubicInterp` and `savgolDerivative`, allowing seamless modular integration.
- **Consequences:**
  The input CC charging segment must be pre-sanitized to ensure that starting and ending voltages encompass a valid span ($\ge 0.20\text{ V}$). If the battery has severe voltage fluctuations, `M04 Denoising` ensures the input is sufficiently smooth before grid resampling.
- **Reversibility:** Easy. The algorithm is isolated within `ev::battery::ica_dva::computeIca` behind a clean functional interface.

---

### DEC-2.02: Grid Spacing Parameter Standard

- **Date:** 2026-10-03
- **Context:**
  Selecting the uniform voltage grid step $\Delta V$ directly governs the trade-off between peak resolution and noise sensitivity.
- **Options Considered:**
  1. $\Delta V = 1\text{ mV}$: Very high resolution, but retains residual quantization noise from 12-bit ADCs.
  2. $\Delta V = 5\text{ mV}$ (Default): Ideal match for BMS ADC resolutions (12-bit across 0-5V $\approx 1.22\text{ mV}$/LSB); provides ~160 points over a 3.4V–4.2V sweep; completely dampens single-bit toggling.
  3. $\Delta V = 10\text{ mV}$: Too coarse for sharp LFP phase transitions (which can be as narrow as 20–30 mV).
- **Decision:** Default to $\Delta V = 5\text{ mV}$ (configurable via `IcaConfig::voltage_step_v`), with Savitzky-Golay window $W = 11$ and polynomial degree $P = 2$.
- **Consequences:** Standardizes feature vector dimensions for subsequent ML training in M08.
