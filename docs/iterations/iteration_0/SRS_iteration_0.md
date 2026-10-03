# Iteration 0 — SRS: Project Scaffolding and Math Utilities

> **Document ID:** SRS-IT0  
> **Version:** 1.0  
> **Created:** 2026-10-03  
> **Parent:** SRS-000 (Initial SRS)  
> **Status:** Active  
> **Modules Delivered:** M15 (math_utils)

---

## 1. Scope

### 1.1 In Scope

- Project directory structure creation (as defined in SRS-000 §4.3).
- Top-level CMake build system with vcpkg integration.
- M15 (`math_utils`) module: foundational numerical routines.
- Google Test and Google Benchmark integration.
- CI configuration (.github/workflows or local build script).
- Placeholder CMakeLists.txt for all 15 modules (empty, but compilable).
- Common data types (`Sample`, `InternalTimeSeries`, error types).

### 1.2 Out of Scope

- All other modules (M01–M14). Only directory stubs are created.
- REST API, ML model inference, dataset download.
- Any signal processing beyond basic math utilities.

---

## 2. Functional Requirements

### 2.1 Build System

| ID | Requirement | Acceptance Criteria |
|----|------------|---------------------|
| FR-0.01 | The project SHALL build with `cmake --preset default` followed by `cmake --build build`. | Clean build with zero errors on Windows (MSVC) and Linux (GCC). |
| FR-0.02 | The project SHALL integrate vcpkg for dependency management. | `vcpkg.json` lists Eigen3, nlohmann-json, gtest, benchmark, spdlog, fmt. All install successfully. |
| FR-0.03 | The project SHALL produce build targets: `ev_battery_core`, `ev_battery_tests`, `ev_battery_bench`. | All three targets compile and link. |

### 2.2 Common Types

| ID | Requirement | Acceptance Criteria |
|----|------------|---------------------|
| FR-0.04 | The `ev::battery::common` namespace SHALL define `Sample` and `InternalTimeSeries` structs. | Structs compile and are usable from all modules. |
| FR-0.05 | The `ev::battery::common` namespace SHALL define error types using `std::expected` or `tl::expected`. | Error handling compiles and works in unit tests. |

### 2.3 M15: Math Utilities

| ID | Requirement | Acceptance Criteria |
|----|------------|---------------------|
| FR-0.06 | `math_utils` SHALL provide Savitzky-Golay filter coefficient computation for arbitrary window size and polynomial order. | Coefficients match published tables (Gorry 1990) to within 1e-12. |
| FR-0.07 | `math_utils` SHALL provide 1D convolution with Savitzky-Golay coefficients. | Convolution of a known polynomial reproduces the polynomial exactly (within floating-point error). |
| FR-0.08 | `math_utils` SHALL provide numerical differentiation via finite differences (forward, backward, central). | Derivative of sin(x) matches cos(x) within O(h²) for central differences. |
| FR-0.09 | `math_utils` SHALL provide linear interpolation and cubic spline interpolation. | Interpolation of a cubic polynomial is exact to within 1e-10. |
| FR-0.10 | `math_utils` SHALL provide basic statistics: mean, standard deviation, median, MAD, percentiles. | All match NumPy reference values on a test vector. |
| FR-0.11 | `math_utils` SHALL provide trapezoidal numerical integration. | Integral of sin(x) from 0 to π equals 2.0 within O(h²). |
| FR-0.12 | `math_utils` SHALL provide a monotonic cubic spline (Fritsch-Carlson or Steffen). | Spline preserves monotonicity on monotone input data. |

---

## 3. Non-Functional Requirements

| ID | Requirement | Acceptance Criteria |
|----|------------|---------------------|
| NFR-0.01 | Savitzky-Golay convolution of 10,000 points SHALL complete in < 0.1 ms. | Google Benchmark result on reference hardware. |
| NFR-0.02 | All math functions SHALL operate on `std::span<const double>` without heap allocation. | No `new`/`malloc` calls in hot paths (verified by code review). |
| NFR-0.03 | The build SHALL compile with `-Wall -Wextra -Wpedantic` (GCC/Clang) or `/W4` (MSVC) with zero warnings. | CI build log shows zero warnings. |

