#include <ev/battery/thermal/thermal.hpp>
#include <algorithm>
#include <numeric>

namespace ev::battery::thermal {

common::Result<ThermalReport> analyzeThermalStress(
    const common::InternalTimeSeries& series,
    const ThermalConfig& config)
{
    if (series.samples.size() < 2) {
        return std::unexpected(common::PipelineError{
            .module = "thermal",
            .code = "INSUFFICIENT_DATA",
            .message = "Thermal analysis requires at least 2 samples",
            .context = series.session_id
        });
    }

    const auto& samples = series.samples;
    const size_t n = samples.size();

    double t_min = 1e9;
    double t_max = -1e9;
    double t_sum = 0.0;

    for (const auto& s : samples) {
        if (s.temperature_c < t_min) t_min = s.temperature_c;
        if (s.temperature_c > t_max) t_max = s.temperature_c;
        t_sum += s.temperature_c;
    }
    double t_mean = t_sum / static_cast<double>(n);

    double dwell_40 = 0.0;
    double dwell_45 = 0.0;
    double dwell_55 = 0.0;
    double total_duration_s = 0.0;

    double c_1c = (config.nominal_capacity_ah > 0.1) ? config.nominal_capacity_ah : 30.0;
    double tdi_integral = 0.0;

    // Compute stress factor S_i = k(T_i) * (1 + gamma * |I_i| / 1C)
    auto compute_stress = [&](const common::Sample& s) {
        double k = arrheniusAcceleration(s.temperature_c, config.activation_energy_j_mol, config.reference_temp_c);
        double c_factor = 1.0 + config.current_weight_gamma * (std::abs(s.current_a) / c_1c);
        return k * c_factor;
    };

    double prev_stress = compute_stress(samples[0]);

    for (size_t i = 1; i < n; ++i) {
        double dt = samples[i].timestamp_s - samples[i - 1].timestamp_s;
        if (dt < 0.0) dt = 0.0;
        if (dt > 3600.0) dt = 3600.0; // Clamp large disconnections

        total_duration_s += dt;

        double avg_t = 0.5 * (samples[i - 1].temperature_c + samples[i].temperature_c);
        if (avg_t >= 40.0) dwell_40 += dt;
        if (avg_t >= 45.0) dwell_45 += dt;
        if (avg_t >= 55.0) dwell_55 += dt;

        double curr_stress = compute_stress(samples[i]);
        tdi_integral += 0.5 * (prev_stress + curr_stress) * dt;
        prev_stress = curr_stress;
    }

    double tdi = (total_duration_s > 0.0) ? (tdi_integral / total_duration_s) : 1.0;
    double frac_40 = (total_duration_s > 0.0) ? (dwell_40 / total_duration_s) : 0.0;
    double frac_45 = (total_duration_s > 0.0) ? (dwell_45 / total_duration_s) : 0.0;

    // Peak heating rate (dT/dt) in °C/min using sliding window
    double max_heating_rate = 0.0;
    double target_window = (config.dt_window_s >= 5.0) ? config.dt_window_s : 60.0;
    size_t right_idx = 0;

    for (size_t left_idx = 0; left_idx < n; ++left_idx) {
        while (right_idx < n && (samples[right_idx].timestamp_s - samples[left_idx].timestamp_s) < target_window) {
            ++right_idx;
        }
        if (right_idx >= n) break;

        double dt_span_s = samples[right_idx].timestamp_s - samples[left_idx].timestamp_s;
        if (dt_span_s >= 10.0) {
            double dt_span_min = dt_span_s / 60.0;
            double rate = (samples[right_idx].temperature_c - samples[left_idx].temperature_c) / dt_span_min;
            if (rate > max_heating_rate) {
                max_heating_rate = rate;
            }
        }
    }

    // Determine thermal severity category
    ThermalSeverity sev = ThermalSeverity::NORMAL;
    if (tdi >= 4.0 || t_max >= 55.0) {
        sev = ThermalSeverity::CRITICAL;
    } else if (tdi >= 2.5 || frac_45 >= 0.20 || t_max >= 48.0) {
        sev = ThermalSeverity::SEVERE;
    } else if (tdi >= 1.5 || frac_40 >= 0.10) {
        sev = ThermalSeverity::ELEVATED;
    }

    return ThermalReport{
        .mean_temperature_c = t_mean,
        .max_temperature_c = t_max,
        .min_temperature_c = t_min,
        .dwell_time_above_40c_s = dwell_40,
        .dwell_time_above_45c_s = dwell_45,
        .dwell_time_above_55c_s = dwell_55,
        .dwell_fraction_above_40c = frac_40,
        .dwell_fraction_above_45c = frac_45,
        .thermal_degradation_index = tdi,
        .max_heating_rate_c_per_min = max_heating_rate,
        .severity = sev
    };
}

} // namespace ev::battery::thermal
