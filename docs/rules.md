# EV Battery Telemetry Analyzer — Development Rules

> **Document ID:** RULES-001  
> **Version:** 1.0  
> **Created:** 2026-10-03  
> **Author:** Development Team  
> **Status:** Active

---

## 1. Overarching Principles

### 1.1 Language Policy

| Priority | Language | Use Case |
|----------|----------|----------|
| **Primary** | **C++20 (or later)** | All core computation: signal processing, Kalman filters, ICA/DVA, feature extraction, model inference, data structures, math kernels. Performance is non-negotiable. |
| **Secondary** | **C** | Low-level sensor drivers, embedded logger firmware (ESP32), or when C++ overhead is unjustified. |
| **Tertiary** | **Python** | *Only* when switching is **necessary**: ML model *training* (PyTorch/TensorFlow), rapid prototyping of research notebooks, scripting CI/CD pipelines, and data exploration. Trained models must be exported (ONNX/TorchScript) and inference must run in C++. |
| **Tertiary** | **Rust** | Permitted for safety-critical network services or when C++ FFI is impractical. Requires explicit justification in the decision log. |

> **IMPORTANT:** Every language switch away from C++ **must** be logged in the iteration's Decision Log with: (a) the reason, (b) the alternatives considered, and (c) the performance impact assessment.

### 1.2 Performance Standards

- All numerical pipelines must be profiled with benchmarks before and after changes.
- Memory allocations in hot paths must be pre-allocated or pooled.
- SIMD intrinsics (SSE4.2 / AVX2 / NEON) should be used where vectorization applies (e.g., Savitzky-Golay convolution, dQ/dV differentiation).
- Real-time processing target: a 1-hour charge log (1 Hz, ~3600 samples) must complete the full pipeline (clean → segment → denoise → ICA → features → inference) in **< 500 ms** on a single core.

### 1.3 Modularity Requirements

- **Every feature is an independent, self-contained module** with:
  - Its own directory under `src/modules/<module_name>/`.
  - Its own public header(s) in `include/<module_name>/`.
  - Its own unit tests in `tests/<module_name>/`.
  - Its own `CMakeLists.txt` (linked as a static or shared library).
- **Semantic Modularity over Function Fragmentation:** Code within a module must be grouped by **purpose, meaning, and mathematical domain** (e.g., `calculus`, `filters`, `statistics`). Never scatter code into micro-files where each individual function has its own source/header/test file.
- **No Premature Placeholders:** Do not create empty stub directories or dummy placeholder CMake files in advance. Create module files only when that specific feature is actively being implemented.
- **Zero circular dependencies.** Dependency direction flows downward:
  ```
  API Layer → Orchestrator → Feature Modules → Core Libraries → Math/Util
  ```
- Modules communicate exclusively through **well-defined interfaces** (abstract base classes or C-style function pointers for hot paths).
- Any module can be compiled, tested, and benchmarked **in isolation**.

### 1.4 Build System

- **CMake >= 3.25** is the sole build system.
- Top-level `CMakeLists.txt` composes modules via `add_subdirectory()`.
- Third-party dependencies managed via **vcpkg** or **Conan 2.x** (decide in Iteration 0 and stick with it).
- Build targets:
  - `ev_battery_core` — static library of all modules.
  - `ev_battery_cli` — command-line tool for batch processing.
  - `ev_battery_api` — REST API server (links core + networking).
  - `ev_battery_tests` — all unit and integration tests.
  - `ev_battery_bench` — benchmarks.

---

## 2. Iteration Workflow

Every iteration follows this exact sequence:

```
1. Analyze previous iteration's outcomes
   ↓
2. Draft new SRS for this iteration
   ↓
3. Prerequisite & Requirement Verification Gate (Gate 0)
   ↳ [If unfulfilled: WARN user, HALT code writing, present approaches to fulfill]
   ↓
4. Design Phase: architecture, interfaces, pros/cons
   ↓
5. Implementation (Modular & Semantic)
   ↓
6. Testing & Benchmarking
   ↓
7. Decision Log entry
   ↓
8. Retrospective & SRS update
```

### 2.1 Pre-Iteration Analysis

Before writing any code in a new iteration:
1. Review the previous iteration's SRS, decision log, and test results.
2. Identify what was delivered, what was deferred, and what failed.
3. Update the master feature backlog.

### 2.2 SRS for Each Iteration

Each iteration produces a new SRS document at:
```
docs/iterations/iteration_N/SRS_iteration_N.md
```

