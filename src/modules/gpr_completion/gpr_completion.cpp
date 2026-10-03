#include <ev/battery/gpr_completion/gpr_completion.hpp>

#include <Eigen/Dense>

#include <algorithm>
#include <cmath>
#include <vector>

namespace ev::battery::gpr_completion {

namespace {

struct DataPoint {
    double voltage_v;
    double charge_ah;
};

} // namespace

common::Result<GprResult> reconstructChargeCurve(
    const common::InternalTimeSeries& cc_segment,
    const GprConfig& config
) {
    if (cc_segment.samples.empty() || cc_segment.samples.size() < 2) {
        return std::unexpected(common::PipelineError{
            .module = "gpr_completion",
            .code = "EMPTY_INPUT",
            .message = "Constant-current segment has insufficient samples for GPR",
            .context = ""
        });
    }

    const auto& samples = cc_segment.samples;
    const size_t n_raw = samples.size();

    // 1. Calculate cumulative Ah throughput
    std::vector<DataPoint> points;
    points.reserve(n_raw);

    double cum_q = 0.0;
    points.push_back({.voltage_v = samples[0].voltage_v, .charge_ah = 0.0});

    for (size_t i = 1; i < n_raw; ++i) {
        double dt = samples[i].timestamp_s - samples[i - 1].timestamp_s;
        if (dt < 0.0) dt = 0.0;
        double avg_i = 0.5 * (samples[i].current_a + samples[i - 1].current_a);
        cum_q += (avg_i * dt) / 3600.0; // Ah
        points.push_back({.voltage_v = samples[i].voltage_v, .charge_ah = cum_q});
    }

    // 2. Sort by voltage and filter non-monotonic points
    std::sort(points.begin(), points.end(), [](const DataPoint& a, const DataPoint& b) {
        return a.voltage_v < b.voltage_v;
    });

    std::vector<DataPoint> filtered;
    filtered.reserve(points.size());
    filtered.push_back(points[0]);

    for (size_t i = 1; i < points.size(); ++i) {
        if (points[i].voltage_v - filtered.back().voltage_v > 1e-4) {
            filtered.push_back(points[i]);
        }
    }

    if (filtered.size() < 2) {
        return std::unexpected(common::PipelineError{
            .module = "gpr_completion",
            .code = "INSUFFICIENT_DISTINCT_VOLTAGES",
            .message = "Voltage variation in segment is negligible",
            .context = ""
        });
    }

    // 3. Downsample to at most max_support_points for O(N^3) bounded Cholesky
    std::vector<DataPoint> support;
    size_t n_filtered = filtered.size();
    if (n_filtered <= config.max_support_points) {
        support = filtered;
    } else {
        support.reserve(config.max_support_points);
        double step = static_cast<double>(n_filtered - 1) / static_cast<double>(config.max_support_points - 1);
        for (size_t i = 0; i < config.max_support_points; ++i) {
            size_t idx = static_cast<size_t>(std::round(static_cast<double>(i) * step));
            if (idx >= n_filtered) idx = n_filtered - 1;
            support.push_back(filtered[idx]);
        }
    }

    const size_t n_train = support.size();

    // 4. Adapt scales for Cell vs. Multi-Cell Pack
    double v_mean = 0.0;
    for (const auto& pt : support) v_mean += pt.voltage_v;
    v_mean /= static_cast<double>(n_train);

    double pack_multiplier = (v_mean > 10.0) ? 16.0 : 1.0;
    double ell = config.length_scale * pack_multiplier;
    double v_step = config.voltage_step_v * pack_multiplier;
    double target_min_v = config.min_extrapolation_v * pack_multiplier;
    double target_max_v = config.max_extrapolation_v * pack_multiplier;

    // Harmonize target boundaries with observed limits
    target_min_v = std::min(target_min_v, support.front().voltage_v);
    target_max_v = std::max(target_max_v, support.back().voltage_v);

    // 4b. Linear Prior Mean Function: mu(V) = m * V + c
    double v_span = support.back().voltage_v - support.front().voltage_v;
    double q_span = support.back().charge_ah - support.front().charge_ah;
    double m_prior = (v_span > 1e-4) ? (q_span / v_span) : 0.0;
    double c_prior = support.front().charge_ah - m_prior * support.front().voltage_v;

    auto prior_mean = [&](double v) -> double {
        return m_prior * v + c_prior;
    };

    // 5. Construct Covariance Matrix K in Eigen & demean targets
    Eigen::MatrixXd K(n_train, n_train);
    Eigen::VectorXd y(n_train);

    const double l_sq_2 = 2.0 * ell * ell;
    const double sigma_f_sq = config.signal_variance;
    const double sigma_n_sq = config.noise_variance;

    for (size_t i = 0; i < n_train; ++i) {
        y(static_cast<Eigen::Index>(i)) = support[i].charge_ah - prior_mean(support[i].voltage_v);
        for (size_t j = i; j < n_train; ++j) {
            double dv = support[i].voltage_v - support[j].voltage_v;
            double cov = sigma_f_sq * std::exp(-(dv * dv) / l_sq_2);
            if (i == j) {
                cov += sigma_n_sq; // Jitter / noise regularization
            }
            K(static_cast<Eigen::Index>(i), static_cast<Eigen::Index>(j)) = cov;
            K(static_cast<Eigen::Index>(j), static_cast<Eigen::Index>(i)) = cov;
        }
    }

    // 6. Cholesky Decomposition (LLT)
    Eigen::LLT<Eigen::MatrixXd> llt(K);
    if (llt.info() != Eigen::Success) {
        return std::unexpected(common::PipelineError{
            .module = "gpr_completion",
            .code = "CHOLESKY_FAILED",
            .message = "Covariance matrix factorization failed (non-positive-definite)",
            .context = ""
        });
    }

    Eigen::VectorXd alpha = llt.solve(y);

    // 7. Predictive Inference on Target Grid
    GprResult result;
    result.is_reconstructed = true;

    size_t num_grid_pts = static_cast<size_t>(std::ceil((target_max_v - target_min_v) / v_step)) + 1;
    result.voltage_grid_v.reserve(num_grid_pts);
    result.mean_charge_ah.reserve(num_grid_pts);
    result.variance_ah2.reserve(num_grid_pts);
    result.lower_bound_ah.reserve(num_grid_pts);
    result.upper_bound_ah.reserve(num_grid_pts);

    Eigen::VectorXd k_star(n_train);

    for (size_t g = 0; g < num_grid_pts; ++g) {
        double v_star = target_min_v + static_cast<double>(g) * v_step;
        if (v_star > target_max_v) v_star = target_max_v;

        for (size_t i = 0; i < n_train; ++i) {
            double dv = v_star - support[i].voltage_v;
            k_star(static_cast<Eigen::Index>(i)) = sigma_f_sq * std::exp(-(dv * dv) / l_sq_2);
        }

        double res_pred = k_star.dot(alpha);
        double mean_pred = prior_mean(v_star) + res_pred;

        // Predictive variance: sigma_f^2 - k_*^T * K^-1 * k_*
        Eigen::VectorXd v_solve = llt.matrixL().solve(k_star);
        double var_pred = sigma_f_sq - v_solve.squaredNorm();
        if (var_pred < 0.0) var_pred = 0.0;
        double std_pred = std::sqrt(var_pred);

        result.voltage_grid_v.push_back(v_star);
        result.mean_charge_ah.push_back(mean_pred);
        result.variance_ah2.push_back(var_pred);
        result.lower_bound_ah.push_back(mean_pred - 1.96 * std_pred);
        result.upper_bound_ah.push_back(mean_pred + 1.96 * std_pred);

        if (std_pred > result.max_uncertainty_ah) {
            result.max_uncertainty_ah = std_pred;
        }

        if (v_star >= target_max_v) break;
    }

    // 8. Enforce Physical Monotonicity of Cumulative Charge
    for (size_t i = 1; i < result.mean_charge_ah.size(); ++i) {
        if (result.mean_charge_ah[i] < result.mean_charge_ah[i - 1]) {
            result.mean_charge_ah[i] = result.mean_charge_ah[i - 1];
        }
        if (result.lower_bound_ah[i] < result.lower_bound_ah[i - 1]) {
            result.lower_bound_ah[i] = result.lower_bound_ah[i - 1];
        }
        if (result.upper_bound_ah[i] < result.upper_bound_ah[i - 1]) {
            result.upper_bound_ah[i] = result.upper_bound_ah[i - 1];
        }
    }

    return result;
}

common::InternalTimeSeries synthesizeCompletedTimeSeries(
    const GprResult& gpr,
    double charge_current_a,
    double nominal_temp_c
) {
    common::InternalTimeSeries series;
    const size_t n = gpr.voltage_grid_v.size();
    series.samples.reserve(n);

    double safe_i = (std::abs(charge_current_a) > 1e-3) ? charge_current_a : 15.0;

    for (size_t i = 0; i < n; ++i) {
        double q_ah = gpr.mean_charge_ah[i];
        double t_s = (q_ah / safe_i) * 3600.0;

        series.samples.push_back({
            .timestamp_s = t_s,
            .voltage_v = gpr.voltage_grid_v[i],
            .current_a = safe_i,
            .temperature_c = nominal_temp_c
        });
    }

    return series;
}

} // namespace ev::battery::gpr_completion
