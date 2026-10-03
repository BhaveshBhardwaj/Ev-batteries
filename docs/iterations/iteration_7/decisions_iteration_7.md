# Iteration 7: Conformal Prediction & Safety Abstention Engine — Decision Log

> **Document ID:** DEC-ITERATION-007  
> **Version:** 1.0  
> **Created:** 2026-10-03  
> **Module ID:** `M09: uncertainty`  
> **Parent Documentation:** [SRS_iteration_7.md](SRS_iteration_7.md)

---

### DEC-7.01: Distribution-Free Split Conformal Prediction vs. Parametric Gaussian Modeling

- **Date:** 2026-10-03
- **Context:**
  For commercial EV battery underwriting and BPAN Tier 3 certification, presenting a single point estimate ($\widehat{\text{SoH}} = 82\%$) without guaranteed confidence bounds exposes financiers to catastrophic risk. We must decide how to construct mathematically rigorous confidence intervals.

- **Options Considered:**

  1. **Option A: Parametric Gaussian $\pm 2\sigma$ Variance Modeling**
     - *Description:* Assume prediction errors follow a normal distribution $\mathcal{N}(0, \sigma^2)$ and output $\widehat{\text{SoH}} \pm 1.96\sigma$.
     - *Pros:* Trivial to implement.
     - *Cons:* Fundamentally invalid for battery degradation; degradation residuals exhibit heavy tails, skewness, and non-linear knee-point discontinuities; lacks any finite-sample mathematical guarantee; rejected by automotive regulatory auditors.
     - *Complexity:* Low.

  2. **Option B: Distribution-Free Split Conformal Prediction (Recommended)**
     - *Description:* Utilize split conformal prediction on a holdout calibration dataset of empirical residuals. Compute the $(1 - \alpha)$-quantile of non-conformity scores:
       $$\hat{q}_{1-\alpha} = \text{Quantile}\left(R, \left\lceil \frac{(N+1)(1-\alpha)}{N} \right\rceil\right)$$
     - *Pros:* Provides an exact, non-asymptotic finite-sample coverage guarantee ($P(\text{SoH}_{true} \in \mathcal{C}(X)) \ge 1 - \alpha$) regardless of the underlying distribution; mathematically irrefutable in audit and legal proceedings; sub-millisecond execution.
     - *Cons:* Requires a representative holdout calibration set (which we obtain from NASA packs and synthetic fleet simulations).
     - *Complexity:* Moderate.

- **Decision:** **Option B (Split Conformal Prediction)**.
- **Rationale:**
  Aligns with the core architectural principle of auditability and mathematical certainty for fintech and leasing underwriting.

---

### DEC-7.02: Heteroscedastic Local Difficulty Scaling via Operational Covariates

- **Date:** 2026-10-03
- **Context:**
  A uniform, homoscedastic interval width across all vehicles is suboptimal: clean telemetry from a vehicle resting at $25^\circ\text{C}$ with calibrated shunts has far less epistemic uncertainty than a vehicle operating at $48^\circ\text{C}$ in summer with an uncalibrated shunt.
- **Decision:**
  Formulate a physics-grounded local difficulty multiplier:
  $$\sigma_{local} = 1.0 + 0.25 \max(0.0, \text{TDI} - 1.0) + (\text{dual\_anchors\_found} ? 0.0 : 0.35) + (\text{sensor\_fault} ? 0.50 : 0.0)$$
  Scale the conformal quantile by $\sigma_{local}$, yielding narrower, sharper intervals under pristine conditions and safely widening intervals under severe operational stress.

---

### DEC-7.03: Gate 3 Automated Safety Abstention Boundary ($\le 5.0\%$)

- **Date:** 2026-10-03
- **Context:**
  In commercial EV fleet underwriting, when the uncertainty interval is too wide (e.g. $[78\%, 86\%]$), automating risk decisions or residual asset guarantees is financially irresponsible.
- **Decision:**
  Enforce a hard Gate 3 threshold:
  - If $\Delta \text{SoH} = \text{SoH}_{high} - \text{SoH}_{low} \le 5.0\%$ and $\text{OOD} = \text{false}$: issue `CertificationDecision::CERTIFIED`.
  - If $\Delta \text{SoH} > 5.0\%$ or $\text{OOD} = \text{true}$: issue `CertificationDecision::AUDIT_REQUIRED`.
  This acts as an automated safety circuit-breaker, ensuring only high-certainty valuations receive automated certification while borderline or abused packs are routed to depot physical testing.