The SRS **must** contain:

| Section | Contents |
|---------|----------|
| **Scope** | What this iteration delivers. What is explicitly out of scope. |
| **Requirements** | Functional requirements (FR-N.xx) and non-functional requirements (NFR-N.xx), each with acceptance criteria. |
| **Dependencies** | Which modules/libraries this iteration depends on. |
| **Interfaces** | Public API contracts (C++ headers, function signatures, data structures). |
| **Design Decisions** | For each significant choice: the decision, alternatives considered, pros/cons of each, and the rationale. |
| **Test Plan** | Unit tests, integration tests, benchmark targets. |
| **Risk Register** | Technical risks and mitigations for this iteration. |
| **Acceptance Criteria** | Binary pass/fail conditions for the iteration to be considered complete. |

### 2.3 Design Phase Protocol

For **every feature** in the iteration:

1. **State the problem** clearly in one paragraph.
2. **List at least 2 alternative approaches.**
3. **For each alternative, write:**
   - Pros (bullet list)
   - Cons (bullet list)
   - Performance implications
   - Complexity estimate (Low / Medium / High)
   - Impact on other modules
4. **Select one.** Write the rationale.
5. **Define the interface** (C++ header with Doxygen comments) before writing the implementation.

### 2.4 Prerequisite & Requirement Verification Protocol (Mandatory Gate)

> **STRICT RULE: Before writing any code for a feature, verify that all prerequisites and requirements are fully satisfied. If any requirement is missing or unfulfilled, you MUST WARN the user immediately and DO NOT write any code.**
>
> **Never write code on assumptions or without required input data (e.g., without real/synthetic battery data, simulation and model training cannot be done).**

#### 2.4.1 What to Audit Before Coding
Before generating implementation code for any feature, perform a prerequisite audit:
1. **Input Data & Reference Assets:** Does the feature require telemetry logs, charge-discharge curves, or reference datasets? (e.g., ICA peak detection requires clean $V(t)$ and $I(t)$ data; simulation requires half-cell potentials or degradation parameters).
2. **Domain & Physical Parameters:** Are cell chemistry parameters (LFP/NMC cell capacities, nominal voltages, resistance baselines, thermal limits) specified?
3. **Hardware / Protocol Interfaces:** Are BMS telemetry packet formats, sensor precision limits, and sampling rates defined?
4. **Validation Ground Truth:** Do we have benchmark reference values (e.g. published experimental results, analytic formulas, NumPy/SciPy reference outputs) to test against?

#### 2.4.2 Protocol When Requirements Are Not Fulfilled
If any requirement or prerequisite is not satisfied:
1. **WARN THE USER IMMEDIATELY:** State clearly:
   - What feature is being requested.
   - Exactly which requirements, assets, or parameters are missing.
   - Why writing code without this prerequisite will fail, produce invalid results, or create untestable mockups.
2. **DO NOT WRITE ANY CODE:** Halt all implementation work on that feature immediately. Do not write placeholder or stub code.
3. **PROPOSE APPROACHES TO FULFILL THE REQUIREMENTS:** Present at least 2–3 concrete, actionable approaches to satisfy the missing prerequisite. For example:
   - *Example (Missing Battery Data for Simulation/ICA):*
     - **Approach A (Public Benchmark Dataset):** Download and integrate verified public laboratory cycling datasets (e.g., NASA Ames Li-ion Battery Aging dataset, CALCE, or Oxford Battery Degradation dataset).
     - **Approach B (Physics-Based Synthetic Generator):** Generate synthetic OCV-Q curves from established literature Single Particle Model (SPM) half-cell equations with parameterized LLI/LAM degradation.
     - **Approach C (User-Supplied Telemetry):** Provide a standardized CSV/JSON template for the user to supply real vehicle/pack telemetry logs.
4. **AWAIT USER DIRECTION:** Let the user select the preferred approach or supply the missing requirements before proceeding to design and implementation.

### 2.5 Decision Log

Every iteration maintains a decision log at:
```
docs/iterations/iteration_N/decisions_iteration_N.md
```

Each entry follows this template:

```markdown
### DEC-N.XX: [Short Title]

- **Date:** YYYY-MM-DD
- **Context:** [Why this decision was needed]
- **Options Considered:**
  1. [Option A] — [one-line summary]
  2. [Option B] — [one-line summary]
  3. [Option C] — [one-line summary]
- **Decision:** [Which option was chosen]
- **Rationale:** [Why]
- **Consequences:** [What this enables or constrains going forward]
- **Reversibility:** [Easy / Medium / Hard]
```

