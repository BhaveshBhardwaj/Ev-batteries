#pragma once

/// @file math_utils.hpp
/// @brief Master umbrella header for all ev_battery math utilities.
///
/// Semantically organized into:
/// - types.hpp:      Zero-allocation error codes and MathResult
/// - calculus.hpp:   Differentiation, trapezoidal integration, and monotone Steffen splines
/// - filters.hpp:    Savitzky-Golay, 1D convolution, and moving window filters
/// - statistics.hpp: Kahan mean, std dev, median, MAD, and percentiles

#include <ev/battery/math_utils/types.hpp>
#include <ev/battery/math_utils/calculus.hpp>
#include <ev/battery/math_utils/filters.hpp>
#include <ev/battery/math_utils/statistics.hpp>
