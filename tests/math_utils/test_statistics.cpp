#include <gtest/gtest.h>
#include <ev/battery/math_utils/statistics.hpp>

#include <cmath>
#include <vector>

using namespace ev::battery::math_utils;

// ============================================================
// Tests for: ev::battery::math_utils::mean
// ============================================================

TEST(MathUtils_Mean, TypicalInput_MatchesExpected) {
    std::vector<double> data = {2.0, 4.0, 6.0, 8.0, 10.0};
    EXPECT_DOUBLE_EQ(mean(data), 6.0);
}

TEST(MathUtils_Mean, SingleElement_ReturnsElement) {
    std::vector<double> data = {42.5};
    EXPECT_DOUBLE_EQ(mean(data), 42.5);
}

TEST(MathUtils_Mean, EmptyInput_ReturnsNaN) {
    std::vector<double> data = {};
    EXPECT_TRUE(std::isnan(mean(data)));
}

TEST(MathUtils_Mean, ConstantInput_ReturnsConstant) {
    std::vector<double> data(100, 3.14159);
    EXPECT_NEAR(mean(data), 3.14159, 1e-12);
}

// ============================================================
// Tests for: ev::battery::math_utils::standardDeviation
// ============================================================

TEST(MathUtils_StandardDeviation, ConstantInput_ReturnsZero) {
    std::vector<double> data = {5.0, 5.0, 5.0, 5.0, 5.0};
    EXPECT_DOUBLE_EQ(standardDeviation(data), 0.0);
}

TEST(MathUtils_StandardDeviation, SingleElement_ReturnsZero) {
    std::vector<double> data = {99.0};
    EXPECT_DOUBLE_EQ(standardDeviation(data), 0.0);
}

TEST(MathUtils_StandardDeviation, EmptyInput_ReturnsNaN) {
    std::vector<double> data = {};
    EXPECT_TRUE(std::isnan(standardDeviation(data)));
}

TEST(MathUtils_StandardDeviation, KnownValues_MatchesNumPyReference) {
    // Reference: np.std([12, 15, 11, 19, 22, 14, 18, 25], ddof=0) == sqrt(21) ≈ 4.58257569495584
    std::vector<double> data = {12.0, 15.0, 11.0, 19.0, 22.0, 14.0, 18.0, 25.0};
    EXPECT_NEAR(standardDeviation(data), std::sqrt(21.0), 1e-12);
}

// ============================================================
// Tests for: ev::battery::math_utils::median
// ============================================================

TEST(MathUtils_Median, OddCount_ReturnsMiddleElement) {
    std::vector<double> data = {7.0, 1.0, 3.0};
    EXPECT_DOUBLE_EQ(median(data), 3.0);
}

TEST(MathUtils_Median, EvenCount_ReturnsAverageOfMiddleTwo) {
    std::vector<double> data = {7.0, 1.0, 3.0, 9.0};
    // Sorted: 1.0, 3.0, 7.0, 9.0 -> (3.0 + 7.0)/2 = 5.0
    EXPECT_DOUBLE_EQ(median(data), 5.0);
}

TEST(MathUtils_Median, EmptyInput_ReturnsNaN) {
    std::vector<double> data = {};
    EXPECT_TRUE(std::isnan(median(data)));
}

TEST(MathUtils_Median, SingleElement_ReturnsElement) {
    std::vector<double> data = {17.5};
    EXPECT_DOUBLE_EQ(median(data), 17.5);
}

// ============================================================
// Tests for: ev::battery::math_utils::medianAbsoluteDeviation
// ============================================================

TEST(MathUtils_MedianAbsoluteDeviation, ConstantInput_ReturnsZero) {
    std::vector<double> data = {4.0, 4.0, 4.0, 4.0};
    EXPECT_DOUBLE_EQ(medianAbsoluteDeviation(data), 0.0);
}

TEST(MathUtils_MedianAbsoluteDeviation, KnownValues_MatchesNumPyReference) {
    // Data: [12, 15, 11, 19, 22, 14, 18, 25], median = 16.5
    // Diff: [4.5, 1.5, 5.5, 2.5, 5.5, 2.5, 1.5, 8.5], sorted: [1.5, 1.5, 2.5, 2.5, 4.5, 5.5, 5.5, 8.5]
    // Median of diff = (2.5 + 4.5)/2 = 3.5
    std::vector<double> data = {12.0, 15.0, 11.0, 19.0, 22.0, 14.0, 18.0, 25.0};
    EXPECT_DOUBLE_EQ(medianAbsoluteDeviation(data), 3.5);
}

TEST(MathUtils_MedianAbsoluteDeviation, EmptyInput_ReturnsNaN) {
    std::vector<double> data = {};
    EXPECT_TRUE(std::isnan(medianAbsoluteDeviation(data)));
}

// ============================================================
// Tests for: ev::battery::math_utils::percentile
// ============================================================

TEST(MathUtils_Percentile, BoundaryValues_ReturnsMinAndMax) {
    std::vector<double> data = {10.0, 20.0, 30.0, 40.0, 50.0};
    EXPECT_DOUBLE_EQ(percentile(data, 0.0), 10.0);
    EXPECT_DOUBLE_EQ(percentile(data, 100.0), 50.0);
}

TEST(MathUtils_Percentile, FiftiethPercentile_MatchesMedian) {
    std::vector<double> data = {12.0, 15.0, 11.0, 19.0, 22.0, 14.0, 18.0, 25.0};
    EXPECT_DOUBLE_EQ(percentile(data, 50.0), median(data));
}

TEST(MathUtils_Percentile, TwentyFifthAndSeventyFifth_MatchesNumPyReference) {
    std::vector<double> data = {12.0, 15.0, 11.0, 19.0, 22.0, 14.0, 18.0, 25.0};
    EXPECT_NEAR(percentile(data, 25.0), 13.5, 1e-12);
    EXPECT_NEAR(percentile(data, 75.0), 19.75, 1e-12);
}

TEST(MathUtils_Percentile, InvalidPercentile_ReturnsNaN) {
    std::vector<double> data = {1.0, 2.0, 3.0};
    EXPECT_TRUE(std::isnan(percentile(data, -5.0)));
    EXPECT_TRUE(std::isnan(percentile(data, 105.0)));
}

TEST(MathUtils_Percentile, EmptyInput_ReturnsNaN) {
    std::vector<double> data = {};
    EXPECT_TRUE(std::isnan(percentile(data, 50.0)));
}
