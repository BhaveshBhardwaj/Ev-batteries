#pragma once

/// @file features.hpp
/// @brief M06: Electrochemical Feature Extraction.
/// Extracts quantitative aging signatures (peak locations, heights, areas, valley shifts)
/// from smooth Incremental Capacity (ICA) curves.

#include <ev/battery/common/types.hpp>
#include <ev/battery/ica_dva/ica_dva.hpp>
#include <vector>

namespace ev::battery::features {

/**
 * @brief Configuration parameters for electrochemical feature extraction.
 */
struct FeatureConfig {
    double min_prominence_ratio = 0.10;     ///< Minimum prominence as fraction of max peak (default 10%)
    double peak_area_half_width_v = 0.040;  ///< Integration half-window around Vp (+/-40 mV)
};

/**
 * @brief Extracted electrochemical features for State-of-Health & degradation modeling.
 */
struct ElectrochemicalFeatures {
    double primary_peak_voltage_v = 0.0;    ///< V_p1 (V)
    double primary_peak_height = 0.0;       ///< h_p1 (Ah/V)
    double primary_peak_area_ah = 0.0;      ///< A_p1 (Ah)
    double secondary_peak_voltage_v = 0.0;  ///< V_p2 (V, 0 if not present)
    double secondary_peak_height = 0.0;     ///< h_p2 (Ah/V, 0 if not present)
    double valley_voltage_v = 0.0;          ///< V_v1 (V, between P1 and P2)
    double valley_height = 0.0;             ///< h_v1 (Ah/V)
    double total_cc_charge_ah = 0.0;        ///< Total charge delivered in CC window (Ah)
    double voltage_span_v = 0.0;            ///< V_end - V_start of CC window (V)
    bool has_secondary_peak = false;

    /// Convert to flat 8-element feature vector for downstream ML estimators (M08)
    [[nodiscard]] std::vector<double> toVector() const {
        return {
            primary_peak_voltage_v,
            primary_peak_height,
            primary_peak_area_ah,
            secondary_peak_voltage_v,
            secondary_peak_height,
            valley_voltage_v,
            valley_height,
            total_cc_charge_ah
        };
    }
};

/**
 * @brief Extract electrochemical degradation features from an Incremental Capacity curve.
 *
 * Algorithm:
 * 1. Scans for candidate local maxima in dq_dv (excluding boundaries).
 * 2. Computes topographic prominence for each candidate peak.
 * 3. Filters peaks exceeding min_prominence_ratio * max_dq_dv.
 * 4. Assigns primary peak (P1) and secondary peak (P2).
 * 5. If P2 is present, locates the minimum valley between P1 and P2.
 * 6. Integrates peak area A_p1 within [V_p1 - half_width, V_p1 + half_width] via trapz.
 * 7. Records total delivered charge and voltage span.
 *
 * @param ica Computed Incremental Capacity curve from M05.
 * @param config Configuration parameters.
 * @return ElectrochemicalFeatures or PipelineError on failure.
 */
[[nodiscard]] common::Result<ElectrochemicalFeatures> extractIcaFeatures(
    const ica_dva::IcaCurve& ica,
    const FeatureConfig& config = {}
);

} // namespace ev::battery::features
