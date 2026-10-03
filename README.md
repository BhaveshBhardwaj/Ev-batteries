# EV Battery Health Certification Engine

State-of-Health (SoH) valuation and certification for India's 2W/3W electric-vehicle fleets.

This repository contains a C++23 battery telemetry processing core, command-line application, automated tests, benchmarks, and a browser-based BPAN Tier 3 inspection interface. The processing pipeline covers ingestion, telemetry quality scoring, cleaning, denoising, ICA/DVA analysis, feature extraction, thermal analysis, calibration, estimation, uncertainty, GPR completion, and certificate output.

## Repository layout

- `include/ev/` - Public C++ headers.
- `src/modules/` - Core processing modules.
- `src/apps/cli/` - Command-line application.
- `tests/` - GoogleTest unit and integration tests.
- `benchmarks/` - Google Benchmark targets.
- `frontend/` - Interactive browser-based inspection and certificate UI.
- `docs/` - Architecture, requirements, rules, and evaluation notes.
- `data/` - Reference datasets.
- `scripts/` - Dataset download utilities.

## Requirements

- CMake 3.25 or newer
- A compiler with C++23 support
- Git, for CMake FetchContent dependencies
- Python 3, optional, for serving the frontend locally

Dependencies are downloaded and configured automatically by CMake: Eigen, spdlog, GoogleTest, and Google Benchmark.

## Build

From the repository root:

```powershell
cmake --preset default
cmake --build --preset default --config Debug
```

For a release build:

```powershell
cmake --preset release
cmake --build --preset release --config Release
```

The generated `build/` and `build-release/` directories are intentionally ignored by Git.

## Test

After configuring and building the default preset:

```powershell
ctest --preset default -C Debug --output-on-failure
```

## Run the CLI

The executable is generated under the configuration-specific build output directory. On Visual Studio generators, run the CLI with:

```powershell
build\Debug\ev_battery_cli.exe
```

The exact executable path can vary by generator. The main application target is `ev_battery_cli`.

## Frontend

The frontend can be opened directly at `frontend/index.html`. For a local server from the repository root:

```powershell
python -m http.server 8080
```

Then open <http://localhost:8080/frontend/> in a browser.

## Data and research context

Project requirements and the research context are documented in `docs/` and `context.txt`. Dataset download scripts are available in `scripts/`; review their source and destination paths before running them.

## License

No license has been declared for this repository yet.