---

## 4. Interfaces

### 4.1 Common Types Header

```cpp
// include/ev/battery/common/types.hpp
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <expected>

namespace ev::battery::common {

/**
 * @brief A single telemetry sample.
 */
struct Sample {
    double timestamp_s;          ///< Unix timestamp (seconds)
    double voltage_v;            ///< Terminal voltage (V)
    double current_a;            ///< Current (A), positive = charge
    double temperature_c;        ///< Temperature (°C)
    std::optional<std::vector<double>> cell_voltages_v; ///< Per-cell voltages (optional)
};

/**
 * @brief Normalized internal time series representation.
 */
struct InternalTimeSeries {
    std::string session_id;
    std::string pack_id;
    std::string chemistry;                                  ///< "LFP", "NMC", "UNKNOWN"
    double nominal_capacity_ah = 0.0;                       ///< Rated capacity (Ah)
    std::vector<Sample> samples;
    std::unordered_map<std::string, std::string> metadata;  ///< Arbitrary key-value metadata
};

/**
 * @brief Segment type classification.
 */
enum class SegmentType : uint8_t {
    CHARGE,
    DISCHARGE,
    REST,
    UNKNOWN
};

/**
 * @brief Quality classification for telemetry data.
 */
enum class QualityLabel : uint8_t {
    EXCELLENT,    ///< Score >= 80
    ACCEPTABLE,   ///< Score 50-79
    MARGINAL,     ///< Score 30-49
    REJECT        ///< Score < 30
};

/**
 * @brief Standard error type for the pipeline.
 */
struct PipelineError {
    std::string module;       ///< Module that generated the error
    std::string code;         ///< Machine-readable error code
    std::string message;      ///< Human-readable description
    std::string context;      ///< Additional context (input ID, etc.)
};

/**
 * @brief Result type alias using std::expected.
 */
template <typename T>
using Result = std::expected<T, PipelineError>;

} // namespace ev::battery::common
```

### 4.2 Math Utilities Header

```cpp
// include/ev/battery/math_utils/math_utils.hpp
#pragma once

#include <span>
#include <vector>
#include <cstddef>

namespace ev::battery::math_utils {

// ─── Savitzky-Golay ──────────────────────────────────────────────

/**
 * @brief Compute Savitzky-Golay smoothing coefficients.
 * @param window_size Odd integer >= 3.
 * @param poly_order  Polynomial order < window_size.
 * @param deriv_order Derivative order (0 = smooth, 1 = first derivative, ...).
 * @return Vector of coefficients (length = window_size).
 */
std::vector<double> savgolCoefficients(int window_size, int poly_order, int deriv_order = 0);

/**
 * @brief Apply Savitzky-Golay filter to data in-place (or to output buffer).
 * @param input  Input data.
 * @param output Output buffer (must be same size as input).
 * @param window_size Odd integer >= 3.
 * @param poly_order  Polynomial order.
 */
void savgolFilter(std::span<const double> input, std::span<double> output,
                  int window_size, int poly_order);

// ─── Differentiation ─────────────────────────────────────────────

/**
 * @brief Central finite difference derivative.
 * @param x Independent variable (uniformly or non-uniformly spaced).
 * @param y Dependent variable.
 * @param dy_dx Output derivative (same size as x).
 */
void centralDifference(std::span<const double> x, std::span<const double> y,
                       std::span<double> dy_dx);

// ─── Integration ─────────────────────────────────────────────────

/**
 * @brief Cumulative trapezoidal integration.
 * @param x Independent variable.
 * @param y Integrand.
 * @param integral Output cumulative integral (same size as x; first element = 0).
 */
void cumulativeTrapz(std::span<const double> x, std::span<const double> y,
                     std::span<double> integral);

/**
 * @brief Total trapezoidal integral.
 */
double trapz(std::span<const double> x, std::span<const double> y);

// ─── Interpolation ───────────────────────────────────────────────

/**
 * @brief Linear interpolation at query points.
 * @param x_data Known x values (sorted, ascending).
 * @param y_data Known y values.
 * @param x_query Query x values.
 * @param y_query Output interpolated y values.
 */
void linearInterp(std::span<const double> x_data, std::span<const double> y_data,
                  std::span<const double> x_query, std::span<double> y_query);

/**
 * @brief Monotone cubic spline interpolation (Steffen method).
 */
void monotoneCubicInterp(std::span<const double> x_data, std::span<const double> y_data,
                         std::span<const double> x_query, std::span<double> y_query);

// ─── Statistics ──────────────────────────────────────────────────

double mean(std::span<const double> data);
double standardDeviation(std::span<const double> data);
double median(std::span<const double> data);   // note: may need a mutable copy
double medianAbsoluteDeviation(std::span<const double> data);
double percentile(std::span<const double> data, double p);  // p in [0, 100]

// ─── Convolution ─────────────────────────────────────────────────

/**
 * @brief 1D convolution (valid mode).
 * @param signal Input signal.
 * @param kernel Convolution kernel.
 * @param output Output (size = signal.size() - kernel.size() + 1).
 */
void convolve1d(std::span<const double> signal, std::span<const double> kernel,
                std::span<double> output);

// ─── Moving Window ───────────────────────────────────────────────

void movingAverage(std::span<const double> input, std::span<double> output, int window_size);
void movingMedian(std::span<const double> input, std::span<double> output, int window_size);

} // namespace ev::battery::math_utils
```

