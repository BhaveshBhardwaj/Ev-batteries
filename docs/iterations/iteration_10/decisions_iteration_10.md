# Iteration 10: Gaussian Process Regression (GPR) Curve Reconstruction — Decision Log

> **Document ID:** DEC-ITERATION-010  
> **Version:** 1.0  
> **Created:** 2026-10-03  
> **Module ID:** `M10: gpr_completion`  
> **Parent Documentation:** [SRS_iteration_10.md](SRS_iteration_10.md)

---

### DEC-10.01: Kernel Choice: Squared Exponential (RBF) with Jitter Regularization

- **Date:** 2026-10-03
- **Context:**
  Electrochemical battery charging curves $Q(V)$ are smooth and monotonic, but partial opportunity charging leaves gaping missing intervals. We need a covariance kernel that guarantees infinitely differentiable curves while preventing matrix singularization on LFP voltage plateaus where $dV \approx 0$.
- **Decision:**
  Use the Radial Basis Function (Squared Exponential) kernel with diagonal noise jitter:
  $$k(V_i, V_j) = \sigma_f^2 \exp\left(-\frac{(V_i - V_j)^2}{2 \ell^2}\right) + \sigma_n^2 \delta_{ij}$$
  where $\ell \approx 0.05\text{ V}$ (50 mV length scale), $\sigma_f^2 = 1.0$, and $\sigma_n^2 = 10^{-4}$.
- **Rationale:**
  Provides smooth, physically plausible electrochemical curve interpolation while preventing ill-conditioned Cholesky factorization on dense flat plateaus.

---

### DEC-10.02: Eigen LLT Decomposition for Sub-Millisecond Inference

- **Date:** 2026-10-03
- **Context:**
  Standard GPR scales with $\mathcal{O}(N^3)$ where $N$ is the number of training samples. In a 3,600-sample session, inverting a $3600 \times 3600$ matrix would take seconds and allocate hundreds of megabytes.
- **Decision:**
  Downsample the observed $(V, Q)$ pairs uniformly across the observed voltage span to at most $N \le 150$ support anchors.
  Solve $\mathbf{\alpha} = (\mathbf{K} + \sigma_n^2 \mathbf{I})^{-1} \mathbf{y}$ using `Eigen::LLT` (Cholesky decomposition $\mathbf{L} \mathbf{L}^T$).
- **Rationale:**
  $150 \times 150$ Cholesky decomposition executes in $< 0.4\text{ ms}$ on modern x86/ARM CPUs, delivering sub-millisecond real-time execution with zero loss in electrochemical resolution.
