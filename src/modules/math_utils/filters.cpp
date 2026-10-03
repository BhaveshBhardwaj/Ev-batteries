#include <ev/battery/math_utils/filters.hpp>

#include <Eigen/Dense>
#include <algorithm>
#include <cmath>

namespace ev::battery::math_utils {

namespace {

double factorial(int n) noexcept {
    double res = 1.0;
    for (int i = 2; i <= n; ++i) {
        res *= static_cast<double>(i);
    }
    return res;
}

int mirrorIndex(int idx, int n) noexcept {
    if (n <= 1) {
        return 0;
    }
    while (idx < 0 || idx >= n) {
        if (idx < 0) {
            idx = -idx;
        } else if (idx >= n) {
            idx = 2 * (n - 1) - idx;
        }
    }
    return idx;
}

} // anonymous namespace

// ─── Savitzky-Golay Smoothing & Differentiation ──────────────────

MathResult<std::vector<double>> savgolCoefficients(
    int window_size,
    int poly_order,
    int deriv_order
) {
    if (window_size < 3 || window_size % 2 == 0 || window_size > 51) {
        return std::unexpected(MathError::INVALID_ARGUMENT);
    }
    if (poly_order < 0 || poly_order >= window_size) {
        return std::unexpected(MathError::INVALID_ARGUMENT);
    }
    if (deriv_order < 0 || deriv_order > poly_order) {
        return std::unexpected(MathError::INVALID_ARGUMENT);
    }

    const int m = (window_size - 1) / 2;
    const int num_cols = poly_order + 1;

    // Vandermonde matrix J (window_size x (poly_order + 1))
    Eigen::MatrixXd J(window_size, num_cols);
    for (int i = 0; i < window_size; ++i) {
        double x = static_cast<double>(-m + i);
        double val = 1.0;
        for (int j = 0; j < num_cols; ++j) {
            J(i, j) = val;
            val *= x;
        }
    }

    // Solve for pseudoinverse: (J^T J)^(-1) J^T
    // J is (W x (P+1)) with full column rank, so J^T J is SPD ((P+1) x (P+1))
    Eigen::MatrixXd JtJ = J.transpose() * J;
    auto ldlt = JtJ.ldlt();
    if (ldlt.info() != Eigen::Success) {
        return std::unexpected(MathError::NUMERICAL_ERROR);
    }

    Eigen::MatrixXd pinv = ldlt.solve(J.transpose());

    // Extract row corresponding to deriv_order, scaled by deriv_order!
    const double factor = factorial(deriv_order);
    std::vector<double> coeffs(window_size);
    for (int i = 0; i < window_size; ++i) {
        coeffs[i] = pinv(deriv_order, i) * factor;
    }

    return coeffs;
}

MathResult<void> savgolFilter(
    std::span<const double> input,
    std::span<double> output,
    int window_size,
    int poly_order
) {
    if (input.size() != output.size()) {
        return std::unexpected(MathError::SIZE_MISMATCH);
    }
    if (static_cast<int>(input.size()) < window_size) {
        return std::unexpected(MathError::INSUFFICIENT_DATA);
    }

    auto coeffs_res = savgolCoefficients(window_size, poly_order, 0);
    if (!coeffs_res) {
        return std::unexpected(coeffs_res.error());
    }

    const auto& coeffs = *coeffs_res;
    const int m = (window_size - 1) / 2;
    const int n = static_cast<int>(input.size());

    for (int i = 0; i < n; ++i) {
        double sum = 0.0;
        for (int k = -m; k <= m; ++k) {
            int src_idx = mirrorIndex(i + k, n);
            sum += coeffs[k + m] * input[src_idx];
        }
        output[i] = sum;
    }

    return {};
}

MathResult<void> savgolDerivative(
    std::span<const double> input,
    std::span<double> output,
    int window_size,
    int poly_order,
    int deriv_order,
    double sample_spacing
) {
    if (input.size() != output.size()) {
        return std::unexpected(MathError::SIZE_MISMATCH);
    }
    if (static_cast<int>(input.size()) < window_size) {
        return std::unexpected(MathError::INSUFFICIENT_DATA);
    }
    if (sample_spacing <= 0.0) {
        return std::unexpected(MathError::INVALID_ARGUMENT);
    }

    auto coeffs_res = savgolCoefficients(window_size, poly_order, deriv_order);
    if (!coeffs_res) {
        return std::unexpected(coeffs_res.error());
    }

    const auto& coeffs = *coeffs_res;
    const int m = (window_size - 1) / 2;
    const int n = static_cast<int>(input.size());
    const double scale = 1.0 / std::pow(sample_spacing, deriv_order);

    for (int i = 0; i < n; ++i) {
        double sum = 0.0;
        for (int k = -m; k <= m; ++k) {
            int src_idx = mirrorIndex(i + k, n);
            sum += coeffs[k + m] * input[src_idx];
        }
        output[i] = sum * scale;
    }

    return {};
}

// ─── Discrete 1D Convolutions ────────────────────────────────────

MathResult<void> convolve1d(
    std::span<const double> signal,
    std::span<const double> kernel,
    std::span<double> output
) {
    if (kernel.empty()) {
        return std::unexpected(MathError::INVALID_ARGUMENT);
    }
    if (signal.size() < kernel.size()) {
        return std::unexpected(MathError::INSUFFICIENT_DATA);
    }

    const size_t expected_size = signal.size() - kernel.size() + 1;
    if (output.size() != expected_size) {
        return std::unexpected(MathError::SIZE_MISMATCH);
    }

    const size_t k_len = kernel.size();
    for (size_t i = 0; i < expected_size; ++i) {
        double sum = 0.0;
        for (size_t j = 0; j < k_len; ++j) {
            sum += signal[i + j] * kernel[k_len - 1 - j];
        }
        output[i] = sum;
    }

    return {};
}

MathResult<void> convolve1dSame(
    std::span<const double> signal,
    std::span<const double> kernel,
    std::span<double> output
) {
    if (kernel.empty() || kernel.size() % 2 == 0) {
        return std::unexpected(MathError::INVALID_ARGUMENT);
    }
    if (signal.size() != output.size()) {
        return std::unexpected(MathError::SIZE_MISMATCH);
    }
    if (signal.empty()) {
        return {};
    }

    const int n = static_cast<int>(signal.size());
    const int k_len = static_cast<int>(kernel.size());
    const int m = (k_len - 1) / 2;

    for (int i = 0; i < n; ++i) {
        double sum = 0.0;
        for (int j = 0; j < k_len; ++j) {
            int offset = j - m;
            int src_idx = mirrorIndex(i + offset, n);
            sum += signal[src_idx] * kernel[k_len - 1 - j];
        }
        output[i] = sum;
    }

    return {};
}

// ─── Moving Window Smoothers ─────────────────────────────────────

MathResult<void> movingAverage(
    std::span<const double> input,
    std::span<double> output,
    int window_size
) {
    if (window_size < 1 || window_size % 2 == 0) {
        return std::unexpected(MathError::INVALID_ARGUMENT);
    }
    if (input.size() != output.size()) {
        return std::unexpected(MathError::SIZE_MISMATCH);
    }
    if (input.empty()) {
        return {};
    }

    const int n = static_cast<int>(input.size());
    const int m = window_size / 2;

    for (int i = 0; i < n; ++i) {
        int start = std::max(0, i - m);
        int end = std::min(n - 1, i + m);
        double sum = 0.0;
        for (int k = start; k <= end; ++k) {
            sum += input[k];
        }
        output[i] = sum / static_cast<double>(end - start + 1);
    }

    return {};
}

MathResult<void> movingMedian(
    std::span<const double> input,
    std::span<double> output,
    int window_size
) {
    if (window_size < 1 || window_size % 2 == 0) {
        return std::unexpected(MathError::INVALID_ARGUMENT);
    }
    if (input.size() != output.size()) {
        return std::unexpected(MathError::SIZE_MISMATCH);
    }
    if (input.empty()) {
        return {};
    }

    const int n = static_cast<int>(input.size());
    const int m = window_size / 2;
    std::vector<double> buf;
    buf.reserve(window_size);

    for (int i = 0; i < n; ++i) {
        int start = std::max(0, i - m);
        int end = std::min(n - 1, i + m);
        buf.assign(input.begin() + start, input.begin() + end + 1);

        const size_t len = buf.size();
        const size_t mid = len / 2;
        std::nth_element(buf.begin(), buf.begin() + mid, buf.end());

        if (len % 2 != 0) {
            output[i] = buf[mid];
        } else {
            double right = buf[mid];
            auto left_it = std::max_element(buf.begin(), buf.begin() + mid);
            output[i] = 0.5 * (*left_it + right);
        }
    }

    return {};
}

} // namespace ev::battery::math_utils
