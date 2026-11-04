#include <ev/battery/output/certificate.hpp>

#include <ev/battery/calibration/calibration.hpp>
#include <ev/battery/cleaning/cleaning.hpp>
#include <ev/battery/denoising/denoising.hpp>
#include <ev/battery/features/features.hpp>
#include <ev/battery/ica_dva/ica_dva.hpp>
#include <ev/battery/output/sha256.hpp>
#include <ev/battery/quality_score/quality_score.hpp>
#include <ev/battery/segmentation/segmentation.hpp>
#include <ev/battery/thermal/thermal.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace ev::battery::output {

namespace {

std::string getCurrentUtcTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_utc{};
#if defined(_WIN32)
    gmtime_s(&tm_utc, &time_t_now);
#else
    gmtime_r(&time_t_now, &tm_utc);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
    return std::string(buf);
}

std::string buildCanonicalPayload(const HealthCertificate& cert) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    ss << cert.metadata.bpan_id << "|"
       << cert.evaluation_timestamp_utc << "|"
       << cert.certified_soh_pct << "|["
       << cert.soh_lower_bound_pct << ","
       << cert.soh_upper_bound_pct << "]|"
       << cert.remaining_useful_life_cycles << "|"
       << cert.thermal_degradation_index << "|"
       << cert.certification_decision;
    return ss.str();
}

} // namespace

std::string serializeCertificateToJson(const HealthCertificate& cert) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);

    ss << "{\n";
    ss << "  \"bpan_tier\": 3,\n";
    ss << "  \"bpan_metadata\": {\n";
    ss << "    \"bpan_id\": \"" << cert.metadata.bpan_id << "\",\n";
    ss << "    \"serial_number\": \"" << cert.metadata.serial_number << "\",\n";
    ss << "    \"chemistry\": \"" << cert.metadata.chemistry << "\",\n";
    ss << "    \"vehicle_category\": \"" << cert.metadata.vehicle_category << "\",\n";
    ss << "    \"nominal_capacity_ah\": " << cert.metadata.nominal_capacity_ah << ",\n";
    ss << "    \"nominal_voltage_v\": " << cert.metadata.nominal_voltage_v << ",\n";
    ss << "    \"series_cells\": " << cert.metadata.series_cells << "\n";
    ss << "  },\n";

    ss << "  \"certified_health_valuation\": {\n";
    ss << "    \"certified_soh_pct\": " << cert.certified_soh_pct << ",\n";
    ss << "    \"usable_capacity_ah\": " << cert.usable_capacity_ah << ",\n";
    ss << "    \"lifetime_efc\": " << cert.lifetime_efc << "\n";
    ss << "  },\n";

    ss << "  \"conformal_uncertainty\": {\n";
    ss << "    \"soh_lower_bound_pct\": " << cert.soh_lower_bound_pct << ",\n";
    ss << "    \"soh_upper_bound_pct\": " << cert.soh_upper_bound_pct << ",\n";
    ss << "    \"confidence_interval_width_pct\": " << cert.confidence_interval_width_pct << ",\n";
    ss << "    \"coverage_level\": " << std::setprecision(2) << cert.coverage_level << "\n";
    ss << "  },\n";

    ss << "  \"degradation_diagnostics\": {\n";
    ss << "    \"remaining_useful_life_cycles\": " << cert.remaining_useful_life_cycles << ",\n";
    ss << "    \"lli_percent\": " << cert.lli_percent << ",\n";
    ss << "    \"lam_percent\": " << cert.lam_percent << ",\n";
    ss << "    \"primary_degradation_mode\": \"" << cert.primary_degradation_mode << "\"\n";
    ss << "  },\n";

    ss << "  \"operational_thermal_sensor\": {\n";
    ss << "    \"thermal_degradation_index\": " << cert.thermal_degradation_index << ",\n";
    ss << "    \"mean_temperature_c\": " << cert.mean_temperature_c << ",\n";
    ss << "    \"max_temperature_c\": " << cert.max_temperature_c << ",\n";
    ss << "    \"sensor_offset_bias_a\": " << std::setprecision(4) << cert.sensor_offset_bias_a << ",\n";
    ss << "    \"sensor_gain_factor\": " << std::setprecision(4) << cert.sensor_gain_factor << "\n";
    ss << "  },\n";

    ss << "  \"certification_status\": {\n";
    ss << "    \"is_certified\": " << (cert.is_certified ? "true" : "false") << ",\n";
    ss << "    \"certification_decision\": \"" << cert.certification_decision << "\",\n";
    ss << "    \"decision_rationale\": \"" << cert.decision_rationale << "\"\n";
    ss << "  },\n";

    ss << "  \"cryptographic_signature\": {\n";
    ss << "    \"algorithm\": \"SHA-256\",\n";
    ss << "    \"evaluation_timestamp_utc\": \"" << cert.evaluation_timestamp_utc << "\",\n";
    ss << "    \"canonical_payload\": \"" << cert.canonical_payload << "\",\n";
    ss << "    \"sha256_digital_signature\": \"" << cert.sha256_digital_signature << "\"\n";
    ss << "  }\n";
    ss << "}\n";

    return ss.str();
}

