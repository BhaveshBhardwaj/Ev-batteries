#pragma once

/// @file sha256.hpp
/// @brief Standard FIPS 180-2 / RFC 6234 compliant SHA-256 cryptographic hash function.
/// Pure C++23 zero-dependency implementation for tamper-evident battery certificates.

#include <cstdint>
#include <string>
#include <string_view>

namespace ev::battery::output {

/**
 * @brief Compute the 64-character lowercase hexadecimal SHA-256 digest of an input string.
 *
 * @param input Text data to hash.
 * @return 64-character hexadecimal SHA-256 hash string.
 */
[[nodiscard]] std::string computeSha256(std::string_view input);

/**
 * @brief Compute the 64-character lowercase hexadecimal SHA-256 digest of raw binary bytes.
 *
 * @param data Pointer to raw byte buffer.
 * @param len Length of byte buffer in bytes.
 * @return 64-character hexadecimal SHA-256 hash string.
 */
[[nodiscard]] std::string computeSha256(const uint8_t* data, size_t len);

} // namespace ev::battery::output