---

## 3. Code Standards

### 3.1 C++ Style

- Follow the **C++ Core Guidelines** (isocpp.github.io/CppCoreGuidelines).
- Naming:
  - Classes/Structs: `PascalCase`
  - Functions/Methods: `camelCase`
  - Variables: `snake_case`
  - Constants/Enums: `UPPER_SNAKE_CASE`
  - Namespaces: `ev::battery::<module>`
- Use `std::span`, `std::optional`, `std::expected` (C++23) or `tl::expected` over raw pointers and error codes.
- No raw `new` / `delete`. Use `std::unique_ptr`, `std::shared_ptr`, or arena allocators.
- No exceptions in hot paths. Use error codes or `expected<T, E>`.
- `#pragma once` for header guards.
- Maximum function length: **50 lines** (excluding braces). Factor longer functions.

### 3.2 Documentation

- Every public function gets a **Doxygen** comment block:
  ```cpp
  /**
   * @brief Compute dQ/dV from a smoothed charge curve.
   * @param voltage Voltage samples (V), monotonically increasing.
   * @param charge  Cumulative charge (Ah), same length as voltage.
   * @param config  Smoothing and differentiation parameters.
   * @return ICAResult containing dQ/dV curve and extracted peaks.
   * @throws std::invalid_argument if voltage.size() != charge.size().
   */
  ```
- Internal implementation comments explain **why**, not **what**.
- Each module has a `README.md` explaining its purpose, public API, and usage examples.

### 3.3 Testing — Mandatory Policy

> **RULE: No code is merged without corresponding tests. Every public function, every class, every module gets tested. No exceptions.**

#### 3.3.1 Framework and Structure

- **Unit tests:** Google Test (gtest) framework.
- **Benchmarks:** Google Benchmark. Tracked in CI; regressions fail the build.
- **Coverage target:** >= 80% line coverage on core modules. >= 90% on `math_utils` and `calibration`.
- **Test file mapping:** One test file per source file, named `test_<source_name>.cpp`.
  ```
  src/modules/denoising/kalman_filter.cpp
  tests/denoising/test_kalman_filter.cpp
  ```
- **Test data:** Stored in `tests/data/<module_name>/` with clear provenance (dataset name, license, download script).

#### 3.3.2 Required Test Categories

Every function or feature added must have tests from **all applicable** categories below:

| Category | What It Validates | When Required |
|----------|-------------------|---------------|
| **Correctness** | Output matches expected value for known inputs. | **Always.** Every function. |
| **Boundary / Edge** | Behavior at limits: empty input, single element, maximum size, min/max values, NaN/Inf. | **Always.** Every function. |
| **Error / Invalid Input** | Graceful handling of bad input: wrong sizes, null spans, out-of-range parameters. | **Always.** Every public function. |
| **Regression** | Reproduces a previously-fixed bug. | When a bug is fixed. |
| **Performance / Benchmark** | Execution time meets the target on reference data sizes. | Every function in a hot path (signal processing, math, ICA, inference). |
| **Numerical Accuracy** | Output error is within a specified tolerance (absolute or relative). | Every numerical/math function. |
| **Property-Based** | Verifies mathematical invariants (e.g., "smoothing a polynomial of degree ≤ order reproduces it exactly"). | Every math function where an invariant exists. |
| **Roundtrip / Consistency** | Inverse operations cancel (e.g., integrate then differentiate ≈ identity). | Where applicable. |
| **Cross-Reference** | C++ output matches a trusted reference implementation (NumPy, SciPy). | At least one cross-reference test per math function. |

#### 3.3.3 Required Tests Per Code Category

**For every public function:**
```
TEST(ModuleName_FunctionName, CorrectOutput_TypicalInput)     // Correctness
TEST(ModuleName_FunctionName, CorrectOutput_KnownAnalytic)     // Numerical accuracy
TEST(ModuleName_FunctionName, EmptyInput_ReturnsErrorOrEmpty)   // Boundary
TEST(ModuleName_FunctionName, SingleElement_HandledCorrectly)   // Boundary
TEST(ModuleName_FunctionName, LargeInput_CompletesWithinLimit)  // Performance
TEST(ModuleName_FunctionName, MismatchedSizes_ReturnsError)     // Error handling
TEST(ModuleName_FunctionName, NaNInput_DoesNotCrash)            // Robustness
TEST(ModuleName_FunctionName, MatchesNumPyReference)            // Cross-reference
```

