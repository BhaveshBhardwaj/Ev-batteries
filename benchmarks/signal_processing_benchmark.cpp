#include <benchmark/benchmark.h>
#include <ev/battery/cleaning/cleaning.hpp>
#include <ev/battery/segmentation/segmentation.hpp>
#include <ev/battery/denoising/denoising.hpp>
#include <ev/battery/ica_dva/ica_dva.hpp>
#include <ev/battery/features/features.hpp>
#include <ev/battery/thermal/thermal.hpp>
#include <ev/battery/calibration/calibration.hpp>
#include <ev/battery/estimation/estimation.hpp>
#include <ev/battery/uncertainty/uncertainty.hpp>
#include <ev/battery/output/certificate.hpp>
#include <ev/battery/output/sha256.hpp>
#include <ev/battery/ingestion/ingestion.hpp>
#include <ev/battery/quality_score/quality_score.hpp>
#include <ev/battery/gpr_completion/gpr_completion.hpp>
#include <random>
#include <vector>

using namespace ev::battery;

static void BM_HampelFilter(benchmark::State& state) {
    const size_t n = state.range(0);
    std::vector<double> input(n);
    std::vector<double> output(n);

    std::mt19937 rng(42);
    std::normal_distribution<double> dist(53.0, 0.5);
    for (size_t i = 0; i < n; ++i) {
        input[i] = dist(rng);
    }

    for (auto _ : state) {
        auto res = cleaning::hampelFilter(input, output, 3, 3.0);
        benchmark::DoNotOptimize(res);
        benchmark::DoNotOptimize(output.data());
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_HampelFilter)->Range(1000, 10000);

static void BM_RepairTimestampGaps(benchmark::State& state) {
    const size_t n = state.range(0);
    common::InternalTimeSeries ts;
    ts.samples.reserve(n);

    double t = 0.0;
    for (size_t i = 0; i < n; ++i) {
        ts.samples.push_back({
            .timestamp_s = t,
            .voltage_v = 53.0,
            .current_a = 15.0,
            .temperature_c = 30.0
        });
        // Introduce occasional gap
        t += (i % 50 == 0) ? 3.0 : 1.0;
    }

    for (auto _ : state) {
        auto res = cleaning::repairTimestampGaps(ts, 1.0, 10.0);
        benchmark::DoNotOptimize(res);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_RepairTimestampGaps)->Range(1000, 10000);

static void BM_SegmentRegimes(benchmark::State& state) {
    const size_t n = state.range(0);
    common::InternalTimeSeries ts;
    ts.samples.reserve(n);

    for (size_t i = 0; i < n; ++i) {
        double cur = 0.0;
        if (i < n / 4) cur = 0.0;
        else if (i < n / 2) cur = -15.0;
        else if (i < 3 * n / 4) cur = 0.0;
        else cur = 20.0;

        ts.samples.push_back({
            .timestamp_s = static_cast<double>(i),
            .voltage_v = 52.0,
            .current_a = cur,
            .temperature_c = 30.0
        });
    }

    for (auto _ : state) {
        auto res = segmentation::segmentRegimes(ts);
        benchmark::DoNotOptimize(res);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_SegmentRegimes)->Range(1000, 10000);

static void BM_AdaptiveKalmanFilter(benchmark::State& state) {
    const size_t n = state.range(0);
    std::vector<double> v(n, 53.0);
    std::vector<double> i(n, 15.0);
    std::vector<double> out(n);

    denoising::DenoisingConfig cfg;

    for (auto _ : state) {
        auto res = denoising::adaptiveKalmanFilter(v, i, out, cfg);
        benchmark::DoNotOptimize(res);
        benchmark::DoNotOptimize(out.data());
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_AdaptiveKalmanFilter)->Range(1000, 10000);

static void BM_CascadedDenoising(benchmark::State& state) {
    const size_t n = state.range(0);
    std::vector<double> v(n, 53.0);
    std::vector<double> i(n, 15.0);
    std::vector<double> out(n);

    denoising::DenoisingConfig cfg;
    cfg.savgol_window = 21;
    cfg.savgol_order = 2;

    for (auto _ : state) {
        auto res = denoising::denoiseVoltage(v, i, out, cfg);
        benchmark::DoNotOptimize(res);
        benchmark::DoNotOptimize(out.data());
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_CascadedDenoising)->Range(1000, 10000);

static void BM_ComputeIca(benchmark::State& state) {
    const size_t n = state.range(0);
    common::InternalTimeSeries cc;
    cc.samples.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        double progress = static_cast<double>(i) / static_cast<double>(n - 1);
        cc.samples.push_back({
            .timestamp_s = static_cast<double>(i),
            .voltage_v = 3.3 + 0.8 * progress,
            .current_a = 2.0,
            .temperature_c = 25.0
        });
    }

    ica_dva::IcaConfig cfg;
    cfg.voltage_step_v = 0.005;

    for (auto _ : state) {
        auto res = ica_dva::computeIca(cc, cfg);
        benchmark::DoNotOptimize(res);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_ComputeIca)->Range(1000, 10000);

static void BM_ExtractFeatures(benchmark::State& state) {
    const size_t n = state.range(0);
    ica_dva::IcaCurve ica;
    ica.voltage_v.resize(n);
    ica.charge_ah.resize(n);
    ica.dq_dv.resize(n);

    for (size_t i = 0; i < n; ++i) {
        double v = 3.3 + 0.005 * static_cast<double>(i);
        ica.voltage_v[i] = v;
        ica.charge_ah[i] = 0.01 * static_cast<double>(i);
        ica.dq_dv[i] = 1.0 + 4.0 * std::exp(-0.5 * std::pow((v - 3.7) / 0.04, 2.0));
    }

    features::FeatureConfig cfg;

    for (auto _ : state) {
        auto res = features::extractIcaFeatures(ica, cfg);
        benchmark::DoNotOptimize(res);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_ExtractFeatures)->Range(160, 1000);

static void BM_AnalyzeThermalStress(benchmark::State& state) {
    const size_t n = state.range(0);
    common::InternalTimeSeries ts;
    ts.samples.reserve(n);

    for (size_t i = 0; i < n; ++i) {
        ts.samples.push_back({
            .timestamp_s = static_cast<double>(i),
            .voltage_v = 51.2,
            .current_a = (i % 2 == 0) ? 25.0 : -5.0,
            .temperature_c = 42.0 + 5.0 * std::sin(static_cast<double>(i) / 100.0)
        });
    }

    thermal::ThermalConfig cfg;

    for (auto _ : state) {
        auto res = thermal::analyzeThermalStress(ts, cfg);
        benchmark::DoNotOptimize(res);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_AnalyzeThermalStress)->Range(1000, 10000);

static void BM_CalibrateTimeSeries(benchmark::State& state) {
    const size_t n = state.range(0);
    common::InternalTimeSeries ts;
    ts.nominal_capacity_ah = 30.0;
    ts.samples.reserve(n);

    // Synthesize alternating rest and drive segments
    for (size_t i = 0; i < n; ++i) {
        double cur = 0.65; // offset
        double v = 51.2;
        if (i > n / 4 && i < 3 * n / 4) {
            cur = -15.0; // active drive
            v = 48.5;
        }
        ts.samples.push_back({
            .timestamp_s = static_cast<double>(i),
            .voltage_v = v,
            .current_a = cur,
            .temperature_c = 35.0
        });
    }

    calibration::CalibrationConfig cfg;
    cfg.nominal_capacity_ah = 30.0;

    for (auto _ : state) {
        auto res = calibration::calibrateTimeSeries(ts, {}, cfg);
        benchmark::DoNotOptimize(res);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_CalibrateTimeSeries)->Range(1000, 10000);

static void BM_EstimateBatteryHealth(benchmark::State& state) {
    estimation::EstimationInput in;
    in.ica_features.primary_peak_voltage_v = 3.98;
    in.ica_features.primary_peak_height = 4.5;
    in.ica_features.primary_peak_area_ah = 1.15;
    in.nominal_capacity_ah = 30.0;
    in.equivalent_full_cycles = 120.0;
    in.thermal_report.thermal_degradation_index = 1.45;
    in.calibration_report.calibrated_coulomb_ah = 27.5;
    in.calibration_report.dual_anchors_found = true;

    estimation::EstimationConfig cfg;

    for (auto _ : state) {
        auto res = estimation::estimateBatteryHealth(in, cfg);
        benchmark::DoNotOptimize(res);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_EstimateBatteryHealth);

static void BM_QuantifyUncertainty(benchmark::State& state) {
    estimation::EstimationReport est;
    est.soh_percent = 84.0;
    est.confidence_score = 0.95;

    estimation::EstimationInput in;
    in.thermal_report.thermal_degradation_index = 1.25;
    in.calibration_report.dual_anchors_found = true;
    in.calibration_report.sensor_fault_detected = false;

    uncertainty::UncertaintyConfig cfg;

    for (auto _ : state) {
        auto res = uncertainty::quantifyUncertainty(est, in, cfg);
        benchmark::DoNotOptimize(res);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_QuantifyUncertainty);

static void BM_ComputeSha256(benchmark::State& state) {
    std::string payload = "IN-BPAN-2W-LFP-00941|2026-10-03T21:50:00Z|84.20|[81.80,86.60]|1250.00|1.35|CERTIFIED";
    for (auto _ : state) {
        auto hash = output::computeSha256(payload);
        benchmark::DoNotOptimize(hash);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ComputeSha256);

static void BM_GenerateHealthCertificate(benchmark::State& state) {
    const size_t n = state.range(0);
    common::InternalTimeSeries ts;
    ts.nominal_capacity_ah = 30.0;
    ts.samples.reserve(n);

    // Synthesize multi-regime session with Rest -> CC Charge -> Rest
    for (size_t i = 0; i < n; ++i) {
        double cur = 0.0;
        double v = 51.2;
        if (i < n / 6) {
            cur = 0.05; // Rest
            v = 50.0;
        } else if (i < 5 * n / 6) {
            cur = 15.0; // CC Charge
            double frac = static_cast<double>(i - n / 6) / static_cast<double>(4 * n / 6);
            v = 50.0 + 4.5 * frac;
        } else {
            cur = 0.02; // Rest
            v = 54.5;
        }
        ts.samples.push_back({
            .timestamp_s = static_cast<double>(i),
            .voltage_v = v,
            .current_a = cur,
            .temperature_c = 38.0
        });
    }

    output::BpanMetadata metadata;
    metadata.bpan_id = "IN-BPAN-2W-LFP-BM01";
    metadata.nominal_capacity_ah = 30.0;
    metadata.nominal_voltage_v = 51.2;

    for (auto _ : state) {
        auto res = output::generateHealthCertificate(ts, metadata, 100.0);
        benchmark::DoNotOptimize(res);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_GenerateHealthCertificate)->Range(1000, 10000);

static void BM_ParseCsv(benchmark::State& state) {
    const size_t n = state.range(0);
    std::string csv_data = "timestamp,voltage,current,temperature\n";
    csv_data.reserve(n * 35 + 40);
    for (size_t i = 0; i < n; ++i) {
        csv_data += std::to_string(i) + ".0,51.25,15.20,35.4\n";
    }

    for (auto _ : state) {
        auto res = ingestion::parseCsv(csv_data);
        benchmark::DoNotOptimize(res);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_ParseCsv)->Range(1000, 10000);

static void BM_ComputeQualityScore(benchmark::State& state) {
    const size_t n = state.range(0);
    common::InternalTimeSeries ts;
    ts.samples.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        ts.samples.push_back({
            .timestamp_s = static_cast<double>(i),
            .voltage_v = 51.25,
            .current_a = 15.0,
            .temperature_c = 34.0
        });
    }

    quality_score::QualityConfig cfg;
    for (auto _ : state) {
        auto res = quality_score::computeQualityScore(ts, cfg);
        benchmark::DoNotOptimize(res);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_ComputeQualityScore)->Range(1000, 10000);

static void BM_GprReconstructChargeCurve(benchmark::State& state) {
    const size_t n = state.range(0);
    common::InternalTimeSeries ts;
    ts.samples.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        double v = 50.0 + 4.5 * (static_cast<double>(i) / static_cast<double>(n));
        ts.samples.push_back({
            .timestamp_s = static_cast<double>(i),
            .voltage_v = v,
            .current_a = 15.0,
            .temperature_c = 32.0
        });
    }

    gpr_completion::GprConfig cfg;
    for (auto _ : state) {
        auto res = gpr_completion::reconstructChargeCurve(ts, cfg);
        benchmark::DoNotOptimize(res);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(BM_GprReconstructChargeCurve)->Range(1000, 5000);
