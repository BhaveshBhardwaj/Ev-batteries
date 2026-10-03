# Iteration 5: Current-Sensor Self-Calibration — Decision Log

> **Document ID:** DEC-ITERATION-005  
> **Version:** 1.0  
> **Created:** 2026-10-03  
> **Module ID:** `M07: calibration`  
> **Parent Documentation:** [SRS_iteration_5.md](SRS_iteration_5.md)

---

### DEC-5.01: Electrochemical Rest-Anchor Zero-Current Offset Regression

- **Date:** 2026-10-03
- **Context:**
  In low-cost Indian 2W/3W commercial vehicles, BMS current measurement typically relies on uncalibrated low-side resistive shunts or open-loop Hall sensors with low-cost analog front-ends (AFEs). These sensors exhibit operational DC offset drift ($\pm 0.2\text{ A}$ to $\pm 1.0\text{ A}$) driven by temperature swings (up to $50^\circ\text{C}$ in Indian summers) and operational aging. We need to isolate and regress this offset $\hat{\alpha}$.

- **Options Considered:**

  1. **Option A: Dynamic Joint Parameter Estimation (Recursive Least Squares / Extended Kalman Filter during driving)**
     - *Description:* Simultaneously estimate battery state of charge ($z$), internal resistance ($R_0$), polarization overpotentials ($V_{rc}$), and sensor bias ($\alpha$) from dynamic drive cycles.
     - *Pros:* Updates continuously while driving.
     - *Cons:* Severe parameter unobservability and cross-coupling between $R_0$ variation, $V_{rc}$ dynamics, and current bias $\alpha$; prone to divergence on short city trips; high computational overhead for embedded/cloud microservices.
     - *Performance:* $O(N)$ with matrix inversions.
     - *Complexity:* High.

  2. **Option B: Electrochemical Rest-Interval Zero-Current Bias Regression (Recommended)**
     - *Description:* Exploit vehicle key-off, staging, and dwell intervals where true current is physically constrained to zero ($I_{true} = 0$). Compute the mean measured current across all identified resting samples:
       $$\hat{\alpha} = \frac{1}{N_{rest}} \sum_{k \in \text{rest}} I_k$$
     - *Pros:* Direct physical isolation of sensor offset with zero electro-chemical model cross-coupling; averaging over hundreds of rest samples attenuates Gaussian measurement noise by $\frac{1}{\sqrt{N}}$ (e.g., $1000$ samples reduces $0.15\text{ A}$ noise to $< 5\text{ mA}$ standard error); allocation-free, rock-solid numerical stability.
     - *Cons:* Requires at least one valid rest period ($\ge 30\text{ s}$).
     - *Performance:* $O(N)$ single pass, sub-millisecond execution.
     - *Complexity:* Low.

- **Decision:** **Option B (Rest-Anchor Offset Regression)**.
- **Rationale:**
  Aligns with the core architectural philosophy of deterministic physics isolation. In real Indian commercial fleets, every shift includes staging, waiting for cargo, key-off pauses, and depot dwell, providing clean zero-current ground truth.

---

### DEC-5.02: Dual OCV Anchor Matching & Tail Relaxation for Gain Factor ($\hat{\beta}$) Regression

- **Date:** 2026-10-03
- **Context:**
  Shunt resistor thermal expansion, solder joint degradation, and ADC reference voltage drift introduce scaling gain errors ($\beta \neq 1.0$). To estimate $\beta$, we must compare measured throughput against true electrochemical charge transfer without requiring laboratory calibration benches.

- **Options Considered:**

  1. **Option A: Full-Session Trapezoidal Coulomb Ratio against Nameplate Capacity**
     - *Description:* Assume a full cycle delivers exactly $100\%$ nameplate capacity $Q_{nom}$.
     - *Pros:* Simple.
     - *Cons:* Conflates battery aging (loss of capacity, $SOH < 100\%$) with current sensor gain error! A degraded $80\%$ SOH battery would be falsely diagnosed as a $0.80$ sensor gain error!

  2. **Option B: Dual Equilibrium OCV Anchor Matching with Tail Sampling (Recommended)**
     - *Description:* Locate two resting intervals separated by active charging or driving with $|\Delta \text{SoC}| \ge 8\%$.
       1. Sample the equilibrium terminal voltage at the settled tail of each rest period (e.g. final samples after capacitive/RC decay: $V(t) \to \text{OCV}$).
       2. Invert the reference OCV curve via $O(\log M)$ binary search: $\text{SoC}_1 = \text{OCV}^{-1}(V_1)$ and $\text{SoC}_2 = \text{OCV}^{-1}(V_2)$.
       3. Theoretical delivered charge: $\Delta Q_{true} = Q_{nom} \cdot (\text{SoC}_2 - \text{SoC}_1)$.
       4. Measured charge corrected for regressed offset: $Q_{net} = \int I_{meas}\mathrm{d}t - \hat{\alpha}\Delta t$.
       5. Regress sensor gain:
          $$\hat{\beta} = \frac{Q_{net}}{\Delta Q_{true}}$$
       6. When multiple rest segments exist, select the anchor pair maximizing $|\Delta \text{SoC}|$ to maximize signal-to-noise ratio.
     - *Pros:* Fully decouples sensor gain error from battery capacity degradation; immune to short-term hysteresis; tail-sampling rejects transient overpotential $V_{rc}$.
     - *Cons:* Requires a minimum throughput threshold ($|\Delta \text{SoC}| \ge 8\%$) to prevent division by small numbers.
     - *Performance:* $O(N + M \log K)$ where $N$ is telemetry size, $M$ is rest pair count, $K$ is OCV table size (< 2 ms for 10k samples).
     - *Complexity:* Moderate.

- **Decision:** **Option B (Dual OCV Anchor Matching)**.
- **Rationale:**
  Provides mathematical certainty and strictly prevents the disastrous conflation of sensor drift with battery capacity degradation.

---

### DEC-5.03: Gate 2 Sensor Diagnostic Fault Thresholds

- **Date:** 2026-10-03
- **Context:**
  BPAN Tier 3 certification and automotive functional safety require unambiguous identification of failing or out-of-spec current sensors before bad data corrupts state-of-health valuation.
- **Decision:**
  Enforce two hard physical thresholds in `CalibrationReport`:
  - **Offset Fault:** $|\hat{\alpha}| > 1.50\text{ A}$ triggers `ALERT_SENSOR_OFFSET_EXCESSIVE`. Standard automotive shunts for 2W/3W have maximum offset limits of $\pm 0.5\text{ A}$ to $\pm 1.0\text{ A}$. An offset exceeding $1.5\text{ A}$ indicates catastrophic operational bias or hardware failure.
  - **Gain Fault:** $|\hat{\beta} - 1.0| > 0.08$ triggers `ALERT_SENSOR_GAIN_EXCESSIVE`. An $8\%$ gain error indicates shunt resistor micro-cracking, severe thermal degradation, or ADC reference voltage drift.
  - When either fault is flagged, `sensor_fault_detected = true` and `fault_message` is populated for operator inspection.
