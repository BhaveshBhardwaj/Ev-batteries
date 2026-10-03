#include <gtest/gtest.h>
#include <ev/battery/math_utils/filters.hpp>

#include <cmath>
#include <numeric>
#include <vector>

using namespace ev::battery::math_utils;

// ============================================================
// Savitzky-Golay Tests
// ============================================================

TEST(MathUtils_SavgolCoefficients, Window5_Poly2_MatchesGorry1990Table) {
    auto res = savgolCoefficients(5, 2, 0);
    ASSERT_TRUE(res.has_value());
    const auto& c = *res;
    ASSERT_EQ(c.size(), 5);

    EXPECT_NEAR(c[0], -3.0 / 35.0, 1e-12);
    EXPECT_NEAR(c[1], 12.0 / 35.0, 1e-12);
    EXPECT_NEAR(c[2], 17.0 / 35.0, 1e-12);
    EXPECT_NEAR(c[3], 12.0 / 35.0, 1e-12);
    EXPECT_NEAR(c[4], -3.0 / 35.0, 1e-12);

    double sum = std::accumulate(c.begin(), c.end(), 0.0);
    EXPECT_NEAR(sum, 1.0, 1e-12);
}

TEST(MathUtils_SavgolCoefficients, Window5_Poly2_Deriv1_MatchesExpected) {
    auto res = savgolCoefficients(5, 2, 1);
    ASSERT_TRUE(res.has_value());
    const auto& c = *res;
    ASSERT_EQ(c.size(), 5);

    EXPECT_NEAR(c[0], -0.2, 1e-12);
    EXPECT_NEAR(c[1], -0.1, 1e-12);
    EXPECT_NEAR(c[2],  0.0, 1e-12);
    EXPECT_NEAR(c[3],  0.1, 1e-12);
    EXPECT_NEAR(c[4],  0.2, 1e-12);

    double sum = std::accumulate(c.begin(), c.end(), 0.0);
    EXPECT_NEAR(sum, 0.0, 1e-12);
}

TEST(MathUtils_SavgolCoefficients, InvalidParameters_ReturnsError) {
    EXPECT_EQ(savgolCoefficients(4, 2, 0).error(), MathError::INVALID_ARGUMENT);
    EXPECT_EQ(savgolCoefficients(1, 0, 0).error(), MathError::INVALID_ARGUMENT);
    EXPECT_EQ(savgolCoefficients(5, 5, 0).error(), MathError::INVALID_ARGUMENT);
    EXPECT_EQ(savgolCoefficients(5, 2, 3).error(), MathError::INVALID_ARGUMENT);
}

TEST(MathUtils_SavgolFilter, ConstantInput_PreservesExactValue) {
    std::vector<double> input(50, 42.0);
    std::vector<double> output(50);

    auto res = savgolFilter(input, output, 7, 2);
    ASSERT_TRUE(res.has_value());

    for (double v : output) {
        EXPECT_NEAR(v, 42.0, 1e-12);
    }
}

TEST(MathUtils_SavgolFilter, QuadraticInput_PreservedExactlyAtInterior) {
    const size_t n = 30;
    std::vector<double> input(n);
    std::vector<double> output(n);

    for (size_t i = 0; i < n; ++i) {
        double x = static_cast<double>(i);
        input[i] = 2.0 * x * x - 3.0 * x + 7.0;
    }

    auto res = savgolFilter(input, output, 5, 2);
    ASSERT_TRUE(res.has_value());

    for (size_t i = 3; i < n - 3; ++i) {
        EXPECT_NEAR(output[i], input[i], 1e-10);
    }
}

TEST(MathUtils_SavgolFilter, MismatchedSizes_ReturnsError) {
    std::vector<double> input(10, 1.0);
    std::vector<double> output(8);

    auto res = savgolFilter(input, output, 5, 2);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), MathError::SIZE_MISMATCH);
}

TEST(MathUtils_SavgolDerivative, QuadraticInput_DerivativeMatchesAnalytic) {
    const size_t n = 31;
    const double h = 0.5;
    std::vector<double> input(n);
    std::vector<double> output(n);

    for (size_t i = 0; i < n; ++i) {
        double x = i * h;
        input[i] = x * x;
    }

    auto res = savgolDerivative(input, output, 5, 2, 1, h);
    ASSERT_TRUE(res.has_value());

    for (size_t i = 3; i < n - 3; ++i) {
        double x = i * h;
        EXPECT_NEAR(output[i], 2.0 * x, 1e-10);
    }
}

// ============================================================
// Discrete 1D Convolution Tests
// ============================================================

