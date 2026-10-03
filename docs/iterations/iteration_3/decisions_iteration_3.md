# Iteration 3: Electrochemical Feature Extraction — Decision Log

> **Document ID:** DEC-ITERATION-003  
> **Version:** 1.0  
> **Created:** 2026-10-03  
> **Module ID:** `M06: features`

---

### DEC-3.01: Peak Finding Algorithm & Prominence Thresholding

- **Date:** 2026-10-03
- **Context:**
  In battery Incremental Capacity curves, true phase transition peaks must be distinguished from boundary ripples or minor inflection points. A simple global maximum search fails when multiple electrochemical phases exist (e.g. LFP has multiple distinct phase transition plateaus, and NMC has shoulder peaks). We need an algorithm capable of identifying primary and secondary peaks alongside their intervening valley.

- **Options Considered:**

  1. **Option A: Simple First Difference Zero-Crossing Detection**
     - *Description:* Detect indices where $\Delta y[i] > 0$ and $\Delta y[i+1] < 0$. Filter purely by absolute height threshold.
     - *Pros:* Minimal code complexity; single pass over the array ($O(N)$).
     - *Cons:* Extremely susceptible to small inflection points and local humps; fails when peaks sit on a sloping baseline; does not establish peak dominance or prominence.
     - *Performance:* Very fast (< 0.1 ms).
     - *Complexity:* Low.
     - *Impact on Other Modules:* May output spurious minor peaks as the primary peak, corrupting downstream ML training.

  2. **Option B: Topographic Prominence Extraction with Inter-Peak Valley Analysis (Recommended)**
     - *Description:* Identify all candidate local maxima. For each peak, compute its topographic prominence (how much the peak stands out relative to the lowest contour line encircling it before reaching a higher peak). Filter candidates where $\text{prominence} \ge \text{threshold\_ratio} \cdot \max(y)$. Order peaks by prominence to reliably assign $V_{p1}$ and $V_{p2}$. Locate the minimum between $V_{p1}$ and $V_{p2}$ to define the phase transition valley ($V_{v1}$).
     - *Pros:* Robust against baseline slope and residual noise; separates primary phase transitions from minor shoulders; directly extracts the peak-to-valley ratio and valley voltage.
     - *Cons:* Slightly higher computational overhead than simple zero-crossings ($O(N \cdot K)$ where $K$ is number of peaks, typically $K \le 5$).
     - *Performance:* < 0.2 ms for 500-point curve (well within 5 ms target).
     - *Complexity:* Medium.
     - *Impact on Other Modules:* Provides clean, invariant degradation features for M08 ML inference.

- **Decision:** **Option B (Topographic Prominence Extraction)**.
- **Rationale:**
  Electrochemical literature establishes peak prominence as the most reliable discriminator of phase transitions in degraded cells. It ensures that even as the primary peak broadens and shifts with age, the algorithm consistently tracks the correct physical phase.
- **Consequences:** Configurable prominence ratio allows tuning between noisy commercial 2W/3W BMS logs (ratio ~ 0.15) and pristine laboratory datasets (ratio ~ 0.08).
- **Reversibility:** Easy. Encapsulated entirely in `ev::battery::features::extractIcaFeatures`.

---

### DEC-3.02: Peak Area Integration Boundary Selection

- **Date:** 2026-10-03
- **Context:**
  Peak area $A_{p1}$ represents the charge exchanged during a specific structural phase transition and is an exceptionally strong predictor of Loss of Active Material ($LAM$). The integration boundaries around the peak must be defined deterministically.
- **Options Considered:**
  1. *Valley-to-Valley Integration:* Integrate from the left valley to the right valley. Cons: in degraded cells, valleys can shift unpredictably or disappear into boundaries.
  2. *Fixed Voltage Window around $V_p$ ($\pm 40\text{ mV}$):* Integrate from $V_p - \delta$ to $V_p + \delta$. Pros: completely deterministic; invariant to missing secondary valleys; perfectly captures the core of the phase transition plateau.
- **Decision:** Fixed symmetric window around the detected peak ($V_{p1} \pm 40\text{ mV}$ default, clamped to grid limits).
- **Consequences:** Enables uniform feature vectors across all degradation states.
