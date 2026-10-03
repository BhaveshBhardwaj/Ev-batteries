#include <ev/battery/segmentation/segmentation.hpp>
#include <algorithm>
#include <cmath>

namespace ev::battery::segmentation {

common::Result<std::vector<Segment>> segmentRegimes(
    const common::InternalTimeSeries& series,
    const SegmentationConfig& config
) {
    if (series.samples.empty()) {
        return std::unexpected(common::PipelineError{
            .module = "segmentation",
            .code = "INVALID_ARGUMENT",
            .message = segmentationErrorToString(SegmentationError::EMPTY_INPUT)
        });
    }

    if (config.charge_current_thresh_a <= config.rest_current_thresh_a ||
        config.discharge_current_thresh_a >= -config.rest_current_thresh_a ||
        config.min_dwell_time_s <= 0.0) {
        return std::unexpected(common::PipelineError{
            .module = "segmentation",
            .code = "INVALID_ARGUMENT",
            .message = segmentationErrorToString(SegmentationError::INVALID_THRESHOLDS)
        });
    }

    const size_t n = series.samples.size();
    if (n < 2) {
        return std::unexpected(common::PipelineError{
            .module = "segmentation",
            .code = "INSUFFICIENT_DATA",
            .message = segmentationErrorToString(SegmentationError::INSUFFICIENT_DATA)
        });
    }

    // Pass 1: Assign instantaneous classification with hysteresis
    std::vector<common::SegmentType> raw_regimes(n);
    common::SegmentType current_state = common::SegmentType::REST;

    for (size_t i = 0; i < n; ++i) {
        const double cur = series.samples[i].current_a;
        if (cur > config.charge_current_thresh_a) {
            current_state = common::SegmentType::CHARGE;
        } else if (cur < config.discharge_current_thresh_a) {
            current_state = common::SegmentType::DISCHARGE;
        } else if (std::abs(cur) <= config.rest_current_thresh_a) {
            current_state = common::SegmentType::REST;
        }
        raw_regimes[i] = current_state;
    }

    // Pass 2: Merge runs shorter than min_dwell_time_s (debounce filtering)
    struct RawInterval {
        common::SegmentType type;
        size_t start;
        size_t end;
    };

    std::vector<RawInterval> intervals;
    size_t run_start = 0;

    for (size_t i = 1; i <= n; ++i) {
        if (i == n || raw_regimes[i] != raw_regimes[run_start]) {
            intervals.push_back({raw_regimes[run_start], run_start, i - 1});
            run_start = i;
        }
    }

    // Debounce loop: merge transient intervals (< min_dwell_time_s) into adjacent regime
    bool changed = true;
    while (changed && intervals.size() > 1) {
        changed = false;
        for (size_t idx = 0; idx < intervals.size(); ++idx) {
            const double dt = series.samples[intervals[idx].end].timestamp_s -
                              series.samples[intervals[idx].start].timestamp_s;

            if (dt < config.min_dwell_time_s) {
                // Merge with left neighbor if possible, else right
                if (idx > 0) {
                    intervals[idx - 1].end = intervals[idx].end;
                    intervals.erase(intervals.begin() + idx);
                    changed = true;
                    break;
                } else if (idx + 1 < intervals.size()) {
                    intervals[idx + 1].start = intervals[idx].start;
                    intervals.erase(intervals.begin() + idx);
                    changed = true;
                    break;
                }
            }
        }
    }

    // Pass 3: Construct enriched Segment records with statistical properties
    std::vector<Segment> result;
    result.reserve(intervals.size());

    for (const auto& iv : intervals) {
        const size_t seg_len = iv.end - iv.start + 1;
        std::vector<double> seg_i(seg_len);
        std::vector<double> seg_v(seg_len);
        std::vector<double> seg_t(seg_len);

        double v_min = series.samples[iv.start].voltage_v;
        double v_max = series.samples[iv.start].voltage_v;

        for (size_t k = 0; k < seg_len; ++k) {
            const auto& smp = series.samples[iv.start + k];
            seg_i[k] = smp.current_a;
            seg_v[k] = smp.voltage_v;
            seg_t[k] = smp.timestamp_s;

            v_min = std::min(v_min, smp.voltage_v);
            v_max = std::max(v_max, smp.voltage_v);
        }

        const double duration = seg_t.back() - seg_t.front();
        const double mean_i = math_utils::mean(seg_i);
        const double std_i = math_utils::standardDeviation(seg_i);
        const double mean_v = math_utils::mean(seg_v);
        const double trapz_val = math_utils::trapz(seg_t, seg_i).value_or(0.0);
        const double q_ah = std::abs(trapz_val) / 3600.0;

        result.push_back(Segment{
            .type = iv.type,
            .start_index = iv.start,
            .end_index = iv.end,
            .start_timestamp_s = seg_t.front(),
            .end_timestamp_s = seg_t.back(),
            .duration_s = duration,
            .mean_current_a = mean_i,
            .std_current_a = std_i,
            .mean_voltage_v = mean_v,
            .min_voltage_v = v_min,
            .max_voltage_v = v_max,
            .throughput_ah = q_ah
        });
    }

    return result;
}

common::Result<Segment> extractConstantCurrentWindow(
    const common::InternalTimeSeries& series,
    double min_duration_s,
    double max_current_ripple_a,
    const SegmentationConfig& config
) {
    auto segs_res = segmentRegimes(series, config);
    if (!segs_res) {
        return std::unexpected(segs_res.error());
    }

    std::optional<Segment> best_seg;
    double max_duration = 0.0;

    for (const auto& seg : *segs_res) {
        if (seg.type != common::SegmentType::CHARGE) {
            continue;
        }

        // Case 1: Pure CC segment (entire segment ripple <= max_current_ripple_a)
        if (seg.duration_s >= min_duration_s && seg.std_current_a <= max_current_ripple_a) {
            if (seg.duration_s > max_duration) {
                max_duration = seg.duration_s;
                best_seg = seg;
            }
            continue;
        }

        // Case 2: CC-CV segment where CV tail causes ripple > max_current_ripple_a.
        // Identify the pure CC plateau before CV taper begins.
        const size_t seg_start = seg.start_index;
        const size_t seg_end = seg.end_index;
        if (seg_end <= seg_start) continue;

        // Compute representative charging current via median of positive charge current samples
        std::vector<double> charge_currents;
        charge_currents.reserve(seg.end_index - seg.start_index + 1);
        for (size_t k = seg.start_index; k <= seg.end_index; ++k) {
            if (series.samples[k].current_a > config.charge_current_thresh_a) {
                charge_currents.push_back(series.samples[k].current_a);
            }
        }
        if (charge_currents.empty()) continue;

        const double target_cc_i = math_utils::median(charge_currents);

        // Find actual start of stable CC plateau (skipping any pre-charge transients)
        size_t cc_start = seg.start_index;
        while (cc_start <= seg.end_index &&
               std::abs(series.samples[cc_start].current_a - target_cc_i) > max_current_ripple_a) {
            ++cc_start;
        }

        if (cc_start > seg.end_index) continue;

        // Find end of stable CC plateau (before CV taper begins)
        size_t cc_end = cc_start;
        for (size_t k = cc_start; k <= seg.end_index; ++k) {
            if (std::abs(series.samples[k].current_a - target_cc_i) <= max_current_ripple_a) {
                cc_end = k;
            } else if (k > cc_start + 10 && series.samples[k].current_a < target_cc_i - max_current_ripple_a) {
                // Monotonic taper / CV decay detected
                break;
            }
        }

        const double cc_dur = series.samples[cc_end].timestamp_s - series.samples[cc_start].timestamp_s;
        if (cc_dur >= min_duration_s && cc_dur > max_duration) {
            const size_t cc_len = cc_end - cc_start + 1;
            std::vector<double> sub_i(cc_len);
            std::vector<double> sub_v(cc_len);
            std::vector<double> sub_t(cc_len);
            double v_min = series.samples[cc_start].voltage_v;
            double v_max = series.samples[cc_start].voltage_v;

            for (size_t k = 0; k < cc_len; ++k) {
                const auto& smp = series.samples[cc_start + k];
                sub_i[k] = smp.current_a;
                sub_v[k] = smp.voltage_v;
                sub_t[k] = smp.timestamp_s;
                v_min = std::min(v_min, smp.voltage_v);
                v_max = std::max(v_max, smp.voltage_v);
            }

            const double std_i = math_utils::standardDeviation(sub_i);
            if (std_i <= max_current_ripple_a) {
                max_duration = cc_dur;
                best_seg = Segment{
                    .type = common::SegmentType::CHARGE,
                    .start_index = cc_start,
                    .end_index = cc_end,
                    .start_timestamp_s = sub_t.front(),
                    .end_timestamp_s = sub_t.back(),
                    .duration_s = cc_dur,
                    .mean_current_a = math_utils::mean(sub_i),
                    .std_current_a = std_i,
                    .mean_voltage_v = math_utils::mean(sub_v),
                    .min_voltage_v = v_min,
                    .max_voltage_v = v_max,
                    .throughput_ah = std::abs(math_utils::trapz(sub_t, sub_i).value_or(0.0)) / 3600.0
                };
            }
        }
    }

    if (!best_seg) {
        return std::unexpected(common::PipelineError{
            .module = "segmentation",
            .code = "NOT_FOUND",
            .message = segmentationErrorToString(SegmentationError::NO_MATCHING_SEGMENT)
        });
    }

    return *best_seg;
}

common::InternalTimeSeries sliceSegment(
    const common::InternalTimeSeries& series,
    const Segment& segment
) {
    common::InternalTimeSeries sliced;
    sliced.session_id = series.session_id;
    sliced.pack_id = series.pack_id;
    sliced.chemistry = series.chemistry;
    sliced.nominal_capacity_ah = series.nominal_capacity_ah;

    if (segment.start_index <= segment.end_index && segment.end_index < series.samples.size()) {
        sliced.samples.assign(
            series.samples.begin() + segment.start_index,
            series.samples.begin() + segment.end_index + 1
        );
    }

    return sliced;
}

} // namespace ev::battery::segmentation
