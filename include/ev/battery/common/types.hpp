#pragma once

/// @file types.hpp
/// @brief Core data types shared across all EV Battery Telemetry Analyzer modules.

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ev::battery::common {

// ─── Telemetry Sample ────────────────────────────────────────

/**
 * @brief A single telemetry sample from BMS/telematics.
 */
struct Sample {
    double timestamp_s;      ///< Unix timestamp (seconds, double for sub-second precision)
    double voltage_v;        ///< Terminal voltage (V)
    double current_a;        ///< Current (A), positive = charging
    double temperature_c;    ///< Temperature (°C)

    /// Per-cell voltages (optional; requires cell-level BMS access)
    std::optional<std::vector<double>> cell_voltages_v;
};

// ─── Internal Time Series ────────────────────────────────────

/**
 * @brief Normalized internal representation of a telemetry session.
 *
 * This is the canonical format produced by the ingestion module (M01)
 * and consumed by all downstream modules.
 */
struct InternalTimeSeries {
    std::string session_id;
    std::string pack_id;
    std::string chemistry;                                  ///< "LFP", "NMC", "UNKNOWN"
    double      nominal_capacity_ah = 0.0;                  ///< Rated capacity (Ah)
    std::vector<Sample> samples;
    std::unordered_map<std::string, std::string> metadata;  ///< Arbitrary key-value pairs
};

// ─── Enumerations ────────────────────────────────────────────

/**
 * @brief Segment type classification.
 */
enum class SegmentType : uint8_t {
    CHARGE,
    DISCHARGE,
    REST,
    UNKNOWN
};

/**
 * @brief Quality classification for telemetry data.
 */
enum class QualityLabel : uint8_t {
    EXCELLENT,    ///< Score >= 80
    ACCEPTABLE,   ///< Score 50–79
    MARGINAL,     ///< Score 30–49
    REJECT        ///< Score < 30
};

// ─── Error Handling ──────────────────────────────────────────

/**
 * @brief Structured error type for pipeline-level errors.
 *
 * Contains enough context for logging and debugging.
 * Use MathError (in math_utils) for allocation-free hot-path errors.
 */
struct PipelineError {
    std::string module;    ///< Module that generated the error (e.g., "cleaning")
    std::string code;      ///< Machine-readable error code (e.g., "TIMESTAMP_GAP")
    std::string message;   ///< Human-readable description
    std::string context;   ///< Additional context (input ID, index, etc.)
};

/**
 * @brief Result type alias using std::expected for pipeline-level operations.
 */
template <typename T>
using Result = std::expected<T, PipelineError>;

} // namespace ev::battery::common
