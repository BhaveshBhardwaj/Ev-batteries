#include <ev/battery/quality_score/quality_score.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <vector>

namespace ev::battery::quality_score {

QualityReport computeQualityScore(
    const common::InternalTimeSeries& series,
    const QualityConfig& config
) {
    QualityReport report;
    const size_t n = series.samples.size();

    if (n < 5) {
        report.total_score = 0.0;
        report.quality_label = common::QualityLabel::REJECT;
        report.gate1_passed = false;
        report.diagnostic_summary = "Insufficient samples for quality assessment (N < 5).";
        return report;
    }

    // ─────────────────────────────────────────────────────────────
    // 1. Sampling Jitter & Dropout Analysis
    // ─────────────────────────────────────────────────────────────
    double sum_dt = 0.0;
    double sum_sq_dt = 0.0;
    double total_missing_s = 0.0;
    double total_span_s = series.samples.back().timestamp_s - series.samples.front().timestamp_s;
    if (total_span_s <= 0.0) total_span_s = 1.0;

    size_t dt_count = n - 1;
    for (size_t i = 1; i < n; ++i) {
        double dt = series.samples[i].timestamp_s - series.samples[i - 1].timestamp_s;
        if (dt < 0.0) dt = 0.0; // Inverted timestamp protection

        sum_dt += dt;
        sum_sq_dt += dt * dt;

        if (dt > 1.5 * config.nominal_dt_s) {
            total_missing_s += (dt - config.nominal_dt_s);
        }
    }

    double mean_dt = sum_dt / static_cast<double>(dt_count);
    double var_dt = (sum_sq_dt / static_cast<double>(dt_count)) - (mean_dt * mean_dt);
    if (var_dt < 0.0) var_dt = 0.0;
    double std_dt = std::sqrt(var_dt);

    report.sampling_jitter_cv = (mean_dt > 1e-6) ? (std_dt / mean_dt) : 1.0;
    report.dropout_fraction = std::clamp(total_missing_s / total_span_s, 0.0, 1.0);

    // Jitter score in [0, 25]: 25 pts for CV <= 0.05, 0 pts for CV >= 0.55
    report.jitter_score = 25.0 * std::clamp(1.0 - 2.0 * report.sampling_jitter_cv, 0.0, 1.0);

    // Dropout score in [0, 30]: 30 pts for 0% drop, 0 pts for >= 20% drop
    report.dropout_score = 30.0 * std::clamp(1.0 - 5.0 * report.dropout_fraction, 0.0, 1.0);

    // ─────────────────────────────────────────────────────────────
    // 2. Physical Sensor Plausibility Analysis
    // ─────────────────────────────────────────────────────────────
    size_t valid_samples = 0;
    for (const auto& sample : series.samples) {
        bool v_ok = (sample.voltage_v >= config.min_plausible_voltage_v &&
                     sample.voltage_v <= config.max_plausible_voltage_v);
        bool i_ok = (std::abs(sample.current_a) <= config.max_plausible_current_a);
        bool t_ok = (sample.temperature_c >= config.min_plausible_temp_c &&
                     sample.temperature_c <= config.max_plausible_temp_c);

        if (v_ok && i_ok && t_ok) {
            valid_samples++;
        }
    }

    report.plausible_fraction = static_cast<double>(valid_samples) / static_cast<double>(n);
    report.plausibility_score = 25.0 * report.plausible_fraction;

    // ─────────────────────────────────────────────────────────────
    // 3. High-Frequency Noise & Signal Roughness Analysis
    // ─────────────────────────────────────────────────────────────
    double sum_sq_d2v = 0.0;
    size_t d2_count = 0;

    for (size_t i = 1; i + 1 < n; ++i) {
        // Exclude large transient current steps (vehicle acceleration/braking)
        if (std::abs(series.samples[i + 1].current_a - series.samples[i - 1].current_a) <= 1.0) {
            double d2v = series.samples[i + 1].voltage_v - 2.0 * series.samples[i].voltage_v + series.samples[i - 1].voltage_v;
            sum_sq_d2v += d2v * d2v;
            d2_count++;
        }
    }

    if (d2_count == 0) {
        report.voltage_noise_std = 0.0;
    } else {
        report.voltage_noise_std = std::sqrt(sum_sq_d2v / static_cast<double>(d2_count));
    }
    // Full points for noise std <= 0.02V, degrades as noise approaches 0.12V
    report.snr_score = 20.0 * std::clamp(1.0 - 8.0 * report.voltage_noise_std, 0.0, 1.0);

    // ─────────────────────────────────────────────────────────────
    // 4. Composite TQS & Gate 1 Decision
    // ─────────────────────────────────────────────────────────────
    double base_score = report.jitter_score + report.dropout_score + report.plausibility_score + report.snr_score;
    // Scale composite score by telemetry completeness (1.0 - dropout_fraction)
    report.total_score = std::clamp(base_score * (1.0 - report.dropout_fraction), 0.0, 100.0);

    if (report.total_score >= 85.0) {
        report.quality_label = common::QualityLabel::EXCELLENT;
    } else if (report.total_score >= 65.0) {
        report.quality_label = common::QualityLabel::ACCEPTABLE;
    } else if (report.total_score >= 30.0) {
        report.quality_label = common::QualityLabel::MARGINAL;
    } else {
        report.quality_label = common::QualityLabel::REJECT;
    }

    report.gate1_passed = (report.total_score >= config.gate1_min_score);

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << "TQS: " << report.total_score << "/100 ("
       << qualityLabelToString(report.quality_label) << ") | "
       << "Jitter: " << report.jitter_score << "/25, "
       << "Drop: " << report.dropout_score << "/30 (" << (report.dropout_fraction * 100.0) << "%), "
       << "Plaus: " << report.plausibility_score << "/25 (" << (report.plausible_fraction * 100.0) << "%), "
       << "SNR: " << report.snr_score << "/20";

    if (!report.gate1_passed) {
        ss << " -> GATE 1 REJECT (Quality score < " << config.gate1_min_score << ").";
    }

    report.diagnostic_summary = ss.str();
    return report;
}

} // namespace ev::battery::quality_score