std::string generateHumanReadableReport(const HealthCertificate& cert) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);

    ss << "================================================================================\n";
    ss << "           GOVERNMENT OF INDIA - NITI AAYOG / MoRTH BATTERY PASSPORT\n";
    ss << "           BPAN TIER 3 BATTERY HEALTH & VALUATION CERTIFICATE\n";
    ss << "================================================================================\n";
    ss << "1. PACK REGISTRATION & METADATA\n";
    ss << "   BPAN ID              : " << cert.metadata.bpan_id << "\n";
    ss << "   Serial Number        : " << cert.metadata.serial_number << "\n";
    ss << "   Vehicle Category     : " << cert.metadata.vehicle_category << "\n";
    ss << "   Cell Chemistry       : " << cert.metadata.chemistry << " (" << cert.metadata.series_cells << "S Pack)\n";
    ss << "   Nominal Rating       : " << cert.metadata.nominal_capacity_ah << " Ah / " << cert.metadata.nominal_voltage_v << " V\n\n";

    ss << "2. CERTIFIED HEALTH VALUATION (BPAN TIER 3)\n";
    ss << "   Certified SoH        : " << cert.certified_soh_pct << " %\n";
    ss << "   Usable Pack Capacity : " << cert.usable_capacity_ah << " Ah\n";
    ss << "   Lifetime Usage (EFC) : " << cert.lifetime_efc << " cycles\n";
    ss << "   Projected RUL        : " << cert.remaining_useful_life_cycles << " cycles (to 70.0% EoL)\n\n";

    ss << "3. CONFORMAL UNCERTAINTY & GATE 3 SAFETY\n";
    ss << "   90% Confidence Bounds: [" << cert.soh_lower_bound_pct << " %, " << cert.soh_upper_bound_pct << " %]\n";
    ss << "   Interval Width       : " << cert.confidence_interval_width_pct << " % (Threshold <= 5.00 %)\n";
    ss << "   Statistical Coverage : " << (cert.coverage_level * 100.0) << " % Finite-Sample Guaranteed\n\n";

    ss << "4. DEGRADATION & THERMAL DIAGNOSTICS\n";
    ss << "   Primary Degradation  : " << cert.primary_degradation_mode << "\n";
    ss << "   Loss of Li Inventory : " << cert.lli_percent << " % (LLI)\n";
    ss << "   Loss of Active Mat.  : " << cert.lam_percent << " % (LAM)\n";
    ss << "   Arrhenius TDI Multi. : " << cert.thermal_degradation_index << "x\n";
    ss << "   Operating Temperature: Mean " << cert.mean_temperature_c << " °C | Peak " << cert.max_temperature_c << " °C\n\n";

    ss << "5. SENSOR HEALTH & HARDWARE CALIBRATION (GATE 2)\n";
    ss << "   Sensor Offset Bias   : " << std::setprecision(4) << cert.sensor_offset_bias_a << " A (Limit <= 1.5000 A)\n";
    ss << "   Sensor Gain Factor   : " << std::setprecision(4) << cert.sensor_gain_factor << " (Limit error <= 0.0800)\n\n";

    ss << "6. OFFICIAL CERTIFICATION DECISION\n";
    ss << "   Status               : [" << cert.certification_decision << "] " << (cert.is_certified ? "PASS" : "ACTION REQUIRED") << "\n";
    ss << "   Decision Rationale   : " << cert.decision_rationale << "\n\n";

    ss << "================================================================================\n";
    ss << "7. CRYPTOGRAPHIC IMMUTABILITY & AUDIT PROOF (FIPS 180-2 SHA-256)\n";
    ss << "   Timestamp (UTC)      : " << cert.evaluation_timestamp_utc << "\n";
    ss << "   Canonical Payload    : " << cert.canonical_payload << "\n";
    ss << "   Digital Signature    : " << cert.sha256_digital_signature << "\n";
    ss << "================================================================================\n";

    return ss.str();
}