**For every class:**
- All of the above for each public method.
- Construction and destruction tests (valid config, invalid config).
- State transition tests (if stateful, e.g., Kalman filter: predict → update → predict sequence).

**For every module (integration level):**
- End-to-end test: representative input → expected output through the entire module.
- Chain test: module output is valid input for the next module in the pipeline.
- Error propagation test: bad input at module entry produces a clear error, not a crash or silent corruption.

**For the full pipeline:**
- Smoke test: a known public dataset (e.g., NASA cell B0005) produces a `HealthReport` with SoH within expected range.
- Regression test: every previously-computed result on a reference input is stored and compared.

#### 3.3.4 Test Naming Convention

```
TEST(TestSuite, ConditionUnderTest_ExpectedBehavior)
```

Examples:
```cpp
TEST(MathUtils_SavgolFilter, QuadraticInput_PreservesExactly)
TEST(MathUtils_SavgolFilter, WindowLargerThanInput_ReturnsError)
TEST(MathUtils_SavgolFilter, OddWindowSize_ComputesCorrectly)
TEST(MathUtils_SavgolFilter, EvenWindowSize_ReturnsError)
TEST(MathUtils_CentralDifference, SinInput_MatchesCosWithinH2)
TEST(MathUtils_CentralDifference, TwoPoints_FallsBackToForwardDifference)
TEST(MathUtils_CentralDifference, ConstantInput_ReturnsZero)
TEST(Denoising_KalmanFilter, SyntheticNoise_SNRImprovesBy10dB)
TEST(Denoising_KalmanFilter, ZeroNoise_OutputEqualsInput)
TEST(Segmentation_ClassifySegments, KnownChargeProfile_CorrectLabels)
TEST(Calibration_EstimateOffset, SyntheticOffset300mA_EstimatedWithin10mA)
```

#### 3.3.5 Test Case Template

When adding a new function, copy this template into the test file:

```cpp
// ============================================================
// Tests for: ev::battery::<module>::<functionName>
// Source:     src/modules/<module>/<source>.cpp
// Date added: YYYY-MM-DD
// ============================================================

#include <gtest/gtest.h>
#include <ev/battery/<module>/<header>.hpp>
#include <vector>
#include <cmath>
#include <limits>

namespace {

// --- Correctness ---

TEST(<Module>_<Function>, TypicalInput_ProducesExpectedOutput) {
    // GIVEN: a known input
    // WHEN:  the function is called
    // THEN:  the output matches the expected value within tolerance
}

TEST(<Module>_<Function>, KnownAnalytic_MatchesExactSolution) {
    // GIVEN: input where the analytic solution is known (e.g., sin → cos)
    // WHEN:  the function is called
    // THEN:  the output matches the analytic solution within O(h^n)
}

// --- Boundary / Edge ---

TEST(<Module>_<Function>, EmptyInput_HandledGracefully) {
    // Empty span or zero-length vector
}

TEST(<Module>_<Function>, SingleElement_HandledCorrectly) {
    // Minimum viable input
}

TEST(<Module>_<Function>, MaximumReasonableSize_CompletesSuccessfully) {
    // Large input (e.g., 1M elements) completes without crash or timeout
}

// --- Error / Invalid Input ---

TEST(<Module>_<Function>, MismatchedArraySizes_ReturnsError) {
    // x.size() != y.size() or similar
}

TEST(<Module>_<Function>, NaNInInput_DoesNotCrash) {
    // Input contains std::numeric_limits<double>::quiet_NaN()
}

TEST(<Module>_<Function>, InfInInput_DoesNotCrash) {
    // Input contains std::numeric_limits<double>::infinity()
}

TEST(<Module>_<Function>, InvalidParameter_ReturnsError) {
    // e.g., negative window size, order >= window size
}

// --- Numerical Accuracy ---

TEST(<Module>_<Function>, Accuracy_WithinSpecifiedTolerance) {
    // Verify error <= tolerance against a reference
    // State the tolerance and why it is acceptable
}

// --- Cross-Reference ---

TEST(<Module>_<Function>, MatchesNumPyReference) {
    // Hardcoded input/output pair computed with NumPy/SciPy
    // Include the Python code as a comment
}

// --- Property-Based ---

TEST(<Module>_<Function>, MathematicalInvariant_Holds) {
    // e.g., smoothing preserves polynomials, integration of derivative ≈ identity
}

} // anonymous namespace
```

