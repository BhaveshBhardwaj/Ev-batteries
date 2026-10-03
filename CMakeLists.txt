cmake_minimum_required(VERSION 3.25)
project(ev_battery VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Options ───────────────────────────────────────────────────
option(EV_BATTERY_BUILD_TESTS      "Build unit tests"   ON)
option(EV_BATTERY_BUILD_BENCHMARKS "Build benchmarks"   ON)

# ── Dependencies via FetchContent ─────────────────────────────
include(FetchContent)

# Eigen 3.4
FetchContent_Declare(eigen
    GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
    GIT_TAG        3.4.0
    GIT_SHALLOW    TRUE
)
set(EIGEN_BUILD_DOC       OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING         OFF CACHE BOOL "" FORCE)
set(EIGEN_BUILD_PKGCONFIG OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(eigen)

# spdlog 1.14
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.14.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(spdlog)

# Google Test 1.14
if(EV_BATTERY_BUILD_TESTS)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
        GIT_SHALLOW    TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    enable_testing()
    include(GoogleTest)
endif()

# Google Benchmark 1.8
if(EV_BATTERY_BUILD_BENCHMARKS)
    FetchContent_Declare(benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        v1.8.4
        GIT_SHALLOW    TRUE
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)
endif()

# ── Core Modules ──────────────────────────────────────────────
add_subdirectory(src/modules/math_utils)
add_subdirectory(src/modules/ingestion)
add_subdirectory(src/modules/quality_score)
add_subdirectory(src/modules/cleaning)
add_subdirectory(src/modules/segmentation)
add_subdirectory(src/modules/denoising)
add_subdirectory(src/modules/ica_dva)
add_subdirectory(src/modules/features)
add_subdirectory(src/modules/thermal)
add_subdirectory(src/modules/calibration)
add_subdirectory(src/modules/estimation)
add_subdirectory(src/modules/uncertainty)
add_subdirectory(src/modules/gpr_completion)
add_subdirectory(src/modules/output)

# Aggregate interface target that links all core modules
add_library(ev_battery_core INTERFACE)
target_link_libraries(ev_battery_core INTERFACE
    ev_battery_math_utils
    ev_battery_ingestion
    ev_battery_quality_score
    ev_battery_cleaning
    ev_battery_segmentation
    ev_battery_denoising
    ev_battery_gpr_completion
    ev_battery_ica_dva
    ev_battery_features
    ev_battery_thermal
    ev_battery_calibration
    ev_battery_estimation
    ev_battery_uncertainty
    ev_battery_output
)

# ── Applications ──────────────────────────────────────────────
add_subdirectory(src/apps/cli)

# ── Tests ─────────────────────────────────────────────────────
if(EV_BATTERY_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# ── Benchmarks ────────────────────────────────────────────────
if(EV_BATTERY_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
