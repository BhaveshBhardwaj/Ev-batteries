#include <gtest/gtest.h>
#include <ev/battery/math_utils/calculus.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace ev::battery::math_utils;

// ============================================================
// Numerical Differentiation Tests
// ============================================================

TEST(MathUtils_CentralDifference, SinInput_MatchesCosWithinH2) {
    const size_t n = 101;
    const double h = std::numbers::pi / static_cast<double>(n - 1);
    std::vector<double> x(n);
    std::vector<double> y(n);
    std::vector<double> dy_dx(n);

    for (size_t i = 0; i < n; ++i) {
        x[i] = i * h;
        y[i] = std::sin(x[i]);
    }

    auto res = centralDifference(x, y, dy_dx);
    ASSERT_TRUE(res.has_value());

    for (size_t i = 1; i < n - 1; ++i) {
        EXPECT_NEAR(dy_dx[i], std::cos(x[i]), 0.01);
    }
}

TEST(MathUtils_CentralDifference, LinearInput_DerivativeIsConstant) {
    std::vector<double> x = {0.0, 1.0, 2.0, 3.0, 4.0};
    std::vector<double> y = {5.0, 8.0, 11.0, 14.0, 17.0};
    std::vector<double> dy_dx(5);

    auto res = centralDifference(x, y, dy_dx);
    ASSERT_TRUE(res.has_value());

    for (double d : dy_dx) {
        EXPECT_NEAR(d, 3.0, 1e-12);
    }
}

TEST(MathUtils_CentralDifference, NonUniformGrid_ComputesCorrectly) {
    std::vector<double> x = {0.0, 1.0, 3.0, 7.0, 10.0};
    std::vector<double> y = {0.0, 2.0, 6.0, 14.0, 20.0};
    std::vector<double> dy_dx(5);

    auto res = centralDifference(x, y, dy_dx);
    ASSERT_TRUE(res.has_value());

    for (double d : dy_dx) {
        EXPECT_NEAR(d, 2.0, 1e-12);
    }
}

TEST(MathUtils_CentralDifference, MismatchedSizes_ReturnsError) {
    std::vector<double> x = {0.0, 1.0, 2.0};
    std::vector<double> y = {0.0, 1.0};
    std::vector<double> dy_dx(3);

    auto res = centralDifference(x, y, dy_dx);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), MathError::SIZE_MISMATCH);
}

TEST(MathUtils_CentralDifference, InsufficientData_ReturnsError) {
    std::vector<double> x = {1.0};
    std::vector<double> y = {2.0};
    std::vector<double> dy_dx(1);

    auto res = centralDifference(x, y, dy_dx);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), MathError::INSUFFICIENT_DATA);
}

TEST(MathUtils_ForwardDifference, LinearInput_MatchesSlope) {
    std::vector<double> x = {0.0, 1.0, 2.0, 3.0};
    std::vector<double> y = {2.0, 6.0, 10.0, 14.0};
    std::vector<double> dy_dx(4);

    auto res = forwardDifference(x, y, dy_dx);
    ASSERT_TRUE(res.has_value());

    for (double d : dy_dx) {
        EXPECT_NEAR(d, 4.0, 1e-12);
    }
}

TEST(MathUtils_BackwardDifference, LinearInput_MatchesSlope) {
    std::vector<double> x = {0.0, 1.0, 2.0, 3.0};
    std::vector<double> y = {2.0, 6.0, 10.0, 14.0};
    std::vector<double> dy_dx(4);

    auto res = backwardDifference(x, y, dy_dx);
    ASSERT_TRUE(res.has_value());

    for (double d : dy_dx) {
        EXPECT_NEAR(d, 4.0, 1e-12);
    }
}

// ============================================================
// Numerical Integration Tests
// ============================================================

TEST(MathUtils_Trapz, SinInput_IntegratesToTwoWithinH2) {
    const size_t n = 1001;
    const double h = std::numbers::pi / static_cast<double>(n - 1);
    std::vector<double> x(n);
    std::vector<double> y(n);

    for (size_t i = 0; i < n; ++i) {
        x[i] = i * h;
        y[i] = std::sin(x[i]);
    }

    auto res = trapz(x, y);
    ASSERT_TRUE(res.has_value());
    EXPECT_NEAR(*res, 2.0, 1e-5);
}

TEST(MathUtils_Trapz, ConstantInput_ExactIntegral) {
    std::vector<double> x = {0.0, 1.0, 2.5, 4.0, 5.0};
    std::vector<double> y = {3.0, 3.0, 3.0, 3.0, 3.0};

    auto res = trapz(x, y);
    ASSERT_TRUE(res.has_value());
    EXPECT_DOUBLE_EQ(*res, 15.0);
}

TEST(MathUtils_Trapz, MismatchedSizes_ReturnsError) {
    std::vector<double> x = {0.0, 1.0, 2.0};
    std::vector<double> y = {1.0, 2.0};

    auto res = trapz(x, y);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), MathError::SIZE_MISMATCH);
}

TEST(MathUtils_CumulativeTrapz, FirstElementZero_LastMatchesTotalTrapz) {
    std::vector<double> x = {0.0, 1.0, 2.0, 3.0, 4.0};
    std::vector<double> y = {1.0, 3.0, 5.0, 7.0, 9.0};
    std::vector<double> integral(5);

    auto res_cum = cumulativeTrapz(x, y, integral);
    ASSERT_TRUE(res_cum.has_value());
    EXPECT_DOUBLE_EQ(integral[0], 0.0);

    auto res_total = trapz(x, y);
    ASSERT_TRUE(res_total.has_value());
    EXPECT_DOUBLE_EQ(integral.back(), *res_total);
}