#### 3.3.6 Performance Test Requirements

For every function identified as "hot path" (signal processing, math kernels, inference):

```cpp
#include <benchmark/benchmark.h>

static void BM_<Module>_<Function>(benchmark::State& state) {
    const auto n = state.range(0);
    // Setup: allocate and fill input/output buffers OUTSIDE the loop
    std::vector<double> input(n), output(n);
    // ... fill with representative data ...

    for (auto _ : state) {
        ev::battery::<module>::<function>(input, output, /* params */);
        benchmark::DoNotOptimize(output.data());
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_<Module>_<Function>)
    ->Arg(1000)     // 1K samples
    ->Arg(3600)     // 1-hour @ 1Hz (primary target)
    ->Arg(10000)    // 10K samples
    ->Arg(100000);  // stress test
```

Performance targets are defined per function in the iteration SRS. All benchmarks must include the **3600-sample (1-hour @ 1Hz) case**, as this is the primary operational scenario.

#### 3.3.7 Test Data Provenance

Every file in `tests/data/` must have an accompanying `PROVENANCE.md`:

```markdown
# Test Data Provenance

## <filename>
- **Source:** [e.g., NASA PCoE Battery Dataset, or "synthetically generated"]
- **URL:** [download URL, or "N/A" if synthetic]
- **License:** [e.g., Public Domain, CC-BY-4.0]
- **Download Date:** YYYY-MM-DD
- **SHA-256:** <hash>
- **Preprocessing:** [any transformations applied, or "None"]
- **Description:** [what the data contains and why it is used]
```

#### 3.3.8 Continuous Testing Rules

- **All tests run on every build.** No "skip in CI" markers without a Decision Log entry.
- **Flaky tests are bugs.** A test that passes sometimes and fails sometimes must be fixed or replaced within 24 hours.
- **Benchmark regressions > 5% fail the build.** Performance is a feature, not an afterthought.
- **New code without tests blocks the iteration.** The iteration cannot be marked complete if any new public function lacks the required test categories.

### 3.4 Version Control

- Git with conventional commits: `feat:`, `fix:`, `refactor:`, `perf:`, `docs:`, `test:`, `chore:`.
- Each iteration is a branch: `iteration/N-short-description`.
- Merge to `main` only after all acceptance criteria pass.
- Tag each iteration completion: `vN.0.0`.

---

## 4. Module Registry

The following modules are planned. Each is independent and will be delivered across iterations.

| Module ID | Name | Description | Primary Language |
|-----------|------|-------------|-----------------|
| `M01` | `ingestion` | REST API, JSON/CSV parsing, schema normalization, OEM adapters | C++ (with Python adapter scripts if needed) |
| `M02` | `cleaning` | Outlier removal, timestamp gap handling, sensor fault correction | C++ |
| `M03` | `segmentation` | Charge/discharge/rest segment detection, CC-phase identification | C++ |
| `M04` | `denoising` | Kalman filter, Savitzky-Golay, moving average smoothers | C++ |
| `M05` | `ica_dva` | dQ/dV and dV/dQ computation, GPR-based curve fitting | C++ (GPR kernel: C++ or Eigen) |
| `M06` | `features` | Peak detection, peak tracking, feature vector assembly | C++ |
| `M07` | `calibration` | Current-sensor self-calibration (offset/gain correction) | C++ |
| `M08` | `estimation` | SoH/RUL inference (Random Forest, gradient boosting, ONNX runtime) | C++ (ONNX Runtime for inference) |
| `M09` | `uncertainty` | Conformal prediction, confidence intervals, abstention logic | C++ |
| `M10` | `synthetic_data` | Half-cell models, degradation simulation, synthetic dataset generation | C++ + Python (for training loop) |
| `M11` | `fleet_adapt` | Few-shot fleet calibration, transfer learning adapter | C++ + Python |
| `M12` | `quality_score` | Telemetry Quality Score computation | C++ |
| `M13` | `output` | Certificate generation (JSON/PDF), BPAN payload formatting | C++ |
| `M14` | `thermal` | Arrhenius aging model, thermal stress scoring | C++ |
| `M15` | `math_utils` | Linear algebra, interpolation, differentiation, statistics | C++ (Eigen) |

---

## 5. Iteration Plan (High-Level)

