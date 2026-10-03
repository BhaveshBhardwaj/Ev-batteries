#pragma once

/// @file calculus.hpp
/// @brief Discrete calculus and curve analysis: numerical differentiation,
/// integration, and interpolation for battery telemetry time series.

#include <ev/battery/math_utils/types.hpp>
#include <span>

namespace ev::battery::math_utils {

// ─── Numerical Differentiation ───────────────────────────────────

/**
 * @brief Central finite difference derivative on uniform or non-uniform grids.
 *
 * Interior points: dy/dx[i] = (y[i+1] - y[i-1]) / (x[i+1] - x[i-1])
 * First point uses forward difference; last point uses backward difference.
 *
 * @param x     Independent variable (e.g. time or voltage).
 * @param y     Dependent variable (e.g. voltage or cumulative charge).
 * @param dy_dx Output derivative array (must match size of x).
 * @return Success, or SIZE_MISMATCH / INSUFFICIENT_DATA error.
 */
MathResult<void> centralDifference(
    std::span<const double> x,
    std::span<const double> y,
    std::span<double> dy_dx
);

/**
 * @brief Forward finite difference derivative.
 */
MathResult<void> forwardDifference(
    std::span<const double> x,
    std::span<const double> y,
    std::span<double> dy_dx
);

/**
 * @brief Backward finite difference derivative.
 */
MathResult<void> backwardDifference(
    std::span<const double> x,
    std::span<const double> y,
    std::span<double> dy_dx
);

// ─── Numerical Integration ───────────────────────────────────────

/**
 * @brief Definite integral using the trapezoidal rule.
 *
 * @param x Independent variable (monotonically ascending).
 * @param y Integrand values (same size as x).
 * @return Total integral value, or error.
 */
MathResult<double> trapz(
    std::span<const double> x,
    std::span<const double> y
);

/**
 * @brief Cumulative trapezoidal integration (Coulomb counting: Q(t) = ∫ I(τ) dτ).
 *
 * integral[0] = 0.0; integral[i] = integral[i-1] + 0.5*(x[i]-x[i-1])*(y[i]+y[i-1]).
 *
 * @param x        Independent variable.
 * @param y        Integrand values.
 * @param integral Output cumulative integral array (must match size of x).
 * @return Success, or error.
 */
MathResult<void> cumulativeTrapz(
    std::span<const double> x,
    std::span<const double> y,
    std::span<double> integral
);

// ─── Interpolation & Curve Resampling ────────────────────────────

/**
 * @brief Piecewise linear interpolation with boundary clamping.
 *
 * Uses binary search for O(log N) lookup per query. Queries outside data
 * bounds clamp to boundary values without extrapolating.
 *
 * @param x_data  Reference x grid (sorted ascending, >= 2 points).
 * @param y_data  Reference y values.
 * @param x_query Target query x coordinates.
 * @param y_query Output interpolated y coordinates (size = x_query.size()).
 * @return Success, or error.
 */
MathResult<void> linearInterp(
    std::span<const double> x_data,
    std::span<const double> y_data,
    std::span<const double> x_query,
    std::span<double> y_query
);

/**
 * @brief Monotone cubic spline interpolation via Steffen's method (1990).
 *
 * Guarantees preservation of monotonicity in battery curves (e.g. Q(V) curve),
 * completely preventing spurious oscillations and false peaks during ICA differentiation.
 *
 * @param x_data  Reference x grid (sorted ascending, >= 2 points).
 * @param y_data  Reference y values.
 * @param x_query Target query x coordinates.
 * @param y_query Output interpolated y coordinates (size = x_query.size()).
 * @return Success, or error.
 */
MathResult<void> monotoneCubicInterp(
    std::span<const double> x_data,
    std::span<const double> y_data,
    std::span<const double> x_query,
    std::span<double> y_query
);

} // namespace ev::battery::math_utils