common::Result<HealthCertificate> generateHealthCertificate(
    const common::InternalTimeSeries& series,
    const BpanMetadata& metadata,
    double lifetime_efc
) {
    if (series.samples.empty()) {
        return std::unexpected(common::PipelineError{
            .module = "output",
            .code = "EMPTY_SERIES",
            .message = "Input time series is empty",
            .context = ""
        });
    }

    HealthCertificate cert;
    cert.metadata = metadata;
    cert.lifetime_efc = lifetime_efc;
    cert.evaluation_timestamp_utc = getCurrentUtcTimestamp();

    // 0. Gate 1: Telemetry Quality Scoring Pre-Qualification
    quality_score::QualityConfig tqs_cfg;
    if (metadata.series_cells > 0) {
        double cell_min = (metadata.chemistry == "NMC") ? 2.5 : 2.0;
        double cell_max = (metadata.chemistry == "NMC") ? 4.35 : 3.8;
        tqs_cfg.min_plausible_voltage_v = metadata.series_cells * cell_min * 0.85;
        tqs_cfg.max_plausible_voltage_v = metadata.series_cells * cell_max * 1.15;
    }
    // Auto-detect base sampling period from series median dt if > 1.2s (e.g. NASA 2.8s or IoT 5s)
    if (series.samples.size() >= 10) {
        std::vector<double> sample_dts;
        sample_dts.reserve(std::min<size_t>(30, series.samples.size() - 1));
        for (size_t k = 1; k < std::min<size_t>(30, series.samples.size()); ++k) {
            double dt = series.samples[k].timestamp_s - series.samples[k-1].timestamp_s;
            if (dt > 0.05 && dt < 60.0) sample_dts.push_back(dt);
        }
        if (!sample_dts.empty()) {
            std::sort(sample_dts.begin(), sample_dts.end());
            double med_dt = sample_dts[sample_dts.size() / 2];
            if (med_dt > 1.2) {
                tqs_cfg.nominal_dt_s = med_dt;
            }
        }
    }
    auto tqs_report = quality_score::computeQualityScore(series, tqs_cfg);
    if (!tqs_report.gate1_passed) {
        cert.is_certified = false;
        cert.certification_decision = "AUDIT_REQUIRED";
        cert.decision_rationale = "Gate 1 Failure: " + tqs_report.diagnostic_summary;
        cert.canonical_payload = buildCanonicalPayload(cert);
        cert.sha256_digital_signature = computeSha256(cert.canonical_payload);
        return cert;
    }

    // 1. Data Cleaning
    auto clean_res = cleaning::cleanTimeSeries(series);
    if (!clean_res) {
        return std::unexpected(clean_res.error());
    }
    const auto& [cleaned_series, clean_report] = *clean_res;

    // 2. Operational Segmentation
    segmentation::SegmentationConfig seg_cfg;
    seg_cfg.rest_current_thresh_a = 0.5;
    seg_cfg.charge_current_thresh_a = 1.0;
    seg_cfg.discharge_current_thresh_a = -1.0;
    auto segs_res = segmentation::segmentRegimes(cleaned_series, seg_cfg);
    std::vector<segmentation::Segment> segments;
    if (segs_res) {
        segments = *segs_res;
    }

    // 3. Current Sensor Calibration
    calibration::CalibrationConfig calib_cfg;
    calib_cfg.nominal_capacity_ah = metadata.nominal_capacity_ah;
    calib_cfg.ocv_table = calibration::getScaledOcvTable(metadata.chemistry, metadata.series_cells);
    auto calib_res = calibration::calibrateTimeSeries(cleaned_series, segments, calib_cfg);

    common::InternalTimeSeries calibrated_series = cleaned_series;
    calibration::CalibrationReport calib_report;
    if (calib_res) {
        calibrated_series = calib_res->first;
        calib_report = calib_res->second;
    }

    cert.sensor_offset_bias_a = calib_report.estimated_offset_a;
    cert.sensor_gain_factor = calib_report.estimated_gain;

    // 4. Thermal Stress Analysis
    thermal::ThermalConfig thermal_cfg;
    thermal_cfg.nominal_capacity_ah = metadata.nominal_capacity_ah;
    auto thermal_res = thermal::analyzeThermalStress(calibrated_series, thermal_cfg);
    thermal::ThermalReport thermal_report;
    if (thermal_res) {
        thermal_report = *thermal_res;
    }

    cert.thermal_degradation_index = thermal_report.thermal_degradation_index;
    cert.mean_temperature_c = thermal_report.mean_temperature_c;
    cert.max_temperature_c = thermal_report.max_temperature_c;

    // 5. Constant Current Window Extraction
    auto cc_res = segmentation::extractConstantCurrentWindow(calibrated_series, 120.0, 1.0, seg_cfg);
    if (!cc_res) {
        // Fallback: try search with relaxed duration (60s)
        cc_res = segmentation::extractConstantCurrentWindow(calibrated_series, 60.0, 2.0, seg_cfg);
    }

    if (!cc_res) {
        cert.is_certified = false;
        cert.certification_decision = "AUDIT_REQUIRED";
        cert.decision_rationale = "No valid constant-current charging window found for electrochemical analysis.";
        cert.canonical_payload = buildCanonicalPayload(cert);
        cert.sha256_digital_signature = computeSha256(cert.canonical_payload);
        return cert;
    }

    // 6. Slicing & Denoising CC window
    auto cc_slice = segmentation::sliceSegment(calibrated_series, *cc_res);
    denoising::DenoisingConfig denoise_cfg;
    denoise_cfg.savgol_window = 15;
    denoise_cfg.savgol_order = 2;
    auto denoise_res = denoising::denoiseTimeSeries(cc_slice, denoise_cfg);
    const auto& denoised_cc = denoise_res ? *denoise_res : cc_slice;

    // 7. Incremental Capacity Analysis (ICA)
    ica_dva::IcaConfig ica_cfg;
    ica_cfg.voltage_step_v = 0.005;
    auto ica_res = ica_dva::computeIca(denoised_cc, ica_cfg);
    if (!ica_res) {
        cert.is_certified = false;
        cert.certification_decision = "AUDIT_REQUIRED";
        cert.decision_rationale = "ICA transformation failed: " + ica_res.error().message;
        cert.canonical_payload = buildCanonicalPayload(cert);
        cert.sha256_digital_signature = computeSha256(cert.canonical_payload);
        return cert;
    }

    // 8. Electrochemical Feature Extraction
    features::FeatureConfig feat_cfg;
    auto feat_res = features::extractIcaFeatures(*ica_res, feat_cfg);
    if (!feat_res) {
        cert.is_certified = false;
        cert.certification_decision = "AUDIT_REQUIRED";
        cert.decision_rationale = "Feature extraction failed: " + feat_res.error().message;
        cert.canonical_payload = buildCanonicalPayload(cert);
        cert.sha256_digital_signature = computeSha256(cert.canonical_payload);
        return cert;
    }

    // 9. State-of-Health & RUL Estimation
    estimation::EstimationInput est_in;
    est_in.ica_features = *feat_res;
    est_in.thermal_report = thermal_report;
    est_in.calibration_report = calib_report;
    est_in.cumulative_throughput_ah = calib_report.calibrated_coulomb_ah;
    est_in.equivalent_full_cycles = lifetime_efc;
    est_in.nominal_capacity_ah = metadata.nominal_capacity_ah;

    estimation::EstimationConfig est_cfg;
    est_cfg.chemistry = metadata.chemistry;
    est_cfg.baseline.nominal_capacity_ah = metadata.nominal_capacity_ah;

    auto est_res = estimation::estimateBatteryHealth(est_in, est_cfg);
    if (!est_res) {
        cert.is_certified = false;
        cert.certification_decision = "AUDIT_REQUIRED";
        cert.decision_rationale = "Health estimation failed: " + est_res.error().message;
        cert.canonical_payload = buildCanonicalPayload(cert);
        cert.sha256_digital_signature = computeSha256(cert.canonical_payload);
        return cert;
    }

    // 10. Conformal Prediction & Safety Abstention
    uncertainty::UncertaintyConfig unc_cfg;
    auto unc_res = uncertainty::quantifyUncertainty(*est_res, est_in, unc_cfg);
    if (!unc_res) {
        cert.is_certified = false;
        cert.certification_decision = "AUDIT_REQUIRED";
        cert.decision_rationale = "Uncertainty quantification failed: " + unc_res.error().message;
        cert.canonical_payload = buildCanonicalPayload(cert);
        cert.sha256_digital_signature = computeSha256(cert.canonical_payload);
        return cert;
    }

    // Populate certified metrics
    cert.certified_soh_pct = unc_res->soh_point_estimate_pct;
    cert.usable_capacity_ah = est_res->estimated_capacity_ah;
    cert.soh_lower_bound_pct = unc_res->soh_lower_bound_pct;
    cert.soh_upper_bound_pct = unc_res->soh_upper_bound_pct;
    cert.confidence_interval_width_pct = unc_res->interval_width_pct;
    cert.coverage_level = unc_res->coverage_level;
    cert.remaining_useful_life_cycles = est_res->remaining_useful_life_cycles;
    cert.lli_percent = est_res->lli_percent;
    cert.lam_percent = est_res->lam_percent;
    cert.primary_degradation_mode = estimation::degradationDominanceToString(est_res->dominance);

    // Gate 2 (Sensor Hardware Fault) & Gate 3 (Uncertainty Interval / OOD) enforcement
    if (calib_report.sensor_fault_detected) {
        cert.is_certified = false;
        cert.certification_decision = "AUDIT_REQUIRED";
        cert.decision_rationale = "Gate 2 Failure: Current sensor fault (" + calib_report.fault_message + ")";
    } else if (unc_res->decision == uncertainty::CertificationDecision::AUDIT_REQUIRED) {
        cert.is_certified = false;
        cert.certification_decision = "AUDIT_REQUIRED";
        cert.decision_rationale = "Gate 3 Failure: " + unc_res->decision_rationale;
    } else {
        cert.is_certified = true;
        cert.certification_decision = "CERTIFIED";
        cert.decision_rationale = unc_res->decision_rationale;
    }

    // 11. Cryptographic Immutability Signing (SHA-256)
    cert.canonical_payload = buildCanonicalPayload(cert);
    cert.sha256_digital_signature = computeSha256(cert.canonical_payload);

    return cert;
}

} // namespace ev::battery::output
