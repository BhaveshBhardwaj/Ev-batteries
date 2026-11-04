# Iteration 11: Decisions Log

> **Iteration:** 11  
> **Period:** 2026-11-04  
> **Scope:** Real-World Digital Twin, Dynamic Kinematics & Analytical Inspector Console  

---

### DEC-11.01: Metric Tile Grid Architecture vs. Single-Line Flex Rows

- **Date:** 2026-11-04
- **Context:** Squeezed single-line `display: flex; justify-content: space-between` layout in `.sim-docked-console` caused long labels and values (such as mass breakdowns and conformal intervals) to overflow horizontally and clip off screen on standard desktop viewports (e.g., 1366x768).
- **Options Considered:**
  1. *Option A (Truncate text with ellipsis):* Fast fix, but hides essential numbers and physical breakdowns from the user.
  2. *Option B (Single-column vertical stack):* Prevents horizontal clipping, but forces extensive vertical scrolling in a 520px container.
  3. *Option C (2-Column Metric Tile Grid with Dedicated Subtexts):* Each tile contains a top uppercase title, centered high-contrast colored numeric value, and descriptive subtext. Structured into a 2-column CSS grid.
- **Decision:** Selected Option C.
- **Rationale:** Guarantees 100% visibility of all numbers, units, and subtexts across any panel width ($280\text{px} - 650\text{px}$) with zero horizontal clipping.
- **Consequences:** Clean, modern, cockpit-style engineering telemetry layout.
- **Reversibility:** Easy.

---

### DEC-11.02: Multi-Deck Tab Navigation in Analytical Inspector

- **Date:** 2026-11-04
- **Context:** Stacking four full-sized inspection cards (Dynamics, Diagnostics, SOH Valuation, and Passport) vertically exceeded the 520px height of the 3D viewport, pushing the lower cards out of immediate view.
- **Options Considered:**
  1. *Option A (Unbounded vertical scrolling):* Keeps all content in a single long scrollable column.
  2. *Option B (Deck Tabs with Unified Summary):* Adds sticky sub-tabs (`[ 📊 All Summary ]`, `[ 🌪️ Real Dynamics ]`, `[ 🛡️ Gate 1 & 2 ]`, `[ ⚡ SOH Valuation ]`, `[ 🏛️ BPAN Passport ]`). The "All Summary" tab displays the top 8 essential KPI tiles fitting directly within 520px without vertical scrolling.
- **Decision:** Selected Option B.
- **Rationale:** Allows instantaneous overview of all primary indicators simultaneously, while offering one-click access to deep-dive decomposition without clutter.
- **Consequences:** Highly modular and responsive UI that scales gracefully to smaller displays.
- **Reversibility:** Easy.

---

### DEC-11.03: Coupling Gravitational Incline and Auxiliary Power Loads

- **Date:** 2026-11-04
- **Context:** User requested exact real-world environmental fidelity including turns, road conditions, and power factors. Real Indian commercial EVs encounter steep flyovers and run continuous cabin AC / BMS fans in high summer temperatures.
- **Options Considered:**
  1. *Option A (Visual 3D terrain tilt only):* Aesthetic changes without affecting battery current.
  2. *Option B (Full Electrochemical Traction Coupling):* Compute gravitational force $F_{\text{grade}} = M g \sin(\theta)$ and continuous auxiliary power $P_{\text{aux}}$, feeding directly into powertrain mechanical-to-electrical energy conversion.
- **Decision:** Selected Option B.
- **Rationale:** Ensures that battery telemetry sent to the C++ core contains physically authentic thermal and current stress profiles matching real-world Indian operational realities.
- **Consequences:** Regenerative braking recharges the battery downhill (-5%), while uphill climbs (+4% to +8%) and high AC loads accurately accelerate pack heating and battery depletion.
- **Reversibility:** Medium.