---

## 5. Design Decisions

See `decisions_iteration_0.md` for the full decision log.

**Key decisions for this iteration:**
1. Package manager: vcpkg vs. Conan.
2. std::expected availability (C++23 vs. tl::expected backport).
3. Savitzky-Golay coefficient computation method.
4. Test data strategy for math utilities.

---

## 6. Test Plan

| Test Category | What is Tested | Data Source |
|--------------|----------------|-------------|
| Unit: savgolCoefficients | Coefficient correctness | Published tables (Gorry 1990) |
| Unit: savgolFilter | Smoothing preserves polynomial of degree ≤ order | Synthetic polynomial data |
| Unit: centralDifference | Derivative accuracy | sin(x) → cos(x) |
| Unit: cumulativeTrapz | Integration accuracy | sin(x) → -cos(x) + 1 |
| Unit: linearInterp | Interpolation accuracy | Linear function (exact) |
| Unit: monotoneCubicInterp | Monotonicity preservation | Monotone step-like data |
| Unit: statistics | Mean, std, median, MAD | Known vectors with hand-computed values |
| Benchmark: savgolFilter | Performance on 10K, 100K, 1M points | Random data |
| Benchmark: cumulativeTrapz | Performance on 10K, 100K, 1M points | Random data |

---

## 7. Risk Register (Iteration 0)

| ID | Risk | Mitigation |
|----|------|------------|
| R-0.01 | vcpkg fails to install Eigen or gtest on Windows | Test in CI immediately; have Conan as fallback |
| R-0.02 | C++23 std::expected not available on target compiler | Use tl::expected header-only library as polyfill |
| R-0.03 | Savitzky-Golay coefficients have numerical instability for large windows | Limit window size to ≤ 51; use double precision throughout |

---

## 8. Acceptance Criteria

- [ ] `cmake --build build` succeeds with zero errors and zero warnings on Windows (MSVC 2022) and Linux (GCC 13).
- [ ] `ctest` runs all math_utils unit tests with zero failures.
- [ ] Google Benchmark runs and produces timing results for savgolFilter and cumulativeTrapz.
- [ ] All 15 module directories exist with placeholder CMakeLists.txt files.
- [ ] `ev::battery::common::Sample` and `InternalTimeSeries` compile and are used in at least one test.
- [ ] Doxygen generates without warnings on math_utils headers.

---

## Revision History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2026-10-03 | Initial Iteration 0 SRS. |