TEST(MathUtils_CumulativeTrapz, LinearIntegrand_YieldsQuadraticIntegral) {
    const size_t n = 21;
    std::vector<double> x(n);
    std::vector<double> y(n);
    std::vector<double> integral(n);

    for (size_t i = 0; i < n; ++i) {
        x[i] = static_cast<double>(i);
        y[i] = 2.0 * x[i];
    }

    auto res = cumulativeTrapz(x, y, integral);
    ASSERT_TRUE(res.has_value());

    for (size_t i = 0; i < n; ++i) {
        EXPECT_NEAR(integral[i], x[i] * x[i], 1e-12);
    }
}

TEST(MathUtils_CumulativeTrapz, OutputBufferWrongSize_ReturnsError) {
    std::vector<double> x = {0.0, 1.0, 2.0};
    std::vector<double> y = {1.0, 2.0, 3.0};
    std::vector<double> integral(2); // Should be 3

    auto res = cumulativeTrapz(x, y, integral);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), MathError::SIZE_MISMATCH);
}

// ============================================================
// Interpolation & Curve Resampling Tests
// ============================================================

TEST(MathUtils_LinearInterp, ExactGridPoints_ReproducesData) {
    std::vector<double> x_data = {1.0, 2.0, 3.0, 4.0};
    std::vector<double> y_data = {10.0, 20.0, 30.0, 40.0};
    std::vector<double> x_query = {1.0, 2.0, 3.0, 4.0};
    std::vector<double> y_query(4);

    auto res = linearInterp(x_data, y_data, x_query, y_query);
    ASSERT_TRUE(res.has_value());

    for (size_t i = 0; i < 4; ++i) {
        EXPECT_DOUBLE_EQ(y_query[i], y_data[i]);
    }
}

TEST(MathUtils_LinearInterp, Midpoints_InterpolatesAccurately) {
    std::vector<double> x_data = {0.0, 10.0};
    std::vector<double> y_data = {0.0, 100.0};
    std::vector<double> x_query = {2.5, 5.0, 7.5};
    std::vector<double> y_query(3);

    auto res = linearInterp(x_data, y_data, x_query, y_query);
    ASSERT_TRUE(res.has_value());

    EXPECT_DOUBLE_EQ(y_query[0], 25.0);
    EXPECT_DOUBLE_EQ(y_query[1], 50.0);
    EXPECT_DOUBLE_EQ(y_query[2], 75.0);
}

TEST(MathUtils_LinearInterp, OutOfBounds_ClampsToBoundary) {
    std::vector<double> x_data = {2.0, 8.0};
    std::vector<double> y_data = {20.0, 80.0};
    std::vector<double> x_query = {-10.0, 0.0, 2.0, 8.0, 15.0};
    std::vector<double> y_query(5);

    auto res = linearInterp(x_data, y_data, x_query, y_query);
    ASSERT_TRUE(res.has_value());

    EXPECT_DOUBLE_EQ(y_query[0], 20.0);
    EXPECT_DOUBLE_EQ(y_query[1], 20.0);
    EXPECT_DOUBLE_EQ(y_query[2], 20.0);
    EXPECT_DOUBLE_EQ(y_query[3], 80.0);
    EXPECT_DOUBLE_EQ(y_query[4], 80.0);
}

TEST(MathUtils_LinearInterp, MismatchedSizes_ReturnsError) {
    std::vector<double> x_data = {1.0, 2.0};
    std::vector<double> y_data = {10.0};
    std::vector<double> x_query = {1.5};
    std::vector<double> y_query(1);

    auto res = linearInterp(x_data, y_data, x_query, y_query);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), MathError::SIZE_MISMATCH);
}

TEST(MathUtils_MonotoneCubicInterp, MonotoneData_PreservesMonotonicityWithoutOvershoot) {
    std::vector<double> x_data = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    std::vector<double> y_data = {3.2, 3.2, 3.2, 3.4, 3.4, 3.4, 3.4};

    const size_t num_q = 61;
    std::vector<double> x_query(num_q);
    for (size_t i = 0; i < num_q; ++i) {
        x_query[i] = 0.1 * static_cast<double>(i);
    }
    std::vector<double> y_query(num_q);

    auto res = monotoneCubicInterp(x_data, y_data, x_query, y_query);
    ASSERT_TRUE(res.has_value());

    for (size_t i = 0; i < num_q - 1; ++i) {
        EXPECT_GE(y_query[i + 1], y_query[i] - 1e-14);
    }
    for (double yq : y_query) {
        EXPECT_GE(yq, 3.2 - 1e-12);
        EXPECT_LE(yq, 3.4 + 1e-12);
    }
}

TEST(MathUtils_MonotoneCubicInterp, ExactGridPoints_MatchesData) {
    std::vector<double> x_data = {1.0, 2.0, 4.0, 7.0};
    std::vector<double> y_data = {2.0, 5.0, 11.0, 20.0};
    std::vector<double> x_query = {1.0, 2.0, 4.0, 7.0};
    std::vector<double> y_query(4);

    auto res = monotoneCubicInterp(x_data, y_data, x_query, y_query);
    ASSERT_TRUE(res.has_value());

    for (size_t i = 0; i < 4; ++i) {
        EXPECT_NEAR(y_query[i], y_data[i], 1e-12);
    }
}

TEST(MathUtils_MonotoneCubicInterp, TwoPoints_FallsBackToLinear) {
    std::vector<double> x_data = {0.0, 10.0};
    std::vector<double> y_data = {0.0, 100.0};
    std::vector<double> x_query = {5.0};
    std::vector<double> y_query(1);

    auto res = monotoneCubicInterp(x_data, y_data, x_query, y_query);
    ASSERT_TRUE(res.has_value());
    EXPECT_DOUBLE_EQ(y_query[0], 50.0);
}