TEST(MathUtils_Convolve1d, IdentityKernel_ReproducesInput) {
    std::vector<double> signal = {1.0, 3.0, 5.0, 7.0};
    std::vector<double> kernel = {1.0};
    std::vector<double> output(4);

    auto res = convolve1d(signal, kernel, output);
    ASSERT_TRUE(res.has_value());

    for (size_t i = 0; i < signal.size(); ++i) {
        EXPECT_DOUBLE_EQ(output[i], signal[i]);
    }
}

TEST(MathUtils_Convolve1d, KnownFilter_ProducesExpectedOutput) {
    std::vector<double> signal = {1.0, 2.0, 3.0, 4.0};
    std::vector<double> kernel = {0.5, 0.5};
    std::vector<double> output(3);

    auto res = convolve1d(signal, kernel, output);
    ASSERT_TRUE(res.has_value());

    EXPECT_DOUBLE_EQ(output[0], 1.5);
    EXPECT_DOUBLE_EQ(output[1], 2.5);
    EXPECT_DOUBLE_EQ(output[2], 3.5);
}

TEST(MathUtils_Convolve1d, OutputWrongSize_ReturnsError) {
    std::vector<double> signal = {1.0, 2.0, 3.0, 4.0};
    std::vector<double> kernel = {0.5, 0.5};
    std::vector<double> output(4);

    auto res = convolve1d(signal, kernel, output);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), MathError::SIZE_MISMATCH);
}

TEST(MathUtils_Convolve1d, KernelLargerThanSignal_ReturnsError) {
    std::vector<double> signal = {1.0};
    std::vector<double> kernel = {1.0, 2.0};
    std::vector<double> output(1);

    auto res = convolve1d(signal, kernel, output);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), MathError::INSUFFICIENT_DATA);
}

TEST(MathUtils_Convolve1dSame, IdentityKernel_MatchesInput) {
    std::vector<double> signal = {10.0, 20.0, 30.0, 40.0};
    std::vector<double> kernel = {0.0, 1.0, 0.0};
    std::vector<double> output(4);

    auto res = convolve1dSame(signal, kernel, output);
    ASSERT_TRUE(res.has_value());

    for (size_t i = 0; i < signal.size(); ++i) {
        EXPECT_DOUBLE_EQ(output[i], signal[i]);
    }
}

// ============================================================
// Moving Window Smoothers Tests
// ============================================================

TEST(MathUtils_MovingAverage, ConstantInput_PreservesValue) {
    std::vector<double> input(20, 15.0);
    std::vector<double> output(20);

    auto res = movingAverage(input, output, 5);
    ASSERT_TRUE(res.has_value());

    for (double v : output) {
        EXPECT_NEAR(v, 15.0, 1e-12);
    }
}

TEST(MathUtils_MovingAverage, WindowSizeOne_MatchesInputExactly) {
    std::vector<double> input = {1.0, 4.0, 9.0, 16.0};
    std::vector<double> output(4);

    auto res = movingAverage(input, output, 1);
    ASSERT_TRUE(res.has_value());

    for (size_t i = 0; i < input.size(); ++i) {
        EXPECT_DOUBLE_EQ(output[i], input[i]);
    }
}

TEST(MathUtils_MovingAverage, EvenWindowSize_ReturnsError) {
    std::vector<double> input = {1.0, 2.0, 3.0};
    std::vector<double> output(3);

    auto res = movingAverage(input, output, 4);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), MathError::INVALID_ARGUMENT);
}

TEST(MathUtils_MovingMedian, SingleSpikeOutlier_RejectsOutlier) {
    std::vector<double> input = {10.0, 10.0, 100.0, 10.0, 10.0};
    std::vector<double> output(5);

    auto res = movingMedian(input, output, 3);
    ASSERT_TRUE(res.has_value());

    EXPECT_DOUBLE_EQ(output[2], 10.0);
}

TEST(MathUtils_MovingMedian, ConstantInput_PreservesValue) {
    std::vector<double> input(10, 7.5);
    std::vector<double> output(10);

    auto res = movingMedian(input, output, 3);
    ASSERT_TRUE(res.has_value());

    for (double v : output) {
        EXPECT_DOUBLE_EQ(v, 7.5);
    }
}

TEST(MathUtils_MovingMedian, MismatchedSizes_ReturnsError) {
    std::vector<double> input = {1.0, 2.0, 3.0};
    std::vector<double> output(2);

    auto res = movingMedian(input, output, 3);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), MathError::SIZE_MISMATCH);
}
