#include <ev/battery/features/features.hpp>
#include <ev/battery/math_utils/calculus.hpp>
#include <algorithm>
#include <cmath>

namespace ev::battery::features {

namespace {

struct PeakCandidate {
    size_t index = 0;
    double voltage_v = 0.0;
    double height = 0.0;
    double prominence = 0.0;
};

// Compute topographic prominence of a candidate local maximum
double computeProminence(const std::vector<double>& y, size_t p) {
    const size_t n = y.size();
    if (p == 0 || p >= n - 1) return 0.0;

    double peak_val = y[p];

    // Scan left: find minimum before reaching any point strictly higher than peak_val
    double left_min = peak_val;
    for (size_t i = p; i-- > 0; ) {
        if (y[i] < left_min) {
            left_min = y[i];
        }
        if (y[i] > peak_val) {
            break;
        }
    }

    // Scan right: find minimum before reaching any point strictly higher than peak_val
    double right_min = peak_val;
    for (size_t i = p + 1; i < n; ++i) {
        if (y[i] < right_min) {
            right_min = y[i];
        }
        if (y[i] > peak_val) {
            break;
        }
    }

    double col_base = std::max(left_min, right_min);
    return std::max(0.0, peak_val - col_base);
}

} // namespace

common::Result<ElectrochemicalFeatures> extractIcaFeatures(
    const ica_dva::IcaCurve& ica,
    const FeatureConfig& config)
{
    const size_t n = ica.voltage_v.size();
    if (n < 10 || ica.dq_dv.size() != n) {
        return std::unexpected(common::PipelineError{
            .module = "features",
            .code = "INSUFFICIENT_DATA",
            .message = "ICA curve has fewer than 10 points or mismatched dimensions",
            .context = "Points: " + std::to_string(n)
        });
    }

    // 1. Locate global maximum and establish prominence threshold
    double global_max = -1e9;
    size_t global_max_idx = 1;
    for (size_t i = 1; i < n - 1; ++i) {
        if (ica.dq_dv[i] > global_max) {
            global_max = ica.dq_dv[i];
            global_max_idx = i;
        }
    }

    double min_prominence = config.min_prominence_ratio * global_max;
    if (min_prominence < 1e-4) min_prominence = 1e-4;

    // 2. Identify candidate local maxima and calculate topographic prominence
    std::vector<PeakCandidate> candidates;
    for (size_t i = 1; i < n - 1; ++i) {
        if (ica.dq_dv[i] > ica.dq_dv[i - 1] && ica.dq_dv[i] >= ica.dq_dv[i + 1]) {
            double prom = computeProminence(ica.dq_dv, i);
            if (prom >= min_prominence) {
                candidates.push_back({
                    .index = i,
                    .voltage_v = ica.voltage_v[i],
                    .height = ica.dq_dv[i],
                    .prominence = prom
                });
            }
        }
    }

    // Fall back to global maximum if no peak passed prominence threshold
    if (candidates.empty()) {
        candidates.push_back({
            .index = global_max_idx,
            .voltage_v = ica.voltage_v[global_max_idx],
            .height = global_max,
            .prominence = global_max
        });
    }

    // 3. Sort peaks by prominence descending
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return a.prominence > b.prominence;
    });

    ElectrochemicalFeatures feat;
    feat.primary_peak_voltage_v = candidates[0].voltage_v;
    feat.primary_peak_height = candidates[0].height;

    // Secondary peak and intervening valley
    if (candidates.size() >= 2) {
        feat.secondary_peak_voltage_v = candidates[1].voltage_v;
        feat.secondary_peak_height = candidates[1].height;
        feat.has_secondary_peak = true;

        size_t low_idx = std::min(candidates[0].index, candidates[1].index);
        size_t high_idx = std::max(candidates[0].index, candidates[1].index);

        double min_val = 1e9;
        size_t min_idx = low_idx;
        for (size_t i = low_idx; i <= high_idx; ++i) {
            if (ica.dq_dv[i] < min_val) {
                min_val = ica.dq_dv[i];
                min_idx = i;
            }
        }
        feat.valley_voltage_v = ica.voltage_v[min_idx];
        feat.valley_height = min_val;
    }

    // 4. Calculate integrated peak area A_p1 within [V_p1 - half_width, V_p1 + half_width]
    double v_min_window = feat.primary_peak_voltage_v - config.peak_area_half_width_v;
    double v_max_window = feat.primary_peak_voltage_v + config.peak_area_half_width_v;

    std::vector<double> sub_v;
    std::vector<double> sub_dq_dv;
    sub_v.reserve(n);
    sub_dq_dv.reserve(n);

    for (size_t i = 0; i < n; ++i) {
        if (ica.voltage_v[i] >= v_min_window && ica.voltage_v[i] <= v_max_window) {
            sub_v.push_back(ica.voltage_v[i]);
            sub_dq_dv.push_back(ica.dq_dv[i]);
        }
    }

    if (sub_v.size() >= 2) {
        auto area_res = math_utils::trapz(sub_v, sub_dq_dv);
        if (area_res.has_value()) {
            feat.primary_peak_area_ah = *area_res;
        }
    }

    // 5. Overall CC charging interval metrics
    if (!ica.charge_ah.empty()) {
        feat.total_cc_charge_ah = ica.charge_ah.back() - ica.charge_ah.front();
    }
    feat.voltage_span_v = ica.voltage_v.back() - ica.voltage_v.front();

    return feat;
}

} // namespace ev::battery::features
