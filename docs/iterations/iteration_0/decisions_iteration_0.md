# Iteration 0 — Decision Log

> **Document ID:** DEC-IT0  
> **Version:** 1.0  
> **Created:** 2026-10-03  
> **Parent:** SRS-IT0

---

### DEC-0.01: Package Manager — vcpkg vs. Conan

- **Date:** 2026-10-03
- **Context:** We need a C++ package manager to handle Eigen3, nlohmann-json, Google Test, Google Benchmark, spdlog, and fmt. The choice affects every future iteration.
- **Options Considered:**
  1. **vcpkg** — Microsoft-backed, manifest-mode (`vcpkg.json`), excellent Windows support, integrates via CMake toolchain file.
  2. **Conan 2.x** — JFrog-backed, `conanfile.py`/`conanfile.txt`, strong Linux support, more flexible binary management, profiles for cross-compilation.
  3. **FetchContent (CMake-only)** — No external tool; download dependencies at configure time via `FetchContent_Declare`.
- **Decision:** **vcpkg (manifest mode)**
- **Rationale:** 
  - Primary development is on Windows (user's OS). vcpkg has first-class Windows/MSVC support.
  - Manifest mode (`vcpkg.json`) keeps dependency declarations in the repo, similar to `package.json`.
  - CMake toolchain integration is a single `-DCMAKE_TOOLCHAIN_FILE=` flag.
  - All required libraries (Eigen3, gtest, benchmark, spdlog, fmt, nlohmann-json) are available in vcpkg.
  - Conan 2.x has a steeper learning curve and its profile system adds complexity we don't need yet.
  - FetchContent works but is fragile for large dependency trees and doesn't cache binaries.
- **Consequences:** Must install vcpkg and set `VCPKG_ROOT` environment variable. CI must clone vcpkg. Linux/macOS builds use the same toolchain file.
- **Reversibility:** Medium. Switching to Conan later requires rewriting dependency declarations but not application code.

---

### DEC-0.02: C++23 `std::expected` vs. `tl::expected`

- **Date:** 2026-10-03
- **Context:** The SRS specifies using `std::expected<T, E>` for error handling. C++23 introduced `std::expected`, but compiler support may be incomplete.
- **Options Considered:**
  1. **C++23 `std::expected`** — Standard library, no extra dependency. Requires MSVC 17.7+ (VS 2022 17.7), GCC 12+, Clang 16+.
  2. **`tl::expected`** — Header-only polyfill by Sy Brand. Works with C++17. Widely used.
  3. **Custom `Result<T>` type** — Write our own. Full control.
- **Decision:** **C++23 `std::expected`** with `tl::expected` as a conditional fallback.
- **Rationale:**
  - MSVC 2022 (current user's compiler) supports `std::expected` since version 17.7 (mid-2023). We are in October 2026 — this is well-established.
  - GCC 13 and Clang 16 also support it. Our CI targets are recent enough.
  - Using the standard avoids an extra dependency.
  - We add a `#if __cpp_lib_expected` check in a compatibility header and fall back to `tl::expected` if needed.
- **Consequences:** We require C++23 mode (`/std:c++latest` on MSVC, `-std=c++23` on GCC/Clang) or at minimum C++20 with the expected header available. This is reasonable for our target compilers.
- **Reversibility:** Easy. `tl::expected` has an almost identical API.

---

### DEC-0.03: Savitzky-Golay Coefficient Computation Method

- **Date:** 2026-10-03
- **Context:** Savitzky-Golay coefficients are computed by solving a least-squares system (Vandermonde matrix). The method affects numerical stability for large windows.
- **Options Considered:**
  1. **Direct Vandermonde inversion** — Construct the Vandermonde matrix J, compute (JᵀJ)⁻¹Jᵀ, extract the appropriate row. Simple but numerically unstable for large window sizes.
  2. **QR decomposition** — Decompose J = QR, then coefficients are a row of R⁻¹Qᵀ. More stable.
  3. **Precomputed tables** — Store coefficients for common window sizes and orders. Fast lookup, but inflexible.
  4. **Gram polynomial recursion (Gorry 1990)** — Use orthogonal polynomials to compute coefficients via a stable recursion. No matrix inversion needed.
- **Decision:** **QR decomposition via Eigen** (Option 2), with Option 4 as a future optimization.
- **Rationale:**
  - QR decomposition is numerically stable for all window sizes we'll use (≤ 51).
  - Eigen provides `householderQr()` which is fast and well-tested.
  - Coefficient computation is done once per pipeline configuration (not per sample), so speed is not critical here.
  - Gorry's recursion is elegant but harder to implement correctly and verify. We can switch later if needed.
- **Consequences:** Dependency on Eigen for coefficient computation (which we already have for other modules). Window sizes > ~51 may still have issues; we cap at 51.
- **Reversibility:** Easy. The coefficient computation is internal to the savgolCoefficients function; changing the method doesn't affect the API.

---

### DEC-0.04: Test Data Strategy for Math Utilities

- **Date:** 2026-10-03
- **Context:** Math utility tests need reference data. Options range from hardcoded values to generated data with known analytic solutions.
- **Options Considered:**
  1. **Analytic test functions** — Use sin, cos, polynomials where derivatives and integrals are known exactly. Generate data in the test.
  2. **Hardcoded reference vectors** — Store input/output pairs computed by NumPy/SciPy. Compare C++ output.
  3. **Cross-validation script** — A Python script generates test cases and writes them to JSON; C++ tests load and compare.
- **Decision:** **Option 1 (analytic functions) as primary, Option 2 for edge cases.**
- **Rationale:**
  - Analytic functions are self-contained, require no external files, and make the mathematical properties being tested explicit.
  - For edge cases (e.g., Savitzky-Golay on specific window/order combinations), hardcoded reference values from NumPy ensure we match the established implementation.
  - Cross-validation scripts add complexity and a Python dependency to the test suite.
- **Consequences:** Tests are self-contained. Adding new reference values requires manual computation or a one-time NumPy run.
- **Reversibility:** Easy. Can add cross-validation later without changing existing tests.

---

### DEC-0.05: Monotone Interpolation Method — Steffen vs. Fritsch-Carlson

- **Date:** 2026-10-03
- **Context:** ICA requires interpolation that preserves monotonicity of the Q(V) curve. Standard cubic splines can overshoot and create spurious oscillations.
- **Options Considered:**
  1. **Steffen's method (1990)** — Guarantees monotonicity by constraining tangent slopes. Simple, local (each interval depends only on neighboring points).
  2. **Fritsch-Carlson (1980)** — Also guarantees monotonicity. Slightly more complex; uses a different tangent modification rule.
  3. **PCHIP (Piecewise Cubic Hermite Interpolating Polynomial)** — MATLAB's implementation, essentially Fritsch-Carlson with refinements.
- **Decision:** **Steffen's method.**
- **Rationale:**
  - Both Steffen and Fritsch-Carlson guarantee monotonicity. Steffen is slightly simpler to implement (fewer conditional branches).
  - Steffen produces smoother curves in general and has better behavior at data boundaries.
  - The difference in output quality is negligible for our use case (smooth battery voltage curves).
- **Consequences:** None significant. Both methods produce nearly identical output on monotone data.
- **Reversibility:** Easy. Both methods have the same input/output interface.

---

### DEC-0.06: Namespace Structure

- **Date:** 2026-10-03
- **Context:** Need a consistent namespace hierarchy across 15 modules.
- **Options Considered:**
  1. **Flat:** `ev::battery::savgolFilter()` — simple but risks name collisions as modules grow.
  2. **Module-level:** `ev::battery::math_utils::savgolFilter()` — clear module boundaries.
  3. **Deep:** `ev::battery::math::filter::savgol::apply()` — too verbose.
- **Decision:** **Module-level namespaces** (Option 2): `ev::battery::<module_name>`.
- **Rationale:** Clear module boundaries without excessive nesting. Consistent with the directory structure (`include/ev/battery/<module>/`).
- **Consequences:** All code follows `ev::battery::<module>` pattern. Common types are in `ev::battery::common`.
- **Reversibility:** Medium. Renaming namespaces across 15 modules is tedious but mechanical.

---

## Summary

| Decision | Choice | Key Reason |
|----------|--------|------------|
| Package manager | vcpkg | Best Windows support, manifest mode |
| Error handling | C++23 std::expected | Standard, widely supported now |
| SavGol coefficients | QR decomposition (Eigen) | Numerically stable, simple |
| Test data | Analytic functions + hardcoded refs | Self-contained, mathematically rigorous |
| Monotone interp | Steffen's method | Simple, guaranteed monotonicity |
| Namespaces | ev::battery::\<module\> | Clear boundaries, matches directory structure |
