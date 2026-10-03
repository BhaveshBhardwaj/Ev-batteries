#pragma once

/// @file certificate.hpp
/// @brief M13: BPAN Tier 3 Health Certification & Cryptographic Signing Engine.
/// Generates digital battery passports compliant with India's NITI Aayog / MoRTH
/// Battery Pack Aadhaar Number (BPAN) Tier 3 specification with SHA-256 tamper-evidence.

#include <ev/battery/common/types.hpp>
#include <ev/battery/estimation/estimation.hpp>
#include <ev/battery/output/sha256.hpp>
#include <ev/battery/uncertainty/uncertainty.hpp>

#include <cstdint>
#include <string>

namespace ev::battery::output {

/**
 * @brief Metadata identifying the physical pack per BPAN Tier 1/2 registry.
 */
struct BpanMetadata {
    std::string bpan_id = "IN-BPAN-2W-LFP-00001"; ///< Unique Battery Pack Aadhaar Number
    std::string serial_number = "UNKNOWN-SN";      ///< OEM manufacturer serial number
    std::string chemistry = "LFP";                 ///< "LFP" or "NMC"
    std::string vehicle_category = "2W";           ///< "2W", "3W", "SWAP_STATION"
    double nominal_capacity_ah = 30.0;             ///< Nameplate rated capacity (Ah)
    double nominal_voltage_v = 51.2;               ///< Nameplate rated pack voltage (V)
    int series_cells = 16;                         ///< Number of series cells
};

/**
 * @brief Certified Battery Health Passport compliant with BPAN Tier 3 specification.
 */
struct HealthCertificate {
    BpanMetadata metadata;

    // Certified Health Valuation
    double certified_soh_pct = 100.0;             ///< Certified State-of-Health in [0.0, 100.0]%
    double usable_capacity_ah = 30.0;             ///< Usable capacity under current aging (Ah)
    double lifetime_efc = 0.0;                    ///< Lifetime Equivalent Full Cycles (EFC)

    // Conformal Uncertainty Bounds (Gate 3)
    double soh_lower_bound_pct = 0.0;             ///< 90% confidence lower bound (%)
    double soh_upper_bound_pct = 100.0;           ///< 90% confidence upper bound (%)
    double confidence_interval_width_pct = 0.0;   ///< Interval width (%)
    double coverage_level = 0.90;                 ///< Statistical coverage probability (0.90)

    // Degradation Diagnostics & RUL
    double remaining_useful_life_cycles = 0.0;    ///< Projected cycles to 70% commercial EoL
    double lli_percent = 0.0;                     ///< Loss of Lithium Inventory (%)
    double lam_percent = 0.0;                     ///< Loss of Active Material (%)
    std::string primary_degradation_mode;         ///< "BALANCED", "LLI_DOMINATED", "LAM_DOMINATED", "THERMAL_ACCELERATED"

    // Operational Thermal & Sensor Quality
    double thermal_degradation_index = 1.0;       ///< Cumulative Arrhenius stress multiplier
    double mean_temperature_c = 25.0;             ///< Average operating temperature (°C)
    double max_temperature_c = 25.0;              ///< Peak operating temperature (°C)
    double sensor_offset_bias_a = 0.0;            ///< Shunt zero-bias drift (A)
    double sensor_gain_factor = 1.0;              ///< Sensor scaling factor

    // Safety & Certification Status
    bool is_certified = false;                    ///< True if all Gate 1-3 conditions passed
    std::string certification_decision;           ///< "CERTIFIED" or "AUDIT_REQUIRED"
    std::string decision_rationale;               ///< Auditor explanation

    // Cryptographic Immutability
    std::string evaluation_timestamp_utc;         ///< ISO-8601 UTC timestamp
    std::string canonical_payload;                ///< Deterministic string representation
    std::string sha256_digital_signature;         ///< 64-char hex SHA-256 cryptographic digest
};

/**
 * @brief Serialize a HealthCertificate into a standardized BPAN Tier 3 JSON string.
 *
 * @param cert Populated HealthCertificate.
 * @return JSON string formatted per NITI Aayog digital battery passport schema.
 */
[[nodiscard]] std::string serializeCertificateToJson(const HealthCertificate& cert);

/**
 * @brief Generate an ASCII / Markdown human-readable certificate with cryptographic seal.
 *
 * @param cert Populated HealthCertificate.
 * @return Human-readable inspection certificate string.
 */
[[nodiscard]] std::string generateHumanReadableReport(const HealthCertificate& cert);

/**
 * @brief Execute the complete 9-module end-to-end pipeline and issue a certified health passport.
 *
 * Chains: Cleaning -> Segmentation -> Denoising -> Calibration -> Thermal -> ICA -> Features -> Estimation -> Uncertainty -> Certification.
 *
 * @param series Ingested raw battery telemetry series.
 * @param metadata Pack identification and rated parameters.
 * @param lifetime_efc Lifetime accumulated equivalent full cycles (EFC).
 * @return Signed HealthCertificate or PipelineError on fatal unrecoverable failure.
 */
[[nodiscard]] common::Result<HealthCertificate> generateHealthCertificate(
    const common::InternalTimeSeries& series,
    const BpanMetadata& metadata,
    double lifetime_efc = 0.0
);

} // namespace ev::battery::output
