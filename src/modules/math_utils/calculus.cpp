#include <ev/battery/math_utils/calculus.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace ev::battery::math_utils {

// ─── Numerical Differentiation ───────────────────────────────────

MathResult<void> centralDifference(
    std::span<const double> x,
    std::span<const double> y,
    std::span<double> dy_dx
) {
    if (x.size() != y.size() || x.size() != dy_dx.size()) {
        return std::unexpected(MathError::SIZE_MISMATCH);
    }
    if (x.size() < 2) {
        return std::unexpected(MathError::INSUFFICIENT_DATA);
    }

    const size_t n = x.size();

    // First point: forward difference
    dy_dx[0] = (y[1] - y[0]) / (x[1] - x[0]);

    // Interior points: central difference
    for (size_t i = 1; i < n - 1; ++i) {
        dy_dx[i] = (y[i + 1] - y[i - 1]) / (x[i + 1] - x[i - 1]);
    }

    // Last point: backward difference
    dy_dx[n - 1] = (y[n - 1] - y[n - 2]) / (x[n - 1] - x[n - 2]);

    return {};
}

MathResult<void> forwardDifference(
    std::span<const double> x,
    std::span<const double> y,
    std::span<double> dy_dx
) {
    if (x.size() != y.size() || x.size() != dy_dx.size()) {
        return std::unexpected(MathError::SIZE_MISMATCH);
    }
    if (x.size() < 2) {
        return std::unexpected(MathError::INSUFFICIENT_DATA);
    }

    const size_t n = x.size();
    for (size_t i = 0; i < n - 1; ++i) {
        dy_dx[i] = (y[i + 1] - y[i]) / (x[i + 1] - x[i]);
    }
    dy_dx[n - 1] = (y[n - 1] - y[n - 2]) / (x[n - 1] - x[n - 2]);

    return {};
}

MathResult<void> backwardDifference(
    std::span<const double> x,
    std::span<const double> y,
    std::span<double> dy_dx
) {
    if (x.size() != y.size() || x.size() != dy_dx.size()) {
        return std::unexpected(MathError::SIZE_MISMATCH);
    }
    if (x.size() < 2) {
        return std::unexpected(MathError::INSUFFICIENT_DATA);
    }

    const size_t n = x.size();
    dy_dx[0] = (y[1] - y[0]) / (x[1] - x[0]);

    for (size_t i = 1; i < n; ++i) {
        dy_dx[i] = (y[i] - y[i - 1]) / (x[i] - x[i - 1]);
    }

    return {};
}

// ─── Numerical Integration ───────────────────────────────────────

MathResult<double> trapz(std::span<const double> x, std::span<const double> y) {
    if (x.size() != y.size()) {
        return std::unexpected(MathError::SIZE_MISMATCH);
    }
    if (x.size() < 2) {
        return std::unexpected(MathError::INSUFFICIENT_DATA);
    }

    double total = 0.0;
    for (size_t i = 0; i < x.size() - 1; ++i) {
        total += 0.5 * (x[i + 1] - x[i]) * (y[i + 1] + y[i]);
    }

    return total;
}

MathResult<void> cumulativeTrapz(
    std::span<const double> x,
    std::span<const double> y,
    std::span<double> integral
) {
    if (x.size() != y.size() || x.size() != integral.size()) {
        return std::unexpected(MathError::SIZE_MISMATCH);
    }
    if (x.size() < 2) {
        return std::unexpected(MathError::INSUFFICIENT_DATA);
    }

    integral[0] = 0.0;
    for (size_t i = 1; i < x.size(); ++i) {
        integral[i] = integral[i - 1] + 0.5 * (x[i] - x[i - 1]) * (y[i] + y[i - 1]);
    }

    return {};
}

// ─── Interpolation & Curve Resampling ────────────────────────────

