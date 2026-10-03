#include <ev/battery/ica_dva/ica_dva.hpp>
#include <algorithm>
#include <cmath>

namespace ev::battery::ica_dva {

common::Result<IcaCurve> computeIca(
    const common::InternalTimeSeries& cc_segment,
    const IcaConfig& config)
{
    if (cc_segment.samples.size() < 10) {
        return std::unexpected(common::PipelineError{
            .module = "ica_dva",
            .code = "INSUFFICIENT_DATA",
            .message = "CC segment has fewer than 10 samples for ICA",
            .context = cc_segment.session_id
        });
    }

    const auto& samples = cc_segment.samples;

    // 1. Compute cumulative charge Q(t) in Ampere-hours (Ah)
    std::vector<double> raw_q(samples.size(), 0.0);
    for (size_t i = 1; i < samples.size(); ++i) {
        double dt_s = samples[i].timestamp_s - samples[i - 1].timestamp_s;
        if (dt_s < 0.0) dt_s = 0.0;
        double avg_i_a = 0.5 * (samples[i].current_a + samples[i - 1].current_a);
        raw_q[i] = raw_q[i - 1] + (avg_i_a * dt_s) / 3600.0;
    }

    // 2. Extract strictly monotonic (V, Q) pairs to prepare for Steffen interpolation
    std::vector<double> mono_v;
    std::vector<double> mono_q;
    mono_v.reserve(samples.size());
    mono_q.reserve(samples.size());

    double last_v = -1e9;
    for (size_t i = 0; i < samples.size(); ++i) {
        double v = samples[i].voltage_v;
        if (v > last_v + 1e-4) {
            mono_v.push_back(v);
            mono_q.push_back(raw_q[i]);
            last_v = v;
        }
    }

    if (mono_v.size() < 5) {
        return std::unexpected(common::PipelineError{
            .module = "ica_dva",
            .code = "NON_MONOTONIC_VOLTAGE",
            .message = "Voltage curve lacks sufficient strictly increasing samples",
            .context = cc_segment.session_id
        });
    }

    double v_span = mono_v.back() - mono_v.front();
    if (v_span < config.min_voltage_span_v) {
        return std::unexpected(common::PipelineError{
            .module = "ica_dva",
            .code = "VOLTAGE_SPAN_TOO_SMALL",
            .message = "CC charging voltage span is below minimum threshold",
            .context = "Span: " + std::to_string(v_span) + " V"
        });
    }

    // 3. Generate uniform voltage grid V_grid
    double v_step = (config.voltage_step_v > 0.0001) ? config.voltage_step_v : 0.005;
    double v_start = std::ceil(mono_v.front() / v_step) * v_step;
    double v_end = std::floor(mono_v.back() / v_step) * v_step;

    if (v_end <= v_start) {
        return std::unexpected(common::PipelineError{
            .module = "ica_dva",
            .code = "GRID_GENERATION_FAILED",
            .message = "Invalid voltage grid bounds",
            .context = "v_start: " + std::to_string(v_start) + ", v_end: " + std::to_string(v_end)
        });
    }

    size_t num_grid_points = static_cast<size_t>(std::round((v_end - v_start) / v_step)) + 1;
    int sg_window = config.savgol_window;
    if (sg_window % 2 == 0) sg_window += 1;
    if (sg_window < 5) sg_window = 5;

    if (num_grid_points < static_cast<size_t>(sg_window)) {
        return std::unexpected(common::PipelineError{
            .module = "ica_dva",
            .code = "GRID_TOO_COARSE",
            .message = "Grid has fewer points than Savitzky-Golay window size",
            .context = "Points: " + std::to_string(num_grid_points) + ", Window: " + std::to_string(sg_window)
        });
    }

    std::vector<double> v_grid(num_grid_points);
    for (size_t i = 0; i < num_grid_points; ++i) {
        v_grid[i] = v_start + static_cast<double>(i) * v_step;
    }

    // 4. Monotone cubic spline interpolation (Steffen 1990) of Q on V_grid
    std::vector<double> q_grid(num_grid_points, 0.0);
    auto interp_res = math_utils::monotoneCubicInterp(mono_v, mono_q, v_grid, q_grid);
    if (!interp_res.has_value()) {
        return std::unexpected(common::PipelineError{
            .module = "ica_dva",
            .code = "INTERPOLATION_FAILED",
            .message = "Steffen monotone interpolation failed on V_grid",
            .context = cc_segment.session_id
        });
    }

    // 5. Numerical differentiation via Savitzky-Golay derivative filter
    std::vector<double> dq_dv(num_grid_points, 0.0);
    int poly_order = (config.savgol_poly_order >= 2 && config.savgol_poly_order < sg_window)
                         ? config.savgol_poly_order : 2;

    auto sg_res = math_utils::savgolDerivative(
        q_grid,
        dq_dv,
        sg_window,
        poly_order,
        1,       // 1st derivative: dQ/dV
        v_step   // Physical grid spacing scaling
    );

    if (!sg_res.has_value()) {
        return std::unexpected(common::PipelineError{
            .module = "ica_dva",
            .code = "DERIVATIVE_FAILED",
            .message = "Savitzky-Golay derivative computation failed",
            .context = cc_segment.session_id
        });
    }

    // 6. Identify dominant peak coordinates (avoiding boundary window margin)
    size_t margin = static_cast<size_t>(sg_window);
    if (num_grid_points <= 2 * margin + 2) {
        margin = static_cast<size_t>(sg_window / 2);
    }
    double max_val = -1e9;
    double peak_v = v_grid[0];

    for (size_t i = margin; i < num_grid_points - margin; ++i) {
        if (dq_dv[i] > max_val) {
            max_val = dq_dv[i];
            peak_v = v_grid[i];
        }
    }

    return IcaCurve{
        .voltage_v = std::move(v_grid),
        .charge_ah = std::move(q_grid),
        .dq_dv = std::move(dq_dv),
        .peak_voltage_v = peak_v,
        .max_dq_dv = max_val
    };
}

common::Result<DvaCurve> computeDva(
    const common::InternalTimeSeries& cc_segment,
    const DvaConfig& config)
{
    if (cc_segment.samples.size() < 10) {
        return std::unexpected(common::PipelineError{
            .module = "ica_dva",
            .code = "INSUFFICIENT_DATA",
            .message = "CC segment has fewer than 10 samples for DVA",
            .context = cc_segment.session_id
        });
    }

    const auto& samples = cc_segment.samples;

    // 1. Compute cumulative charge Q(t) in Ah
    std::vector<double> raw_q(samples.size(), 0.0);
    for (size_t i = 1; i < samples.size(); ++i) {
        double dt_s = samples[i].timestamp_s - samples[i - 1].timestamp_s;
        if (dt_s < 0.0) dt_s = 0.0;
        double avg_i_a = 0.5 * (samples[i].current_a + samples[i - 1].current_a);
        raw_q[i] = raw_q[i - 1] + (avg_i_a * dt_s) / 3600.0;
    }

    // 2. Extract strictly monotonic (Q, V) pairs
    std::vector<double> mono_q;
    std::vector<double> mono_v;
    mono_q.reserve(samples.size());
    mono_v.reserve(samples.size());

    double last_q = -1e9;
    for (size_t i = 0; i < samples.size(); ++i) {
        double q = raw_q[i];
        if (q > last_q + 1e-5) {
            mono_q.push_back(q);
            mono_v.push_back(samples[i].voltage_v);
            last_q = q;
        }
    }

    if (mono_q.size() < 5) {
        return std::unexpected(common::PipelineError{
            .module = "ica_dva",
            .code = "NON_MONOTONIC_CAPACITY",
            .message = "Charge curve lacks sufficient strictly increasing samples",
            .context = cc_segment.session_id
        });
    }

    double q_span = mono_q.back() - mono_q.front();
    if (q_span < config.min_capacity_span_ah) {
        return std::unexpected(common::PipelineError{
            .module = "ica_dva",
            .code = "CAPACITY_SPAN_TOO_SMALL",
            .message = "Charge capacity span is below minimum threshold",
            .context = "Span: " + std::to_string(q_span) + " Ah"
        });
    }

    // 3. Generate uniform capacity grid Q_grid
    double q_step = (config.capacity_step_ah > 0.0001) ? config.capacity_step_ah : 0.01;
    double q_start = std::ceil(mono_q.front() / q_step) * q_step;
    double q_end = std::floor(mono_q.back() / q_step) * q_step;

    if (q_end <= q_start) {
        return std::unexpected(common::PipelineError{
            .module = "ica_dva",
            .code = "GRID_GENERATION_FAILED",
            .message = "Invalid capacity grid bounds",
            .context = "q_start: " + std::to_string(q_start) + ", q_end: " + std::to_string(q_end)
        });
    }

    size_t num_grid_points = static_cast<size_t>(std::round((q_end - q_start) / q_step)) + 1;
    int sg_window = config.savgol_window;
    if (sg_window % 2 == 0) sg_window += 1;
    if (sg_window < 5) sg_window = 5;

    if (num_grid_points < static_cast<size_t>(sg_window)) {
        return std::unexpected(common::PipelineError{
            .module = "ica_dva",
            .code = "GRID_TOO_COARSE",
            .message = "Capacity grid has fewer points than SG window",
            .context = "Points: " + std::to_string(num_grid_points)
        });
    }

    std::vector<double> q_grid(num_grid_points);
    for (size_t i = 0; i < num_grid_points; ++i) {
        q_grid[i] = q_start + static_cast<double>(i) * q_step;
    }

    // 4. Monotone cubic spline interpolation of V on Q_grid
    std::vector<double> v_grid(num_grid_points, 0.0);
    auto interp_res = math_utils::monotoneCubicInterp(mono_q, mono_v, q_grid, v_grid);
    if (!interp_res.has_value()) {
        return std::unexpected(common::PipelineError{
            .module = "ica_dva",
            .code = "INTERPOLATION_FAILED",
            .message = "Steffen monotone interpolation failed on Q_grid",
            .context = cc_segment.session_id
        });
    }

    // 5. Numerical differentiation via Savitzky-Golay
    std::vector<double> dv_dq(num_grid_points, 0.0);
    int poly_order = (config.savgol_poly_order >= 2 && config.savgol_poly_order < sg_window)
                         ? config.savgol_poly_order : 2;

    auto sg_res = math_utils::savgolDerivative(
        v_grid,
        dv_dq,
        sg_window,
        poly_order,
        1,       // 1st derivative: dV/dQ
        q_step   // Physical grid spacing scaling
    );

    if (!sg_res.has_value()) {
        return std::unexpected(common::PipelineError{
            .module = "ica_dva",
            .code = "DERIVATIVE_FAILED",
            .message = "Savitzky-Golay derivative computation failed for DVA",
            .context = cc_segment.session_id
        });
    }

    return DvaCurve{
        .capacity_ah = std::move(q_grid),
        .voltage_v = std::move(v_grid),
        .dv_dq = std::move(dv_dq)
    };
}

} // namespace ev::battery::ica_dva