| Iteration | Focus | Modules Delivered | Milestone |
|-----------|-------|-------------------|-----------|
| **0** | Project scaffolding, build system, CI, math utilities | `M15` | Build compiles, tests pass, benchmark harness works |
| **1** | Core signal processing pipeline | `M02`, `M03`, `M04` | Can clean, segment, and denoise a raw charge log |
| **2** | ICA/DVA engine and feature extraction | `M05`, `M06` | dQ/dV peaks extracted from public dataset (NASA/CALCE) with < 2.5% error |
| **3** | Data ingestion and telemetry quality | `M01`, `M12` | REST API accepts JSON/CSV, computes quality score |
| **4** | Current-sensor self-calibration | `M07` | Calibration corrects synthetic offset/gain errors to < 0.5% residual |
| **5** | SoH estimation model | `M08` | Trained model achieves target RMSE on test set |
| **6** | Uncertainty and abstention | `M09` | Conformal prediction intervals with >= 90% coverage |
| **7** | Synthetic data and sim-to-real | `M10` | Synthetic dataset generated, model fine-tuned |
| **8** | Thermal model and output certificates | `M14`, `M13` | Thermal stress score + JSON/PDF certificate |
| **9** | Fleet adaptation | `M11` | Few-shot calibration demonstrated on held-out fleet |
| **10** | Integration, hardening, API polish | All | End-to-end pipeline, benchmarked, documented |

> **NOTE:** This plan will be refined at the start of each iteration based on learnings. Iterations may be split, merged, or reordered as needed, but every change must be logged.

---

## 6. Dependency Rules

### 6.1 Third-Party Libraries (Approved List)

| Library | Purpose | License |
|---------|---------|---------|
| **Eigen 3** | Linear algebra, matrix operations | MPL-2.0 |
| **ONNX Runtime** | ML model inference in C++ | MIT |
| **nlohmann/json** | JSON parsing | MIT |
| **cpp-httplib** or **Drogon** | HTTP server for REST API | MIT / MIT |
| **Google Test** | Unit testing | BSD-3 |
| **Google Benchmark** | Performance benchmarking | Apache-2.0 |
| **spdlog** | Logging | MIT |
| **fmt** | String formatting | MIT |
| **Catch2** | Alternative test framework (if gtest is problematic) | BSL-1.0 |

> **WARNING:** Adding a new third-party dependency requires a Decision Log entry with license review and justification.

### 6.2 Python Libraries (Training Only)

| Library | Purpose |
|---------|---------|
| PyTorch / TensorFlow | Model training |
| scikit-learn | Baseline models, evaluation |
| NumPy / SciPy | Numerical computing |
| pandas | Data manipulation |
| matplotlib / plotly | Visualization |
| ONNX | Model export to C++ runtime |

---

## 7. Documentation Structure

```
docs/
├── rules.md                           ← THIS FILE
├── SRS_initial.md                     ← Master SRS (initial)
├── architecture/
│   ├── system_overview.md
│   ├── module_interfaces.md
│   └── data_flow.md
├── iterations/
│   ├── iteration_0/
│   │   ├── SRS_iteration_0.md
│   │   └── decisions_iteration_0.md
│   ├── iteration_1/
│   │   ├── SRS_iteration_1.md
│   │   └── decisions_iteration_1.md
│   └── ...
└── research/
    ├── ica_literature_review.md
    ├── kalman_filter_design.md
    └── dataset_provenance.md
```

---

## 8. Quality Gates

No iteration is complete until **all** of the following pass:

- [ ] All unit tests pass (zero failures).
- [ ] All benchmarks meet their targets (no regressions > 5%).
- [ ] Code coverage >= 80% on new code.
- [ ] No compiler warnings with `-Wall -Wextra -Wpedantic -Werror`.
- [ ] Doxygen generates without warnings.
- [ ] Decision log is complete for all design choices.
- [ ] SRS acceptance criteria are met.
- [ ] Code reviewed (self-review checklist if solo).

---

## 9. Data Handling Rules

- **Raw data is immutable.** Never modify ingested files; write processed data to separate output paths.
- **All datasets must have provenance:** name, source URL, license, download date, and a SHA-256 hash.
- **No PII in the repository.** Fleet data must be anonymized before commit.
- **Test data in `tests/data/`** must be small (< 10 MB per file) and synthetic or from permissively licensed public datasets.

---

## 10. Revision History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2026-10-03 | Initial rules document. |
