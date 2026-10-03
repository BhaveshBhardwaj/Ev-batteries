#include <ev/battery/math_utils/statistics.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace ev::battery::math_utils {

double mean(std::span<const double> data) {
    if (data.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    // Kahan compensated summation
    double sum = 0.0;
    double c = 0.0;
    for (double x : data) {
        double y = x - c;
        double t = sum + y;
        c = (t - sum) - y;
        sum = t;
    }

    return sum / static_cast<double>(data.size());
}

double standardDeviation(std::span<const double> data) {
    if (data.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (data.size() == 1) {
        return 0.0;
    }

    double mu = mean(data);

    // Kahan summation for variance
    double sum_sq_diff = 0.0;
    double c = 0.0;
    for (double x : data) {
        double diff = x - mu;
        double y = (diff * diff) - c;
        double t = sum_sq_diff + y;
        c = (t - sum_sq_diff) - y;
        sum_sq_diff = t;
    }

    return std::sqrt(sum_sq_diff / static_cast<double>(data.size()));
}

double median(std::span<const double> data) {
    if (data.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (data.size() == 1) {
        return data[0];
    }

    std::vector<double> copy(data.begin(), data.end());
    const size_t n = copy.size();
    const size_t mid = n / 2;

    if (n % 2 != 0) {
        std::nth_element(copy.begin(), copy.begin() + mid, copy.end());
        return copy[mid];
    } else {
        std::nth_element(copy.begin(), copy.begin() + mid, copy.end());
        double right = copy[mid];
        // Find largest element in the lower half
        auto left_it = std::max_element(copy.begin(), copy.begin() + mid);
        return 0.5 * (*left_it + right);
    }
}

double medianAbsoluteDeviation(std::span<const double> data) {
    if (data.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (data.size() == 1) {
        return 0.0;
    }

    double med = median(data);
    std::vector<double> abs_diffs(data.size());
    for (size_t i = 0; i < data.size(); ++i) {
        abs_diffs[i] = std::abs(data[i] - med);
    }

    return median(abs_diffs);
}

double percentile(std::span<const double> data, double p) {
    if (data.empty() || p < 0.0 || p > 100.0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (data.size() == 1) {
        return data[0];
    }

    std::vector<double> sorted(data.begin(), data.end());
    std::sort(sorted.begin(), sorted.end());

    const size_t n = sorted.size();
    double idx = (p / 100.0) * static_cast<double>(n - 1);
    size_t i0 = static_cast<size_t>(std::floor(idx));
    size_t i1 = static_cast<size_t>(std::ceil(idx));
    double fraction = idx - static_cast<double>(i0);

    if (i1 >= n) {
        i1 = n - 1;
    }

    return sorted[i0] + fraction * (sorted[i1] - sorted[i0]);
}

} // namespace ev::battery::math_utils
