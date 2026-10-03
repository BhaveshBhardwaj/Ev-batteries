#pragma once

/// @file types.hpp
/// @brief Error types for the math_utils module.
///
/// Uses a lightweight enum instead of strings for allocation-free
/// error handling in hot numerical paths.

#include <cstdint>
#include <expected>
#include <string_view>

namespace ev::battery::math_utils {

/**
 * @brief Error codes for math utility functions.
 *
 * Designed to be zero-cost: no heap allocation, trivially copyable.
 */
enum class MathError : uint8_t {
    INVALID_ARGUMENT,    ///< Bad parameter (e.g., even window size, negative order)
    SIZE_MISMATCH,       ///< Input/output array sizes don't match
    INSUFFICIENT_DATA,   ///< Not enough data points for the operation
    NUMERICAL_ERROR      ///< Computation failed (singular matrix, convergence failure)
};

/**
 * @brief Convert MathError to a human-readable string.
 */
constexpr std::string_view mathErrorToString(MathError err) noexcept {
    switch (err) {
        case MathError::INVALID_ARGUMENT:  return "Invalid argument";
        case MathError::SIZE_MISMATCH:     return "Size mismatch";
        case MathError::INSUFFICIENT_DATA: return "Insufficient data";
        case MathError::NUMERICAL_ERROR:   return "Numerical error";
    }
    return "Unknown error";
}

/**
 * @brief Result type for math functions. Allocation-free on the error path.
 */
template <typename T>
using MathResult = std::expected<T, MathError>;

} // namespace ev::battery::math_utils