MathResult<void> linearInterp(
    std::span<const double> x_data,
    std::span<const double> y_data,
    std::span<const double> x_query,
    std::span<double> y_query
) {
    if (x_data.size() != y_data.size() || x_query.size() != y_query.size()) {
        return std::unexpected(MathError::SIZE_MISMATCH);
    }
    if (x_data.size() < 2) {
        return std::unexpected(MathError::INSUFFICIENT_DATA);
    }

    for (size_t q = 0; q < x_query.size(); ++q) {
        double xq = x_query[q];

        if (xq <= x_data.front()) {
            y_query[q] = y_data.front();
        } else if (xq >= x_data.back()) {
            y_query[q] = y_data.back();
        } else {
            auto it = std::upper_bound(x_data.begin(), x_data.end(), xq);
            size_t idx = static_cast<size_t>(std::distance(x_data.begin(), it));
            size_t i0 = idx - 1;
            size_t i1 = idx;

            double dx = x_data[i1] - x_data[i0];
            if (dx <= 0.0) {
                return std::unexpected(MathError::INVALID_ARGUMENT);
            }
            double t = (xq - x_data[i0]) / dx;
            y_query[q] = y_data[i0] + t * (y_data[i1] - y_data[i0]);
        }
    }

    return {};
}

MathResult<void> monotoneCubicInterp(
    std::span<const double> x_data,
    std::span<const double> y_data,
    std::span<const double> x_query,
    std::span<double> y_query
) {
    if (x_data.size() != y_data.size() || x_query.size() != y_query.size()) {
        return std::unexpected(MathError::SIZE_MISMATCH);
    }
    if (x_data.size() < 2) {
        return std::unexpected(MathError::INSUFFICIENT_DATA);
    }

    const size_t n = x_data.size();

    if (n == 2) {
        return linearInterp(x_data, y_data, x_query, y_query);
    }

    std::vector<double> h(n - 1);
    std::vector<double> s(n - 1);
    for (size_t i = 0; i < n - 1; ++i) {
        h[i] = x_data[i + 1] - x_data[i];
        if (h[i] <= 0.0) {
            return std::unexpected(MathError::INVALID_ARGUMENT);
        }
        s[i] = (y_data[i + 1] - y_data[i]) / h[i];
    }

    std::vector<double> d(n);

    // Interior points via Steffen's method
    for (size_t i = 1; i < n - 1; ++i) {
        double p = (s[i - 1] * h[i] + s[i] * h[i - 1]) / (h[i - 1] + h[i]);
        if (s[i - 1] * s[i] <= 0.0) {
            d[i] = 0.0;
        } else {
            double sign = (s[i] >= 0.0) ? 1.0 : -1.0;
            double limit = 2.0 * std::min(std::abs(s[i - 1]), std::abs(s[i]));
            d[i] = sign * std::min(std::abs(p), limit);
        }
    }

    // Left endpoint (i = 0)
    {
        double p0 = s[0] * (1.0 + h[0] / (h[0] + h[1])) - s[1] * (h[0] / (h[0] + h[1]));
        if (p0 * s[0] <= 0.0) {
            d[0] = 0.0;
        } else if (std::abs(p0) > 2.0 * std::abs(s[0])) {
            d[0] = 2.0 * s[0];
        } else {
            d[0] = p0;
        }
    }

    // Right endpoint (i = n - 1)
    {
        size_t last = n - 1;
        double p_end = s[last - 1] * (1.0 + h[last - 1] / (h[last - 2] + h[last - 1]))
                     - s[last - 2] * (h[last - 1] / (h[last - 2] + h[last - 1]));
        if (p_end * s[last - 1] <= 0.0) {
            d[last] = 0.0;
        } else if (std::abs(p_end) > 2.0 * std::abs(s[last - 1])) {
            d[last] = 2.0 * s[last - 1];
        } else {
            d[last] = p_end;
        }
    }

    // Evaluate Hermite cubic polynomials at query coordinates
    for (size_t q = 0; q < x_query.size(); ++q) {
        double xq = x_query[q];

        if (xq <= x_data.front()) {
            y_query[q] = y_data.front();
        } else if (xq >= x_data.back()) {
            y_query[q] = y_data.back();
        } else {
            auto it = std::upper_bound(x_data.begin(), x_data.end(), xq);
            size_t idx = static_cast<size_t>(std::distance(x_data.begin(), it));
            size_t i = idx - 1;

            double hi = h[i];
            double t = (xq - x_data[i]) / hi;
            double t2 = t * t;
            double t3 = t2 * t;

            double c0 = 2.0 * t3 - 3.0 * t2 + 1.0;
            double c1 = t3 - 2.0 * t2 + t;
            double c2 = -2.0 * t3 + 3.0 * t2;
            double c3 = t3 - t2;

            y_query[q] = c0 * y_data[i] + c1 * hi * d[i] + c2 * y_data[i + 1] + c3 * hi * d[i + 1];
        }
    }

    return {};
}

} // namespace ev::battery::math_utils
